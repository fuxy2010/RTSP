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
    File:       FakeRTSPClient.cpp

    Contains:   .  
                    
    
*/

#ifndef __Win32__
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/uio.h>
#include <unistd.h>

#endif


#include "FakeRTSPClient.h"
#include "StringParser.h"
#include "OSMemory.h"
#include "OSHeaders.h"

#include <errno.h>

#define ENABLE_AUTHENTICATION 1

// STRTOCHAR is used in verbose mode and for debugging
static char temp[2048]; 
static char * STRTOCHAR(StrPtrLen *theStr)
{
    temp[0] = 0;
    UInt32 len = theStr->Len < 2047 ? theStr->Len : 2047;
    if(theStr->Len > 0 || NULL != theStr->Ptr)
    {   memcpy(temp,theStr->Ptr,len); 
        temp[len] = 0;
    }
    else 
        strcpy(temp,"Empty Ptr or len is 0");
    return temp;
}

//======== includes for authentication ======
#include "base64.h"
#include "md5digest.h"
#include "OS.h"
//===========================================
static UInt8 sWhiteQuoteOrEOLorEqual[] =
{
    0, 0, 0, 0, 0, 0, 0, 0, 0, 1, //0-9     // \t is a stop
    1, 0, 0, 1, 0, 0, 0, 0, 0, 0, //10-19    //'\r' & '\n' are stop conditions
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //20-29
    0, 0, 1, 0, 1, 0, 0, 0, 0, 0, //30-39   ' ' , '"' is a stop
    0, 0, 0, 0, 1, 0, 0, 0, 0, 0, //40-49   ',' is a stop
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //50-59
    0, 1, 0, 0, 0, 0, 0, 0, 0, 0, //60-69  '=' is a stop
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //70-79
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //80-89
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //90-99
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //100-109
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //110-119
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //120-129
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //130-139
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //140-149
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //150-159
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //160-169
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //170-179
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //180-189
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //190-199
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //200-209
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //210-219
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //220-229
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //230-239
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //240-249
    0, 0, 0, 0, 0, 0             //250-255
};
static UInt8 sNOTWhiteQuoteOrEOLorEqual[] = // don't stop
{
    1, 1, 1, 1, 1, 1, 1, 1, 1, 0, //0-9      //  on '\t'
    0, 1, 1, 0, 1, 1, 1, 1, 1, 1, //10-19    // '\r', '\n'
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, //20-29
    1, 1, 0, 1, 0, 1, 1, 1, 1, 1, //30-39   //  ' '
    1, 1, 1, 1, 0, 1, 1, 1, 1, 1, //40-49   // ','
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, //50-59
    1, 0, 1, 1, 1, 1, 1, 1, 1, 1, //60-69   //  '='
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, //70-79
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, //80-89
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, //90-99
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, //100-109
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, //110-119
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, //120-129
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, //130-139
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, //140-149
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, //150-159
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, //160-169
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, //170-179
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, //180-189
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, //190-199
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, //200-209
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, //210-219
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, //220-229
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, //230-239
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, //240-249
    1, 1, 1, 1, 1, 1             //250-255
};

static char* sEmptyString = "";
//fym char* FakeRTSPClient::sUserAgent = "None";
char* FakeRTSPClient::sControlID = "trackID";

FakeRTSPClient::InterleavedParams FakeRTSPClient::sInterleavedParams;

FakeRTSPClient::FakeRTSPClient(ClientSocket* inSocket, Bool16 verbosePrinting, char* inUserAgent)
:   fSocket(inSocket),
    fVerbose(verbosePrinting),
    fCSeq(1),
    fStatus(0),
    fSessionID(sEmptyString),
    fServerPort(0),
    fContentLength(0),
    fSetupHeaders(NULL),
    fNumChannelElements(kMinNumChannelElements),
    fNumSSRCElements(0),
    fSSRCMapSize(kMinNumChannelElements),
    fNumFieldIDElements(0),
    fFieldIDMapSize(kMinNumChannelElements),
    fPacketBuffer(NULL),
    fPacketBufferOffset(0),
    fPacketOutstanding(false),
    fRecvContentBuffer(NULL),
    fSendBufferLen(0),
    fContentRecvLen(0),
    fHeaderRecvLen(0),
    fHeaderLen(0),
    fSetupTrackID(0),
    fTransactionStarted(false),
    fReceiveInProgress(false),
    fReceivedResponse(false),
    fConnected(false),
    fResponseCount(0),
    fTransportMode(kPlayMode), 
    fPacketDataInHeaderBufferLen(0),
    fPacketDataInHeaderBuffer(NULL),
    fUserAgent(NULL),
    fControlID(FakeRTSPClient::sControlID)
{
#if DEBUG
    fIsFirstPacket = true;
#endif

    fChannelTrackMap = NEW ChannelMapElem[kMinNumChannelElements];
    ::memset(fChannelTrackMap, 0, sizeof(ChannelMapElem) * kMinNumChannelElements);
    fSSRCMap = NEW SSRCMapElem[kMinNumChannelElements];
    ::memset(fSSRCMap, 0, sizeof(SSRCMapElem) * kMinNumChannelElements);
    fFieldIDMap = NEW FieldIDArrayElem[kMinNumChannelElements];
    ::memset(fFieldIDMap, 0, sizeof(FieldIDArrayElem) * kMinNumChannelElements);
    
    ::memset(fSendBuffer, 0,kReqBufSize + 1);
    ::memset(fRecvHeaderBuffer, 0,kReqBufSize + 1);
    fHaveTransactionBuffer = false;
    
    fSetupHeaders = NEW char[2];
    fSetupHeaders[0] = '\0';
    
    ::memset(&sInterleavedParams, 0, sizeof(sInterleavedParams));
        
	/*fym
	if(inUserAgent != NULL)
	{
		fUserAgent = NEW char[::strlen(inUserAgent) + 1];
		::strcpy(fUserAgent, inUserAgent);
	}
	else
	{
		fUserAgent = NEW char[::strlen(sUserAgent) + 1];
		::strcpy(fUserAgent, sUserAgent);
	}*/
}

