/* SPDX-License-Identifier: Apache-2.0 */
#define BC250_BOUND_READ_MOCK 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bc250_pci_bounded_reader.c"
BOOLEAN Bc250BoundReadMockAllowed=TRUE;
static unsigned checks,fixtures,reads,guards,beforeCalls,afterCalls,refCalls;
static ULONG count,mutation;static NTSTATUS beforeStatus,afterStatus;static UCHAR irql;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %u: %s\n",(unsigned)__LINE__,#x);exit(1);}}while(0)
UCHAR KeGetCurrentIrql(VOID){return irql;}
static VOID Ref(PVOID p){(void)p;++refCalls;}
static ULONG Read(PVOID p,ULONG kind,PVOID buffer,ULONG offset,ULONG length)
{
    UCHAR b[64]={0};CHECK(p==&count && kind==PCI_WHICHSPACE_CONFIG && !offset && length==64);++reads;
    if(mutation==6)return count; /* claims full length but writes nothing */
    b[0]=0x02;b[1]=0x10;b[2]=0xfe;b[3]=0x13;b[11]=3;
    if(mutation==1)b[0]=0xff;if(mutation==2)b[2]=0xff;
    if(mutation==3)b[11]=6;if(mutation==4)b[14]=1;if(mutation==5)b[14]=0x80;
    memcpy(buffer,b,count<64?count:64);return count;
}
static NTSTATUS Before(PVOID p){CHECK(p==&mutation);++guards;++beforeCalls;return beforeStatus;}
static NTSTATUS After(PVOID p){CHECK(p==&mutation);++guards;++afterCalls;return afterStatus;}
static BOOLEAN Zero(const VOID *p,size_t n){const UCHAR *b=p;size_t i;for(i=0;i<n;++i)if(b[i])return FALSE;return TRUE;}
static VOID Init(BUS_INTERFACE_STANDARD *b)
{
    memset(b,0,sizeof(*b));b->Size=sizeof(*b);b->Version=1;b->Context=&count;
    b->InterfaceReference=Ref;b->InterfaceDereference=Ref;b->GetBusData=Read;
    count=64;mutation=0;irql=0;beforeStatus=afterStatus=STATUS_SUCCESS;++fixtures;
}
int main(VOID)
{
    BUS_INTERFACE_STANDARD bus;struct {ULONG A;BC250_BOUND_RESULT R;ULONG B;} box;
    ULONG n,prior;NTSTATUS st;unsigned before,after;
    for(n=0;n<=66;++n){
        Init(&bus);count=n;box.A=0xabcdef01;box.B=0x98765432;memset(&box.R,0xcc,sizeof(box.R));
        st=Bc250BoundReadIdentity(&bus,Before,After,&mutation,&box.R);
        CHECK((n==64 && st==STATUS_SUCCESS && box.R.Valid && box.R.BytesReturned==64) ||
              (n!=64 && st==STATUS_DATA_ERROR && Zero(&box.R,sizeof(box.R))));
        CHECK(box.A==0xabcdef01 && box.B==0x98765432 && !box.R.HardwareAuthorized && !box.R.DmaAuthorized);
    }
    Init(&bus);count=~(ULONG)0;CHECK(Bc250BoundReadIdentity(&bus,Before,After,&mutation,&box.R)==STATUS_DATA_ERROR && Zero(&box.R,sizeof(box.R)));
    for(n=1;n<=6;++n){Init(&bus);mutation=n;st=Bc250BoundReadIdentity(&bus,Before,After,&mutation,&box.R);
        CHECK((n==5 && st==STATUS_SUCCESS) || (n!=5 && st==STATUS_DATA_ERROR && Zero(&box.R,sizeof(box.R))));}
    for(n=0;n<6;++n){
        Init(&bus);if(n==0)bus.Size=0;if(n==1)bus.Version=2;if(n==2)bus.InterfaceReference=NULL;
        if(n==3)bus.InterfaceDereference=NULL;if(n==4)bus.GetBusData=NULL;if(n==5)irql=2;
        prior=reads;CHECK(Bc250BoundReadIdentity(&bus,Before,After,&mutation,&box.R)!=STATUS_SUCCESS && reads==prior && Zero(&box.R,sizeof(box.R)));
    }
    for(n=0;n<6;++n){
        Init(&bus);before=beforeCalls;after=afterCalls;prior=reads;
        if(n==0)beforeStatus=STATUS_DEVICE_NOT_READY;if(n==1)beforeStatus=(NTSTATUS)0x103;
        if(n==2)afterStatus=STATUS_DEVICE_NOT_READY;if(n==3)afterStatus=(NTSTATUS)0x103;
        if(n==4)beforeStatus=(NTSTATUS)0xc0000120U; /* CANCELLED */
        if(n==5)afterStatus=(NTSTATUS)0xc0000120U;
        CHECK(Bc250BoundReadIdentity(&bus,Before,After,&mutation,&box.R)!=STATUS_SUCCESS && Zero(&box.R,sizeof(box.R)));
        CHECK(beforeCalls==before+1 && afterCalls==after+(n==2 || n==3 || n==5) && reads==prior+(n==2 || n==3 || n==5));
    }
    Init(&bus);CHECK(Bc250BoundReadIdentity(NULL,Before,After,&mutation,&box.R)==STATUS_INVALID_PARAMETER);
    CHECK(Bc250BoundReadIdentity(&bus,NULL,After,&mutation,&box.R)==STATUS_INVALID_PARAMETER);
    CHECK(Bc250BoundReadIdentity(&bus,Before,NULL,&mutation,&box.R)==STATUS_INVALID_PARAMETER);
    CHECK(Bc250BoundReadIdentity(&bus,Before,After,&mutation,NULL)==STATUS_INVALID_PARAMETER);
    Bc250BoundReadMockAllowed=FALSE;memset(&box.R,0xcc,sizeof(box.R));prior=guards;
    CHECK(Bc250BoundReadIdentity(NULL,NULL,NULL,NULL,&box.R)==STATUS_NOT_SUPPORTED && guards==prior && ((UCHAR *)&box.R)[0]==0xcc);
    CHECK(!refCalls);
    printf("PASS: bounded PCI reader %u checks, %u fixtures, %u fake reads; fixed 64 bytes, zero ownership/hardware.\n",checks,fixtures,reads);return 0;
}
