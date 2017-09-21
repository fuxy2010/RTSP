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
    File:       main.cpp

    Contains:   main function to drive streaming server.

    

*/

#include <errno.h>

#include "RunServer.h"
#include "SafeStdLib.h"
#include "OS.h"
#include "OSMemory.h"
#include "OSThread.h"
#include "Socket.h"
#include "SocketUtils.h"
#include "ev.h"
#include "OSArrayObjectDeleter.h"
#include "Task.h"
#include "IdleTask.h"
#include "TimeoutTask.h"
#include "DateTranslator.h"
#include "QTSSRollingLog.h"


#ifndef __Win32__
    #include <sys/types.h>
    #include <unistd.h>
#endif
#include "QTSServerInterface.h"
#include "QTSServer.h"

#include <stdlib.h>
#include <sys/stat.h>

QTSServer* sServer = NULL;
int sStatusUpdateInterval = 0;
Bool16 sHasPID = false;
UInt64 sLastStatusPackets = 0;
UInt64 sLastDebugPackets = 0;
SInt64 sLastDebugTotalQuality = 0;
#ifdef __sgi__ 
#include <sched.h>
#endif

QTSS_ServerState StartServer(XMLPrefsParser* inPrefsSource, PrefsSource* inMessagesSource, UInt16 inPortOverride, int statsUpdateInterval, QTSS_ServerState inInitialState, Bool16 inDontFork, UInt32 debugLevel, UInt32 debugOptions)
{
    //Mark when we are done starting up. If auto-restart is enabled, we want to make sure
    //to always exit with a status of 0 if we encountered a problem WHILE STARTING UP. This
    //will prevent infinite-auto-restart-loop type problems
    Bool16 doneStartingUp = false;
    QTSS_ServerState theServerState = qtssStartingUpState;
    
    sStatusUpdateInterval = statsUpdateInterval;
    
    //Initialize utility classes
    OS::Initialize();
    OSThread::Initialize();

    Socket::Initialize();
    SocketUtils::Initialize(!inDontFork);

#if !MACOSXEVENTQUEUE
    ::select_startevents();//initialize the select() implementation of the event queue
#endif
    
    //start the server
    QTSSDictionaryMap::Initialize();
    QTSServerInterface::Initialize();// this must be called before constructing the server object
    sServer = NEW QTSServer();
    sServer->SetDebugLevel(debugLevel);
    sServer->SetDebugOptions(debugOptions);
    
    // re-parse config file
    inPrefsSource->Parse();

    Bool16 createListeners = true;
    if(qtssShuttingDownState == inInitialState) 
        createListeners = false;
    
    sServer->Initialize(inPrefsSource, inMessagesSource, inPortOverride,createListeners);

    if(inInitialState == qtssShuttingDownState)
    {  
        sServer->InitModules(inInitialState);
        return inInitialState;
    }
    
    OSCharArrayDeleter runGroupName(sServer->GetPrefs()->GetRunGroupName());
    OSCharArrayDeleter runUserName(sServer->GetPrefs()->GetRunUserName());
    OSThread::SetPersonality(runUserName.GetObject(), runGroupName.GetObject());

    if(sServer->GetServerState() != qtssFatalErrorState)
    {
        UInt32 numThreads = 0;
        
        if(OS::ThreadSafe())
        {
            numThreads = sServer->GetPrefs()->GetNumThreads(); // whatever the prefs say
            if(numThreads == 0)
                numThreads = OS::GetNumProcessors(); // 1 worker thread per processor
        }
        if(numThreads == 0)
            numThreads = 1;

		//fym CPU有几个核心即创建几个任务线程
        TaskThreadPool::AddThreads(numThreads);

    #if DEBUG
        qtss_printf("Number of task threads: %lu\n",numThreads);
    #endif
    
		//创建所有TimeoutTask对象共用的TimeoutTaskThread对象并调用signal方法将其加入到TaskThread中运行
        // Start up the server's global tasks, and start listening
        TimeoutTask::Initialize();     // The TimeoutTask mechanism is task based,
                                    // we therefore must do this after adding task threads
                                    // this be done before starting the sockets and server tasks
     }

    //Make sure to do this stuff last. Because these are all the threads that
    //do work in the server, this ensures that no work can go on while the server
    //is in the process of staring up
    if(sServer->GetServerState() != qtssFatalErrorState)
    {
		//创建一个TimeoutTaskThread类对象，实际上这个类的名字容易产生混淆，它并不是一个线程类，而是一个基于Task类的任务类。
		//因为前面已经在线程池里添加了一个任务线程，所以在这里调用signal的时候，就会找到这个线程
		//并把事件加入到这个线程的任务队列里，等待被处理。(这时，刚才创建的线程应该也在TaskThread::Entry函数里等待事件的发生)
        IdleTask::Initialize();

		//启动Socket类的sEventThread类所对应的线程。sEventThread类在Socket::Initialize函数里创建
		//到目前为止，这已是第三个启动的线程，分别是任务线程、空闲任务线程、事务线程。
        Socket::StartThread();
        OSThread::Sleep(1000);
        
        //
        // On Win32, in order to call modwatch the Socket EventQueue thread must be
        // created first. Modules call modwatch from their initializer, and we don't
        // want to prevent them from doing that, so module initialization is separated
        // out from other initialization, and we start the Socket EventQueue thread first.
        // The server is still prevented from doing anything as of yet, because there
        // aren't any TaskThreads yet.
        sServer->InitModules(inInitialState);

		//创建RTCPTask、RTPStatsUpdaterTask
		//Start listening，因为TCPListenerSocket是EventContext的继承类，所以这里实际上调用的是EventContext::RequestEvent().
        sServer->StartTasks();

		//针对系统的每一个ip地址，都创建并绑定一个socket端口对（分别用于RTP data发送和RTCP data接收），并申请对这两个socket端口的监听。
		//注意调用CreateUDPSocketPair函数传进去的Port参数为0，所以在播放静态多媒体文件时
		//不论是同一个媒体文件的音频、视频流还是同时播放的多个媒体文件，都是这两个socket端口来完成RTCP、RTP数据的处理。
        //fym for leak sServer->SetupUDPSockets(); // udp sockets are set up after the rtcp task is instantiated

        theServerState = sServer->GetServerState();
    }

    if(theServerState != qtssFatalErrorState)
    {
        CleanPid(true);
        WritePid(!inDontFork);

        doneStartingUp = true;
        qtss_printf("Streaming Server done starting up\n");
        OSMemory::SetMemoryError(ENOMEM);
    }


    // SWITCH TO RUN USER AND GROUP ID
	//执行setgid、setuid函数
    if(!sServer->SwitchPersonality())
        theServerState = qtssFatalErrorState;

   //
    // Tell the caller whether the server started up or not
    return theServerState;
}