FakeRTSPClient::~FakeRTSPClient()
{
    delete [] fRecvContentBuffer;
    delete [] fURL.Ptr;
    //delete [] fName.Ptr;
    //delete [] fPassword.Ptr;
    if(fSessionID.Ptr != sEmptyString)
        delete [] fSessionID.Ptr;
        
    delete [] fSetupHeaders;
    delete [] fChannelTrackMap;
    delete [] fSSRCMap;
    delete [] fFieldIDMap;
    delete [] fPacketBuffer;
        
    delete [] fUserAgent;
    if(fControlID != FakeRTSPClient::sControlID)
        delete [] fControlID;
}

void FakeRTSPClient::SetControlID(char* controlID)
{
    if(NULL == controlID)
        return;
        
    if(fControlID != FakeRTSPClient::sControlID)
        delete [] fControlID;

	fControlID = NEW char[::strlen(controlID) + 1]; 
	::strcpy(fControlID, controlID);

}

/*fym
void FakeRTSPClient::SetName(char *name)
{ 
    Assert (name);  
    delete [] fName.Ptr;  
    fName.Ptr = NEW char[::strlen(name) + 2]; 
    ::strcpy(fName.Ptr, name);
    fName.Len = ::strlen(name);
}

void FakeRTSPClient::SetPassword(char *password)
{   
    Assert (password);  
    delete [] fPassword.Ptr;  
    fPassword.Ptr = NEW char[::strlen(password) + 2]; 
    ::strcpy(fPassword.Ptr, password);
    fPassword.Len = ::strlen(password);
}
*/

void FakeRTSPClient::Set(const StrPtrLen& inURL)
{
    delete [] fURL.Ptr;
    fURL.Ptr = NEW char[inURL.Len + 2];
    fURL.Len = inURL.Len;
    char* destPtr = fURL.Ptr;
    
    // add a leading '/' to the url if it isn't a full URL and doesn't have a leading '/'
    if( !inURL.NumEqualIgnoreCase("rtsp://", strlen("rtsp://")) && inURL.Ptr[0] != '/')
    {
        *destPtr = '/';
        destPtr++;
        fURL.Len++;
    }
    ::memcpy(destPtr, inURL.Ptr, inURL.Len);
    fURL.Ptr[fURL.Len] = '\0';
}

void FakeRTSPClient::SetSetupParams(Float32 inLateTolerance, char* inMetaInfoFields)
{
    delete [] fSetupHeaders;
    fSetupHeaders = NEW char[256];
    fSetupHeaders[0] = '\0';
    
    if(inLateTolerance != 0)
        qtss_sprintf(fSetupHeaders, "x-Transport-Options: late-tolerance=%f\r\n", inLateTolerance);
    if((inMetaInfoFields != NULL) && (::strlen(inMetaInfoFields) > 0))
        qtss_sprintf(fSetupHeaders + ::strlen(fSetupHeaders), "x-RTP-Meta-Info: %s\r\n", inMetaInfoFields);
}

OS_Error FakeRTSPClient::SendDescribe(Bool16 inAppendJunkData)
{
	qtss_printf("\nFakeRTSPClient::SendDescribe");//fym
    if(!fTransactionStarted)
    {
        qtss_sprintf(fMethod,"%s","DESCRIBE");

        if(inAppendJunkData)
        {
            qtss_sprintf(fSendBuffer, "DESCRIBE %s RTSP/1.0\r\nCSeq: %lu\r\nAccept: application/sdp\r\nContent-Length: 200\r\nUser-agent: %s\r\n\r\n", fURL.Ptr, fCSeq, fUserAgent);
            UInt32 theBufLen = ::strlen(fSendBuffer);
            Assert((theBufLen + 200) < kReqBufSize);
            for (UInt32 x = theBufLen; x < (theBufLen + 200); x++)
                fSendBuffer[x] = 'd';
            fSendBuffer[theBufLen + 200] = '\0';
        }
        else
        {
            qtss_sprintf(fSendBuffer, "DESCRIBE %s RTSP/1.0\r\nCSeq: %lu\r\nAccept: application/sdp\r\nUser-agent: %s\r\n\r\n", fURL.Ptr, fCSeq, fUserAgent);
        }
    }
    return this->DoTransaction();
}

OS_Error FakeRTSPClient::SendSetParameter()
{
    if(!fTransactionStarted)
    {
        qtss_sprintf(fMethod,"%s","SET_PARAMETER");
        qtss_sprintf(fSendBuffer, "SET_PARAMETER %s RTSP/1.0\r\nCSeq:%lu\r\nUser-agent: %s\r\n\r\n", fURL.Ptr, fCSeq, fUserAgent);
    }
    return this->DoTransaction();
}

OS_Error FakeRTSPClient::SendOptions()
{
	qtss_printf("\nFakeRTSPClient::SendOptions");//fym
    if(!fTransactionStarted)
    {
        qtss_sprintf(fMethod,"%s","OPTIONS");
        qtss_sprintf(fSendBuffer, "OPTIONS * RTSP/1.0\r\nCSeq:%lu\r\nUser-agent: %s\r\n\r\n", fCSeq, fUserAgent);
     }
    return this->DoTransaction();
}

OS_Error FakeRTSPClient::SendOptionsWithRandomDataRequest(SInt32 dataSize)
{
    if(!fTransactionStarted)
    {
        qtss_sprintf(fMethod,"%s","OPTIONS");
        qtss_sprintf(fSendBuffer, "OPTIONS * RTSP/1.0\r\nCSeq:%lu\r\nUser-agent: %s\r\nRequire: x-Random-Data-Size\r\nx-Random-Data-Size: %ld\r\n\r\n", fCSeq, fUserAgent, dataSize);
     }
    return this->DoTransaction();
}


