/* SPDX-License-Identifier: Apache-2.0 */
/* Stable Parent fixture remains alive through every attempt/return; fake child
 * poisoned only after successful metadata detach. No native OS/SMP/hardware. */
#define BC250_LIFE_MOCK 1
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "bc250_context_lifetime.c"
BOOLEAN Bc250LifeMockAllowed=TRUE;
static unsigned checks,held,fixtures,bodyCalls,detachedInsideExit;
static KIRQL irql;
typedef struct CHILD { ULONG Alive,BodyActive,Value; } CHILD;
static BC250_LIFE_DOMAIN *detachHook;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL line %u: %s\n",(unsigned)__LINE__,#x);exit(1);}}while(0)
unsigned char KeGetCurrentIrql(VOID){return irql;}
VOID KeInitializeSpinLock(KSPIN_LOCK *p){p->Initialized=1;p->Held=0;}
VOID KeAcquireSpinLock(KSPIN_LOCK *p,KIRQL *old)
{CHECK(!held && p->Initialized && !p->Held && irql<=2);*old=irql;irql=2;held=p->Held=1;}
VOID KeReleaseSpinLock(KSPIN_LOCK *p,KIRQL old)
{
    CHECK(held && p->Held && irql==2);held=p->Held=0;irql=old;
    if(detachHook){
        BC250_LIFE_DOMAIN *d=detachHook;PVOID payload=NULL;CHILD *child;
        detachHook=NULL;CHECK(irql==0);
        CHECK(Bc250LifeDetach(d,&payload)==STATUS_SUCCESS && payload);
        child=payload;CHECK(child->Alive==1 && !child->BodyActive);
        memset(child,0xdd,sizeof(*child));++detachedInsideExit;
        /* Exit helper now continues after child detach/poison, uses Parent only. */
    }
}
static VOID InitLife(BC250_LIFE_DOMAIN *d,CHILD *c,ULONGLONG scope)
{
    RtlZeroMemory(d,sizeof(*d));RtlZeroMemory(c,sizeof(*c));c->Alive=1;c->Value=17;
    CHECK(Bc250LifeInit(d,scope)==STATUS_SUCCESS);
    CHECK(Bc250LifePublish(d,1,1,c)==STATUS_SUCCESS);++fixtures;
}
static VOID DetachLife(BC250_LIFE_DOMAIN *d,CHILD *c)
{
    PVOID p=(PVOID)(ULONG_PTR)1;CHECK(Bc250LifeClose(d)==STATUS_SUCCESS);
    CHECK(Bc250LifeDetach(d,&p)==STATUS_SUCCESS && p==c && !c->BodyActive);
    memset(c,0xdd,sizeof(*c));CHECK(!d->Payload && !d->Calls && !d->Holds);
}
/* Structured TEST wrapper: acquire BEFORE any child read; retain until body
 * returns and its output copy done, even error paths; then parent-only Exit. */
