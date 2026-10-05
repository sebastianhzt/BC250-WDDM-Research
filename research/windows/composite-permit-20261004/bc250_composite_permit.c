/* SPDX-License-Identifier: Apache-2.0 */
#include "bc250_composite_permit.h"
#ifdef BC250_PERMIT_MOCK
extern BOOLEAN Bc250PermitMockAllowed;
#endif
static NTSTATUS PermitPolicy(BOOLEAN dispatch)
{
#ifdef BC250_PERMIT_MOCK
    if(!Bc250PermitMockAllowed)return STATUS_NOT_SUPPORTED;
    return KeGetCurrentIrql()<=(dispatch?2U:PASSIVE_LEVEL)?STATUS_SUCCESS:STATUS_INVALID_DEVICE_STATE;
#else
    (VOID)dispatch;return STATUS_NOT_SUPPORTED;
#endif
}
static NTSTATUS PermitGuard(BC250_PERMIT *r,BOOLEAN dispatch)
{ NTSTATUS st=PermitPolicy(dispatch);if(st!=STATUS_SUCCESS)return st;
  return r && r->Signature==BC250_PERMIT_SIGNATURE?STATUS_SUCCESS:STATUS_INVALID_PARAMETER; }
static BOOLEAN PermitZero(const VOID *p,SIZE_T n)
{ SIZE_T k;const UCHAR *b=p;for(k=0;k<n;++k)if(b[k])return FALSE;return TRUE; }
/* Helpers below require gate Lock. No callbacks, nested locks, waits or DDIs. */
static VOID PermitFault(BC250_PERMIT *r){r->Closing=r->Fault=1;r->Published=0;}
static BOOLEAN PermitAdvance(BC250_PERMIT *r)
{ if(r->Epoch>=BC250_PERMIT_LIMIT-1U){PermitFault(r);return FALSE;}++r->Epoch;return TRUE; }
static BOOLEAN PermitWriterMatch(BC250_PERMIT *r,const BC250_PERMIT_WRITER *t)
{
    const BC250_PERMIT_WRITER *s=&r->WriterSeal;
    return t && r->WriterAddress==t && t->Root==r && t->Root==s->Root &&
        t->ScopeId==s->ScopeId && t->Id==s->Id && t->Generation==s->Generation &&
        t->Epoch==s->Epoch && t->Kind==s->Kind;
}
static VOID PermitWriterClear(BC250_PERMIT *r,BC250_PERMIT_WRITER *t)
{ RtlZeroMemory(t,sizeof(*t));RtlZeroMemory(&r->WriterSeal,sizeof(r->WriterSeal));r->WriterAddress=NULL;r->WriterPhase=0; }
static BC250_PERMIT_SLOT *PermitSlot(BC250_PERMIT *r,const BC250_PERMIT_READER *t)
{ ULONG n;for(n=0;n<BC250_PERMIT_SLOTS;++n)if(t && r->Readers[n].Address==t)return &r->Readers[n];return NULL; }
static BOOLEAN PermitReaderMatch(BC250_PERMIT *r,const BC250_PERMIT_READER *t,BC250_PERMIT_SLOT *slot,PVOID thread)
{
    const BC250_PERMIT_READER *s;if(!slot || !t)return FALSE;s=&slot->Seal;
    return t->Root==r && t->Root==s->Root && t->Thread==thread && t->Thread==s->Thread &&
        t->ScopeId==s->ScopeId && t->Id==s->Id && t->Revision==s->Revision &&
        t->Generation==s->Generation && t->Epoch==s->Epoch;
}
NTSTATUS Bc250PermitInit(BC250_PERMIT *r,ULONGLONG scope,ULONG generation)
{
    NTSTATUS st=PermitPolicy(FALSE);if(st!=STATUS_SUCCESS)return st;
    if(!r || !scope || !generation || generation>=BC250_PERMIT_LIMIT || !PermitZero(r,sizeof(*r)))return STATUS_INVALID_PARAMETER;
    KeInitializeSpinLock(&r->Lock);r->ScopeId=scope;r->Generation=generation;r->Epoch=1;
    r->Signature=BC250_PERMIT_SIGNATURE;return STATUS_SUCCESS; /* initially unpublished */
}
NTSTATUS Bc250PermitWriteAdmit(BC250_PERMIT *r,ULONG kind,BC250_PERMIT_WRITER *t)
{
    KIRQL old;NTSTATUS st=PermitGuard(r,TRUE);if(st!=STATUS_SUCCESS)return st;
    if(!t || !PermitZero(t,sizeof(*t)) || kind<1 || kind>6)return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&r->Lock,&old);
    if(r->Closing || r->Fault)st=STATUS_DEVICE_NOT_READY;
    else if(r->WriterAddress){(VOID)PermitAdvance(r);PermitFault(r);st=STATUS_DEVICE_BUSY;}
    else if(r->LastId==~(ULONGLONG)0 || !PermitAdvance(r)){PermitFault(r);st=STATUS_DATA_ERROR;}
    else {
        r->Published=0;++r->LastId;t->Root=r;t->ScopeId=r->ScopeId;t->Id=r->LastId;
        t->Generation=r->Generation;t->Epoch=r->Epoch;t->Kind=kind;
        r->WriterAddress=t;r->WriterSeal=*t;r->WriterPhase=1;st=STATUS_SUCCESS;
    }
    KeReleaseSpinLock(&r->Lock,old);return st;
}
NTSTATUS Bc250PermitWriteClaim(BC250_PERMIT *r,const BC250_PERMIT_WRITER *t)
{
    KIRQL old;NTSTATUS st=PermitGuard(r,FALSE);if(st!=STATUS_SUCCESS)return st;
    KeAcquireSpinLock(&r->Lock,&old);
    if(!PermitWriterMatch(r,t))st=STATUS_INVALID_PARAMETER;
    else if(r->Closing || r->Fault)st=STATUS_DEVICE_NOT_READY;
    else if(r->WriterPhase!=1 || r->Pins || r->Steps)st=STATUS_DEVICE_BUSY;
    else if(!PermitAdvance(r))st=STATUS_DATA_ERROR;
    else {r->WriterPhase=2;st=STATUS_SUCCESS;}
    KeReleaseSpinLock(&r->Lock,old);return st;
}
NTSTATUS Bc250PermitWriteFinish(BC250_PERMIT *r,BC250_PERMIT_WRITER *t,NTSTATUS final,ULONGLONG revision)
{
    KIRQL old;NTSTATUS st=PermitGuard(r,FALSE);if(st!=STATUS_SUCCESS)return st;
    KeAcquireSpinLock(&r->Lock,&old);
    if(!PermitWriterMatch(r,t))st=STATUS_INVALID_PARAMETER;
    else if(r->WriterPhase!=2 || final==STATUS_PENDING)st=STATUS_DEVICE_BUSY;
    else {
        r->Published=0;(VOID)PermitAdvance(r);st=STATUS_SUCCESS;
        if(final==STATUS_SUCCESS){
            if(!revision || revision<=r->Revision || revision==~(ULONGLONG)0){PermitFault(r);st=STATUS_DATA_ERROR;}
            else if(!r->Closing && !r->Fault){r->Revision=revision;r->Published=1;}
        }else if(((ULONG)final&0xc0000000U)!=0xc0000000U){PermitFault(r);st=STATUS_DATA_ERROR;}
        PermitWriterClear(r,t); /* known final metadata obligation only, NOT OS cleanup */
        if(st==STATUS_SUCCESS && (r->Closing || r->Fault))st=STATUS_DEVICE_NOT_READY;
    }
    KeReleaseSpinLock(&r->Lock,old);return st;
}
NTSTATUS Bc250PermitWriteAbandon(BC250_PERMIT *r,BC250_PERMIT_WRITER *t)
{
    KIRQL old;NTSTATUS st=PermitGuard(r,TRUE);if(st!=STATUS_SUCCESS)return st;
    KeAcquireSpinLock(&r->Lock,&old);
    if(!PermitWriterMatch(r,t))st=STATUS_INVALID_PARAMETER;
    else if(r->WriterPhase!=1)st=STATUS_DEVICE_BUSY;
    else {
        r->Published=0;(VOID)PermitAdvance(r);PermitWriterClear(r,t);
        st=(r->Closing || r->Fault)?STATUS_DEVICE_NOT_READY:STATUS_SUCCESS;
    }
    KeReleaseSpinLock(&r->Lock,old);return st;
}
NTSTATUS Bc250PermitAcquire(BC250_PERMIT *r,BC250_PERMIT_READER *t)
{
    KIRQL old;ULONG n;PVOID thread;NTSTATUS st=PermitGuard(r,FALSE);if(st!=STATUS_SUCCESS)return st;
    if(!t || !PermitZero(t,sizeof(*t)))return STATUS_INVALID_PARAMETER;
    thread=KeGetCurrentThread();KeAcquireSpinLock(&r->Lock,&old);
    if(PermitSlot(r,t))st=STATUS_INVALID_PARAMETER;
    else if(r->Closing || r->Fault || !r->Published || r->WriterAddress)st=STATUS_DEVICE_NOT_READY;
    else if(r->LastId==~(ULONGLONG)0){PermitFault(r);st=STATUS_DATA_ERROR;}
    else {
        for(n=0;n<BC250_PERMIT_SLOTS && r->Readers[n].Address;++n){}
        if(n==BC250_PERMIT_SLOTS)st=STATUS_DEVICE_BUSY;
        else {
            ++r->LastId;t->Root=r;t->Thread=thread;t->ScopeId=r->ScopeId;t->Id=r->LastId;
            t->Revision=r->Revision;t->Generation=r->Generation;t->Epoch=r->Epoch;
            r->Readers[n].Address=t;r->Readers[n].Seal=*t;++r->Pins;st=STATUS_SUCCESS;
        }
    }
    KeReleaseSpinLock(&r->Lock,old);return st;
}
NTSTATUS Bc250PermitStepEnter(BC250_PERMIT *r,const BC250_PERMIT_READER *t,ULONGLONG *revision)
{
    KIRQL old;PVOID thread;BC250_PERMIT_SLOT *slot;NTSTATUS st;
    if(revision)*revision=0;st=PermitGuard(r,TRUE);if(st!=STATUS_SUCCESS)return st;
    if(!revision)return STATUS_INVALID_PARAMETER;thread=KeGetCurrentThread();KeAcquireSpinLock(&r->Lock,&old);
    slot=PermitSlot(r,t);
    if(!PermitReaderMatch(r,t,slot,thread))st=STATUS_INVALID_PARAMETER;
    else if(slot->Step)st=STATUS_DEVICE_BUSY;
    else if(r->Closing || r->Fault || !r->Published || r->WriterAddress ||
        r->Epoch!=slot->Seal.Epoch || r->Revision!=slot->Seal.Revision)st=STATUS_DEVICE_NOT_READY;
    else {slot->Step=1;++r->Steps;*revision=slot->Seal.Revision;st=STATUS_SUCCESS;}
    KeReleaseSpinLock(&r->Lock,old);return st; /* NO lock held across bounded step */
}
NTSTATUS Bc250PermitStepLeave(BC250_PERMIT *r,const BC250_PERMIT_READER *t)
{
    KIRQL old;PVOID thread;BC250_PERMIT_SLOT *slot;NTSTATUS st=PermitGuard(r,TRUE);if(st!=STATUS_SUCCESS)return st;
    thread=KeGetCurrentThread();KeAcquireSpinLock(&r->Lock,&old);slot=PermitSlot(r,t);
    if(!PermitReaderMatch(r,t,slot,thread))st=STATUS_INVALID_PARAMETER;
    else if(!slot->Step)st=STATUS_DEVICE_BUSY;
    else {slot->Step=0;--r->Steps;st=STATUS_SUCCESS;} /* known revoked cleanup allowed */
    KeReleaseSpinLock(&r->Lock,old);return st;
}
NTSTATUS Bc250PermitRelease(BC250_PERMIT *r,BC250_PERMIT_READER *t)
{
    KIRQL old;PVOID thread;BC250_PERMIT_SLOT *slot;NTSTATUS st=PermitGuard(r,TRUE);if(st!=STATUS_SUCCESS)return st;
    thread=KeGetCurrentThread();KeAcquireSpinLock(&r->Lock,&old);slot=PermitSlot(r,t);
    if(!PermitReaderMatch(r,t,slot,thread))st=STATUS_INVALID_PARAMETER;
    else if(slot->Step)st=STATUS_DEVICE_BUSY;
    else {RtlZeroMemory(slot,sizeof(*slot));RtlZeroMemory(t,sizeof(*t));--r->Pins;st=STATUS_SUCCESS;}
    KeReleaseSpinLock(&r->Lock,old);return st;
}
NTSTATUS Bc250PermitClose(BC250_PERMIT *r)
{
    KIRQL old;NTSTATUS st=PermitGuard(r,TRUE);if(st!=STATUS_SUCCESS)return st;
    KeAcquireSpinLock(&r->Lock,&old);
    if(!r->Closing){(VOID)PermitAdvance(r);r->Closing=1;r->Published=0;}
    KeReleaseSpinLock(&r->Lock,old);return STATUS_SUCCESS;
}
NTSTATUS Bc250PermitInspect(BC250_PERMIT *r,BC250_PERMIT_SNAPSHOT *out)
{
    KIRQL old;NTSTATUS st;if(out)RtlZeroMemory(out,sizeof(*out));st=PermitGuard(r,TRUE);if(st!=STATUS_SUCCESS)return st;
    if(!out)return STATUS_INVALID_PARAMETER;KeAcquireSpinLock(&r->Lock,&old);
    out->Revision=r->Revision;out->WriterId=r->WriterAddress?r->WriterSeal.Id:0;
    out->Generation=r->Generation;out->Epoch=r->Epoch;out->Closing=r->Closing;out->Fault=r->Fault;
    out->Published=r->Published;out->Pins=r->Pins;out->Steps=r->Steps;out->WriterPhase=r->WriterPhase;
    KeReleaseSpinLock(&r->Lock,old);return STATUS_SUCCESS; /* historical, not reclaim proof */
}
