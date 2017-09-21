/*
*
* @APPLE_LICENSE_HEADER_START@
*
* Copyright (c) 1999-2008 Apple Inc.  All Rights Reserved.
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
File:		StreamingLoadTool.cpp

Contains:       Tool that simulates streaming movie load
*/

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include<string.h>

#ifndef kVersionString
#include "revision.h"
#endif
#ifndef __Win32__
#include <unistd.h>
#include <netdb.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#endif

#include "ClientSession.h"
//#include "FilePrefsSource.h"
#include "OSMemory.h"
#include "OSArrayObjectDeleter.h"
#include "SocketUtils.h"
//#include "StringFormatter.h"
#include "Socket.h"
//#include "OS.h"
//#include "Task.h"
//#include "TimeoutTask.h"
#include "SVector.h"
#ifndef __MacOSX__
//#include "getopt.h"
#include "revision.h"
#endif

#include "RTSPClientS.h"

#define STREAMINGLOADTOOL_DEBUG 0
#define PACKETADDSIZE  28 // IP headers = 20 + UDP headers = 8

//
// Static data
static UInt32	sConnectionsThatErrored = 0;
static UInt32	sFailedConnections = 0;
static UInt32	sSuccessfulConnections = 0;
static FILE*	sLog = NULL;

static ClientSession** sClientSessionArray = NULL;
static UInt32 sNumClients = 1;
static Bool16 sNumClientsIsSpecified = false;
static Bool16 sGotSigInt = false;
static Bool16 sQuitNow = false;
static SInt64 sSigIntTime = 0;

static UInt64 sTotalBytesReceived = 0;
static UInt64 sTotalPacketsReceived = 0;
static UInt64 sTotalPacketsLost = 0;
static UInt64 sTotalOutOfOrder = 0;
static UInt64 sTotalOutOfBound = 0;
static UInt64 sTotalDuplicates = 0;
static UInt64 sTotalNumAcks = 0;
static UInt64 sTotalMalformed = 0;
static UInt64 sTotalLatePackets;
static UInt64 sTotalBufferOverflowedPackets;
static Bool16 sEnable3GPP = false;


//fym int main(int argc, char *argv[]);

//
// Helper functions
char* 	GetClientTypeDescription(ClientSession::ClientType inClientType);
void	DoDNSLookup(SVector<char *> &theURLlist, SVector<UInt32> &ioIPAddrs);
void	DoSingleDNSLookup(std::string inURL, UInt16& outPort, UInt32& outIPAddr);//fym
void 	RecordClientInfoBeforeDeath(ClientSession* inSession);
char*	GetDeathReasonDescription(UInt32 inDeathReason);
char*	GetPayloadDescription(QTSS_RTPPayloadType inPayload);
//fym void	CheckForStreamingLoadToolDotMov(SVector<UInt32> &ioIPAddrArray, SVector<char *> &theURLlist, UInt16 inPort, SVector<char *> &userList, SVector<char *> &passwordList, UInt32 verboseLevel);
UInt32 CalcStartTime(Bool16 inRandomThumb, UInt32 inMovieLength);

extern char* optarg;

#ifdef _FOR_ACTIVE_X//如ActiveX控件使用该协议栈,需在程序预处理选项中加入宏定义"_FOR_ACTIVE_X"
static void InitRTSPClientLib();//fym
static void DestoryRTSPClientLib();//fym

static unsigned short sRTSPClientNum = 0;
OSMutex sInitRTSPClientMutex;

//CRTSPClient////////////////////////////////////////////////////////////////////////
CRTSPClient::CRTSPClient()
:	_url(""),
_session(NULL),
_data_call_back_func(NULL),
_user_data(0)
{
	//fym if(false == sIsRTSPClientLibInit)
		//fym InitRTSPClientLib();

	//fym
	OSMutexLocker locker(&sInitRTSPClientMutex);
	if(!sRTSPClientNum)
	{
		InitRTSPClientLib();
	}
	++sRTSPClientNum;
}

CRTSPClient::~CRTSPClient()
{
	OSMutexLocker locker(&sInitRTSPClientMutex);//fym

	Disconnect();

	//_url = "";
	//_data_call_back_func = NULL;
	//_user_data = NULL;

	--sRTSPClientNum;
	if(!sRTSPClientNum)
	{
		DestoryRTSPClientLib();
	}
}

int CRTSPClient::Connect(const char* url, int transport_mode)
{
	OSMutexLocker locker(&sInitRTSPClientMutex);

	if(NULL != _session)
	{
		qtss_printf("\nCRTSPClient still running.");//fym

		_session->Signal(Task::kKillEvent);
		_session = NULL;
	}

	_url = url;

	if(14 > _url.length())
		return -1;

	UInt32 _ip_addr = 0;
	UInt16 _port = 0;

	::DoSingleDNSLookup(url, _port, _ip_addr);

	ClientSession::ClientType theClientType;

	switch(transport_mode)
	{
	case CRTSPClient::kByTCP:
		theClientType = ClientSession::kRTSPTCPClientType;
		break;

	case CRTSPClient::kByUDP:
		theClientType = ClientSession::kRTSPUDPClientType;
		break;

	default:
		theClientType = ClientSession::kRTSPUDPClientType;
		break;
	}

	//qtss_printf("\nclient type: %d", transport_mode);
	_session = NEW ClientSession(_ip_addr, _port, (char*)_url.c_str(), theClientType);

	_session->fDataCallBackFunc = _data_call_back_func;
	_session->fUserData = _user_data;

	return 0;
}

