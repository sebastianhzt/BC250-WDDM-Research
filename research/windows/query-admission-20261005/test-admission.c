/* SPDX-License-Identifier: Apache-2.0 */
#define BC250_QUERY_ADMIT_MOCK 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bc250_query_admission.c"
BOOLEAN Bc250QueryAdmitMockAllowed=TRUE;
static KIRQL irql;
static ULONG held,apcs,checks,calls;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %u: %s\n",(unsigned)__LINE__,#x);exit(1);}}while(0)
UCHAR KeGetCurrentIrql(VOID){return irql;}
BOOLEAN KeAreApcsDisabled(VOID){++calls;return apcs?TRUE:FALSE;}
VOID KeInitializeSpinLock(KSPIN_LOCK *p){p->Initialized=1;p->Held=0;}
VOID KeAcquireSpinLock(KSPIN_LOCK *p,KIRQL *old)
{++calls;CHECK(!held && p->Initialized && !p->Held && irql<=2);*old=irql;irql=2;held=p->Held=1;}
VOID KeReleaseSpinLock(KSPIN_LOCK *p,KIRQL old)
{CHECK(held && p->Held);held=p->Held=0;irql=old;}
static VOID Init(BC250_QUERY_ADMISSION *d)
{memset(d,0,sizeof(*d));CHECK(Bc250QueryAdmitInit(d,7,11,13)==STATUS_SUCCESS);}
int main(VOID)
{
    BC250_QUERY_ADMISSION d,other;BC250_QUERY_TICKET t={0},copy,second={0},saved;
    ULONG reason,n,before;
    Init(&d);CHECK(Bc250QueryAdmitEnter(&d,&t)==STATUS_SUCCESS && !held && !irql && !apcs);
    saved=t;CHECK(Bc250QueryAdmitEnter(&d,&t)==STATUS_DEVICE_BUSY && !memcmp(&saved,&t,sizeof(t)));
    CHECK(Bc250QueryAdmitEnter(&d,&second)==STATUS_DEVICE_BUSY && !second.Domain);
    copy=t;CHECK(Bc250QueryAdmitCheck(&d,&copy)==STATUS_INVALID_PARAMETER);
    CHECK(Bc250QueryAdmitLeave(&d,&copy)==STATUS_INVALID_PARAMETER && d.Address==&t);
    Init(&other);CHECK(Bc250QueryAdmitLeave(&other,&t)==STATUS_INVALID_PARAMETER && d.Address==&t);
    CHECK(Bc250QueryAdmitCheck(&d,&t)==STATUS_SUCCESS);
    apcs=1;CHECK(Bc250QueryAdmitCheck(&d,&t)==STATUS_INVALID_DEVICE_STATE && !held);
    CHECK(Bc250QueryAdmitEnter(&other,&second)==STATUS_INVALID_DEVICE_STATE);apcs=0;
    for(n=0;n<5;++n){
        saved=t;if(n==0)t.Domain=&other;if(n==1)++t.Scope;if(n==2)++t.Generation;
        if(n==3)++t.Epoch;if(n==4)++t.Id;
        CHECK(Bc250QueryAdmitLeave(&d,&t)==STATUS_INVALID_PARAMETER && d.Address==&t);
        CHECK(Bc250QueryAdmitQuarantine(&d,&t)==STATUS_INVALID_PARAMETER && !d.Quarantined);
        t=saved;
    }
    CHECK(Bc250QueryAdmitEnter(&other,(BC250_QUERY_TICKET *)&other.Seal)==STATUS_INVALID_PARAMETER);
    CHECK(Bc250QueryAdmitEnter(&other,(BC250_QUERY_TICKET *)(ULONG_PTR)(~(ULONG_PTR)0-1U))==STATUS_INVALID_PARAMETER);
    CHECK(Bc250QueryAdmitClose(&d,0)==STATUS_INVALID_PARAMETER && !d.Reasons);
    CHECK(Bc250QueryAdmitClose(&d,3)==STATUS_INVALID_PARAMETER && !d.Reasons);
    CHECK(Bc250QueryAdmitClose(&d,64)==STATUS_INVALID_PARAMETER && !d.Reasons);
    for(reason=1;reason<=32;reason*=2){
        irql=2;apcs=1;CHECK(Bc250QueryAdmitClose(&d,reason)==STATUS_SUCCESS && irql==2 && !held);
        irql=0;apcs=0;CHECK(Bc250QueryAdmitCheck(&d,&t)==STATUS_DEVICE_NOT_READY);
    }
    CHECK(d.Reasons==63 && Bc250QueryAdmitLeave(&d,&t)==STATUS_SUCCESS && !d.Address && d.Used && !t.Domain);
    CHECK(Bc250QueryAdmitLeave(&d,&t)==STATUS_INVALID_PARAMETER);
    CHECK(Bc250QueryAdmitEnter(&d,&t)==STATUS_DEVICE_NOT_READY);
    Init(&d);CHECK(Bc250QueryAdmitEnter(&d,&t)==STATUS_SUCCESS);
    CHECK(Bc250QueryAdmitLeave(&d,&t)==STATUS_SUCCESS);
    CHECK(Bc250QueryAdmitEnter(&d,&t)==STATUS_INVALID_DEVICE_STATE);
    Init(&d);CHECK(Bc250QueryAdmitEnter(&d,&t)==STATUS_SUCCESS);saved=t;
    CHECK(Bc250QueryAdmitQuarantine(&d,&t)==STATUS_SUCCESS);
    CHECK(Bc250QueryAdmitLeave(&d,&t)==STATUS_DATA_ERROR && !memcmp(&saved,&t,sizeof(t)) && d.Address==&t);
    CHECK(Bc250QueryAdmitCheck(&d,&t)==STATUS_DATA_ERROR);
    CHECK(Bc250QueryAdmitEnter(&d,&second)==STATUS_DATA_ERROR);
    CHECK(Bc250QueryAdmitClose(&d,BC250_QA_REMOVE)==STATUS_SUCCESS && d.Address==&t);
    CHECK(Bc250QueryAdmitQuarantine(&d,&t)==STATUS_DATA_ERROR);
    irql=3;CHECK(Bc250QueryAdmitClose(&d,BC250_QA_STOP)==STATUS_INVALID_DEVICE_STATE);irql=0;
    Bc250QueryAdmitMockAllowed=FALSE;before=calls;
    CHECK(Bc250QueryAdmitEnter((BC250_QUERY_ADMISSION *)1,(BC250_QUERY_TICKET *)1)==STATUS_NOT_SUPPORTED && before==calls);
    Bc250QueryAdmitMockAllowed=TRUE;
    /* No free/recovery of retained final ticket: fixture end not reclamation. */
    printf("PASS: query admission %u checks; short locks, APC context, exact seal, one-shot, monotonic close, unknown retained; serial RAM only.\n",checks);
    return 0;
}