void WritePid(Bool16 forked)
{
#ifndef __Win32__
    // WRITE PID TO FILE
    OSCharArrayDeleter thePidFileName(sServer->GetPrefs()->GetPidFilePath());
    FILE *thePidFile = fopen(thePidFileName, "w");
    if(thePidFile)
    {
        if(!forked)
            fprintf(thePidFile,"%d\n",getpid());    // write own pid
        else
        {
            fprintf(thePidFile,"%d\n",getppid());    // write parent pid
            fprintf(thePidFile,"%d\n",getpid());    // and our own pid in the next line
        }                
        fclose(thePidFile);
        sHasPID = true;
    }
#endif
}

void CleanPid(Bool16 force)
{
#ifndef __Win32__
    if(sHasPID || force)
    {
        OSCharArrayDeleter thePidFileName(sServer->GetPrefs()->GetPidFilePath());
        unlink(thePidFileName);
    }
#endif
}
void LogStatus(QTSS_ServerState theServerState)
{
    static QTSS_ServerState lastServerState = 0;
    static char *sPLISTHeader[] =
    {     "<?xml version=\"1.0\" encoding=\"UTF-8\"?>",
#if __MacOSX__
        "<!DOCTYPE plist SYSTEM \"file://localhost/System/Library/DTDs/PropertyList.dtd\">",
#else
        "<!ENTITY % plistObject \"(array | data | date | dict | real | integer | string | true | false )\">",
        "<!ELEMENT plist %plistObject;>",
        "<!ATTLIST plist version CDATA \"0.9\">",
        "",
        "<!-- Collections -->",
        "<!ELEMENT array (%plistObject;)*>",
        "<!ELEMENT dict (key, %plistObject;)*>",
        "<!ELEMENT key (#PCDATA)>",
        "",
        "<!--- Primitive types -->",
        "<!ELEMENT string (#PCDATA)>",
        "<!ELEMENT data (#PCDATA)> <!-- Contents interpreted as Base-64 encoded -->",
        "<!ELEMENT date (#PCDATA)> <!-- Contents should conform to a subset of ISO 8601 (in particular, YYYY '-' MM '-' DD 'T' HH ':' MM ':' SS 'Z'.  Smaller units may be omitted with a loss of precision) -->",
        "",
        "<!-- Numerical primitives -->",
        "<!ELEMENT true EMPTY>  <!-- Boolean constant true -->",
        "<!ELEMENT false EMPTY> <!-- Boolean constant false -->",
        "<!ELEMENT real (#PCDATA)> <!-- Contents should represent a floating point number matching (\"+\" | \"-\")? d+ (\".\"d*)? (\"E\" (\"+\" | \"-\") d+)? where d is a digit 0-9.  -->",
        "<!ELEMENT integer (#PCDATA)> <!-- Contents should represent a (possibly signed) integer number in base 10 -->",
        "]>",
#endif
    };

    static int numHeaderLines = sizeof(sPLISTHeader) / sizeof(char*);

    static char*    sPlistStart = "<plist version=\"0.9\">";
    static char*    sPlistEnd = "</plist>";
    static char*    sDictStart = "<dict>";
    static char*    sDictEnd = "</dict>";
    
    static char*    sKey    = "     <key>%s</key>\n";
    static char*    sValue  = "     <string>%s</string>\n";
    
    static char *sAttributes[] =
    {
        "qtssSvrServerName",
        "qtssSvrServerVersion",
        "qtssSvrServerBuild",
        "qtssSvrServerPlatform",
        "qtssSvrRTSPServerComment",
        "qtssSvrServerBuildDate",
        "qtssSvrStartupTime",
        "qtssSvrCurrentTimeMilliseconds",
        "qtssSvrCPULoadPercent",
         "qtssSvrState",
        "qtssRTPSvrCurConn",
        "qtssRTSPCurrentSessionCount",
        "qtssRTSPHTTPCurrentSessionCount",
        "qtssRTPSvrCurBandwidth",
        "qtssRTPSvrCurPackets",
        "qtssRTPSvrTotalConn",
        "qtssRTPSvrTotalBytes",
        "qtssMP3SvrCurConn",
        "qtssMP3SvrTotalConn",
        "qtssMP3SvrCurBandwidth",
        "qtssMP3SvrTotalBytes"
    };
    static int numAttributes = sizeof(sAttributes) / sizeof(char*);
        
    static StrPtrLen statsFileNameStr("server_status");    
    
    if(false == sServer->GetPrefs()->ServerStatFileEnabled())
        return;
        
    UInt32 interval = sServer->GetPrefs()->GetStatFileIntervalSec();
    if(interval == 0 || (OS::UnixTime_Secs() % interval) > 0 ) 
        return;
    
    // If the total number of RTSP sessions is 0  then we 
    // might not need to update the "server_status" file.
    char* thePrefStr = NULL;
    // We start lastRTSPSessionCount off with an impossible value so that
    // we force the "server_status" file to be written at least once.
    static int lastRTSPSessionCount = -1; 
    // Get the RTSP session count from the server.
    (void)QTSS_GetValueAsString(sServer, qtssRTSPCurrentSessionCount, 0, &thePrefStr);
    int currentRTSPSessionCount = ::atoi(thePrefStr);
    delete [] thePrefStr; thePrefStr = NULL;
    if(currentRTSPSessionCount == 0 && currentRTSPSessionCount == lastRTSPSessionCount)
    {
        // we don't need to update the "server_status" file except the
        // first time we are in the idle state.
        if(theServerState == qtssIdleState && lastServerState == qtssIdleState)
        {
            lastRTSPSessionCount = currentRTSPSessionCount;
            lastServerState = theServerState;
            return;
        }
    }
    else
    {
        // save the RTSP session count for the next time we execute.
        lastRTSPSessionCount = currentRTSPSessionCount;
    }

    StrPtrLenDel pathStr(sServer->GetPrefs()->GetErrorLogDir());
    StrPtrLenDel fileNameStr(sServer->GetPrefs()->GetStatsMonitorFileName());
    ResizeableStringFormatter pathBuffer(NULL,0);
    pathBuffer.PutFilePath(&pathStr,&fileNameStr);
    pathBuffer.PutTerminator();
    
    char*   filePath = pathBuffer.GetBufPtr();    
    FILE*   statusFile = ::fopen(filePath, "w");
    char*   theAttributeValue = NULL;
    int     i;
    
    if(statusFile != NULL)
    {
        ::chmod(filePath, 0640);
        for ( i = 0; i < numHeaderLines; i++)
        {    
            qtss_fprintf(statusFile, "%s\n",sPLISTHeader[i]);    
        }

        qtss_fprintf(statusFile, "%s\n", sPlistStart);
        qtss_fprintf(statusFile, "%s\n", sDictStart);    

          // show each element value
         for ( i = 0; i < numAttributes; i++)
        {
            (void)QTSS_GetValueAsString(sServer, QTSSModuleUtils::GetAttrID(sServer,sAttributes[i]), 0, &theAttributeValue);
            if(theAttributeValue != NULL)
             {
                qtss_fprintf(statusFile, sKey, sAttributes[i]);    
               qtss_fprintf(statusFile, sValue, theAttributeValue);    
                delete [] theAttributeValue;
                theAttributeValue = NULL;
             }
         }
                  
        qtss_fprintf(statusFile, "%s\n", sDictEnd);
        qtss_fprintf(statusFile, "%s\n\n", sPlistEnd);    
         
          ::fclose(statusFile);
    }
    lastServerState = theServerState;
}

