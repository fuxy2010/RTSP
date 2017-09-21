/*
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
 */
/*
    File:       win32main.cpp

    Contains:   main function to drive streaming server on win32.


*/

#include "getopt.h"
#include "FilePrefsSource.h"

#include "RunServerLib.h"
#include "QTSServer.h"
//fym #include "QTSSExpirationDate.h"
#include "GenerateXMLPrefs.h"

#include "RTSPServer.h"//fym

using namespace RTSPServerLib;

//
// Data
static FilePrefsSource sPrefsSource(true); // Allow dups
static XMLPrefsParser* sXMLParser = NULL;
static FilePrefsSource sMessagesSource;
static UInt16 sPort = 0; //port can be set on the command line
static int sStatsUpdateInterval = 1;//fym 0;
static SERVICE_STATUS_HANDLE sServiceStatusHandle = 0;
static QTSS_ServerState sInitialState = qtssRunningState;


CRTSPServer::CRTSPServer(unsigned short rtsp_port) :
_rtsp_port(rtsp_port)
{
	//_rtsp_port = rtsp_port;//fym

	sPort = _rtsp_port;//fym

	extern char* optarg;

	//First thing to do is to read command-line arguments.
	int ch;

	char* theConfigFilePath = "streamingserver.cfg";
	char* theXMLFilePath = "streamingserver.xml";
	Bool16 notAService = true;//fym false;
	Bool16 theXMLPrefsExist = true;
	Bool16 dontFork = false;

#if _DEBUG
	char* compileType = "Compile_Flags/_DEBUG; ";
#else
	char* compileType = "";
#endif

	// Create an XML prefs parser object using the specified path
	sXMLParser = new XMLPrefsParser(theXMLFilePath);

	//
	// Check to see if the XML file exists as a directory. If it does,
	// just bail because we do not want to overwrite a directory
	if (sXMLParser->DoesFileExistAsDirectory())
	{
		qtss_printf("Directory located at location where streaming server prefs file should be.\n");
		return;//::exit(0);
	}

	if (!sXMLParser->CanWriteFile())
	{
		qtss_printf("Cannot write to the streaming server prefs file.\n");
		return;//::exit(0);
	}

	// If we aren't forced to create a new XML prefs file, whether
	// we do or not depends solely on whether the XML prefs file exists currently.
	if (theXMLPrefsExist)
		theXMLPrefsExist = sXMLParser->DoesFileExist();

	if (!theXMLPrefsExist)
	{
		//
		//Construct a Prefs Source object to get server preferences

		int prefsErr = sPrefsSource.InitFromConfigFile(theConfigFilePath);
		if ( prefsErr )
		{
			qtss_printf("Could not load configuration file at %s.\n Generating a new prefs file at %s\n", theConfigFilePath, theXMLFilePath);
			return;
		}

		//
		// Generate a brand-new XML prefs file out of the old prefs
		int xmlGenerateErr = GenerateAllXMLPrefs(&sPrefsSource, sXMLParser);
		if (xmlGenerateErr)
		{
			qtss_printf("Fatal Error: Could not create new prefs file at: %s. (%d)\n", theConfigFilePath, OSThread::GetErrno());
			return;//::exit(-1);
		}       
	}

	//
	// Parse the configs from the XML file
	int xmlParseErr = sXMLParser->Parse();
	if (xmlParseErr)
	{
		qtss_printf("Fatal Error: Could not load configuration file at %s. (%d)\n", theXMLFilePath, OSThread::GetErrno());
		return;//::exit(-1);
	}

	//
	// Construct a messages source object
	sMessagesSource.InitFromConfigFile("RTSPServerMessages.txt");

	//
	// Start Win32 DLLs
	WORD wsVersion = MAKEWORD(1, 1);
	WSADATA wsData;
	(void)::WSAStartup(wsVersion, &wsData);

	//不以服务形式运行
	::StartServerLib(sXMLParser, &sMessagesSource, sPort, sStatsUpdateInterval, sInitialState, false,0, 0/*fym kRunServerDebug_Off*/); // No stats update interval for now
}

CRTSPServer::~CRTSPServer()
{
	//fym for leak
	delete sXMLParser;
	sXMLParser = NULL;

	StopServerLib();
}

/*int CRTSPServer::add_relay_source(unsigned short uuid, std::string& sdp)
{
	if(0 > uuid || 20000 <= uuid)
	{
		sdp.clear();
		return -1;
	}

	char temp[16];
	ZeroMemory(temp, sizeof(temp));

	sprintf_s(temp, ":%d/%d.sdp", _rtsp_port, uuid);

	sdp.clear();
	sdp = temp;

	return 0;
}

int CRTSPServer::remove_relay_source(unsigned short uuid)
{
	return 0;
}*/

#include "reflectorstream.h"
void CRTSPServer::set_max_packet_size(unsigned short nMaxPacketSize)
{
	ReflectorStream::fRTPPayloadSize = (!nMaxPacketSize || 1400 < nMaxPacketSize) ? 1400 : nMaxPacketSize;
}

int CRTSPServer::input_stream_data(unsigned short uuid, void* pData, unsigned long nLen, unsigned short nDataType)
{
	if(0 > uuid || !nLen || NULL == pData || 0 > nDataType)
		return -1;

	//qtss_printf("id %d len %d, ", uuid, nLen);//fym


	QTSS_RoleParams relay_source_params;
	QTSS_Error ret = 1;

	relay_source_params.relay_stream_data.in_relay_source_uuid = uuid;
	relay_source_params.relay_stream_data.in_packet_data = (char*)pData;
	relay_source_params.relay_stream_data.in_packet_len = nLen;
	relay_source_params.relay_stream_data.in_packet_type = nDataType;
	//relay_source_params.relay_stream_data.in_time_stamp_ssrc = nTimeStamp;
	//relay_source_params.relay_stream_data.in_time_stamp_ssrc <<= 32;
	//relay_source_params.relay_stream_data.in_time_stamp_ssrc |= nSSRC;

	//肯定增加了QTSS_AddRelaySource_Role角色的module只有一个，所以直接用0
	QTSSModule* theModule = QTSServerInterface::GetModule(QTSSModule::kInputStreamData, 0);
	if(NULL != theModule)
	{
		ret = theModule->CallDispatch(QTSS_InputStreamData_Role, &relay_source_params);
	}

	return (int)ret;
}

//int CRTSPServer::query_relay_source(unsigned short uuid, RELAY_SOURCE_INFO& relay_source_info)
int CRTSPServer::query_relay_source()
{
	PrintStatus();

	return 0;
}

void CRTSPServer::set_data_callback(void (CALLBACK* pDataCallBackFunc)(const char* data, const unsigned long& length))
{
	QTSServer::fDataCallBackFunc = pDataCallBackFunc;
}

