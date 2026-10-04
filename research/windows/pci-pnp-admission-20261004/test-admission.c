/* SPDX-License-Identifier: Apache-2.0 */
#define BC250_PCI_ADMISSION_MOCK 1
#define BC250_PCI_CONTRACT_MOCK 1
#define BC250_DMA_GATE_MOCK 1
#include "bc250_pci_admission.h"
#include "mock-admission-gate.c"
#include "../pci-pnp-contract-20261004/bc250_pci_contract.c"
#include "bc250_pci_admission.c"
/* Same frozen gate fake DDIs, including deterministic scheduler events.
 * This does NOT provide real threads, IRQL, rundown or PDO lifetime. */
BOOLEAN Bc250AdmissionMockAllowed=TRUE;
LONG InterlockedOr(volatile LONG *target,LONG value)
{
    LONG old=*target;++gateCalls;*target=old|value;return old;
}
typedef struct PCI_FAKE {
    BC250_PCI_OWNER Owner;
    BC250_PCI_OPS Ops;
    unsigned Leases,Interfaces,AcquireCalls,QueryCalls,ReadCalls,Drops,Releases;
    unsigned Event,Reason,Malformed,ShortRead,Reentrant,EpochMismatch;
    NTSTATUS AcquireStatus,QueryStatus;
} PCI_FAKE;
static PCI_FAKE pci;
static int Clear(const VOID *buffer,SIZE_T count)
{
    const UCHAR *p=(const UCHAR *)buffer;
    SIZE_T n;
    for(n=0;n<count;++n)if(p[n])return 0;
    return 1;
}
static VOID OwnerPulse(PCI_FAKE *f,unsigned point)
{
    BC250_PCI_OBSERVATION out;
    unsigned held=threadIndex;
    CHECK(f->Owner.Gate.OwnerThread==KeGetCurrentThread());
    CHECK(f->Owner.Gate.Rundown.Count==1 && f->Owner.Gate.Mutex.Held && critical[held]==1);
    if(f->Event==point) CHECK(Bc250PciOwnerStop(&f->Owner,f->Reason)==STATUS_SUCCESS);
    if(f->Reentrant){
        /* Same-thread reentry rejected before claim. Self-retire never waits. */
        CHECK(Bc250PciOwnerObserve(&f->Owner,&out)==STATUS_DEVICE_BUSY);
        CHECK(Clear(&out,sizeof(out)));
        CHECK(Bc250PciOwnerRetire(&f->Owner)==STATUS_DEVICE_BUSY);
        CHECK(f->Owner.Retirement==BC250_PCI_OWNER_ACTIVE);
        threadIndex=1;
        CHECK(Bc250PciOwnerObserve(&f->Owner,&out)!=STATUS_SUCCESS);
        CHECK(Clear(&out,sizeof(out)) && critical[1]==0);
        threadIndex=held;
    }
}
static NTSTATUS PciAcquire(PVOID context,BC250_PCI_LEASE *lease)
{
    PCI_FAKE *f=(PCI_FAKE *)context;++f->AcquireCalls;OwnerPulse(f,1);
    if(f->AcquireStatus!=STATUS_SUCCESS)return f->AcquireStatus;
    ++f->Leases;lease->Anchor=f;
    lease->Generation=f->Owner.Generation;lease->Epoch=f->Owner.Epoch;
    if(f->EpochMismatch)++lease->Epoch;
    return 0;
}
static BOOLEAN PciStill(PVOID context,const BC250_PCI_LEASE *lease)
{
    PCI_FAKE *f=(PCI_FAKE *)context;
    CHECK(f->Leases==1 && lease->Anchor==f);
    OwnerPulse(f,2);
    return TRUE; /* Owner wrapper, not this fake, checks admission STOP. */
}
static VOID PciRelease(PVOID context,BC250_PCI_LEASE *lease)
{
    PCI_FAKE *f=(PCI_FAKE *)context;
    CHECK(f->Leases==1 && f->Interfaces==0 && lease->Anchor==f);
    OwnerPulse(f,6);--f->Leases;++f->Releases;
}
static VOID PciReference(PVOID context)
{
    (void)context;CHECK(0); /* Provider has already taken interface reference. */
}
static VOID PciDrop(PVOID context)
{
    PCI_FAKE *f=(PCI_FAKE *)context;
    CHECK(f->Leases==1 && f->Interfaces==1);
    OwnerPulse(f,5);--f->Interfaces;++f->Drops;
}
static ULONG PciRead(PVOID context,ULONG space,PVOID buffer,ULONG offset,ULONG length)
{
    PCI_FAKE *f=(PCI_FAKE *)context;
    UCHAR *h=(UCHAR *)buffer;
    CHECK(space==0 && offset==0 && length==64 && f->Leases==1 && f->Interfaces==1);
    ++f->ReadCalls;OwnerPulse(f,4);
    memset(h,0,64);h[0]=2;h[1]=0x10;h[2]=0xfe;h[3]=0x13;h[11]=3;
    return f->ShortRead ? 63:64;
}
static NTSTATUS PciQuery(PVOID context,const BC250_PCI_LEASE *lease,BUS_INTERFACE_STANDARD *bus)
{
    PCI_FAKE *f=(PCI_FAKE *)context;
    CHECK(f->Leases==1 && lease->Anchor==f && Clear(bus,sizeof(*bus)));
    ++f->QueryCalls;OwnerPulse(f,3);
    if(f->QueryStatus!=STATUS_SUCCESS){
        if(f->Malformed==3)bus->Version=1;return f->QueryStatus;
    }
    ++f->Interfaces;bus->Size=(USHORT)sizeof(*bus);bus->Version=1;bus->Context=f;
    bus->InterfaceReference=PciReference;bus->InterfaceDereference=PciDrop;bus->GetBusData=PciRead;
    if(f->Malformed==1)bus->InterfaceDereference=NULL;
    if(f->Malformed==2)bus->GetBusData=NULL;
    return 0;
}
static VOID NewFixture(VOID)
{
    /* Fresh fictitious world, never resets a real retired/quarantined owner. */
    memset(&pci,0,sizeof(pci));memset(critical,0,sizeof(critical));
    threadIndex=gateCalls=rundownGets=rundownPuts=waits=mutexPuts=0;
    denyRundown=stopAtAcquire=stopAtMutex=0;completeTicket=NULL;
    eventGate=&pci.Owner.Gate;fakeWaitStatus=STATUS_SUCCESS;
    Bc250AdmissionMockAllowed=TRUE;Bc250MockGateExecutionAllowed=TRUE;irql=0;
    pci.Ops.Context=&pci;pci.Ops.Acquire=PciAcquire;pci.Ops.StillStarted=PciStill;
    pci.Ops.Query=PciQuery;pci.Ops.Release=PciRelease;
    CHECK(Bc250PciOwnerInit(&pci.Owner,&pci.Ops,11,19)==0);
}
static VOID ValidateFinished(NTSTATUS status,const BC250_PCI_OBSERVATION *out)
{
    CHECK(!critical[0] && !critical[1] && !pci.Owner.Gate.Rundown.Count && !pci.Owner.Gate.Mutex.Held);
    CHECK(rundownGets==rundownPuts);
    if(status==0)CHECK(out->Valid==1 && out->Generation==11 && out->Epoch==19);
    else CHECK(Clear(out,sizeof(*out)));
    if(pci.Owner.Faulted){
        unsigned gateCallsBefore=gateCalls;
        CHECK(Bc250PciOwnerRetire(&pci.Owner)==STATUS_INVALID_DEVICE_STATE);
        CHECK(!pci.Owner.Gate.Rundown.Closing); /* Unknown refs forbid rundown wait. */
        CHECK(gateCalls>gateCallsBefore && pci.Owner.Session.State==BC250_PCI_QUARANTINED);
    }else CHECK(!pci.Leases && !pci.Interfaces);
}
int main(VOID)
{
    unsigned reason,event,malformed,reentryMode,shortRead;
    BC250_PCI_OBSERVATION out;
    for(reason=1;reason<=32;reason*=2)
      for(event=0;event<=6;++event)
       for(malformed=0;malformed<=3;++malformed)
        for(reentryMode=0;reentryMode<=1;++reentryMode)
         for(shortRead=0;shortRead<=1;++shortRead){
            NTSTATUS status;
            NewFixture();pci.Reason=reason;pci.Event=event;pci.Malformed=malformed;
            pci.Reentrant=reentryMode;pci.ShortRead=shortRead;
            if(malformed==3)pci.QueryStatus=STATUS_NOT_SUPPORTED;
            status=Bc250PciOwnerObserve(&pci.Owner,&out);
            ValidateFinished(status,&out);
            if(event==0 && malformed==0 && !shortRead)CHECK(status==0 && out.Valid);
            if(status==0)CHECK(!pci.Owner.Reasons && !pci.Owner.Faulted);
            if(pci.Owner.Faulted)CHECK(pci.Owner.Gate.State==BC250_DMA_GATE_STOPPING);
            else{
                CHECK(Bc250PciOwnerStop(&pci.Owner,reason)==0);
                CHECK(Bc250PciOwnerRetire(&pci.Owner)==0);
                CHECK(pci.Owner.Retirement==BC250_PCI_OWNER_RETIRED);
                CHECK(Bc250PciOwnerObserve(&pci.Owner,&out)!=0 && Clear(&out,sizeof(out)));
                CHECK(Bc250PciOwnerInit(&pci.Owner,&pci.Ops,12,20)!=0);
            }
         }
    for(reason=1;reason<=32;reason*=2){
        NewFixture();CHECK(Bc250PciOwnerStop(&pci.Owner,reason)==0);
        CHECK(Bc250PciOwnerObserve(&pci.Owner,&out)!=0 && Clear(&out,sizeof(out)));
        CHECK(!pci.AcquireCalls && !pci.QueryCalls && !pci.ReadCalls);
        CHECK(Bc250PciOwnerRetire(&pci.Owner)==0);
    }
    NewFixture();pci.EpochMismatch=1;
    CHECK(Bc250PciOwnerObserve(&pci.Owner,&out)==STATUS_DEVICE_NOT_READY);
    CHECK(pci.Releases==1 && !pci.QueryCalls && Clear(&out,sizeof(out)));
    CHECK(Bc250PciOwnerStop(&pci.Owner,1)==0 && Bc250PciOwnerRetire(&pci.Owner)==0);
    NewFixture();stopAtAcquire=1;
    CHECK(Bc250PciOwnerObserve(&pci.Owner,&out)!=0 && !pci.AcquireCalls);
    CHECK(!critical[0] && !pci.Owner.Gate.Rundown.Count);
    CHECK(Bc250PciOwnerRetire(&pci.Owner)==0);
    NewFixture();stopAtMutex=1;
    CHECK(Bc250PciOwnerObserve(&pci.Owner,&out)!=0 && !pci.AcquireCalls);
    CHECK(Bc250PciOwnerRetire(&pci.Owner)==0);
    NewFixture();pci.AcquireStatus=STATUS_PENDING;
    CHECK(Bc250PciOwnerObserve(&pci.Owner,&out)==STATUS_DATA_ERROR);
    CHECK(pci.Owner.Faulted && Bc250PciOwnerRetire(&pci.Owner)!=0);
    NewFixture();pci.QueryStatus=STATUS_PENDING;
    CHECK(Bc250PciOwnerObserve(&pci.Owner,&out)==STATUS_DATA_ERROR);
    CHECK(pci.Owner.Faulted && pci.Leases==1 && Bc250PciOwnerRetire(&pci.Owner)!=0);
    NewFixture();CHECK(Bc250PciOwnerRetire(&pci.Owner)==STATUS_INVALID_DEVICE_STATE);
    CHECK(Bc250PciOwnerStop(&pci.Owner,0)==STATUS_INVALID_PARAMETER);
    CHECK(Bc250PciOwnerStop(&pci.Owner,3)==STATUS_INVALID_PARAMETER);
    CHECK(Bc250PciOwnerStop(&pci.Owner,64)==STATUS_INVALID_PARAMETER);
    irql=1;CHECK(Bc250PciOwnerObserve(&pci.Owner,&out)==STATUS_INVALID_DEVICE_STATE);
    CHECK(Clear(&out,sizeof(out)));irql=0;
    /* Every API production policy path refuses before fake platform calls. */
    Bc250AdmissionMockAllowed=FALSE;
    {unsigned gateCallsBefore=gateCalls;
     CHECK(Bc250PciOwnerInit(&pci.Owner,&pci.Ops,1,1)==STATUS_NOT_SUPPORTED);
     CHECK(Bc250PciOwnerObserve(&pci.Owner,&out)==STATUS_NOT_SUPPORTED);
     CHECK(Bc250PciOwnerStop(&pci.Owner,1)==STATUS_NOT_SUPPORTED);
     CHECK(Bc250PciOwnerRetire(&pci.Owner)==STATUS_NOT_SUPPORTED);
     CHECK(gateCalls==gateCallsBefore && Clear(&out,sizeof(out)));}
    printf("PASS: %u admission checks; RAM events only, no PnP/power/hardware proof.\n",checks);
    return 0;
}
