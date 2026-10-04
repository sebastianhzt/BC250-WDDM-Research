/* SPDX-License-Identifier: Apache-2.0 */
#include "bc250_pci_contract.h"
static BOOLEAN Enabled(VOID)
{
#ifdef BC250_PCI_CONTRACT_MOCK
    return TRUE;
#else
    return FALSE; /* No registry/environment/IOCTL activation switch. */
#endif
}
static BOOLEAN ZeroBytes(const VOID *Buffer,ULONG Length)
{
    const UCHAR *bytes=(const UCHAR *)Buffer;
    ULONG index;
    for(index=0;index<Length;++index) if(bytes[index]!=0) return FALSE;
    return TRUE;
}
static ULONG Read32(const UCHAR *Bytes)
{
    return (ULONG)Bytes[0] | ((ULONG)Bytes[1]<<8) |
        ((ULONG)Bytes[2]<<16) | ((ULONG)Bytes[3]<<24);
}
static BOOLEAN Identity(const UCHAR *Header)
{
    return Header[0]==0x02 && Header[1]==0x10 &&
        Header[2]==0xfe && Header[3]==0x13 &&
        Header[11]==3 && (Header[14]&0x7f)==0;
}
NTSTATUS Bc250PciSessionInit(BC250_PCI_SESSION *Session,const BC250_PCI_OPS *Ops,ULONG Irql)
{
    if(!Enabled()) return STATUS_NOT_SUPPORTED;
    if(Session==NULL || Ops==NULL || Irql!=0 ||
        !ZeroBytes(Session,sizeof(*Session)) ||
        Ops->Acquire==NULL || Ops->StillStarted==NULL ||
        Ops->Release==NULL || Ops->Query==NULL) return STATUS_INVALID_PARAMETER;
    Session->Ops=*Ops;
    Session->Signature=BC250_PCI_SIGNATURE;
    Session->State=BC250_PCI_READY;
    return STATUS_SUCCESS;
}
NTSTATUS Bc250PciObserve(BC250_PCI_SESSION *Session,BC250_PCI_OBSERVATION *Output,ULONG Irql)
{
    NTSTATUS status;
    BC250_PCI_OBSERVATION scratch;
    ULONG read;
    if(Output!=NULL) RtlZeroMemory(Output,sizeof(*Output));
    if(!Enabled()) return STATUS_NOT_SUPPORTED;
    if(Session==NULL || Output==NULL || Irql!=0 ||
        Session->Signature!=BC250_PCI_SIGNATURE) return STATUS_INVALID_PARAMETER;
    if(Session->Busy) return STATUS_DEVICE_BUSY;
    if(Session->State!=BC250_PCI_READY) return STATUS_DEVICE_NOT_READY;
    Session->Busy=1;
    Session->State=BC250_PCI_CLOSED; /* One attempt per initialized object. */
    RtlZeroMemory(&scratch,sizeof(scratch));
    status=Session->Ops.Acquire(Session->Ops.Context,&Session->Lease);
    if(status!=STATUS_SUCCESS) {
        if(status>=0) goto Quarantine; /* No PENDING/late completion contract. */
        if(!ZeroBytes(&Session->Lease,sizeof(Session->Lease)))
            goto Quarantine;
        goto Cleanup;
    }
    Session->LeaseHeld=1;
    if(Session->Lease.Anchor==NULL || Session->Lease.Generation==0 ||
        Session->Lease.Epoch==0 ||
        Session->Lease.Generation==~(ULONGLONG)0 ||
        Session->Lease.Epoch==~(ULONGLONG)0 ||
        Session->Ops.StillStarted(Session->Ops.Context,&Session->Lease)!=TRUE) {
        status=STATUS_DEVICE_NOT_READY; goto Cleanup;
    }
    status=Session->Ops.Query(Session->Ops.Context,&Session->Lease,&Session->Bus);
    if(status!=STATUS_SUCCESS) {
        if(status>=0) goto Quarantine;
        if(!ZeroBytes(&Session->Bus,sizeof(Session->Bus))) goto Quarantine;
        goto Cleanup;
    }
    Session->InterfaceHeld=1; /* Provider already took this reference. */
    if(Session->Bus.InterfaceDereference==NULL) goto Quarantine;
    if(Session->Bus.Size!=sizeof(Session->Bus) || Session->Bus.Version!=1 ||
        Session->Bus.InterfaceReference==NULL || Session->Bus.GetBusData==NULL) {
        status=STATUS_DATA_ERROR; goto Cleanup;
    }
    if(Session->Ops.StillStarted(Session->Ops.Context,&Session->Lease)!=TRUE) {
        status=STATUS_DEVICE_NOT_READY; goto Cleanup;
    }
    read=Session->Bus.GetBusData(Session->Bus.Context,PCI_WHICHSPACE_CONFIG,
        scratch.Header,0,BC250_PCI_HEADER_BYTES);
    if(read!=BC250_PCI_HEADER_BYTES || !Identity(scratch.Header)) {
        status=STATUS_DATA_ERROR; goto Cleanup;
    }
    if(Session->Ops.StillStarted(Session->Ops.Context,&Session->Lease)!=TRUE) {
        status=STATUS_DEVICE_NOT_READY; goto Cleanup;
    }
    scratch.Generation=Session->Lease.Generation;
    scratch.Epoch=Session->Lease.Epoch;
    scratch.Valid=1;
    status=STATUS_SUCCESS;
Cleanup:
    if(Session->InterfaceHeld) {
        /* Keep lease anchored throughout provider cleanup; never release first. */
        Session->Bus.InterfaceDereference(Session->Bus.Context);
        Session->InterfaceHeld=0;
        RtlZeroMemory(&Session->Bus,sizeof(Session->Bus));
    }
    /* Detect transition in dereference too. Still NOT an atomic publish gate. */
    if(status==STATUS_SUCCESS &&
        Session->Ops.StillStarted(Session->Ops.Context,&Session->Lease)!=TRUE)
        status=STATUS_DEVICE_NOT_READY;
    if(Session->LeaseHeld) {
        Session->Ops.Release(Session->Ops.Context,&Session->Lease);
        Session->LeaseHeld=0;
        RtlZeroMemory(&Session->Lease,sizeof(Session->Lease));
    }
    if(status==STATUS_SUCCESS) *Output=scratch;
    RtlZeroMemory(&scratch,sizeof(scratch));
    Session->Busy=0;
    return status;
Quarantine:
    Session->State=BC250_PCI_QUARANTINED;
    Session->Busy=0;
    /* Provider violated acquisition contract. Do not guess cleanup/ownership. */
    return STATUS_DATA_ERROR;
}
NTSTATUS Bc250PciCorrelate(const BC250_PCI_OBSERVATION *Observation,
    const BC250_PCI_RESOURCES *Resources,BC250_PCI_CORRELATION *Output)
{
    BC250_PCI_CORRELATION result;
    ULONG slot,index,other,used=0;
    if(Output==NULL) return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(Output,sizeof(*Output));
    RtlZeroMemory(&result,sizeof(result));
    if(Observation==NULL || Resources==NULL ||
        Observation->Valid!=1 || Resources->Started!=1 || Resources->Valid!=1 ||
        Observation->Generation==0 || Observation->Epoch==0 ||
        Observation->Generation==~(ULONGLONG)0 || Observation->Epoch==~(ULONGLONG)0 ||
        Observation->Generation!=Resources->Generation || Observation->Epoch!=Resources->Epoch ||
        !Identity(Observation->Header) || Resources->DescriptorCount>32 ||
        Resources->MemoryCount==0 || Resources->MemoryCount>BC250_PCI_MAX_MEMORY)
        return STATUS_DATA_ERROR;
    for(index=0;index<Resources->MemoryCount;++index) {
        const BC250_PCI_MEMORY *memory=&Resources->Memory[index];
        if(memory->Ordinal>=Resources->DescriptorCount || memory->Length==0 ||
            memory->RawBase==0 || memory->RawBase>~(ULONGLONG)0-memory->Length ||
            memory->TranslatedBase>~(ULONGLONG)0-memory->Length ||
            (memory->RawFlags&0x80)==0 ||
            (index && memory->Ordinal<=Resources->Memory[index-1].Ordinal))
            return STATUS_DATA_ERROR;
        for(other=0;other<index;++other) {
            const BC250_PCI_MEMORY *previous=&Resources->Memory[other];
            if((memory->RawBase<previous->RawBase+previous->Length &&
                previous->RawBase<memory->RawBase+memory->Length) ||
               (memory->TranslatedBase<previous->TranslatedBase+previous->Length &&
                previous->TranslatedBase<memory->TranslatedBase+memory->Length))
                return STATUS_DATA_ERROR;
        }
    }
    for(slot=0;slot<6;++slot) {
        ULONG low=Read32(Observation->Header+16+4*slot),bar=slot,type;
        ULONGLONG base;
        ULONG matches=0,matchIndex=0;
        if(low==0) continue; /* Unassigned slot, no aperture proof. */
        if(low==0xffffffffU) return STATUS_DATA_ERROR;
        if(low&1) continue; /* IO BAR: deliberately not correlated here. */
        type=(low>>1)&3;
        if(type!=0 && type!=2) return STATUS_DATA_ERROR;
        base=(ULONGLONG)(low&0xfffffff0U);
        if(type==2) {
            if(slot==5) return STATUS_DATA_ERROR;
            base|=(ULONGLONG)Read32(Observation->Header+16+4*(++slot))<<32;
        }
        if(base==0) continue;
        for(index=0;index<Resources->MemoryCount;++index)
            if(Resources->Memory[index].RawBase==base &&
                !!(Resources->Memory[index].RawFlags&4)==!!(low&8)) {
                ++matches; matchIndex=index;
            }
        if(matches!=1 || (used&(1U<<matchIndex))) return STATUS_DATA_ERROR;
        used|=1U<<matchIndex;
        result.Matches[result.Count].Bar=bar;
        result.Matches[result.Count].DescriptorOrdinal=Resources->Memory[matchIndex].Ordinal;
        result.Matches[result.Count].RawBase=base;
        result.Matches[result.Count].TranslatedBase=Resources->Memory[matchIndex].TranslatedBase;
        result.Matches[result.Count].PnpLength=Resources->Memory[matchIndex].Length;
        ++result.Count;
    }
    if(used!=((1U<<Resources->MemoryCount)-1U)) return STATUS_DATA_ERROR;
    result.Valid=1;
    /* PnP length is NOT BAR sizing. Raw/translated domains remain distinct.
     * This relation confers neither VRAM ownership nor permission to DMA/map. */
    *Output=result;
    return STATUS_SUCCESS;
}