int CRTSPClient::Disconnect()
{
	OSMutexLocker locker(&sInitRTSPClientMutex);

	if(NULL == _session)
		return -1;

	if(true == _session->IsPlaying())
	{
		_session->Signal(ClientSession::kTeardownEvent);

		/*UInt16 try_count = 0;
		while(false == _session->IsDone() || 100 < (try_count++))
		{
		OSThread::Sleep(10);
		}*/
		OSThread::Sleep(20);//fym 100);
	}

	_session->Signal(Task::kKillEvent);
	//fym OSThread::Sleep(100);
	_session = NULL;

	return 0;
}

int CRTSPClient::SetDataCallBack(void (CALLBACK* pDataCallBackFunc)(unsigned char* pData, int nLen, int nDataType, unsigned long nSequence, DWORD dwUserData),
								 DWORD dwUserData)
{
	_data_call_back_func = pDataCallBackFunc;
	_user_data = dwUserData;

	if(NULL != _session)
	{
		_session->fDataCallBackFunc = _data_call_back_func;
		_session->fUserData = _user_data;
	}

	return 0;
}

int CRTSPClient::SendDataPacket(const char* data, const unsigned short length, const unsigned char type)
{
	if(NULL == _session)
		return -1;

	if(ClientSession::kPlaying != _session->GetState())
		return -1;

#if 1
	_session->GetSocket()->send_media_data(data, length, type);
#else
	struct iovec iov[2];

	struct RTPInterleaveHeader
	{
		unsigned char header;
		unsigned char channel;
		unsigned short len;
	};
	struct RTPInterleaveHeader  rih;

	// write direct to stream
	rih.header = '$';
	rih.channel = 2 * type;//0-video, 2-audio, 4-whiteboard
	rih.len = htons((unsigned short)length);

	iov[0].iov_base = (char*)&rih;
	iov[0].iov_len = sizeof(rih);

	iov[1].iov_base = const_cast<char*>(data);
	iov[1].iov_len = length;

	//_session->GetClient()->GetSocket()->Send(const_cast<char*>(data), length);
	//_session->GetSocket()->Send(const_cast<char*>(data), length);
	_session->GetSocket()->SendV(iov, 2);
#endif

	return 0;
}

//////////////////////////////////////////////////////////////////////////

void InitRTSPClientLib()
{
	qtss_printf("\nStart Streaming Client.");//fym

	//
	// Start Win32 DLLs
	WORD wsVersion = MAKEWORD(1, 1);
	WSADATA wsData;

	int err = ::WSAStartup(wsVersion, &wsData);

	if(!err)
	{
		//fym sIsRTSPClientLibInit = true;
	}
	else
	{
		qtss_printf("\nWSAStartup error!");//fym
		::WSACleanup();
		return;
	}

	// Final setup before running the show
	OS::Initialize();
	OSThread::Initialize();
	OSMemory::SetMemoryError(ENOMEM);
	Socket::Initialize();
	SocketUtils::Initialize();

#if !MACOSXEVENTQUEUE
	::select_startevents();//initialize the select() implementation of the event queue
#endif

	/*fym UInt32 numThreads = 1;
	if(OS::ThreadSafe())
	{
		numThreads = OS::GetNumProcessors(); // 1 worker thread per processor
	}
	if(!numThreads)
		numThreads = 1;*/
	TaskThreadPool::AddThreads(1);//fym numThreads);

	TimeoutTask::Initialize();
	Socket::StartThread();

}

//fym
void DestoryRTSPClientLib()
{
	qtss_printf("\nDestory Streaming Client.");//fym

	//OSThread::Sleep(1000);

	//fym 针对ActiveX控件重复初始化问题

	TaskThreadPool::RemoveThreads();//for TaskThreadPool::AddThreads(1);

	Socket::StopThread();//for Socket::StartThread() and Socket::Initialize();

	TimeoutTask::Destory();//for TimeoutTask::Initialize();

	OSThread::Destory();

	OS::Cleanup();//for OS::Initialize();

	::WSACleanup();

}
#else//fym _FOR_ACTIVE_X
//fym for CS Client
static void InitRTSPClientLib();//fym
static Bool16 sIsRTSPClientLibInit = false;

//CRTSPClient////////////////////////////////////////////////////////////////////////
CRTSPClient::CRTSPClient()
:	_url(""),
_session(NULL),
_data_call_back_func(NULL),
_user_data(0)
{
	if(false == sIsRTSPClientLibInit)
		InitRTSPClientLib();
}

CRTSPClient::~CRTSPClient()
{
	Disconnect();

	//_url = "";
	//_data_call_back_func = NULL;
	//_user_data = NULL;
}

int CRTSPClient::Connect(const char* url, int transport_mode)
{
	if(NULL != _session)
	{
		qtss_printf("\nCRTSPClient still running.");//fym

		_session->Signal(Task::kKillEvent);
		_session = NULL;
	}

	_url = url;

	if(14 > _url.length())
		return -1;

	UInt32 _ip_addr = 0;
	UInt16 _port = 0;

	::DoSingleDNSLookup(url, _port, _ip_addr);

	ClientSession::ClientType theClientType;

	switch(transport_mode)
	{
	case CRTSPClient::kByTCP:
		theClientType = ClientSession::kRTSPTCPClientType;
		break;

	case CRTSPClient::kByUDP:
		theClientType = ClientSession::kRTSPUDPClientType;
		break;

	default:
		theClientType = ClientSession::kRTSPUDPClientType;
		break;
	}

	//qtss_printf("\nclient type: %d", transport_mode);
	_session = NEW ClientSession(_ip_addr, _port, (char*)_url.c_str(), theClientType);

	_session->fDataCallBackFunc = _data_call_back_func;
	_session->fUserData = _user_data;

	return 0;
}

