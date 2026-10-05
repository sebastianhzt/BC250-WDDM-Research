/* SPDX-License-Identifier: Apache-2.0 */
#include "bc250_query_admission.h"
static BOOLEAN QaEnabled(VOID)
{
#ifdef BC250_QUERY_ADMIT_MOCK
    return Bc250QueryAdmitMockAllowed;
#else
    return FALSE;
#endif
}
static NTSTATUS QaPolicy(BOOLEAN passive)
{
    if(!QaEnabled())return STATUS_NOT_SUPPORTED;
    if(KeGetCurrentIrql()>(passive?PASSIVE_LEVEL:2U))return STATUS_INVALID_DEVICE_STATE;
    if(passive && KeAreApcsDisabled())return STATUS_INVALID_DEVICE_STATE;
    return STATUS_SUCCESS;
}
static NTSTATUS QaGuard(BC250_QUERY_ADMISSION *d,BOOLEAN passive)
{NTSTATUS st=QaPolicy(passive);if(st!=STATUS_SUCCESS)return st;return d && d->Signature==BC250_QA_SIGNATURE?STATUS_SUCCESS:STATUS_INVALID_PARAMETER;}
static BOOLEAN QaZero(const VOID *p,SIZE_T n)
{const UCHAR *b=p;SIZE_T i;for(i=0;i<n;++i)if(b[i])return FALSE;return TRUE;}
static BOOLEAN QaAway(BC250_QUERY_ADMISSION *d,const VOID *p,SIZE_T n)
{
    ULONG_PTR a=(ULONG_PTR)d,b=(ULONG_PTR)p,max=~(ULONG_PTR)0;
    return p && n && a<=max-(sizeof(*d)-1U) && b<=max-(n-1U) &&
        (a+sizeof(*d)-1U<b || b+n-1U<a);
}
/* Exact address + independent immutable seal, short lock held. */
static BOOLEAN QaMatch(BC250_QUERY_ADMISSION *d,const BC250_QUERY_TICKET *t)
{
    const BC250_QUERY_TICKET *s=&d->Seal;
    return d->Address==t && t->Domain==d && t->Domain==s->Domain &&
        t->Scope==s->Scope && t->Generation==s->Generation && t->Epoch==s->Epoch &&
        t->Id==s->Id && t->Id==1U;
}
NTSTATUS Bc250QueryAdmitInit(BC250_QUERY_ADMISSION *d,ULONGLONG scope,ULONGLONG gen,ULONGLONG epoch)
{
    NTSTATUS st=QaPolicy(TRUE);if(st!=STATUS_SUCCESS)return st;
    if(!d || !scope || !gen || !epoch || scope==~(ULONGLONG)0 ||
       gen==~(ULONGLONG)0 || epoch==~(ULONGLONG)0 || !QaZero(d,sizeof(*d)))return STATUS_INVALID_PARAMETER;
    KeInitializeSpinLock(&d->Lock);d->Scope=scope;d->Generation=gen;d->Epoch=epoch;
    d->Signature=BC250_QA_SIGNATURE;return STATUS_SUCCESS;
}
NTSTATUS Bc250QueryAdmitEnter(BC250_QUERY_ADMISSION *d,BC250_QUERY_TICKET *t)
{
    KIRQL old;NTSTATUS st=QaGuard(d,TRUE);if(st!=STATUS_SUCCESS)return st;
    if(!QaAway(d,t,sizeof(*t)))return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&d->Lock,&old);
    if(d->Quarantined)st=STATUS_DATA_ERROR;
    else if(d->Reasons)st=STATUS_DEVICE_NOT_READY;
    else if(d->Address)st=STATUS_DEVICE_BUSY;
    else if(d->Used)st=STATUS_INVALID_DEVICE_STATE;
    else if(!QaZero(t,sizeof(*t)))st=STATUS_INVALID_PARAMETER;
    else{
        t->Domain=d;t->Scope=d->Scope;t->Generation=d->Generation;t->Epoch=d->Epoch;t->Id=1U;
        d->Seal=*t;d->Address=t;d->Used=1;st=STATUS_SUCCESS;
    }
    KeReleaseSpinLock(&d->Lock,old);return st;
}
static NTSTATUS QaToken(BC250_QUERY_ADMISSION *d,const BC250_QUERY_TICKET *t,ULONG operation)
{
    KIRQL old;NTSTATUS st=QaGuard(d,operation==0U);if(st!=STATUS_SUCCESS)return st;
    if(!QaAway(d,t,sizeof(*t)))return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&d->Lock,&old);
    if(!QaMatch(d,t))st=STATUS_INVALID_PARAMETER;
    else if(d->Quarantined)st=STATUS_DATA_ERROR;
    else if(operation==0U)st=d->Reasons?STATUS_DEVICE_NOT_READY:STATUS_SUCCESS;
    else if(operation==1U){
        RtlZeroMemory(d->Address,sizeof(*d->Address));d->Address=NULL;
        RtlZeroMemory(&d->Seal,sizeof(d->Seal));st=STATUS_SUCCESS;
    }else{d->Quarantined=1;st=STATUS_SUCCESS;}
    KeReleaseSpinLock(&d->Lock,old);return st;
}
NTSTATUS Bc250QueryAdmitCheck(BC250_QUERY_ADMISSION *d,const BC250_QUERY_TICKET *t)
{return QaToken(d,t,0U);}
NTSTATUS Bc250QueryAdmitLeave(BC250_QUERY_ADMISSION *d,BC250_QUERY_TICKET *t)
{return QaToken(d,t,1U);}
NTSTATUS Bc250QueryAdmitQuarantine(BC250_QUERY_ADMISSION *d,const BC250_QUERY_TICKET *t)
{return QaToken(d,t,2U);}
NTSTATUS Bc250QueryAdmitClose(BC250_QUERY_ADMISSION *d,ULONG reason)
{
    KIRQL old;NTSTATUS st=QaGuard(d,FALSE);if(st!=STATUS_SUCCESS)return st;
    if(!reason || reason>BC250_QA_REMOVE || (reason&(reason-1U)))return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&d->Lock,&old);d->Reasons|=reason;KeReleaseSpinLock(&d->Lock,old);
    return STATUS_SUCCESS;
}