OS_Error FakeRTSPClient::SendReliableUDPSetup(UInt32 inTrackID, UInt16 inClientPort)
{
	qtss_printf("\nFakeRTSPClient::SendReliableUDPSetup");//fym
    fSetupTrackID = inTrackID; // Needed when SETUP response is received.
    fSendBuffer[0] = 0;
    if(!fTransactionStarted)
    {
        qtss_sprintf(fMethod,"%s","SETUP");
        
        if(fTransportMode == kPushMode)
			qtss_sprintf(fSendBuffer, "SETUP %s/%s=%lu RTSP/1.0\r\nCSeq: %lu\r\n%sTransport: RTP/AVP;unicast;client_port=%u-%u;mode=record\r\nx-Retransmit: our-retransmit\r\nUser-agent: %s\r\n\r\n", fURL.Ptr,fControlID, inTrackID, fCSeq, fSessionID.Ptr, inClientPort, inClientPort + 1, fUserAgent);
        else 
            qtss_sprintf(fSendBuffer, "SETUP %s/%s=%lu RTSP/1.0\r\nCSeq: %lu\r\n%sTransport: RTP/AVP;unicast;client_port=%u-%u\r\nx-Retransmit: our-retransmit\r\nUser-agent: %s\r\n\r\n", fURL.Ptr,fControlID, inTrackID, fCSeq, fSessionID.Ptr, inClientPort, inClientPort + 1, fUserAgent);
    }
    return this->DoTransaction();
}

OS_Error FakeRTSPClient::SendUDPSetup(UInt32 inTrackID, UInt16 inClientPort)
{
	qtss_printf("\nFakeRTSPClient::SendUDPSetup");//fym
    fSetupTrackID = inTrackID; // Needed when SETUP response is received.
    
    if(!fTransactionStarted)
    {
        qtss_sprintf(fMethod,"%s","SETUP");
        
        if(fTransportMode == kPushMode)
            qtss_sprintf(fSendBuffer, "SETUP %s/%s=%lu RTSP/1.0\r\nCSeq: %lu\r\n%sTransport: RTP/AVP;unicast;client_port=%u-%u;mode=record\r\nUser-agent: %s\r\n\r\n", fURL.Ptr,fControlID, inTrackID, fCSeq, fSessionID.Ptr, inClientPort, inClientPort + 1, fUserAgent);
        else 
            qtss_sprintf(fSendBuffer, "SETUP %s/%s=%lu RTSP/1.0\r\nCSeq: %lu\r\n%sTransport: RTP/AVP;unicast;client_port=%u-%u\r\n%sUser-agent: %s\r\n\r\n", fURL.Ptr,fControlID, inTrackID, fCSeq, fSessionID.Ptr, inClientPort, inClientPort + 1, fSetupHeaders, fUserAgent);
    }
    return this->DoTransaction();
}

OS_Error FakeRTSPClient::SendTCPSetup(UInt32 inTrackID, UInt16 inClientRTPid, UInt16 inClientRTCPid)
{
	qtss_printf("\nFakeRTSPClient::SendTCPSetup");//fym
    fSetupTrackID = inTrackID; // Needed when SETUP response is received.
    
    if(!fTransactionStarted)
    {   
        qtss_sprintf(fMethod,"%s","SETUP");
        
        if(fTransportMode == kPushMode)
            qtss_sprintf(fSendBuffer, "SETUP %s/%s=%lu RTSP/1.0\r\nCSeq: %lu\r\n%sTransport: RTP/AVP/TCP;unicast;mode=record;interleaved=%u-%u\r\nUser-agent: %s\r\n\r\n", fURL.Ptr,fControlID, inTrackID, fCSeq, fSessionID.Ptr,inClientRTPid, inClientRTCPid, fUserAgent);
        else 
            qtss_sprintf(fSendBuffer, "SETUP %s/%s=%lu RTSP/1.0\r\nCSeq: %lu\r\n%sTransport: RTP/AVP/TCP;unicast;interleaved=%u-%u\r\n%sUser-agent: %s\r\n\r\n", fURL.Ptr,fControlID, inTrackID, fCSeq, fSessionID.Ptr, inClientRTPid, inClientRTCPid,fSetupHeaders, fUserAgent);
    }

    return this->DoTransaction();

}

OS_Error FakeRTSPClient::SendPlay(UInt32 inStartPlayTimeInSec, Float32 inSpeed)
{
	qtss_printf("\nFakeRTSPClient::SendPlay");//fym
    char speedBuf[128];
    speedBuf[0] = '\0';
    
    if(inSpeed != 1)
        qtss_sprintf(speedBuf, "Speed: %f5.2\r\n", inSpeed);
        
    if(!fTransactionStarted)
    {   qtss_sprintf(fMethod,"%s","PLAY");
        qtss_sprintf(fSendBuffer, "PLAY %s RTSP/1.0\r\nCSeq: %lu\r\n%sRange: npt=%lu.0-\r\n%sx-prebuffer: maxtime=3.0\r\nUser-agent: %s\r\n\r\n", fURL.Ptr, fCSeq, fSessionID.Ptr, inStartPlayTimeInSec, speedBuf, fUserAgent);
        //qtss_sprintf(fSendBuffer, "PLAY %s RTSP/1.0\r\nCSeq: %lu\r\n%sRange: npt=7.0-\r\n%sx-prebuffer: maxtime=3.0\r\nUser-agent: %s\r\n\r\n", fURL.Ptr, fCSeq, fSessionID.Ptr, speedBuf, fUserAgent);
    }
    return this->DoTransaction();
}