int CRTSPClient::Disconnect()
{
	if(NULL == _session)
		return -1;

	if(true == _session->IsPlaying())
	{
		_session->Signal(ClientSession::kTeardownEvent);

		/*UInt16 try_count = 0;
		while(false == _session->IsDone() || 100 < (try_count++))
		{
		OSThread::Sleep(10);
		}*/
		OSThread::Sleep(20);//fym 100);
	}

	_session->Signal(Task::kKillEvent);
	//fym OSThread::Sleep(100);
	_session = NULL;

	return 0;
}

int CRTSPClient::SetDataCallBack(void (CALLBACK* pDataCallBackFunc)(unsigned char* pData, int nLen, int nDataType, unsigned long nSequence, DWORD dwUserData),
								 DWORD dwUserData)
{
	_data_call_back_func = pDataCallBackFunc;
	_user_data = dwUserData;

	if(NULL != _session)
	{
		_session->fDataCallBackFunc = _data_call_back_func;
		_session->fUserData = _user_data;
	}

	return 0;
}

//////////////////////////////////////////////////////////////////////////

void InitRTSPClientLib()
{
	qtss_printf("\nStart Streaming Client.");//fym
	//
	// Start Win32 DLLs
	WORD wsVersion = MAKEWORD(1, 1);
	WSADATA wsData;

	int err = ::WSAStartup(wsVersion, &wsData);

	if(!err)
	{
		sIsRTSPClientLibInit = true;
	}
	else
	{
		::WSACleanup();
		return;
	}

	// Final setup before running the show
	OS::Initialize();
	OSThread::Initialize();
	OSMemory::SetMemoryError(ENOMEM);
	Socket::Initialize();
	SocketUtils::Initialize();

#if !MACOSXEVENTQUEUE
	::select_startevents();//initialize the select() implementation of the event queue
#endif

	/*fym UInt32 numThreads = 1;
	if(OS::ThreadSafe())
	{
	numThreads = OS::GetNumProcessors(); // 1 worker thread per processor
	}
	if(!numThreads)
	numThreads = 1;*/
	TaskThreadPool::AddThreads(1);//fym numThreads);


	TimeoutTask::Initialize();
	Socket::StartThread();

}
#endif//fym _FOR_ACTIVE_X

void CALLBACK TestFunc(unsigned char* pData, int nLen, int nDataType, unsigned long nSequence, DWORD dwUserData)
{
	//qtss_printf("<%d %d %d %d> ", nDataType, nSequence, nLen, dwUserData);
	//qtss_printf("<%d %d %d> ", nDataType, nSequence, nLen);
	return;

	typedef struct tagRTPHeader//fym struct RTPHeader
	{
		unsigned char csrccount:4;
		unsigned char extension:1;
		unsigned char padding:1;
		unsigned char version:2;

		unsigned char payloadtype:7;
		unsigned char marker:1;

		unsigned short sequencenumber;
		unsigned long timestamp;
		unsigned long ssrc;
	}
	RTPHeader, *RTPHeaderPtr;//fym

	RTPHeader* header = reinterpret_cast<RTPHeader*>(pData);

	//qtss_printf("<%d %d %d> ", header->payloadtype, header->sequencenumber, header->ssrc);
	//qtss_printf("%d ", header->payloadtype);
	//qtss_printf("<%u %d> ", ntohl(header->ssrc), header->payloadtype);
	//qtss_printf("<%u %d %d> ", ntohs(header->sequencenumber), nLen, header->payloadtype);

	if(2 < header->payloadtype)// && nDataType)
	{
		unsigned char i = header->payloadtype;
		qtss_printf("\n%d %d %d", i, nDataType, nLen);
	}
	qtss_printf(".");
}

unsigned long count = 0;

