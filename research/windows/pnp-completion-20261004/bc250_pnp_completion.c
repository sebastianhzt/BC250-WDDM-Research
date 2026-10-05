/* SPDX-License-Identifier: Apache-2.0 */
#include "bc250_pnp_completion.h"
#ifdef BC250_PNP_COMPLETION_MOCK
extern BOOLEAN Bc250CompletionMockAllowed;
#endif
static BOOLEAN CompletionEnabled(VOID)
{
#ifdef BC250_PNP_COMPLETION_MOCK
    return Bc250CompletionMockAllowed;
#else
    return FALSE;
#endif
}
static NTSTATUS CompletionPolicy(BOOLEAN dispatch)
{
    if(!CompletionEnabled())return STATUS_NOT_SUPPORTED;
    return KeGetCurrentIrql()<=(dispatch?2U:PASSIVE_LEVEL)?STATUS_SUCCESS:STATUS_INVALID_DEVICE_STATE;
}
static BOOLEAN CompletionZero(const VOID *p,SIZE_T n)
{ SIZE_T k;const UCHAR *b=p;for(k=0;k<n;++k)if(b[k])return FALSE;return TRUE; }
/* Caller holds context Lock. Never guesses ownership from an external status. */
static VOID CompletionFaultLocked(BC250_COMPLETION *c)
{ c->Phase=BC250_COMPLETION_FAULT; }
static VOID CompletionDropDispatch(BC250_COMPLETION *c)
{
    PIO_REMOVE_LOCK lock=c->RemoveLock;PVOID tag=&c->DispatchTag;KIRQL old;
    KeAcquireSpinLock(&c->Lock,&old);c->HeldTags&=~BC250_COMPLETION_DISPATCH_TAG;
    KeReleaseSpinLock(&c->Lock,old);IoReleaseRemoveLock(lock,tag); /* No c access after. */
}
/* Known pre-forward failure, caller context still exclusively prepared. */
static NTSTATUS CompletionBeforeForward(BC250_COMPLETION *c,NTSTATUS error)
{
    PIRP irp=c->Irp;PIO_REMOVE_LOCK lock=c->RemoveLock;PVOID workerTag=&c->WorkerTag;
    PIO_WORKITEM work=c->Work;BOOLEAN requestClean=TRUE;ULONG held=c->HeldTags;KIRQL old;NTSTATUS sourceStatus=STATUS_SUCCESS;
    if(c->RequestOwned) {
        Bc250SourceClose(c->Source); /* Do not roll query back to Ready on setup failure. */
        sourceStatus=Bc250SourceComplete(c->Source,&c->Request,error,NULL);
        requestClean=CompletionZero(&c->Request,sizeof(c->Request));
    }
    KeAcquireSpinLock(&c->Lock,&old);c->SourceStatus=sourceStatus;
    c->RequestOwned=requestClean?0U:1U;
    c->Irp=NULL;c->Phase=requestClean?BC250_COMPLETION_DONE:BC250_COMPLETION_FAULT;
    if(requestClean)c->Work=NULL;
    KeReleaseSpinLock(&c->Lock,old);
    irp->IoStatus.Status=error;irp->IoStatus.Information=0;
    IoCompleteRequest(irp,IO_NO_INCREMENT); /* IRP now inaccessible. */
    if(requestClean) {
        if(work)IoFreeWorkItem(work);
        if(held&BC250_COMPLETION_WORKER_TAG) {
            KeAcquireSpinLock(&c->Lock,&old);c->HeldTags&=~BC250_COMPLETION_WORKER_TAG;
            KeReleaseSpinLock(&c->Lock,old);IoReleaseRemoveLock(lock,workerTag);
        }
    }
    if(held&BC250_COMPLETION_DISPATCH_TAG)CompletionDropDispatch(c);
    /* No c/IRP access after final dispatch-tag release. */
    return error;
}
NTSTATUS Bc250CompletionDispatch(BC250_COMPLETION *c,BC250_SOURCE *source,PDEVICE_OBJECT fdo,
    PDEVICE_OBJECT lower,PIO_REMOVE_LOCK lock,PIRP irp,ULONG kind,ULONG target,
    const BC250_SOURCE_RESOURCES *resources,BOOLEAN *taken)
{
    NTSTATUS st;KIRQL old;PIO_WORKITEM work;
    if(taken)*taken=FALSE;st=CompletionPolicy(FALSE);if(st!=STATUS_SUCCESS)return st;
    if(!c || !source || !fdo || !lower || !lock || !irp || !taken || !CompletionZero(c,sizeof(*c)))
        return STATUS_INVALID_PARAMETER; /* caller still owns IRP */
    c->Source=source;c->Fdo=fdo;c->Lower=lower;c->RemoveLock=lock;c->Irp=irp;
    c->Phase=BC250_COMPLETION_PREPARED;KeInitializeSpinLock(&c->Lock);
    if(resources) { c->Resources=*resources;c->HaveResources=1; }
    c->Signature=BC250_COMPLETION_SIGNATURE;*taken=TRUE;
    st=IoAcquireRemoveLock(lock,&c->DispatchTag);
    if(st!=STATUS_SUCCESS)return CompletionBeforeForward(c,st);
    KeAcquireSpinLock(&c->Lock,&old);c->HeldTags=BC250_COMPLETION_DISPATCH_TAG;KeReleaseSpinLock(&c->Lock,old);
    st=IoAcquireRemoveLock(lock,&c->WorkerTag);
    if(st!=STATUS_SUCCESS)return CompletionBeforeForward(c,st);
    KeAcquireSpinLock(&c->Lock,&old);c->HeldTags|=BC250_COMPLETION_WORKER_TAG;KeReleaseSpinLock(&c->Lock,old);
    work=IoAllocateWorkItem(fdo);
    if(!work)return CompletionBeforeForward(c,STATUS_INSUFFICIENT_RESOURCES);
    KeAcquireSpinLock(&c->Lock,&old);c->Work=work;KeReleaseSpinLock(&c->Lock,old);
    st=Bc250SourceBegin(source,kind,target,&c->Request);
    if(st!=STATUS_SUCCESS)return CompletionBeforeForward(c,st);
    KeAcquireSpinLock(&c->Lock,&old);c->RequestOwned=1;c->Phase=BC250_COMPLETION_REGISTERED;KeReleaseSpinLock(&c->Lock,old);
    IoCopyCurrentIrpStackLocationToNext(irp);
    st=IoSetCompletionRoutineEx(fdo,irp,Bc250CompletionCallback,c,TRUE,TRUE,TRUE);
    if(st!=STATUS_SUCCESS)return CompletionBeforeForward(c,st);
    /* Nothing fallible between successful registration and mandatory forward. */
    IoMarkIrpPending(irp);
    (VOID)IoCallDriver(lower,irp); /* final IO_STATUS_BLOCK, not this return, drives worker */
    CompletionDropDispatch(c); /* c/IRP can no longer be accessed by this path. */
    return STATUS_PENDING;
}
_Use_decl_annotations_
NTSTATUS Bc250CompletionCallback(PDEVICE_OBJECT device,PIRP irp,PVOID context)
{
    BC250_COMPLETION *c=context;PIO_WORKITEM work;KIRQL old;BC250_SOURCE *source;
    NTSTATUS st=CompletionPolicy(TRUE);(VOID)device;
    if(st!=STATUS_SUCCESS)return st;
    if(!c || c->Signature!=BC250_COMPLETION_SIGNATURE)return STATUS_MORE_PROCESSING_REQUIRED;
    KeAcquireSpinLock(&c->Lock,&old);
    if(!irp || c->Irp!=irp || c->Phase!=BC250_COMPLETION_REGISTERED || !c->RequestOwned || !c->Work) {
        source=c->Source;CompletionFaultLocked(c);KeReleaseSpinLock(&c->Lock,old);
        Bc250SourceClose(source);return STATUS_MORE_PROCESSING_REQUIRED;
    }
    if(irp->IoStatus.Status==STATUS_PENDING) {
        source=c->Source;CompletionFaultLocked(c);KeReleaseSpinLock(&c->Lock,old);
        Bc250SourceClose(source);return STATUS_MORE_PROCESSING_REQUIRED; /* unknown finality, retain */
    }
    c->FinalStatus=irp->IoStatus.Status;c->FinalInformation=irp->IoStatus.Information;
    work=c->Work;c->Phase=BC250_COMPLETION_QUEUED;
    KeReleaseSpinLock(&c->Lock,old);
    if(irp->PendingReturned)IoMarkIrpPending(irp);
    IoQueueWorkItem(work,Bc250CompletionWorker,DelayedWorkQueue,c);
    /* Work can already be done: NO irp/c/work dereference from here. */
    return STATUS_MORE_PROCESSING_REQUIRED;
}
_Use_decl_annotations_
VOID Bc250CompletionWorker(PDEVICE_OBJECT device,PVOID context)
{
    BC250_COMPLETION *c=context;KIRQL old;PIRP irp;PIO_WORKITEM work;
    PIO_REMOVE_LOCK lock;PVOID tag;NTSTATUS finalStatus,sourceStatus;ULONG_PTR information;
    (VOID)device;
    if(CompletionPolicy(FALSE)!=STATUS_SUCCESS || !c || c->Signature!=BC250_COMPLETION_SIGNATURE)return;
    KeAcquireSpinLock(&c->Lock,&old);
    if(c->Phase!=BC250_COMPLETION_QUEUED) {
        CompletionFaultLocked(c);KeReleaseSpinLock(&c->Lock,old);Bc250SourceClose(c->Source);return;
    }
    c->Phase=BC250_COMPLETION_RUNNING;irp=c->Irp;work=c->Work;
    lock=c->RemoveLock;tag=&c->WorkerTag;finalStatus=c->FinalStatus;information=c->FinalInformation;
    KeReleaseSpinLock(&c->Lock,old);
    sourceStatus=Bc250SourceComplete(c->Source,&c->Request,finalStatus,c->HaveResources?&c->Resources:NULL);
    KeAcquireSpinLock(&c->Lock,&old);c->SourceStatus=sourceStatus;
    if(!CompletionZero(&c->Request,sizeof(c->Request))) {
        CompletionFaultLocked(c);KeReleaseSpinLock(&c->Lock,old);
        Bc250SourceClose(c->Source);return; /* Unknown source ref: do NOT free/complete/guess. */
    }
    c->RequestOwned=0;
    if(c->Phase!=BC250_COMPLETION_RUNNING) { CompletionFaultLocked(c);KeReleaseSpinLock(&c->Lock,old);return; }
    c->Irp=NULL;c->Work=NULL;c->Phase=BC250_COMPLETION_DONE;
    KeReleaseSpinLock(&c->Lock,old);
    irp->IoStatus.Status=finalStatus;irp->IoStatus.Information=information;
    IoCompleteRequest(irp,IO_NO_INCREMENT); /* Never touch irp again. */
    IoFreeWorkItem(work); /* system dequeued it before worker ran */
    KeAcquireSpinLock(&c->Lock,&old);c->HeldTags&=~BC250_COMPLETION_WORKER_TAG;
    KeReleaseSpinLock(&c->Lock,old);
    IoReleaseRemoveLock(lock,tag); /* No c access after final request-tag release. */
}
NTSTATUS Bc250CompletionInspect(BC250_COMPLETION *c,BC250_COMPLETION_STATUS *out)
{
    NTSTATUS st;KIRQL old;if(out)RtlZeroMemory(out,sizeof(*out));
    st=CompletionPolicy(FALSE);if(st!=STATUS_SUCCESS)return st;
    if(!c || !out || c->Signature!=BC250_COMPLETION_SIGNATURE)return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&c->Lock,&old);out->Phase=c->Phase;out->HeldTags=c->HeldTags;
    out->RequestOwned=c->RequestOwned;out->WorkPresent=c->Work!=NULL;out->IrpPresent=c->Irp!=NULL;
    out->SourceStatus=c->SourceStatus;KeReleaseSpinLock(&c->Lock,old);
    return STATUS_SUCCESS; /* historical only, never reclaim authorization */
}
