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

//fym 队列成员类，成员实际是一个void*指针
class OSQueueElem {
    public:
        OSQueueElem(void* enclosingObject = NULL) : fNext(NULL), fPrev(NULL), fQueue(NULL),
                                                    fEnclosingObject(enclosingObject) {}
        virtual ~OSQueueElem() { Assert(fQueue == NULL); }

        Bool16 IsMember(const OSQueue& queue) { return (&queue == fQueue); }//该成员是否从属于队列queue
        Bool16 IsMemberOfAnyQueue()     { return fQueue != NULL; }//该成员是否从属于某一（而不是“任何”）队列
        void* GetEnclosingObject()  { return fEnclosingObject; }//获取该成员实际指向的对象指针
        void SetEnclosingObject(void* obj) { fEnclosingObject = obj; }//向该成员实际指向的对象指针赋值

        OSQueueElem* Next() { return fNext; }
        OSQueueElem* Prev() { return fPrev; }
        OSQueue* InQueue()  { return fQueue; }//获取该成员所在队列的指针
        inline void Remove();//调用该成员所在队列的remove函数，将该成员从该队列中删除,但不修改该成员存储的对象

    private:

        OSQueueElem*    fNext;
        OSQueueElem*    fPrev;
        OSQueue *       fQueue;//该成员所在队列的指针，如果该指针不为空，表明该成员已加入某一队列，否则为散兵游勇
        void*           fEnclosingObject;//改成员实际指向的对象指针

        friend class    OSQueue;
};

//fym 队列类
class OSQueue {
    public:
        OSQueue();
        ~OSQueue() {}

		//插入规律,新插入的成员成为哨兵(fSentinel)的next，并为队列原第一个成员的prev（同时新成员的next是原第一个成员）
		//哨兵的next永远是队列中第一个成员
		//注意!如果该成员已有所属的队列(fQueue!=NULL)，则不插入
		//新插入的成员是队列tail，最先插入的是队列head,换言之，哨兵的next永远指向tail，prev永远指向head
		//注意队列的顺序是,新成员的next是旧成员，旧成员的prev是新成员，head为旧，tail为新
        void            EnQueue(OSQueueElem* object);
		//取出队列head的成员，并将该成员的fQueue置空
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

        OSQueueElem     fSentinel;//岗哨？
        UInt32          fLength;
		//fym OSMutex             fMutex;//fym
};

//遍历队列
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
		//遍历队列，遍历顺序为从旧至新，即从head向tail
        void            Next();
        
        Bool16          IsDone() { return fCurrentElemP == NULL; }
        
    private:
    
        OSQueue*        fQueueP;
        OSQueueElem*    fCurrentElemP;
};

//加互斥锁保护的队列
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
