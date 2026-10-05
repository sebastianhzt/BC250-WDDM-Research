/* SPDX-License-Identifier: Apache-2.0 */
#include "bc250_context_lifetime.h"
#ifdef BC250_LIFE_MOCK
extern BOOLEAN Bc250LifeMockAllowed;
#endif
static BOOLEAN LifeEnabled(VOID)
{
#ifdef BC250_LIFE_MOCK
    return Bc250LifeMockAllowed;
#else
    return FALSE;
#endif
}
static NTSTATUS LifePolicy(BOOLEAN passive)
{
    if(!LifeEnabled())return STATUS_NOT_SUPPORTED;
    return KeGetCurrentIrql()<=(passive?PASSIVE_LEVEL:2U)?STATUS_SUCCESS:STATUS_INVALID_DEVICE_STATE;
}
static NTSTATUS LifeGuard(BC250_LIFE_DOMAIN *d,BOOLEAN passive)
{
    NTSTATUS st=LifePolicy(passive);if(st!=STATUS_SUCCESS)return st;
    return d && d->Signature==BC250_LIFE_SIGNATURE?STATUS_SUCCESS:STATUS_INVALID_PARAMETER;
}
static BOOLEAN LifeZero(const VOID *p,SIZE_T bytes)
{ SIZE_T n;const UCHAR *b=p;for(n=0;n<bytes;++n)if(b[n])return FALSE;return TRUE; }
/* Lock held below. All bookkeeping resides in stable Parent, never Payload. */
static VOID LifeFault(BC250_LIFE_DOMAIN *d){d->Fault=d->Closing=1;d->Published=0;}
static BC250_LIFE_SLOT *LifeFind(BC250_LIFE_DOMAIN *d,const BC250_LIFE_TOKEN *t)
{
    ULONG n;for(n=0;n<BC250_LIFE_SLOTS;++n)if(t && d->Slots[n].Address==t)return &d->Slots[n];return NULL;
}
static BOOLEAN LifeMatch(BC250_LIFE_DOMAIN *d,const BC250_LIFE_TOKEN *t,BC250_LIFE_SLOT *s,ULONG kind)
{
    const BC250_LIFE_TOKEN *v;if(!t || !s)return FALSE;v=&s->Seal;
    return t->Domain==d && t->Domain==v->Domain && t->Scope==v->Scope && t->Key==v->Key &&
        t->Id==v->Id && t->HoldId==v->HoldId && t->Generation==v->Generation && t->Kind==kind && t->Kind==v->Kind;
}
static BC250_LIFE_SLOT *LifeHoldById(BC250_LIFE_DOMAIN *d,ULONGLONG id)
{
    ULONG n;for(n=0;n<BC250_LIFE_SLOTS;++n)if(d->Slots[n].Address &&
        d->Slots[n].Seal.Kind==BC250_LIFE_HOLD && d->Slots[n].Seal.Id==id)return &d->Slots[n];return NULL;
}
static NTSTATUS LifeRegister(BC250_LIFE_DOMAIN *d,BC250_LIFE_TOKEN *t,ULONG kind,ULONGLONG holdId)
{
    ULONG n;
    if(LifeFind(d,t))return STATUS_INVALID_PARAMETER;
    if(d->LastId>=~(ULONGLONG)0-1U){LifeFault(d);return STATUS_DATA_ERROR;}
    for(n=0;n<BC250_LIFE_SLOTS && d->Slots[n].Address;++n){}
    if(n==BC250_LIFE_SLOTS)return STATUS_DEVICE_BUSY;
    t->Domain=d;t->Scope=d->Scope;t->Key=d->Key;t->Id=++d->LastId;t->HoldId=holdId;
    t->Generation=d->Generation;t->Kind=kind;d->Slots[n].Address=t;d->Slots[n].Seal=*t;
    if(kind==BC250_LIFE_CALL)++d->Calls;else ++d->Holds;
    return STATUS_SUCCESS;
}
NTSTATUS Bc250LifeInit(BC250_LIFE_DOMAIN *d,ULONGLONG scope)
{
    NTSTATUS st=LifePolicy(TRUE);if(st!=STATUS_SUCCESS)return st;
    if(!d || !scope || scope==~(ULONGLONG)0 || !LifeZero(d,sizeof(*d)))return STATUS_INVALID_PARAMETER;
    KeInitializeSpinLock(&d->Lock);d->Scope=scope;d->Signature=BC250_LIFE_SIGNATURE;return STATUS_SUCCESS;
}
NTSTATUS Bc250LifePublish(BC250_LIFE_DOMAIN *d,ULONGLONG key,ULONG generation,PVOID payload)
{
    KIRQL old;NTSTATUS st=LifeGuard(d,TRUE);if(st!=STATUS_SUCCESS)return st;
    if(!payload || !key || key==~(ULONGLONG)0 || !generation || generation>=BC250_LIFE_LIMIT)return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&d->Lock,&old);
    if(d->Fault)st=STATUS_DEVICE_NOT_READY;
    else if(d->Payload || d->Calls || d->Holds)st=STATUS_DEVICE_BUSY;
    else if(key<=d->LastKey || generation<=d->LastGeneration)st=STATUS_INVALID_PARAMETER;
    else{
        d->Key=d->LastKey=key;d->Generation=d->LastGeneration=generation;
        d->Payload=payload;d->Closing=0;d->Published=1;st=STATUS_SUCCESS;
    }
    KeReleaseSpinLock(&d->Lock,old);return st;
}
NTSTATUS Bc250LifeHold(BC250_LIFE_DOMAIN *d,ULONGLONG key,BC250_LIFE_TOKEN *t)
{
    KIRQL old;NTSTATUS st=LifeGuard(d,FALSE);if(st!=STATUS_SUCCESS)return st;
    if(!t || !LifeZero(t,sizeof(*t)))return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&d->Lock,&old);
    if(!d->Published || d->Closing || d->Fault || !d->Payload || key!=d->Key)st=STATUS_DEVICE_NOT_READY;
    else st=LifeRegister(d,t,BC250_LIFE_HOLD,0);
    KeReleaseSpinLock(&d->Lock,old);return st;
}
NTSTATUS Bc250LifeEnter(BC250_LIFE_DOMAIN *d,ULONGLONG key,BC250_LIFE_TOKEN *t,PVOID *payload)
{
    KIRQL old;NTSTATUS st;if(payload)*payload=NULL;st=LifeGuard(d,FALSE);if(st!=STATUS_SUCCESS)return st;
    if(!payload || !t || !LifeZero(t,sizeof(*t)))return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&d->Lock,&old);
    if(!d->Published || d->Closing || d->Fault || !d->Payload || key!=d->Key)st=STATUS_DEVICE_NOT_READY;
    else{st=LifeRegister(d,t,BC250_LIFE_CALL,0);if(st==STATUS_SUCCESS)*payload=d->Payload;}
    KeReleaseSpinLock(&d->Lock,old);return st;
}
NTSTATUS Bc250LifeEnterHeld(BC250_LIFE_DOMAIN *d,const BC250_LIFE_TOKEN *hold,BC250_LIFE_TOKEN *t,PVOID *payload)
{
    KIRQL old;BC250_LIFE_SLOT *s;NTSTATUS st;if(payload)*payload=NULL;st=LifeGuard(d,FALSE);if(st!=STATUS_SUCCESS)return st;
    if(!payload || !t || !LifeZero(t,sizeof(*t)))return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&d->Lock,&old);s=LifeFind(d,hold);
    if(!LifeMatch(d,hold,s,BC250_LIFE_HOLD))st=STATUS_INVALID_PARAMETER;
    else if(d->Fault || !d->Payload)st=STATUS_DEVICE_NOT_READY;
    else{
        st=LifeRegister(d,t,BC250_LIFE_CALL,s->Seal.Id);
        if(st==STATUS_SUCCESS){++s->Users;*payload=d->Payload;}
    }
    KeReleaseSpinLock(&d->Lock,old);return st;
}
NTSTATUS Bc250LifeExit(BC250_LIFE_DOMAIN *d,BC250_LIFE_TOKEN *t)
{
    KIRQL old;BC250_LIFE_SLOT *s,*h=NULL;NTSTATUS st=LifeGuard(d,FALSE);if(st!=STATUS_SUCCESS)return st;
    KeAcquireSpinLock(&d->Lock,&old);s=LifeFind(d,t);
    if(!LifeMatch(d,t,s,BC250_LIFE_CALL))st=STATUS_INVALID_PARAMETER;
    else{
        if(s->Seal.HoldId)h=LifeHoldById(d,s->Seal.HoldId);
        if(!d->Calls || (s->Seal.HoldId && (!h || !h->Users))){LifeFault(d);st=STATUS_DATA_ERROR;}
        else{
            if(h)--h->Users;
            RtlZeroMemory(t,sizeof(*t));RtlZeroMemory(s,sizeof(*s));--d->Calls;st=STATUS_SUCCESS;
        }
    }
    KeReleaseSpinLock(&d->Lock,old);return st; /* only PARENT access after final drop */
}
NTSTATUS Bc250LifeReleaseHold(BC250_LIFE_DOMAIN *d,BC250_LIFE_TOKEN *t)
{
    KIRQL old;BC250_LIFE_SLOT *s;NTSTATUS st=LifeGuard(d,FALSE);if(st!=STATUS_SUCCESS)return st;
    KeAcquireSpinLock(&d->Lock,&old);s=LifeFind(d,t);
    if(!LifeMatch(d,t,s,BC250_LIFE_HOLD))st=STATUS_INVALID_PARAMETER;
    else if(s->Users)st=STATUS_DEVICE_BUSY;
    else if(!d->Holds){LifeFault(d);st=STATUS_DATA_ERROR;}
    else{RtlZeroMemory(t,sizeof(*t));RtlZeroMemory(s,sizeof(*s));--d->Holds;st=STATUS_SUCCESS;}
    KeReleaseSpinLock(&d->Lock,old);return st;
}
NTSTATUS Bc250LifeClose(BC250_LIFE_DOMAIN *d)
{
    KIRQL old;NTSTATUS st=LifeGuard(d,FALSE);if(st!=STATUS_SUCCESS)return st;
    KeAcquireSpinLock(&d->Lock,&old);d->Published=0;d->Closing=1;KeReleaseSpinLock(&d->Lock,old);return STATUS_SUCCESS;
}
NTSTATUS Bc250LifeDetach(BC250_LIFE_DOMAIN *d,PVOID *payload)
{
    KIRQL old;ULONG n;NTSTATUS st;if(payload)*payload=NULL;st=LifeGuard(d,TRUE);if(st!=STATUS_SUCCESS)return st;
    if(!payload)return STATUS_INVALID_PARAMETER;KeAcquireSpinLock(&d->Lock,&old);
    if(d->Fault || !d->Closing || d->Published || !d->Payload)st=STATUS_DEVICE_NOT_READY;
    else if(d->Calls || d->Holds)st=STATUS_DEVICE_BUSY;
    else{
        for(n=0;n<BC250_LIFE_SLOTS;++n)if(d->Slots[n].Address)break;
        if(n!=BC250_LIFE_SLOTS){LifeFault(d);st=STATUS_DATA_ERROR;}
        else{*payload=d->Payload;d->Payload=NULL;d->Key=0;d->Generation=0;st=STATUS_SUCCESS;}
    }
    KeReleaseSpinLock(&d->Lock,old);return st;
}
NTSTATUS Bc250LifeInspect(BC250_LIFE_DOMAIN *d,BC250_LIFE_STATUS *out)
{
    KIRQL old;NTSTATUS st;if(out)RtlZeroMemory(out,sizeof(*out));st=LifeGuard(d,FALSE);if(st!=STATUS_SUCCESS)return st;
    if(!out)return STATUS_INVALID_PARAMETER;KeAcquireSpinLock(&d->Lock,&old);
    out->Scope=d->Scope;out->Key=d->Key;out->Generation=d->Generation;out->Published=d->Published;
    out->Closing=d->Closing;out->Fault=d->Fault;out->Calls=d->Calls;out->Holds=d->Holds;
    KeReleaseSpinLock(&d->Lock,old);return STATUS_SUCCESS;
}