OS_Error FakeRTSPClient::SendPacketRangePlay(char* inPacketRangeHeader, Float32 inSpeed)
{
    char speedBuf[128];
    speedBuf[0] = '\0';
    
    if(inSpeed != 1)
        qtss_sprintf(speedBuf, "Speed: %f5.2\r\n", inSpeed);
        
    if(!fTransactionStarted)
    {
        qtss_sprintf(fMethod,"%s","PLAY");
        qtss_sprintf(fSendBuffer, "PLAY %s RTSP/1.0\r\nCSeq: %lu\r\n%sx-Packet-Range: %s\r\n%sUser-agent: %s\r\n\r\n", fURL.Ptr, fCSeq, fSessionID.Ptr, inPacketRangeHeader, speedBuf, fUserAgent);
    }
    return this->DoTransaction();   
}

OS_Error FakeRTSPClient::SendReceive(UInt32 inStartPlayTimeInSec)
{
    if(!fTransactionStarted)
    {
        qtss_sprintf(fMethod,"%s","RECORD");
        qtss_sprintf(fSendBuffer, "RECORD %s RTSP/1.0\r\nCSeq: %lu\r\n%sRange: npt=%lu.0-\r\nUser-agent: %s\r\n\r\n", fURL.Ptr, fCSeq, fSessionID.Ptr, inStartPlayTimeInSec, fUserAgent);
    }
    return this->DoTransaction();
}

OS_Error FakeRTSPClient::SendAnnounce(char *sdp)
{
//ANNOUNCE rtsp://server.example.com/permanent_broadcasts/TestBroadcast.sdp RTSP/1.0
    if(!fTransactionStarted)
    {   
        qtss_sprintf(fMethod,"%s","ANNOUNCE");
        if(sdp == NULL)
            qtss_sprintf(fSendBuffer, "ANNOUNCE %s RTSP/1.0\r\nCSeq: %lu\r\nAccept: application/sdp\r\nUser-agent: %s\r\n\r\n", fURL.Ptr, fCSeq, fUserAgent);
        else
        {   UInt32 len = strlen(sdp);
            if(len > kReqBufSize)
                return OS_NotEnoughSpace;
            qtss_sprintf(fSendBuffer, "ANNOUNCE %s RTSP/1.0\r\nCSeq: %lu\r\nContent-Type: application/sdp\r\nUser-agent: %s\r\nContent-Length: %lu\r\n\r\n%s", fURL.Ptr, fCSeq, fUserAgent, len, sdp);
        }   
    }
    return this->DoTransaction();
}

OS_Error FakeRTSPClient::SendRTSPRequest(iovec* inRequest, UInt32 inNumVecs)
{
	qtss_printf("\nFakeRTSPClient::SendRTSPRequest");//fym
    if(!fTransactionStarted)
    {
        UInt32 curOffset = 0;
        for (UInt32 x = 0; x < inNumVecs; x++)
        {
            ::memcpy(fSendBuffer + curOffset, inRequest[x].iov_base, inRequest[x].iov_len);
            curOffset += inRequest[x].iov_len;
        }
        Assert(kReqBufSize > curOffset);
        fSendBuffer[curOffset] = '\0';
    }
    return this->DoTransaction();
}

OS_Error FakeRTSPClient::PutMediaPacket(UInt32 inTrackID, Bool16 isRTCP, char* inData, UInt16 inLen)
{
    // Find the right channel number for this trackID
    for (int x = 0; x < fNumChannelElements; x++)
    {
        if((fChannelTrackMap[x].fTrackID == inTrackID) && (fChannelTrackMap[x].fIsRTCP == isRTCP))
        {
            char header[5];
            header[0] = '$';
            header[1] = (UInt8)x;
            UInt16* theLenP = (UInt16*)header;
            theLenP[1] = htons(inLen);
                    
            //
            // Build the iovec
            iovec ioVec[2];
            ioVec[0].iov_len = 4;
            ioVec[0].iov_base = header;
            ioVec[1].iov_len = inLen;
            ioVec[1].iov_base = inData;
            
            //
            // Send it
            return fSocket->SendV(ioVec, 2);
        }
    }
    
    return OS_NoErr;
}


