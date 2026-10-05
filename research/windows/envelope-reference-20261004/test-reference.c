/* SPDX-License-Identifier: Apache-2.0 */
#define BC250_ENV_MOCK 1
#define BC250_LIFE_MOCK 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../context-lifetime-20261004/bc250_context_lifetime.c"
#include "bc250_envelope_reference.c"
BOOLEAN Bc250LifeMockAllowed=TRUE,Bc250EnvMockAllowed=TRUE;
static unsigned checks,fixtures,platform,childDrops,parentDrops,busy,unknown;
static ULONG held;static KIRQL irql;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL line %u: %s\n",(unsigned)__LINE__,#x);exit(1);}}while(0)
typedef struct CHILD {ULONG Alive,Value;} CHILD;
typedef struct WORLD {
    BC250_ENV_DOMAIN Env;
    BC250_LIFE_DOMAIN Life;
    BC250_ENV_RECEIPT Receipts[9];
    DEVICE_OBJECT Self;
    IO_REMOVE_LOCK Remove;
    CHILD Child;
    ULONG CloseEnvAt,CloseLifeAt,Current,RetireChild,DeleteParent,ChildGone,ParentGone;
} WORLD;
/* Serial externally anchored fixture including code through every API return.
 * Poison is observer instrumentation, NOT real Ob deletion/SMP proof. */
