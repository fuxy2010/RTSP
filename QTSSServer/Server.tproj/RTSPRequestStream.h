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
    File:       RTSPRequestStream.h

    Contains:   Provides a stream abstraction for RTSP. Given a socket, this object
                can read in data until an entire RTSP request header is available.
                (do this by calling ReadRequest). It handles RTSP pipelining (request
                headers are produced serially even if multiple headers arrive simultaneously),
                & RTSP request data.
                    
*/

#ifndef __RTSPREQUESTSTREAM_H__
#define __RTSPREQUESTSTREAM_H__


//INCLUDES
#include "StrPtrLen.h"
#include "TCPSocket.h"
#include "QTSS.h"
#include "QTSServer.h"//fym

class RTSPRequestStream
{
public:

    //CONSTRUCTOR / DESTRUCTOR
    RTSPRequestStream(TCPSocket* sock) :
	fSocket(sock),
	fRetreatBytes(0), 
	fRetreatBytesRead(0),
	fCurOffset(0),
	fEncodedBytesRemaining(0),
	fRequest(fRequestBuffer, 0),
	fRequestPtr(NULL),
	fDecode(false),
	fPrintRTSP(false)
	{
		_track_id = -1;
		_remain_packet_length = 0;
		_new_packet = false;
		::memset(_incomplete_rtsp_header, 0, sizeof(_incomplete_rtsp_header));
		_incomplete_header_length = 0;//未补齐的TCP数据包头长度

		::memset(_recv_media_buf, 0, sizeof(_recv_media_buf));
		::memset(_media_buf_pos, 0, sizeof(_media_buf_pos));
		::memset(_latest_sequence, 0, sizeof(_latest_sequence));
	}
    
    // We may have to delete this memory if it was allocated due to base64 decoding
    ~RTSPRequestStream() { if (fRequest.Ptr != &fRequestBuffer[0]) delete [] fRequest.Ptr; }

    //ReadRequest
    //This function will not block.
    //Attempts to read data into the stream, stopping when we hit the EOL - EOL that
    //ends an RTSP header.
    //
    //Returns:          QTSS_NoErr:     Out of data, haven't hit EOL - EOL yet
    //                  QTSS_RequestArrived: full request has arrived
    //                  E2BIG: ran out of buffer space
    //                  QTSS_RequestFailed: if the client has disconnected
    //                  EINVAL: if we are base64 decoding and the stream is corrupt
    //                  QTSS_OutOfState: 
    QTSS_Error      ReadRequest();
    
    // Read
    //
    // This function reads data off of the stream, and places it into the buffer provided
    // Returns: QTSS_NoErr, EAGAIN if it will block, or another socket error.
    QTSS_Error      Read(void* ioBuffer, UInt32 inBufLen, UInt32* outLengthRead);
    
    // Use a different TCPSocket to read request data 
    // this will be used by RTSPSessionInterface::SnarfInputSocket
    void                AttachToSocket(TCPSocket* sock) { fSocket = sock; }
    
    // Tell the request stream whether or not to decode from base64.
    void                IsBase64Encoded(Bool16 isDataEncoded) { fDecode = isDataEncoded; }
    
    //GetRequestBuffer
    //This returns a buffer containing the full client request. The length is set to
    //the exact length of the request headers. This will return NULL UNLESS this object
    //is in the proper state (has been initialized, ReadRequest has been called until it returns
        //RequestArrived).
    StrPtrLen*  GetRequestBuffer()  { return fRequestPtr; }
    Bool16      IsDataPacket()      { return fIsDataPacket; }
    void        ShowRTSP(Bool16 enable) {fPrintRTSP = enable; }     
    void SnarfRetreat( RTSPRequestStream &fromRequest );
        
private:

        
    //CONSTANTS:
	//TCP数据包最长为2048字节
    enum
    {
        kRequestBufferSizeInBytes = 4096//2048        //UInt32
    };
    
    // Base64 decodes into fRequest.Ptr, updates fRequest.Len, and returns the amount
    // of data left undecoded in inSrcData
    QTSS_Error              DecodeIncomingData(char* inSrcData, UInt32 inSrcDataLen);

    TCPSocket*              fSocket;
    UInt32                  fRetreatBytes;
	UInt32                  fRetreatBytesRead; // Used by Read() when it is reading RetreatBytes
    
    char                    fRequestBuffer[kRequestBufferSizeInBytes];
    UInt32                  fCurOffset; // tracks how much valid data is in the above buffer
    UInt32                  fEncodedBytesRemaining; // If we are decoding, tracks how many encoded bytes are in the buffer
    
    StrPtrLen               fRequest;
    StrPtrLen*              fRequestPtr;    // pointer to a request header
    Bool16                  fDecode;        // should we base 64 decode?
    Bool16                  fIsDataPacket;  // is this a data packet? Like for a record?//收到的数据是否非RTSP请求而是数据包
    Bool16                  fPrintRTSP;     // debugging printfs

private://fym////////////////////////////////////////////////////////////////////////
	//数据包结构
	//BYTE0: '$'
	//BYTE1: track_id, 0-video, 2-audio, 4-whiteboard
	//BYTE2: 包净荷长度字的高8位
	//BYTE3: 包净荷长度字的低8位
	//12 BYTEs: RTSP Server添加的RTP包头
	//12 BYTEs: 数据输入RTSP Server前添加的RTP包头
	int _track_id;// = -1;//最近接收的数据包头的轨道值
	int _remain_packet_length;// = 0;//最近接收的数据包剩下未复制的净荷长度
	bool _new_packet;// = false;
	char _incomplete_rtsp_header[4];//用于存取残缺的TCP数据包头
	int _incomplete_header_length;// = 0;//未补齐的TCP数据包头长度