void print_status(FILE* file, FILE* console, char* format, char* theStr)
{
    if(file) qtss_fprintf(file, format, theStr);
    if(console) qtss_fprintf(console, format, theStr);

}

void DebugLevel_1(FILE*   statusFile, FILE*   stdOut,  Bool16 printHeader )
{
    char*  thePrefStr = NULL;
    static char numStr[12] ="";
    static char dateStr[25] ="";
    UInt32 theLen = 0;

    if( printHeader )
    {                   
   
        print_status(statusFile,stdOut,"%s", "     RTP-Conns RTSP-Conns HTTP-Conns  kBits/Sec   Pkts/Sec   RTP-Playing   AvgDelay CurMaxDelay  MaxDelay  AvgQuality  NumThinned      Time\n");

    }
    
    (void)QTSS_GetValueAsString(sServer, qtssRTPSvrCurConn, 0, &thePrefStr);
    print_status(statusFile, stdOut,"%11s", thePrefStr);
    
    delete [] thePrefStr; thePrefStr = NULL;
    
    (void)QTSS_GetValueAsString(sServer, qtssRTSPCurrentSessionCount, 0, &thePrefStr);
    print_status(statusFile, stdOut,"%11s", thePrefStr);
    delete [] thePrefStr; thePrefStr = NULL;
    
    (void)QTSS_GetValueAsString(sServer, qtssRTSPHTTPCurrentSessionCount, 0, &thePrefStr);
    print_status(statusFile, stdOut,"%11s", thePrefStr);
    delete [] thePrefStr; thePrefStr = NULL;
    
    UInt32 curBandwidth = 0;
    theLen = sizeof(curBandwidth);
    (void)QTSS_GetValue(sServer, qtssRTPSvrCurBandwidth, 0, &curBandwidth, &theLen);
    qtss_snprintf(numStr, 11, "%lu", curBandwidth/1024);
    print_status(statusFile, stdOut,"%11s", numStr);

    (void)QTSS_GetValueAsString(sServer, qtssRTPSvrCurPackets, 0, &thePrefStr);
    print_status(statusFile, stdOut,"%11s", thePrefStr);
    delete [] thePrefStr; thePrefStr = NULL;


    UInt32 currentPlaying = sServer->GetNumRTPPlayingSessions();
    qtss_snprintf( numStr, sizeof(numStr) -1, "%lu", currentPlaying);
    print_status(statusFile, stdOut,"%14s", numStr);

   
    //is the server keeping up with the streams?
    //what quality are the streams?
    SInt64 totalRTPPaackets = sServer->GetTotalRTPPackets();
    SInt64 deltaPackets = totalRTPPaackets - sLastDebugPackets;
    sLastDebugPackets = totalRTPPaackets;

    SInt64 totalQuality = sServer->GetTotalQuality();
    SInt64 deltaQuality = totalQuality - sLastDebugTotalQuality;
    sLastDebugTotalQuality = totalQuality;

    SInt64 currentMaxLate =  sServer->GetCurrentMaxLate();
    SInt64 totalLate =  sServer->GetTotalLate();

    sServer->ClearTotalLate();
    sServer->ClearCurrentMaxLate();
    sServer->ClearTotalQuality();
    
    ::qtss_snprintf(numStr, sizeof(numStr) -1, "%s", "0");
    if(deltaPackets > 0)
        qtss_snprintf(numStr, sizeof(numStr) -1, "%ld", (SInt32) ((SInt64)totalLate /  (SInt64) deltaPackets ));
    print_status(statusFile, stdOut,"%11s", numStr);

    qtss_snprintf(numStr,sizeof(numStr) -1, "%ld", (SInt32) currentMaxLate);
    print_status(statusFile, stdOut,"%11s", numStr);
    
    qtss_snprintf(numStr,sizeof(numStr) -1, "%ld", (SInt32)  sServer->GetMaxLate() );
    print_status(statusFile, stdOut,"%11s", numStr);

    ::qtss_snprintf(numStr, sizeof(numStr) -1, "%s", "0");
    if(deltaPackets > 0)
        qtss_snprintf(numStr, sizeof(numStr) -1, "%ld", (SInt32) ((SInt64) deltaQuality /  (SInt64) deltaPackets));
    print_status(statusFile, stdOut,"%11s", numStr);

    qtss_snprintf(numStr,sizeof(numStr) -1, "%ld", (SInt32) sServer->GetNumThinned() );
    print_status(statusFile, stdOut,"%11s", numStr);

    
    
    char theDateBuffer[QTSSRollingLog::kMaxDateBufferSizeInBytes];
    (void) QTSSRollingLog::FormatDate(theDateBuffer, false);
    
    qtss_snprintf(dateStr,sizeof(dateStr) -1, "%s", theDateBuffer );
    print_status(statusFile, stdOut,"%24s\n", dateStr);
}

