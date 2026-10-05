/* SPDX-License-Identifier: Apache-2.0 */
#define BC250_PERMIT_MOCK 1
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "bc250_composite_permit.c"
BOOLEAN Bc250PermitMockAllowed=TRUE;
static unsigned checks,held;static KIRQL irql;static ULONG thread=1;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL line %u: %s\n",(unsigned)__LINE__,#x);exit(1);}}while(0)
unsigned char KeGetCurrentIrql(VOID){return irql;}
PVOID KeGetCurrentThread(VOID){CHECK(!held);return (PVOID)(ULONG_PTR)thread;}
VOID KeInitializeSpinLock(KSPIN_LOCK *l){l->Initialized=1;l->Held=0;}
VOID KeAcquireSpinLock(KSPIN_LOCK *l,KIRQL *old)
{CHECK(l->Initialized && !l->Held && !held && irql<=2);*old=irql;l->Held=held=1;irql=2;}
VOID KeReleaseSpinLock(KSPIN_LOCK *l,KIRQL old)
{CHECK(l->Held && held && irql==2);l->Held=held=0;irql=old;}
static VOID Init(BC250_PERMIT *r,ULONGLONG scope)
{RtlZeroMemory(r,sizeof(*r));irql=0;thread=1;CHECK(Bc250PermitInit(r,scope,1)==STATUS_SUCCESS);}
static VOID Publish(BC250_PERMIT *r,ULONGLONG revision)
{
    BC250_PERMIT_WRITER w={0};irql=2;CHECK(Bc250PermitWriteAdmit(r,1,&w)==STATUS_SUCCESS);irql=0;
    CHECK(Bc250PermitWriteClaim(r,&w)==STATUS_SUCCESS);
    CHECK(Bc250PermitWriteFinish(r,&w,STATUS_SUCCESS,revision)==STATUS_SUCCESS && PermitZero(&w,sizeof(w)));
}
static VOID Cycles(VOID)
{
    BC250_PERMIT r;ULONG n,k;Init(&r,1);Publish(&r,1);
    for(n=0;n<2000;++n){
        BC250_PERMIT_READER reads[8]={{0}},extra={0};BC250_PERMIT_WRITER w={0};ULONGLONG revision;
        for(k=0;k<8;++k){
            thread=k+1;CHECK(Bc250PermitAcquire(&r,&reads[k])==STATUS_SUCCESS);
            irql=(KIRQL)(k%3);CHECK(Bc250PermitStepEnter(&r,&reads[k],&revision)==STATUS_SUCCESS && revision==n+1U && !held);
            CHECK(Bc250PermitRelease(&r,&reads[k])==STATUS_DEVICE_BUSY);irql=0;
        }
        CHECK(r.Pins==8 && r.Steps==8);thread=100;
        CHECK(Bc250PermitAcquire(&r,&extra)==STATUS_DEVICE_BUSY && PermitZero(&extra,sizeof(extra)));
        irql=2;CHECK(Bc250PermitWriteAdmit(&r,2+n%5,&w)==STATUS_SUCCESS && !r.Published);
        CHECK(Bc250PermitWriteClaim(&r,&w)==STATUS_INVALID_DEVICE_STATE);irql=0;
        CHECK(Bc250PermitWriteClaim(&r,&w)==STATUS_DEVICE_BUSY && r.Revision==n+1U);
        CHECK(Bc250PermitAcquire(&r,&extra)==STATUS_DEVICE_NOT_READY && PermitZero(&extra,sizeof(extra)));
        for(k=0;k<8;++k){
            thread=k+1;irql=2;CHECK(Bc250PermitStepLeave(&r,&reads[k])==STATUS_SUCCESS);
            revision=~(ULONGLONG)0;CHECK(Bc250PermitStepEnter(&r,&reads[k],&revision)==STATUS_DEVICE_NOT_READY && !revision);
            irql=0;CHECK(Bc250PermitWriteClaim(&r,&w)==STATUS_DEVICE_BUSY); /* pin still held */
            irql=2;CHECK(Bc250PermitRelease(&r,&reads[k])==STATUS_SUCCESS && PermitZero(&reads[k],sizeof(reads[k])));irql=0;
            if(k<7)CHECK(Bc250PermitWriteClaim(&r,&w)==STATUS_DEVICE_BUSY);
        }
        CHECK(!r.Pins && !r.Steps);CHECK(Bc250PermitWriteClaim(&r,&w)==STATUS_SUCCESS);
        CHECK(Bc250PermitWriteClaim(&r,&w)==STATUS_DEVICE_BUSY);
        CHECK(Bc250PermitWriteAbandon(&r,&w)==STATUS_DEVICE_BUSY);
        CHECK(Bc250PermitWriteFinish(&r,&w,STATUS_PENDING,0)==STATUS_DEVICE_BUSY && r.WriterAddress==&w);
        CHECK(Bc250PermitAcquire(&r,&extra)==STATUS_DEVICE_NOT_READY);
        CHECK(Bc250PermitWriteFinish(&r,&w,STATUS_SUCCESS,n+2U)==STATUS_SUCCESS);
        CHECK(r.Published && r.Revision==n+2U && !r.WriterAddress && PermitZero(&w,sizeof(w)));
    }
    CHECK(Bc250PermitClose(&r)==STATUS_SUCCESS);
}
static VOID CloseAndOverlap(VOID)
{
    ULONG mode;for(mode=0;mode<5;++mode){
        BC250_PERMIT r;BC250_PERMIT_READER a={0},newRead={0};BC250_PERMIT_WRITER w={0},second={0};ULONGLONG v;
        Init(&r,100+mode);Publish(&r,1);
        if(mode!=3){CHECK(Bc250PermitAcquire(&r,&a)==STATUS_SUCCESS);
            if(mode!=0)CHECK(Bc250PermitStepEnter(&r,&a,&v)==STATUS_SUCCESS);}
        if(mode>=2){irql=2;CHECK(Bc250PermitWriteAdmit(&r,6,&w)==STATUS_SUCCESS);irql=0;}
        if(mode==3)CHECK(Bc250PermitWriteClaim(&r,&w)==STATUS_SUCCESS);
        irql=2;
        if(mode==4)CHECK(Bc250PermitWriteAdmit(&r,3,&second)==STATUS_DEVICE_BUSY && r.Fault && PermitZero(&second,sizeof(second)));
        CHECK(Bc250PermitClose(&r)==STATUS_SUCCESS);irql=0;
        CHECK(!r.Published && r.Closing && r.Revision==1);
        CHECK(Bc250PermitAcquire(&r,&newRead)==STATUS_DEVICE_NOT_READY);
        if(mode!=3){
            CHECK(Bc250PermitStepEnter(&r,&a,&v)==(mode?STATUS_DEVICE_BUSY:STATUS_DEVICE_NOT_READY) && !v);
            if(mode)CHECK(Bc250PermitStepLeave(&r,&a)==STATUS_SUCCESS);
            CHECK(Bc250PermitRelease(&r,&a)==STATUS_SUCCESS && PermitZero(&a,sizeof(a)));
        }
        if(mode>=2){
            CHECK(Bc250PermitWriteClaim(&r,&w)==STATUS_DEVICE_NOT_READY);
            if(mode==3)CHECK(Bc250PermitWriteFinish(&r,&w,STATUS_SUCCESS,2)==STATUS_DEVICE_NOT_READY);
            else CHECK(Bc250PermitWriteAbandon(&r,&w)==STATUS_DEVICE_NOT_READY);
            CHECK(!r.WriterAddress && PermitZero(&w,sizeof(w)));
        }
        CHECK(!r.Pins && !r.Steps && !r.Published && r.Revision==1);
    }
}
static VOID IdentityAndQuarantine(VOID)
{
    BC250_PERMIT r,other;BC250_PERMIT_READER a={0},copy;BC250_PERMIT_WRITER w={0},wc;ULONGLONG v;
    Init(&r,200);Publish(&r,1);Init(&other,201);Publish(&other,1);
    CHECK(Bc250PermitAcquire(&r,&a)==STATUS_SUCCESS);copy=a;
    CHECK(Bc250PermitStepEnter(&r,&copy,&v)==STATUS_INVALID_PARAMETER && !v);
    CHECK(Bc250PermitRelease(&r,&copy)==STATUS_INVALID_PARAMETER);
    CHECK(Bc250PermitRelease(&other,&a)==STATUS_INVALID_PARAMETER);
    thread=2;CHECK(Bc250PermitStepEnter(&r,&a,&v)==STATUS_INVALID_PARAMETER);
    CHECK(Bc250PermitRelease(&r,&a)==STATUS_INVALID_PARAMETER);thread=1;
    CHECK(Bc250PermitStepEnter(&r,&a,&v)==STATUS_SUCCESS);
    CHECK(Bc250PermitStepLeave(&r,&a)==STATUS_SUCCESS);
    irql=2;CHECK(Bc250PermitWriteAdmit(&r,6,&w)==STATUS_SUCCESS);irql=0;wc=w;
    CHECK(Bc250PermitWriteClaim(&r,&wc)==STATUS_INVALID_PARAMETER);
    CHECK(Bc250PermitWriteAbandon(&r,&wc)==STATUS_INVALID_PARAMETER);
    CHECK(Bc250PermitWriteClaim(&other,&w)==STATUS_INVALID_PARAMETER);
    /* Illegal lease refresh is rejected against independently stored seal. */
    a.Epoch=r.Epoch;
    CHECK(Bc250PermitStepEnter(&r,&a,&v)==STATUS_INVALID_PARAMETER && !v);
    CHECK(Bc250PermitRelease(&r,&a)==STATUS_INVALID_PARAMETER && r.Pins==1);
    CHECK(Bc250PermitWriteClaim(&r,&w)==STATUS_DEVICE_BUSY);
    CHECK(Bc250PermitClose(&r)==STATUS_SUCCESS);
    CHECK(Bc250PermitWriteAbandon(&r,&w)==STATUS_DEVICE_NOT_READY);
    CHECK(r.Pins==1 && !r.WriterAddress); /* unrecognized pin intentionally quarantined, never repaired */
    CHECK(Bc250PermitClose(&other)==STATUS_SUCCESS);
}
static VOID FinalStates(VOID)
{
    ULONG n;for(n=0;n<7;++n){
        BC250_PERMIT r;BC250_PERMIT_WRITER w={0};BC250_PERMIT_READER a={0};NTSTATUS final,expected;ULONGLONG revision;
        Init(&r,300+n);Publish(&r,1);CHECK(Bc250PermitWriteAdmit(&r,6,&w)==STATUS_SUCCESS);
        if(n==0){CHECK(Bc250PermitWriteAbandon(&r,&w)==STATUS_SUCCESS);
            CHECK(!r.Published && !r.WriterAddress && Bc250PermitAcquire(&r,&a)==STATUS_DEVICE_NOT_READY);Publish(&r,2);}
        else{
            CHECK(Bc250PermitWriteClaim(&r,&w)==STATUS_SUCCESS);
            final=n==1?STATUS_CANCELLED:n==2?(NTSTATUS)0x80000005U:n==3?(NTSTATUS)1:STATUS_SUCCESS;
            revision=n==4?0:n==5?1:~(ULONGLONG)0;
            expected=n==1?STATUS_SUCCESS:STATUS_DATA_ERROR;
            CHECK(Bc250PermitWriteFinish(&r,&w,final,revision)==expected);
            CHECK(!r.Published && !r.WriterAddress && PermitZero(&w,sizeof(w)));
            if(n==1)Publish(&r,2);else CHECK(r.Fault && r.Closing);
        }
        CHECK(Bc250PermitClose(&r)==STATUS_SUCCESS);
    }
}
static VOID LimitsAndInvalid(VOID)
{
    ULONG n;for(n=0;n<5;++n){
        BC250_PERMIT r;BC250_PERMIT_WRITER w={0};BC250_PERMIT_READER a={0};Init(&r,400+n);
        /* Private fixture seeds only; caller may never edit a published root. */
        if(n==0){r.Epoch=BC250_PERMIT_LIMIT-1U;CHECK(Bc250PermitWriteAdmit(&r,1,&w)==STATUS_DATA_ERROR);}
        if(n==1){r.LastId=~(ULONGLONG)0;CHECK(Bc250PermitWriteAdmit(&r,1,&w)==STATUS_DATA_ERROR);}
        if(n==2){Publish(&r,1);r.LastId=~(ULONGLONG)0;CHECK(Bc250PermitAcquire(&r,&a)==STATUS_DATA_ERROR);}
        if(n==3){r.Epoch=BC250_PERMIT_LIMIT-2U;CHECK(Bc250PermitWriteAdmit(&r,1,&w)==STATUS_SUCCESS);
            CHECK(Bc250PermitWriteClaim(&r,&w)==STATUS_DATA_ERROR);CHECK(Bc250PermitWriteAbandon(&r,&w)==STATUS_DEVICE_NOT_READY);}
        if(n==4){r.Epoch=BC250_PERMIT_LIMIT-3U;CHECK(Bc250PermitWriteAdmit(&r,1,&w)==STATUS_SUCCESS);
            CHECK(Bc250PermitWriteClaim(&r,&w)==STATUS_SUCCESS);CHECK(Bc250PermitWriteFinish(&r,&w,STATUS_SUCCESS,1)==STATUS_DEVICE_NOT_READY);}
        CHECK(r.Closing && r.Fault && !r.Published && !r.WriterAddress && !r.Pins && !r.Steps);
    }
    {
        BC250_PERMIT r;BC250_PERMIT_WRITER w={0};BC250_PERMIT_READER a={0};BC250_PERMIT_SNAPSHOT s;ULONGLONG v=99;
        Init(&r,500);CHECK(Bc250PermitAcquire(&r,&a)==STATUS_DEVICE_NOT_READY);
        CHECK(Bc250PermitWriteAdmit(&r,0,&w)==STATUS_INVALID_PARAMETER);
        CHECK(Bc250PermitWriteAdmit(&r,7,&w)==STATUS_INVALID_PARAMETER);
        CHECK(Bc250PermitWriteAdmit(&r,1,NULL)==STATUS_INVALID_PARAMETER);
        CHECK(Bc250PermitStepEnter(&r,NULL,&v)==STATUS_INVALID_PARAMETER && !v);
        irql=3;memset(&s,0xff,sizeof(s));CHECK(Bc250PermitInspect(&r,&s)==STATUS_INVALID_DEVICE_STATE && PermitZero(&s,sizeof(s)));
        CHECK(Bc250PermitClose(&r)==STATUS_INVALID_DEVICE_STATE);irql=2;
        CHECK(Bc250PermitAcquire(&r,&a)==STATUS_INVALID_DEVICE_STATE);irql=0;
        CHECK(Bc250PermitInit(&r,501,1)==STATUS_INVALID_PARAMETER);CHECK(r.Epoch==1);
        CHECK(Bc250PermitClose(&r)==STATUS_SUCCESS);
    }
}
static VOID UnregisteredWriterNegativeProof(VOID)
{
    BC250_PERMIT r;BC250_PERMIT_READER a={0};ULONGLONG admitted,testExternalData=1;
    Init(&r,600);Publish(&r,1);CHECK(Bc250PermitAcquire(&r,&a)==STATUS_SUCCESS);
    CHECK(Bc250PermitStepEnter(&r,&a,&admitted)==STATUS_SUCCESS && !held);
    testExternalData=2; /* Test-owned external data mutated WITHOUT gate; not Root or hardware. */
    CHECK(admitted==1 && r.Revision==1 && testExternalData!=admitted);
    CHECK(Bc250PermitStepLeave(&r,&a)==STATUS_SUCCESS);CHECK(Bc250PermitRelease(&r,&a)==STATUS_SUCCESS);
    CHECK(Bc250PermitClose(&r)==STATUS_SUCCESS);
}
int main(VOID)
{
    Cycles();CloseAndOverlap();IdentityAndQuarantine();FinalStates();LimitsAndInvalid();UnregisteredWriterNegativeProof();CHECK(!held && !irql);
    printf("PASS: composite permit %u checks; participant metadata pins/steps/revocation/drain-only claim; no real Source/hardware/SMP.\n",checks);return 0;
}