OS_Error FakeRTSPClient::SendInterleavedWrite(UInt8 channel, UInt16 len, char*data,Bool16 *getNext)
{

    Assert(len < FakeRTSPClient::kReqBufSize);
    
    iovec ioVEC[1];
    struct iovec* iov = &ioVEC[0];
    UInt16 interleavedLen =0;   
    UInt16 sendLen = 0;
    
    if(sInterleavedParams.extraLen > 0)
    {   *getNext = false; // can't handle new packet now. Send it again
        ioVEC[0].iov_base   = sInterleavedParams.extraBytes;
        ioVEC[0].iov_len    = sInterleavedParams.extraLen;
        sendLen = sInterleavedParams.extraLen;
    }
    else
    {   *getNext = true; // handle a new packet
        fSendBuffer[0] = '$';
        fSendBuffer[1] = channel;
        UInt16 netlen = htons(len);
        memcpy(&fSendBuffer[2],&netlen,2);
        memcpy(&fSendBuffer[4],data,len);
        
        interleavedLen = len+4;
        ioVEC[0].iov_base=&fSendBuffer[0];
        ioVEC[0].iov_len= interleavedLen;
        sendLen = interleavedLen;
        sInterleavedParams.extraChannel =channel;
    }   
        
    UInt32 outLenSent;
    OS_Error theErr = fSocket->GetSocket()->WriteV(iov, 1,&outLenSent);
    if(theErr != 0)
        outLenSent = 0;

    //qtss_printf("FakeRTSPClient::SendInterleavedWrite Send channel=%u bufferlen=%u err=%ld outLenSent=%lu\n",(UInt16) extraChannel, sendLen,theErr,outLenSent);
    if(theErr == 0 && outLenSent != sendLen) 
    {   if(sInterleavedParams.extraLen > 0) // sending extra len so keep sending it.
        {   //qtss_printf("FakeRTSPClient::SendInterleavedWrite partial Send channel=%u bufferlen=%u err=%ld amountSent=%lu \n",(UInt16) extraChannel,sendLen,theErr,outLenSent);
            sInterleavedParams.extraLen = sendLen - outLenSent;
            sInterleavedParams.extraByteOffset += outLenSent;
            sInterleavedParams.extraBytes = &fSendBuffer[sInterleavedParams.extraByteOffset];
        }
        else // we were sending a new packet so record the data
        {   //qtss_printf("FakeRTSPClient::SendInterleavedWrite partial Send channel=%u bufferlen=%u err=%ld amountSent=%lu \n",(UInt16) channel,sendLen,theErr,outLenSent);
            sInterleavedParams.extraBytes = &fSendBuffer[outLenSent];
            sInterleavedParams.extraLen = sendLen - outLenSent;
            sInterleavedParams.extraChannel = channel;
            sInterleavedParams.extraByteOffset = outLenSent;
        }
    }
    else // either an error occured or we sent everything ok
    {
        if(theErr == 0)
        {   
            if(sInterleavedParams.extraLen > 0) // we were busy sending some old data and it all got sent
            {   //qtss_printf("FakeRTSPClient::SendInterleavedWrite FULL Send channel=%u bufferlen=%u err=%ld amountSent=%lu \n",(UInt16) extraChannel,sendLen,theErr,outLenSent);
            }
            else 
            {   // it all worked so ask for more data
                //qtss_printf("FakeRTSPClient::SendInterleavedWrite FULL Send channel=%u bufferlen=%u err=%ld amountSent=%lu \n",(UInt16) channel,sendLen,theErr,outLenSent);
            }
            sInterleavedParams.extraLen = 0;
            sInterleavedParams.extraBytes = NULL;
            sInterleavedParams.extraByteOffset = 0;
        }
        else // we got an error so nothing was sent
        {   //qtss_printf("FakeRTSPClient::SendInterleavedWrite Send ERR sending=%ld \n",theErr);

            if(sInterleavedParams.extraLen == 0) // retry the new packet
            {   
                sInterleavedParams.extraBytes = &fSendBuffer[0];
                sInterleavedParams.extraLen = sendLen;
                sInterleavedParams.extraChannel = channel;              
                sInterleavedParams.extraByteOffset = 0;
            }       
        }
    }   
    return theErr;          
}

OS_Error FakeRTSPClient::SendTeardown()
{
    if(!fTransactionStarted)
    {   qtss_sprintf(fMethod,"%s","TEARDOWN");
        qtss_sprintf(fSendBuffer, "TEARDOWN %s RTSP/1.0\r\nCSeq: %lu\r\n%sUser-agent: %s\r\n\r\n", fURL.Ptr, fCSeq, fSessionID.Ptr, fUserAgent);
    }
    return this->DoTransaction();
}


OS_Error    FakeRTSPClient::GetMediaPacket(UInt32* outTrackID, Bool16* outIsRTCP, char** outBuffer, UInt32* outLen)
{
    static const UInt32 kPacketHeaderLen = 4;
    static const UInt32 kMaxPacketSize = 4096;
    
    // We need to buffer until we get a full packet.
    if(fPacketBuffer == NULL)
        fPacketBuffer = NEW char[kMaxPacketSize];
        
    if(fPacketOutstanding)
    {
        // The previous call to this function returned a packet successfully. We've been holding
        // onto that packet data until now... Now we can blow it away.
        UInt16* thePacketLenP = (UInt16*)fPacketBuffer;
        UInt16 thePacketLen = ntohs(thePacketLenP[1]);
        
        Assert(fPacketBuffer[0] == '$');

        // Move the leftover data (part of the next packet) to the beginning of the buffer
        Assert(fPacketBufferOffset >= (thePacketLen + kPacketHeaderLen));
        fPacketBufferOffset -= thePacketLen + kPacketHeaderLen;
        ::memmove(fPacketBuffer, &fPacketBuffer[thePacketLen + kPacketHeaderLen], fPacketBufferOffset);
#if DEBUG
        if(fPacketBufferOffset > 0)
        {
            Assert(fPacketBuffer[0] == '$');
        }
#endif
        
        fPacketOutstanding = false;
    }
    
    if(fPacketDataInHeaderBufferLen > 0)
    {
        //
        // If there is some packet data in the header buffer, clear it out
        //qtss_printf("%d bytes of packet data in header buffer\n",fPacketDataInHeaderBufferLen);
        
        Assert(fPacketDataInHeaderBuffer[0] == '$');
        Assert(fPacketDataInHeaderBufferLen < (kMaxPacketSize - fPacketBufferOffset));
        ::memcpy(&fPacketBuffer[fPacketBufferOffset], fPacketDataInHeaderBuffer, fPacketDataInHeaderBufferLen);
        fPacketBufferOffset += fPacketDataInHeaderBufferLen;
        fPacketDataInHeaderBufferLen = 0;
    }

    Assert(fPacketBufferOffset < kMaxPacketSize);
    UInt32 theRecvLen = 0;
    OS_Error theErr = fSocket->Read(&fPacketBuffer[fPacketBufferOffset], kMaxPacketSize - fPacketBufferOffset, &theRecvLen);
    if(theErr != OS_NoErr)
        return theErr;

    fPacketBufferOffset += theRecvLen;
    Assert(fPacketBufferOffset <= kMaxPacketSize);

    if(fPacketBufferOffset > kPacketHeaderLen)
    {
        Assert(fPacketBuffer[0] == '$');
        UInt16* thePacketLenP = (UInt16*)fPacketBuffer;
        UInt16 thePacketLen = ntohs(thePacketLenP[1]);
        
        if(fPacketBufferOffset >= (thePacketLen + kPacketHeaderLen))
        {
            // We have a complete packet. Return it to the caller.
            Assert(fPacketBuffer[1] < fNumChannelElements); // This is really not a safe assert, but anyway...
            *outTrackID = fChannelTrackMap[fPacketBuffer[1]].fTrackID;
            *outIsRTCP = fChannelTrackMap[fPacketBuffer[1]].fIsRTCP;
            *outLen = thePacketLen;
            
            // Next time we call this function, we will blow away the packet, but until then
            // we leave it untouched.
            fPacketOutstanding = true;
            *outBuffer = &fPacketBuffer[kPacketHeaderLen];
#if DEBUG
            fIsFirstPacket = false;
#endif
            return OS_NoErr;
        }
    }
    return OS_NoErr;
}

