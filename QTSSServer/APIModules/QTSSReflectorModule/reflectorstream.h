/*
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * Copyright (c) 1999-2003 Apple Computer, Inc.  All Rights Reserved.
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 *
 */
/*
    File:       ReflectorStream.h

    Contains:   This object supports reflecting an RTP multicast stream to N
                RTPStreams. It spaces out the packet send times in order to
                maximize the randomness of the sending pattern and smooth
                the stream.
                    
    

*/

#ifndef _REFLECTOR_STREAM_H_
#define _REFLECTOR_STREAM_H_

#include "QTSS.h"

#include "IdleTask.h"
#include "SourceInfo.h"

#include "UDPSocket.h"
#include "UDPSocketPool.h"
#include "UDPDemuxer.h"
#include "EventContext.h"
#include "SequenceNumberMap.h"

#include "OSMutex.h"
#include "OSQueue.h"
#include "OSRef.h"

#include "RTCPSRPacket.h"
#include "ReflectorOutput.h"
#include "atomic.h"

//This will add some printfs that are useful for checking the thinning
#define REFLECTOR_THINNING_DEBUGGING 0 

//Define to use new potential workaround for NAT problems
#define NAT_WORKAROUND 1

class ReflectorPacket;
class ReflectorSender;
class ReflectorStream;
class RTPSessionOutput;

class ReflectorPacket
{
    public:
    
		//fym 数据包将自己作为包队列的第一个成员，其实该队列只有一个成员
        ReflectorPacket() : fQueueElem()
		{
			fQueueElem.SetEnclosingObject(this); this->Reset();
		}//将自己作为数据包队列中的第一个包

        void Reset()    { // make packet ready to reuse fQueueElem is always in use
                            fBucketsSeenThisPacket = 0; 
                            fTimeArrived = 0; 
                            //fQueueElem -- should be set to this
                            fPacketPtr.Set(fPacketData, 0); 
                            fIsRTCP = false;
                            fStreamCountID = 0;
                            fNeededByOutput = false; 
                        }

        ~ReflectorPacket() {}
        
        void    SetPacketData(char *data, UInt32 len)
		{
			Assert(kMaxReflectorPacketSize > len);
			if (len > 0 && kMaxReflectorPacketSize > len)
				memcpy(this->fPacketPtr.Ptr,data,len); this->fPacketPtr.Len = len;
		}
        Bool16  IsRTCP() { return fIsRTCP; }
inline  UInt32  GetPacketRTPTime();
inline  UInt16  GetPacketRTPSeqNum();
inline  UInt32  GetSSRC(Bool16 isRTCP);
inline  SInt64  GetPacketNTPTime();
 
 private: 

        enum
        {
			//fym 转发包最大尺寸
            kMaxReflectorPacketSize = 2060  //jm 5/02 increased from 2048 by 12 bytes for test bytes appended to packets
        };

        UInt32      fBucketsSeenThisPacket;
        SInt64      fTimeArrived;
        OSQueueElem fQueueElem;
        char        fPacketData[kMaxReflectorPacketSize];
        StrPtrLen   fPacketPtr;
        Bool16      fIsRTCP;
        Bool16      fNeededByOutput; // is this packet still needed for output?
        UInt64      fStreamCountID;
                
        friend class ReflectorSender;
        friend class ReflectorSocket;
        friend class RTPSessionOutput;
        
   
};

UInt32  ReflectorPacket::GetSSRC(Bool16 isRTCP) 
{
    if (fPacketPtr.Ptr == NULL || fPacketPtr.Len < 8)
        return 0;       
                
    UInt32* theSsrcPtr = (UInt32*)fPacketPtr.Ptr;
    if (isRTCP)// RTCP 
        return ntohl(theSsrcPtr[1]); 
            
    if (fPacketPtr.Len < 12)
        return 0;
    
    return ntohl(theSsrcPtr[2]);  // RTP SSRC
}

UInt32 ReflectorPacket::GetPacketRTPTime()
{
    
    UInt32 timestamp = 0;
    if (!fIsRTCP)
    {
        //The RTP timestamp number is the second long of the packet
        if (fPacketPtr.Ptr == NULL || fPacketPtr.Len < 8)
            return 0;
        timestamp = ntohl( ((UInt32*)fPacketPtr.Ptr)[1]);
    }
    else
    {
        if (fPacketPtr.Ptr == NULL || fPacketPtr.Len < 20)
            return 0;
        timestamp = ntohl( ((UInt32*)fPacketPtr.Ptr)[4]);
    }
    return timestamp;
}

