/* SPDX-License-Identifier: Apache-2.0 */
#include "bc250_pci_publisher.h"
static BOOLEAN PublisherEnabled(VOID)
{
#ifdef BC250_PCI_PUBLISHER_MOCK
    return Bc250PublisherMockAllowed;
#else
    return FALSE; /* No native activation switch. */
#endif
}
static NTSTATUS PublisherPolicy(VOID)
{
    if(!PublisherEnabled()) return STATUS_NOT_SUPPORTED;
    return KeGetCurrentIrql()==PASSIVE_LEVEL ? STATUS_SUCCESS:STATUS_INVALID_DEVICE_STATE;
}
static NTSTATUS PublisherGuard(BC250_PCI_PUBLISHER *Root)
{
    NTSTATUS status=PublisherPolicy();
    if(status!=STATUS_SUCCESS) return status;
    if(Root==NULL || Root->Signature!=BC250_PUBLISHER_SIGNATURE)
        return STATUS_INVALID_PARAMETER;
    return STATUS_SUCCESS;
}
/* Call only under Root.Lock. No callbacks, allocation or waiting. */
static ULONG FindPin(BC250_PCI_PUBLISHER *Root,BC250_PCI_PIN *Pin)
{
    ULONG n;
    if(Pin==NULL || Pin->Root!=Root || Pin->Thread!=KeGetCurrentThread() ||
        Pin->Id==0 || Pin->Id==~(ULONGLONG)0) return BC250_PUBLISHER_PINS;
    for(n=0;n<BC250_PUBLISHER_PINS;++n) {
        BC250_PCI_PIN_RECORD *record=&Root->Records[n];
        if(record->Ticket==Pin && record->Binding==Pin->Binding &&
            record->Thread==Pin->Thread && record->Thread==KeGetCurrentThread() &&
            record->Id==Pin->Id && Pin->Binding!=NULL &&
            Pin->Generation==record->Binding->Owner.Generation &&
            Pin->Epoch==record->Binding->Owner.Epoch) return n;
    }
    return BC250_PUBLISHER_PINS;
}
NTSTATUS Bc250PciPublisherInit(BC250_PCI_PUBLISHER *Root)
{
    BC250_PCI_PUBLISHER zero;
    NTSTATUS status=PublisherPolicy();
    if(status!=STATUS_SUCCESS) return status;
    RtlZeroMemory(&zero,sizeof(zero));
    if(Root==NULL || RtlCompareMemory(Root,&zero,sizeof(zero))!=sizeof(zero))
        return STATUS_INVALID_PARAMETER;
    KeInitializeSpinLock(&Root->Lock);
    Root->Signature=BC250_PUBLISHER_SIGNATURE;
    return STATUS_SUCCESS;
}
NTSTATUS Bc250PciBindingPrepare(BC250_PCI_BINDING *Binding,const BC250_PCI_OPS *Backend,
    const BC250_PCI_RESOURCES *Resources)
{
    BC250_PCI_BINDING zero;
    NTSTATUS status=PublisherPolicy();
    if(status!=STATUS_SUCCESS) return status;
    RtlZeroMemory(&zero,sizeof(zero));
    if(Binding==NULL || Resources==NULL || Backend==NULL ||
        RtlCompareMemory(Binding,&zero,sizeof(zero))!=sizeof(zero) ||
        Resources->Valid!=1 || Resources->Started!=1 ||
        Resources->DescriptorCount==0 || Resources->DescriptorCount>32 ||
        Resources->MemoryCount==0 || Resources->MemoryCount>BC250_PCI_MAX_MEMORY)
        return STATUS_INVALID_PARAMETER;
    Binding->Signature=BC250_BINDING_SIGNATURE;
    Binding->Resources=*Resources;
    status=Bc250PciOwnerInit(&Binding->Owner,Backend,Resources->Generation,Resources->Epoch);
    Binding->Phase=status==STATUS_SUCCESS ? BC250_BINDING_PREPARED:BC250_BINDING_FAULT;
    return status;
}
NTSTATUS Bc250PciPublish(BC250_PCI_PUBLISHER *Root,BC250_PCI_BINDING *Binding)
{
    KIRQL oldIrql;
    NTSTATUS status=PublisherGuard(Root);
    if(status!=STATUS_SUCCESS) return status;
    if(Binding==NULL) return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&Root->Lock,&oldIrql);
    if(Root->Current!=NULL || Root->Draining!=NULL) status=STATUS_DEVICE_BUSY;
    else if(Binding->Signature!=BC250_BINDING_SIGNATURE ||
        Binding->Phase!=BC250_BINDING_PREPARED || Binding->Home!=NULL ||
        Binding->Owner.Generation<=Root->LastGeneration ||
        Binding->Owner.Epoch<=Root->LastEpoch ||
        Binding->Resources.Generation!=Binding->Owner.Generation ||
        Binding->Resources.Epoch!=Binding->Owner.Epoch ||
        InterlockedCompareExchange(&Binding->Owner.Gate.State,0,0)!=BC250_DMA_GATE_RUNNING ||
        InterlockedCompareExchange(&Binding->Owner.Retirement,0,0)!=BC250_PCI_OWNER_ACTIVE ||
        InterlockedCompareExchange(&Binding->Owner.Faulted,0,0)!=0 ||
        InterlockedCompareExchange(&Binding->Owner.Reasons,0,0)!=0)
        status=STATUS_INVALID_DEVICE_STATE;
    else {
        Binding->Home=Root;Binding->Phase=BC250_BINDING_LIVE;
        Root->Current=Binding;
        Root->LastGeneration=Binding->Owner.Generation;Root->LastEpoch=Binding->Owner.Epoch;
    }
    KeReleaseSpinLock(&Root->Lock,oldIrql);
    return status;
}
NTSTATUS Bc250PciPin(BC250_PCI_PUBLISHER *Root,BC250_PCI_PIN *Pin)
{
    BC250_PCI_PIN zero;
    KIRQL oldIrql;
    ULONG n;
    NTSTATUS status=PublisherGuard(Root);
    if(status!=STATUS_SUCCESS) return status;
    RtlZeroMemory(&zero,sizeof(zero));
    if(Pin==NULL || RtlCompareMemory(Pin,&zero,sizeof(zero))!=sizeof(zero))
        return STATUS_INVALID_PARAMETER; /* Never wipe an already live Pin. */
    KeAcquireSpinLock(&Root->Lock,&oldIrql);
    if(Root->Current==NULL || Root->Current->Phase!=BC250_BINDING_LIVE)
        status=STATUS_DEVICE_NOT_READY;
    else if(Root->NextId>=~(ULONGLONG)0-1U) status=STATUS_INVALID_DEVICE_STATE;
    else {
        for(n=0;n<BC250_PUBLISHER_PINS && Root->Records[n].Ticket!=NULL;++n) {}
        if(n==BC250_PUBLISHER_PINS) status=STATUS_DEVICE_BUSY;
        else {
            Pin->Root=Root;Pin->Binding=Root->Current;Pin->Thread=KeGetCurrentThread();
            Pin->Id=++Root->NextId;Pin->Generation=Root->Current->Owner.Generation;
            Pin->Epoch=Root->Current->Owner.Epoch;
            Root->Records[n].Ticket=Pin;Root->Records[n].Binding=Root->Current;
            Root->Records[n].Thread=Pin->Thread;
            Root->Records[n].Id=Pin->Id;++Root->Current->Pins;
        }
    }
    KeReleaseSpinLock(&Root->Lock,oldIrql);
    return status;
}
NTSTATUS Bc250PciPinRead(BC250_PCI_PUBLISHER *Root,BC250_PCI_PIN *Pin,BC250_PCI_SAMPLE *Output)
{
    BC250_PCI_SAMPLE scratch;
    BC250_PCI_BINDING *binding;
    KIRQL oldIrql;
    ULONG n;
    NTSTATUS status;
    if(Output!=NULL) RtlZeroMemory(Output,sizeof(*Output));
    status=PublisherGuard(Root);
    if(status!=STATUS_SUCCESS) return status;
    if(Output==NULL) return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(&scratch,sizeof(scratch));
    KeAcquireSpinLock(&Root->Lock,&oldIrql);
    n=FindPin(Root,Pin);
    if(n==BC250_PUBLISHER_PINS) status=STATUS_INVALID_PARAMETER;
    else if(Pin->Busy) status=STATUS_DEVICE_BUSY;
    else if(Pin->Binding!=Root->Current || Pin->Binding->Phase!=BC250_BINDING_LIVE)
        status=STATUS_DEVICE_NOT_READY;
    if(status!=STATUS_SUCCESS) {
        KeReleaseSpinLock(&Root->Lock,oldIrql);return status;
    }
    Pin->Busy=1;binding=Pin->Binding;
    KeReleaseSpinLock(&Root->Lock,oldIrql);
    /* Pin remains registered through ALL Owner API returns, even rejects. */
    status=Bc250PciOwnerObserve(&binding->Owner,&scratch.Pci);
    if(status==STATUS_SUCCESS) {
        scratch.Resources=binding->Resources; /* Immutable, pinned, outside lock. */
        status=Bc250PciCorrelate(&scratch.Pci,&scratch.Resources,&scratch.Relation);
    }
    KeAcquireSpinLock(&Root->Lock,&oldIrql);
    if(FindPin(Root,Pin)!=n || Pin->Busy!=1) {
        binding->Phase=BC250_BINDING_FAULT;Root->Current=NULL;Root->Draining=binding;
        status=STATUS_INVALID_DEVICE_STATE; /* Unknown pin contract: retain anchors. */
    } else {
        if(InterlockedCompareExchange(&binding->Owner.Faulted,0,0)!=0) {
            binding->Phase=BC250_BINDING_FAULT;Root->Current=NULL;Root->Draining=binding;
        }
        if(status==STATUS_SUCCESS &&
            (Root->Current!=binding || binding->Phase!=BC250_BINDING_LIVE))
            status=STATUS_DEVICE_NOT_READY;
        if(status==STATUS_SUCCESS) {
            scratch.Valid=1;*Output=scratch; /* Busy retained throughout final copy. */
        }
        Pin->Busy=0;
    }
    KeReleaseSpinLock(&Root->Lock,oldIrql);
    RtlZeroMemory(&scratch,sizeof(scratch));
    return status; /* No Binding payload access after this point. */
}
NTSTATUS Bc250PciUnpin(BC250_PCI_PUBLISHER *Root,BC250_PCI_PIN *Pin)
{
    KIRQL oldIrql;
    ULONG n;
    NTSTATUS status=PublisherGuard(Root);
    if(status!=STATUS_SUCCESS) return status;
    KeAcquireSpinLock(&Root->Lock,&oldIrql);
    n=FindPin(Root,Pin);
    if(n==BC250_PUBLISHER_PINS) status=STATUS_INVALID_PARAMETER;
    else if(Pin->Busy) status=STATUS_DEVICE_BUSY;
    else {
        --Root->Records[n].Binding->Pins;
        RtlZeroMemory(&Root->Records[n],sizeof(Root->Records[n]));
        RtlZeroMemory(Pin,sizeof(*Pin));
    }
    KeReleaseSpinLock(&Root->Lock,oldIrql);
    return status; /* No binding/pin payload access after dropping pin. */
}
NTSTATUS Bc250PciPublisherClose(BC250_PCI_PUBLISHER *Root,ULONG Reason)
{
    BC250_PCI_BINDING *binding;
    KIRQL oldIrql;
    NTSTATUS status=PublisherGuard(Root);
    if(status!=STATUS_SUCCESS) return status;
    if(Reason==0 || Reason>BC250_PCI_REASON_REMOVE || (Reason&(Reason-1U))!=0)
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&Root->Lock,&oldIrql);
    binding=Root->Current;
    if(binding==NULL || binding->Phase!=BC250_BINDING_LIVE) {
        KeReleaseSpinLock(&Root->Lock,oldIrql);return STATUS_DEVICE_NOT_READY;
    }
    binding->Phase=BC250_BINDING_CLOSING;Root->Current=NULL;Root->Draining=binding;
    KeReleaseSpinLock(&Root->Lock,oldIrql);
    status=Bc250PciOwnerStop(&binding->Owner,Reason);
    KeAcquireSpinLock(&Root->Lock,&oldIrql);
    /* PinRead quarantine can mark FAULT while Stop is outside lock. */
    if(status!=STATUS_SUCCESS || binding->Phase!=BC250_BINDING_CLOSING) {
        binding->Phase=BC250_BINDING_FAULT;
        if(status==STATUS_SUCCESS) status=STATUS_INVALID_DEVICE_STATE;
    }
    else binding->Phase=BC250_BINDING_DRAINING;
    KeReleaseSpinLock(&Root->Lock,oldIrql);
    return status;
}
NTSTATUS Bc250PciPublisherRetire(BC250_PCI_PUBLISHER *Root,BC250_PCI_BINDING **Detached)
{
    BC250_PCI_BINDING *binding;
    KIRQL oldIrql;
    NTSTATUS status;
    if(Detached!=NULL) *Detached=NULL;
    status=PublisherGuard(Root);
    if(status!=STATUS_SUCCESS) return status;
    if(Detached==NULL) return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&Root->Lock,&oldIrql);
    binding=Root->Draining;
    if(binding==NULL || binding->Phase!=BC250_BINDING_DRAINING)
        status=STATUS_INVALID_DEVICE_STATE;
    else if(binding->Pins!=0) status=STATUS_DEVICE_BUSY; /* Never wait on own Pin. */
    if(status!=STATUS_SUCCESS) {
        KeReleaseSpinLock(&Root->Lock,oldIrql);return status;
    }
    binding->Phase=BC250_BINDING_RETIRING;
    KeReleaseSpinLock(&Root->Lock,oldIrql);
    status=Bc250PciOwnerRetire(&binding->Owner); /* Root management anchor retained. */
    KeAcquireSpinLock(&Root->Lock,&oldIrql);
    if(status==STATUS_SUCCESS) {
        binding->Phase=BC250_BINDING_RETIRED;Root->Draining=NULL;*Detached=binding;
    } else binding->Phase=BC250_BINDING_FAULT;
    KeReleaseSpinLock(&Root->Lock,oldIrql);
    return status; /* Detached storage only reclaimable after complete API return. */
}
