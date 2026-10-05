/* SPDX-License-Identifier: Apache-2.0 */
#include "bc250_envelope_reference.h"
#ifdef BC250_ENV_MOCK
extern BOOLEAN Bc250EnvMockAllowed;
#endif
static NTSTATUS EnvPolicy(BOOLEAN passive)
{
#ifdef BC250_ENV_MOCK
    if(!Bc250EnvMockAllowed)return STATUS_NOT_SUPPORTED;
    return KeGetCurrentIrql()<=(passive?PASSIVE_LEVEL:2U)?STATUS_SUCCESS:STATUS_INVALID_DEVICE_STATE;
#else
    (VOID)passive;return STATUS_NOT_SUPPORTED;
#endif
}
static BOOLEAN EnvZero(const VOID *p,SIZE_T n)
{SIZE_T k;const UCHAR *b=p;for(k=0;k<n;++k)if(b[k])return FALSE;return TRUE;}
static NTSTATUS EnvGuard(BC250_ENV_DOMAIN *d)
{
    NTSTATUS st=EnvPolicy(FALSE);if(st!=STATUS_SUCCESS)return st;
    return d && d->Signature==BC250_ENV_SIGNATURE?STATUS_SUCCESS:STATUS_INVALID_PARAMETER;
}
static BC250_ENV_SLOT *EnvFind(BC250_ENV_DOMAIN *d,BC250_ENV_RECEIPT *e)
{ULONG n;for(n=0;n<BC250_ENV_SLOTS;++n)if(d->Slots[n].Address==e)return &d->Slots[n];return NULL;}
static VOID EnvErase(BC250_ENV_DOMAIN *d,BC250_ENV_SLOT *slot,BC250_ENV_RECEIPT *e)
{
    KIRQL old;KeAcquireSpinLock(&d->Lock,&old);
    RtlZeroMemory(e,sizeof(*e));RtlZeroMemory(slot,sizeof(*slot));
    KeReleaseSpinLock(&d->Lock,old);
}
NTSTATUS Bc250EnvInit(BC250_ENV_DOMAIN *d,ULONGLONG scope,PDEVICE_OBJECT self,
    PIO_REMOVE_LOCK removeLock,BC250_LIFE_DOMAIN *life,ULONGLONG key)
{
    NTSTATUS st=EnvPolicy(TRUE);if(st!=STATUS_SUCCESS)return st;
    if(!d || !scope || scope==~(ULONGLONG)0 || !self || !removeLock || !life || !key ||
       key==~(ULONGLONG)0 || !EnvZero(d,sizeof(*d)))return STATUS_INVALID_PARAMETER;
    KeInitializeSpinLock(&d->Lock);d->Scope=scope;d->Self=self;d->RemoveLock=removeLock;
    d->Life=life;d->Key=key;d->Signature=BC250_ENV_SIGNATURE;return STATUS_SUCCESS;
}
NTSTATUS Bc250EnvAcquire(BC250_ENV_DOMAIN *d,BC250_ENV_RECEIPT *e)
{
    KIRQL old;ULONG n;BC250_ENV_SLOT *slot=NULL;PDEVICE_OBJECT self=NULL;
    PIO_REMOVE_LOCK lock=NULL;BC250_LIFE_DOMAIN *life=NULL;ULONGLONG key=0;
    NTSTATUS st=EnvGuard(d);if(st!=STATUS_SUCCESS)return st;
    if(!e || !EnvZero(e,sizeof(*e)))return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&d->Lock,&old);
    if(d->Closing || d->Fault)st=STATUS_DEVICE_NOT_READY;
    else if(EnvFind(d,e))st=STATUS_INVALID_PARAMETER;
    else if(d->LastId>=~(ULONGLONG)0-1U){d->Closing=d->Fault=1;st=STATUS_DATA_ERROR;}
    else{
        for(n=0;n<BC250_ENV_SLOTS && d->Slots[n].Address;++n){}
        if(n==BC250_ENV_SLOTS)st=STATUS_DEVICE_BUSY;
        else{
            slot=&d->Slots[n];e->Root=d;e->Self=d->Self;e->Scope=d->Scope;e->Id=++d->LastId;
            slot->Address=e;slot->Phase=BC250_ENV_PREPARING;slot->Seal=*e;
            self=d->Self;lock=d->RemoveLock;life=d->Life;key=d->Key;st=STATUS_SUCCESS;
        }
    }
    KeReleaseSpinLock(&d->Lock,old);if(st!=STATUS_SUCCESS)return st;
    /* All pointers above had independent bootstrap validity BEFORE ref call. */
    ObReferenceObject(self);
    st=IoAcquireRemoveLock(lock,e);
    if(st!=STATUS_SUCCESS){EnvErase(d,slot,e);ObDereferenceObject(self);return st;}
    st=Bc250LifeHold(life,key,&e->Hold);
    if(st!=STATUS_SUCCESS){
        IoReleaseRemoveLock(lock,e);EnvErase(d,slot,e);ObDereferenceObject(self);return st;
    }
    KeAcquireSpinLock(&d->Lock,&old);slot->Seal=*e;slot->Phase=BC250_ENV_ACTIVE;
    KeReleaseSpinLock(&d->Lock,old);return STATUS_SUCCESS;
}
NTSTATUS Bc250EnvRelease(BC250_ENV_DOMAIN *d,BC250_ENV_RECEIPT *e)
{
    KIRQL old;BC250_ENV_SLOT *slot;PDEVICE_OBJECT self=NULL;PIO_REMOVE_LOCK lock=NULL;
    BC250_LIFE_DOMAIN *life=NULL;NTSTATUS st=EnvGuard(d);if(st!=STATUS_SUCCESS)return st;
    if(!e)return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&d->Lock,&old);slot=EnvFind(d,e);
    if(!slot)st=STATUS_INVALID_PARAMETER;
    else if(slot->Phase!=BC250_ENV_ACTIVE)st=STATUS_DEVICE_BUSY;
    else if(RtlCompareMemory(e,&slot->Seal,sizeof(*e))!=sizeof(*e))st=STATUS_INVALID_PARAMETER;
    else{slot->Phase=BC250_ENV_RELEASING;self=slot->Seal.Self;lock=d->RemoveLock;life=d->Life;st=STATUS_SUCCESS;}
    KeReleaseSpinLock(&d->Lock,old);if(st!=STATUS_SUCCESS)return st;
    st=Bc250LifeReleaseHold(life,&e->Hold);
    if(st!=STATUS_SUCCESS){
        KeAcquireSpinLock(&d->Lock,&old);slot->Phase=BC250_ENV_ACTIVE;
        if(st!=STATUS_DEVICE_BUSY)d->Closing=d->Fault=1;
        KeReleaseSpinLock(&d->Lock,old);return st; /* retains ALL known ref/tag obligations */
    }
    /* No child access below. Own FDO ref keeps Parent storage after tag drops. */
    IoReleaseRemoveLock(lock,e);EnvErase(d,slot,e);
    ObDereferenceObject(self);return STATUS_SUCCESS; /* stack-only after final drop */
}
NTSTATUS Bc250EnvClose(BC250_ENV_DOMAIN *d)
{
    KIRQL old;NTSTATUS st=EnvGuard(d);if(st!=STATUS_SUCCESS)return st;
    KeAcquireSpinLock(&d->Lock,&old);d->Closing=1;KeReleaseSpinLock(&d->Lock,old);
    return STATUS_SUCCESS;
}