UInt16 ReflectorPacket::GetPacketRTPSeqNum()
{
    Assert(!fIsRTCP); // not a supported type

   if (fPacketPtr.Ptr == NULL || fPacketPtr.Len < 4 || fIsRTCP)
        return 0;
     
    UInt16 sequence = ntohs( ((UInt16*)fPacketPtr.Ptr)[1]); //The RTP sequenc number is the second short of the packet
    return sequence;
}


SInt64  ReflectorPacket::GetPacketNTPTime()
{
   Assert(fIsRTCP); // not a supported type
   if (fPacketPtr.Ptr == NULL || fPacketPtr.Len < 16 || !fIsRTCP)
       return 0;
 
    UInt32* theReport = (UInt32*)fPacketPtr.Ptr;
    theReport +=2;
    SInt64 ntp = 0;
    ::memcpy(&ntp, theReport, sizeof(SInt64));
    
    return OS::Time1900Fixed64Secs_To_TimeMilli(OS::NetworkToHostSInt64(ntp));


}


//Custom UDP socket classes for doing reflector packet retrieval, socket management
//fym 负责针对某一媒体源的某一发送端口的接受数据的task
class ReflectorSocket : public IdleTask, public UDPSocket
{
    public:

        ReflectorSocket();
        virtual ~ReflectorSocket();
        void    AddBroadcasterSession(QTSS_ClientSessionObject inSession) { OSMutexLocker locker(this->GetDemuxer()->GetMutex()); fBroadcasterClientSession = inSession; }
        void    RemoveBroadcasterSession(QTSS_ClientSessionObject inSession){   OSMutexLocker locker(this->GetDemuxer()->GetMutex()); if (inSession == fBroadcasterClientSession) fBroadcasterClientSession = NULL; }
        void    AddSender(ReflectorSender* inSender);
        void    RemoveSender(ReflectorSender* inStreamElem);
        Bool16  HasSender() { return (this->GetDemuxer()->GetHashTable()->GetNumEntries() > 0); }
        Bool16  ProcessPacket(const SInt64& inMilliseconds,ReflectorPacket* thePacket,UInt32 theRemoteAddr,UInt16 theRemotePort);
        ReflectorPacket*    GetPacket();
        virtual SInt64      Run();
        void    SetSSRCFilter(Bool16 state, UInt32 timeoutSecs) { fFilterSSRCs = state; fTimeoutSecs = timeoutSecs;}
    private:
        
        //virtual SInt64        Run();
        void    GetIncomingData(const SInt64& inMilliseconds);//从该端口接受数据包并处理
        void    FilterInvalidSSRCs(ReflectorPacket* thePacket,Bool16 isRTCP);

        //Number of packets to allocate when the socket is first created
        enum
        {
            kNumPreallocatedPackets = 10,//fym 20,   //UInt32
            kRefreshBroadcastSessionIntervalMilliSecs = 10000,
            kSSRCTimeOut = 30000 // milliseconds before clearing the SSRC if no new ssrcs have come in
        };
        QTSS_ClientSessionObject    fBroadcasterClientSession;
        SInt64                      fLastBroadcasterTimeOutRefresh; 
        // Queue of available ReflectorPackets
        OSQueue fFreeQueue;//fym 暂存从数据源接受，并待转给接收端的数据包,须指出，qtss的做法是先向该队列中插入空数据包ReflectorPacket，再在接受到数据后向队列中的数据包的缓冲区中写数据
       // Queue of senders
        OSQueue fSenderQueue;//fym 存储数据源ReflectorSender的队列，该队列在ReflectorSession设置时即由AddSender生成
        SInt64  fSleepTime;
                
        UInt32  fValidSSRC;
        SInt64  fLastValidSSRCTime;
        Bool16  fFilterSSRCs;
        UInt32  fTimeoutSecs;
        
        Bool16  fHasReceiveTime;
        UInt64  fFirstReceiveTime;
        SInt64  fFirstArrivalTime;
        UInt32  fCurrentSSRC;
		OSMutex fMutex;//fym 删除数据包时的互斥量

};


class ReflectorSocketPool : public UDPSocketPool
{
    public:
    
