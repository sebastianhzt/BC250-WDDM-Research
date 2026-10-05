/* SPDX-License-Identifier: Apache-2.0 */
#define BC250_INTAKE_MOCK 1
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "bc250_dispatch_intake.c"
BOOLEAN Bc250IntakeMockAllowed=TRUE;
static unsigned checks,held;static KIRQL irql;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL line %u: %s\n",(unsigned)__LINE__,#x);exit(1);}}while(0)
unsigned char KeGetCurrentIrql(VOID){return irql;}
VOID KeInitializeSpinLock(KSPIN_LOCK *l){l->Initialized=1;l->Held=0;}
VOID KeAcquireSpinLock(KSPIN_LOCK *l,KIRQL *old)
{CHECK(l->Initialized && !l->Held && !held && irql<=2);*old=irql;l->Held=held=1;irql=2;}
VOID KeReleaseSpinLock(KSPIN_LOCK *l,KIRQL old)
{CHECK(l->Held && held && irql==2);l->Held=held=0;irql=old;}
static VOID Init(BC250_INTAKE *r,ULONGLONG scope)
{RtlZeroMemory(r,sizeof(*r));irql=0;CHECK(Bc250IntakeInit(r,scope,1)==STATUS_SUCCESS);}
static BC250_INTAKE_SNAPSHOT Snap(BC250_INTAKE *r)
{BC250_INTAKE_SNAPSHOT s;CHECK(Bc250IntakeInspect(r,&s)==STATUS_SUCCESS);return s;}
static VOID Cycles(VOID)
{
    BC250_INTAKE r;ULONG n,kind,level;BC250_INTAKE_SNAPSHOT before,after;
    Init(&r,1);
    for(n=0;n<2000;++n)for(kind=1;kind<=6;++kind)for(level=0;level<=2;++level){
        BC250_INTAKE_TICKET t={0};NTSTATUS final=(n&1)?STATUS_CANCELLED:STATUS_SUCCESS;
        before=Snap(&r);CHECK(before.MetadataIdle && !before.PendingId);
        CHECK(Bc250IntakeCheckIdleAtEpoch(&r,before.Epoch)==STATUS_SUCCESS);
        irql=(KIRQL)level;
        CHECK(Bc250IntakeAdmit(&r,kind,kind==6?1+n%4:0,&t)==STATUS_SUCCESS);
        after=Snap(&r);CHECK(!after.MetadataIdle && after.Phase==1 && after.PendingId==t.Id);
        if(level)CHECK(Bc250IntakeClaim(&r,&t)==STATUS_INVALID_DEVICE_STATE);
        irql=0;CHECK(Bc250IntakeCheckIdleAtEpoch(&r,before.Epoch)==STATUS_DEVICE_NOT_READY);
        CHECK(Bc250IntakeClaim(&r,&t)==STATUS_SUCCESS);
        CHECK(Bc250IntakeClaim(&r,&t)==STATUS_DEVICE_BUSY);
        irql=2;CHECK(Bc250IntakeAbandon(&r,&t)==STATUS_DEVICE_BUSY);
        CHECK(Bc250IntakeFinish(&r,&t,final)==STATUS_INVALID_DEVICE_STATE);
        irql=0;CHECK(Bc250IntakeFinish(&r,&t,STATUS_PENDING)==STATUS_DEVICE_BUSY);
        CHECK(!IntakeZero(&t,sizeof(t)) && r.Address==&t);
        CHECK(Bc250IntakeFinish(&r,&t,final)==STATUS_SUCCESS);
        CHECK(IntakeZero(&t,sizeof(t)) && !r.Address && !r.Phase && r.LastFinal==final);
        after=Snap(&r);CHECK(after.MetadataIdle && after.Epoch>before.Epoch);
        CHECK(Bc250IntakeCheckIdleAtEpoch(&r,before.Epoch)==STATUS_DEVICE_NOT_READY);
        CHECK(Bc250IntakeCheckIdleAtEpoch(&r,after.Epoch)==STATUS_SUCCESS);
        CHECK(Bc250IntakeFinish(&r,&t,final)==STATUS_INVALID_PARAMETER);
    }
}
static VOID OverlapAndClose(VOID)
{
    ULONG first,second,claimed,closeFirst;
    for(first=1;first<=6;++first)for(second=1;second<=6;++second)
    for(claimed=0;claimed<2;++claimed)for(closeFirst=0;closeFirst<2;++closeFirst){
        BC250_INTAKE r;BC250_INTAKE_TICKET a={0},b={0};BC250_INTAKE_SNAPSHOT old;
        Init(&r,100+first*100+second*4+claimed*2+closeFirst);old=Snap(&r);
        irql=2;CHECK(Bc250IntakeAdmit(&r,first,first==6?1:0,&a)==STATUS_SUCCESS);
        irql=0;if(claimed)CHECK(Bc250IntakeClaim(&r,&a)==STATUS_SUCCESS);
        irql=2;if(closeFirst)CHECK(Bc250IntakeClose(&r)==STATUS_SUCCESS);
        CHECK(Bc250IntakeAdmit(&r,second,second==6?4:0,&b)==
            (closeFirst?STATUS_DEVICE_NOT_READY:STATUS_DEVICE_BUSY));
        CHECK(IntakeZero(&b,sizeof(b)) && r.Address==&a && r.Closing);
        CHECK(closeFirst?!r.Fault:r.Fault);CHECK(!Snap(&r).MetadataIdle);
        irql=0;CHECK(Bc250IntakeCheckIdleAtEpoch(&r,old.Epoch)==STATUS_DEVICE_NOT_READY);
        if(claimed){CHECK(Bc250IntakeAbandon(&r,&a)==STATUS_DEVICE_BUSY);
            CHECK(Bc250IntakeFinish(&r,&a,STATUS_SUCCESS)==STATUS_DEVICE_NOT_READY);}
        else{CHECK(Bc250IntakeClaim(&r,&a)==STATUS_DEVICE_NOT_READY);irql=2;
            CHECK(Bc250IntakeAbandon(&r,&a)==STATUS_DEVICE_NOT_READY);irql=0;}
        CHECK(IntakeZero(&a,sizeof(a)) && !r.Address && !Snap(&r).MetadataIdle);
        CHECK(Bc250IntakeAdmit(&r,1,0,&b)==STATUS_DEVICE_NOT_READY);
    }
}
static VOID IdentityAndInvalid(VOID)
{
    BC250_INTAKE r,other;BC250_INTAKE_TICKET a={0},copy,zero={0};BC250_INTAKE_SNAPSHOT s;
    Init(&r,900);Init(&other,901);irql=2;
    CHECK(Bc250IntakeAdmit(&r,1,0,&a)==STATUS_SUCCESS);copy=a;irql=0;
    CHECK(Bc250IntakeClaim(&r,&copy)==STATUS_INVALID_PARAMETER);
    CHECK(Bc250IntakeAbandon(&r,&copy)==STATUS_INVALID_PARAMETER);
    CHECK(Bc250IntakeClaim(&other,&a)==STATUS_INVALID_PARAMETER);
    CHECK(Bc250IntakeFinish(&r,&a,STATUS_SUCCESS)==STATUS_DEVICE_BUSY);
    a.Id++;CHECK(Bc250IntakeClaim(&r,&a)==STATUS_INVALID_PARAMETER);
    CHECK(Bc250IntakeAbandon(&r,&a)==STATUS_INVALID_PARAMETER);CHECK(r.Address==&a);
    /* No repair of unknown identity. This fixture remains deliberately pinned. */
    CHECK(Bc250IntakeClose(&r)==STATUS_SUCCESS);CHECK(r.Address==&a);
    CHECK(Bc250IntakeAdmit(&other,0,0,&zero)==STATUS_INVALID_PARAMETER);
    CHECK(Bc250IntakeAdmit(&other,7,0,&zero)==STATUS_INVALID_PARAMETER);
    CHECK(Bc250IntakeAdmit(&other,1,1,&zero)==STATUS_INVALID_PARAMETER);
    CHECK(Bc250IntakeAdmit(&other,6,0,&zero)==STATUS_INVALID_PARAMETER);
    CHECK(Bc250IntakeAdmit(&other,6,5,&zero)==STATUS_INVALID_PARAMETER);
    CHECK(Bc250IntakeAdmit(&other,1,0,NULL)==STATUS_INVALID_PARAMETER);
    CHECK(Snap(&other).Epoch==1 && !other.Address);
    CHECK(Bc250IntakeCheckIdleAtEpoch(&other,0)==STATUS_DEVICE_NOT_READY);
    irql=3;CHECK(Bc250IntakeAdmit(&other,1,0,&zero)==STATUS_INVALID_DEVICE_STATE);
    CHECK(Bc250IntakeClose(&other)==STATUS_INVALID_DEVICE_STATE);
    memset(&s,0xff,sizeof(s));CHECK(Bc250IntakeInspect(&other,&s)==STATUS_INVALID_DEVICE_STATE && IntakeZero(&s,sizeof(s)));
    irql=0;CHECK(Bc250IntakeInit(&other,902,1)==STATUS_INVALID_PARAMETER);
    RtlZeroMemory(&other,sizeof(other));CHECK(Bc250IntakeInit(&other,0,1)==STATUS_INVALID_PARAMETER);
    CHECK(Bc250IntakeInit(&other,902,0)==STATUS_INVALID_PARAMETER);
    CHECK(Bc250IntakeInit(&other,902,BC250_INTAKE_LIMIT)==STATUS_INVALID_PARAMETER);
}
static VOID AbandonAndLimits(VOID)
{
    ULONG n;for(n=0;n<5;++n){
        BC250_INTAKE r;BC250_INTAKE_TICKET t={0};Init(&r,1000+n);
        if(n==0){CHECK(Bc250IntakeAdmit(&r,1,0,&t)==STATUS_SUCCESS);irql=2;
            CHECK(Bc250IntakeAbandon(&r,&t)==STATUS_SUCCESS);CHECK(!r.Address && IntakeZero(&t,sizeof(t)));irql=0;}
        if(n==1){r.Epoch=BC250_INTAKE_LIMIT-1U;
            CHECK(Bc250IntakeAdmit(&r,1,0,&t)==STATUS_DATA_ERROR);CHECK(!r.Address && r.Fault);}
        if(n==2){r.LastId=~(ULONGLONG)0;
            CHECK(Bc250IntakeAdmit(&r,1,0,&t)==STATUS_DATA_ERROR);CHECK(!r.Address && r.Fault);}
        if(n==3){r.Epoch=BC250_INTAKE_LIMIT-2U;
            CHECK(Bc250IntakeAdmit(&r,1,0,&t)==STATUS_SUCCESS);
            CHECK(Bc250IntakeClaim(&r,&t)==STATUS_DATA_ERROR);
            CHECK(Bc250IntakeAbandon(&r,&t)==STATUS_DEVICE_NOT_READY);CHECK(!r.Address);}
        if(n==4){r.Epoch=BC250_INTAKE_LIMIT-3U;
            CHECK(Bc250IntakeAdmit(&r,1,0,&t)==STATUS_SUCCESS);CHECK(Bc250IntakeClaim(&r,&t)==STATUS_SUCCESS);
            CHECK(Bc250IntakeFinish(&r,&t,STATUS_SUCCESS)==STATUS_DEVICE_NOT_READY);CHECK(!r.Address && r.Fault);}
    }
}
int main(VOID)
{
    Cycles();OverlapAndClose();IdentityAndInvalid();AbandonAndLimits();CHECK(!irql && !held);
    printf("PASS: DISPATCH intake %u checks; metadata invalidation/overlap/known cleanup only; no IRP/workqueue/SMP/hardware.\n",checks);return 0;
}