UInt32  FakeRTSPClient::GetSSRCByTrack(UInt32 inTrackID)
{
    for (UInt32 x = 0; x < fNumSSRCElements; x++)
    {
        if(inTrackID == fSSRCMap[x].fTrackID)
            return fSSRCMap[x].fSSRC;
    }
    return 0;
}

RTPMetaInfoPacket::FieldID* FakeRTSPClient::GetFieldIDArrayByTrack(UInt32 inTrackID)
{
    for (UInt32 x = 0; x < fNumFieldIDElements; x++)
    {
        if(inTrackID == fFieldIDMap[x].fTrackID)
            return fFieldIDMap[x].fFieldIDs;
    }
    return NULL;
}


OS_Error FakeRTSPClient::DoTransaction()
{

    OS_Error theErr = OS_NoErr;
    Bool16 isAuthenticated = false;
    
    fSendBufferLen = ::strlen(fSendBuffer);
    StrPtrLen theRequest(fSendBuffer,fSendBufferLen);
    StrPtrLen theMethod(fMethod);

    if(!fTransactionStarted)
    {   
        //
        // Make sure that if there is some packet data in the header buffer, we forget all about it.
        fPacketDataInHeaderBufferLen = 0;
        
        fCSeq++;
    }
    
    if(!fReceiveInProgress)
    {   fResponseCount = 0;
        StrPtrLen theTransaction;
        fTransactionStarted = true;
        if(!fHaveTransactionBuffer)
        {   fHaveTransactionBuffer = true;
            theTransaction.Set(theRequest.Ptr,theRequest.Len);
        }
        theErr = OS_NoErr;//fym fake fSocket->Send(theTransaction.Ptr, theTransaction.Len);
        
        if(fVerbose)
            qtss_printf("\n-----REQUEST-----len=%lu\n%s\n", theRequest.Len, STRTOCHAR(&theRequest));
        //qtss_printf("FakeRTSPClient::DoTransaction Send len=%lu err = %ld\n",theTransaction.Len, theErr);
        
        if(theErr != OS_NoErr)
            return theErr;
            
        fHaveTransactionBuffer = false;
        

        // Done sending request, we're moving onto receiving the response
        fConnected = true;
        fContentRecvLen = 0;
        fHeaderRecvLen = 0;
        fReceivedResponse = false;
        fReceiveInProgress = true;
        memset(fRecvHeaderBuffer,0,kReqBufSize+1);

    }
    
    if(fReceiveInProgress)
    {
        Assert(theErr == OS_NoErr);
        theErr = this->ReceiveResponse();
        //qtss_printf("FakeRTSPClient::DoTransaction ReceiveResponse fStatus=%lu len=%lu err = %ld\n",fStatus, fHeaderRecvLen, theErr);
    
        if(theErr != OS_NoErr)
            return theErr;

        fReceiveInProgress = false;
    }
    

    if(!fReceiveInProgress && (401 == fStatus)) // authentication required or authentication failed
    {
        fResponseCount ++;
        if(fResponseCount != 1) // just try to authenticate once against a 401.
            return 0;

        if(fVerbose)
            qtss_printf("\n-----REQUEST----\n%s\n", STRTOCHAR(&theRequest));
        
        
        StrPtrLen theTransaction;
        if(!fHaveTransactionBuffer)
        {   fHaveTransactionBuffer = true;
            theTransaction.Set(theRequest.Ptr,theRequest.Len);
        }

        theErr = fSocket->Send(theTransaction.Ptr, theTransaction.Len);
        //qtss_printf("fSocket->Send err =%ld len=%lu\n",theErr, theTransaction.Len);
        if(theErr != OS_NoErr)
            return theErr;

        fHaveTransactionBuffer = false; 
        
        fContentRecvLen = 0;
        fHeaderRecvLen = 0;
        fReceivedResponse = false;
        fRecvHeaderBuffer[0] = 0;
        fHeaderLen = 0;
        memset(fRecvHeaderBuffer,0,kReqBufSize+1);
        
        fReceiveInProgress = true;
        fTransactionStarted = true;
        return EAGAIN;
    }
    
    
    return theErr;
}

