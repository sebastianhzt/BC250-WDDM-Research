/* SPDX-License-Identifier: Apache-2.0 */
#define BC250_PNP_COMPLETION_MOCK 1
#define BC250_PNP_SOURCE_MOCK 1
#define BC250_PNP_CAPTURE_TYPES_MOCK 1
#define BC250_PCI_TYPES_MOCK 1
#define BC250_PERMIT_MOCK 1
#define BC250_LIFE_MOCK 1
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "../pnp-completion-20261004/bc250_pnp_completion.c"
#include "../pnp-source-20261004/bc250_pnp_source.c"
#include "../composite-permit-20261004/bc250_composite_permit.c"
#include "../context-lifetime-20261004/bc250_context_lifetime.c"
BOOLEAN Bc250LifeMockAllowed=TRUE;
BOOLEAN Bc250CompletionMockAllowed=TRUE,Bc250SourceMockAllowed=TRUE;
BOOLEAN Bc250PermitMockAllowed=TRUE;
static unsigned checks,held,queues,forwards,completions,frees,allocs;
static KIRQL irql;
static ULONG currentThread=1,inlineLower,inlineWorker,failAlloc,failRegister;
static NTSTATUS lowerReturn,lowerFinal;
static ULONG_PTR lowerInformation;
static IO_WORKITEM item;
static BC250_SOURCE *activeSource;
static PIO_REMOVE_LOCK activeLock;
static BC250_COMPLETION *activeBridge;
static ULONG closeDuringQueue,sourcePendingAtQueue,earlyWorkerObserved;
/* Fake platform derived from frozen completion fixtures; only TEST observers
 * below added. Frozen kernel implementations included without edits.
 * ALL roots/contexts/code/IRPs remain externally anchored by serial fixture. */
typedef struct MODEL_FINAL {
    BC250_PERMIT Permit;
    BC250_PERMIT_WRITER Writer,Second;
    BC250_CAPTURE_INPUT Published;
    BC250_SOURCE *Source;
    BC250_COMPLETION *Bridge;
    PIO_COMPLETION_ROUTINE Callback;
    PIO_WORKITEM_ROUTINE Worker;
    BOOLEAN Taken;
    NTSTATUS DispatchStatus;
    ULONG DispatchReturned,AllReturnsKnown,Forwarded,CallbackDepth,WorkerDepth;
    ULONG ForwardDepth,QueueDepth,CompleteDepth,FreeDepth,ReleaseDepth;
    ULONG CloseMode,CloseInjected,Observations,SourceCleanEarly,DoneEarly,TagsZeroEarly;
} MODEL_FINAL;
static MODEL_FINAL *model;
/* TEST ONLY: Parent and all envelope tokens/callback code/IRP/FDO/lower/lock
 * remain independently resident through ALL returns. Child is separate RAM.
 * No native wrapper, OS cancellation, Parent/module lifetime or SMP proof. */