FILE* LogDebugEnabled()
{

    if(DebugLogOn(sServer))
    {
        static StrPtrLen statsFileNameStr("server_debug_status");    
    
        StrPtrLenDel pathStr(sServer->GetPrefs()->GetErrorLogDir());
        ResizeableStringFormatter pathBuffer(NULL,0);
        pathBuffer.PutFilePath(&pathStr,&statsFileNameStr);
        pathBuffer.PutTerminator();
        
        char*   filePath = pathBuffer.GetBufPtr();    
        return ::fopen(filePath, "a");
    }
    
    return NULL;
}


FILE* DisplayDebugEnabled()
{        
    return ( DebugDisplayOn(sServer) ) ? stdout   : NULL ;
}


void DebugStatus(UInt32 debugLevel, Bool16 printHeader)
{
        
    FILE*   statusFile = LogDebugEnabled();
    FILE*   stdOut = DisplayDebugEnabled();
    
    if(debugLevel > 0)
        DebugLevel_1(statusFile, stdOut, printHeader);

    if(statusFile) 
        ::fclose(statusFile);
}

void FormattedTotalBytesBuffer(char *outBuffer, int outBufferLen, UInt64 totalBytes)
{
    Float32 displayBytes = 0.0;
    char  sizeStr[] = "B";
    char* format = NULL;
        
    if(totalBytes > 1073741824 ) //GBytes
    {   displayBytes = (Float32) ( (Float64) (SInt64) totalBytes /  (Float64) (SInt64) 1073741824 );
        sizeStr[0] = 'G';
        format = "%.4f%s ";
     }
    else if(totalBytes > 1048576 ) //MBytes
    {   displayBytes = (Float32) (SInt32) totalBytes /  (Float32) (SInt32) 1048576;
        sizeStr[0] = 'M';
        format = "%.3f%s ";
     }
    else if(totalBytes > 1024 ) //KBytes
    {    displayBytes = (Float32) (SInt32) totalBytes /  (Float32) (SInt32) 1024;
         sizeStr[0] = 'K';
         format = "%.2f%s ";
    }
    else
    {    displayBytes = (Float32) (SInt32) totalBytes;  //Bytes
         sizeStr[0] = 'B';
         format = "%4.0f%s ";
    }
    
    outBuffer[outBufferLen -1] = 0;
    qtss_snprintf(outBuffer, outBufferLen -1,  format , displayBytes, sizeStr);
}

