/* SPDX-License-Identifier: Apache-2.0 */
#define BC250_BUS_QUERY_MOCK 1
#define BC250_BOUND_READ_MOCK 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../pci-bounded-reader-20261004/bc250_pci_bounded_reader.c"
#include "bc250_pci_bus_query.c"
BOOLEAN Bc250BusQueryMockAllowed=TRUE,Bc250BoundReadMockAllowed=TRUE;
static unsigned checks,fixtures,queries,reads,drops,unknowns;
static ULONG mode,held,bootstrap,guardCalls,denyAt,allocFail,pending,irpGone;
static UCHAR irql;static BC250_BUS_QUERY q;static IRP request;static DEVICE_OBJECT lower;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %u: %s\n",(unsigned)__LINE__,#x);exit(1);}}while(0)
UCHAR KeGetCurrentIrql(VOID){return irql;}
VOID KeInitializeEvent(PKEVENT e,EVENT_TYPE kind,BOOLEAN signal)
{CHECK(e==&q.Event && kind==NotificationEvent && !signal && bootstrap);e->Signal=0;}
static VOID Ref(PVOID p){(void)p;CHECK(FALSE);}
static VOID Drop(PVOID p)
{CHECK(p==&q && bootstrap && held==1 && q.InterfaceHeld==1);--held;++drops;if(mode==8)denyAt=guardCalls+1;}
static ULONG Read(PVOID p,ULONG space,PVOID buffer,ULONG offset,ULONG length)
{
    UCHAR *b=buffer;CHECK(p==&q && bootstrap && held==1 && !offset && length==64 && space==PCI_WHICHSPACE_CONFIG);++reads;
    memset(buffer,0,64);b[0]=2;b[1]=0x10;b[2]=0xfe;b[3]=0x13;b[11]=3;
    if(mode==7)denyAt=guardCalls+1;return mode==6?63:64;
}
static VOID Complete(VOID)
{
    BUS_INTERFACE_STANDARD *b=request.Stack.Parameters.QueryInterface.Interface;
    CHECK(bootstrap && !irpGone && b==&q.Bus && request.IoStatus.Status==STATUS_NOT_SUPPORTED);
    q.Final.Status=mode==1?STATUS_NOT_SUPPORTED:mode==2?STATUS_PENDING:STATUS_SUCCESS;
    if(mode!=1 && mode!=2){
        b->Size=sizeof(*b);b->Version=1;b->Context=&q;b->InterfaceReference=Ref;b->InterfaceDereference=Drop;b->GetBusData=Read;held=1;
        if(mode==3)b->Size=0;if(mode==4)b->GetBusData=NULL;if(mode==5)b->InterfaceDereference=NULL;
    }
    if(mode==9){q.Final.Status=STATUS_NOT_SUPPORTED;b->Context=&q;}
    q.Event.Signal=1;memset(&request,0xdd,sizeof(request));irpGone=1; /* fake OS owns/frees IRP */
}
PIRP IoBuildSynchronousFsdRequest(ULONG major,PDEVICE_OBJECT target,PVOID buf,ULONG len,PLARGE_INTEGER pos,PKEVENT event,PIO_STATUS_BLOCK status)
{
    CHECK(bootstrap && target==&lower && major==IRP_MJ_PNP && !buf && !len && !pos && event==&q.Event && status==&q.Final);
    if(allocFail)return NULL;
    memset(&request,0,sizeof(request));request.Stack.MajorFunction=(UCHAR)major;request.Event=event;request.Final=status;return &request;
}
PIO_STACK_LOCATION IoGetNextIrpStackLocation(PIRP irp){CHECK(irp==&request && !irpGone);return &irp->Stack;}
NTSTATUS IoCallDriver(PDEVICE_OBJECT target,PIRP irp)
{
    IO_STACK_LOCATION *s=&irp->Stack;CHECK(bootstrap && target==&lower && irp==&request && !irpGone && q.Sent==1);++queries;
    CHECK(s->MajorFunction==IRP_MJ_PNP && s->MinorFunction==IRP_MN_QUERY_INTERFACE);
    CHECK(s->Parameters.QueryInterface.InterfaceType==&GUID_BUS_INTERFACE_STANDARD && s->Parameters.QueryInterface.Size==sizeof(q.Bus) &&
        s->Parameters.QueryInterface.Version==1 && s->Parameters.QueryInterface.Interface==&q.Bus && !s->Parameters.QueryInterface.InterfaceSpecificData);
    if(!pending)Complete();
    return pending?STATUS_PENDING:STATUS_CANCELLED; /* dispatch != final status intentionally */
}
NTSTATUS KeWaitForSingleObject(PVOID event,KWAIT_REASON reason,KPROCESSOR_MODE waitMode,BOOLEAN alert,PLARGE_INTEGER timeout)
{
    CHECK(event==&q.Event && reason==Executive && waitMode==KernelMode && !alert && !timeout && bootstrap);
    if(mode==10)return (NTSTATUS)0x102; /* violated no-timeout wait contract: retains outstanding roots */
    if(!q.Event.Signal)Complete();CHECK(q.Event.Signal && irpGone);return STATUS_SUCCESS;
}
static NTSTATUS Guard(PVOID context)
{CHECK(context==&bootstrap && bootstrap);++guardCalls;return denyAt==guardCalls?STATUS_CANCELLED:STATUS_SUCCESS;}
static BOOLEAN Zero(const VOID *p,SIZE_T n){const UCHAR *b=p;SIZE_T i;for(i=0;i<n;++i)if(b[i])return FALSE;return TRUE;}
static VOID Init(ULONG m,ULONG async)
{memset(&q,0,sizeof(q));mode=m;pending=async;held=guardCalls=denyAt=allocFail=irpGone=0;bootstrap=1;irql=0;++fixtures;}
int main(VOID)
{
    ULONG m,async,prior;NTSTATUS st;BC250_BOUND_RESULT out;UCHAR saved[sizeof(q)];
    for(async=0;async<=1;++async)for(m=0;m<=10;++m){
        Init(m,async);if(m==10)pending=1;prior=drops;memset(&out,0xcc,sizeof(out));
        st=Bc250BusQueryObserve(&q,&lower,Guard,Guard,&bootstrap,&out);
        CHECK((m==0 && st==STATUS_SUCCESS && out.Valid && out.BytesReturned==64) ||
              (m!=0 && st!=STATUS_SUCCESS && Zero(&out,sizeof(out))));
        CHECK(!out.HardwareAuthorized && !out.DmaAuthorized);
        if(m==2 || m==5 || m==9 || m==10){CHECK(q.State==BC250_BUS_QUERY_UNKNOWN && bootstrap);++unknowns;
            if(m==5)CHECK(held==1 && q.InterfaceHeld);if(m==10)CHECK(q.Sent && !irpGone);
            memcpy(saved,&q,sizeof(q));prior=queries;
            CHECK(Bc250BusQueryObserve(&q,&lower,Guard,Guard,&bootstrap,&out)==STATUS_INVALID_DEVICE_STATE);
            CHECK(queries==prior && !memcmp(saved,&q,sizeof(q)) && Zero(&out,sizeof(out)));
            /* fixture abandonment, NOT completion recovery or releasing bootstrap */
        }else{CHECK(q.State==BC250_BUS_QUERY_DONE && !q.Sent && !q.InterfaceHeld && !held && !q.Lower);
            CHECK(drops==prior+(m==0 || m>=3));bootstrap=0;}
    }
    Init(0,0);allocFail=1;prior=queries;
    CHECK(Bc250BusQueryObserve(&q,&lower,Guard,Guard,&bootstrap,&out)==STATUS_INSUFFICIENT_RESOURCES && q.State==BC250_BUS_QUERY_DONE && queries==prior);
    Init(0,0);denyAt=1;CHECK(Bc250BusQueryObserve(&q,&lower,Guard,Guard,&bootstrap,&out)==STATUS_CANCELLED && !q.Sent);
    Init(0,1);denyAt=2;prior=reads;CHECK(Bc250BusQueryObserve(&q,&lower,Guard,Guard,&bootstrap,&out)==STATUS_CANCELLED && reads==prior && !held);
    CHECK(Bc250BusQueryObserve(&q,&lower,Guard,Guard,&bootstrap,&out)==STATUS_INVALID_DEVICE_STATE);
    Init(0,0);q.State=BC250_BUS_QUERY_ACTIVE;CHECK(Bc250BusQueryObserve(&q,&lower,Guard,Guard,&bootstrap,&out)==STATUS_DEVICE_BUSY);
    Init(0,0);irql=2;CHECK(Bc250BusQueryObserve(&q,&lower,Guard,Guard,&bootstrap,&out)==STATUS_INVALID_DEVICE_STATE && !q.State);irql=0;
    CHECK(Bc250BusQueryObserve(NULL,&lower,Guard,Guard,&bootstrap,&out)==STATUS_INVALID_PARAMETER);
    CHECK(Bc250BusQueryObserve(&q,NULL,Guard,Guard,&bootstrap,&out)==STATUS_INVALID_PARAMETER);
    CHECK(Bc250BusQueryObserve(&q,&lower,NULL,Guard,&bootstrap,&out)==STATUS_INVALID_PARAMETER);
    CHECK(Bc250BusQueryObserve(&q,&lower,Guard,NULL,&bootstrap,&out)==STATUS_INVALID_PARAMETER);
    CHECK(Bc250BusQueryObserve(&q,&lower,Guard,Guard,&bootstrap,NULL)==STATUS_INVALID_PARAMETER);
    CHECK(unknowns==8);
    printf("PASS: bus query %u checks, %u fixtures; queries %u fake reads %u drops %u unknown %u; OS-owned fake IRP, native FALSE, no hardware.\n",checks,fixtures,queries,reads,drops,unknowns);return 0;
}
