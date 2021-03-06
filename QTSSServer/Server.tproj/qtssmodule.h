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
    Contains:   This object represents a single QTSS API compliant module.
                A module may either be compiled directly into the server,
                or loaded from a code fragment residing on the disk.
                
                Object does the loading and initialization of a module, and
                stores all per-module data.
                    

*/

#ifndef __QTSSMODULE_H__
#define __QTSSMODULE_H__

#include "QTSS.h"
#include "QTSS_Private.h"
#include "QTSSDictionary.h"
#include "Task.h"
#include "QTSSPrefs.h"

#include "OSCodeFragment.h"
#include "OSQueue.h"
#include "StrPtrLen.h"

class QTSSModule : public QTSSDictionary, public Task
{
    public:
    
        //
        // INITIALIZE
        static void     Initialize();
    
        // CONSTRUCTOR / SETUP / DESTRUCTOR
        
        // Specify the path to the code fragment if this module
        // is to be loaded from disk. If it is loaded from disk, the
        // name of the module will be its file name. Otherwise, the
        // inName parameter will set it.

        QTSSModule(char* inName, char* inPath = NULL);

        // This function does all the module setup. If the module is being
        // loaded from disk, you need not pass in a main entrypoint (as
        // it will be grabbed from the fragment). Otherwise, you must pass
        // in a main entrypoint.
        //
        // Note that this function does not invoke any public module roles.
        QTSS_Error  SetupModule(QTSS_CallbacksPtr inCallbacks, QTSS_MainEntryPointPtr inEntrypoint = NULL);

        // Doesn't free up internally allocated stuff
        virtual ~QTSSModule(){}
        
        //
        // MODIFIERS
        void            SetPrefsDict(QTSSPrefs* inPrefs) { fPrefs = inPrefs; }
        void            SetAttributesDict(QTSSDictionary* inAttributes) { fAttributes = inAttributes; }
        //
        // ACCESSORS
        
        OSQueueElem*    GetQueueElem()  { return &fQueueElem; }
        Bool16          IsInitialized() { return fDispatchFunc != NULL; }
        QTSSPrefs*      GetPrefsDict()  { return fPrefs; }
        QTSSDictionary* GetAttributesDict() { return fAttributes; }
        OSMutex*        GetAttributesMutex() { return &fAttributesMutex; }
        
        // This calls into the module.
        QTSS_Error  CallDispatch(QTSS_Role inRole, QTSS_RoleParamPtr inParams)
            {  return (fDispatchFunc)(inRole, inParams);    }
        

        // These enums allow roles to be stored in a more optimized way
                
        enum
        {
            kInitializeRole =           0,
            kShutdownRole =             1,
            kRTSPFilterRole =           2,
            kRTSPRouteRole =            3,
            kRTSPAthnRole =             4,          
            kRTSPAuthRole =             5,
            kRTSPPreProcessorRole =     6,
            kRTSPRequestRole =          7,
            kRTSPPostProcessorRole =    8,
            kRTSPSessionClosingRole =   9,
            kRTPSendPacketsRole =       10,
            kClientSessionClosingRole = 11,
            kRTCPProcessRole =          12,
            kErrorLogRole =             13,
            kRereadPrefsRole =          14,
            kOpenFileRole =             15,
            kOpenFilePreProcessRole =   16,
            kAdviseFileRole =           17,
            kReadFileRole =             18,
            kCloseFileRole =            19,
            kRequestEventFileRole =     20,
            kRTSPIncomingDataRole =     21,
            kStateChangeRole =          22,
            kTimedIntervalRole =        23,
			kAddRelaySource =			24,//fym
			kRemoveRelaySource =		25,//fym
			kInputStreamData =	26,//fym
            
            kNumRoles =                 27//fym 24
        };
        typedef UInt32 RoleIndex;
        
        // Call this to activate this module in the specified role.
        QTSS_Error  AddRole(QTSS_Role inRole);
        
        // This returns true if this module is supposed to run in the specified role.
        Bool16  RunsInRole(RoleIndex inIndex) { Assert(inIndex < kNumRoles); return fRoleArray[inIndex]; }
        
        SInt64 Run();
        
        QTSS_ModuleState* GetModuleState() { return &fModuleState;}
        
    private:
    
        QTSS_Error LoadFromDisk(QTSS_MainEntryPointPtr* outEntrypoint);

        OSQueueElem                 fQueueElem;
        char*                       fPath;
        OSCodeFragment*             fFragment;
        QTSS_DispatchFuncPtr        fDispatchFunc;
        Bool16                      fRoleArray[kNumRoles];
        QTSSPrefs*                  fPrefs;
        QTSSDictionary*             fAttributes;
        OSMutex                     fAttributesMutex;   

        static Bool16       sHasRTSPRequestModule;
        static Bool16       sHasOpenFileModule;
        static Bool16       sHasRTSPAuthenticateModule;
    
        static QTSSAttrInfoDict::AttrInfo   sAttributes[];
        
        QTSS_ModuleState    fModuleState;

};



#endif //__QTSSMODULE_H__