#if 1//fym
#define CLIENT_NUM	1//200
int main()
{
	typedef struct tagRTPHeader//fym struct RTPHeader
	{
		unsigned char csrccount:4;
		unsigned char extension:1;
		unsigned char padding:1;
		unsigned char version:2;

		unsigned char payloadtype:7;
		unsigned char marker:1;

		unsigned short sequencenumber;
		unsigned long timestamp;
		unsigned long ssrc;
	}
	RTPHeader, *RTPHeaderPtr;//fym

	unsigned short sequence = 0;

#if 0
	UInt16 i = 0;
	char temp[32];

	char content[2000];
	::memset(content, 'A', sizeof(content));
	
	CRTSPClient* c = new CRTSPClient();

	//OSThread::Sleep(1000);

	c->SetDataCallBack(TestFunc, 111);

	while(1)
	{
		qtss_printf("\n*********\n");
		unsigned long count = 100;

		//Connect和Disconnect的调用必须匹配!
		//c->Connect("rtsp://127.0.0.1:1554/11.sdp", CRTSPClient::kByTCP);
		//c->Connect("rtsp://10.10.10.15:1554/11.sdp", CRTSPClient::kByUDP);
		//c->Connect("rtsp://127.0.0.1:8000/1.sdp", CRTSPClient::kByUDP);
		//c->Connect("rtsp://192.168.1.3:554/1.sdp", CRTSPClient::kByTCP);
		//c->Connect("rtsp://192.168.1.3:554/1.sdp", CRTSPClient::kByUDP);
		c->Connect("rtsp://220.249.112.22:2554/1.sdp", CRTSPClient::kByTCP);
		//c->Connect("rtsp://192.168.1.6:1554/0.sdp", CRTSPClient::kByTCP);

		/*while(1)//--count)
		{
			OSThread::Sleep(10);

			//++count;
			//::memcpy(content, &count, sizeof(unsigned long));

			content[2] = sequence >> 8;
			content[3] = 0xff & (sequence);

			qtss_printf("<%d> ", sequence++);

			c->SendDataPacket(content, sizeof(content), CRTSPClient::kVideoStream);
		}*/

		FILE* _video_file = fopen("test.264", "rb");

		while(1)
		{
			//本次应读取的一帧内分包个数
			unsigned char packet_num = 0;
			fread(&packet_num, sizeof(unsigned char), 1, _video_file);

			//从头开始读视频文件
			if(!packet_num)
			{
				fseek(_video_file, 0, SEEK_SET);
				continue;
			}

			unsigned long timestamp = ::timeGetTime();

			for(unsigned char i = 1; i <= packet_num; ++i)
			{
				//本次应读取的视频包长度
				unsigned short packet_len = 0;
				fread(&packet_len, sizeof(unsigned short), 1, _video_file);

				//从头开始读视频文件
				if(!packet_len)
				{
					fseek(_video_file, 0, SEEK_SET);
					continue;
				}

				::memset(content, 0, sizeof(content));

				//读取视频包				
				size_t read_len = fread(content + 12 + 4, 1, packet_len, _video_file);
				//qtss_printf("<%d, %d> ", packet_len, read_len);

				//从头开始读视频文件
				if(packet_len != read_len)
				{
					fseek(_video_file, 0, SEEK_SET);
					continue;
				}

				//qtss_printf("<%d, %d> ", sequence, read_len);

				//加RTP包头
				RTPHeader* header = reinterpret_cast<RTPHeader*>(content + 4);
				header->csrccount = 0;
				header->extension = 0;
				header->padding = 0;
				header->version = 2;
				header->payloadtype = 0;
				header->marker = 1;
				header->sequencenumber = htons(sequence++);;
				header->timestamp = htonl(timestamp);
				header->ssrc = 0;

				//加RTSP包头
				content[0] = '$';
				content[1] = 2 * CRTSPClient::kVideoStream;
				content[2] = (unsigned char)((read_len + 12) >> 8);
				content[3] = (unsigned char)((read_len + 12) & 0xff);

				c->SendDataPacket(content, read_len + 12 + 4, CRTSPClient::kVideoStream);
			}

			Sleep(60);
		}

		c->Disconnect();
	}
#else
	const int NUM = 30;
	CRTSPClient* client[50];
	char temp[64];
	char content[1024];

	::ZeroMemory(client, sizeof(client));

	for(int i = 1; i <= NUM; ++i)
	{
		client[i] = new CRTSPClient();
		client[i]->SetDataCallBack(TestFunc, 111);
	}

	while(1)
	{
		for(int i = 1; i <= NUM; ++i)
		{
			::ZeroMemory(temp, sizeof(temp));
			qtss_sprintf(temp, "rtsp://220.249.112.22:2554/%d.sdp", i);

			client[i]->Connect(temp, CRTSPClient::kByTCP);
		}

		int count = 100;
		int len = 1000;
		while(count--)
		{
			//加RTP包头
			RTPHeader* header = reinterpret_cast<RTPHeader*>(content + 4);
			header->csrccount = 0;
			header->extension = 0;
			header->padding = 0;
			header->version = 2;
			header->payloadtype = 0;
			header->marker = 1;
			header->sequencenumber = htons(sequence++);;
			header->timestamp = htonl(::timeGetTime());
			header->ssrc = 0;

			//加RTSP包头
			content[0] = '$';
			content[1] = 2 * CRTSPClient::kVideoStream;
			content[2] = (unsigned char)((len) >> 8);
			content[3] = (unsigned char)((len) & 0xff);
			
			for(int i = 1; i <= NUM; ++i)
			{
				client[i]->SendDataPacket(content, len + 4, CRTSPClient::kVideoStream);
			}

			Sleep(60);
		}

		for(int i = 1; i <= NUM; ++i)
		{
			client[i]->Disconnect();
		}
	}
#endif
}
#endif

