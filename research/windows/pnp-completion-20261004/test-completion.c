/* SPDX-License-Identifier: Apache-2.0 */
#define BC250_PNP_COMPLETION_MOCK 1
#define BC250_PNP_SOURCE_MOCK 1
#define BC250_PNP_CAPTURE_TYPES_MOCK 1
#define BC250_PCI_TYPES_MOCK 1
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "bc250_pnp_completion.c"
#include "../pnp-source-20261004/bc250_pnp_source.c"
BOOLEAN Bc250CompletionMockAllowed=TRUE,Bc250SourceMockAllowed=TRUE;
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
#define CHECK(x) do { ++checks;if(!(x)) { fprintf(stderr,"FAIL line %u: %s\n",(unsigned)__LINE__,#x);exit(1); } } while(0)
unsigned char KeGetCurrentIrql(VOID) { return irql; }
PVOID KeGetCurrentThread(VOID) { return (PVOID)(ULONG_PTR)currentThread; }
SIZE_T RtlCompareMemory(const VOID *a,const VOID *b,SIZE_T n)
{ SIZE_T k;const UCHAR *x=a,*y=b;for(k=0;k<n && x[k]==y[k];++k){}return k; }
VOID KeInitializeSpinLock(KSPIN_LOCK *p) { p->Initialized=1;p->Held=0; }
VOID KeAcquireSpinLock(KSPIN_LOCK *p,KIRQL *old)
{ CHECK(p->Initialized && !p->Held && !held && irql<=2);*old=irql;irql=2;p->Held=1;held=1; }
VOID KeReleaseSpinLock(KSPIN_LOCK *p,KIRQL old)
{ CHECK(p->Held && held && irql==2);p->Held=0;held=0;irql=old; }
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
    for(n=0;n<8 && p->Tags[n]!=tag;++n){}CHECK(n<8);p->Tags[n]=NULL;--p->Count;
}
PIO_WORKITEM IoAllocateWorkItem(PDEVICE_OBJECT device)
{
    CHECK(!held && irql==0);if(failAlloc)return NULL;CHECK(!item.Allocated);
    memset(&item,0,sizeof(item));item.Allocated=1;item.Device=device;++allocs;return &item;
}
VOID IoFreeWorkItem(PIO_WORKITEM p)
{ CHECK(!held && p==&item && p->Allocated && !p->Queued);p->Allocated=0;++frees; }
VOID IoCopyCurrentIrpStackLocationToNext(PIRP p) { CHECK(!held && !p->Completed);++p->StackCopied; }
NTSTATUS IoSetCompletionRoutineEx(PDEVICE_OBJECT d,PIRP p,PIO_COMPLETION_ROUTINE fn,PVOID ctx,BOOLEAN a,BOOLEAN b,BOOLEAN c)
{
    CHECK(!held && d && p->StackCopied==1 && !p->Routine && a && b && c);
    if(failRegister)return STATUS_INSUFFICIENT_RESOURCES;
    p->Routine=fn;p->Context=ctx;return STATUS_SUCCESS;
}
VOID IoMarkIrpPending(PIRP p) { CHECK(!held && !p->Completed);++p->Marked; }
VOID IoCompleteRequest(PIRP p,char boost)
{
    CHECK(!held && boost==0 && !p->Completed && p->IoStatus.Status!=STATUS_PENDING);
    if(p->Marked)CHECK(p->IoStatus.Status==lowerFinal && p->IoStatus.Information==lowerInformation);
    CHECK(activeLock!=NULL);++p->Completed;++completions;
    /* Poison status at the ownership boundary; code must not read it later. */
    p->Routine=NULL;p->Context=NULL;p->IoStatus.Status=(NTSTATUS)0xdeadbeefU;p->IoStatus.Information=~(ULONG_PTR)0;
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
    p->Routine=fn;p->Context=ctx;p->Queued=1;++queues;
    sourcePendingAtQueue=activeSource->PendingAddress!=NULL;
    CHECK(sourcePendingAtQueue && activeLock->Count!=0);
    if(closeDuringQueue)CHECK(Bc250SourceClose(activeSource)==STATUS_SUCCESS);
    if(inlineWorker) {
        RunWorker();earlyWorkerObserved=1;
        CHECK(activeBridge->Phase==BC250_COMPLETION_DONE && activeLock->Count==(inlineLower?1U:0U));
    }
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
    if(inlineLower)LowerComplete(p);
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
int main(VOID)
{
    TimingMatrix();SetupFailures();PowerAndStatus();UnknownAndDuplicates();CHECK(!held && !irql && allocs>=frees);
    printf("PASS: deferred completion %u checks; timing/pre-forward/pending/RemoveLock/quarantine model; no real IRP/SMP/hardware.\n",checks);
    return 0;
}
