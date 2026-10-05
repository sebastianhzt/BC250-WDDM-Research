/* SPDX-License-Identifier: Apache-2.0 */
/* Exact Build22 lookup bodies, reduced fake platform, NO kernel integration. */
#define BC250_ENV_MOCK 1
#define BC250_LIFE_MOCK 1
#define BC250_BOUND_READ_MOCK 1
#define BC250_BUS_QUERY_MOCK 1
#define BC250_QUERY_ADMIT_MOCK 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../context-lifetime-20261004/bc250_context_lifetime.c"
#include "../envelope-reference-20261004/bc250_envelope_reference.c"
#include "../pci-bounded-reader-20261004/bc250_pci_bounded_reader.c"
#include "mock-query-composed.h"
#include "../pci-bus-query-20261004/bc250_pci_bus_query.c"
#include "bc250_query_admission.c"
#ifndef _Out_
#define _Out_
#endif
#ifndef _Inout_
#define _Inout_
#endif
#ifndef PASSIVE_LEVEL
#define PASSIVE_LEVEL 0
#endif
#ifndef NT_SUCCESS
#define NT_SUCCESS(s) ((NTSTATUS)(s)>=0)
#endif
#define STATUS_DELETE_PENDING ((NTSTATUS)0xc0000056U)
#include "build22-types.inc"
BOOLEAN Bc250LifeMockAllowed=TRUE,Bc250EnvMockAllowed=TRUE;
BOOLEAN Bc250BoundReadMockAllowed=TRUE;
BOOLEAN Bc250BusQueryMockAllowed=TRUE;
BOOLEAN Bc250QueryAdmitMockAllowed=TRUE;
typedef struct EXT {
    ULONG Signature;
    PDEVICE_OBJECT LowerDeviceObject,PhysicalDeviceObject;
    IO_REMOVE_LOCK RemoveLock;
    volatile LONG State,StateEpoch;
    BOOLEAN HardwareIdValidated;
    BC250_ENV_DOMAIN Env;
    BC250_LIFE_DOMAIN Life;
} DREAM_V3_WDM_PNP_EXTENSION,*PDREAM_V3_WDM_PNP_EXTENSION;
typedef struct WORLD {
    DREAM_V3_WDM_PNP_EXTENSION Ext;
    DEVICE_OBJECT Self,Lower,Pdo;
    struct {ULONG Alive,Value;} Child;
    DREAM_V3_PNP_PDO_REFERENCE Reference;
    BC250_ENV_RECEIPT Receipt;
    BC250_LIFE_TOKEN Call;
    BC250_BUS_QUERY Query;
    BC250_QUERY_ADMISSION Admission;
    BC250_QUERY_TICKET Ticket;
    struct {IRP Irp;IO_STACK_LOCATION Stack;} Packet;
    ULONG Busy,Pending,Mode,InterfaceHeld,IrpGone,Reenter;
    ULONG Action,Point,Removing,ParentGone,ChildGone,RetireChild;
} WORLD;
static WORLD *world;
static ULONG g_DreamV3PnpBindingLock,pub,critical,spin;
static PDREAM_V3_WDM_PNP_EXTENSION g_DreamV3PnpBinding;
static volatile LONG g_DreamV3BindingGeneration;
static KIRQL irql;
static unsigned checks,fixtures,parentDrops,childDrops,historical,unknown,providerCalls,queries,drops;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %u: %s\n",(unsigned)__LINE__,#x);exit(1);}}while(0)
UCHAR KeGetCurrentIrql(VOID){return irql;}
BOOLEAN KeAreApcsDisabled(VOID){return critical?TRUE:FALSE;}
SIZE_T RtlCompareMemory(const VOID *a,const VOID *b,SIZE_T n)
{SIZE_T k;const UCHAR *x=a,*y=b;for(k=0;k<n && x[k]==y[k];++k){}return k;}
VOID KeInitializeSpinLock(KSPIN_LOCK *p){p->Initialized=1;p->Held=0;}
VOID KeAcquireSpinLock(KSPIN_LOCK *p,KIRQL *old)
{CHECK(!spin && !pub && p->Initialized && !p->Held);*old=irql;irql=2;spin=p->Held=1;}
VOID KeReleaseSpinLock(KSPIN_LOCK *p,KIRQL old)
{
    CHECK(spin && p->Held);spin=p->Held=0;irql=old;
    if(world->RetireChild && p==&world->Ext.Life.Lock && !irql &&
       !world->Ext.Life.Calls && !world->Ext.Life.Holds){
        PVOID ptr=NULL;world->RetireChild=0;
        CHECK(Bc250LifeDetach(&world->Ext.Life,&ptr)==STATUS_SUCCESS && ptr==&world->Child);
        CHECK(world->Ext.RemoveLock.Count==2 && world->Self.Fake==2);
        memset(ptr,0xdd,sizeof(world->Child));world->ChildGone=1;++childDrops;
    }
}
VOID KeEnterCriticalRegion(VOID){CHECK(!critical);critical=1;}
VOID KeLeaveCriticalRegion(VOID){CHECK(critical && !pub);critical=0;}
VOID ExAcquirePushLockShared(ULONG *p){CHECK(p==&g_DreamV3PnpBindingLock && critical && !pub && !spin);pub=1;}
VOID ExReleasePushLockShared(ULONG *p){CHECK(p==&g_DreamV3PnpBindingLock && pub);pub=0;}
LONG InterlockedCompareExchange(volatile LONG *p,LONG value,LONG compare)
{LONG old=*p;if(old==compare)*p=value;return old;}
static VOID Hook(ULONG point)
{
    WORLD *w=world;CHECK(!spin && !w->ParentGone);
    if(w->Point!=point || !w->Action)return;
    w->Point=0;
    CHECK(Bc250QueryAdmitClose(&w->Admission,w->Action==1?BC250_QA_STOP:BC250_QA_REMOVE)==STATUS_SUCCESS);
    w->Ext.State=w->Action==1?AMDBC250_PNP_STATE_STOPPED:AMDBC250_PNP_STATE_DELETED;
    ++w->Ext.StateEpoch;
    if(w->Action==2){CHECK(!pub);w->Removing=1;g_DreamV3PnpBinding=NULL;++g_DreamV3BindingGeneration;}
    /* Proposed future closure participation, NOT actual Build22 dispatch. */
    if(!pub){CHECK(Bc250EnvClose(&w->Ext.Env)==STATUS_SUCCESS);
        CHECK(Bc250LifeClose(&w->Ext.Life)==STATUS_SUCCESS);w->RetireChild=1;}
}
VOID ObReferenceObject(PVOID p)
{
    DEVICE_OBJECT *o=p;CHECK(!spin && !world->ParentGone);
    CHECK(o==&world->Self || o==&world->Lower || o==&world->Pdo);++o->Fake;
    if(o==&world->Self)Hook(2);
}
VOID ObDereferenceObject(PVOID p)
{
    DEVICE_OBJECT *o=p;CHECK(!spin && !world->ParentGone && o->Fake>=2);
    CHECK(o==&world->Self || o==&world->Lower || o==&world->Pdo);--o->Fake;
}
NTSTATUS IoAcquireRemoveLock(PIO_REMOVE_LOCK p,PVOID tag)
{
    ULONG i;CHECK(!spin && !world->ParentGone && p==&world->Ext.RemoveLock);
    ++p->Attempts;if(p->Attempts==1 && world->Point==7)Hook(7);
    if(world->Removing || p->RejectAt==p->Attempts)return STATUS_DELETE_PENDING;
    for(i=0;i<8 && p->Tags[i];++i){}CHECK(i<8);p->Tags[i]=tag;++p->Count;return STATUS_SUCCESS;
}
VOID IoReleaseRemoveLock(PIO_REMOVE_LOCK p,PVOID tag)
{
    ULONG i;CHECK(!spin && !world->ParentGone && p==&world->Ext.RemoveLock);
    for(i=0;i<8 && p->Tags[i]!=tag;++i){}CHECK(i<8 && p->Count);p->Tags[i]=NULL;--p->Count;
    if(world->Removing && !p->Count){
        CHECK(world->Self.Fake==1 && world->Lower.Fake==1 && world->Pdo.Fake==1);
        CHECK(!world->Ext.Life.Calls && !world->Ext.Life.Holds);
        /* Fake REMOVE deletes storage inside LAST lookup tag release. */
        memset(&world->Ext,0xdd,sizeof(world->Ext));world->ParentGone=1;++parentDrops;
    }
}
#include "build22-lookup.inc"
static VOID InterfaceRef(PVOID p){(void)p;CHECK(FALSE);}
static NTSTATUS ReaderCheck(PVOID p)
{
    WORLD *w=world;CHECK(p==&w->Busy && !w->ParentGone && w->Ext.Life.Calls==1 && w->Ext.Life.Holds==1);
    CHECK(!pub && !spin && !critical && w->Ext.RemoveLock.Count==2);
    if(Bc250QueryAdmitCheck(&w->Admission,&w->Ticket)!=STATUS_SUCCESS)return STATUS_DEVICE_NOT_READY;
    return g_DreamV3PnpBinding==&w->Ext && w->Ext.State==AMDBC250_PNP_STATE_STARTED &&
        (ULONG)g_DreamV3BindingGeneration==w->Reference.BindingGeneration &&
        (ULONG)w->Ext.StateEpoch==w->Reference.StateEpoch?STATUS_SUCCESS:STATUS_DEVICE_NOT_READY;
}
static ULONG GetData(PVOID p,ULONG kind,PVOID buffer,ULONG offset,ULONG length)
{
    UCHAR *b=buffer;WORLD *w=p;
    CHECK(w==world && kind==PCI_WHICHSPACE_CONFIG && !offset && length==64 && !spin && !pub);
    CHECK(w->Ext.Life.Calls==1 && w->Ext.Life.Holds==1 && w->Ext.RemoveLock.Count==2);
    CHECK(w->Admission.Address==&w->Ticket && !w->Admission.Quarantined);
    ++providerCalls;Hook(8);CHECK(!w->ChildGone && !w->ParentGone);
    memset(buffer,0,64);b[0]=0x02;b[1]=0x10;b[2]=0xfe;b[3]=0x13;b[11]=3;b[8]=(UCHAR)w->Child.Value;
    return length;
}
static VOID InterfaceDrop(PVOID p)
{
    WORLD *w=p;CHECK(w==world && !pub && !spin && !critical && w->InterfaceHeld==1);
    CHECK(w->Ext.Life.Calls==1 && w->Ext.Life.Holds==1 && w->Ext.RemoveLock.Count==2);
    Hook(14);w->InterfaceHeld=0;++drops;
}
VOID KeInitializeEvent(PKEVENT event,EVENT_TYPE kind,BOOLEAN signal)
{CHECK(event==&world->Query.Event && kind==NotificationEvent && !signal && !pub && !spin && !critical);event->Signal=0;}
PIRP IoBuildSynchronousFsdRequest(ULONG major,PDEVICE_OBJECT lower,PVOID buffer,ULONG length,PLARGE_INTEGER offset,PKEVENT event,PIO_STATUS_BLOCK status)
{
    WORLD *w=world;CHECK(!pub && !spin && !critical && lower==&w->Lower && w->Lower.Fake==2);
    CHECK(major==IRP_MJ_PNP && !buffer && !length && !offset && event==&w->Query.Event && status==&w->Query.Final);
    Hook(10);if(w->Mode==1)return NULL;
    memset(&w->Packet,0,sizeof(w->Packet));w->Packet.Stack.MajorFunction=(UCHAR)major;return &w->Packet.Irp;
}
PIO_STACK_LOCATION IoGetNextIrpStackLocation(PIRP irp)
{CHECK(irp==&world->Packet.Irp && !world->IrpGone);return &world->Packet.Stack;}
static VOID CompleteQuery(VOID)
{
    WORLD *w=world;BUS_INTERFACE_STANDARD *bus=w->Packet.Stack.Parameters.QueryInterface.Interface;
    CHECK(!w->IrpGone && bus==&w->Query.Bus && w->Ext.Life.Calls==1 && w->Ext.Life.Holds==1);
    CHECK(w->Ext.RemoveLock.Count==2 && w->Lower.Fake==2 && w->Pdo.Fake==2 && w->Self.Fake==2);
    w->Query.Final.Status=w->Mode==2?STATUS_NOT_SUPPORTED:w->Mode==3?STATUS_PENDING:STATUS_SUCCESS;
    if(w->Mode!=2 && w->Mode!=3){
        bus->Size=sizeof(*bus);bus->Version=1;bus->Context=w;bus->InterfaceReference=InterfaceRef;
        bus->InterfaceDereference=InterfaceDrop;bus->GetBusData=GetData;w->InterfaceHeld=1;
        if(w->Mode==4)bus->InterfaceDereference=NULL;
        if(w->Mode==5){w->Query.Final.Status=STATUS_NOT_SUPPORTED;}
        if(w->Mode==7)bus->Size=0;
    }
    w->Query.Event.Signal=1;memset(&w->Packet,0xdd,sizeof(w->Packet));w->IrpGone=1;
}
typedef struct RESULT {ULONG Value,HardwareAuthorized,ProviderCalls,Historical,Retained;} RESULT;
static NTSTATUS Compose(RESULT *,BOOLEAN);
NTSTATUS IoCallDriver(PDEVICE_OBJECT lower,PIRP irp)
{
    WORLD *w=world;IO_STACK_LOCATION *s=&w->Packet.Stack;RESULT result;
    CHECK(!pub && !spin && !critical && lower==&w->Lower && irp==&w->Packet.Irp && !w->IrpGone);++queries;
    CHECK(s->MajorFunction==IRP_MJ_PNP && s->MinorFunction==IRP_MN_QUERY_INTERFACE);
    CHECK(s->Parameters.QueryInterface.InterfaceType==&GUID_BUS_INTERFACE_STANDARD && s->Parameters.QueryInterface.Size==sizeof(w->Query.Bus) &&
        s->Parameters.QueryInterface.Version==1 && !s->Parameters.QueryInterface.InterfaceSpecificData);
    Hook(11);
    if(w->Reenter)CHECK(Compose(&result,FALSE)==STATUS_DEVICE_BUSY && !result.Value && !result.Retained);
    if(!w->Pending)CompleteQuery();return w->Pending?STATUS_PENDING:STATUS_CANCELLED;
}
NTSTATUS KeWaitForSingleObject(PVOID event,KWAIT_REASON reason,KPROCESSOR_MODE mode,BOOLEAN alert,PLARGE_INTEGER timeout)
{
    WORLD *w=world;CHECK(event==&w->Query.Event && reason==Executive && mode==KernelMode && !alert && !timeout);
    CHECK(!pub && !spin && !critical && w->Ext.Life.Calls==1 && w->Ext.Life.Holds==1);Hook(12);
    if(w->Mode==6)return (NTSTATUS)0x102;
    if(!w->Query.Event.Signal)CompleteQuery();CHECK(w->IrpGone);return STATUS_SUCCESS;
}
static NTSTATUS Compose(RESULT *out,BOOLEAN tamper)
{
    WORLD *w=world;PDREAM_V3_WDM_PNP_EXTENSION e;NTSTATUS st,cleanup;
    PVOID child=NULL;
    BC250_BOUND_RESULT read={0};
    memset(out,0,sizeof(*out));
    if(w->Query.State==BC250_BUS_QUERY_UNKNOWN)return STATUS_INVALID_DEVICE_STATE;
    st=Bc250QueryAdmitEnter(&w->Admission,&w->Ticket);
    if(st!=STATUS_SUCCESS)return st; /* reject BEFORE zeroing live lookup */
    w->Busy=1;st=DreamV3AcquireStartedPnpPdo(&w->Reference);
    if(st!=STATUS_SUCCESS){CHECK(Bc250QueryAdmitLeave(&w->Admission,&w->Ticket)==STATUS_SUCCESS);w->Busy=0;return st;}
    e=w->Reference.BindingContext;Hook(1);
    CHECK(w->Admission.Generation==w->Reference.BindingGeneration && w->Admission.Epoch==w->Reference.StateEpoch);
    st=Bc250EnvAcquire(&e->Env,&w->Receipt);
    if(st==STATUS_SUCCESS){
        Hook(3);
        KeEnterCriticalRegion();ExAcquirePushLockShared(&g_DreamV3PnpBindingLock);
        if(g_DreamV3PnpBinding!=e || e->State!=AMDBC250_PNP_STATE_STARTED ||
           (ULONG)g_DreamV3BindingGeneration!=w->Reference.BindingGeneration ||
           (ULONG)e->StateEpoch!=w->Reference.StateEpoch)st=STATUS_DEVICE_NOT_READY;
        ExReleasePushLockShared(&g_DreamV3PnpBindingLock);KeLeaveCriticalRegion();
        /* Instantaneous metadata check is NOT hardware admission. */
        if(st==STATUS_SUCCESS){
            st=Bc250LifeEnterHeld(&e->Life,&w->Receipt.Hold,&w->Call,&child);
            if(st==STATUS_SUCCESS){
                Hook(4);CHECK(child==&w->Child && w->Child.Alive==1);
                st=Bc250BusQueryObserve(&w->Query,w->Reference.LowerDeviceObject,ReaderCheck,ReaderCheck,&w->Busy,&read);
                if(w->Query.State==BC250_BUS_QUERY_UNKNOWN){
                    CHECK(!read.Valid && !read.BytesReturned);out->Retained=1;++unknown;
                    CHECK(Bc250QueryAdmitQuarantine(&w->Admission,&w->Ticket)==STATUS_SUCCESS);
                    /* Session/Call/lookup/Env live beyond this return. No guesses. */
                    return st;
                }
                if(st==STATUS_SUCCESS){out->Value=read.Header[8];out->Historical=1;++historical;}
                else CHECK(!read.Valid && !read.BytesReturned);
                CHECK(Bc250LifeExit(&e->Life,&w->Call)==STATUS_SUCCESS);
            }
        }
        if(tamper)++w->Receipt.Id;
        cleanup=Bc250EnvRelease(&e->Env,&w->Receipt);
        if(cleanup!=STATUS_SUCCESS){out->Retained=1;st=cleanup;++unknown;}
    }
    if(out->Retained){CHECK(Bc250QueryAdmitQuarantine(&w->Admission,&w->Ticket)==STATUS_SUCCESS);}
    else CHECK(Bc250QueryAdmitLeave(&w->Admission,&w->Ticket)==STATUS_SUCCESS);
    if(st!=STATUS_SUCCESS){out->Value=out->Historical=0;}
    /* No extension or adapter read after the actual last-tag release. */
    w->Busy=0;DreamV3ReleasePnpPdo(&w->Reference);return st;
}
static VOID Init(WORLD *w)
{
    memset(w,0,sizeof(*w));world=w;irql=0;CHECK(!pub && !spin && !critical);
    w->Self.Fake=w->Lower.Fake=w->Pdo.Fake=1;w->Child.Alive=1;w->Child.Value=23;
    w->Ext.Signature=DREAM_V3_PNP_EXTENSION_SIGNATURE;
    w->Ext.LowerDeviceObject=&w->Lower;w->Ext.PhysicalDeviceObject=&w->Pdo;
    w->Ext.HardwareIdValidated=TRUE;w->Ext.State=AMDBC250_PNP_STATE_STARTED;w->Ext.StateEpoch=1;
    g_DreamV3PnpBinding=&w->Ext;g_DreamV3BindingGeneration=1;
    CHECK(Bc250LifeInit(&w->Ext.Life,100+fixtures)==STATUS_SUCCESS);
    CHECK(Bc250LifePublish(&w->Ext.Life,1,1,&w->Child)==STATUS_SUCCESS);
    CHECK(Bc250EnvInit(&w->Ext.Env,200+fixtures,&w->Self,&w->Ext.RemoveLock,&w->Ext.Life,1)==STATUS_SUCCESS);++fixtures;
    CHECK(Bc250QueryAdmitInit(&w->Admission,300+fixtures,1,1)==STATUS_SUCCESS);
}
static VOID Empty(WORLD *w)
{
    CHECK(!pub && !spin && !critical && w->Self.Fake==1 && w->Lower.Fake==1 && w->Pdo.Fake==1);
    CHECK(w->Reference.BindingContext==NULL && w->Reference.LowerDeviceObject==NULL && w->Reference.PhysicalDeviceObject==NULL);
    CHECK(!w->Admission.Address && !w->Ticket.Domain && !w->Admission.Quarantined);
    if(!w->ParentGone)CHECK(!w->Ext.RemoveLock.Count && !w->Ext.Life.Holds && !w->Ext.Life.Calls);
    world=NULL;
}
int main(VOID)
{
    ULONG action,index,n,async;WORLD w;RESULT r;NTSTATUS st;
    const ULONG points[]={1,2,3,4,10,11,12,8,14};
    for(async=0;async<=1;++async){
        Init(&w);w.Pending=async;w.Reenter=1;
        CHECK(Compose(&r,FALSE)==STATUS_SUCCESS && r.Value==23 && r.Historical);Empty(&w);
        for(action=1;action<=2;++action)for(index=0;index<sizeof(points)/sizeof(points[0]);++index){
            Init(&w);w.Pending=async;w.Action=action;w.Point=points[index];st=Compose(&r,FALSE);
            CHECK(st!=STATUS_SUCCESS && !r.Value && !r.Historical);
            CHECK(!r.HardwareAuthorized && !r.ProviderCalls && !r.Retained);Empty(&w);
        }
        for(n=1;n<=7;++n){
            Init(&w);w.Pending=async;w.Mode=n;if(n==6)w.Pending=1;
            st=Compose(&r,FALSE);CHECK(st!=STATUS_SUCCESS && !r.Value);
            if(n>=3 && n<=6){
                BC250_BUS_QUERY saved=w.Query;ULONG before=queries;
                CHECK(r.Retained && w.Query.State==BC250_BUS_QUERY_UNKNOWN);
                CHECK(w.Admission.Address==&w.Ticket && w.Admission.Quarantined && w.Ticket.Domain==&w.Admission);
                CHECK(w.Self.Fake==2 && w.Lower.Fake==2 && w.Pdo.Fake==2 && w.Ext.RemoveLock.Count==2);
                CHECK(w.Ext.Life.Holds==1 && w.Ext.Life.Calls==1 && w.Reference.BindingContext==&w.Ext);
                if(n==6)CHECK(w.Query.Sent && !w.IrpGone);
                CHECK(Compose(&r,FALSE)==STATUS_INVALID_DEVICE_STATE && queries==before && !memcmp(&saved,&w.Query,sizeof(saved)));
                world=NULL; /* anchored abandoned fixture, NOT production recovery */
            }else{CHECK(!r.Retained && !w.InterfaceHeld);Empty(&w);}
        }
    }
    for(action=1;action<=2;++action)for(n=4;n<=6;n+=2){
        Init(&w);w.Action=action;w.Point=12;w.Pending=1;w.Mode=n;
        CHECK(Compose(&r,FALSE)==STATUS_DATA_ERROR && r.Retained && !r.Value);
        CHECK(w.Self.Fake==2 && w.Lower.Fake==2 && w.Pdo.Fake==2 && w.Ext.RemoveLock.Count==2);
        CHECK(w.Ext.Life.Calls==1 && w.Ext.Life.Holds==1 && !w.ParentGone && !w.ChildGone);
        CHECK(w.Query.State==BC250_BUS_QUERY_UNKNOWN && (n!=6 || w.Query.Sent));
        CHECK(w.Admission.Quarantined && w.Admission.Address==&w.Ticket);
        world=NULL; /* uncertain query survives closure under fixture anchor */
    }
    for(n=1;n<=2;++n){Init(&w);w.Ext.RemoveLock.RejectAt=n;CHECK(Compose(&r,FALSE)==STATUS_DELETE_PENDING);Empty(&w);}
    for(n=0;n<5;++n){
        Init(&w);if(n==0)w.Ext.HardwareIdValidated=FALSE;
        if(n==1)w.Ext.Signature=0;if(n==2)w.Ext.LowerDeviceObject=NULL;
        if(n==3)g_DreamV3PnpBinding=NULL;if(n==4)irql=2;
        CHECK(Compose(&r,FALSE)!=STATUS_SUCCESS && !r.Value);irql=0;Empty(&w);
    }
    Init(&w);CHECK(Compose(&r,TRUE)==STATUS_INVALID_PARAMETER && r.Retained);
    CHECK(!r.Value && !r.Historical && w.Admission.Quarantined && w.Admission.Address==&w.Ticket);
    CHECK(w.Ext.RemoveLock.Count==1 && w.Self.Fake==2 && w.Ext.Life.Holds==1);
    CHECK(w.Lower.Fake==1 && w.Pdo.Fake==1 && !w.Reference.BindingContext);
    /* Unknown receipt quarantined; fixture end is NOT recovery/free. */
    world=NULL;CHECK(unknown==13 && historical==3 && providerCalls==11);
    printf("PASS: query admission composition %u checks, %u fixtures; parent drops %u child drops %u historical-captures %u unknown %u provider %u queries %u interface drops %u; serial RAM, no real admission/hardware.\n",checks,fixtures,parentDrops,childDrops,historical,unknown,providerCalls,queries,drops);
    return 0;
}