int fym_main()//(int argc, char *argv[])
{
	qtss_printf("\nStart Streaming Client.");//fym
	//
	// Start Win32 DLLs
	WORD wsVersion = MAKEWORD(1, 1);
	WSADATA wsData;
	(void)::WSAStartup(wsVersion, &wsData);

#ifdef __Win32__
	char* configFilePath = "streamingloadtool.cfg";
#else
	char* configFilePath = "streamingloadtool.conf";
#endif
    Bool16 configFilePathIsSpecified = false;
	Bool16 dropPost = false;
	ClientSession::ClientType theClientType = ClientSession::kRTSPUDPClientType;
    Bool16 theClientTypeIsSpecified = false;
	UInt16 thePort = 554;
	Bool16 thePortIsSpecified = false;
	UInt32 theMovieLength = 60;
    Bool16 theMovieLengthIsSpecified = false;
	Bool16 runForever = false;
	UInt32 theHTTPCookie = 1;
	Bool16 shouldLog = false;
	char* logPath = "StreamingClient.log";
	UInt32 proxyIP = 0;
	Bool16 appendJunk = false;
	UInt32 theReadInterval = 50;
	UInt32 sockRcvBuf = 32768;
	Float32 lateTolerance = 0;
	char* rtpMetaInfo = NULL;
	Float32 speed = 1;
	UInt32 verboseLevel = 0;
	char* packetPlayHeader = NULL;
	UInt32 overbufferwindowInK = 0;
	Bool16 randomThumb = false;
	Bool16 sendOptions = false; 
	Bool16 requestRandomData = false; 
	SInt32 randomDataSize = 0;
	UInt32 rtcpInterval = 5000;
	UInt32 bandwidth = 0;
	UInt32 guarenteedBitRate = 0;
	UInt32 maxBitRate = 0;
	UInt32 maxTransferDelay = 0;
	Bool16 enableForcePlayoutDelay = false;
	UInt32 playoutDelay = 0;
    UInt32 bufferSpace = 100000;
    UInt32 delayTime = 10000;
	Float32 startDelayFrac = 0.5;
	//

#if 0//fym
	// Set up our User Agent string for the RTSP client
	char theUserAgentStr[128];
	::sprintf(theUserAgentStr, "StreamingLoadTool-%s",kVersionString);
	RTSPClient::SetUserAgentStr(theUserAgentStr);
#endif

	SVector<char *>userList;
	SVector<char *>passwordList;
	SVector<char *> theURLlist;
	
	theClientType = ClientSession::kRTSPTCPClientType;//kRTSPUDPClientType;//kRTSPTCPClientType;//fym
	//RTSPClient::SetUserAgentStr("FYM");//fym
	//If doing RTSP / HTTP, set droppost to "yes" if you would like StreamingLoadTool
	//to drop the POST half of each RTSP / HTTP connection after sending the
	//PLAY. "yes" best emulates the "real" client behavior.
	dropPost = true;//fym
	sNumClients = 200;//fym
	thePort = 554;//fym
	//StreamingLoadTool should send a TEARDOWN after streaming for this # of seconds
	theMovieLength = 40;//fym
	runForever = true;//fym
	shouldLog = true;//fym
	//Append junk data after each DESCRIBE request
	appendJunk = true;//fym
	proxyIP = 0;//fym
	sockRcvBuf = 32768;//fym
	theHTTPCookie = 1000000;//fym
	theReadInterval = 10;//fym
	lateTolerance = 0;//fym
	speed = 1;//fym
	overbufferwindowInK = 5120;//fym
	randomThumb = false;//fym
	sendOptions = true;//fym

	//fym
	//char* temp = new char[32];
	for(int i = 1; i <= sNumClients; ++i)
	{
		char* temp = new char[32];
		::ZeroMemory(temp, sizeof(temp));
		qtss_sprintf(temp, "rtsp://192.168.101.69/%d.sdp", i);
		//qtss_sprintf(temp, "rtsp://192.168.101.142/%d.sdp", (i % 20));
		theURLlist.push_back(temp);
	}
	//delete temp;
	//temp = NULL;

	//theURLlist.push_back("rtsp://192.168.101.131/h.mp4");
	//theURLlist.push_back("rtsp://192.168.101.69/1.sdp");
   
	//
	// Figure out what type of clients we are going to run
	if ((theClientType == ClientSession::kRTSPHTTPClientType) && dropPost)
		theClientType = ClientSession::kRTSPHTTPDropPostClientType;
		
	// Do IP lookups on all the URLs
    SVector<UInt32> theIPAddrArray;
    theIPAddrArray.resize(theURLlist.size(), 0);
	::DoDNSLookup(theURLlist, theIPAddrArray);


	// Final setup before running the show
	OS::Initialize();
	OSThread::Initialize();
	OSMemory::SetMemoryError(ENOMEM);
	Socket::Initialize();
	SocketUtils::Initialize();

#if !MACOSXEVENTQUEUE
	::select_startevents();//initialize the select() implementation of the event queue
#endif
	TaskThreadPool::AddThreads(1);
	TimeoutTask::Initialize();
	Socket::StartThread();

	if (sGotSigInt)
	{
		//
		// If someone hits Ctrl-C, force all the clients to wrap it up
		printf("Sending TEARDOWNs.\n");
		
		//
		// Tell all the clients to wrap it up.
		for (UInt32 clientCount = 0; clientCount < sNumClients; clientCount++)
		{
			if (sClientSessionArray == NULL)
				continue; //skip over NULL client sessions
			if (sClientSessionArray[clientCount] != NULL)	
				sClientSessionArray[clientCount]->Signal(ClientSession::kTeardownEvent);
		}
		//
		// Wait for the clients to complete
		Bool16 isDone = false;
		while (!isDone && !sQuitNow)
		{
			//wait until all the clients are done doing teardown
			OSThread::Sleep(1000);
			isDone = true;
			for (UInt32 cc2 = 0; cc2 < sNumClients; cc2++)
			{
				if (sClientSessionArray == NULL)
					continue; //skip over NULL client sessions
			
				if (sClientSessionArray[cc2] == NULL)	
					continue;

				if (!sClientSessionArray[cc2]->IsDone())
					isDone = false;
			}
		}	
	}
	
	//
	// We're done... now go through and delete the last sessions(not really)
	for (UInt32 z = 0; z < sNumClients; z++)
	{
		if (sClientSessionArray == NULL)
			continue; //skip over NULL client sessions
				
		if (sClientSessionArray[z] == NULL)	
			continue;
	
		::RecordClientInfoBeforeDeath(sClientSessionArray[z]);
	}
	
	if (sLog != NULL)
		::fclose(sLog);
		
	printf("%5lu %6lu %8lu %6lu %6lu %6lu %9.0fk\n",
		ClientSession:: GetActiveConnections (),
		ClientSession:: GetPlayingConnections (),
		ClientSession:: GetConnectionAttempts (),
		sSuccessfulConnections,
		sConnectionsThatErrored,
		sFailedConnections,
		0.0// depends on 8 second update for bits per second
	);
		
	printf("StreamingLoadTool test complete. Total number of connections: %"_U32BITARG_".\n", ClientSession:: GetConnectionAttempts ());
    printf(
            "Total bytes received: %"_U64BITARG_
            ". Total packets received: %"_U64BITARG_
            ". Total out of order packets: %"_U64BITARG_
            ". Total out of bound packets: %"_U64BITARG_
            ". Total ACKs sent: %"_U64BITARG_
            ". Total malformed packets: %"_U64BITARG_,
            sTotalBytesReceived,
            sTotalPacketsReceived,
            sTotalOutOfOrder,
            sTotalOutOfBound,
            sTotalNumAcks,
            sTotalMalformed
    );
    if (sEnable3GPP)
    {
        printf(
            ". Total 3g packets lost: %"_U64BITARG_
            ". Total 3g duplicate packets: %"_U64BITARG_
            ". Total 3g late packets: %"_U64BITARG_
            ". Total 3g buffer-overflowed packets: %"_U64BITARG_,
            sTotalPacketsLost,
            sTotalDuplicates,
            sTotalLatePackets,
            sTotalBufferOverflowedPackets
    );
	
	
	
	}
	
	printf(".\n");

	return 0;
	
}

