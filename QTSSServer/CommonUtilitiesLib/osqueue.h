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
    File:       OSQueue.h

    Contains:   implements OSQueue class
                    
    
*/

#ifndef _OSQUEUE_H_
#define _OSQUEUE_H_

#include "MyAssert.h"
#include "OSHeaders.h"
#include "OSMutex.h"
#include "OSCond.h"
#include "OSThread.h"

#define OSQUEUETESTING 0

class OSQueue;

//fym ���г�Ա�࣬��Աʵ����һ��void*ָ��
class OSQueueElem {
    public:
        OSQueueElem(void* enclosingObject = NULL) : fNext(NULL), fPrev(NULL), fQueue(NULL),
                                                    fEnclosingObject(enclosingObject) {}
        virtual ~OSQueueElem() { Assert(fQueue == NULL); }

        Bool16 IsMember(const OSQueue& queue) { return (&queue == fQueue); }//�ó�Ա�Ƿ�����ڶ���queue
        Bool16 IsMemberOfAnyQueue()     { return fQueue != NULL; }//�ó�Ա�Ƿ������ĳһ�������ǡ��κΡ�������
        void* GetEnclosingObject()  { return fEnclosingObject; }//��ȡ�ó�Աʵ��ָ��Ķ���ָ��
        void SetEnclosingObject(void* obj) { fEnclosingObject = obj; }//��ó�Աʵ��ָ��Ķ���ָ�븳ֵ

        OSQueueElem* Next() { return fNext; }
        OSQueueElem* Prev() { return fPrev; }
        OSQueue* InQueue()  { return fQueue; }//��ȡ�ó�Ա���ڶ��е�ָ��
        inline void Remove();//���øó�Ա���ڶ��е�remove���������ó�Ա�Ӹö�����ɾ��,�����޸ĸó�Ա�洢�Ķ���

    private:

        OSQueueElem*    fNext;
        OSQueueElem*    fPrev;
        OSQueue *       fQueue;//�ó�Ա���ڶ��е�ָ�룬�����ָ�벻Ϊ�գ������ó�Ա�Ѽ���ĳһ���У�����Ϊɢ������
        void*           fEnclosingObject;//�ĳ�Աʵ��ָ��Ķ���ָ��

        friend class    OSQueue;
};

//fym ������
class OSQueue {
    public:
        OSQueue();
        ~OSQueue() {}

		//�������,�²���ĳ�Ա��Ϊ�ڱ�(fSentinel)��next����Ϊ����ԭ��һ����Ա��prev��ͬʱ�³�Ա��next��ԭ��һ����Ա��
		//�ڱ���next��Զ�Ƕ����е�һ����Ա
		//ע��!����ó�Ա���������Ķ���(fQueue!=NULL)���򲻲���
		//�²���ĳ�Ա�Ƕ���tail�����Ȳ�����Ƕ���head,����֮���ڱ���next��Զָ��tail��prev��Զָ��head
		//ע����е�˳����,�³�Ա��next�Ǿɳ�Ա���ɳ�Ա��prev���³�Ա��headΪ�ɣ�tailΪ��
        void            EnQueue(OSQueueElem* object);
		//ȡ������head�ĳ�Ա�������ó�Ա��fQueue�ÿ�
        OSQueueElem*    DeQueue();

        OSQueueElem*    GetHead() { /*fym OSMutexLocker theLocker(&fMutex);/*fym*/ if (fLength > 0) return fSentinel.fPrev; return NULL; }
        OSQueueElem*    GetTail() { /*fym OSMutexLocker theLocker(&fMutex);/*fym*/ if (fLength > 0) return fSentinel.fNext; return NULL; }
        UInt32          GetLength() { /*fym OSMutexLocker theLocker(&fMutex);/*fym*/ return fLength; }
        
        void            Remove(OSQueueElem* object);

		//fym OSMutex*		GetMutex() { return &fMutex; }//fym

		void			Clear();//fym

#if OSQUEUETESTING
        static Bool16       Test();
#endif

    protected:

        OSQueueElem     fSentinel;//���ڣ�
        UInt32          fLength;
		//fym OSMutex             fMutex;//fym
};

//��������
class OSQueueIter
{
    public:
        OSQueueIter(OSQueue* inQueue) : fQueueP(inQueue), fCurrentElemP(inQueue->GetHead()) {}

        OSQueueIter(OSQueue* inQueue, OSQueueElem* startElemP ) : fQueueP(inQueue)
        {
                if ( startElemP )
                {   Assert( startElemP->IsMember(*inQueue ) );
                    fCurrentElemP = startElemP;
                
                }
                else
                    fCurrentElemP = NULL;
        }

        ~OSQueueIter() {}
        
        void            Reset() { fCurrentElemP = fQueueP->GetHead(); }
        
        OSQueueElem*    GetCurrent() { return fCurrentElemP; }
		//�������У�����˳��Ϊ�Ӿ����£�����head��tail
        void            Next();
        
        Bool16          IsDone() { return fCurrentElemP == NULL; }
        
    private:
    
        OSQueue*        fQueueP;
        OSQueueElem*    fCurrentElemP;
};

//�ӻ����������Ķ���
class OSQueue_Blocking
{
    public:
        OSQueue_Blocking() {}
        ~OSQueue_Blocking() {}
        
        OSQueueElem*    DeQueueBlocking(OSThread* inCurThread, SInt32 inTimeoutInMilSecs);
        OSQueueElem*    DeQueue();//will not block
        void            EnQueue(OSQueueElem* obj);
        
        OSCond*         GetCond()   { return &fCond; }
        OSQueue*        GetQueue()  { return &fQueue; }
        
    private:

        OSCond              fCond;
        OSMutex             fMutex;
        OSQueue             fQueue;
};


void    OSQueueElem::Remove()
{
    if (fQueue != NULL)
        fQueue->Remove(this);
}
#endif //_OSQUEUE_H_