	void handle_media_data(char* data, unsigned short data_length)
	{
		if(NULL == data || 1 > data_length)
		{
			qtss_printf("\nerror packet %d", data_length);
			return;
		}
		
		int pos = 0;

		while(pos < data_length)
		{
			//Log.v("RTSP", "process packet 2 " + pos + " " + data_length);
			if(0 == _remain_packet_length && '$' == data[pos])
			{
				if(4 <= data_length - pos)
				{
					//Log.v("RTSP", "process packet 3 " + pos + " " + data_length);
					//收到数据包头
					_track_id = *(unsigned char*)(data + pos + 1);
					_remain_packet_length = 0x100 * (*(unsigned char*)(data + pos + 2)) + (*(unsigned char*)(data + pos + 3));
					//Log.v("RTSP", "1 track " + _track_id + " length " + _remain_packet_length);

					pos += 4;

					_incomplete_header_length = 0;
				}
				else
				{
					//TCP包头不全,先复制一部分
					::memcpy(_incomplete_rtsp_header, data + pos, data_length - pos);
					_incomplete_header_length = 4 - (data_length - pos);
					return;
				}
			}
			else if(-1 == _track_id)
			{
				qtss_printf("\nerror data %d", data_length);
				return;
			}

			//补齐残缺的TCP数据包头
			if(0 < _incomplete_header_length)
			{
				::memcpy(_incomplete_rtsp_header + 4 - _incomplete_header_length, data + pos, _incomplete_header_length);

				_track_id = *(unsigned char*)(_incomplete_rtsp_header + 1);
				_remain_packet_length = 0x100 * (*(unsigned char*)(_incomplete_rtsp_header + 2)) + (*(unsigned char*)(_incomplete_rtsp_header + 3));
				//Log.v("RTSP", "2 track " + _track_id + " length " + _remain_packet_length);

				pos += _incomplete_header_length;

				_incomplete_header_length = 0;
			}

			//复制数据
			//本次复制的长度
			int copy_length = (_remain_packet_length > data_length - pos) ? (data_length - pos) : _remain_packet_length;

			if(false == handle_media_data(data, pos, copy_length, (_track_id / 2)))
			{
				_track_id = -1;
				qtss_printf("\ndata copy fail");
				return;
			}

			pos += copy_length;
		}
	}

	//0-video, 1-audio, 2-whiteboard
	char _recv_media_buf[3][2048];//媒体缓冲区
	int _media_buf_pos[3];// = {0, 0, 0};//媒体缓冲区位置
	unsigned short _latest_sequence[3];//最近收到包的序号

	bool handle_media_data(char* data, int pos, int length, int type)
	{
		if(2 < type || 0 > type)//轨道错误
			return false;

		if(length > sizeof(_recv_media_buf[type]) - _media_buf_pos[type])//缓冲区容纳不下
		{
			return false;
		}

		::memcpy(_recv_media_buf[type] + _media_buf_pos[type], data + pos, length);

		_media_buf_pos[type] += length;    	
		_remain_packet_length -= length;

		if(0 >= _remain_packet_length)
		{
			if(0 == _media_buf_pos[type])
				return false;

			//读取到一个完整的数据包
			//复制到解码缓冲区...
			//qtss_printf("<data %d %d> ", type, _media_buf_pos[type]);

			unsigned short sequence = 0x100 * (*(unsigned char*)(_recv_media_buf[type] + 2)) + (*(unsigned char*)(_recv_media_buf[type] + 3));
			//Log.v("RTSP", "t " + type + " s " + sequence + " " + _media_buf_pos[type]);

			{//检查有无丢包
				if(sequence != _latest_sequence[type] + 1)
				{
					if(!type)
						qtss_printf("\n<VL %d, %d> ", _latest_sequence[type], sequence);
					else
						qtss_printf("\n<AL %d, %d> ", _latest_sequence[type], sequence);
				}

				_latest_sequence[type] = sequence;
			}

			if(0 == type)
			{
				//qtss_printf("<vs %d %d> ", sequence, _media_buf_pos[type]);
				//MediaThreadManager.get_instance().add_video_packetx(_recv_media_buf[type],  _media_buf_pos[type]);
				if(NULL != QTSServer::fDataCallBackFunc)
				{
					(*QTSServer::fDataCallBackFunc)(_recv_media_buf[type], _media_buf_pos[type]);
				}
			}
			else if(1 == type)
			{
				//qtss_printf("<as %d %d> ", sequence, _media_buf_pos[type]);
				//MediaThreadManager.get_instance().add_audio_packet_ex(_recv_media_buf[type],  _media_buf_pos[type]);
				//MediaThreadManager.get_instance().decode_audio_packet(_recv_media_buf[type],  _media_buf_pos[type]);
				if(NULL != QTSServer::fDataCallBackFunc)
				{
					(*QTSServer::fDataCallBackFunc)(_recv_media_buf[type], _media_buf_pos[type]);
				}
			}
			else if(2 == type)
			{
				if(NULL != QTSServer::fDataCallBackFunc)
				{
					(*QTSServer::fDataCallBackFunc)(_recv_media_buf[type], _media_buf_pos[type]);
				}
			}

			_media_buf_pos[type] = 0;
		}

		return true;
	}
    
};

#endif