static NTSTATUS BodyWrapper(BC250_LIFE_DOMAIN *d,ULONGLONG key,BOOLEAN error,BOOLEAN close)
{
    BC250_LIFE_TOKEN call={0};PVOID ptr=NULL;NTSTATUS st,body;CHILD *c;ULONG value;
    st=Bc250LifeEnter(d,key,&call,&ptr);if(st!=STATUS_SUCCESS){CHECK(!ptr);return st;}
    c=ptr;CHECK(c->Alive==1 && !c->BodyActive && !held);c->BodyActive=1;++bodyCalls;
    value=c->Value;body=error?STATUS_CANCELLED:STATUS_SUCCESS;
    if(close){
        PVOID gone=(PVOID)(ULONG_PTR)1;irql=2;CHECK(Bc250LifeClose(d)==STATUS_SUCCESS);irql=0;
        CHECK(Bc250LifeDetach(d,&gone)==STATUS_DEVICE_BUSY && !gone);
    }
    CHECK(c->Alive==1 && c->Value==value); /* includes final metadata output copy */
    c->BodyActive=0; /* body and child access complete; no child access below */
    CHECK(Bc250LifeExit(d,&call)==STATUS_SUCCESS && LifeZero(&call,sizeof(call)));
    return body;
}
static VOID LookupAndReturnBoundary(VOID)
{
    ULONG n;for(n=0;n<4;++n){
        BC250_LIFE_DOMAIN d;CHILD c;InitLife(&d,&c,10+n);
        CHECK(BodyWrapper(&d,1,(BOOLEAN)(n&1),(BOOLEAN)(n&2))==((n&1)?STATUS_CANCELLED:STATUS_SUCCESS));
        if(n&2)CHECK(BodyWrapper(&d,1,FALSE,FALSE)==STATUS_DEVICE_NOT_READY);
        DetachLife(&d,&c);
        CHECK(BodyWrapper(&d,1,FALSE,FALSE)==STATUS_DEVICE_NOT_READY); /* no stale pointer read */
    }
    {
        BC250_LIFE_DOMAIN d;BC250_LIFE_TOKEN t={0};CHILD c;PVOID p;
        InitLife(&d,&c,20);CHECK(Bc250LifeEnter(&d,1,&t,&p)==STATUS_SUCCESS && p==&c);
        CHECK(Bc250LifeClose(&d)==STATUS_SUCCESS);
        detachHook=&d;CHECK(Bc250LifeExit(&d,&t)==STATUS_SUCCESS && !detachHook && detachedInsideExit==1);
        CHECK(!d.Payload && !d.Calls && LifeZero(&t,sizeof(t)));
        CHECK(BodyWrapper(&d,1,FALSE,FALSE)==STATUS_DEVICE_NOT_READY);
    }
}
static VOID Continuations(VOID)
{
    ULONG a,b;for(a=0;a<2;++a)for(b=0;b<2;++b){
        BC250_LIFE_DOMAIN d;CHILD c;BC250_LIFE_TOKEN dispatch={0},callback={0},worker={0};
        BC250_LIFE_TOKEN callbackCall={0},workerCall={0},newCall={0},extraHold={0};PVOID p,gone;
        InitLife(&d,&c,100+a*2+b);
        CHECK(Bc250LifeEnter(&d,1,&dispatch,&p)==STATUS_SUCCESS && p==&c);
        /* Reserve BOTH future envelopes before registration/forwarding in model. */
        irql=2;CHECK(Bc250LifeHold(&d,1,&callback)==STATUS_SUCCESS);
        CHECK(Bc250LifeHold(&d,1,&worker)==STATUS_SUCCESS);irql=0;
        if(a)CHECK(Bc250LifeExit(&d,&dispatch)==STATUS_SUCCESS); /* dispatch may return first */
        irql=2;CHECK(Bc250LifeClose(&d)==STATUS_SUCCESS);
        CHECK(Bc250LifeEnter(&d,1,&newCall,&p)==STATUS_DEVICE_NOT_READY && !p);
        CHECK(Bc250LifeHold(&d,1,&extraHold)==STATUS_DEVICE_NOT_READY && LifeZero(&extraHold,sizeof(extraHold)));
        CHECK(Bc250LifeEnterHeld(&d,&callback,&callbackCall,&p)==STATUS_SUCCESS && p==&c);
        CHECK(Bc250LifeReleaseHold(&d,&callback)==STATUS_DEVICE_BUSY);
        /* Queued worker can enter PASSIVE while callback's DISPATCH call retained. */
        irql=0;CHECK(Bc250LifeEnterHeld(&d,&worker,&workerCall,&p)==STATUS_SUCCESS && p==&c);
        CHECK(c.Alive==1);CHECK(Bc250LifeDetach(&d,&gone)==STATUS_DEVICE_BUSY && !gone);
        if(b){
            CHECK(Bc250LifeExit(&d,&workerCall)==STATUS_SUCCESS);
            CHECK(Bc250LifeReleaseHold(&d,&worker)==STATUS_SUCCESS); /* worker returns before callback */
        }
        irql=2;CHECK(Bc250LifeExit(&d,&callbackCall)==STATUS_SUCCESS);
        CHECK(Bc250LifeReleaseHold(&d,&callback)==STATUS_SUCCESS);irql=0;
        if(!a || !b)CHECK(Bc250LifeDetach(&d,&gone)==STATUS_DEVICE_BUSY);
        if(!b){CHECK(Bc250LifeExit(&d,&workerCall)==STATUS_SUCCESS);CHECK(Bc250LifeReleaseHold(&d,&worker)==STATUS_SUCCESS);}
        if(!a){CHECK(Bc250LifeDetach(&d,&gone)==STATUS_DEVICE_BUSY);CHECK(Bc250LifeExit(&d,&dispatch)==STATUS_SUCCESS);}
        DetachLife(&d,&c);
    }
}
static VOID CapacityAndRenewal(VOID)
{
    BC250_LIFE_DOMAIN d;CHILD c;BC250_LIFE_TOKEN holds[8]={0},calls[8]={0},extra={0};
    PVOID p;ULONG round,n;InitLife(&d,&c,200);
    for(round=0;round<1000;++round){
        for(n=0;n<8;++n){
            irql=(KIRQL)(n%3);CHECK(Bc250LifeHold(&d,round+1U,&holds[n])==STATUS_SUCCESS);
            CHECK(Bc250LifeEnterHeld(&d,&holds[n],&calls[n],&p)==STATUS_SUCCESS && p==&c);
        }
        irql=0;CHECK(d.Calls==8 && d.Holds==8);
        CHECK(Bc250LifeEnter(&d,round+1U,&extra,&p)==STATUS_DEVICE_BUSY && !p && LifeZero(&extra,sizeof(extra)));
        CHECK(Bc250LifeClose(&d)==STATUS_SUCCESS);
        for(n=0;n<8;++n){
            CHECK(Bc250LifeReleaseHold(&d,&holds[n])==STATUS_DEVICE_BUSY);
            CHECK(Bc250LifeDetach(&d,&p)==STATUS_DEVICE_BUSY && !p);
            CHECK(Bc250LifeExit(&d,&calls[n])==STATUS_SUCCESS);
            CHECK(Bc250LifeReleaseHold(&d,&holds[n])==STATUS_SUCCESS);
        }
        DetachLife(&d,&c);
        CHECK(Bc250LifePublish(&d,round+1U,round+1U,&c)==STATUS_INVALID_PARAMETER); /* no ABA reuse */
        c.Alive=1;c.BodyActive=0;c.Value=round;
        CHECK(Bc250LifePublish(&d,round+2U,round+2U,&c)==STATUS_SUCCESS);
        CHECK(Bc250LifeEnter(&d,round+1U,&extra,&p)==STATUS_DEVICE_NOT_READY && !p);
    }
    DetachLife(&d,&c);CHECK(d.LastKey==1001 && d.LastGeneration==1001);
}
static VOID IdentityAndUnknown(VOID)
{
    ULONG n;for(n=0;n<5;++n){
        BC250_LIFE_DOMAIN d,other;CHILD c,otherChild;BC250_LIFE_TOKEN h={0},t={0},copy,extra={0};
        PVOID p;InitLife(&d,&c,300+n);InitLife(&other,&otherChild,400+n);
        CHECK(Bc250LifeHold(&d,1,&h)==STATUS_SUCCESS);
        CHECK(Bc250LifeEnterHeld(&d,&h,&t,&p)==STATUS_SUCCESS);
        copy=t;CHECK(Bc250LifeExit(&d,&copy)==STATUS_INVALID_PARAMETER && d.Calls==1);
        CHECK(Bc250LifeExit(&other,&t)==STATUS_INVALID_PARAMETER && d.Calls==1);
        copy=h;CHECK(Bc250LifeEnterHeld(&d,&copy,&extra,&p)==STATUS_INVALID_PARAMETER && !p);
        CHECK(Bc250LifeReleaseHold(&d,&copy)==STATUS_INVALID_PARAMETER && d.Holds==1);
        CHECK(Bc250LifeEnterHeld(&other,&h,&extra,&p)==STATUS_INVALID_PARAMETER && !p && LifeZero(&extra,sizeof(extra)));
        if(n==0)t.Id++;if(n==1)t.Key++;if(n==2)t.HoldId++;
        if(n==3){RtlZeroMemory(&t,sizeof(t));CHECK(Bc250LifeEnter(&d,1,&t,&p)==STATUS_INVALID_PARAMETER && !p);}
        if(n==4){
            h.Id++; /* hold identity lost; independent hold seal still anchors known call cleanup */
            CHECK(Bc250LifeExit(&d,&t)==STATUS_SUCCESS && !d.Calls);
            CHECK(Bc250LifeReleaseHold(&d,&h)==STATUS_INVALID_PARAMETER);
        }else CHECK(Bc250LifeExit(&d,&t)==STATUS_INVALID_PARAMETER);
        CHECK(Bc250LifeClose(&d)==STATUS_SUCCESS);
        CHECK(Bc250LifeDetach(&d,&p)==STATUS_DEVICE_BUSY && !p && d.Calls==(n==4?0U:1U) && d.Holds==1);
        if(n!=4)CHECK(Bc250LifeReleaseHold(&d,&h)==STATUS_DEVICE_BUSY); /* unknown call still quarantined */
        DetachLife(&other,&otherChild);
        /* Do not restore token/guess cleanup; fake child deliberately retained. */
    }
}
static VOID PolicyCountersAndMisuse(VOID)
{
    BC250_LIFE_DOMAIN d;CHILD c;BC250_LIFE_TOKEN t={0},h={0};BC250_LIFE_STATUS status;PVOID p;
    InitLife(&d,&c,500);
    irql=2;CHECK(Bc250LifePublish(&d,2,2,&c)==STATUS_INVALID_DEVICE_STATE);
    CHECK(Bc250LifeDetach(&d,&p)==STATUS_INVALID_DEVICE_STATE && !p);irql=3;
    CHECK(Bc250LifeEnter(&d,1,&t,&p)==STATUS_INVALID_DEVICE_STATE && !p);irql=0;
    Bc250LifeMockAllowed=FALSE;CHECK(Bc250LifeEnter(&d,1,&t,&p)==STATUS_NOT_SUPPORTED && !p);
    Bc250LifeMockAllowed=TRUE;CHECK(Bc250LifeEnter(&d,1,&t,&p)==STATUS_SUCCESS);
    CHECK(Bc250LifeInspect(&d,&status)==STATUS_SUCCESS && status.Calls==1 && status.Holds==0);
    CHECK(Bc250LifeExit(&d,&t)==STATUS_SUCCESS);DetachLife(&d,&c);
    RtlZeroMemory(&d,sizeof(d));c.Alive=1;c.BodyActive=0;
    CHECK(Bc250LifeInit(&d,501)==STATUS_SUCCESS);d.LastId=~(ULONGLONG)0-1U; /* private fresh counter fixture */
    CHECK(Bc250LifePublish(&d,1,1,&c)==STATUS_SUCCESS);
    CHECK(Bc250LifeHold(&d,1,&h)==STATUS_DATA_ERROR && d.Fault && !d.Published);
    CHECK(Bc250LifeDetach(&d,&p)==STATUS_DEVICE_NOT_READY && !p); /* counter fault quarantines */
    {
        BC250_LIFE_DOMAIN bad;CHILD child;BC250_LIFE_TOKEN premature={0};
        InitLife(&bad,&child,502);CHECK(Bc250LifeEnter(&bad,1,&premature,&p)==STATUS_SUCCESS);
        child.BodyActive=1;CHECK(Bc250LifeClose(&bad)==STATUS_SUCCESS);
        CHECK(Bc250LifeExit(&bad,&premature)==STATUS_SUCCESS); /* DELIBERATE contract violation */
        CHECK(Bc250LifeDetach(&bad,&p)==STATUS_SUCCESS && p==&child && child.BodyActive==1);
        /* Negative proof: tracking cannot know body is active after caller drops
         * token too early. No actual free/UAF occurs in this resident fake world. */
    }
}
int main(VOID)
{
    LookupAndReturnBoundary();Continuations();CapacityAndRenewal();IdentityAndUnknown();PolicyCountersAndMisuse();
    CHECK(!held && !irql && !detachHook && bodyCalls>=4 && detachedInsideExit==1);
    printf("PASS: context lifetime %u checks, %u fixtures; stable parent/key lookup/call+continuation holds; no OS parent lifetime/SMP/hardware.\n",checks,fixtures);
    return 0;
}
