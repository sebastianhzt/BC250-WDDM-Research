/* SPDX-License-Identifier: Apache-2.0 */
#include "bc250_dispatch_intake.h"
#ifdef BC250_INTAKE_MOCK
extern BOOLEAN Bc250IntakeMockAllowed;
#endif
static NTSTATUS IntakePolicy(BOOLEAN dispatch)
{
#ifdef BC250_INTAKE_MOCK
    if(!Bc250IntakeMockAllowed)return STATUS_NOT_SUPPORTED;
    return KeGetCurrentIrql()<=(dispatch?2U:PASSIVE_LEVEL)?STATUS_SUCCESS:STATUS_INVALID_DEVICE_STATE;
#else
    (VOID)dispatch;return STATUS_NOT_SUPPORTED;
#endif
}
static NTSTATUS IntakeGuard(BC250_INTAKE *r,BOOLEAN dispatch)
{ NTSTATUS s=IntakePolicy(dispatch);if(s!=STATUS_SUCCESS)return s;
  return r && r->Signature==BC250_INTAKE_SIGNATURE?STATUS_SUCCESS:STATUS_INVALID_PARAMETER; }
static BOOLEAN IntakeZero(const VOID *p,SIZE_T n)
{ const UCHAR *b=p;SIZE_T k;for(k=0;k<n;++k)if(b[k])return FALSE;return TRUE; }
/* All following helpers require root Lock; no platform call under it. */
static VOID IntakeFault(BC250_INTAKE *r) { r->Fault=r->Closing=1; }
static BOOLEAN IntakeAdvance(BC250_INTAKE *r)
{ if(r->Epoch>=BC250_INTAKE_LIMIT-1U){IntakeFault(r);return FALSE;}++r->Epoch;return TRUE; }
static BOOLEAN IntakeMatch(BC250_INTAKE *r,const BC250_INTAKE_TICKET *t)
{
    const BC250_INTAKE_TICKET *s=&r->Seal;
    return t && r->Address==t && t->Root==r && t->Root==s->Root &&
        t->ScopeId==s->ScopeId && t->Id==s->Id && t->Generation==s->Generation &&
        t->Epoch==s->Epoch && t->Kind==s->Kind && t->Target==s->Target;
}
static VOID IntakeClear(BC250_INTAKE *r,BC250_INTAKE_TICKET *t)
{ RtlZeroMemory(t,sizeof(*t));RtlZeroMemory(&r->Seal,sizeof(r->Seal));r->Address=NULL;r->Phase=0; }
NTSTATUS Bc250IntakeInit(BC250_INTAKE *r,ULONGLONG scope,ULONG generation)
{
    NTSTATUS s=IntakePolicy(FALSE);if(s!=STATUS_SUCCESS)return s;
    if(!r || !scope || !generation || generation>=BC250_INTAKE_LIMIT || !IntakeZero(r,sizeof(*r)))
        return STATUS_INVALID_PARAMETER;
    KeInitializeSpinLock(&r->Lock);r->ScopeId=scope;r->Generation=generation;r->Epoch=1;
    r->Signature=BC250_INTAKE_SIGNATURE;return STATUS_SUCCESS;
}
NTSTATUS Bc250IntakeAdmit(BC250_INTAKE *r,ULONG kind,ULONG target,BC250_INTAKE_TICKET *t)
{
    KIRQL old;NTSTATUS s=IntakeGuard(r,TRUE);if(s!=STATUS_SUCCESS)return s;
    if(!t || !IntakeZero(t,sizeof(*t)) || kind<1 || kind>6 ||
        (kind==6?(target<1 || target>4):target!=0))return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&r->Lock,&old);
    if(r->Closing || r->Fault)s=STATUS_DEVICE_NOT_READY;
    else if(r->Address){(VOID)IntakeAdvance(r);IntakeFault(r);s=STATUS_DEVICE_BUSY;}
    else if(r->LastId==~(ULONGLONG)0 || !IntakeAdvance(r)){IntakeFault(r);s=STATUS_DATA_ERROR;}
    else {
        ++r->LastId;t->Root=r;t->ScopeId=r->ScopeId;t->Id=r->LastId;
        t->Generation=r->Generation;t->Epoch=r->Epoch;t->Kind=kind;t->Target=target;
        r->Address=t;r->Seal=*t;r->Phase=BC250_INTAKE_WAITING;s=STATUS_SUCCESS;
    }
    KeReleaseSpinLock(&r->Lock,old);return s;
}
NTSTATUS Bc250IntakeClaim(BC250_INTAKE *r,const BC250_INTAKE_TICKET *t)
{
    KIRQL old;NTSTATUS s=IntakeGuard(r,FALSE);if(s!=STATUS_SUCCESS)return s;
    KeAcquireSpinLock(&r->Lock,&old);
    if(!IntakeMatch(r,t))s=STATUS_INVALID_PARAMETER;
    else if(r->Closing || r->Fault)s=STATUS_DEVICE_NOT_READY;
    else if(r->Phase!=BC250_INTAKE_WAITING)s=STATUS_DEVICE_BUSY;
    else if(!IntakeAdvance(r))s=STATUS_DATA_ERROR;
    else {r->Phase=BC250_INTAKE_CLAIMED;s=STATUS_SUCCESS;}
    KeReleaseSpinLock(&r->Lock,old);return s;
}
NTSTATUS Bc250IntakeAbandon(BC250_INTAKE *r,BC250_INTAKE_TICKET *t)
{
    KIRQL old;NTSTATUS s=IntakeGuard(r,TRUE);if(s!=STATUS_SUCCESS)return s;
    KeAcquireSpinLock(&r->Lock,&old);
    if(!IntakeMatch(r,t))s=STATUS_INVALID_PARAMETER;
    else if(r->Phase!=BC250_INTAKE_WAITING)s=STATUS_DEVICE_BUSY;
    else { (VOID)IntakeAdvance(r);IntakeClear(r,t);s=(r->Closing || r->Fault)?STATUS_DEVICE_NOT_READY:STATUS_SUCCESS; }
    KeReleaseSpinLock(&r->Lock,old);return s;
}
NTSTATUS Bc250IntakeFinish(BC250_INTAKE *r,BC250_INTAKE_TICKET *t,NTSTATUS final)
{
    KIRQL old;NTSTATUS s=IntakeGuard(r,FALSE);if(s!=STATUS_SUCCESS)return s;
    KeAcquireSpinLock(&r->Lock,&old);
    if(!IntakeMatch(r,t))s=STATUS_INVALID_PARAMETER;
    else if(r->Phase!=BC250_INTAKE_CLAIMED || final==STATUS_PENDING)s=STATUS_DEVICE_BUSY;
    else { (VOID)IntakeAdvance(r);r->LastFinal=final;IntakeClear(r,t);
        s=(r->Closing || r->Fault)?STATUS_DEVICE_NOT_READY:STATUS_SUCCESS; }
    KeReleaseSpinLock(&r->Lock,old);return s;
}
NTSTATUS Bc250IntakeClose(BC250_INTAKE *r)
{
    KIRQL old;NTSTATUS s=IntakeGuard(r,TRUE);if(s!=STATUS_SUCCESS)return s;
    KeAcquireSpinLock(&r->Lock,&old);
    if(!r->Closing){(VOID)IntakeAdvance(r);r->Closing=1;}
    KeReleaseSpinLock(&r->Lock,old);return STATUS_SUCCESS;
}
NTSTATUS Bc250IntakeInspect(BC250_INTAKE *r,BC250_INTAKE_SNAPSHOT *out)
{
    KIRQL old;NTSTATUS s;if(out)RtlZeroMemory(out,sizeof(*out));
    s=IntakeGuard(r,TRUE);if(s!=STATUS_SUCCESS)return s;if(!out)return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&r->Lock,&old);out->ScopeId=r->ScopeId;out->Generation=r->Generation;
    out->Epoch=r->Epoch;out->Closing=r->Closing;out->Fault=r->Fault;out->Phase=r->Phase;
    out->PendingId=r->Address?r->Seal.Id:0;out->LastFinal=r->LastFinal;
    out->MetadataIdle=!r->Closing && !r->Fault && !r->Address;
    KeReleaseSpinLock(&r->Lock,old);return STATUS_SUCCESS;
}
NTSTATUS Bc250IntakeCheckIdleAtEpoch(BC250_INTAKE *r,ULONG epoch)
{
    KIRQL old;NTSTATUS s=IntakeGuard(r,FALSE);if(s!=STATUS_SUCCESS)return s;
    KeAcquireSpinLock(&r->Lock,&old);
    s=epoch && epoch==r->Epoch && !r->Closing && !r->Fault && !r->Address?
        STATUS_SUCCESS:STATUS_DEVICE_NOT_READY;
    KeReleaseSpinLock(&r->Lock,old);return s; /* historical only */
}
