/* SPDX-License-Identifier: Apache-2.0 */
#define BC250_PCI_PUBLISHER_MOCK 1
#define BC250_PCI_ADMISSION_MOCK 1
#define BC250_PCI_CONTRACT_MOCK 1
#define BC250_DMA_GATE_MOCK 1
#include "bc250_pci_publisher.h"
#include "../pci-pnp-admission-20261004/mock-admission-gate.c"
#include "../pci-pnp-contract-20261004/bc250_pci_contract.c"
#include "../pci-pnp-admission-20261004/bc250_pci_admission.c"
#include "bc250_pci_publisher.c"
BOOLEAN Bc250AdmissionMockAllowed=TRUE;
BOOLEAN Bc250PublisherMockAllowed=TRUE;
static BC250_PCI_PUBLISHER pub;
static BC250_PCI_BINDING bindings[4];
static BC250_PCI_PIN pins[BC250_PUBLISHER_PINS+1];
static unsigned spinGets,spinPuts,closeHook;
typedef struct BACKEND {
    BC250_PCI_BINDING *Binding;
    unsigned Leases,Interfaces,Reads,Event,Reason,Mode,Reentrant,Closed;
} BACKEND;
static BACKEND providers[4];
VOID KeInitializeSpinLock(KSPIN_LOCK *lock)
{
    CHECK(irql==0 && !lock->Initialized && !lock->Held);lock->Initialized=1;
}
VOID KeAcquireSpinLock(KSPIN_LOCK *lock,KIRQL *oldIrql)
{
    CHECK(irql==0 && lock->Initialized && !lock->Held);
    *oldIrql=(KIRQL)irql;irql=2;lock->Held=1;++spinGets;
}
VOID KeReleaseSpinLock(KSPIN_LOCK *lock,KIRQL oldIrql)
{
    CHECK(irql==2 && lock->Held && oldIrql==0);lock->Held=0;irql=oldIrql;++spinPuts;
}
LONG InterlockedOr(volatile LONG *target,LONG value)
{
    LONG old=*target;++gateCalls;*target=old|value;
    CHECK(!pub.Lock.Held && irql==0);
    if(closeHook){
        BC250_PCI_BINDING *detached=(BC250_PCI_BINDING *)1;
        BC250_PCI_PIN rejected={0};
        CHECK(pub.Current==NULL && pub.Draining!=NULL &&
            pub.Draining->Phase==BC250_BINDING_CLOSING);
        CHECK(Bc250PciPublisherRetire(&pub,&detached)==STATUS_INVALID_DEVICE_STATE && !detached);
        CHECK(Bc250PciPublish(&pub,&bindings[1])==STATUS_DEVICE_BUSY);
        CHECK(Bc250PciPin(&pub,&rejected)==STATUS_DEVICE_NOT_READY);
    }
    return old;
}
static int Empty(const VOID *p,SIZE_T bytes)
{
    const UCHAR *b=(const UCHAR *)p;SIZE_T k;
    for(k=0;k<bytes;++k)if(b[k])return 0;
    return 1;
}
static VOID Pulse(BACKEND *f,unsigned point)
{
    BC250_PCI_BINDING *detached;
    BC250_PCI_SAMPLE sample;
    CHECK(irql==0 && !pub.Lock.Held && pins[0].Busy==1 && f->Binding->Pins==1);
    CHECK(f->Binding->Owner.Gate.Mutex.Held && f->Binding->Owner.Gate.Rundown.Count==1);
    if(f->Event==point && !f->Closed){
        closeHook=1;CHECK(Bc250PciPublisherClose(&pub,f->Reason)==0);closeHook=0;f->Closed=1;
        CHECK(Bc250PciPublisherClose(&pub,f->Reason)==STATUS_DEVICE_NOT_READY);
    }
    if(f->Reentrant){
        CHECK(Bc250PciUnpin(&pub,&pins[0])==STATUS_DEVICE_BUSY);
        CHECK(Bc250PciPinRead(&pub,&pins[0],&sample)==STATUS_DEVICE_BUSY);
        CHECK(Empty(&sample,sizeof(sample)));
        if(pub.Draining && pub.Draining->Phase==BC250_BINDING_DRAINING)
            CHECK(Bc250PciPublisherRetire(&pub,&detached)==STATUS_DEVICE_BUSY && !detached);
        else CHECK(Bc250PciPublisherRetire(&pub,&detached)==STATUS_INVALID_DEVICE_STATE && !detached);
    }
}
static NTSTATUS BackendAcquire(PVOID context,BC250_PCI_LEASE *lease)
{
    BACKEND *f=(BACKEND *)context;Pulse(f,1);
    ++f->Leases;lease->Anchor=f;
    lease->Generation=f->Binding->Owner.Generation;lease->Epoch=f->Binding->Owner.Epoch;
    return 0;
}
static BOOLEAN BackendStill(PVOID context,const BC250_PCI_LEASE *lease)
{
    BACKEND *f=(BACKEND *)context;
    CHECK(lease->Anchor==f && f->Leases==1);Pulse(f,2);return TRUE;
}
static VOID BackendRelease(PVOID context,BC250_PCI_LEASE *lease)
{
    BACKEND *f=(BACKEND *)context;
    CHECK(lease->Anchor==f && f->Leases==1 && !f->Interfaces);Pulse(f,6);--f->Leases;
}
static VOID BackendReference(PVOID context) { (void)context;CHECK(0); }
static VOID BackendDrop(PVOID context)
{
    BACKEND *f=(BACKEND *)context;CHECK(f->Interfaces==1 && f->Leases==1);
    Pulse(f,5);--f->Interfaces;
}
static VOID Put32(UCHAR *p,ULONG v)
{
    p[0]=(UCHAR)v;p[1]=(UCHAR)(v>>8);p[2]=(UCHAR)(v>>16);p[3]=(UCHAR)(v>>24);
}
static ULONG BackendRead(PVOID context,ULONG space,PVOID buffer,ULONG offset,ULONG length)
{
    BACKEND *f=(BACKEND *)context;UCHAR *h=(UCHAR *)buffer;
    CHECK(space==0 && offset==0 && length==64 && f->Leases==1 && f->Interfaces==1);
    Pulse(f,4);++f->Reads;memset(h,0,64);
    h[0]=2;h[1]=0x10;h[2]=0xfe;h[3]=0x13;h[11]=3;
    Put32(h+16,0xc000000cU);Put32(h+24,0xd000000cU);
    Put32(h+32,0xef01U);Put32(h+36,0xfe800000U);
    if(f->Mode==5)Put32(h+16,0xb000000cU); /* Synthetic BAR/resource mismatch. */
    return f->Mode==1 ? 63:64;
}
static NTSTATUS BackendQuery(PVOID context,const BC250_PCI_LEASE *lease,BUS_INTERFACE_STANDARD *bus)
{
    BACKEND *f=(BACKEND *)context;CHECK(lease->Anchor==f && Empty(bus,sizeof(*bus)));Pulse(f,3);
    if(f->Mode==3){bus->Version=1;return STATUS_NOT_SUPPORTED;}
    if(f->Mode==4)return STATUS_PENDING;
    ++f->Interfaces;bus->Size=(USHORT)sizeof(*bus);bus->Version=1;bus->Context=f;
    bus->InterfaceReference=BackendReference;bus->InterfaceDereference=BackendDrop;
    bus->GetBusData=BackendRead;
    if(f->Mode==2)bus->InterfaceDereference=NULL;
    return 0;
}
static VOID Prepare(unsigned which,ULONGLONG generation,ULONGLONG epoch)
{
    BC250_PCI_OPS ops={0};
    BC250_PCI_RESOURCES resources={0};
    BACKEND *f=&providers[which];
    f->Binding=&bindings[which];f->Reason=1;
    ops.Context=f;ops.Acquire=BackendAcquire;ops.StillStarted=BackendStill;
    ops.Release=BackendRelease;ops.Query=BackendQuery;
    resources.Valid=resources.Started=1;resources.Generation=generation;resources.Epoch=epoch;
    resources.DescriptorCount=9;resources.MemoryCount=3;
    resources.Memory[0].RawBase=resources.Memory[0].TranslatedBase=0xc0000000;
    resources.Memory[0].Length=0x10000000;resources.Memory[0].RawFlags=resources.Memory[0].TranslatedFlags=0x84;
    resources.Memory[1].Ordinal=2;resources.Memory[1].RawBase=resources.Memory[1].TranslatedBase=0xd0000000;
    resources.Memory[1].Length=0x200000;resources.Memory[1].RawFlags=resources.Memory[1].TranslatedFlags=0x84;
    resources.Memory[2].Ordinal=6;resources.Memory[2].RawBase=resources.Memory[2].TranslatedBase=0xfe800000;
    resources.Memory[2].Length=0x80000;resources.Memory[2].RawFlags=resources.Memory[2].TranslatedFlags=0x80;
    CHECK(Bc250PciBindingPrepare(&bindings[which],&ops,&resources)==0);
}
static VOID Fresh(VOID)
{
    /* New fictitious world, NEVER real fault recovery/reset/restart. */
    memset(&pub,0,sizeof(pub));memset(bindings,0,sizeof(bindings));
    memset(providers,0,sizeof(providers));memset(pins,0,sizeof(pins));memset(critical,0,sizeof(critical));
    threadIndex=irql=gateCalls=rundownGets=rundownPuts=waits=mutexPuts=0;
    denyRundown=stopAtAcquire=stopAtMutex=0;completeTicket=NULL;closeHook=spinGets=spinPuts=0;
    fakeWaitStatus=STATUS_SUCCESS;Bc250AdmissionMockAllowed=Bc250MockGateExecutionAllowed=TRUE;
    Bc250PublisherMockAllowed=TRUE;
    CHECK(Bc250PciPublisherInit(&pub)==0);Prepare(0,17,31);Prepare(1,18,32);
    eventGate=&bindings[0].Owner.Gate;CHECK(Bc250PciPublish(&pub,&bindings[0])==0);
}
static VOID Balanced(VOID)
{
    CHECK(!pub.Lock.Held && !irql && spinGets==spinPuts && !critical[0] && !critical[1]);
    CHECK(rundownGets==rundownPuts && bindings[0].Pins==0);
}
int main(VOID)
{
    unsigned reason,event,mode,entry,k;
    BC250_PCI_SAMPLE sample;
    BC250_PCI_BINDING *detached;
    BC250_PCI_PIN copy;
    for(reason=1;reason<=32;reason*=2)
     for(event=0;event<=6;++event)
      for(mode=0;mode<=5;++mode)
       for(entry=0;entry<=1;++entry){
        NTSTATUS status;
        Fresh();providers[0].Event=event;providers[0].Reason=reason;
        providers[0].Mode=mode;providers[0].Reentrant=entry;
        CHECK(Bc250PciPin(&pub,&pins[0])==0);
        copy=pins[0];CHECK(Bc250PciUnpin(&pub,&copy)==STATUS_INVALID_PARAMETER);
        CHECK(Bc250PciPin(&pub,&pins[0])==STATUS_INVALID_PARAMETER && bindings[0].Pins==1);
        status=Bc250PciPinRead(&pub,&pins[0],&sample);
        if(event==0 && mode==0){
            CHECK(status==0 && sample.Valid && sample.Pci.Valid && sample.Resources.Valid);
            CHECK(sample.Pci.Generation==17 && sample.Resources.Generation==17);
            CHECK(sample.Pci.Epoch==31 && sample.Resources.Epoch==31 && sample.Relation.Count==3);
            CHECK(!sample.Relation.VramOwnership && !sample.Relation.DmaAuthorized);
            CHECK(sample.Relation.Matches[0].Bar==0 && sample.Relation.Matches[1].Bar==2 &&
                sample.Relation.Matches[2].Bar==5);
        }else CHECK(status!=0 && Empty(&sample,sizeof(sample)));
        CHECK(!pins[0].Busy && bindings[0].Pins==1);
        CHECK(Bc250PciUnpin(&pub,&pins[0])==0 && Empty(&pins[0],sizeof(pins[0])));
        if(bindings[0].Phase==BC250_BINDING_FAULT){
            CHECK(pub.Current==NULL && pub.Draining==&bindings[0]);
            CHECK(Bc250PciPublisherRetire(&pub,&detached)==STATUS_INVALID_DEVICE_STATE && !detached);
            CHECK(Bc250PciPublish(&pub,&bindings[1])==STATUS_DEVICE_BUSY);
        }else{
            if(pub.Current)CHECK(Bc250PciPublisherClose(&pub,reason)==0);
            CHECK(Bc250PciPublisherRetire(&pub,&detached)==0 && detached==&bindings[0]);
            CHECK(bindings[0].Phase==BC250_BINDING_RETIRED && !providers[0].Leases && !providers[0].Interfaces);
            CHECK(Bc250PciPublish(&pub,&bindings[1])==0);
        }
        Balanced();
       }
    Fresh();
    for(k=0;k<BC250_PUBLISHER_PINS;++k)CHECK(Bc250PciPin(&pub,&pins[k])==0);
    CHECK(bindings[0].Pins==16 && Bc250PciPin(&pub,&pins[16])==STATUS_DEVICE_BUSY);
    CHECK(Bc250PciPublisherClose(&pub,32)==0);
    CHECK(Bc250PciPublisherRetire(&pub,&detached)==STATUS_DEVICE_BUSY && !detached);
    for(k=0;k<16;++k){
        CHECK(Bc250PciPinRead(&pub,&pins[k],&sample)==STATUS_DEVICE_NOT_READY && Empty(&sample,sizeof(sample)));
        CHECK(Bc250PciUnpin(&pub,&pins[k])==0);
    }
    CHECK(Bc250PciPublisherRetire(&pub,&detached)==0);Balanced();
    Fresh();CHECK(Bc250PciPin(&pub,&pins[0])==0);copy=pins[0];
    { BC250_PCI_PUBLISHER otherRoot={0};
      CHECK(Bc250PciPublisherInit(&otherRoot)==0);
      CHECK(Bc250PciPublish(&otherRoot,&bindings[0])==STATUS_INVALID_DEVICE_STATE);
      CHECK(Bc250PciPinRead(&otherRoot,&pins[0],&sample)==STATUS_INVALID_PARAMETER);
      CHECK(Bc250PciUnpin(&otherRoot,&pins[0])==STATUS_INVALID_PARAMETER);
      CHECK(!otherRoot.Current && !otherRoot.Draining && bindings[0].Pins==1); }
    threadIndex=1;CHECK(Bc250PciUnpin(&pub,&pins[0])==STATUS_INVALID_PARAMETER);
    CHECK(Bc250PciPinRead(&pub,&pins[0],&sample)==STATUS_INVALID_PARAMETER);
    pins[0].Thread=KeGetCurrentThread();CHECK(Bc250PciUnpin(&pub,&pins[0])==STATUS_INVALID_PARAMETER);
    pins[0].Thread=copy.Thread;threadIndex=0;
    CHECK(Bc250PciUnpin(&pub,&pins[0])==0);CHECK(Bc250PciPin(&pub,&pins[0])==0);
    { ULONGLONG newId=pins[0].Id;
      pins[0].Id=copy.Id;CHECK(Bc250PciUnpin(&pub,&pins[0])==STATUS_INVALID_PARAMETER);
      pins[0].Id=newId; }
    CHECK(Bc250PciPinRead(&pub,&pins[0],&sample)==0);
    CHECK(Bc250PciPinRead(&pub,&pins[0],&sample)==STATUS_DEVICE_NOT_READY && Empty(&sample,sizeof(sample)));
    CHECK(Bc250PciUnpin(&pub,&pins[0])==0);
    CHECK(Bc250PciPublisherClose(&pub,1)==0 && Bc250PciPublisherRetire(&pub,&detached)==0);
    Prepare(2,17,31);CHECK(Bc250PciPublish(&pub,&bindings[2])==STATUS_INVALID_DEVICE_STATE);
    Prepare(3,19,31);CHECK(Bc250PciPublish(&pub,&bindings[3])==STATUS_INVALID_DEVICE_STATE);
    CHECK(Bc250PciPublish(&pub,&bindings[1])==0);
    eventGate=&bindings[1].Owner.Gate;CHECK(Bc250PciPin(&pub,&pins[0])==0);
    CHECK(Bc250PciUnpin(&pub,&copy)==STATUS_INVALID_PARAMETER);
    CHECK(Bc250PciPinRead(&pub,&pins[0],&sample)==0 && sample.Pci.Generation==18 && sample.Resources.Epoch==32);
    CHECK(Bc250PciUnpin(&pub,&pins[0])==0);
    CHECK(Bc250PciPublisherClose(&pub,16)==0 && Bc250PciPublisherRetire(&pub,&detached)==0);
    Fresh();pub.NextId=~(ULONGLONG)0-2U;
    CHECK(Bc250PciPin(&pub,&pins[0])==0 && pins[0].Id==~(ULONGLONG)0-1U);
    CHECK(Bc250PciPin(&pub,&pins[1])==STATUS_INVALID_DEVICE_STATE && Empty(&pins[1],sizeof(pins[1])));
    CHECK(Bc250PciUnpin(&pub,&pins[0])==0);
    CHECK(Bc250PciPublisherClose(&pub,4)==0 && Bc250PciPublisherRetire(&pub,&detached)==0);
    Fresh();CHECK(Bc250PciPublisherClose(&pub,0)==STATUS_INVALID_PARAMETER && pub.Current==&bindings[0]);
    CHECK(Bc250PciPublisherClose(&pub,3)==STATUS_INVALID_PARAMETER);
    irql=1;CHECK(Bc250PciPin(&pub,&pins[0])==STATUS_INVALID_DEVICE_STATE);irql=0;
    Bc250PublisherMockAllowed=FALSE;
    { unsigned callsBefore=calls,gateBefore=gateCalls,spinBefore=spinGets;
      CHECK(Bc250PciPublisherInit(NULL)==STATUS_NOT_SUPPORTED);
      CHECK(Bc250PciBindingPrepare(NULL,NULL,NULL)==STATUS_NOT_SUPPORTED);
      CHECK(Bc250PciPublish(NULL,NULL)==STATUS_NOT_SUPPORTED);
      CHECK(Bc250PciPin(NULL,NULL)==STATUS_NOT_SUPPORTED);
      CHECK(Bc250PciPinRead(NULL,NULL,&sample)==STATUS_NOT_SUPPORTED && Empty(&sample,sizeof(sample)));
      CHECK(Bc250PciUnpin(NULL,NULL)==STATUS_NOT_SUPPORTED);
      CHECK(Bc250PciPublisherClose(NULL,1)==STATUS_NOT_SUPPORTED);
      CHECK(Bc250PciPublisherRetire(NULL,&detached)==STATUS_NOT_SUPPORTED && !detached);
      CHECK(calls==callsBefore && gateCalls==gateBefore && spinGets==spinBefore); }
    printf("PASS: %u publisher checks; synthetic lifetime and historical tuples only, no hardware/PnP/SMP proof.\n",checks);
    return 0;
}
