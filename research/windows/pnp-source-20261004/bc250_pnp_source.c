/* SPDX-License-Identifier: Apache-2.0 */
#include "bc250_pnp_source.h"
#ifdef BC250_PNP_SOURCE_MOCK
extern BOOLEAN Bc250SourceMockAllowed;
#endif
static BOOLEAN SourceEnabled(VOID)
{
#ifdef BC250_PNP_SOURCE_MOCK
    return Bc250SourceMockAllowed;
#else
    return FALSE;
#endif
}
static NTSTATUS SourcePolicy(BOOLEAN dispatch)
{
    if(!SourceEnabled())return STATUS_NOT_SUPPORTED;
    return KeGetCurrentIrql()<=(dispatch?2U:PASSIVE_LEVEL)?STATUS_SUCCESS:STATUS_INVALID_DEVICE_STATE;
}
static NTSTATUS SourceGuard(BC250_SOURCE *s,BOOLEAN dispatch)
{
    NTSTATUS st=SourcePolicy(dispatch);if(st!=STATUS_SUCCESS)return st;
    return s && s->Signature==BC250_SOURCE_SIGNATURE?STATUS_SUCCESS:STATUS_INVALID_PARAMETER;
}
static ULONG SourceRead32(const UCHAR *p)
{ return (ULONG)p[0]|((ULONG)p[1]<<8)|((ULONG)p[2]<<16)|((ULONG)p[3]<<24); }
static VOID SourceWrite32(UCHAR *p,ULONG v)
{ ULONG n;for(n=0;n<4;++n)p[n]=(UCHAR)(v>>(n*8)); }
static VOID SourceWrite64(UCHAR *p,ULONGLONG v)
{ SourceWrite32(p,(ULONG)v);SourceWrite32(p+4,(ULONG)(v>>32)); }
static VOID SourceState(BC250_SOURCE *s,ULONG state)
{ SourceWrite32(s->Current.Wire+BC250_CW_STATE,state); }
static ULONG SourceGetState(const BC250_SOURCE *s)
{ return SourceRead32(s->Current.Wire+BC250_CW_STATE); }
static VOID SourceNoResources(BC250_SOURCE *s)
{
    UCHAR *w=s->Current.Wire;
    RtlZeroMemory(w+BC250_CW_MEMORY,BC250_CAPTURE_WIRE_BYTES-BC250_CW_MEMORY);
    SourceWrite32(w+BC250_CW_DESCRIPTORS,0);SourceWrite32(w+BC250_CW_COUNT,0);
    SourceWrite32(w+BC250_CW_SNAPSHOT,0);SourceWrite32(w+BC250_CW_BLOCKERS,0x1d);
}
static VOID SourceFault(BC250_SOURCE *s)
{ s->Fault=s->Closing=1;SourceState(s,6);s->Current.PowerState=0;SourceNoResources(s); }
static BOOLEAN SourceAdvance(BC250_SOURCE *s)
{
    if(s->Current.SourceEpoch>=BC250_CAPTURE_COUNTER_LIMIT-1U) { SourceFault(s);return FALSE; }
    ++s->Current.SourceEpoch;return TRUE;
}
static BOOLEAN SourceReady(const BC250_SOURCE *s)
{
    return !s->Closing && !s->Fault && !s->Retirement && !s->PendingAddress &&
        SourceGetState(s)==1 && s->Current.PowerState==1 && s->Current.InterlocksOff==1 &&
        SourceRead32(s->Current.Wire+BC250_CW_SNAPSHOT)==1 &&
        SourceRead32(s->Current.Wire+BC250_CW_COUNT)!=0;
}
static BOOLEAN SourceZero(const VOID *p,SIZE_T n)
{ const UCHAR *b=p;SIZE_T k;for(k=0;k<n;++k)if(b[k])return FALSE;return TRUE; }
static BOOLEAN SourceResourcesValid(const BC250_SOURCE_RESOURCES *r)
{
    ULONG n,k;
    if(!r || !r->DescriptorCount || r->DescriptorCount>32 || !r->MemoryCount ||
        r->MemoryCount>8 || r->MemoryCount>r->DescriptorCount)return FALSE;
    for(n=0;n<r->MemoryCount;++n) {
        const BC250_SOURCE_MEMORY *m=&r->Memory[n];
        if(m->Reserved || m->RawFlags>0xffff || m->TranslatedFlags>0xffff ||
            m->Ordinal>=r->DescriptorCount || (n && m->Ordinal<=r->Memory[n-1].Ordinal) ||
            !m->Raw || !m->Length || m->Length>0x7fffffffffffffffULL ||
            m->Raw>0x7fffffffffffffffULL-m->Length ||
            m->Translated>0x7fffffffffffffffULL-m->Length)return FALSE;
        for(k=0;k<n;++k) {
            const BC250_SOURCE_MEMORY *a=&r->Memory[k];
            if((m->Raw<a->Raw+a->Length && a->Raw<m->Raw+m->Length) ||
                (m->Translated<a->Translated+a->Length && a->Translated<m->Translated+m->Length))return FALSE;
        }
    }
    return SourceZero(&r->Memory[r->MemoryCount],(8-r->MemoryCount)*sizeof(r->Memory[0]));
}
static VOID SourceResourcesCopy(BC250_SOURCE *s,const BC250_SOURCE_RESOURCES *r)
{
    ULONG n;UCHAR *w=s->Current.Wire;
    SourceNoResources(s);SourceWrite32(w+BC250_CW_SNAPSHOT,1);SourceWrite32(w+BC250_CW_BLOCKERS,0x1c);
    SourceWrite32(w+BC250_CW_DESCRIPTORS,r->DescriptorCount);SourceWrite32(w+BC250_CW_COUNT,r->MemoryCount);
    for(n=0;n<r->MemoryCount;++n) {
        const BC250_SOURCE_MEMORY *m=&r->Memory[n];UCHAR *v=w+BC250_CW_MEMORY+n*BC250_CW_ENTRY_BYTES;
        SourceWrite64(v+BC250_CW_RAW,m->Raw);SourceWrite64(v+BC250_CW_TRANSLATED,m->Translated);
        SourceWrite64(v+BC250_CW_LENGTH,m->Length);SourceWrite32(v+BC250_CW_RAW_FLAGS,m->RawFlags);
        SourceWrite32(v+BC250_CW_TRANSLATED_FLAGS,m->TranslatedFlags);SourceWrite32(v+BC250_CW_ORDINAL,m->Ordinal);
    }
}
NTSTATUS Bc250SourceInit(BC250_SOURCE *s,ULONGLONG scope,ULONG generation,PDEVICE_OBJECT pdo,PDEVICE_OBJECT lower)
{
    NTSTATUS st=SourcePolicy(FALSE);UCHAR *w;
    if(st!=STATUS_SUCCESS)return st;
    if(!s || !scope || scope==~(ULONGLONG)0 || !generation || generation>=BC250_CAPTURE_COUNTER_LIMIT ||
        !pdo || !lower || !SourceZero(s,sizeof(*s)))return STATUS_INVALID_PARAMETER;
    /* All validation precedes VOID object refs; no fallible operation follows. */
    s->ScopeId=scope;s->Current.Version=1;s->Current.StructSize=sizeof(s->Current);
    s->Current.SourceGeneration=generation; /* interlocks UNKNOWN/unsafe until observation */
    w=s->Current.Wire;SourceWrite32(w+BC250_CW_VERSION,2);SourceWrite32(w+BC250_CW_SIZE,360);
    SourceWrite32(w+BC250_CW_BUILD,22);SourceWrite32(w+BC250_CW_STATUS,(ULONG)STATUS_DEVICE_NOT_READY);
    SourceWrite32(w+BC250_CW_GENERATION,generation);SourceNoResources(s);
    KeInitializeSpinLock(&s->Lock);ExInitializeRundownProtection(&s->Rundown);
    ObReferenceObject(pdo);ObReferenceObject(lower);s->Pdo=pdo;s->Lower=lower;
    s->Signature=BC250_SOURCE_SIGNATURE;return STATUS_SUCCESS;
}
NTSTATUS Bc250SourceBegin(BC250_SOURCE *s,ULONG kind,ULONG target,BC250_SOURCE_REQUEST *ticket)
{
    KIRQL old;ULONG state;NTSTATUS st=SourceGuard(s,FALSE);
    if(st!=STATUS_SUCCESS)return st;
    if(!ticket || !SourceZero(ticket,sizeof(*ticket)))return STATUS_INVALID_PARAMETER;
    if(!ExAcquireRundownProtection(&s->Rundown))return STATUS_DEVICE_NOT_READY;
    KeAcquireSpinLock(&s->Lock,&old);state=SourceGetState(s);
    if(s->Closing || s->Fault || s->Retirement)st=STATUS_DEVICE_NOT_READY;
    else if(s->PendingAddress) { SourceFault(s);st=STATUS_DEVICE_BUSY; }
    else if(kind<1 || kind>6 || (kind==6?(target<1 || target>4):target!=0) ||
        (kind==1 && state!=0 && state!=3) ||
        ((kind==2 || kind==3 || kind==6) && state!=1) ||
        (kind==4 && (state!=2 || s->QueryKind!=2)) ||
        (kind==5 && (state!=4 || s->QueryKind!=3))) { SourceFault(s);st=STATUS_INVALID_DEVICE_STATE; }
    else if(s->LastId>=~(ULONGLONG)0-1U || !SourceAdvance(s)) { SourceFault(s);st=STATUS_INVALID_DEVICE_STATE; }
    else {
        s->PreviousState=state;
        if(kind==1) { SourceState(s,0);SourceNoResources(s);s->Current.PowerState=0;s->QueryKind=0; }
        if(kind==2)SourceState(s,2);
        if(kind==3)SourceState(s,4);
        if(kind==6)s->Current.PowerState=0;
        s->Pending.Source=s;s->Pending.ScopeId=s->ScopeId;s->Pending.Id=++s->LastId;
        s->Pending.Generation=s->Current.SourceGeneration;s->Pending.Epoch=s->Current.SourceEpoch;
        s->Pending.Kind=kind;s->Pending.TargetPower=target;s->PendingAddress=ticket;*ticket=s->Pending;
    }
    KeReleaseSpinLock(&s->Lock,old);
    if(st!=STATUS_SUCCESS)ExReleaseRundownProtection(&s->Rundown);
    return st;
}
NTSTATUS Bc250SourceComplete(BC250_SOURCE *s,BC250_SOURCE_REQUEST *ticket,NTSTATUS finalStatus,
    const BC250_SOURCE_RESOURCES *resources)
{
    KIRQL old;ULONG kind;BOOLEAN release=FALSE;NTSTATUS st=SourceGuard(s,FALSE);
    if(st!=STATUS_SUCCESS)return st;
    if(!ticket)return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&s->Lock,&old);
    if(s->PendingAddress!=ticket || RtlCompareMemory(ticket,&s->Pending,sizeof(*ticket))!=sizeof(*ticket))
        st=STATUS_INVALID_PARAMETER; /* Cannot infer or erase another cleanup obligation. */
    else if(finalStatus==STATUS_PENDING)st=STATUS_DEVICE_BUSY; /* Not a terminal completion. */
    else {
        kind=s->Pending.Kind;
        if(s->Closing || s->Fault || s->Retirement || s->Current.SourceEpoch!=s->Pending.Epoch)
            st=STATUS_DEVICE_NOT_READY; /* Stale completion cleans known ref, never resurrects. */
        else if(!SourceAdvance(s))st=STATUS_INVALID_DEVICE_STATE;
        else if(finalStatus>STATUS_SUCCESS || ((ULONG)finalStatus&0xc0000000U)==0x80000000U)
            { SourceFault(s);st=STATUS_DATA_ERROR; }
        else if(kind==1) {
            if(finalStatus==STATUS_SUCCESS && SourceResourcesValid(resources)) {
                SourceResourcesCopy(s,resources);SourceState(s,1);
            } else { SourceState(s,0);SourceNoResources(s);if(finalStatus==STATUS_SUCCESS) { SourceFault(s);st=STATUS_DATA_ERROR; } }
        } else if(kind==2 || kind==3) {
            if(finalStatus==STATUS_SUCCESS)s->QueryKind=kind;
            else { SourceState(s,s->PreviousState);s->QueryKind=0; }
        } else if(kind==4 || kind==5) {
            if(finalStatus==STATUS_SUCCESS) { SourceState(s,1);s->QueryKind=0; }
            /* Failed cancel stays blocked; this model does not complete an IRP. */
        } else if(kind==6) {
            if(finalStatus==STATUS_SUCCESS)s->Current.PowerState=s->Pending.TargetPower;
            /* Failed power retains UNKNOWN, not an assumed rollback D0. */
        }
        RtlZeroMemory(ticket,sizeof(*ticket));s->PendingAddress=NULL;
        RtlZeroMemory(&s->Pending,sizeof(s->Pending));release=TRUE;
    }
    KeReleaseSpinLock(&s->Lock,old);
    if(release)ExReleaseRundownProtection(&s->Rundown);
    return st;
}
NTSTATUS Bc250SourceTransition(BC250_SOURCE *s,ULONG kind)
{
    KIRQL old;NTSTATUS st=SourceGuard(s,TRUE);if(st!=STATUS_SUCCESS)return st;
    KeAcquireSpinLock(&s->Lock,&old);
    if(s->Closing || s->Fault || s->Retirement)st=STATUS_DEVICE_NOT_READY;
    else if(kind<7 || kind>9 || (kind==7 && SourceGetState(s)!=1 && SourceGetState(s)!=2))
        { SourceFault(s);st=STATUS_INVALID_DEVICE_STATE; }
    else if(!SourceAdvance(s))st=STATUS_INVALID_DEVICE_STATE;
    else {
        SourceState(s,kind==7?3U:kind==8?5U:6U);SourceNoResources(s);s->Current.PowerState=0;s->QueryKind=0;
        if(kind!=7)s->Closing=1;
    }
    KeReleaseSpinLock(&s->Lock,old);return st;
}
NTSTATUS Bc250SourceInterlocks(BC250_SOURCE *s,ULONG off)
{
    KIRQL old;NTSTATUS st=SourceGuard(s,FALSE);if(st!=STATUS_SUCCESS)return st;
    KeAcquireSpinLock(&s->Lock,&old);
    if(s->Closing || s->Fault || s->Retirement)st=STATUS_DEVICE_NOT_READY;
    else if(off>1) { SourceFault(s);st=STATUS_INVALID_PARAMETER; }
    else if(!SourceAdvance(s))st=STATUS_INVALID_DEVICE_STATE;
    else s->Current.InterlocksOff=off;
    KeReleaseSpinLock(&s->Lock,old);return st;
}
static BC250_SOURCE_SLOT *SourceSlot(BC250_SOURCE *s,const BC250_SOURCE_LEASE *lease)
{
    ULONG n;for(n=0;n<BC250_SOURCE_SLOTS;++n)if(s->Slots[n].Address==lease)return &s->Slots[n];
    return NULL;
}
static BOOLEAN SourceLeaseMatches(BC250_SOURCE *s,const BC250_SOURCE_LEASE *l,BC250_SOURCE_SLOT *slot)
{
    return slot && slot->Thread==KeGetCurrentThread() && l->Thread==slot->Thread &&
        l->Source==s && l->ScopeId==s->ScopeId && l->Id==slot->Id;
}
NTSTATUS Bc250SourceAcquire(BC250_SOURCE *s,BC250_SOURCE_LEASE *lease)
{
    KIRQL old;ULONG n;NTSTATUS st=SourceGuard(s,FALSE);if(st!=STATUS_SUCCESS)return st;
    if(!lease || !SourceZero(lease,sizeof(*lease)))return STATUS_INVALID_PARAMETER;
    if(!ExAcquireRundownProtection(&s->Rundown))return STATUS_DEVICE_NOT_READY;
    KeAcquireSpinLock(&s->Lock,&old);
    if(SourceSlot(s,lease))st=STATUS_INVALID_PARAMETER;
    else if(!SourceReady(s))st=STATUS_DEVICE_NOT_READY;
    else if(s->LastId>=~(ULONGLONG)0-1U) { SourceFault(s);st=STATUS_INVALID_DEVICE_STATE; }
    else {
        for(n=0;n<BC250_SOURCE_SLOTS && s->Slots[n].Address;++n){}
        if(n==BC250_SOURCE_SLOTS)st=STATUS_INSUFFICIENT_RESOURCES;
        else {
            lease->Source=s;lease->Thread=KeGetCurrentThread();lease->ScopeId=s->ScopeId;
            lease->Id=++s->LastId;lease->Input=s->Current;
            s->Slots[n].Address=lease;s->Slots[n].Thread=lease->Thread;s->Slots[n].Id=lease->Id;
            s->Slots[n].Generation=s->Current.SourceGeneration;s->Slots[n].Epoch=s->Current.SourceEpoch;
        }
    }
    KeReleaseSpinLock(&s->Lock,old);
    if(st!=STATUS_SUCCESS)ExReleaseRundownProtection(&s->Rundown);return st;
}
NTSTATUS Bc250SourceValidate(BC250_SOURCE *s,const BC250_SOURCE_LEASE *lease,BC250_CAPTURE_INPUT *out)
{
    KIRQL old;BC250_SOURCE_SLOT *slot;NTSTATUS st; if(out)RtlZeroMemory(out,sizeof(*out));
    st=SourceGuard(s,FALSE);if(st!=STATUS_SUCCESS)return st;
    if(!lease || !out)return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&s->Lock,&old);
    slot=SourceSlot(s,lease);
    if(!SourceLeaseMatches(s,lease,slot) || !SourceReady(s) ||
        lease->Input.SourceGeneration!=slot->Generation || lease->Input.SourceEpoch!=slot->Epoch ||
        s->Current.SourceGeneration!=slot->Generation || s->Current.SourceEpoch!=slot->Epoch ||
        RtlCompareMemory(&lease->Input,&s->Current,sizeof(s->Current))!=sizeof(s->Current))st=STATUS_DEVICE_NOT_READY;
    else *out=s->Current; /* trusted source copy, not arbitrary lease payload */
    KeReleaseSpinLock(&s->Lock,old);return st;
}
NTSTATUS Bc250SourceRelease(BC250_SOURCE *s,BC250_SOURCE_LEASE *lease)
{
    KIRQL old;BC250_SOURCE_SLOT *slot;NTSTATUS st=SourceGuard(s,FALSE);
    if(st!=STATUS_SUCCESS)return st;if(!lease)return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&s->Lock,&old);slot=SourceSlot(s,lease);
    if(!SourceLeaseMatches(s,lease,slot))st=STATUS_INVALID_PARAMETER;
    else { RtlZeroMemory(slot,sizeof(*slot));RtlZeroMemory(lease,sizeof(*lease)); }
    KeReleaseSpinLock(&s->Lock,old);
    if(st==STATUS_SUCCESS)ExReleaseRundownProtection(&s->Rundown);return st;
}
NTSTATUS Bc250SourceClose(BC250_SOURCE *s)
{
    KIRQL old;NTSTATUS st=SourceGuard(s,TRUE);if(st!=STATUS_SUCCESS)return st;
    KeAcquireSpinLock(&s->Lock,&old);
    if(s->Retirement)st=STATUS_INVALID_DEVICE_STATE;
    else if(!s->Closing) {
        s->Closing=1;if(!SourceAdvance(s))st=STATUS_INVALID_DEVICE_STATE;
        SourceState(s,6);s->Current.PowerState=0;SourceNoResources(s);
    }
    KeReleaseSpinLock(&s->Lock,old);return st;
}
NTSTATUS Bc250SourceDrain(BC250_SOURCE *s)
{
    KIRQL old;ULONG n;PDEVICE_OBJECT pdo=NULL,lower=NULL;
    NTSTATUS st=SourceGuard(s,FALSE);if(st!=STATUS_SUCCESS)return st;
    KeAcquireSpinLock(&s->Lock,&old);
    if(s->Fault)st=STATUS_DATA_ERROR;
    else if(!s->Closing || s->Retirement)st=STATUS_INVALID_DEVICE_STATE;
    else if(s->PendingAddress)st=STATUS_DEVICE_BUSY;
    else {
        for(n=0;n<BC250_SOURCE_SLOTS;++n)if(s->Slots[n].Address) { st=STATUS_DEVICE_BUSY;break; }
        if(st==STATUS_SUCCESS)s->Retirement=1;
    }
    KeReleaseSpinLock(&s->Lock,old);if(st!=STATUS_SUCCESS)return st;
    ExWaitForRundownProtectionRelease(&s->Rundown); /* no source lock held */
    KeAcquireSpinLock(&s->Lock,&old);
    if(s->PendingAddress || s->Fault) { SourceFault(s);st=STATUS_DATA_ERROR; }
    else {
        for(n=0;n<BC250_SOURCE_SLOTS;++n)if(s->Slots[n].Address) { SourceFault(s);st=STATUS_DATA_ERROR;break; }
        if(st==STATUS_SUCCESS) { pdo=s->Pdo;lower=s->Lower;s->Pdo=s->Lower=NULL; }
    }
    KeReleaseSpinLock(&s->Lock,old);
    if(st==STATUS_SUCCESS) {
        ObDereferenceObject(lower);ObDereferenceObject(pdo);
        KeAcquireSpinLock(&s->Lock,&old);s->Retirement=2;KeReleaseSpinLock(&s->Lock,old);
    }
    return st; /* reclaim only after external ALL-calls anchor also drains */
}