        ReflectorSocketPool() {}
        virtual ~ReflectorSocketPool() {}
        
        virtual UDPSocketPair*  ConstructUDPSocketPair();
        virtual void            DestructUDPSocketPair(UDPSocketPair *inPair);
};

//fym 转发源
class ReflectorSender : public UDPDemuxerTask
{
    public:
    ReflectorSender(ReflectorStream* inStream, UInt32 inWriteFlag);
    virtual ~ReflectorSender();
        // Queue of senders
    OSQueue fSenderQueue;//fym 该定义完全无用!
    SInt64  fSleepTime;
    
    //Used for adjusting sequence numbers in light of thinning
    UInt16      GetPacketSeqNumber(const StrPtrLen& inPacket);
    void        SetPacketSeqNumber(const StrPtrLen& inPacket, UInt16 inSeqNumber);
    Bool16      PacketShouldBeThinned(QTSS_RTPStreamObject inStream, const StrPtrLen& inPacket);

    //We want to make sure that ReflectPackets only gets invoked when there
    //is actually work to do, because it is an expensive function
    Bool16      ShouldReflectNow(const SInt64& inCurrentTime, SInt64* ioWakeupTime);
    
    //This function gets data from the multicast source and reflects.
    //Returns the time at which it next needs to be invoked
    void        ReflectPackets(SInt64* ioWakeupTime, OSQueue* inFreeQueue);

    //this is the old way of doing reflect packets. It is only here until the relay code can be cleaned up.
    void        ReflectRelayPackets(SInt64* ioWakeupTime, OSQueue* inFreeQueue);//fym 基本无用
    
	//fym 实际发送函数，从fPacketQueue取出数据包发送
    OSQueueElem*    SendPacketsToOutput(ReflectorOutput* theOutput, OSQueueElem* currentPacket, SInt64 currentTime,  SInt64  bucketDelay);

    UInt32      GetOldestPacketRTPTime(Bool16 *foundPtr);          
    UInt16      GetFirstPacketRTPSeqNum(Bool16 *foundPtr);             
    Bool16      GetFirstPacketInfo(UInt16* outSeqNumPtr, UInt32* outRTPTimePtr, SInt64* outArrivalTimePtr);

    OSQueueElem*GetClientBufferNextPacketTime(UInt32 inRTPTime);
    Bool16      GetFirstRTPTimePacket(UInt16* outSeqNumPtr, UInt32* outRTPTimePtr, SInt64* outArrivalTimePtr);

    void        RemoveOldPackets(OSQueue* inFreeQueue);
    OSQueueElem* GetClientBufferStartPacketOffset(SInt64 offsetMsec); 
    OSQueueElem* GetClientBufferStartPacket() { return this->GetClientBufferStartPacketOffset(0); };

    ReflectorStream*    fStream;
    UInt32              fWriteFlag;
    
    OSQueue         fPacketQueue;//fym 待发送的数据包队列
    OSQueueElem*    fFirstNewPacketInQueue;
    OSQueueElem*    fFirstPacketInQueueForNewOutput;
    
    //these serve as an optimization, keeping track of when this
    //sender needs to run so it doesn't run unnecessarily
    Bool16      fHasNewPackets;
    SInt64      fNextTimeToRun;
            
    //how often to send RRs to the source
    enum
    {
        kRRInterval = 5000      //SInt64 (every 5 seconds)
    };

    SInt64      fLastRRTime;
    OSQueueElem fSocketQueueElem;

	OSMutex fMutex;//fym 删除数据包时的互斥量
    
    friend class ReflectorSocket;
    friend class ReflectorStream;
};