#if 0
void PrintStatus(Bool16 printHeader)
{
	char* thePrefStr = NULL;
	UInt32 theLen = 0;

	if( printHeader )
	{                       
		qtss_printf("     RTP-Conns RTSP-Conns HTTP-Conns  kBits/Sec   Pkts/Sec    TotConn     TotBytes   TotPktsLost          Time\n");   
	}

	(void)QTSS_GetValueAsString(sServer, qtssRTPSvrCurConn, 0, &thePrefStr);
	qtss_printf( "%11s", thePrefStr);
	delete [] thePrefStr; thePrefStr = NULL;

	(void)QTSS_GetValueAsString(sServer, qtssRTSPCurrentSessionCount, 0, &thePrefStr);
	qtss_printf( "%11s", thePrefStr);
	delete [] thePrefStr; thePrefStr = NULL;

	(void)QTSS_GetValueAsString(sServer, qtssRTSPHTTPCurrentSessionCount, 0, &thePrefStr);
	qtss_printf( "%11s", thePrefStr);
	delete [] thePrefStr; thePrefStr = NULL;

	UInt32 curBandwidth = 0;
	theLen = sizeof(curBandwidth);
	(void)QTSS_GetValue(sServer, qtssRTPSvrCurBandwidth, 0, &curBandwidth, &theLen);
	qtss_printf( "%11lu", curBandwidth/1024);

	(void)QTSS_GetValueAsString(sServer, qtssRTPSvrCurPackets, 0, &thePrefStr);
	qtss_printf( "%11s", thePrefStr);
	delete [] thePrefStr; thePrefStr = NULL;

	(void)QTSS_GetValueAsString(sServer, qtssRTPSvrTotalConn, 0, &thePrefStr);
	qtss_printf( "%11s", thePrefStr);
	delete [] thePrefStr; thePrefStr = NULL;

	UInt64 totalBytes = sServer->GetTotalRTPBytes();
	char  displayBuff[32] = "";
	FormattedTotalBytesBuffer(displayBuff, sizeof(displayBuff),totalBytes);
	qtss_printf( "%17s", displayBuff);

	qtss_printf( "%11"_64BITARG_"u", sServer->GetTotalRTPPacketsLost());

	char theDateBuffer[QTSSRollingLog::kMaxDateBufferSizeInBytes];
	(void) QTSSRollingLog::FormatDate(theDateBuffer, false);
	qtss_printf( "%25s",theDateBuffer);

	qtss_printf( "\n");

}
#else
void PrintStatus(Bool16 printHeader)
{
	//return;//fym

	char* thePrefStr = NULL;
	UInt32 theLen = 0;

	/*if( printHeader )
	{                       
		qtss_printf("RTP-Conns RTSP-Conns HTTP-Conns kBits/Seca Pkts/Sec TotConn TotBytes TotPktsLost\n");
	}*/

	qtss_printf("\n------------------------------------------------------------------\n");

	//RTP-Conns
	(void)QTSS_GetValueAsString(sServer, qtssRTPSvrCurConn, 0, &thePrefStr);
	qtss_printf( "RTP-Conns %s, ", thePrefStr);
	delete [] thePrefStr; thePrefStr = NULL;

	//RTSP-Conns
	(void)QTSS_GetValueAsString(sServer, qtssRTSPCurrentSessionCount, 0, &thePrefStr);
	qtss_printf( "RTSP-Conns %s, ", thePrefStr);
	delete [] thePrefStr; thePrefStr = NULL;

	//HTTP-Conns
	(void)QTSS_GetValueAsString(sServer, qtssRTSPHTTPCurrentSessionCount, 0, &thePrefStr);
	qtss_printf( "HTTP-Conns %s, ", thePrefStr);
	delete [] thePrefStr; thePrefStr = NULL;

	//TotConn
	(void)QTSS_GetValueAsString(sServer, qtssRTPSvrTotalConn, 0, &thePrefStr);
	qtss_printf( "TotConn %s\n", thePrefStr);
	delete [] thePrefStr; thePrefStr = NULL;

	//kBits/Seca
	UInt32 curBandwidth = 0;
	theLen = sizeof(curBandwidth);
	(void)QTSS_GetValue(sServer, qtssRTPSvrCurBandwidth, 0, &curBandwidth, &theLen);
	qtss_printf( "Bitrate %u kbps, ", curBandwidth/1024);

	//Pkts/Sec
	(void)QTSS_GetValueAsString(sServer, qtssRTPSvrCurPackets, 0, &thePrefStr);
	qtss_printf( "Pktrate %s pktps, ", thePrefStr);
	delete [] thePrefStr; thePrefStr = NULL;
	

	//TotBytes
	UInt64 totalBytes = sServer->GetTotalRTPBytes();
	char  displayBuff[32] = "";
	FormattedTotalBytesBuffer(displayBuff, sizeof(displayBuff),totalBytes);
	qtss_printf( "TotBytes %s\n", displayBuff);

	//TotPktsLost
	qtss_printf( "TotPktsLost %8"_64BITARG_"u\n", sServer->GetTotalRTPPacketsLost());

	qtss_printf( "\n");

}
#endif