typedef struct LIFE_CHILD {
    MODEL_FINAL M;
    BC250_SOURCE Source;
    BC250_COMPLETION Bridge;
    ULONG ActiveBodies,Alive;
} LIFE_CHILD;
typedef struct LIFE_PARENT {
    BC250_LIFE_DOMAIN Domain;
    BC250_LIFE_TOKEN DispatchCall,CallbackHold,WorkerHold,CallbackCall,WorkerCall;
    PIO_COMPLETION_ROUTINE Callback;
    PIO_WORKITEM_ROUTINE Worker;
    ULONGLONG Key;
    ULONG Forwarded,Registered,Queued,Quarantined,Detached,ObserveCount;
    ULONG HookRetire,HelperRetire,RejectedCallbacks,RejectedWorkers;
    NTSTATUS DispatchResult,RetireResult;
} LIFE_PARENT;
static LIFE_PARENT *life;
static unsigned lifeFixtures,lifeObservations,lifeDoneEarly,lifeZeroTagsEarly;
static NTSTATUS LifeRetire(LIFE_PARENT *);
static VOID LifeObserve(ULONG);
static NTSTATUS LifeCallback(PDEVICE_OBJECT,PIRP,PVOID);
static VOID LifeWorker(PDEVICE_OBJECT,PVOID);
static unsigned modelFixtures,totalObservations,totalSourceClean,totalDone,totalTagsZero;
static NTSTATUS ModelRetire(MODEL_FINAL *);
static VOID Observe(ULONG);
static NTSTATUS ObserveCallback(PDEVICE_OBJECT,PIRP,PVOID);
static VOID ObserveWorker(PDEVICE_OBJECT,PVOID);
#define CHECK(x) do { ++checks;if(!(x)) { fprintf(stderr,"FAIL line %u: %s\n",(unsigned)__LINE__,#x);exit(1); } } while(0)
unsigned char KeGetCurrentIrql(VOID) { return irql; }
PVOID KeGetCurrentThread(VOID) { return (PVOID)(ULONG_PTR)currentThread; }
SIZE_T RtlCompareMemory(const VOID *a,const VOID *b,SIZE_T n)
{ SIZE_T k;const UCHAR *x=a,*y=b;for(k=0;k<n && x[k]==y[k];++k){}return k; }
VOID KeInitializeSpinLock(KSPIN_LOCK *p) { p->Initialized=1;p->Held=0; }
VOID KeAcquireSpinLock(KSPIN_LOCK *p,KIRQL *old)
{ CHECK(p->Initialized && !p->Held && !held && irql<=2);*old=irql;irql=2;p->Held=1;held=1; }
VOID KeReleaseSpinLock(KSPIN_LOCK *p,KIRQL old)
{
    CHECK(p->Held && held && irql==2);p->Held=0;held=0;irql=old;
    if(life && life->HookRetire && p==&life->Domain.Lock && irql==0 &&
        !life->Domain.Calls && !life->Domain.Holds){
        life->HookRetire=0;CHECK(LifeRetire(life)==STATUS_SUCCESS);++life->HelperRetire;
    }
}
VOID ExInitializeRundownProtection(PEX_RUNDOWN_REF p) { CHECK(!held);p->Initialized=1;p->Count=p->Closing=0; }
BOOLEAN ExAcquireRundownProtection(PEX_RUNDOWN_REF p) { CHECK(!held && p->Initialized);if(p->Closing)return FALSE;++p->Count;return TRUE; }
VOID ExReleaseRundownProtection(PEX_RUNDOWN_REF p) { CHECK(!held && p->Count);--p->Count; }
VOID ExWaitForRundownProtectionRelease(PEX_RUNDOWN_REF p) { CHECK(!held && irql==0 && !p->Count);p->Closing=1; }
VOID ObReferenceObject(PVOID p) { CHECK(!held && irql==0 && p);++((PDEVICE_OBJECT)p)->Fake; }
VOID ObDereferenceObject(PVOID p) { CHECK(!held && irql==0 && p && ((PDEVICE_OBJECT)p)->Fake>0);--((PDEVICE_OBJECT)p)->Fake; }
NTSTATUS IoAcquireRemoveLock(PIO_REMOVE_LOCK p,PVOID tag)
{
    ULONG n;CHECK(!held && irql<=2);++p->Attempts;
    if(p->Attempts==p->RejectAt)return (NTSTATUS)0xc0000056U;
    for(n=0;n<8 && p->Tags[n];++n){}CHECK(n<8);p->Tags[n]=tag;++p->Count;return STATUS_SUCCESS;
}
VOID IoReleaseRemoveLock(PIO_REMOVE_LOCK p,PVOID tag)
{
    ULONG n;CHECK(!held && irql<=2 && p->Count);
    if(model)++model->ReleaseDepth;Observe(5);
    for(n=0;n<8 && p->Tags[n]!=tag;++n){}CHECK(n<8);p->Tags[n]=NULL;--p->Count;
    Observe(6);if(model)--model->ReleaseDepth;
}
PIO_WORKITEM IoAllocateWorkItem(PDEVICE_OBJECT device)
{
    CHECK(!held && irql==0);if(failAlloc)return NULL;CHECK(!item.Allocated);
    memset(&item,0,sizeof(item));item.Allocated=1;item.Device=device;++allocs;return &item;
}
VOID IoFreeWorkItem(PIO_WORKITEM p)
{
    CHECK(!held && p==&item && p->Allocated && !p->Queued);
    if(model)++model->FreeDepth;Observe(4);p->Allocated=0;++frees;
    Observe(4);if(model)--model->FreeDepth;
}
VOID IoCopyCurrentIrpStackLocationToNext(PIRP p) { CHECK(!held && !p->Completed);++p->StackCopied; }
NTSTATUS IoSetCompletionRoutineEx(PDEVICE_OBJECT d,PIRP p,PIO_COMPLETION_ROUTINE fn,PVOID ctx,BOOLEAN a,BOOLEAN b,BOOLEAN c)
{
    CHECK(!held && d && p->StackCopied==1 && !p->Routine && a && b && c);
    if(failRegister)return STATUS_INSUFFICIENT_RESOURCES;
    if(life){
        CHECK(ctx==activeBridge && !LifeZero(&life->CallbackHold,sizeof(life->CallbackHold)));
        life->Callback=fn;life->Registered=1;p->Routine=LifeCallback;p->Context=life;
    }else{
        if(model){model->Callback=fn;p->Routine=ObserveCallback;}else p->Routine=fn;
        p->Context=ctx;
    }
    return STATUS_SUCCESS;
}
VOID IoMarkIrpPending(PIRP p) { CHECK(!held && !p->Completed);++p->Marked; }
VOID IoCompleteRequest(PIRP p,char boost)
{
    CHECK(!held && boost==0 && !p->Completed && p->IoStatus.Status!=STATUS_PENDING);
    if(model)++model->CompleteDepth;Observe(3);
    if(p->Marked)CHECK(p->IoStatus.Status==lowerFinal && p->IoStatus.Information==lowerInformation);
    CHECK(activeLock!=NULL);++p->Completed;++completions;
    /* Poison status at the ownership boundary; code must not read it later. */
    p->Routine=NULL;p->Context=NULL;p->IoStatus.Status=(NTSTATUS)0xdeadbeefU;p->IoStatus.Information=~(ULONG_PTR)0;
    Observe(3);if(model)--model->CompleteDepth;
}
static VOID RunWorker(VOID)
{
    PIO_WORKITEM_ROUTINE fn=item.Routine;PVOID ctx=item.Context;PDEVICE_OBJECT device=item.Device;
    KIRQL saved=irql;ULONG savedThread=currentThread;
    CHECK(item.Allocated && item.Queued);item.Queued=0;irql=0;currentThread=2;
    fn(device,ctx);currentThread=savedThread;irql=saved;
}
VOID IoQueueWorkItem(PIO_WORKITEM p,PIO_WORKITEM_ROUTINE fn,WORK_QUEUE_TYPE q,PVOID ctx)
{
    CHECK(!held && irql<=2 && p==&item && p->Allocated && !p->Queued && q==DelayedWorkQueue);
    if(life){
        CHECK(ctx==activeBridge && !LifeZero(&life->WorkerHold,sizeof(life->WorkerHold)));
        life->Worker=fn;life->Queued=1;p->Routine=LifeWorker;p->Context=life;
    }else{
        if(model){++model->QueueDepth;model->Worker=fn;p->Routine=ObserveWorker;}else p->Routine=fn;
        p->Context=ctx;
    }
    p->Queued=1;++queues;Observe(2);
    sourcePendingAtQueue=activeSource->PendingAddress!=NULL;
    CHECK(sourcePendingAtQueue && activeLock->Count!=0);
    if(closeDuringQueue)CHECK(Bc250SourceClose(activeSource)==STATUS_SUCCESS);
    if(inlineWorker) {
        RunWorker();earlyWorkerObserved=1;
        CHECK(activeBridge->Phase==BC250_COMPLETION_DONE && activeLock->Count==(inlineLower?1U:0U));
    }
    Observe(2);if(model && !life)--model->QueueDepth;
    /* Simulated cross-thread worker before queue return, not true OS SMP. */
}
static VOID LowerComplete(PIRP p)
{
    PIO_COMPLETION_ROUTINE fn=p->Routine;PVOID ctx=p->Context;KIRQL old=irql;
    CHECK(fn!=NULL && !p->Completed);p->IoStatus.Status=lowerFinal;p->IoStatus.Information=lowerInformation;
    p->PendingReturned=TRUE;irql=2;
    CHECK(fn(NULL,p,ctx)==STATUS_MORE_PROCESSING_REQUIRED);irql=old;
    /* p may already be completed/poisoned: no access from here. */
}
NTSTATUS IoCallDriver(PDEVICE_OBJECT device,PIRP p)
{
    CHECK(!held && device && p->Routine && p->Marked && activeSource->PendingAddress && activeLock->Count==2);
    CHECK(!SourceReady(activeSource));++forwards;
    if(model){++model->ForwardDepth;model->Forwarded=1;}Observe(1);
    if(life)life->Forwarded=1;
    if(inlineLower)LowerComplete(p);
    Observe(1);if(model)--model->ForwardDepth;
    return lowerReturn; /* Deliberately independent from final IoStatus. */
}
static BC250_SOURCE_RESOURCES Packet(void)
{
    BC250_SOURCE_RESOURCES r;memset(&r,0,sizeof(r));r.DescriptorCount=1;r.MemoryCount=1;
    r.Memory[0].Raw=r.Memory[0].Translated=0xc0000000ULL;r.Memory[0].Length=0x10000000ULL;
    r.Memory[0].RawFlags=r.Memory[0].TranslatedFlags=0x84;return r;
}
static VOID Fixture(BC250_SOURCE *s,DEVICE_OBJECT *pdo,DEVICE_OBJECT *lower,BC250_COMPLETION *c,IO_REMOVE_LOCK *lock,IRP *irp,ULONGLONG scope)
{
    memset(s,0,sizeof(*s));pdo->Fake=lower->Fake=0;memset(c,0,sizeof(*c));memset(lock,0,sizeof(*lock));memset(irp,0,sizeof(*irp));
    CHECK(Bc250SourceInit(s,scope,1,pdo,lower)==STATUS_SUCCESS);
    memset(&item,0,sizeof(item));inlineLower=inlineWorker=failAlloc=failRegister=closeDuringQueue=earlyWorkerObserved=0;
    lowerReturn=lowerFinal=STATUS_SUCCESS;lowerInformation=0x12345678U;
    activeSource=s;activeLock=lock;activeBridge=c;currentThread=1;irql=0;
}
static VOID ReadySource(BC250_SOURCE *s)
{
    BC250_SOURCE_REQUEST q={0};BC250_SOURCE_RESOURCES r=Packet();
    CHECK(Bc250SourceBegin(s,1,0,&q)==STATUS_SUCCESS);CHECK(Bc250SourceComplete(s,&q,STATUS_SUCCESS,&r)==STATUS_SUCCESS);
    CHECK(Bc250SourceInterlocks(s,1)==STATUS_SUCCESS);CHECK(Bc250SourceBegin(s,6,1,&q)==STATUS_SUCCESS);
    CHECK(Bc250SourceComplete(s,&q,STATUS_SUCCESS,NULL)==STATUS_SUCCESS);CHECK(SourceReady(s));
}
static VOID FinishSource(BC250_SOURCE *s)
{ CHECK(Bc250SourceClose(s)==STATUS_SUCCESS);CHECK(Bc250SourceDrain(s)==STATUS_SUCCESS); }
static VOID CheckDone(BC250_COMPLETION *c,IO_REMOVE_LOCK *lock,IRP *irp)
{
    BC250_COMPLETION_STATUS out;CHECK(Bc250CompletionInspect(c,&out)==STATUS_SUCCESS);
    CHECK(out.Phase==5 && !out.HeldTags && !out.RequestOwned && !out.WorkPresent && !out.IrpPresent);
    CHECK(lock->Count==0 && irp->Completed==1 && !item.Allocated);
}
static VOID TimingMatrix(VOID)
{
    ULONG lowerMode,workerMode,ret,stop;
    for(lowerMode=0;lowerMode<2;++lowerMode)for(workerMode=0;workerMode<2;++workerMode)
    for(ret=0;ret<3;++ret)for(stop=0;stop<2;++stop) {
        BC250_SOURCE s;DEVICE_OBJECT p,l;BC250_COMPLETION c;IO_REMOVE_LOCK lock;IRP irp;BOOLEAN taken=FALSE;
        unsigned queueBefore=queues,completeBefore=completions;BC250_SOURCE_RESOURCES r=Packet();
        Fixture(&s,&p,&l,&c,&lock,&irp,10+lowerMode*12+workerMode*6+ret*2+stop);
        inlineLower=lowerMode;inlineWorker=workerMode;closeDuringQueue=stop;
        lowerReturn=ret==0?STATUS_SUCCESS:ret==1?STATUS_PENDING:STATUS_CANCELLED;
        CHECK(Bc250CompletionDispatch(&c,&s,&p,&l,&lock,&irp,1,0,&r,&taken)==STATUS_PENDING && taken);
        CHECK(irp.Marked!=0);if(!lowerMode)LowerComplete(&irp);
        if(item.Queued) { CHECK(lock.Count==1);RunWorker(); }
        CheckDone(&c,&lock,&irp);CHECK(queues==queueBefore+1 && completions==completeBefore+1);
        CHECK(!s.PendingAddress && s.Rundown.Count==0);
        CHECK(stop?SourceGetState(&s)==6:SourceGetState(&s)==1);CHECK(s.Current.PowerState==0);
        CHECK(!lowerMode || !workerMode || earlyWorkerObserved);
        FinishSource(&s);
    }
}
static VOID SetupFailures(VOID)
{
    ULONG n;for(n=0;n<5;++n) {
        BC250_SOURCE s;DEVICE_OBJECT p,l;BC250_COMPLETION c;IO_REMOVE_LOCK lock;IRP irp;BOOLEAN taken=FALSE;
        unsigned f=forwards,q=queues,freeBefore=frees;NTSTATUS expected;
        Fixture(&s,&p,&l,&c,&lock,&irp,100+n);
        if(n==0 || n==1)lock.RejectAt=n+1;
        if(n==2)failAlloc=1;if(n==3)failRegister=1;
        if(n==4)CHECK(Bc250SourceClose(&s)==STATUS_SUCCESS);
        expected=n<2?(NTSTATUS)0xc0000056U:n<4?STATUS_INSUFFICIENT_RESOURCES:STATUS_DEVICE_NOT_READY;
        CHECK(Bc250CompletionDispatch(&c,&s,&p,&l,&lock,&irp,1,0,NULL,&taken)==expected && taken);
        CheckDone(&c,&lock,&irp);CHECK(forwards==f && queues==q && !irp.Marked && !s.PendingAddress);
        CHECK(frees==freeBefore+(n>=3?1U:0U));FinishSource(&s);
    }
    {
        BC250_COMPLETION c={0};BOOLEAN taken=TRUE;IRP irp={0};
        CHECK(Bc250CompletionDispatch(&c,NULL,NULL,NULL,NULL,&irp,1,0,NULL,&taken)==STATUS_INVALID_PARAMETER && !taken && !irp.Completed);
    }
}
static VOID PowerAndStatus(VOID)
{
    ULONG n;for(n=0;n<5;++n) {
        BC250_SOURCE s;DEVICE_OBJECT p,l;BC250_COMPLETION c;IO_REMOVE_LOCK lock;IRP irp;BOOLEAN taken=FALSE;
        Fixture(&s,&p,&l,&c,&lock,&irp,200+n);ReadySource(&s);
        lowerFinal=n==0?STATUS_SUCCESS:n==1?STATUS_CANCELLED:n==2?(NTSTATUS)0x80000005U:n==3?(NTSTATUS)1:STATUS_SUCCESS;
        CHECK(Bc250CompletionDispatch(&c,&s,&p,&l,&lock,&irp,6,1,NULL,&taken)==STATUS_PENDING && taken);
        CHECK(s.Current.PowerState==0 && !SourceReady(&s));
        if(n==4)CHECK(Bc250SourceClose(&s)==STATUS_SUCCESS);
        LowerComplete(&irp);CHECK(item.Queued && !SourceReady(&s));RunWorker();CheckDone(&c,&lock,&irp);
        CHECK(c.FinalStatus==lowerFinal && c.FinalInformation==lowerInformation);
        CHECK(n==0?SourceReady(&s):!SourceReady(&s));
        if(n==2 || n==3)CHECK(s.Fault && Bc250SourceDrain(&s)==STATUS_DATA_ERROR);
        else FinishSource(&s);
    }
}
static VOID UnknownAndDuplicates(VOID)
{
    ULONG n;for(n=0;n<4;++n) {
        BC250_SOURCE s;DEVICE_OBJECT p,l;BC250_COMPLETION c;IO_REMOVE_LOCK lock;IRP irp,wrong={0};BOOLEAN taken=FALSE;
        BC250_SOURCE_RESOURCES r=Packet();unsigned q=queues,freeBefore=frees,completeBefore=completions;
        Fixture(&s,&p,&l,&c,&lock,&irp,300+n);
        CHECK(Bc250CompletionDispatch(&c,&s,&p,&l,&lock,&irp,1,0,&r,&taken)==STATUS_PENDING);
        if(n==0)lowerFinal=STATUS_PENDING;
        if(n==1) { irql=2;CHECK(Bc250CompletionCallback(NULL,&wrong,&c)==STATUS_MORE_PROCESSING_REQUIRED);irql=0; }
        else LowerComplete(&irp);
        if(n==2) { irql=2;CHECK(Bc250CompletionCallback(NULL,&irp,&c)==STATUS_MORE_PROCESSING_REQUIRED);irql=0; }
        if(n==3) { c.Request.Id++;RunWorker(); }
        if(n<2)CHECK(queues==q && item.Allocated && !item.Queued);
        if(n==2) { CHECK(item.Queued);RunWorker(); }
        CHECK(c.Phase==6 && lock.Count==1 && !irp.Completed && frees==freeBefore && completions==completeBefore);
        CHECK(!SourceReady(&s) && c.RequestOwned); /* intentional known-obligation quarantine */
    }
}
static int FrozenCompletionFixtures(VOID)
{
    TimingMatrix();SetupFailures();PowerAndStatus();UnknownAndDuplicates();CHECK(!held && !irql && allocs>=frees);
    printf("PASS: deferred completion %u checks; timing/pre-forward/pending/RemoveLock/quarantine model; no real IRP/SMP/hardware.\n",checks);
    return 0;
}
/* TEST ONLY. AllReturnsKnown is an EXTERNAL SERIAL FIXTURE witness, NOT a
 * lifetime primitive implemented by these counters or CompletionInspect. */