/*
0                   1                   2                   3
0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|V=2|P|X|  CC   |M|     PT      |       sequence number         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                           timestamp                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|           synchronization source (SSRC) identifier            |
+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
|            contributing source (CSRC) identifiers             |
|                             ....                              |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
版本(V):2比特   此域定义了RTP的版本.此协议定义的版本是2.(值1被RTP草案版本使用,值0用在最初"vat"语音工具使用的协议中.)     
填料(P):1比特   若填料比特被设置,此包包含一到多个附加在末端的填充比特,不是负载的一部分.填料的最后一个字节包含可以忽略多少个填充比特.
填料可能用于某些具有固定长度的加密算法,或者在底层数据单元中传输多个RTP包.     
扩展(X):1比特   若设置扩展比特,固定头(仅)后面跟随一个头扩展.     
CSRC计数(CC):4比特   CSRC计数包含了跟在固定头后面CSRC识别符的数目.     
标志(M):1比特   标志的解释由具体协议规定.它用来允许在比特流中标记重要的事件,如帧范围.规定该标志在静音后的第一个语音包时置位.     
负载类型(PT):7比特   此域定义了负载的格式,由具体应用决定其解释.此域不用来复用不同的媒体流.     
序列号:16比特   每发送一个RTP数据包,序列号加一,接收机可以据此检测包损和重建包序列.序列号的初始值是随机的(不可预测),
	以使即便在源本身不加密时(有时包要通过翻译器,它会这样做),对加密算法泛知的普通文本攻击也会更加困难.     
时间标志:32比特   时间标志反映了RTP数据包中第一个比特的抽样瞬间.抽样瞬间必须由随时间单调和线形增长的时钟得到,以进行同步和抖动计算.
	 时钟的分辨率必须满足要求的同步准确度,足以进行包到达抖动测量.时钟频率与作为负载传输的数据格式独立,在协议中或定义此格式的负载类型说明中静态定义,
	 也可以在通过非RTP方法定义的负载格式中动态说明.若RTP包周期性生成,可以使用由抽样时钟确定的额定抽样瞬间,而不是读系统时钟.
	 例如,对于固定速率语音,时间标志钟可以每个抽样周期加1.若语音设备从输入设备读取覆盖160个抽样周期的数据块,对于每个这样的数据块,时间标志增加160,无论此块被发送还是被静音压缩.     
	 时间标志的起始值是随机的,如同序列号.多个连续的RTP包可能由同样的时间标志,若他们在逻辑上同时产生.如属于同一个图象帧.若数据没有按照抽样的     
	 顺序发送,连续的RTP包可以包含不单调的时间标志,如MPEG交织图象帧.     
SSRC:32比特   SSRC域用以识别同步源.标识符被随机生成,以使在同一个RTP会话期中没有任何两个同步源有相同的SSRC识别符.
	 尽管多个源选择同一个SSRC识别符的概率很低,所有RTP实现工具都必须准备检测和解决冲突.若一个源改变本身的源传输地址,必须选择新的SSRC识别符,以避免被当作一个环路源.     
CSRC列表:0到15项,每项32比特   CSRC列表识别在此包中负载的有贡献源.识别符的数目在CC域中给定.若有贡献源多于15个,仅识别15个.
	   CSRC识别符由混合器插入,用有贡献源的SSRC识别符.例如语音包,混合产生新包的所有源的SSRC标识符都被陈列,以期在接收机处正确指示交谈者.*/

//fym
typedef struct tagRTPHeaderParam
{
	//1 Byte
	unsigned char CC:4;//CSRC count shoule be 3
	unsigned char X:1;//extensin
	unsigned char P:1;//padding
	unsigned char V:2;//version shoule be 2

	//1 Byte
	unsigned char PT:7;//payload type
	unsigned char M:1;//marker
	//2 Bytes
	unsigned short SEQ_NUM:16;//sequence number

	//4 Bytes
	unsigned long TIME_STAMP:32;//time stamp

	//4 Bytes
	unsigned long SSRC:32;//SSRC
}RTPHeaderParam;

//fym 一个转发源地址和端口对应一个ReflectorStream，ReflectorStream是ReflectorSender的实质内容
class ReflectorStream
{
    public:
    
        enum
        {
            // A ReflectorStream is uniquely identified by the
            // destination IP address & destination port of the broadcast.
            // This ID simply contains that information.
            //
            // A unicast broadcast can also be identified by source IP address. If
            // you are attempting to demux by source IP, this ID will not guarentee
            // uniqueness and special care should be used.
            kStreamIDSize = sizeof(UInt32) + sizeof(UInt16)
        };
        
        // Uses a StreamInfo to generate a unique ID
        static void GenerateSourceID(SourceInfo::StreamInfo* inInfo, char* ioBuffer);
    
        ReflectorStream(SourceInfo::StreamInfo* inInfo);
        ~ReflectorStream();
        
        //
        // SETUP
        //
        // Call Register from the Register role, as this object has some QTSS API
        // attributes to setup
        static void Register();
        static void Initialize(QTSS_ModulePrefsObject inPrefs);
        