UInt32 CalcStartTime(Bool16 inRandomThumb, UInt32 inMovieLength)
{
	UInt32 theMovieLength = inMovieLength;
	if (theMovieLength > 1)
		theMovieLength--;
		
	if (inRandomThumb)
		return ::rand() % theMovieLength;
	else
		return 0;
}

void CheckForStreamingLoadToolPermission(UInt32* inIPAddrArray, UInt32 inNumURLs, UInt16 inPort)
{
	//Eventually check for the existance of a specially formatted sdp file (assuming the server blindly returns sdps)
}

#if 0//fym
//Currently will only authenticate with the FIRST username/password if provided
void	CheckForStreamingLoadToolDotMov(SVector<UInt32> &ioIPAddrArray, SVector<char *> &theURLlist, UInt16 inPort, SVector<char *> &userList, SVector<char *> &passwordList, UInt32 verboseLevel)
{
    Assert(ioIPAddrArray.size() == theURLlist.size());
	printf("Checking for 'streamingloadtool.mov' on the target servers\n");

	OSArrayObjectDeleter<UInt32> holder = NEW UInt32[theURLlist.size() + 1];
	UInt32* uniqueIPAddrs = holder.GetObject();
	::memset(uniqueIPAddrs, 0, sizeof(UInt32) * (theURLlist.size() + 1));
	

	for (UInt32 count = 0; count < theURLlist.size(); count++)
	{
		if (ioIPAddrArray[count] == 0) //skip over one's that failed DNS
			continue;
		
		//check for duplicates
		/*
		Bool16 dup = false;
		for (UInt32 x = 0; uniqueIPAddrs[x] != 0; x++)
		{
			if (uniqueIPAddrs[x] == ioIPAddrArray[count])
			{
				dup = true;
				break;
			}
		}
		if (dup)
			continue;

		// For tracking dups.
		uniqueIPAddrs[count] = ioIPAddrArray[count];
		*/

 		
		// Format the URL: rtsp://xx.xx.xx.xx/streamingloadtool.mov
		char theAddrBuf[50];
		StrPtrLen theAddrBufPtr(theAddrBuf, 50);
		struct in_addr theAddr;
		theAddr.s_addr = htonl(ioIPAddrArray[count]);
		
		SocketUtils::ConvertAddrToString(theAddr, &theAddrBufPtr);

		char theURLBuf[100];
		StringFormatter theFormatter(theURLBuf, 100);
		
		theFormatter.Put("rtsp://");
		theFormatter.Put(theAddrBufPtr);
		theFormatter.Put("/streamingloadtool.mov");
		theFormatter.PutTerminator();

		StrPtrLenDel theURL(theFormatter.GetAsCString());
		
		// Make an RTSP client. We'll send a DESCRIBE to the server to check for this sucker
		TCPClientSocket theSocket = TCPClientSocket(0); //blocking

		// tell the client this is the URL to use
		theSocket.Set(ioIPAddrArray[count], inPort);

		RTSPClient theClient = RTSPClient(&theSocket);
		theClient.SetVerboseLevel(verboseLevel);

		if(userList.size() > 0)
		{
			theClient.SetName(userList.back());
			theClient.SetPassword(passwordList.back());
		}
		theClient.Set(theURL);

		//
		// Send the DESCRIBE! Whoo hoo!
		OS_Error theErr = theClient.SendDescribe();
		
		while (theErr == EINPROGRESS || theErr == EAGAIN)
				theErr = theClient.SendDescribe();
		if (theErr != OS_NoErr)
		{
			printf("##WARNING: Error connecting to %s.\n\n", theURLlist[count]);
			ioIPAddrArray[count] = 0;
			continue;
		}
		
		if (theClient.GetStatus() != 200)
		{
			printf("##WARNING: Cannot access %s\n\n", theURL.Ptr);
			ioIPAddrArray[count] = 0;
		}
		theClient.SendTeardown();
	}

	int addrCount = 0;
	for (UInt32 x = 0; x < theURLlist.size(); x++)
	{
		if ( 0 != ioIPAddrArray[x])
			addrCount++ ;
	}
	if (addrCount == 0)
	{	printf("No valid destinations\n");
		exit (-1);
	}
	printf("Done checking for 'streamingloadtool.mov' on all servers -- %i valid URL's\n", addrCount);	
}
#endif


