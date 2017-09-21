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
    File:       RTSPRequestStream.cpp

    Contains:   Implementation of RTSPRequestStream class. 
                    
    

*/


#include "RTSPRequestStream.h"
#include "StringParser.h"
#include "OSMemory.h"
#include "base64.h"
#include "OSArrayObjectDeleter.h"
#include "OS.h"

#include <errno.h>

#define READ_DEBUGGING 0

/*RTSPRequestStream::RTSPRequestStream(TCPSocket* sock)
:   fSocket(sock),
    fRetreatBytes(0), 
    fRetreatBytesRead(0),
    fCurOffset(0),
    fEncodedBytesRemaining(0),
    fRequest(fRequestBuffer, 0),
    fRequestPtr(NULL),
    fDecode(false),
    fPrintRTSP(false)
{}*/

void RTSPRequestStream::SnarfRetreat( RTSPRequestStream &fromRequest )
{
    // Simplest thing to do is to just completely blow away everything in this current
    // stream, and replace it with the retreat bytes from the other stream.
    fRequestPtr = NULL;
    Assert(fRetreatBytes < kRequestBufferSizeInBytes);
    fRetreatBytes = fromRequest.fRetreatBytes;
    fEncodedBytesRemaining = fCurOffset = fRequest.Len = 0;
    ::memcpy(&fRequestBuffer[0], fromRequest.fRequest.Ptr + fromRequest.fRequest.Len, fromRequest.fRetreatBytes);
}