        //
        // MODIFIERS
        
        // Call this to initialize the reflector sockets. Uses the QTSS_RTSPRequestObject
        // if provided to report any errors that occur 
        // Passes the QTSS_ClientSessionObject to the socket so the socket can update the session if needed.
        QTSS_Error BindSockets(QTSS_StandardRTSP_Params* inParams, UInt32 inReflectorSessionFlags, Bool16 filterState, UInt32 timeout);
        
        // This stream reflects packets from the broadcast to specific ReflectorOutputs.
        // You attach outputs to ReflectorStreams this way. You can force the ReflectorStream
        // to put this output into a certain bucket by passing in a certain bucket index.
        // Pass in -1 if you don't care. AddOutput returns the bucket index this output was
        // placed into, or -1 on an error.
        
        SInt32  AddOutput(ReflectorOutput* inOutput, SInt32 putInThisBucket);
        
        // Removes the specified output from this ReflectorStream.
        void    RemoveOutput(ReflectorOutput* inOutput); // Removes this output from all tracks
        
        void  TearDownAllOutputs(); // causes a tear down and then a remove

        // If the incoming data is RTSP interleaved, packets for this stream are identified
        // by channel numbers
        void                    SetRTPChannelNum(SInt16 inChannel) { fRTPChannel = inChannel; }
        void                    SetRTCPChannelNum(SInt16 inChannel) { fRTCPChannel = inChannel; }
        void                    PushPacket(char *packet, UInt32 packetLen, Bool16 isRTCP);
		void                    PushRelayPacket(char *packet, UInt32 packetLen, UInt32 src_addr, UInt16 src_port, UInt16 stream_index);//fym
		UInt32					fSequence;//fym 待转发的数据包的序号(一个数据包可能被拆为多个RTP包）
		UInt16					fRTPPacketSeqNum;//fym RTP包的序号
        
        //
        // ACCESSORS
        
        OSRef*                  GetRef()            { return &fRef; }
        UInt32                  GetBitRate()        { return fCurrentBitRate; }
        SourceInfo::StreamInfo* GetStreamInfo()     { return &fStreamInfo; }
        OSMutex*                GetMutex()          { return &fBucketMutex; }
        void*                   GetStreamCookie()   { return this; }
        SInt16                  GetRTPChannel()     { return fRTPChannel; }
        SInt16                  GetRTCPChannel()    { return fRTCPChannel; }
        UDPSocketPair*          GetSocketPair()     { return fSockets;}
        ReflectorSender*        GetRTPSender()      { return &fRTPSender; }
        ReflectorSender*        GetRTCPSender()     { return &fRTCPSender; }
                
        void                    SetHasFirstRTCP(Bool16 hasPacket)       { fHasFirstRTCPPacket = hasPacket; }
        Bool16                  HasFirstRTCP()                          { return fHasFirstRTCPPacket; }
        
        void                    SetFirst_RTCP_RTP_Time(UInt32 time)     { fFirst_RTCP_RTP_Time = time; }
        UInt32                  GetFirst_RTCP_RTP_Time()                { return fFirst_RTCP_RTP_Time; }
        
        void                    SetFirst_RTCP_Arrival_Time(SInt64 time)     { fFirst_RTCP_Arrival_Time = time; }
        SInt64                  GetFirst_RTCP_Arrival_Time()                { return fFirst_RTCP_Arrival_Time; }
        
        
        void                    SetHasFirstRTP(Bool16 hasPacket)        { fHasFirstRTPPacket = hasPacket; }
        Bool16                  HasFirstRTP()                           { return fHasFirstRTPPacket; }
                
        UInt32                  GetBufferDelay()                        { return ReflectorStream::sOverBufferInMsec; }
        UInt32                  GetTimeScale()                          { return fStreamInfo.fTimeScale; }
        UInt64                  fPacketCount;

        void                    SetEnableBuffer(Bool16 enableBuffer)    { fEnableBuffer = enableBuffer; }
        Bool16                  BufferEnabled()                         { return fEnableBuffer; }
inline  void                    UpdateBitRate(SInt64 currentTime);
        static UInt32           sOverBufferInMsec;
        
        void                    IncEyeCount()                           { OSMutexLocker locker(&fBucketMutex); fEyeCount ++; }
        void                    DecEyeCount()                           { OSMutexLocker locker(&fBucketMutex); fEyeCount --; }
        UInt32                  GetEyeCount()                           { OSMutexLocker locker(&fBucketMutex); return fEyeCount; }