static NTSTATUS ModelRetire(MODEL_FINAL *m)
{
    BC250_COMPLETION_STATUS status;NTSTATUS st,final;BOOLEAN publish=FALSE;
    if(irql!=0)return STATUS_INVALID_DEVICE_STATE;
    if(!m->AllReturnsKnown || !m->DispatchReturned || m->CallbackDepth || m->WorkerDepth ||
        m->ForwardDepth || m->QueueDepth || m->CompleteDepth || m->FreeDepth || m->ReleaseDepth)
        return STATUS_DEVICE_BUSY;
    if(SourceZero(&m->Writer,sizeof(m->Writer)))return STATUS_INVALID_PARAMETER;
    if(m->Taken){
        st=Bc250CompletionInspect(m->Bridge,&status);if(st!=STATUS_SUCCESS)return st;
        if(status.Phase==BC250_COMPLETION_FAULT){
            CHECK(Bc250PermitClose(&m->Permit)==STATUS_SUCCESS);return STATUS_DEVICE_NOT_READY;
        }
        if(status.Phase!=BC250_COMPLETION_DONE || status.HeldTags || status.RequestOwned ||
            status.WorkPresent || status.IrpPresent || !SourceZero(&m->Bridge->Request,sizeof(m->Bridge->Request)) ||
            m->Source->PendingAddress || activeLock->Count || item.Allocated || item.Queued)
            return STATUS_DEVICE_NOT_READY;
        final=m->Forwarded?m->Bridge->FinalStatus:m->DispatchStatus;
        publish=m->Forwarded && final==STATUS_SUCCESS && status.SourceStatus==STATUS_SUCCESS && SourceReady(m->Source);
    }else{
        /* Known no-ownership handshake only; no fake completion/free guessed. */
        if(!CompletionZero(m->Bridge,sizeof(*m->Bridge)) || m->Source->PendingAddress ||
            activeLock->Count || item.Allocated || item.Queued)return STATUS_DEVICE_NOT_READY;
        final=m->DispatchStatus;CHECK(final!=STATUS_SUCCESS && final!=STATUS_PENDING);
    }
    if(m->Source->Closing || m->Source->Fault)CHECK(Bc250PermitClose(&m->Permit)==STATUS_SUCCESS);
    if(publish){
        CHECK(m->Source->ScopeId==m->Permit.ScopeId && m->Source->Current.SourceGeneration==m->Permit.Generation);
        m->Published=m->Source->Current;
    }
    return Bc250PermitWriteFinish(&m->Permit,&m->Writer,publish?STATUS_SUCCESS:STATUS_CANCELLED,publish?2:0);
}
static VOID Observe(ULONG point)
{
    BC250_PERMIT_SNAPSHOT snapshot;
    if(life){LifeObserve(point);return;}
    if(!model)return;
    CHECK(!held && model->Source==activeSource && model->Bridge==activeBridge);
    CHECK(Bc250PermitInspect(&model->Permit,&snapshot)==STATUS_SUCCESS);
    CHECK(!snapshot.Published && snapshot.WriterPhase==2 && !snapshot.Pins && !snapshot.Steps);
    CHECK(model->Permit.WriterAddress==&model->Writer && !SourceZero(&model->Writer,sizeof(model->Writer)));
    ++model->Observations;
    if(!activeSource->PendingAddress)++model->SourceCleanEarly;
    if(activeBridge->Phase==BC250_COMPLETION_DONE)++model->DoneEarly;
    if(activeBridge->Phase==BC250_COMPLETION_DONE && !activeBridge->HeldTags && !activeLock->Count)
        ++model->TagsZeroEarly;
    if(irql==0)CHECK(ModelRetire(model)==STATUS_DEVICE_BUSY);
    if(point==2 && model->CloseMode && !model->CloseInjected){
        model->CloseInjected=1;
        if(model->CloseMode==1)CHECK(Bc250PermitClose(&model->Permit)==STATUS_SUCCESS);
        else CHECK(Bc250PermitWriteAdmit(&model->Permit,2,&model->Second)==STATUS_DEVICE_BUSY);
        CHECK(!snapshot.Published && model->Permit.WriterAddress==&model->Writer);
    }
}
static NTSTATUS ObserveCallback(PDEVICE_OBJECT device,PIRP irp,PVOID context)
{
    MODEL_FINAL *m=model;NTSTATUS st;
    CHECK(m && m->Callback && context==m->Bridge);++m->CallbackDepth;Observe(7);
    st=m->Callback(device,irp,context);Observe(7);--m->CallbackDepth;
    return st; /* no IRP dereference after original callback return */
}
static VOID ObserveWorker(PDEVICE_OBJECT device,PVOID context)
{
    MODEL_FINAL *m=model;
    CHECK(m && m->Worker && context==m->Bridge);++m->WorkerDepth;Observe(8);
    m->Worker(device,context);Observe(8);--m->WorkerDepth;
    /* AllReturnsKnown still FALSE: this wrapper/fake scheduler must also return. */
}
static VOID ModelPrepare(MODEL_FINAL *m,BC250_SOURCE *s,DEVICE_OBJECT *p,DEVICE_OBJECT *l,
    BC250_COMPLETION *c,IO_REMOVE_LOCK *lock,IRP *irp,ULONGLONG scope)
{
    BC250_PERMIT_WRITER boot={0};CHECK(!model);
    Fixture(s,p,l,c,lock,irp,scope);ReadySource(s); /* exclusively unpublished fixture Source */
    RtlZeroMemory(m,sizeof(*m));m->Source=s;m->Bridge=c;m->Published=s->Current;
    CHECK(Bc250PermitInit(&m->Permit,scope,1)==STATUS_SUCCESS);
    CHECK(Bc250PermitWriteAdmit(&m->Permit,1,&boot)==STATUS_SUCCESS);
    CHECK(Bc250PermitWriteClaim(&m->Permit,&boot)==STATUS_SUCCESS);
    CHECK(Bc250PermitWriteFinish(&m->Permit,&boot,STATUS_SUCCESS,1)==STATUS_SUCCESS);++modelFixtures;
}
static NTSTATUS ModelDispatch(MODEL_FINAL *m,DEVICE_OBJECT *p,DEVICE_OBJECT *l,
    IO_REMOVE_LOCK *lock,IRP *irp)
{
    NTSTATUS st=Bc250PermitWriteClaim(&m->Permit,&m->Writer);if(st!=STATUS_SUCCESS)return st;
    CHECK(!model);model=m;
    m->DispatchStatus=Bc250CompletionDispatch(m->Bridge,m->Source,p,l,lock,irp,6,1,NULL,&m->Taken);
    m->DispatchReturned=1;
    return m->DispatchStatus; /* serial fixture still anchors helper/context/code */
}
static VOID ModelReadPublished(MODEL_FINAL *m,BOOLEAN expect)
{
    BC250_PERMIT_READER pin={0};BC250_SOURCE_LEASE lease={0};BC250_CAPTURE_INPUT value;
    ULONGLONG revision;NTSTATUS st=Bc250PermitAcquire(&m->Permit,&pin);
    CHECK(st==(expect?STATUS_SUCCESS:STATUS_DEVICE_NOT_READY));
    if(!expect){CHECK(SourceZero(&pin,sizeof(pin)));return;}
    CHECK(Bc250SourceAcquire(m->Source,&lease)==STATUS_SUCCESS);
    CHECK(Bc250PermitStepEnter(&m->Permit,&pin,&revision)==STATUS_SUCCESS && revision==2);
    CHECK(Bc250SourceValidate(m->Source,&lease,&value)==STATUS_SUCCESS);
    CHECK(RtlCompareMemory(&value,&m->Published,sizeof(value))==sizeof(value));
    CHECK(Bc250PermitStepLeave(&m->Permit,&pin)==STATUS_SUCCESS);
    CHECK(Bc250SourceRelease(m->Source,&lease)==STATUS_SUCCESS);
    CHECK(Bc250PermitRelease(&m->Permit,&pin)==STATUS_SUCCESS);
}
static VOID ModelDispose(MODEL_FINAL *m)
{
    CHECK(m->AllReturnsKnown && m->DispatchReturned && !m->CallbackDepth && !m->WorkerDepth);
    CHECK(SourceZero(&m->Writer,sizeof(m->Writer)) && !m->Source->PendingAddress && !m->Source->Rundown.Count);
    totalObservations+=m->Observations;totalSourceClean+=m->SourceCleanEarly;
    totalDone+=m->DoneEarly;totalTagsZero+=m->TagsZeroEarly;model=NULL;
    if(!m->Source->Closing){
        if(m->Permit.Closing){
            /* Closed gate gives NO new mutation permission for SourceClose.
             * External cleanup coordinator absent: retain Source refs, do not guess. */
            CHECK(m->Source->Pdo->Fake==1 && m->Source->Lower->Fake==1);return;
        }
        CHECK(Bc250PermitWriteAdmit(&m->Permit,6,&m->Writer)==STATUS_SUCCESS);
        CHECK(Bc250PermitWriteClaim(&m->Permit,&m->Writer)==STATUS_SUCCESS);
        CHECK(Bc250SourceClose(m->Source)==STATUS_SUCCESS);
        CHECK(Bc250PermitClose(&m->Permit)==STATUS_SUCCESS);
        CHECK(Bc250PermitWriteFinish(&m->Permit,&m->Writer,STATUS_CANCELLED,0)==STATUS_DEVICE_NOT_READY);
    }
    CHECK(Bc250PermitClose(&m->Permit)==STATUS_SUCCESS);
    CHECK(Bc250SourceDrain(m->Source)==(m->Source->Fault?STATUS_DATA_ERROR:STATUS_SUCCESS));
}
static VOID ComposedTimingMatrix(VOID)
{
    ULONG lm,wm,ret,final,closing;
    for(lm=0;lm<2;++lm)for(wm=0;wm<2;++wm)for(ret=0;ret<3;++ret)
    for(final=0;final<2;++final)for(closing=0;closing<3;++closing){
        MODEL_FINAL m;BC250_SOURCE s;DEVICE_OBJECT p,l;BC250_COMPLETION c;IO_REMOVE_LOCK lock;IRP irp;
        ModelPrepare(&m,&s,&p,&l,&c,&lock,&irp,1000+lm*36+wm*18+ret*6+final*3+closing);
        irql=2;CHECK(Bc250PermitWriteAdmit(&m.Permit,6,&m.Writer)==STATUS_SUCCESS);irql=0;
        inlineLower=lm;inlineWorker=wm;lowerReturn=ret==0?STATUS_SUCCESS:ret==1?STATUS_PENDING:STATUS_CANCELLED;
        lowerFinal=final?STATUS_CANCELLED:STATUS_SUCCESS;m.CloseMode=closing;closeDuringQueue=closing==1;
        CHECK(ModelDispatch(&m,&p,&l,&lock,&irp)==STATUS_PENDING && m.Taken);
        CHECK(ModelRetire(&m)==STATUS_DEVICE_BUSY);ModelReadPublished(&m,FALSE);
        if(!lm || !wm){
            m.AllReturnsKnown=TRUE; /* deliberate invalid witness: pending obligations still reject */
            CHECK(ModelRetire(&m)==STATUS_DEVICE_NOT_READY && m.Permit.WriterAddress==&m.Writer);
            m.AllReturnsKnown=FALSE;
        }
        if(!lm)LowerComplete(&irp);if(item.Queued)RunWorker();
        CheckDone(&c,&lock,&irp);CHECK(!s.PendingAddress && !s.Rundown.Count);
        CHECK(ModelRetire(&m)==STATUS_DEVICE_BUSY); /* even zero tags/done is not ALL-return witness */
        ModelReadPublished(&m,FALSE);m.AllReturnsKnown=TRUE;
        CHECK(ModelRetire(&m)==(closing?STATUS_DEVICE_NOT_READY:STATUS_SUCCESS));
        CHECK(SourceZero(&m.Writer,sizeof(m.Writer)));
        ModelReadPublished(&m,!closing && !final);ModelDispose(&m);
    }
}
static VOID ReaderBeforeBridge(VOID)
{
    MODEL_FINAL m;BC250_SOURCE s;DEVICE_OBJECT p,l;BC250_COMPLETION c;IO_REMOVE_LOCK lock;IRP irp;
    BC250_PERMIT_READER pin={0};BC250_SOURCE_LEASE lease={0};ULONGLONG revision;unsigned before=forwards;
    ModelPrepare(&m,&s,&p,&l,&c,&lock,&irp,2000);
    CHECK(Bc250PermitAcquire(&m.Permit,&pin)==STATUS_SUCCESS);
    CHECK(Bc250SourceAcquire(&s,&lease)==STATUS_SUCCESS);
    CHECK(Bc250PermitStepEnter(&m.Permit,&pin,&revision)==STATUS_SUCCESS);
    irql=2;CHECK(Bc250PermitWriteAdmit(&m.Permit,6,&m.Writer)==STATUS_SUCCESS);irql=0;
    CHECK(ModelDispatch(&m,&p,&l,&lock,&irp)==STATUS_DEVICE_BUSY);
    CHECK(!model && CompletionZero(&c,sizeof(c)) && !s.PendingAddress && forwards==before && !irp.Completed);
    CHECK(Bc250PermitStepLeave(&m.Permit,&pin)==STATUS_SUCCESS);
    CHECK(Bc250SourceRelease(&s,&lease)==STATUS_SUCCESS);
    CHECK(ModelDispatch(&m,&p,&l,&lock,&irp)==STATUS_DEVICE_BUSY && forwards==before);
    CHECK(Bc250PermitRelease(&m.Permit,&pin)==STATUS_SUCCESS);
    CHECK(ModelDispatch(&m,&p,&l,&lock,&irp)==STATUS_PENDING);
    LowerComplete(&irp);RunWorker();CheckDone(&c,&lock,&irp);
    CHECK(ModelRetire(&m)==STATUS_DEVICE_BUSY);m.AllReturnsKnown=TRUE;
    CHECK(ModelRetire(&m)==STATUS_SUCCESS);ModelReadPublished(&m,TRUE);ModelDispose(&m);
}
static VOID ComposedSetupFailures(VOID)
{
    ULONG n;for(n=0;n<6;++n){
        MODEL_FINAL m;BC250_SOURCE s;DEVICE_OBJECT p,l;BC250_COMPLETION c;IO_REMOVE_LOCK lock;IRP irp;
        unsigned before=forwards;NTSTATUS st;
        ModelPrepare(&m,&s,&p,&l,&c,&lock,&irp,3000+n);
        CHECK(Bc250PermitWriteAdmit(&m.Permit,6,&m.Writer)==STATUS_SUCCESS);
        if(n<2)lock.RejectAt=n+1;if(n==2)failAlloc=1;if(n==3)failRegister=1;
        if(n==4)Bc250SourceMockAllowed=FALSE;if(n==5)Bc250CompletionMockAllowed=FALSE;
        st=ModelDispatch(&m,&p,&l,&lock,&irp);Bc250SourceMockAllowed=Bc250CompletionMockAllowed=TRUE;
        CHECK(st!=STATUS_PENDING && st!=STATUS_SUCCESS && forwards==before);
        if(n==5)CHECK(!m.Taken && !irp.Completed && CompletionZero(&c,sizeof(c)));
        else{CHECK(m.Taken);CheckDone(&c,&lock,&irp);}
        CHECK(ModelRetire(&m)==STATUS_DEVICE_BUSY);m.AllReturnsKnown=TRUE;
        CHECK(ModelRetire(&m)==(n==3?STATUS_DEVICE_NOT_READY:STATUS_SUCCESS));
        CHECK(SourceZero(&m.Writer,sizeof(m.Writer)));ModelReadPublished(&m,FALSE);ModelDispose(&m);
    }
}
static VOID ComposedUnknown(VOID)
{
    ULONG n;for(n=0;n<4;++n){
        MODEL_FINAL m;BC250_SOURCE s;DEVICE_OBJECT p,l;BC250_COMPLETION c;IO_REMOVE_LOCK lock;IRP irp,wrong={0};
        unsigned oldFrees=frees,oldCompletions=completions;
        ModelPrepare(&m,&s,&p,&l,&c,&lock,&irp,4000+n);
        CHECK(Bc250PermitWriteAdmit(&m.Permit,6,&m.Writer)==STATUS_SUCCESS);
        CHECK(ModelDispatch(&m,&p,&l,&lock,&irp)==STATUS_PENDING);
        if(n==0)lowerFinal=STATUS_PENDING;
        if(n==3){irql=2;CHECK(ObserveCallback(NULL,&wrong,&c)==STATUS_MORE_PROCESSING_REQUIRED);irql=0;}
        else LowerComplete(&irp);
        if(n==1){irql=2;CHECK(ObserveCallback(NULL,&irp,&c)==STATUS_MORE_PROCESSING_REQUIRED);irql=0;}
        if(n==2)c.Request.Id++; /* malicious fixture only; no guessed repair */
        if(item.Queued)RunWorker();
        CHECK(c.Phase==BC250_COMPLETION_FAULT && s.PendingAddress && s.Rundown.Count==1 && lock.Count==1);
        CHECK(!irp.Completed && frees==oldFrees && completions==oldCompletions);
        m.AllReturnsKnown=TRUE;CHECK(ModelRetire(&m)==STATUS_DEVICE_NOT_READY);
        CHECK(m.Permit.Closing && m.Permit.WriterAddress==&m.Writer && !SourceZero(&m.Writer,sizeof(m.Writer)));
        ModelReadPublished(&m,FALSE);model=NULL;
        /* Fake test world intentionally retains Source/bridge/work/remove-tag/
         * permit obligations; no production recovery or OS cancellation invented. */
    }
}
static int FrozenPermitFixtures(VOID)
{
    unsigned baseline;CHECK(FrozenCompletionFixtures()==0);baseline=checks;
    ComposedTimingMatrix();ReaderBeforeBridge();ComposedSetupFailures();ComposedUnknown();
    CHECK(!model && !held && !irql && totalObservations && totalSourceClean && totalDone && totalTagsZero);
    printf("PASS: permit completion composition %u checks, %u fixtures; %u boundary observations, retained until external fixture ALL-returns; no native wrapper/SMP/hardware.\n",
        checks-baseline,modelFixtures,totalObservations);
    return 0;
}