static WORLD *world;
unsigned char KeGetCurrentIrql(VOID){++platform;return irql;}
SIZE_T RtlCompareMemory(const VOID *a,const VOID *b,SIZE_T n)
{SIZE_T k;const UCHAR *x=a,*y=b;++platform;for(k=0;k<n && x[k]==y[k];++k){}return k;}
VOID KeInitializeSpinLock(KSPIN_LOCK *p){++platform;p->Initialized=1;p->Held=0;}
VOID KeAcquireSpinLock(KSPIN_LOCK *p,KIRQL *old)
{++platform;CHECK(!held && p->Initialized && !p->Held && irql<=2);*old=irql;irql=2;held=p->Held=1;}
VOID KeReleaseSpinLock(KSPIN_LOCK *p,KIRQL old)
{
    ++platform;CHECK(held && p->Held && irql==2);held=p->Held=0;irql=old;
    if(world && world->RetireChild && p==&world->Life.Lock && !irql &&
       !world->Life.Calls && !world->Life.Holds){
        PVOID ptr=NULL;world->RetireChild=0;
        CHECK(Bc250LifeDetach(&world->Life,&ptr)==STATUS_SUCCESS && ptr==&world->Child);
        CHECK(world->Self.Fake>=2 && world->Remove.Count==1); /* ref and tag STILL retained */
        memset(ptr,0xdd,sizeof(CHILD));world->ChildGone=1;++childDrops;
    }
}
static VOID Hook(ULONG point)
{
    CHECK(!held && world && !world->ParentGone);
    if(world->CloseEnvAt==point){world->CloseEnvAt=0;CHECK(Bc250EnvClose(&world->Env)==STATUS_SUCCESS);}
    if(world->CloseLifeAt==point){world->CloseLifeAt=0;CHECK(Bc250LifeClose(&world->Life)==STATUS_SUCCESS);}
}
VOID ObReferenceObject(PVOID p)
{++platform;CHECK(!held && p==&world->Self && world->Self.Fake>=1 && irql<=2);++world->Self.Fake;Hook(1);}
VOID ObDereferenceObject(PVOID p)
{
    ULONG i;++platform;CHECK(!held && p==&world->Self && world->Self.Fake>=1 && irql<=2);
    CHECK(EnvZero(&world->Receipts[world->Current],sizeof(BC250_ENV_RECEIPT)));
    for(i=0;i<BC250_ENV_SLOTS;++i)CHECK(world->Env.Slots[i].Address!=&world->Receipts[world->Current]);
    Hook(4);--world->Self.Fake;
    if(!world->Self.Fake){
        CHECK(world->DeleteParent && !world->Remove.Count && !world->Life.Calls && !world->Life.Holds);
        memset(&world->Env,0xdd,sizeof(world->Env));memset(&world->Life,0xdd,sizeof(world->Life));
        memset(world->Receipts,0xdd,sizeof(world->Receipts));world->ParentGone=1;++parentDrops;
        /* Native wrapper MUST return stack-only; no root/envelope access below. */
    }
}
NTSTATUS IoAcquireRemoveLock(PIO_REMOVE_LOCK p,PVOID tag)
{
    ULONG i;++platform;CHECK(!held && p==&world->Remove && irql<=2);Hook(2);++p->Attempts;
    if(p->RejectAt==p->Attempts)return (NTSTATUS)0xc0000056U;
    for(i=0;i<8 && p->Tags[i];++i){}CHECK(i<8);p->Tags[i]=tag;++p->Count;return STATUS_SUCCESS;
}
VOID IoReleaseRemoveLock(PIO_REMOVE_LOCK p,PVOID tag)
{
    ULONG i;++platform;CHECK(!held && p==&world->Remove && p->Count && irql<=2);
    for(i=0;i<8 && p->Tags[i]!=tag;++i){}CHECK(i<8);p->Tags[i]=NULL;--p->Count;Hook(3);
    if(world->DeleteParent){CHECK(!p->Count && world->Self.Fake==2);--world->Self.Fake;}
}
static VOID Init(WORLD *w)
{
    CHECK(!held);irql=0;memset(w,0,sizeof(*w));world=w;w->Self.Fake=1;w->Child.Alive=1;w->Child.Value=23;
    CHECK(Bc250LifeInit(&w->Life,100+fixtures)==STATUS_SUCCESS);
    CHECK(Bc250LifePublish(&w->Life,1,1,&w->Child)==STATUS_SUCCESS);
    CHECK(Bc250EnvInit(&w->Env,200+fixtures,&w->Self,&w->Remove,&w->Life,1)==STATUS_SUCCESS);++fixtures;
}
static VOID KnownEmpty(WORLD *w)
{
    ULONG i;CHECK(!w->ParentGone && w->Self.Fake==1 && !w->Remove.Count && !w->Life.Calls && !w->Life.Holds);
    for(i=0;i<8;++i)CHECK(!w->Env.Slots[i].Address && !w->Env.Slots[i].Phase);
    irql=0;if(!w->ChildGone){PVOID ptr=NULL;CHECK(Bc250LifeClose(&w->Life)==STATUS_SUCCESS);
        CHECK(Bc250LifeDetach(&w->Life,&ptr)==STATUS_SUCCESS && ptr==&w->Child);memset(ptr,0xdd,sizeof(CHILD));}
    world=NULL;
}
static VOID AcquireAndDrop(VOID)
{
    ULONG level,closeEnv,closeLife;
    for(level=0;level<=2;level+=2)for(closeEnv=0;closeEnv<3;++closeEnv)for(closeLife=0;closeLife<3;++closeLife){
        WORLD w;NTSTATUS st;Init(&w);irql=(KIRQL)level;w.CloseEnvAt=closeEnv;w.CloseLifeAt=closeLife;
        st=Bc250EnvAcquire(&w.Env,&w.Receipts[0]);
        if(closeLife){CHECK(st==STATUS_DEVICE_NOT_READY && EnvZero(&w.Receipts[0],sizeof(w.Receipts[0])));}
        else{
            CHECK(st==STATUS_SUCCESS && w.Self.Fake==2 && w.Remove.Count==1 && w.Life.Holds==1);
            if(closeEnv)CHECK(Bc250EnvAcquire(&w.Env,&w.Receipts[1])==STATUS_DEVICE_NOT_READY);
            CHECK(Bc250EnvRelease(&w.Env,&w.Receipts[0])==STATUS_SUCCESS);
            CHECK(Bc250EnvRelease(&w.Env,&w.Receipts[0])==STATUS_INVALID_PARAMETER);
        }
        KnownEmpty(&w);
    }
    for(level=0;level<=2;level+=2){
        WORLD w;Init(&w);irql=(KIRQL)level;w.Remove.RejectAt=1;
        CHECK(Bc250EnvAcquire(&w.Env,&w.Receipts[0])==(NTSTATUS)0xc0000056U);
        CHECK(EnvZero(&w.Receipts[0],sizeof(w.Receipts[0])));KnownEmpty(&w);
    }
}
static VOID DerivedAndFinalDrops(VOID)
{
    ULONG level;
    for(level=0;level<=2;level+=2){
        WORLD w;BC250_LIFE_TOKEN call={0};PVOID ptr=NULL;Init(&w);irql=(KIRQL)level;
        CHECK(Bc250EnvAcquire(&w.Env,&w.Receipts[0])==STATUS_SUCCESS);
        CHECK(Bc250LifeEnterHeld(&w.Life,&w.Receipts[0].Hold,&call,&ptr)==STATUS_SUCCESS && ptr==&w.Child);
        CHECK(((CHILD *)ptr)->Alive==1 && ((CHILD *)ptr)->Value==23);
        CHECK(Bc250EnvClose(&w.Env)==STATUS_SUCCESS && Bc250LifeClose(&w.Life)==STATUS_SUCCESS);
        CHECK(Bc250EnvRelease(&w.Env,&w.Receipts[0])==STATUS_DEVICE_BUSY);++busy;
        CHECK(w.Remove.Count==1 && w.Self.Fake==2 && w.Life.Holds==1 && w.Life.Calls==1);
        CHECK(Bc250LifeExit(&w.Life,&call)==STATUS_SUCCESS);
        w.CloseEnvAt=3;CHECK(Bc250EnvRelease(&w.Env,&w.Receipts[0])==STATUS_SUCCESS);KnownEmpty(&w);
    }
    {
        WORLD w;Init(&w);CHECK(Bc250EnvAcquire(&w.Env,&w.Receipts[0])==STATUS_SUCCESS);
        CHECK(Bc250LifeClose(&w.Life)==STATUS_SUCCESS);w.RetireChild=1;w.DeleteParent=1;
        CHECK(Bc250EnvRelease(&w.Env,&w.Receipts[0])==STATUS_SUCCESS);
        CHECK(w.ChildGone && w.ParentGone && !w.Self.Fake && !held);world=NULL;
    }
}
static VOID IdentityAndCapacity(VOID)
{
    ULONG n;WORLD w;BC250_ENV_RECEIPT copy;
    Init(&w);for(n=0;n<8;++n)CHECK(Bc250EnvAcquire(&w.Env,&w.Receipts[n])==STATUS_SUCCESS);
    CHECK(Bc250EnvAcquire(&w.Env,&w.Receipts[8])==STATUS_DEVICE_BUSY && w.Self.Fake==9 && w.Remove.Count==8);
    copy=w.Receipts[0];CHECK(Bc250EnvRelease(&w.Env,&copy)==STATUS_INVALID_PARAMETER && w.Remove.Count==8);
    for(n=0;n<8;++n){w.Current=n;CHECK(Bc250EnvRelease(&w.Env,&w.Receipts[n])==STATUS_SUCCESS);}KnownEmpty(&w);
    for(n=0;n<6;++n){
        Init(&w);CHECK(Bc250EnvAcquire(&w.Env,&w.Receipts[0])==STATUS_SUCCESS);copy=w.Receipts[0];
        if(n==0)++w.Receipts[0].Id;
        if(n==1)w.Receipts[0].Self=NULL;
        if(n==2)w.Receipts[0].Root=NULL;
        if(n==3)++w.Receipts[0].Scope;
        if(n==4)++w.Receipts[0].Hold.Id;
        if(n==5){
            CHECK(Bc250EnvRelease(&w.Env,&w.Receipts[0])==STATUS_SUCCESS);
            CHECK(Bc250EnvAcquire(&w.Env,&w.Receipts[0])==STATUS_SUCCESS && w.Receipts[0].Id>copy.Id);
            w.Receipts[0]=copy; /* test-only old identity replay at SAME address */
        }
        CHECK(Bc250EnvRelease(&w.Env,&w.Receipts[0])==STATUS_INVALID_PARAMETER);++unknown;
        CHECK(w.Self.Fake==2 && w.Remove.Count==1 && w.Life.Holds==1);
        CHECK(Bc250EnvClose(&w.Env)==STATUS_SUCCESS && Bc250EnvAcquire(&w.Env,&w.Receipts[1])==STATUS_DEVICE_NOT_READY);
        /* Fixture end, NOT a recovery/free of unknown production resources. */
        world=NULL;
    }
}
static VOID PolicyAndLimits(VOID)
{
    WORLD w;ULONG before;Init(&w);irql=3;before=platform;
    CHECK(Bc250EnvAcquire(&w.Env,&w.Receipts[0])==STATUS_INVALID_DEVICE_STATE);
    CHECK(Bc250EnvRelease(&w.Env,&w.Receipts[0])==STATUS_INVALID_DEVICE_STATE);
    CHECK(Bc250EnvClose(&w.Env)==STATUS_INVALID_DEVICE_STATE && platform==before+3);
    irql=0;Bc250EnvMockAllowed=FALSE;before=platform;
    CHECK(Bc250EnvAcquire(&w.Env,&w.Receipts[0])==STATUS_NOT_SUPPORTED);
    CHECK(Bc250EnvRelease(&w.Env,&w.Receipts[0])==STATUS_NOT_SUPPORTED);
    CHECK(Bc250EnvClose(&w.Env)==STATUS_NOT_SUPPORTED && platform==before);Bc250EnvMockAllowed=TRUE;
    w.Env.LastId=~(ULONGLONG)0-1U;
    CHECK(Bc250EnvAcquire(&w.Env,&w.Receipts[0])==STATUS_DATA_ERROR && w.Env.Fault && w.Env.Closing);
    CHECK(Bc250EnvAcquire(&w.Env,&w.Receipts[0])==STATUS_DEVICE_NOT_READY);
    CHECK(Bc250EnvInit(&w.Env,1,&w.Self,&w.Remove,&w.Life,1)==STATUS_INVALID_PARAMETER);
    KnownEmpty(&w);
}
int main(VOID)
{
    AcquireAndDrop();DerivedAndFinalDrops();IdentityAndCapacity();PolicyAndLimits();
    CHECK(childDrops==1 && parentDrops==1 && busy==2 && unknown==6);
    printf("PASS: envelope reference %u checks, %u fixtures; busy %u unknown %u child-last-drop %u parent-last-deref %u; external OS/module anchor, fake refs/tags only, no hardware.\n",
        checks,fixtures,busy,unknown,childDrops,parentDrops);return 0;
}