	public:
		static UInt16 fRTPPayloadSize;//fym 暂定1400

    private:
    
         //Sends an RTCP receiver report to the broadcast source
        void    SendReceiverReport();
        void    AllocateBucketArray(UInt32 inNumBuckets);
        SInt32  FindBucket();
        // Unique ID & OSRef. ReflectorStreams can be mapped & shared
        OSRef               fRef;
        char                fSourceIDBuf[kStreamIDSize];
        
        // Reflector sockets, retrieved from the socket pool
        UDPSocketPair*      fSockets;

        ReflectorSender     fRTPSender;
        ReflectorSender     fRTCPSender;
        SequenceNumberMap   fSequenceNumberMap; //for removing duplicate packets

		char fRTPPacket[1600];//fym 将输入数据切分后存放在此,需确保缓冲尺寸大于fRTPPayloadSize + (8 * sizeof(RTPHeaderParam)))
        
        // All the necessary info about this stream
        SourceInfo::StreamInfo  fStreamInfo;
        
        enum
        {
            kReceiverReportSize = 16,               //UInt32
            kAppSize = 36,                          //UInt32
            kMinNumBuckets = 16,                    //UInt32
            kBitRateAvgIntervalInMilSecs = 30000 // time between bitrate averages
        };
    
        // BUCKET ARRAY
        
        //ReflectorOutputs are kept in a 2-dimensional array, "Buckets"
        typedef ReflectorOutput** Bucket;
        Bucket*     fOutputArray;//fym 发包目的地数组

        UInt32      fNumBuckets;        //Number of buckets currently
        UInt32      fNumElements;       //Number of reflector outputs in the array
        
        //Bucket array can't be modified while we are sending packets.
        OSMutex     fBucketMutex;
        
        // RTCP RR information
        
        char        fReceiverReportBuffer[kReceiverReportSize + kAppSize +
                                        RTCPSRPacket::kMaxCNameLen];
        UInt32*     fEyeLocation;//place in the buffer to write the eye information
        UInt32      fReceiverReportSize;
        
        // This is the destination address & port for RTCP
        // receiver reports.
        UInt32      fDestRTCPAddr;
        UInt16      fDestRTCPPort;
    
        // Used for calculating average bit rate
        UInt32              fCurrentBitRate;
        SInt64              fLastBitRateSample;
        unsigned int        fBytesSentInThisInterval;// unsigned long because we need to atomic_add it

        // If incoming data is RTSP interleaved
        SInt16              fRTPChannel; //These will be -1 if not set to anything
        SInt16              fRTCPChannel;
        
        Bool16              fHasFirstRTCPPacket;
        Bool16              fHasFirstRTPPacket;
        
        Bool16              fEnableBuffer;
        UInt32              fEyeCount;
        
        UInt32              fFirst_RTCP_RTP_Time;
        SInt64              fFirst_RTCP_Arrival_Time;
    
        static UInt32       sBucketSize;
        static UInt32       sMaxPacketAgeMSec;
        static UInt32       sMaxFuturePacketSec;
        
        static UInt32       sMaxFuturePacketMSec;
        static UInt32       sOverBufferInSec;
        static UInt32       sBucketDelayInMsec;
        static Bool16       sUsePacketReceiveTime;
        static UInt32       sFirstPacketOffsetMsec;
        
        friend class ReflectorSocket;
        friend class ReflectorSender;
};


void    ReflectorStream::UpdateBitRate(SInt64 currentTime)
{
    if ((fLastBitRateSample + ReflectorStream::kBitRateAvgIntervalInMilSecs) < currentTime)
    {
        unsigned int intervalBytes = fBytesSentInThisInterval;
        (void)atomic_sub(&fBytesSentInThisInterval, intervalBytes);
        
        // Multiply by 1000 to convert from milliseconds to seconds, and by 8 to convert from bytes to bits
        Float32 bps = (Float32)(intervalBytes * 8) / (Float32)(currentTime - fLastBitRateSample);
        bps *= 1000;
        fCurrentBitRate = (UInt32)bps;
        
        // Don't check again for awhile!
        fLastBitRateSample = currentTime;
    }
}
#endif //_REFLECTOR_SESSION_H_