OS_Error FakeRTSPClient::ReceiveResponse()
{
    OS_Error theErr = OS_NoErr;

	qtss_printf("%d", this->GetServerPort());//fym

    while (!fReceivedResponse)
    {
        UInt32 theRecvLen = 0;
        //fRecvHeaderBuffer[0] = 0;
        theErr = fSocket->Read(&fRecvHeaderBuffer[fHeaderRecvLen], kReqBufSize - fHeaderRecvLen, &theRecvLen);
        if(theErr != OS_NoErr)
            return theErr;
        
        fHeaderRecvLen += theRecvLen;
        fRecvHeaderBuffer[fHeaderRecvLen] = 0;
        if(fVerbose)
            qtss_printf("\n-----RESPONSE----\n%s\n", fRecvHeaderBuffer);

        //fRecvHeaderBuffer[fHeaderRecvLen] = '\0';
        // Check to see if we've gotten a complete header, and if the header has even started       
        // The response may not start with the response if we are interleaving media data,
        // in which case there may be leftover media data in the stream. If we encounter any
        // of this cruft, we can just strip it off.
        char* theHeaderStart = ::strstr(fRecvHeaderBuffer, "RTSP");
        if(theHeaderStart == NULL)
        {
            fHeaderRecvLen = 0;
            continue;
        }
        else if(theHeaderStart != fRecvHeaderBuffer)
        {
            fHeaderRecvLen -= theHeaderStart - fRecvHeaderBuffer;
            ::memmove(fRecvHeaderBuffer, theHeaderStart, fHeaderRecvLen);
            //fRecvHeaderBuffer[fHeaderRecvLen] = '\0';
        }
        //qtss_printf("FakeRTSPClient::ReceiveResponse fRecvHeaderBuffer=%s\n",fRecvHeaderBuffer);
        char* theResponseData = ::strstr(fRecvHeaderBuffer, "\r\n\r\n");    

        if(theResponseData != NULL)
        {               
            // skip past the \r\n\r\n
            theResponseData += 4;
            
            // We've got a new response
            fReceivedResponse = true;
            
            // Figure out how much of the content body we've already received
            // in the header buffer. If we are interleaving, this may also be packet data
            fHeaderLen = theResponseData - &fRecvHeaderBuffer[0];
            fContentRecvLen = fHeaderRecvLen - fHeaderLen;

            // Zero out fields that will change with every RTSP response
            fServerPort = 0;
            fStatus = 0;
            fContentLength = 0;
        
            // Parse the response.
            StrPtrLen theData(fRecvHeaderBuffer, (theResponseData - (&fRecvHeaderBuffer[0])));
            StringParser theParser(&theData);
            
            theParser.ConsumeLength(NULL, 9); //skip past RTSP/1.0
            fStatus = theParser.ConsumeInteger(NULL);
            
            StrPtrLen theLastHeader;
            while (theParser.GetDataRemaining() > 0)
            {
                static StrPtrLen sSessionHeader("Session");
                static StrPtrLen sContentLenHeader("Content-length");
                static StrPtrLen sTransportHeader("Transport");
                static StrPtrLen sRTPInfoHeader("RTP-Info");
                static StrPtrLen sRTPMetaInfoHeader("x-RTP-Meta-Info");
                //fym static StrPtrLen sAuthenticateHeader("WWW-Authenticate");
                static StrPtrLen sSameAsLastHeader(" ,");
                
                StrPtrLen theKey;
                theParser.GetThruEOL(&theKey);
                
                if(theKey.NumEqualIgnoreCase(sSessionHeader.Ptr, sSessionHeader.Len))
                {
                    if(fSessionID.Len == 0)
                    {
                        // Copy the session ID and store it.
                        // First figure out how big the session ID is. We copy
                        // everything up until the first ';' returned from the server
                        UInt32 keyLen = 0;
                        while ((theKey.Ptr[keyLen] != ';') && (theKey.Ptr[keyLen] != '\r') && (theKey.Ptr[keyLen] != '\n'))
                            keyLen++;
                        
                        // Append an EOL so we can stick this thing transparently into the SETUP request
                        
                        fSessionID.Ptr = NEW char[keyLen + 3];
                        fSessionID.Len = keyLen + 2;
                        ::memcpy(fSessionID.Ptr, theKey.Ptr, keyLen);
                        ::memcpy(fSessionID.Ptr + keyLen, "\r\n", 2);//Append a EOL
                        fSessionID.Ptr[keyLen + 2] = '\0';
                    }
                }
                else if(theKey.NumEqualIgnoreCase(sContentLenHeader.Ptr, sContentLenHeader.Len))
                {
                    StringParser theCLengthParser(&theKey);
                    theCLengthParser.ConsumeUntil(NULL, StringParser::sDigitMask);
                    fContentLength = theCLengthParser.ConsumeInteger(NULL);
                    
                    delete [] fRecvContentBuffer;
                    fRecvContentBuffer = NEW char[fContentLength + 1];
                    
                    // Immediately copy the bit of the content body that we've already
                    // read off of the socket.
                    ::memcpy(fRecvContentBuffer, theResponseData, fContentRecvLen);
                    
                }
                else if(theKey.NumEqualIgnoreCase(sTransportHeader.Ptr, sTransportHeader.Len))
                {
                    StringParser theTransportParser(&theKey);
                    StrPtrLen theSubHeader;

                    while (theTransportParser.GetDataRemaining() > 0)
                    {
                        static StrPtrLen sServerPort("server_port");
                        static StrPtrLen sInterleaved("interleaved");

                        theTransportParser.GetThru(&theSubHeader, ';');
                        if(theSubHeader.NumEqualIgnoreCase(sServerPort.Ptr, sServerPort.Len))
                        {
                            StringParser thePortParser(&theSubHeader);
                            thePortParser.ConsumeUntil(NULL, StringParser::sDigitMask);
                            fServerPort = (UInt16) thePortParser.ConsumeInteger(NULL);
                        }
                        else if(theSubHeader.NumEqualIgnoreCase(sInterleaved.Ptr, sInterleaved.Len))
                            this->ParseInterleaveSubHeader(&theSubHeader);                          
                    }
                }
                else if(theKey.NumEqualIgnoreCase(sRTPInfoHeader.Ptr, sRTPInfoHeader.Len))
                    ParseRTPInfoHeader(&theKey);
                else if(theKey.NumEqualIgnoreCase(sRTPMetaInfoHeader.Ptr, sRTPMetaInfoHeader.Len))
                    ParseRTPMetaInfoHeader(&theKey);
                else if(theKey.NumEqualIgnoreCase(sSameAsLastHeader.Ptr, sSameAsLastHeader.Len))
                {
                    //
                    // If the last header was an RTP-Info header
                    if(theLastHeader.NumEqualIgnoreCase(sRTPInfoHeader.Ptr, sRTPInfoHeader.Len))
                        ParseRTPInfoHeader(&theKey);
                }
                theLastHeader = theKey;
            }
            
            //
            // Check to see if there is any packet data in the header buffer
            if(fContentRecvLen > fContentLength)
            {
                fPacketDataInHeaderBuffer = theResponseData + fContentLength;
                fPacketDataInHeaderBufferLen = fContentRecvLen - fContentLength;
            }
        }
        else if(fHeaderRecvLen == kReqBufSize)
            return ENOBUFS; // This response is too big for us to handle!
    }
    
    /*fym fake
	while (fContentLength > fContentRecvLen)
    {
        UInt32 theContentRecvLen = 0;
        theErr = fSocket->Read(&fRecvContentBuffer[fContentRecvLen], fContentLength - fContentRecvLen, &theContentRecvLen);
        if(theErr != OS_NoErr)
        {
            fEventMask = EV_RE;
            return theErr;
        }
        fContentRecvLen += theContentRecvLen;       
    }*/
    
    // We're all done, reset all our state information.
    fReceivedResponse = false;
    fReceiveInProgress = false;
    fTransactionStarted = false;

    return OS_NoErr;
}