//读取RTSP请求及非RTSP请求数据
unsigned long timestamp = timeGetTime();
QTSS_Error RTSPRequestStream::ReadRequest()
{
	//qtss_printf("\nRTSPRequestStream::ReadRequest 1");//fym

    while (true)
    {
		//qtss_printf("\nRTSPRequestStream::ReadRequest 2");//fym
		//qtss_printf("\nRTSPRequestStream::ReadRequest %s", fRequest.Ptr);//fym

        UInt32 newOffset = 0;

		//fRequestPtr指向一个完整的RTSP请求字符串
        
        //If this is the case, we already HAVE a request on this session, and we now are done
        //with the request and want to move onto the next one. The first thing we should do
        //is check whether there is any lingering data in the stream. If there is, the parent
        //session believes that is part of a new request
		//如fRequestPtr不为NULL则会话中仍然还有请求字符串，必须首先处理此请求
		//首先，必须检查socket缓冲区中是否还有残余数据,如有则会话认为其是一个新请求的一部分
        if(fRequestPtr != NULL)
        {
            fRequestPtr = NULL;//flag that we no longer have a complete request
            
            // Take all the retreated leftover data and move it to the beginning of the buffer
            if((fRetreatBytes > 0) && (fRequest.Len > 0))
                ::memmove(fRequest.Ptr, fRequest.Ptr + fRequest.Len + fRetreatBytesRead, fRetreatBytes);

            // if we are decoding, we need to also move over the remaining encoded bytes
            // to the right position in the fRequestBuffer
            if(fEncodedBytesRemaining > 0)
            {
                //Assert(fEncodedBytesRemaining < 4);
                
                // The right position is at fRetreatBytes offset in the request buffer. The reason for this is:
                //  1) We need to find a place in the request buffer where we know we have enough space to store
                //  fEncodedBytesRemaining. fRetreatBytes + fEncodedBytesRemaining will always be less than
                //  kRequestBufferSize because all this data must have been in the same request buffer, together, at one point.
                //
                //  2) We need to make sure that there is always more data in the RequestBuffer than in the decoded
                //  request buffer, otherwise we could overrun the decoded request buffer (we bounds check on the encoded
                //  buffer, not the decoded buffer). Leaving fRetreatBytes as empty space in the request buffer ensures
                //  that this principle is maintained. 
                ::memmove(&fRequestBuffer[fRetreatBytes], &fRequestBuffer[fCurOffset - fEncodedBytesRemaining], fEncodedBytesRemaining);
                fCurOffset = fRetreatBytes + fEncodedBytesRemaining;
                Assert(fCurOffset < kRequestBufferSizeInBytes);
            }
            else
                fCurOffset = fRetreatBytes;
                
            newOffset = fRequest.Len = fRetreatBytes;
            fRetreatBytes = fRetreatBytesRead = 0;
        }

        // We don't have any new data, so try and get some
        if(newOffset == 0)
        {
            if(fRetreatBytes > 0)
            {
                // This will be true if we've just snarfed another input stream, in which case the encoded data
                // is copied into our request buffer, and its length is tracked in fRetreatBytes.
                // If this is true, just fall through and decode the data.
                newOffset = fRetreatBytes;
                fRetreatBytes = 0;
                Assert(fEncodedBytesRemaining == 0);
            }
            else
            {
                // We don't have any new data, get some from the socket...
                QTSS_Error sockErr = fSocket->Read(&fRequestBuffer[fCurOffset], (kRequestBufferSizeInBytes - fCurOffset) - 1, &newOffset);
				
				//qtss_printf("\n%d ====== %s", newOffset, &fRequestBuffer[fCurOffset]);
				
				if(newOffset)
				{
					//qtss_printf("<%d> ", newOffset);

					bool is_rtsp_signal = false;//是否为RTSP信令

					//加快搜索速度
#if 1
					char* t = fRequestBuffer;
					if(!fCurOffset)
					{
						char* t = &fRequestBuffer[fCurOffset];
						if(('D' == t[0] && 'E' == t[1] && 'S' == t[2] && 'C' == t[3] && 'R' == t[4] && 'I' == t[5] && 'B' == t[6] && 'E' == t[7])//DESCRIBE
						|| ('S' == t[0] && 'E' == t[1] && 'T' == t[2] && 'U' == t[3] && 'P' == t[4])//SETUP
						|| ('P' == t[0] && 'L' == t[1] && 'A' == t[2] && 'Y' == t[3])//PLAY
						|| ('T' == t[0] && 'E' == t[1] && 'A' == t[2] && 'R' == t[3] && 'D' == t[4] && 'O' == t[5] && 'W' == t[6] && 'N' == t[7])//TEARDOWN
						|| ('P' == t[0] && 'A' == t[1] && 'U' == t[2] && 'S' == t[3] && 'E' == t[4])//PAUSE
						|| ('O' == t[0] && 'P' == t[1] && 'T' == t[2] && 'I' == t[3] && 'O' == t[4] && 'N' == t[5] && 'S' == t[6]))//OPTIONS
						//|| ('D' == t[0] && 'E' == t[1] && 'S' == t[2] && 'C' == t[3] && 'R' == t[4] && 'I' == t[5] && 'B' == t[6] && 'E' == t[7])//ANNOUNCE
						//|| ('D' == t[0] && 'E' == t[1] && 'S' == t[2] && 'C' == t[3] && 'R' == t[4] && 'I' == t[5] && 'B' == t[6] && 'E' == t[7])//GET_PARAMETER
						//|| ('D' == t[0] && 'E' == t[1] && 'S' == t[2] && 'C' == t[3] && 'R' == t[4] && 'I' == t[5] && 'B' == t[6] && 'E' == t[7])//SET_PARAMETER
						//|| ('D' == t[0] && 'E' == t[1] && 'S' == t[2] && 'C' == t[3] && 'R' == t[4] && 'I' == t[5] && 'B' == t[6] && 'E' == t[7])//REDIRECT
						//|| ('D' == t[0] && 'E' == t[1] && 'S' == t[2] && 'C' == t[3] && 'R' == t[4] && 'I' == t[5] && 'B' == t[6] && 'E' == t[7]))//RECORD
						{
							is_rtsp_signal = true;
						}
					}
#else
					char temp[16];
					::memcpy(temp, &fRequestBuffer[fCurOffset], sizeof(temp));					
					temp[15] = 0;

					if(NULL != ::strstr(temp, "DESCRIBE")
						|| NULL != ::strstr(temp, "SETUP")
						|| NULL != ::strstr(temp, "PLAY")
						|| NULL != ::strstr(temp, "TEARDOWN")
						|| NULL != ::strstr(temp, "PAUSE")
						|| NULL != ::strstr(temp, "OPTIONS"))
						//|| NULL == ::strstr(temp, "ANNOUNCE")
						//|| NULL == ::strstr(temp, "GET_PARAMETER")
						//|| NULL == ::strstr(temp, "SET_PARAMETER")
						//|| NULL == ::strstr(temp, "REDIRECT")
						//|| NULL == ::strstr(temp, "RECORD"))
					{
						is_rtsp_signal = true;
					}
#endif
					
					//非RTSP信令数据
					if(false == is_rtsp_signal)
					{
						handle_media_data(&fRequestBuffer[fCurOffset], newOffset);

						fIsDataPacket = true;
						fRequestPtr = &fRequest;
						return QTSS_RequestArrived;
					}
				}

				//qtss_printf("\nReadRequest %s", &fRequestBuffer[fCurOffset]);//fym
                //assume the client is dead if we get an error back
                if(sockErr == EAGAIN)
                    return QTSS_NoErr;
                if(sockErr != QTSS_NoErr)
                {
                    Assert(!fSocket->IsConnected());
                    return sockErr;
                }
            }   

            if(fDecode)
            {
                // If we need to decode this data, do it now.
                Assert(fCurOffset >= fEncodedBytesRemaining);
                QTSS_Error decodeErr = this->DecodeIncomingData(&fRequestBuffer[fCurOffset - fEncodedBytesRemaining],
                                                                    newOffset + fEncodedBytesRemaining);
                // If the above function returns an error, it is because we've
                // encountered some non-base64 data in the stream. We can process
                // everything up until that point, but all data after this point will
                // be ignored.
                if(decodeErr == QTSS_NoErr)
                    Assert(fEncodedBytesRemaining < 4);
            }
            else
                fRequest.Len += newOffset;
            Assert(fRequest.Len < kRequestBufferSizeInBytes);
            fCurOffset += newOffset;
        }
        Assert(newOffset > 0);

        // See if this is an interleaved data packet
        if('$' == *(fRequest.Ptr))
        {
			if(fRequest.Len < 4)
			{
				//fym qtss_printf("!");//fym
                continue;
			}

            UInt16* dataLenP = (UInt16*)fRequest.Ptr;
            UInt32 interleavedPacketLen = ntohs(dataLenP[1]) + 4;

            if(interleavedPacketLen > fRequest.Len)
			{
				//fym qtss_printf("?");//fym
                continue;
			}

            //put back any data that is not part of the header
            fRetreatBytes += fRequest.Len - interleavedPacketLen;
            fRequest.Len = interleavedPacketLen;
        
            fRequestPtr = &fRequest;
            fIsDataPacket = true;
            return QTSS_RequestArrived;
        }
        fIsDataPacket = false;

        if(fPrintRTSP)
        {
            DateBuffer theDate;
            DateTranslator::UpdateDateBuffer(&theDate, 0); // get the current GMT date and time
			qtss_printf("\n\n#C->S:\n#time: ms=%lu date=%s\n", (UInt32) OS::StartTimeMilli_Int(), theDate.GetDateBuffer());

            if(fSocket != NULL)    
            {
                UInt16 serverPort = fSocket->GetLocalPort();
                UInt16 clientPort = fSocket->GetRemotePort();    
                StrPtrLen* theLocalAddrStr = fSocket->GetLocalAddrStr();
                StrPtrLen* theRemoteAddrStr = fSocket->GetRemoteAddrStr();
                if(theLocalAddrStr != NULL)
                {	qtss_printf("#server: ip="); theLocalAddrStr->PrintStr(); qtss_printf(" port=%u\n" , serverPort );
                }
                else
              	{	qtss_printf("#server: ip=NULL port=%u\n" , serverPort );
              	}
               	
                if(theRemoteAddrStr != NULL)
                {	qtss_printf("#client: ip="); theRemoteAddrStr->PrintStr(); qtss_printf(" port=%u\n" , clientPort );
                }
            	else
            	{	qtss_printf("#client: ip=NULL port=%u\n" , clientPort );
            	}

            }

			StrPtrLen str(fRequest);
			str.PrintStrEOL("\n\r\n", "\n");// print the request but stop on \n\r\n and add a \n afterwards.
        }
        
        //use a StringParser object to search for a double EOL, which signifies the end of
        //the header.
        Bool16 weAreDone = false;
        StringParser headerParser(&fRequest);
        
        UInt16 lcount = 0;
        while (headerParser.GetThruEOL(NULL))
        {
            lcount++;
            if(headerParser.ExpectEOL())
            {
                //The legal end-of-header sequences are \r\r, \r\n\r\n, & \n\n. NOT \r\n\r!
                //If the packets arrive just a certain way, we could get here with the latter
                //combo, and not wait for a final \n.
                if((headerParser.GetDataParsedLen() > 2) &&
                    (memcmp(headerParser.GetCurrentPosition() - 3, "\r\n\r", 3) == 0))
                    continue;
                weAreDone = true;
                break;
            }
            else if(lcount == 1) {
                // if this request is actually a ShoutCast password it will be 
                // in the form of "xxxxxx\r" where "xxxxx" is the password.
                // If we get a 1st request line ending in \r with no blanks we will
                // assume that this is the end of the request.
                UInt16 flag = 0;
                UInt16 i = 0;
                for (i=0; i<fRequest.Len; i++)
                {
                    if(fRequest.Ptr[i] == ' ')
                        flag++;
                }
                if(flag == 0)
                {
                    weAreDone = true;
                    break;
                }
            }
        }
        
        //weAreDone means we have gotten a full request
        if(weAreDone)
        {
            //put back any data that is not part of the header
            fRequest.Len -= headerParser.GetDataRemaining();
            fRetreatBytes += headerParser.GetDataRemaining();
            
            fRequestPtr = &fRequest;
            return QTSS_RequestArrived;
        }
        
        //check for a full buffer
        if(fCurOffset == kRequestBufferSizeInBytes - 1)
        {
            fRequestPtr = &fRequest;
            return E2BIG;
        }
    }
}

