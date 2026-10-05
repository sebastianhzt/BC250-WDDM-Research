/* SPDX-License-Identifier: Apache-2.0 */
/* RAM fixture ONLY: one private request, CSQ-shaped serial arbitration.
 * Parent/module/request/envelope storage externally anchored through ALL
 * attempts and returns. Fake resource receipts are NOT allocations or OS refs.
 * No actual CSQ/IRP/queue/cancel/forward; frozen Life native remains FALSE. */
#define BC250_LIFE_MOCK 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../context-lifetime-20261004/bc250_context_lifetime.c"
BOOLEAN Bc250LifeMockAllowed=TRUE;
static unsigned checks,fixtures,inlineCancelled,emptyWorkers,workerWins,unwinds;
static unsigned observations,retiredInsideDrop,rejected;
static ULONG held;
static KIRQL irql;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL line %u: %s\n",(unsigned)__LINE__,#x);exit(1);}}while(0)
enum { DISPATCH_OWNS=1, CSQ_OWNS, CANCEL_OWNS, WORKER_OWNS, DEAD };
enum { PRIVATE_KIND=1, PNP_KIND, POWER_KIND, DIAGNOSTIC_KIND };
typedef struct REQUEST { ULONG Alive,Owner,Marked,Cancel,Value; } REQUEST;
typedef struct CHILD { ULONG Alive,Touches; } CHILD;
typedef struct ENVELOPE {
    ULONG Live,Ref,Tag;
    BC250_LIFE_TOKEN Hold;
    REQUEST *Owned; /* separately retained identity, never read after completion */
} ENVELOPE;
typedef struct PARENT {
    BC250_LIFE_DOMAIN Life;
    BC250_LIFE_TOKEN DispatchCall,WorkCall,RequestCall;
    ENVELOPE RequestEnvelope,WakeEnvelope;
    REQUEST *Slot;
    KSPIN_LOCK QueueLock;
    ULONG References,Tags,CompleteCount,FinalStatus,Allocations,Frees;
    ULONG Scheduled,WorkState,DispatchActive,WorkActive,CancelActive;
    ULONG InsertCalls;
    ULONG FailStage,CloseStage,Stage,CancelPoint,ClosePoint,InlineWork;
    ULONG Quarantine,Retired,HookRetire;
    CHILD *FixtureChild; /* TEST observer only; never production authorization */
} PARENT;
static PARENT *active;
static VOID Observe(PARENT *);
static VOID Retire(PARENT *);
unsigned char KeGetCurrentIrql(VOID){return irql;}
VOID KeInitializeSpinLock(KSPIN_LOCK *p){p->Initialized=1;p->Held=0;}
VOID KeAcquireSpinLock(KSPIN_LOCK *p,KIRQL *old)
{CHECK(!held && p->Initialized && !p->Held && irql<=2);*old=irql;irql=2;held=p->Held=1;}
VOID KeReleaseSpinLock(KSPIN_LOCK *p,KIRQL old)
{
    CHECK(held && p->Held && irql==2);held=p->Held=0;irql=old;
    if(active && active->HookRetire && p==&active->Life.Lock && !irql &&
       !active->Life.Calls && !active->Life.Holds){
        active->HookRetire=0;Retire(active);++retiredInsideDrop;
    }
}
static VOID Touch(REQUEST *r,ULONG owner)
{CHECK(r && r->Alive==1 && r->Owner==owner);}
static VOID Observe(PARENT *p)
{
    CHECK(!held);++observations;
    CHECK(p->CompleteCount<=1 && p->Frees<=p->Allocations);
    CHECK(p->References==p->RequestEnvelope.Ref+p->WakeEnvelope.Ref);
    CHECK(p->Tags==p->RequestEnvelope.Tag+p->WakeEnvelope.Tag);
    if(p->Slot){CHECK(p->RequestEnvelope.Owned==p->Slot && p->RequestEnvelope.Live && p->RequestEnvelope.Tag && p->RequestEnvelope.Ref);
        Touch(p->Slot,CSQ_OWNS);CHECK(p->Slot->Marked==1);}
    if(p->WorkState==1){CHECK(p->WakeEnvelope.Live && p->WakeEnvelope.Ref && p->WakeEnvelope.Tag);}
    if(p->Retired){CHECK(!p->Life.Payload && !p->Life.Calls && !p->Life.Holds);}
    else CHECK(p->FixtureChild->Alive==1);
    if(p->Quarantine){CHECK(!p->Retired && p->RequestEnvelope.Ref && p->RequestEnvelope.Tag);}
}
static VOID Init(PARENT *p,CHILD *c,REQUEST *r)
{
    CHECK(!held && !irql);memset(p,0,sizeof(*p));memset(c,0,sizeof(*c));memset(r,0,sizeof(*r));
    c->Alive=1;r->Alive=1;r->Owner=DISPATCH_OWNS;r->Value=19;
    p->FixtureChild=c;active=p;KeInitializeSpinLock(&p->QueueLock);
    CHECK(Bc250LifeInit(&p->Life,100+fixtures)==STATUS_SUCCESS);
    CHECK(Bc250LifePublish(&p->Life,1,1,c)==STATUS_SUCCESS);++fixtures;
}
static VOID Retire(PARENT *p)
{
    PVOID ptr=NULL;CHECK(!irql && !held && !p->Retired);
    CHECK(Bc250LifeClose(&p->Life)==STATUS_SUCCESS);
    CHECK(Bc250LifeDetach(&p->Life,&ptr)==STATUS_SUCCESS && ptr==p->FixtureChild);
    CHECK(!p->Slot && !p->Tags && !p->WorkActive && !p->CancelActive);
    memset(ptr,0xdd,sizeof(CHILD));p->Retired=1;
}
/* Separate exact acquisition receipts, not inferred from IRP status/count0. */
static BOOLEAN Stage(PARENT *p)
{
    ++p->Stage;
    if(p->CloseStage==p->Stage)CHECK(Bc250LifeClose(&p->Life)==STATUS_SUCCESS);
    Observe(p);return p->Stage!=p->FailStage;
}
static VOID Alloc(PARENT *p,ENVELOPE *e)
{CHECK(!held && !e->Live);e->Live=1;++p->Allocations;Observe(p);}
static VOID Ref(PARENT *p,ENVELOPE *e)
{CHECK(e->Live && !e->Ref);e->Ref=1;++p->References;Observe(p);}
static VOID Tag(PARENT *p,ENVELOPE *e)
{CHECK(e->Live && !e->Tag);e->Tag=1;++p->Tags;Observe(p);}
static VOID Drop(PARENT *p,ENVELOPE *e)
{
    CHECK(!held && e->Live && !e->Owned);
    if(e->Tag){CHECK(p->Tags);e->Tag=0;--p->Tags;}
    /* Body finished: child HOLD remains until tag cleanup finishes. Stable
     * envelope/ref is independent and survives this final child drop return. */
    if(!LifeZero(&e->Hold,sizeof(e->Hold)))CHECK(Bc250LifeReleaseHold(&p->Life,&e->Hold)==STATUS_SUCCESS);
    if(e->Ref){CHECK(p->References);e->Ref=0;--p->References;}
    e->Live=0;++p->Frees;Observe(p);
    /* Stable receipt storage remains anchored through helper/wrapper return. */
}
static NTSTATUS Prepare(PARENT *p,REQUEST *r)
{
    NTSTATUS st=STATUS_INSUFFICIENT_RESOURCES;
    if(!Stage(p))goto fail;Alloc(p,&p->RequestEnvelope);p->RequestEnvelope.Owned=r;
    if(!Stage(p))goto fail;Ref(p,&p->RequestEnvelope);
    if(!Stage(p))goto fail;Tag(p,&p->RequestEnvelope);
    if(!Stage(p))goto fail;
    st=Bc250LifeHold(&p->Life,1,&p->RequestEnvelope.Hold);if(st!=STATUS_SUCCESS)goto fail;
    st=STATUS_INSUFFICIENT_RESOURCES;
    if(!Stage(p))goto fail;Alloc(p,&p->WakeEnvelope);
    if(!Stage(p))goto fail;Ref(p,&p->WakeEnvelope);
    if(!Stage(p))goto fail;Tag(p,&p->WakeEnvelope);
    if(!Stage(p))goto fail;
    st=Bc250LifeHold(&p->Life,1,&p->WakeEnvelope.Hold);if(st!=STATUS_SUCCESS)goto fail;
    return STATUS_SUCCESS;
fail:
    CHECK(!p->Slot && !p->Scheduled);
    if(p->WakeEnvelope.Live)Drop(p,&p->WakeEnvelope);
    p->RequestEnvelope.Owned=NULL; /* local dispatch still owns r; no published recipient */
    if(p->RequestEnvelope.Live)Drop(p,&p->RequestEnvelope);
    ++unwinds;return st;
}
/* Fake callbacks use only queue list state under its lock, never Life APIs. */
static VOID InsertCallback(PARENT *p,REQUEST *r)
{CHECK(held && p->QueueLock.Held && !p->Slot);Touch(r,DISPATCH_OWNS);CHECK(r->Marked==1);r->Owner=CSQ_OWNS;p->Slot=r;++p->InsertCalls;}
static REQUEST *PeekCallback(PARENT *p)
{CHECK(held && p->QueueLock.Held);return p->Slot;}
static VOID RemoveCallback(PARENT *p,REQUEST *r,ULONG owner)
{CHECK(held && p->QueueLock.Held && p->Slot==r);Touch(r,CSQ_OWNS);p->Slot=NULL;r->Owner=owner;}
static VOID Complete(PARENT *p,REQUEST *r,ULONG owner,NTSTATUS status)
{
    CHECK(!held && !p->CompleteCount);Touch(r,owner);
    if(owner!=DISPATCH_OWNS){CHECK(p->RequestEnvelope.Owned==r);p->RequestEnvelope.Owned=NULL;}
    p->FinalStatus=(ULONG)status;++p->CompleteCount;
    memset(r,0xdd,sizeof(*r)); /* Completed request poisoned BEFORE callback returns. */
}
static BOOLEAN FinishOwned(PARENT *p,REQUEST *r,ULONG owner,NTSTATUS status)
{
    PVOID ptr=NULL;NTSTATUS st;CHECK(p->RequestEnvelope.Owned==r);
    st=Bc250LifeEnterHeld(&p->Life,&p->RequestEnvelope.Hold,&p->RequestCall,&ptr);
    if(st!=STATUS_SUCCESS){p->Quarantine=1;Observe(p);return FALSE;}
    CHECK(((CHILD *)ptr)->Alive==1);++((CHILD *)ptr)->Touches;
    Complete(p,r,owner,status); /* no request access after this point */
    CHECK(Bc250LifeExit(&p->Life,&p->RequestCall)==STATUS_SUCCESS);
    Drop(p,&p->RequestEnvelope);return TRUE;
}
static VOID CancelCallback(PARENT *p,REQUEST *r)
{
    KIRQL saved=irql;CHECK(!held && irql<=2);irql=2;++p->CancelActive;Observe(p);
    (void)FinishOwned(p,r,CANCEL_OWNS,STATUS_CANCELLED);
    --p->CancelActive;Observe(p);irql=saved;
}
static VOID CancelQueued(PARENT *p)
{
    KIRQL old;REQUEST *r;
    KeAcquireSpinLock(&p->QueueLock,&old);r=PeekCallback(p);
    if(r){Touch(r,CSQ_OWNS);r->Cancel=1;RemoveCallback(p,r,CANCEL_OWNS);}
    KeReleaseSpinLock(&p->QueueLock,old);
    if(r)CancelCallback(p,r); /* loser gets no request, cannot complete */
    Observe(p);
}
static VOID FakeInsert(PARENT *p,REQUEST *r)
{
    KIRQL old;REQUEST *cancelled=NULL;
    KeAcquireSpinLock(&p->QueueLock,&old);Touch(r,DISPATCH_OWNS);CHECK(r->Marked==1);
    if(r->Cancel){r->Owner=CANCEL_OWNS;cancelled=r;} /* precancel bypasses insert callback */
    else InsertCallback(p,r);
    KeReleaseSpinLock(&p->QueueLock,old);
    if(cancelled){CancelCallback(p,cancelled);++inlineCancelled;}
    /* Completion may already have destroyed r. Never read it again. */
    Observe(p);
}
static REQUEST *FakeRemoveNext(PARENT *p)
{
    KIRQL old;REQUEST *r,*cancelled=NULL;
    KeAcquireSpinLock(&p->QueueLock,&old);r=PeekCallback(p);
    if(r){
        if(r->Cancel){RemoveCallback(p,r,CANCEL_OWNS);cancelled=r;r=NULL;}
        else RemoveCallback(p,r,WORKER_OWNS);
    }
    KeReleaseSpinLock(&p->QueueLock,old);
    if(cancelled)CancelCallback(p,cancelled);
    return r;
}
static VOID Boundary(PARENT *p,ULONG point)
{
    if(p->ClosePoint==point)CHECK(Bc250LifeClose(&p->Life)==STATUS_SUCCESS);
    if(p->CancelPoint==point)CancelQueued(p);
    Observe(p);
}
static VOID Worker(PARENT *p)
{
    PVOID ptr=NULL;REQUEST *r;NTSTATUS st;
    CHECK(!held && p->WorkState==1);p->WorkState=2;++p->WorkActive;
    st=Bc250LifeEnterHeld(&p->Life,&p->WakeEnvelope.Hold,&p->WorkCall,&ptr);
    if(st!=STATUS_SUCCESS){p->Quarantine=1;--p->WorkActive;Observe(p);return;}
    CHECK(((CHILD *)ptr)->Alive==1);++((CHILD *)ptr)->Touches;
    Boundary(p,3);r=FakeRemoveNext(p);Boundary(p,4);
    if(r){
        Touch(r,WORKER_OWNS);++workerWins;
        /* After dequeue, cancel callback has NO ownership. Test-private policy
         * consumes a later cancel bit; never generalize to forwarded IRPs. */
        if(p->CancelPoint==5)r->Cancel=1;
        Boundary(p,5);
        st=r->Cancel?STATUS_CANCELLED:STATUS_SUCCESS;
        (void)FinishOwned(p,r,WORKER_OWNS,st);
    }else ++emptyWorkers;
    CHECK(Bc250LifeExit(&p->Life,&p->WorkCall)==STATUS_SUCCESS);
    --p->WorkActive;p->WorkState=3;
    Drop(p,&p->WakeEnvelope); /* child may detach before this helper returns */
    Observe(p); /* Parent-only until wrapper returns; external fixture anchor */
}
static VOID QueueWake(PARENT *p)
{
    CHECK(!held && p->WakeEnvelope.Live && p->WakeEnvelope.Ref && p->WakeEnvelope.Tag);
    CHECK(!LifeZero(&p->WakeEnvelope.Hold,sizeof(p->WakeEnvelope.Hold)) && !p->Scheduled);
    ++p->Scheduled;p->WorkState=1;Observe(p);
    if(p->InlineWork)Worker(p);
    /* Worker may have freed its fake allocation; no wake-envelope access here. */
}
static NTSTATUS Dispatch(PARENT *p,REQUEST *r,ULONG kind)
{
    PVOID ptr=NULL;NTSTATUS st;
    if(kind!=PRIVATE_KIND){++rejected;return STATUS_NOT_SUPPORTED;}
    CHECK(!held && !irql);st=Bc250LifeEnter(&p->Life,1,&p->DispatchCall,&ptr);
    if(st!=STATUS_SUCCESS){++rejected;return st;}
    p->DispatchActive=1;CHECK(((CHILD *)ptr)->Alive==1);++((CHILD *)ptr)->Touches;
    st=Prepare(p,r);
    if(st!=STATUS_SUCCESS){Complete(p,r,DISPATCH_OWNS,st);goto exit;}
    Touch(r,DISPATCH_OWNS);CHECK(!r->Marked);r->Marked=1; /* mark BEFORE transfer */
    if(p->CancelPoint==1)r->Cancel=1;
    Boundary(p,1);FakeInsert(p,r);
    /* r is NOT accessed after FakeInsert, even when cancellation was inline. */
    Boundary(p,2);QueueWake(p);st=STATUS_PENDING;
exit:
    p->DispatchActive=0;
    CHECK(Bc250LifeExit(&p->Life,&p->DispatchCall)==STATUS_SUCCESS);
    Observe(p);return st; /* captured local status, not request output */
}
static VOID CleanKnown(PARENT *p)
{
    CHECK(!p->Quarantine && p->CompleteCount==1 && !p->Slot);
    CHECK(!p->References && !p->Tags && p->Allocations==p->Frees);
    CHECK(!p->Life.Calls && !p->Life.Holds);
    if(!p->Retired)Retire(p);Observe(p);active=NULL;
}
static VOID TimingCases(VOID)
{
    ULONG cancel,close,inlineWork;
    for(cancel=0;cancel<=5;++cancel)for(close=0;close<=5;++close)for(inlineWork=0;inlineWork<2;++inlineWork){
        PARENT p;CHILD c;REQUEST r;Init(&p,&c,&r);
        p.CancelPoint=cancel;p.ClosePoint=close;p.InlineWork=inlineWork;
        CHECK(Dispatch(&p,&r,PRIVATE_KIND)==STATUS_PENDING && p.Scheduled==1);
        CHECK(p.InsertCalls==(cancel==1?0U:1U));
        if(!inlineWork){
            CHECK(p.WorkState==1 && !p.Retired);p.HookRetire=1;Worker(&p);
        }
        CHECK(p.CompleteCount==1 && p.FinalStatus==(ULONG)((cancel==1 || cancel==2 || cancel==3 || cancel==5)?STATUS_CANCELLED:STATUS_SUCCESS));
        CHECK(FakeRemoveNext(&p)==NULL);CancelQueued(&p); /* duplicate loser does not complete */
        CleanKnown(&p);
    }
}
static VOID FailureCases(VOID)
{
    ULONG fail,close;
    for(fail=1;fail<=8;++fail)for(close=0;close<=8;++close){
        PARENT p;CHILD c;REQUEST r;NTSTATUS st;Init(&p,&c,&r);
        p.FailStage=fail;p.CloseStage=close;st=Dispatch(&p,&r,PRIVATE_KIND);
        CHECK(st==STATUS_INSUFFICIENT_RESOURCES || st==STATUS_DEVICE_NOT_READY);
        CHECK(!p.Scheduled && p.CompleteCount==1);CleanKnown(&p);
    }
    /* Close between preparations can reject a future hold even without injected
     * allocator failure; already-successful receipts still unwind exactly. */
    for(close=1;close<=8;++close){
        PARENT p;CHILD c;REQUEST r;Init(&p,&c,&r);p.CloseStage=close;
        CHECK(Dispatch(&p,&r,PRIVATE_KIND)==STATUS_DEVICE_NOT_READY);
        CHECK(!p.Scheduled);CleanKnown(&p);
    }
}
static VOID ScopeAndUnknown(VOID)
{
    ULONG kind;
    for(kind=PNP_KIND;kind<=DIAGNOSTIC_KIND;++kind){
        PARENT p;CHILD c;REQUEST r;Init(&p,&c,&r);
        CHECK(Dispatch(&p,&r,kind)==STATUS_NOT_SUPPORTED);
        Touch(&r,DISPATCH_OWNS);CHECK(!p.Stage && !p.CompleteCount && !p.Scheduled && !p.Allocations);
        Retire(&p);active=NULL;
    }
    {
        PARENT p;CHILD c;REQUEST r;PVOID ptr=NULL;Init(&p,&c,&r);
        CHECK(Bc250LifeClose(&p.Life)==STATUS_SUCCESS);
        CHECK(Dispatch(&p,&r,PRIVATE_KIND)==STATUS_DEVICE_NOT_READY);
        Touch(&r,DISPATCH_OWNS);CHECK(!p.Stage && !p.CompleteCount);Retire(&p);active=NULL;
        Init(&p,&c,&r);CHECK(Dispatch(&p,&r,PRIVATE_KIND)==STATUS_PENDING);
        /* Tampered identity is NOT guessed or silently released. Other known
         * worker resources can release, unknown request resources retained. */
        ++p.RequestEnvelope.Hold.Id;Worker(&p);
        CHECK(p.Quarantine && !p.CompleteCount && !p.Slot && !p.WakeEnvelope.Live);
        Touch(&r,WORKER_OWNS);CHECK(p.RequestEnvelope.Owned==&r && p.RequestEnvelope.Live && p.References==1 && p.Tags==1 && p.Life.Holds==1);
        CHECK(Bc250LifeClose(&p.Life)==STATUS_SUCCESS);
        CHECK(Bc250LifeDetach(&p.Life,&ptr)==STATUS_DEVICE_BUSY && !ptr);Observe(&p);
        /* TEST end-of-world destroys fixture after all fake frames return;
         * NOT a production recovery or permission to release unknown holds. */
        active=NULL;
    }
}
int main(void)
{
    TimingCases();FailureCases();ScopeAndUnknown();
    CHECK(inlineCancelled && emptyWorkers && workerWins && unwinds && retiredInsideDrop && rejected);
    printf("PASS: private queue handoff %u checks, %u fixtures, %u boundaries; inline-cancel %u, empty-worker %u, worker-owner %u, unwind %u, child-retire-inside-drop %u; RAM only, no CSQ/IRP/OS/SMP/hardware.\n",
        checks,fixtures,observations,inlineCancelled,emptyWorkers,workerWins,unwinds,retiredInsideDrop);
    return 0;
}