Bool16 PrintHeader(UInt32 loopCount)
{
     return ( (loopCount % (sStatusUpdateInterval * 10) ) == 0 ) ? true : false;
}

Bool16 PrintLine(UInt32 loopCount)
{
     return ( (loopCount % sStatusUpdateInterval) == 0 ) ? true : false;
}

//fym
void RTSP_AddRelaySource(UInt16 uuid)
{
	/*if(0 > uuid)
		return;

	QTSS_RoleParams relay_source_params;

	relay_source_params.relay_stream_data.in_relay_source_uuid = uuid;
	relay_source_params.relay_stream_data.in_packet_data = NULL;
	relay_source_params.relay_stream_data.in_packet_len = 0;
	relay_source_params.relay_stream_data.in_packet_type = 0;

	//肯定增加了QTSS_AddRelaySource_Role角色的module只有一个，所以直接用0
	QTSSModule* theModule = QTSServerInterface::GetModule(QTSSModule::kAddRelaySource, 0);
	if(NULL != theModule)
	{
		(void)theModule->CallDispatch(QTSS_AddRelaySource_Role, &relay_source_params);
	}*/
}

//fym
void RTSP_RemoveRelaySource(UInt16 uuid)
{
	/*if(0 > uuid)
		return;

	QTSS_RoleParams relay_source_params;

	relay_source_params.relay_stream_data.in_relay_source_uuid = uuid;
	relay_source_params.relay_stream_data.in_packet_data = NULL;
	relay_source_params.relay_stream_data.in_packet_len = 0;
	relay_source_params.relay_stream_data.in_packet_type = 0;

	//肯定增加了QTSS_RemoveRelaySource_Role角色的module只有一个，所以直接用0
	QTSSModule*  theModule = QTSServerInterface::GetModule(QTSSModule::kRemoveRelaySource, 0);
	if(NULL != theModule)
	{
		(void)theModule->CallDispatch(QTSS_RemoveRelaySource_Role, &relay_source_params);
	}*/

}