QTSS_Error RTSPRequestStream::Read(void* ioBuffer, UInt32 inBufLen, UInt32* outLengthRead)
{
    UInt32 theLengthRead = 0;
    UInt8* theIoBuffer = (UInt8*)ioBuffer;
    
    //
    // If there are retreat bytes available, read them first.
    if(fRetreatBytes > 0)
    {
        theLengthRead = fRetreatBytes;
        if(inBufLen < theLengthRead)
            theLengthRead = inBufLen;
        
        ::memcpy(theIoBuffer, fRequest.Ptr + fRequest.Len + fRetreatBytesRead, theLengthRead);
        
        //
        // We should not update fRequest.Len even though we've read some of the retreat bytes.
        // fRequest.Len always refers to the length of the request header. Instead, we
        // have a separate variable, fRetreatBytesRead
        fRetreatBytes -= theLengthRead;
        fRetreatBytesRead += theLengthRead;
#if READ_DEBUGGING
        qtss_printf("In RTSPRequestStream::Read: Got %d Retreat Bytes\n",theLengthRead);
#endif  
    }

    //
    // If there is still space available in ioBuffer, continue. Otherwise, we can return now
    if(theLengthRead == inBufLen)
    {
        if(outLengthRead != NULL)
            *outLengthRead = theLengthRead;
        return QTSS_NoErr;
    }
    
    //
    // Read data directly from the socket and place it in our buffer
    UInt32 theNewOffset = 0;
    QTSS_Error theErr = fSocket->Read(&theIoBuffer[theLengthRead], inBufLen - theLengthRead, &theNewOffset);
#if READ_DEBUGGING
    qtss_printf("In RTSPRequestStream::Read: Got %d bytes off Socket\n",theNewOffset);
#endif  
    if(outLengthRead != NULL)
        *outLengthRead = theNewOffset + theLengthRead;

	qtss_printf("\nRequest: %s", ioBuffer);
        
    return theErr;
}