void	DoDNSLookup(SVector<char *> &theURLlist, SVector<UInt32> &ioIPAddrs)
{
    Assert(theURLlist.size() == ioIPAddrs.size());
    enum { eDNSNameSize = 128 };
    char theDNSName[eDNSNameSize + 1];
	
	for (UInt32 x = 0; x < theURLlist.size(); x++)
	{
		//qtss_printf("DoDNSLookup %d %s, ", x, theURLlist[x]);//fym
		// First extract the DNS name from this URL as a c-string
        StrPtrLen theURL = StrPtrLen(theURLlist[x]);
		StringParser theURLParser(&theURL);
		StrPtrLen theDNSNamePtr;
		
		theURLParser.ConsumeLength(NULL, 7); // skip over rtsp://
		theURLParser.ConsumeUntil(&theDNSNamePtr, '/'); // grab the DNS name
		StringParser theDNSParser(&theDNSNamePtr);
		theDNSParser.ConsumeUntil(&theDNSNamePtr, ':'); // strip off the port number if any
		
			
        if (theDNSNamePtr.Len > eDNSNameSize)
        {
            qtss_printf("DSN Name Failed Lookup.\n", "\n");//fym theDNSNamePtr.PrintStr("DSN Name Failed Lookup.\n", "\n");
            printf("The DNS name is %"_U32BITARG_" in length and is longer than the allowed %d.\n",theDNSNamePtr.Len, eDNSNameSize);
            return;
        }

		theDNSName[0] = 0;
		::memcpy(theDNSName, theDNSNamePtr.Ptr, theDNSNamePtr.Len);
		theDNSName[theDNSNamePtr.Len] = 0;
		
		
		ioIPAddrs[x] = 0;
		
		// Now pass that DNS name into gethostbyname.
		struct hostent* theHostent = ::gethostbyname(theDNSName);
		
		if (theHostent != NULL)
			ioIPAddrs[x] = ntohl(*(UInt32*)(theHostent->h_addr_list[0]));
		else
			ioIPAddrs[x] = SocketUtils::ConvertStringToAddr(theDNSName);
		
		if (ioIPAddrs[x] == 0)
		{
			printf("Couldn't look up host name: %s.\n", theDNSName);
			//exit(-1);
		}
	}
}

void	DoSingleDNSLookup(std::string inURL, UInt16& outPort, UInt32& outIPAddr)//fym
{
	if(14 > inURL.length())//14; rtsp://x/x.sdp
	{
		outIPAddr = 0;
		return;
	}

	//fym get outPort
	outPort = 0;
	char* str = ::strstr((char*)inURL.c_str(), ":");
	if(NULL  == str)
		outPort = 554;
	else
	{
		str = ::strstr(str + 1, ":");
		if(NULL == str)
			outPort = 554;
		else
		{
			char* str1 = ::strstr(str, "/");
			if(NULL == str1)
				outPort = 554;
			else
			{
				char temp[8];
				::ZeroMemory(temp, sizeof(temp));
				memcpy(temp, str +1, str1 - str - 1);
				outPort = ::atoi(temp);
				outPort = (0 > outPort) ? 554 : outPort;
			}
		}
	}

	//fym get outIPAddr
	enum { eDNSNameSize = 128 };
	char theDNSName[eDNSNameSize + 1];

	StrPtrLen theURL = StrPtrLen((char*)inURL.c_str());
	StringParser theURLParser(&theURL);
	StrPtrLen theDNSNamePtr;

	theURLParser.ConsumeLength(NULL, 7); // skip over rtsp://
	theURLParser.ConsumeUntil(&theDNSNamePtr, '/'); // grab the DNS name

	StringParser theDNSParser(&theDNSNamePtr);
	theDNSParser.ConsumeUntil(&theDNSNamePtr, ':'); // strip off the port number if any


	if (theDNSNamePtr.Len > eDNSNameSize)
	{
		qtss_printf("DSN Name Failed Lookup.\n", "\n");//fym theDNSNamePtr.PrintStr("DSN Name Failed Lookup.\n", "\n");
		qtss_printf("The DNS name is %"_U32BITARG_" in length and is longer than the allowed %d.\n",theDNSNamePtr.Len, eDNSNameSize);
		return;
	}

	theDNSName[0] = 0;
	::memcpy(theDNSName, theDNSNamePtr.Ptr, theDNSNamePtr.Len);
	theDNSName[theDNSNamePtr.Len] = 0;


	outIPAddr = 0;

	// Now pass that DNS name into gethostbyname.
	struct hostent* theHostent = ::gethostbyname(theDNSName);

	if (theHostent != NULL)
		outIPAddr = ntohl(*(UInt32*)(theHostent->h_addr_list[0]));
	else
		outIPAddr = SocketUtils::ConvertStringToAddr(theDNSName);

	if(!outIPAddr)
	{
		printf("Couldn't look up host name: %s.\n", theDNSName);
		//exit(-1);
	}
}

char* 	GetClientTypeDescription(ClientSession::ClientType inClientType)
{
	static char* kUDPString = "RTSP/UDP client";
	static char* kTCPString = "RTSP/TCP client";
	static char* kHTTPString = "RTSP/HTTP client";
	static char* kHTTPDropPostString = "RTSP/HTTP drop post client";
	static char* kReliableUDPString = "RTSP/ReliableUDP client";
	
	switch (inClientType)
	{
		case ClientSession::kRTSPUDPClientType:
			return kUDPString;
		case ClientSession::kRTSPTCPClientType:
			return kTCPString;
		case ClientSession::kRTSPHTTPClientType:
			return kHTTPString;
		case ClientSession::kRTSPHTTPDropPostClientType:
			return kHTTPDropPostString;
		case ClientSession::kRTSPReliableUDPClientType:
			return kReliableUDPString;
	}
	Assert(0);
	return NULL;
}

char*	GetDeathReasonDescription(UInt32 inDeathReason)
{
	static char* kDiedNormallyString = "Completed normally";
	static char* kTeardownFailedString = "Failure: Couldn't complete TEARDOWN";
	static char* kRequestFailedString = "Failure: Failed RTSP request";
	static char* kBadSDPString = "Failure: misformatted SDP";
	static char* kSessionTimedoutString = "Failure: Couldn't connect to server(timeout)";
	static char* kConnectionFailedString = "Failure: Server refused connection";
	static char* kDiedWhilePlayingString = "Failure: Disconnected while playing";

	switch (inDeathReason)
	{
		case ClientSession::kDiedNormally:
			return kDiedNormallyString;
		case ClientSession::kTeardownFailed:
			return kTeardownFailedString;
		case ClientSession::kRequestFailed:
			return kRequestFailedString;
		case ClientSession::kBadSDP:
			return kBadSDPString;
		case ClientSession::kSessionTimedout:
			return kSessionTimedoutString;
		case ClientSession::kConnectionFailed:
			return kConnectionFailedString;
		case ClientSession::kDiedWhilePlaying:
			return kDiedWhilePlayingString;
	}
	Assert(0);
	return NULL;
}