//fym
void RTSP_InputStreamData(UInt16 uuid, void* pData, UInt32 nLen, UInt16 nDataType)
{
	if(0 > uuid || !nLen || NULL == pData || 0 > nDataType)
		return;

	QTSS_RoleParams relay_source_params;

	relay_source_params.relay_stream_data.in_relay_source_uuid = uuid;
	relay_source_params.relay_stream_data.in_packet_data = (char*)pData;
	relay_source_params.relay_stream_data.in_packet_len = nLen;
	relay_source_params.relay_stream_data.in_packet_type = nDataType;
	//relay_source_params.relay_stream_data.in_time_stamp_ssrc = llTimeStampSSRC;

	//肯定增加了QTSS_AddRelaySource_Role角色的module只有一个，所以直接用0
	QTSSModule* theModule = QTSServerInterface::GetModule(QTSSModule::kInputStreamData, 0);
	if(NULL != theModule)
	{
		(void)theModule->CallDispatch(QTSS_InputStreamData_Role, &relay_source_params);
	}
}

//#define _BY_HEAP_
void RunServer()
{   
    Bool16 restartServer = false;
    UInt32 loopCount = 0;
    UInt32 debugLevel = 0;
    Bool16 printHeader = false;
    Bool16 printStatus = true;

	//////////////////////////////////////////////////////////////////////////
	UInt16 i = 0;
	struct stat file_stat;
	char CSRC[] = "RTSP Test!";

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
	
#ifndef _BY_HEAP_
	char content[1000];
	//ZeroMemory(content, sizeof(content));
	//::memset(content, 'A', sizeof(content));

	unsigned short print_status = 0;

	unsigned short num = 200;

	unsigned short sequence[200];

	while(1)
	{
		for(unsigned short i = 0; i < num; ++i)
		{
			::memset(content, i, sizeof(content));

			RTPHeader* header = reinterpret_cast<RTPHeader*>(content);
			header->csrccount = 0;
			header->extension = 0;
			header->padding = 0;
			header->version = 2;
			header->payloadtype = 0;
			header->marker = 1;
			header->sequencenumber = htons(sequence[i]++);;
			header->timestamp = htonl(timeGetTime());
			header->ssrc = htonl(i);

			RTSP_InputStreamData(i, content, sizeof(content), 0);//fym

			header->payloadtype = 1;
			RTSP_InputStreamData(i, content, sizeof(content), 1);//fym
		}

		OSThread::Sleep(50);

		if(!(print_status++ % 100))
			PrintStatus(true);
	}

#endif

#if 0//play hik.mp4
	FILE* theFile = ::fopen("hik.mp4", "rb");//open

	if(NULL == theFile)
	{
		qtss_printf("\nCannot open file!");
		return;
	}

	UInt32 packet_flag = 0;
	UInt16 packet_len = 0;
	UInt16 packet_mark = 0;
	UInt32 read_len = 0;

	ZeroMemory(&file_stat, sizeof(file_stat));
	if(0 > ::stat("hik.mp4", &file_stat))
		return;

	while(1)
	{
		OSThread::Sleep(20);

		if(read_len >= file_stat.st_size)
		{
			::fseek(theFile, 0, SEEK_SET);
			read_len = 0;
			//qtss_printf("\nrepeat reading file!");
		}

		::fread(&packet_flag, sizeof(UInt32), 1, theFile);
		read_len += sizeof(UInt32);
		::fread(&packet_len, sizeof(UInt16), 1, theFile);
		read_len += sizeof(UInt16);

		//qtss_printf("\n%u, %u", packet_len, packet_flag);

		if(0xffffffff != packet_flag)
		{
			qtss_printf("\nread packet header error!");
			return;
		}

		::fseek(theFile, 18, SEEK_CUR);
		read_len += 18;

#ifdef _BY_HEAP_
		char* content = NEW char[packet_len + 12];
		ZeroMemory(content, packet_len + 12);
#else
		//ZeroMemory(content, sizeof(content));
#endif

		if(packet_len != ::fread(content, 1, packet_len, theFile))
		{
			qtss_printf("\nread packet content error!");
			return;
		}

		read_len += packet_len;

		for(i = 1; i <= 200; ++i)
		{
			RTSP_InputStreamData(i, content, packet_len, 0);//fym
		}
	}
#else//play vd.mp4
	FILE* theFile = ::fopen("vd.mp4", "rb");//open

	if(NULL == theFile)
	{
		qtss_printf("\nCannot open file!");
		return;
	}

	UInt32 packet_len = 0;
	UInt32 read_len = 0;

	ZeroMemory(&file_stat, sizeof(file_stat));
	if(0 > ::stat("vd.mp4", &file_stat))
		return;


	UInt32 theTimer = 0;

	UInt32 theCount = 0;

	while(1)
	{
		OSThread::Sleep(10);

		//fym
		if(200 == theTimer++)
		{
			PrintStatus(true);
			theTimer = 0;
		}

		if(read_len >= file_stat.st_size)
		{
			::fseek(theFile, 0, SEEK_SET);
			read_len = 0;
			//qtss_printf("\nrepeat reading file!");
		}

		::fread(&packet_len, sizeof(UInt32), 1, theFile);
		read_len += sizeof(UInt32);

		//qtss_printf("\n%u", packet_len);

#ifdef _BY_HEAP_
		char* content = NEW char[packet_len + 12];
		ZeroMemory(content, packet_len + 12);
#else
		//ZeroMemory(content, sizeof(content));
#endif

		if(packet_len != ::fread(content, 1, packet_len, theFile))
		{
			qtss_printf("\nread packet content error!");
			return;
		}

		read_len += packet_len;

		for(i = 1; i <= 200; ++i)
		{
			RTSP_InputStreamData(i, content, packet_len, 0);//fym
		}
	}
#endif
	//////////////////////////////////////////////////////////////////////////

    //just wait until someone stops the server or a fatal error occurs.
    QTSS_ServerState theServerState = sServer->GetServerState();
#if 0//fym
    while ((theServerState != qtssShuttingDownState) &&
            (theServerState != qtssFatalErrorState))
    {
#ifdef __sgi__
        OSThread::Sleep(999);
#else
        OSThread::Sleep(1000);//fym 1000);
#endif

        LogStatus(theServerState);

        if(sStatusUpdateInterval)
        {
            debugLevel = sServer->GetDebugLevel();             
            printHeader = PrintHeader(loopCount);
            printStatus = PrintLine(loopCount);
                
            if(true)//fym printStatus)
            {
                if  (DebugOn(sServer) ) // debug level display or logging is on
                    DebugStatus(debugLevel, printHeader);
                
                if(!DebugDisplayOn(sServer))
                    PrintStatus(printHeader); // default status output
            }
            
            
            loopCount++;

        }
        
        if((sServer->SigIntSet()) || (sServer->SigTermSet()))
        {
            //
            // start the shutdown process
            theServerState = qtssShuttingDownState;
            (void)QTSS_SetValue(QTSServerInterface::GetServer(), qtssSvrState, 0, &theServerState, sizeof(theServerState));

            if(sServer->SigIntSet())
                restartServer = true;
        }
        
        theServerState = sServer->GetServerState();
        if(theServerState == qtssIdleState)
            sServer->KillAllRTPSessions();
    }
#endif//fym
    
    //
    // Kill all the sessions and wait for them to die,
    // but don't wait more than 5 seconds
    sServer->KillAllRTPSessions();
    for (UInt32 shutdownWaitCount = 0; (sServer->GetNumRTPSessions() > 0) && (shutdownWaitCount < 5); shutdownWaitCount++)
        OSThread::Sleep(1000);
        
    //Now, make sure that the server can't do any work
    TaskThreadPool::RemoveThreads();
    
    //now that the server is definitely stopped, it is safe to initate
    //the shutdown process
    delete sServer;
    
    CleanPid(false);
    //ok, we're ready to exit. If we're quitting because of some fatal error
    //while running the server, make sure to let the parent process know by
    //exiting with a nonzero status. Otherwise, exit with a 0 status
    if(theServerState == qtssFatalErrorState || restartServer)
        ::exit (-2);//-2 signals parent process to restart server
}