QTSS_Error RTSPRequestStream::DecodeIncomingData(char* inSrcData, UInt32 inSrcDataLen)
{
    Assert(fRetreatBytes == 0);
    
    if(fRequest.Ptr == &fRequestBuffer[0])
    {
        fRequest.Ptr = NEW char[kRequestBufferSizeInBytes];
        fRequest.Len = 0;
    }
    
    // We always decode up through the last chunk of 4.
    fEncodedBytesRemaining = inSrcDataLen & 3;
    
    // Let our friendly Base64Decode function know this by NULL terminating at that point
    UInt32 bytesToDecode = inSrcDataLen - fEncodedBytesRemaining;
    char endChar = inSrcData[bytesToDecode];
    inSrcData[bytesToDecode] = '\0';
    
    UInt32 encodedBytesConsumed = 0;
    
    // Loop until the whole load is decoded
    while (encodedBytesConsumed < bytesToDecode)
    {
        Assert((encodedBytesConsumed & 3) == 0);
        Assert((bytesToDecode & 3) == 0);

        UInt32 bytesDecoded = Base64decode(fRequest.Ptr + fRequest.Len, inSrcData + encodedBytesConsumed);

        // If bytesDecoded is 0, we will end up being in an endless loop. The
        // base64 must be corrupt, so let's just return an error and abort
        if(bytesDecoded == 0)
        {
            //Assert(0);
            return QTSS_BadArgument;
        }
        
        fRequest.Len += bytesDecoded;

        // Assuming the stream is valid, the # of encoded bytes we just consumed is
        // 4/3rds of the number of decoded bytes returned by the decode function,
        // rounded up to the nearest multiple of 4.
        encodedBytesConsumed += (bytesDecoded / 3) * 4;
        if((bytesDecoded % 3) > 0)
            encodedBytesConsumed += 4;
        
    }
    
    // Make sure to replace the sacred endChar
    inSrcData[bytesToDecode] = endChar;

    Assert(fRequest.Len < kRequestBufferSizeInBytes);
    Assert(encodedBytesConsumed == bytesToDecode);
    
    return QTSS_NoErr;
}