void    FakeRTSPClient::ParseRTPInfoHeader(StrPtrLen* inHeader)
{
    static StrPtrLen sURL("url");
    StringParser theParser(inHeader);
    theParser.ConsumeUntil(NULL, 'u'); // consume until "url"
    
    if(fNumSSRCElements == fSSRCMapSize)
    {
        SSRCMapElem* theNewMap = NEW SSRCMapElem[fSSRCMapSize * 2];
        ::memset(theNewMap, 0, sizeof(SSRCMapElem) * (fSSRCMapSize * 2));
        ::memcpy(theNewMap, fSSRCMap, sizeof(SSRCMapElem) * fNumSSRCElements);
        fSSRCMapSize *= 2;
        delete [] fSSRCMap;
        fSSRCMap = theNewMap;
    }   
    
    fSSRCMap[fNumSSRCElements].fTrackID = 0;
    fSSRCMap[fNumSSRCElements].fSSRC = 0;
    
    // Parse out the trackID & the SSRC
    StrPtrLen theRTPInfoSubHeader;
    (void)theParser.GetThru(&theRTPInfoSubHeader, ';');
    
    while (theRTPInfoSubHeader.Len > 0)
    {
        static StrPtrLen sURLSubHeader("url");
        static StrPtrLen sSSRCSubHeader("ssrc");
        
        if(theRTPInfoSubHeader.NumEqualIgnoreCase(sURLSubHeader.Ptr, sURLSubHeader.Len))
        {
            StringParser theURLParser(&theRTPInfoSubHeader);
            theURLParser.ConsumeUntil(NULL, StringParser::sDigitMask);
            fSSRCMap[fNumSSRCElements].fTrackID = theURLParser.ConsumeInteger(NULL);
        }
        else if(theRTPInfoSubHeader.NumEqualIgnoreCase(sSSRCSubHeader.Ptr, sSSRCSubHeader.Len))
        {
            StringParser theURLParser(&theRTPInfoSubHeader);
            theURLParser.ConsumeUntil(NULL, StringParser::sDigitMask);
            fSSRCMap[fNumSSRCElements].fSSRC = theURLParser.ConsumeInteger(NULL);
        }

        // Move onto the next parameter
        (void)theParser.GetThru(&theRTPInfoSubHeader, ';');
    }
    
    fNumSSRCElements++;
}

void    FakeRTSPClient::ParseRTPMetaInfoHeader(StrPtrLen* inHeader)
{
    //
    // Reallocate the array if necessary
    if(fNumFieldIDElements == fFieldIDMapSize)
    {
        FieldIDArrayElem* theNewMap = NEW FieldIDArrayElem[fFieldIDMapSize * 2];
        ::memset(theNewMap, 0, sizeof(FieldIDArrayElem) * (fFieldIDMapSize * 2));
        ::memcpy(theNewMap, fFieldIDMap, sizeof(FieldIDArrayElem) * fNumFieldIDElements);
        fFieldIDMapSize *= 2;
        delete [] fFieldIDMap;
        fFieldIDMap = theNewMap;
    }
    
    //
    // Build the FieldIDArray for this track
    RTPMetaInfoPacket::ConstructFieldIDArrayFromHeader(inHeader, fFieldIDMap[fNumFieldIDElements].fFieldIDs);
    fFieldIDMap[fNumFieldIDElements].fTrackID = fSetupTrackID;
    fNumFieldIDElements++;
}

void    FakeRTSPClient::ParseInterleaveSubHeader(StrPtrLen* inSubHeader)
{
    StringParser theChannelParser(inSubHeader);
    
    // Parse out the channel numbers
    theChannelParser.ConsumeUntil(NULL, StringParser::sDigitMask);
    UInt8 theRTPChannel = (UInt8) theChannelParser.ConsumeInteger(NULL);
    theChannelParser.ConsumeLength(NULL, 1);
    UInt8 theRTCPChannel = (UInt8) theChannelParser.ConsumeInteger(NULL);
    
    UInt8 theMaxChannel = theRTCPChannel;
    if(theRTPChannel > theMaxChannel)
        theMaxChannel = theRTPChannel;
    
    // Reallocate the channel-track array if it is too little
    if(theMaxChannel >= fNumChannelElements)
    {
        ChannelMapElem* theNewMap = NEW ChannelMapElem[theMaxChannel + 1];
        ::memset(theNewMap, 0, sizeof(ChannelMapElem) * (theMaxChannel + 1));
        ::memcpy(theNewMap, fChannelTrackMap, sizeof(ChannelMapElem) * fNumChannelElements);
        fNumChannelElements = theMaxChannel + 1;
        delete [] fChannelTrackMap;
        fChannelTrackMap = theNewMap;
    }
    
    // Copy the relevent information into the channel-track array.
    fChannelTrackMap[theRTPChannel].fTrackID = fSetupTrackID;
    fChannelTrackMap[theRTPChannel].fIsRTCP = false;
    fChannelTrackMap[theRTCPChannel].fTrackID = fSetupTrackID;
    fChannelTrackMap[theRTCPChannel].fIsRTCP = true;
}