/* New composition: no AllReturnsKnown/depth counters authorize retirement.
 * Body counters below are poison assertions only, not acquisition authority. */
static VOID LifeObserve(ULONG point)
{
    BC250_LIFE_STATUS ls;BC250_PERMIT_SNAPSHOT ps;PVOID payload=(PVOID)1;
    CHECK(life && !held && model && activeBridge==model->Bridge);
    CHECK(Bc250LifeInspect(&life->Domain,&ls)==STATUS_SUCCESS && ls.Calls);
    CHECK(Bc250PermitInspect(&model->Permit,&ps)==STATUS_SUCCESS);
    CHECK(!ps.Published && ps.WriterPhase==2 && model->Permit.WriterAddress==&model->Writer);
    ++life->ObserveCount;
    if(activeBridge->Phase==BC250_COMPLETION_DONE)++lifeDoneEarly;
    if(activeBridge->Phase==BC250_COMPLETION_DONE && !activeBridge->HeldTags && !activeLock->Count)++lifeZeroTagsEarly;
    if(irql==0){CHECK(Bc250LifeDetach(&life->Domain,&payload)==STATUS_DEVICE_BUSY && !payload);}
    (VOID)point;
}
static NTSTATUS LifeCallback(PDEVICE_OBJECT device,PIRP irp,PVOID context)
{
    LIFE_PARENT *parent=context;LIFE_CHILD *child;PVOID payload=NULL;NTSTATUS st;ULONG known;
    CHECK(parent==life && parent->Callback);
    st=Bc250LifeEnterHeld(&parent->Domain,&parent->CallbackHold,&parent->CallbackCall,&payload);
    if(st!=STATUS_SUCCESS){CHECK(!payload);++parent->RejectedCallbacks;return STATUS_MORE_PROCESSING_REQUIRED;}
    child=payload;CHECK(child->Alive && context!=&child->Bridge);++child->ActiveBodies;
    CHECK(Bc250LifeReleaseHold(&parent->Domain,&parent->CallbackHold)==STATUS_DEVICE_BUSY);
    LifeObserve(7);st=parent->Callback(device,irp,&child->Bridge);LifeObserve(7);
    known=child->Bridge.Phase!=BC250_COMPLETION_FAULT;
    if(!known)parent->Quarantined=1;
    CHECK(child->ActiveBodies);--child->ActiveBodies;
    CHECK(Bc250LifeExit(&parent->Domain,&parent->CallbackCall)==STATUS_SUCCESS);
    /* NO child/IRP access after Exit. Stable Parent/token/code only. */
    if(known)CHECK(Bc250LifeReleaseHold(&parent->Domain,&parent->CallbackHold)==STATUS_SUCCESS);
    return st;
}
static VOID LifeWorker(PDEVICE_OBJECT device,PVOID context)
{
    LIFE_PARENT *parent=context;LIFE_CHILD *child;PVOID payload=NULL;NTSTATUS st;ULONG known;
    CHECK(parent==life && parent->Worker);
    st=Bc250LifeEnterHeld(&parent->Domain,&parent->WorkerHold,&parent->WorkerCall,&payload);
    if(st!=STATUS_SUCCESS){CHECK(!payload);++parent->RejectedWorkers;return;}
    child=payload;CHECK(child->Alive);++child->ActiveBodies;
    CHECK(Bc250LifeReleaseHold(&parent->Domain,&parent->WorkerHold)==STATUS_DEVICE_BUSY);
    LifeObserve(8);parent->Worker(device,&child->Bridge);LifeObserve(8);
    known=child->Bridge.Phase==BC250_COMPLETION_DONE && !child->Bridge.RequestOwned &&
        !child->Bridge.Work && !child->Bridge.Irp && !(child->Bridge.HeldTags&BC250_COMPLETION_WORKER_TAG) &&
        !child->Source.PendingAddress && !child->Source.Fault && !item.Allocated;
    if(!known)parent->Quarantined=1;
    CHECK(child->ActiveBodies);--child->ActiveBodies;
    CHECK(Bc250LifeExit(&parent->Domain,&parent->WorkerCall)==STATUS_SUCCESS);
    if(known)CHECK(Bc250LifeReleaseHold(&parent->Domain,&parent->WorkerHold)==STATUS_SUCCESS);
    /* NO child/work/IRP access after Exit or final hold release. */
}
static NTSTATUS LifeRetire(LIFE_PARENT *parent)
{
    LIFE_CHILD *child;PVOID payload=NULL;NTSTATUS st;
    st=Bc250LifeDetach(&parent->Domain,&payload);if(st!=STATUS_SUCCESS){CHECK(!payload);return st;}
    child=payload;CHECK(child->Alive && !child->ActiveBodies && !parent->Quarantined);
    CHECK(!child->M.AllReturnsKnown); /* old fixture witness NEVER asserted here */
    if(child->M.Taken){
        CHECK(child->Bridge.Phase==BC250_COMPLETION_DONE && !child->Bridge.HeldTags &&
            !child->Bridge.RequestOwned && !child->Bridge.Work && !child->Bridge.Irp &&
            CompletionZero(&child->Bridge.Request,sizeof(child->Bridge.Request)));
    }else CHECK(CompletionZero(&child->Bridge,sizeof(child->Bridge)));
    CHECK(!child->Source.PendingAddress && !child->Source.Rundown.Count && !activeLock->Count && !item.Allocated && !item.Queued);
    CHECK(child->M.Permit.WriterAddress==&child->M.Writer);
    /* Exclusive detached RAM owner, still retaining claimed metadata writer.
     * Cleanup policy here is a TEST coordinator, NOT native OS authority. */
    CHECK(Bc250SourceClose(&child->Source)==STATUS_SUCCESS);
    CHECK(Bc250SourceDrain(&child->Source)==STATUS_SUCCESS);
    CHECK(Bc250PermitClose(&child->M.Permit)==STATUS_SUCCESS);
    CHECK(Bc250PermitWriteFinish(&child->M.Permit,&child->M.Writer,STATUS_CANCELLED,0)==STATUS_DEVICE_NOT_READY);
    CHECK(SourceZero(&child->M.Writer,sizeof(child->M.Writer)));
    lifeObservations+=parent->ObserveCount;model=NULL;activeSource=NULL;activeBridge=NULL;
    memset(child,0xdd,sizeof(*child));parent->Detached=1;parent->RetireResult=STATUS_SUCCESS;
    return STATUS_SUCCESS;
}
static VOID LifePrepare(LIFE_PARENT *parent,LIFE_CHILD *child,DEVICE_OBJECT *p,DEVICE_OBJECT *l,
    IO_REMOVE_LOCK *lock,IRP *irp,ULONGLONG scope)
{
    PVOID payload=NULL;CHECK(!life && !model);
    memset(parent,0,sizeof(*parent));memset(child,0,sizeof(*child));
    ModelPrepare(&child->M,&child->Source,p,l,&child->Bridge,lock,irp,scope);child->Alive=1;
    CHECK(Bc250PermitWriteAdmit(&child->M.Permit,6,&child->M.Writer)==STATUS_SUCCESS);
    CHECK(Bc250PermitWriteClaim(&child->M.Permit,&child->M.Writer)==STATUS_SUCCESS);
    parent->Key=scope;CHECK(Bc250LifeInit(&parent->Domain,scope)==STATUS_SUCCESS);
    CHECK(Bc250LifePublish(&parent->Domain,parent->Key,1,child)==STATUS_SUCCESS);
    CHECK(Bc250LifeEnter(&parent->Domain,parent->Key,&parent->DispatchCall,&payload)==STATUS_SUCCESS && payload==child);
    CHECK(Bc250LifeHold(&parent->Domain,parent->Key,&parent->CallbackHold)==STATUS_SUCCESS);
    CHECK(Bc250LifeHold(&parent->Domain,parent->Key,&parent->WorkerHold)==STATUS_SUCCESS);
    /* Reserve BOTH future envelopes before original registration/forward/queue.
     * Close prevents further key entries; admitted body/held cleanup may finish. */
    CHECK(Bc250LifeClose(&parent->Domain)==STATUS_SUCCESS);
    life=parent;model=&child->M;++lifeFixtures;
}
static NTSTATUS LifeDispatchBody(LIFE_PARENT *parent,LIFE_CHILD *child,DEVICE_OBJECT *p,DEVICE_OBJECT *l,
    IO_REMOVE_LOCK *lock,IRP *irp)
{
    NTSTATUS st;CHECK(child->Alive && parent->DispatchCall.Kind==BC250_LIFE_CALL);++child->ActiveBodies;
    st=Bc250CompletionDispatch(&child->Bridge,&child->Source,p,l,lock,irp,6,1,NULL,&child->M.Taken);
    child->M.DispatchStatus=st;parent->DispatchResult=st;
    CHECK(child->ActiveBodies);--child->ActiveBodies;
    CHECK(Bc250LifeExit(&parent->Domain,&parent->DispatchCall)==STATUS_SUCCESS);
    /* All child accesses/body have finished. From here only parent envelopes. */
    if(!parent->Forwarded){
        CHECK(!parent->Registered && !parent->Queued); /* explicit fake API handshake */
        CHECK(Bc250LifeReleaseHold(&parent->Domain,&parent->CallbackHold)==STATUS_SUCCESS);
        CHECK(Bc250LifeReleaseHold(&parent->Domain,&parent->WorkerHold)==STATUS_SUCCESS);
    }
    return st;
}
static VOID LifeTiming(VOID)
{
    ULONG lm,wm,ret,final,closing;
    for(lm=0;lm<2;++lm)for(wm=0;wm<2;++wm)for(ret=0;ret<3;++ret)
    for(final=0;final<2;++final)for(closing=0;closing<3;++closing){
        LIFE_PARENT parent;LIFE_CHILD child;DEVICE_OBJECT p,l;IO_REMOVE_LOCK lock;IRP irp;
        BC250_LIFE_TOKEN denied={0};PVOID payload=(PVOID)1;unsigned before=checks;
        LifePrepare(&parent,&child,&p,&l,&lock,&irp,5000+lm*36+wm*18+ret*6+final*3+closing);
        inlineLower=lm;inlineWorker=wm;lowerReturn=ret==0?STATUS_SUCCESS:ret==1?STATUS_PENDING:STATUS_CANCELLED;
        lowerFinal=final?STATUS_CANCELLED:STATUS_SUCCESS;closeDuringQueue=closing==1;child.M.CloseMode=closing;
        CHECK(Bc250LifeEnter(&parent.Domain,parent.Key,&denied,&payload)==STATUS_DEVICE_NOT_READY && !payload);
        CHECK(LifeDispatchBody(&parent,&child,&p,&l,&lock,&irp)==STATUS_PENDING);
        if(!lm || !wm)CHECK(LifeRetire(&parent)==STATUS_DEVICE_BUSY);
        if(!lm)LowerComplete(&irp);
        if(item.Queued){parent.HookRetire=closing==2;RunWorker();}
        if(!parent.Detached)CHECK(LifeRetire(&parent)==STATUS_SUCCESS);
        CHECK(parent.Detached && parent.RetireResult==STATUS_SUCCESS && !parent.Quarantined);
        CHECK(!parent.Domain.Payload && !parent.Domain.Calls && !parent.Domain.Holds);
        CHECK(irp.Completed==1 && !lock.Count && !item.Allocated && !model && !activeSource && !activeBridge);
        if(closing==2 && !wm)CHECK(parent.HelperRetire==1);
        CHECK(p.Fake==0 && l.Fake==0 && checks>before);
        /* Late callback/worker envelopes are rejected BEFORE any child/IRP use.
         * Parent/code/envelopes stay resident under fixture's external anchor. */
        irql=2;CHECK(LifeCallback(NULL,&irp,&parent)==STATUS_MORE_PROCESSING_REQUIRED);irql=0;
        LifeWorker(NULL,&parent);CHECK(parent.RejectedCallbacks==1 && parent.RejectedWorkers==1);
        CHECK(!parent.Domain.Payload);life=NULL;
    }
}
static VOID LifeSetupFailures(VOID)
{
    ULONG n;for(n=0;n<6;++n){
        LIFE_PARENT parent;LIFE_CHILD child;DEVICE_OBJECT p,l;IO_REMOVE_LOCK lock;IRP irp;
        NTSTATUS st;unsigned oldForwards=forwards,oldQueues=queues;
        LifePrepare(&parent,&child,&p,&l,&lock,&irp,6000+n);
        if(n<2)lock.RejectAt=n+1;if(n==2)failAlloc=1;if(n==3)failRegister=1;
        if(n==4)Bc250SourceMockAllowed=FALSE;if(n==5)Bc250CompletionMockAllowed=FALSE;
        st=LifeDispatchBody(&parent,&child,&p,&l,&lock,&irp);
        Bc250SourceMockAllowed=Bc250CompletionMockAllowed=TRUE;
        CHECK(st!=STATUS_SUCCESS && st!=STATUS_PENDING && !parent.Forwarded && !parent.Registered && !parent.Queued);
        CHECK(forwards==oldForwards && queues==oldQueues);
        CHECK(LifeRetire(&parent)==STATUS_SUCCESS && parent.Detached && !model);
        CHECK(irp.Completed==(n==5?0U:1U) && !lock.Count && p.Fake==0 && l.Fake==0);life=NULL;
    }
}
static VOID LifeUnknown(VOID)
{
    ULONG n;for(n=0;n<5;++n){
        LIFE_PARENT parent;LIFE_CHILD child;DEVICE_OBJECT p,l;IO_REMOVE_LOCK lock;IRP irp,wrong={0};
        unsigned oldFrees=frees,oldCompletions=completions;PVOID payload=(PVOID)1;
        LifePrepare(&parent,&child,&p,&l,&lock,&irp,7000+n);
        CHECK(LifeDispatchBody(&parent,&child,&p,&l,&lock,&irp)==STATUS_PENDING);
        if(n==0)lowerFinal=STATUS_PENDING;
        if(n==4)lowerFinal=(NTSTATUS)0x80000005U; /* Source fault despite completed IRP/DONE */
        if(n==1){irql=2;CHECK(LifeCallback(NULL,&wrong,&parent)==STATUS_MORE_PROCESSING_REQUIRED);irql=0;}
        else LowerComplete(&irp);
        if(n==2)child.Bridge.Request.Id++; /* deliberately malicious serial fixture */
        if(n==3)parent.WorkerHold.Id++;
        if(item.Queued)RunWorker();
        CHECK(Bc250LifeDetach(&parent.Domain,&payload)==STATUS_DEVICE_BUSY && !payload);
        CHECK(!parent.Detached && parent.Domain.Holds && !parent.Domain.Calls && child.Alive && !child.ActiveBodies);
        CHECK(child.M.Permit.WriterAddress==&child.M.Writer && !SourceZero(&child.M.Writer,sizeof(child.M.Writer)));
        if(n==4){
            CHECK(irp.Completed==1 && frees==oldFrees+1 && completions==oldCompletions+1 &&
                !child.Source.PendingAddress && child.Source.Fault && child.Bridge.Phase==BC250_COMPLETION_DONE && !lock.Count);
        }else CHECK(!irp.Completed && frees==oldFrees && completions==oldCompletions && child.Source.PendingAddress);
        CHECK(n==3?parent.RejectedWorkers==1:parent.Quarantined==1);
        CHECK(LifeRetire(&parent)==STATUS_DEVICE_BUSY);
        /* Unknown obligations retained without repair or forced retirement.
         * Fake RAM fixture ends, not an OS cleanup/recovery implementation. */
        life=NULL;model=NULL;activeSource=NULL;activeBridge=NULL;
    }
}
static VOID LifeDuplicateBeforeWorker(VOID)
{
    LIFE_PARENT parent;LIFE_CHILD child;DEVICE_OBJECT p,l;IO_REMOVE_LOCK lock;IRP irp;
    LifePrepare(&parent,&child,&p,&l,&lock,&irp,8000);
    CHECK(LifeDispatchBody(&parent,&child,&p,&l,&lock,&irp)==STATUS_PENDING);LowerComplete(&irp);
    irql=2;CHECK(LifeCallback(NULL,&irp,&parent)==STATUS_MORE_PROCESSING_REQUIRED);irql=0;
    CHECK(parent.RejectedCallbacks==1 && child.Bridge.Phase==BC250_COMPLETION_QUEUED && !parent.Quarantined);
    CHECK(LifeRetire(&parent)==STATUS_DEVICE_BUSY);RunWorker();CHECK(LifeRetire(&parent)==STATUS_SUCCESS);
    CHECK(parent.Detached && irp.Completed==1 && p.Fake==0 && l.Fake==0);life=NULL;
}
int main(VOID)
{
    unsigned baseline;CHECK(FrozenPermitFixtures()==0);baseline=checks;
    LifeTiming();LifeSetupFailures();LifeUnknown();LifeDuplicateBeforeWorker();
    CHECK(!life && !model && !held && !irql && lifeObservations && lifeDoneEarly && lifeZeroTagsEarly);
    printf("PASS: lifetime completion composition %u checks, %u fixtures; %u boundaries; child drained by tokens, no ALL-returns boolean; external Parent/module anchor, no native/SMP/hardware.\n",
        checks-baseline,lifeFixtures,lifeObservations);
    return 0;
}