char*	GetPayloadDescription(QTSS_RTPPayloadType inPayload)
{
	static char*	kSound = "Sound";
	static char*	kVideo = "Video";
	static char*	kUnknown = "Unknown";

	switch (inPayload)
	{
		case qtssVideoPayloadType:
			return kVideo;
		case qtssAudioPayloadType:
			return kSound;
		default:
			return kUnknown;
	}
	return NULL;
}

void RecordClientInfoBeforeDeath(ClientSession* inSession)
{
	if (inSession->GetReasonForDying() == ClientSession::kRequestFailed)
		sConnectionsThatErrored++;
	else if (inSession->GetReasonForDying() != ClientSession::kDiedNormally)
		sFailedConnections++;
	else
		sSuccessfulConnections++;
				
				
				
				
	
	{
		UInt32 theReason = inSession->GetReasonForDying();
		in_addr theAddr;
		theAddr.s_addr = htonl(inSession->GetSocket()->GetHostAddr());
		char* theAddrStr = ::inet_ntoa(theAddr);
		
		//
		// Write a log entry for this client
		if (sLog != NULL)
		    ::fprintf(sLog, "Client complete. IP addr = %s, URL = %s. Connection status: %s. ",
						theAddrStr,
						inSession->GetClient()->GetURL()->Ptr,
						::GetDeathReasonDescription(theReason));
						
		if (theReason == ClientSession::kRequestFailed)
			if (sLog != NULL) ::fprintf(sLog, "Failed request status: %"_U32BITARG_"", inSession->GetRequestStatus());
		
		if (sLog != NULL) ::fprintf(sLog, "\n");
		
		//
		// If this was a successful connection, log statistics for this connection		
		if ((theReason == ClientSession::kDiedNormally) || (theReason == ClientSession::kTeardownFailed) || (theReason == ClientSession::kSessionTimedout))
		{
		
			UInt32 bytesReceived = 0;
			for (UInt32 trackCount = 0; trackCount < inSession->GetSDPInfo()->GetNumStreams(); trackCount++)
			{
				if (sLog != NULL)
					;/*fym ::fprintf(sLog,
					"Track type: %s. Total packets received: %"_U32BITARG_
					". Total out of order packets: %"_U32BITARG_
					". Total out of bound packets: %"_U32BITARG_
					". Total ACKs sent: %"_U32BITARG_
					". Total malformed packets: %"_U32BITARG_,
					::GetPayloadDescription(inSession->GetTrackType(trackCount)),
					inSession->GetNumPacketsReceived(trackCount),
					inSession->GetNumPacketsOutOfOrder(trackCount),
					inSession->GetNumOutOfBoundPackets(trackCount),
					inSession->GetNumAcks(trackCount),
					inSession->GetNumMalformedPackets(trackCount)
					);*/
					
					
                if (sEnable3GPP)
                {
                    /*fym if (sLog != NULL) ::fprintf(sLog,
                        ". Total 3g packets lost: %"_U32BITARG_
                        ". Total 3g duplicate packets: %"_U32BITARG_
                        ". Total 3g late packets: %"_U32BITARG_
                        ". Total 3g rate adapt buffer-overflowed packets: %"_U32BITARG_,
                        inSession->Get3gNumPacketsLost(trackCount),
                        inSession->Get3gNumDuplicates(trackCount),
                        inSession->Get3gNumLatePackets(trackCount),
                        inSession->Get3gNumBufferOverflowedPackets(trackCount)
                        );*/
                }

                if (sLog != NULL) ::fprintf(sLog,".\n");


                bytesReceived += inSession->GetNumBytesReceived(trackCount);
                bytesReceived += inSession->GetNumPacketsReceived(trackCount) * PACKETADDSIZE;

                sTotalBytesReceived += inSession->GetNumBytesReceived(trackCount);
                sTotalPacketsReceived += inSession->GetNumPacketsReceived(trackCount);
                sTotalOutOfOrder += inSession->GetNumPacketsOutOfOrder(trackCount);
                //fym sTotalOutOfBound += inSession->GetNumOutOfBoundPackets(trackCount);
                sTotalNumAcks += inSession->GetNumAcks(trackCount);
                //fym sTotalMalformed += inSession->GetNumMalformedPackets(trackCount);
                //fym sTotalPacketsLost += inSession->Get3gNumPacketsLost(trackCount);
                //fym sTotalDuplicates += inSession->Get3gNumDuplicates(trackCount);
                //fym sTotalLatePackets += inSession->Get3gNumLatePackets(trackCount);
                //fym sTotalBufferOverflowedPackets += inSession->Get3gNumBufferOverflowedPackets(trackCount);
            }
			UInt32 duration = (UInt32)(inSession->GetTotalPlayTimeInMsec() / 1000);
			Float32 bitRate = (((Float32)bytesReceived) / ((Float32)duration) * 8) / 1024;
						

			if (sLog != NULL) ::fprintf(sLog, "Play duration in sec: %"_U32BITARG_". Total stream bit rate in Kbits / sec: %f.\n", duration, bitRate);
		}
		
		if (sLog != NULL) ::fprintf(sLog, "\n");		
	}
}

