/* SPDX-License-Identifier: Apache-2.0 */
#include "bc250_pci_admission.h"
static BOOLEAN OwnerPolicy(VOID)
{
#ifdef BC250_PCI_ADMISSION_MOCK
    return Bc250AdmissionMockAllowed;
#else
    return FALSE; /* No production activation switch. */
#endif
}
static NTSTATUS OwnerGuard(BC250_PCI_OWNER *Owner)
{
    if(!OwnerPolicy()) return STATUS_NOT_SUPPORTED;
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL) return STATUS_INVALID_DEVICE_STATE;
    if(Owner==NULL) return STATUS_INVALID_PARAMETER;
    return STATUS_SUCCESS;
}
static BOOLEAN OwnerStarted(BC250_PCI_OWNER *Owner)
{
    return InterlockedCompareExchange(&Owner->Gate.State,0,0)==BC250_DMA_GATE_RUNNING &&
        InterlockedCompareExchange(&Owner->Reasons,0,0)==0 &&
        InterlockedCompareExchange(&Owner->Faulted,0,0)==0;
}
static NTSTATUS OwnerAcquire(PVOID Context,BC250_PCI_LEASE *Lease)
{
    BC250_PCI_OWNER *pciOwner=(BC250_PCI_OWNER *)Context;
    NTSTATUS status;
    if(!OwnerStarted(pciOwner)) return STATUS_DEVICE_NOT_READY;
    status=pciOwner->Backend.Acquire(pciOwner->Backend.Context,Lease);
    if(status==STATUS_SUCCESS &&
        (Lease->Generation!=pciOwner->Generation || Lease->Epoch!=pciOwner->Epoch)) {
        /* Trusted synchronous success owns a known lease; discard old tokens. */
        pciOwner->Backend.Release(pciOwner->Backend.Context,Lease);
        RtlZeroMemory(Lease,sizeof(*Lease));
        return STATUS_DEVICE_NOT_READY;
    }
    return status;
}
static BOOLEAN OwnerStill(PVOID Context,const BC250_PCI_LEASE *Lease)
{
    BC250_PCI_OWNER *pciOwner=(BC250_PCI_OWNER *)Context;
    return OwnerStarted(pciOwner) && Lease->Generation==pciOwner->Generation &&
        Lease->Epoch==pciOwner->Epoch &&
        pciOwner->Backend.StillStarted(pciOwner->Backend.Context,Lease)==TRUE;
}
static VOID OwnerRelease(PVOID Context,BC250_PCI_LEASE *Lease)
{
    BC250_PCI_OWNER *pciOwner=(BC250_PCI_OWNER *)Context;
    pciOwner->Backend.Release(pciOwner->Backend.Context,Lease);
}
static NTSTATUS OwnerQuery(PVOID Context,const BC250_PCI_LEASE *Lease,BUS_INTERFACE_STANDARD *Bus)
{
    BC250_PCI_OWNER *pciOwner=(BC250_PCI_OWNER *)Context;
    if(!OwnerStarted(pciOwner)) return STATUS_DEVICE_NOT_READY;
    return pciOwner->Backend.Query(pciOwner->Backend.Context,Lease,Bus);
}
NTSTATUS Bc250PciOwnerInit(BC250_PCI_OWNER *Owner,const BC250_PCI_OPS *Backend,
    ULONGLONG Generation,ULONGLONG Epoch)
{
    BC250_PCI_OWNER zero;
    BC250_PCI_OPS wrapper;
    NTSTATUS status=OwnerGuard(Owner);
    if(status!=STATUS_SUCCESS) return status;
    RtlZeroMemory(&zero,sizeof(zero));
    if(Backend==NULL || Backend->Acquire==NULL || Backend->StillStarted==NULL ||
        Backend->Release==NULL || Backend->Query==NULL ||
        Generation==0 || Epoch==0 || Generation==~(ULONGLONG)0 || Epoch==~(ULONGLONG)0 ||
        RtlCompareMemory(Owner,&zero,sizeof(zero))!=sizeof(zero))
        return STATUS_INVALID_PARAMETER;
    Owner->Backend=*Backend;
    Owner->Generation=Generation;Owner->Epoch=Epoch;
    status=Bc250DmaGateInit(&Owner->Gate);
    if(status!=STATUS_SUCCESS) return status;
    RtlZeroMemory(&wrapper,sizeof(wrapper));
    wrapper.Context=Owner;wrapper.Acquire=OwnerAcquire;wrapper.StillStarted=OwnerStill;
    wrapper.Release=OwnerRelease;wrapper.Query=OwnerQuery;
    status=Bc250PciSessionInit(&Owner->Session,&wrapper,PASSIVE_LEVEL);
    if(status!=STATUS_SUCCESS) {
        InterlockedExchange(&Owner->Faulted,1);
        InterlockedExchange(&Owner->Retirement,BC250_PCI_OWNER_FAULT);
        return status; /* Failed init is never published/retried. */
    }
    Owner->Signature=BC250_PCI_OWNER_SIGNATURE;
    return STATUS_SUCCESS;
}
NTSTATUS Bc250PciOwnerObserve(BC250_PCI_OWNER *Owner,BC250_PCI_OBSERVATION *Output)
{
    BC250_DMA_GATE_TICKET ticket={0};
    NTSTATUS status,leave;
    if(Output!=NULL) RtlZeroMemory(Output,sizeof(*Output));
    status=OwnerGuard(Owner);
    if(status!=STATUS_SUCCESS) return status;
    if(Output==NULL || Owner->Signature!=BC250_PCI_OWNER_SIGNATURE)
        return STATUS_INVALID_PARAMETER;
    status=Bc250DmaGateEnter(&Owner->Gate,&ticket);
    if(status!=STATUS_SUCCESS) return status;
    if(InterlockedCompareExchange(&Owner->Retirement,0,0)!=BC250_PCI_OWNER_ACTIVE ||
        !OwnerStarted(Owner)) status=STATUS_DEVICE_NOT_READY;
    else status=Bc250PciObserve(&Owner->Session,Output,PASSIVE_LEVEL);
    if(Owner->Session.State==BC250_PCI_QUARANTINED) {
        InterlockedExchange(&Owner->Faulted,1);
        (VOID)Bc250DmaGateStop(&Owner->Gate);
    }
    if(status==STATUS_SUCCESS && !OwnerStarted(Owner)) {
        RtlZeroMemory(Output,sizeof(*Output));status=STATUS_DEVICE_NOT_READY;
    }
    leave=Bc250DmaGateLeave(&Owner->Gate,&ticket);
    /* No Owner/Gate payload access after releasing the last gate reference.
     * The caller must still anchor storage through the complete API return. */
    if(leave!=STATUS_SUCCESS) {
        RtlZeroMemory(Output,sizeof(*Output));return leave;
    }
    return status;
}
NTSTATUS Bc250PciOwnerStop(BC250_PCI_OWNER *Owner,ULONG Reason)
{
    NTSTATUS status=OwnerGuard(Owner);
    if(status!=STATUS_SUCCESS) return status;
    if(Owner->Signature!=BC250_PCI_OWNER_SIGNATURE || Reason==0 ||
        Reason>BC250_PCI_REASON_REMOVE || (Reason&(Reason-1U))!=0)
        return STATUS_INVALID_PARAMETER;
    InterlockedOr(&Owner->Reasons,(LONG)Reason);
    return Bc250DmaGateStop(&Owner->Gate);
}
NTSTATUS Bc250PciOwnerRetire(BC250_PCI_OWNER *Owner)
{
    NTSTATUS status=OwnerGuard(Owner);
    LONG phase;
    if(status!=STATUS_SUCCESS) return status;
    if(Owner->Signature!=BC250_PCI_OWNER_SIGNATURE) return STATUS_INVALID_PARAMETER;
    if(InterlockedCompareExchangePointer(&Owner->Gate.OwnerThread,NULL,NULL)==KeGetCurrentThread())
        return STATUS_DEVICE_BUSY; /* Do not poison phase by claiming then waiting on self. */
    if(InterlockedCompareExchange(&Owner->Faulted,0,0)!=0)
        return STATUS_INVALID_DEVICE_STATE;
    if(InterlockedCompareExchange(&Owner->Gate.State,0,0)!=BC250_DMA_GATE_STOPPING)
        return STATUS_INVALID_DEVICE_STATE;
    phase=InterlockedCompareExchange(&Owner->Retirement,
        BC250_PCI_OWNER_RETIRING,BC250_PCI_OWNER_ACTIVE);
    if(phase!=BC250_PCI_OWNER_ACTIVE)
        return phase==BC250_PCI_OWNER_RETIRING ? STATUS_DEVICE_BUSY:STATUS_INVALID_DEVICE_STATE;
    status=Bc250DmaGateQuiesce(&Owner->Gate);
    if(status!=STATUS_SUCCESS || InterlockedCompareExchange(&Owner->Faulted,0,0)!=0 ||
        Owner->Session.LeaseHeld || Owner->Session.InterfaceHeld) {
        InterlockedExchange(&Owner->Retirement,BC250_PCI_OWNER_FAULT);
        return STATUS_INVALID_DEVICE_STATE; /* Retained unknown refs prohibit deletion. */
    }
    InterlockedExchange(&Owner->Retirement,BC250_PCI_OWNER_RETIRED);
    return STATUS_SUCCESS; /* Only model access drained; no hardware/resource release. */
}
