/* SPDX-License-Identifier: Apache-2.0 */
#define BC250_PNP_SOURCE_MOCK 1
#define BC250_PNP_CAPTURE_MOCK 1
#define BC250_PCI_TYPES_MOCK 1
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "bc250_pnp_source.c"
#include "../pci-pnp-capture-20261004/bc250_pnp_capture.c"
BOOLEAN Bc250SourceMockAllowed=TRUE,Bc250CaptureMockAllowed=TRUE;
static unsigned checks,held,refs,derefs,waits;
static KIRQL irql;
static ULONG threadId=1;
static PVOID derefOrder[8];
#define CHECK(x) do { ++checks;if(!(x)) { fprintf(stderr,"FAIL line %u: %s\n",(unsigned)__LINE__,#x);exit(1); } } while(0)
unsigned char KeGetCurrentIrql(VOID) { return irql; }
PVOID KeGetCurrentThread(VOID) { return (PVOID)(ULONG_PTR)threadId; }
SIZE_T RtlCompareMemory(const VOID *a,const VOID *b,SIZE_T n)
{ const UCHAR *x=a,*y=b;SIZE_T k;for(k=0;k<n && x[k]==y[k];++k){}return k; }
VOID KeInitializeSpinLock(KSPIN_LOCK *p) { p->Initialized=1;p->Held=0; }
VOID KeAcquireSpinLock(KSPIN_LOCK *p,KIRQL *old)
{ CHECK(p->Initialized && !p->Held && !held && irql<=2);*old=irql;irql=2;p->Held=1;held=1; }
VOID KeReleaseSpinLock(KSPIN_LOCK *p,KIRQL old)
{ CHECK(p->Held && held && irql==2);p->Held=0;held=0;irql=old; }
VOID ExInitializeRundownProtection(PEX_RUNDOWN_REF p)
{ CHECK(!held);p->Count=0;p->Closing=0;p->Initialized=1; }
BOOLEAN ExAcquireRundownProtection(PEX_RUNDOWN_REF p)
{ CHECK(!held && p->Initialized);if(p->Closing)return FALSE;++p->Count;return TRUE; }
VOID ExReleaseRundownProtection(PEX_RUNDOWN_REF p)
{ CHECK(!held && p->Count>0);--p->Count; }
VOID ExWaitForRundownProtectionRelease(PEX_RUNDOWN_REF p)
{ CHECK(!held && irql==0 && p->Initialized && p->Count==0);p->Closing=1;++waits; }
VOID ObReferenceObject(PVOID p)
{ CHECK(!held && irql==0 && p!=NULL);++((PDEVICE_OBJECT)p)->Fake;++refs; }
VOID ObDereferenceObject(PVOID p)
{ CHECK(!held && irql==0 && p!=NULL && ((PDEVICE_OBJECT)p)->Fake>0);--((PDEVICE_OBJECT)p)->Fake;derefOrder[derefs++%8]=p; }
static void Zero(const VOID *p,SIZE_T n)
{ SIZE_T k;const UCHAR *b=p;for(k=0;k<n;++k)CHECK(b[k]==0); }
static BC250_SOURCE_RESOURCES Resources(void)
{
    BC250_SOURCE_RESOURCES r;ULONG n;
    const ULONGLONG base[]={0xc0000000ULL,0xd0000000ULL,0xfe800000ULL};
    const ULONGLONG len[]={0x10000000ULL,0x200000ULL,0x80000ULL};
    const ULONG ord[]={0,2,6};memset(&r,0,sizeof(r));r.DescriptorCount=9;r.MemoryCount=3;
    for(n=0;n<3;++n) { r.Memory[n].Raw=r.Memory[n].Translated=base[n];r.Memory[n].Length=len[n];
        r.Memory[n].Ordinal=ord[n];r.Memory[n].RawFlags=r.Memory[n].TranslatedFlags=n==2?0x80U:0x84U; }
    return r;
}
static void Init(BC250_SOURCE *s,DEVICE_OBJECT *p,DEVICE_OBJECT *l,ULONGLONG scope)
{ memset(s,0,sizeof(*s));p->Fake=l->Fake=0;CHECK(Bc250SourceInit(s,scope,1,p,l)==STATUS_SUCCESS);CHECK(p->Fake==1 && l->Fake==1); }
static void Finish(BC250_SOURCE *s)
{ CHECK(Bc250SourceClose(s)==STATUS_SUCCESS);CHECK(Bc250SourceDrain(s)==STATUS_SUCCESS);CHECK(s->Retirement==2 && !s->Pdo && !s->Lower && s->Rundown.Count==0); }
static void Start(BC250_SOURCE *s)
{
    BC250_SOURCE_REQUEST q={0};BC250_SOURCE_RESOURCES r=Resources();
    CHECK(Bc250SourceBegin(s,1,0,&q)==STATUS_SUCCESS);
    CHECK(Bc250SourceComplete(s,&q,STATUS_SUCCESS,&r)==STATUS_SUCCESS);
    CHECK(SourceGetState(s)==1 && s->Current.PowerState==0);Zero(&q,sizeof(q));
}
static void Ready(BC250_SOURCE *s)
{
    BC250_SOURCE_REQUEST q={0};Start(s);CHECK(Bc250SourceInterlocks(s,1)==STATUS_SUCCESS);
    CHECK(Bc250SourceBegin(s,6,1,&q)==STATUS_SUCCESS);
    CHECK(Bc250SourceComplete(s,&q,STATUS_SUCCESS,NULL)==STATUS_SUCCESS);CHECK(SourceReady(s));
}
static void Blocked(BC250_SOURCE *s,const BC250_SOURCE_LEASE *l)
{
    BC250_CAPTURE_INPUT out;BC250_SOURCE_LEASE newLease={0};memset(&out,0xa5,sizeof(out));
    CHECK(Bc250SourceAcquire(s,&newLease)==STATUS_DEVICE_NOT_READY);Zero(&newLease,sizeof(newLease));
    CHECK(Bc250SourceValidate(s,l,&out)==STATUS_DEVICE_NOT_READY);Zero(&out,sizeof(out));
}
static void Lifecycle(void)
{
    BC250_SOURCE s;DEVICE_OBJECT p,l;BC250_SOURCE_REQUEST q={0};BC250_SOURCE_LEASE a={0},b={0};
    BC250_CAPTURE_INPUT out;BC250_CAPTURE_CACHE cache={0};BC250_CAPTURE_FRAME f;
    unsigned beforeDeref=derefs;ULONG oldEpoch;
    Init(&s,&p,&l,100);Start(&s);CHECK(Bc250SourceAcquire(&s,&a)==STATUS_DEVICE_NOT_READY);
    CHECK(s.Current.InterlocksOff==0);CHECK(Bc250SourceInterlocks(&s,1)==STATUS_SUCCESS);
    CHECK(Bc250SourceBegin(&s,6,1,&q)==STATUS_SUCCESS);CHECK(!SourceReady(&s));
    CHECK(Bc250SourceComplete(&s,&q,STATUS_SUCCESS,NULL)==STATUS_SUCCESS);
    CHECK(Bc250SourceAcquire(&s,&a)==STATUS_SUCCESS);CHECK(s.Rundown.Count==1);
    CHECK(Bc250SourceValidate(&s,&a,&out)==STATUS_SUCCESS && out.SourceEpoch==5);
    CHECK(Bc250CaptureInit(&cache,100)==STATUS_SUCCESS);
    CHECK(Bc250CaptureCommit(&cache,&out)==STATUS_SUCCESS);CHECK(Bc250CaptureRead(&cache,&f)==STATUS_SUCCESS);
    oldEpoch=out.SourceEpoch;
    CHECK(Bc250SourceBegin(&s,2,0,&q)==STATUS_SUCCESS);Blocked(&s,&a);
    CHECK(Bc250SourceComplete(&s,&q,STATUS_SUCCESS,NULL)==STATUS_SUCCESS);CHECK(s.QueryKind==2);
    CHECK(Bc250SourceBegin(&s,4,0,&q)==STATUS_SUCCESS);
    CHECK(Bc250SourceComplete(&s,&q,STATUS_SUCCESS,NULL)==STATUS_SUCCESS);CHECK(SourceReady(&s));
    CHECK(Bc250SourceValidate(&s,&a,&out)==STATUS_DEVICE_NOT_READY);Zero(&out,sizeof(out));
    a.Input=s.Current; /* Attempt to refresh a stale token without reacquisition. */
    CHECK(Bc250SourceValidate(&s,&a,&out)==STATUS_DEVICE_NOT_READY);Zero(&out,sizeof(out));
    CHECK(Bc250SourceAcquire(&s,&b)==STATUS_SUCCESS);CHECK(Bc250SourceValidate(&s,&b,&out)==STATUS_SUCCESS);
    CHECK(out.SourceEpoch>oldEpoch && out.SourceGeneration==1);
    CHECK(Bc250CaptureCommit(&cache,&out)==STATUS_SUCCESS);
    CHECK(Bc250CaptureValidate(&cache,&f)==STATUS_DEVICE_NOT_READY);
    CHECK(Bc250SourceBegin(&s,6,4,&q)==STATUS_SUCCESS);CHECK(Bc250SourceComplete(&s,&q,STATUS_SUCCESS,NULL)==STATUS_SUCCESS);
    Blocked(&s,&b);CHECK(Bc250SourceBegin(&s,6,1,&q)==STATUS_SUCCESS);
    CHECK(Bc250SourceComplete(&s,&q,STATUS_SUCCESS,NULL)==STATUS_SUCCESS);CHECK(SourceReady(&s));
    CHECK(Bc250SourceValidate(&s,&b,&out)==STATUS_DEVICE_NOT_READY);
    irql=2;CHECK(Bc250SourceTransition(&s,7)==STATUS_SUCCESS);CHECK(irql==2);irql=0;CHECK(SourceGetState(&s)==3);
    Start(&s);CHECK(s.Current.PowerState==0);
    CHECK(Bc250SourceRelease(&s,&a)==STATUS_SUCCESS);CHECK(Bc250SourceRelease(&s,&b)==STATUS_SUCCESS);
    Finish(&s);CHECK(p.Fake==0 && l.Fake==0 && derefs==beforeDeref+2);
    CHECK(derefOrder[beforeDeref%8]==&l && derefOrder[(beforeDeref+1)%8]==&p);
    CHECK(Bc250SourceInit(&s,101,2,&p,&l)==STATUS_INVALID_PARAMETER);
}
static void PendingAndFailures(void)
{
    unsigned n;
    for(n=0;n<8;++n) {
        BC250_SOURCE s;DEVICE_OBJECT p,l;BC250_SOURCE_REQUEST q={0};BC250_SOURCE_LEASE lease={0};
        BC250_CAPTURE_INPUT out;BC250_SOURCE_RESOURCES r=Resources();unsigned d=derefs;
        Init(&s,&p,&l,200+n);Ready(&s);CHECK(Bc250SourceAcquire(&s,&lease)==STATUS_SUCCESS);
        if(n<4) {
            CHECK(Bc250SourceBegin(&s,n<2?2U:3U,0,&q)==STATUS_SUCCESS);
            CHECK(Bc250SourceComplete(&s,&q,STATUS_CANCELLED,NULL)==STATUS_SUCCESS);
            CHECK(SourceReady(&s) && s.QueryKind==0);CHECK(Bc250SourceValidate(&s,&lease,&out)==STATUS_DEVICE_NOT_READY);
        } else {
            CHECK(Bc250SourceBegin(&s,6,1,&q)==STATUS_SUCCESS);
            CHECK(Bc250SourceComplete(&s,&q,STATUS_PENDING,NULL)==STATUS_DEVICE_BUSY);
            CHECK(s.PendingAddress==&q && s.Rundown.Count==2);Blocked(&s,&lease);
            if(n==4) { CHECK(Bc250SourceComplete(&s,&q,STATUS_CANCELLED,NULL)==STATUS_SUCCESS);CHECK(s.Current.PowerState==0); }
            if(n==5) {
                CHECK(Bc250SourceClose(&s)==STATUS_SUCCESS);CHECK(s.PendingAddress==&q);
                CHECK(Bc250SourceDrain(&s)==STATUS_DEVICE_BUSY && derefs==d);
                threadId=2;CHECK(Bc250SourceComplete(&s,&q,STATUS_SUCCESS,&r)==STATUS_DEVICE_NOT_READY);threadId=1;
                CHECK(!SourceReady(&s) && s.Rundown.Count==1);
            }
            if(n==6) {
                CHECK(Bc250SourceInterlocks(&s,0)==STATUS_SUCCESS);
                CHECK(Bc250SourceComplete(&s,&q,STATUS_SUCCESS,NULL)==STATUS_DEVICE_NOT_READY);
                CHECK(s.Current.InterlocksOff==0 && s.Current.PowerState==0);
            }
            if(n==7) {
                CHECK(Bc250SourceTransition(&s,8)==STATUS_SUCCESS);
                CHECK(Bc250SourceComplete(&s,&q,STATUS_SUCCESS,NULL)==STATUS_DEVICE_NOT_READY);
                CHECK(SourceGetState(&s)==5);
            }
        }
        CHECK(Bc250SourceRelease(&s,&lease)==STATUS_SUCCESS);Finish(&s);
    }
}
static void Faults(void)
{
    unsigned n;
    for(n=0;n<9;++n) {
        BC250_SOURCE s;DEVICE_OBJECT p,l;BC250_SOURCE_REQUEST q={0},other={0};
        BC250_SOURCE_LEASE lease={0};BC250_SOURCE_RESOURCES r=Resources();unsigned d=derefs;
        Init(&s,&p,&l,300+n);
        if(n<4) {
            CHECK(Bc250SourceBegin(&s,1,0,&q)==STATUS_SUCCESS);
            if(n==0)r.Memory[0].RawFlags=0x10084;
            if(n==1)r.Memory[1].Raw=r.Memory[0].Raw+4;
            if(n==2)r.Memory[3].Reserved=1;
            if(n==3)r.MemoryCount=9;
            CHECK(Bc250SourceComplete(&s,&q,STATUS_SUCCESS,&r)==STATUS_DATA_ERROR);
        } else {
            Ready(&s);CHECK(Bc250SourceAcquire(&s,&lease)==STATUS_SUCCESS);
            if(n==4) {
                CHECK(Bc250SourceBegin(&s,6,1,&q)==STATUS_SUCCESS);
                CHECK(Bc250SourceBegin(&s,2,0,&other)==STATUS_DEVICE_BUSY);
                CHECK(s.PendingAddress==&q && s.Rundown.Count==2);Zero(&other,sizeof(other));
                CHECK(Bc250SourceComplete(&s,&q,STATUS_SUCCESS,NULL)==STATUS_DEVICE_NOT_READY);
            }
            if(n==5 || n==6) {
                CHECK(Bc250SourceBegin(&s,2,0,&q)==STATUS_SUCCESS);
                CHECK(Bc250SourceComplete(&s,&q,n==5?(NTSTATUS)0x80000005U:(NTSTATUS)1,NULL)==STATUS_DATA_ERROR);
            }
            if(n==7) { s.Current.SourceEpoch=BC250_CAPTURE_COUNTER_LIMIT-1U;CHECK(Bc250SourceBegin(&s,6,1,&q)==STATUS_INVALID_DEVICE_STATE); }
            if(n==8) { s.LastId=~(ULONGLONG)0-1U;CHECK(Bc250SourceBegin(&s,2,0,&q)==STATUS_INVALID_DEVICE_STATE); }
            Blocked(&s,&lease);CHECK(Bc250SourceRelease(&s,&lease)==STATUS_SUCCESS);
        }
        CHECK(s.Fault && s.Closing && !s.PendingAddress && s.Rundown.Count==0);
        CHECK(Bc250SourceDrain(&s)==STATUS_DATA_ERROR && derefs==d && p.Fake==1 && l.Fake==1);
        CHECK(Bc250SourceInit(&s,400+n,2,&p,&l)==STATUS_INVALID_PARAMETER);
        /* Fake quarantine deliberately retains2 refs; not native allocation. */
    }
}
static void IdentityAndCapacity(void)
{
    BC250_SOURCE s,d;DEVICE_OBJECT p,l,p2,l2;BC250_SOURCE_LEASE leases[17]={0},copy;
    BC250_CAPTURE_INPUT out;BC250_SOURCE_REQUEST q={0},copyQ;unsigned n,count,refBefore=refs;
    memset(&s,0,sizeof(s));p.Fake=0;
    CHECK(Bc250SourceInit(&s,1,0,&p,&p)==STATUS_INVALID_PARAMETER && refs==refBefore);
    CHECK(Bc250SourceInit(&s,1,1,&p,NULL)==STATUS_INVALID_PARAMETER && refs==refBefore);
    CHECK(Bc250SourceInit(&s,1,1,&p,&p)==STATUS_SUCCESS && p.Fake==2);Finish(&s);CHECK(p.Fake==0);
    Init(&s,&p,&l,500);Init(&d,&p2,&l2,501);Ready(&s);
    for(n=0;n<16;++n)CHECK(Bc250SourceAcquire(&s,&leases[n])==STATUS_SUCCESS);
    copy=leases[0];memset(&leases[0],0,sizeof(leases[0])); /* hostile fixture only */
    CHECK(Bc250SourceAcquire(&s,&leases[0])==STATUS_INVALID_PARAMETER && s.Rundown.Count==16);
    leases[0]=copy; /* Restore exact live identity for known cleanup. */
    CHECK(Bc250SourceAcquire(&s,&leases[16])==STATUS_INSUFFICIENT_RESOURCES && s.Rundown.Count==16);
    copy=leases[0];CHECK(Bc250SourceRelease(&s,&copy)==STATUS_INVALID_PARAMETER && s.Rundown.Count==16);
    CHECK(Bc250SourceValidate(&d,&leases[0],&out)==STATUS_DEVICE_NOT_READY);Zero(&out,sizeof(out));
    threadId=2;CHECK(Bc250SourceRelease(&s,&leases[0])==STATUS_INVALID_PARAMETER);threadId=1;
    leases[0].Id++;CHECK(Bc250SourceRelease(&s,&leases[0])==STATUS_INVALID_PARAMETER);leases[0].Id--;
    leases[0].Input.PowerState=4;CHECK(Bc250SourceValidate(&s,&leases[0],&out)==STATUS_DEVICE_NOT_READY);Zero(&out,sizeof(out));
    CHECK(Bc250SourceRelease(&s,&leases[0])==STATUS_SUCCESS); /* trusted identity, not payload */
    CHECK(Bc250SourceBegin(&s,6,1,&q)==STATUS_SUCCESS);copyQ=q;count=s.Rundown.Count;
    CHECK(Bc250SourceComplete(&s,&copyQ,STATUS_SUCCESS,NULL)==STATUS_INVALID_PARAMETER && s.Rundown.Count==count);
    q.Id++;CHECK(Bc250SourceComplete(&s,&q,STATUS_SUCCESS,NULL)==STATUS_INVALID_PARAMETER);q.Id--;
    CHECK(Bc250SourceClose(&s)==STATUS_SUCCESS);CHECK(Bc250SourceDrain(&s)==STATUS_DEVICE_BUSY);
    CHECK(Bc250SourceComplete(&s,&q,STATUS_SUCCESS,NULL)==STATUS_DEVICE_NOT_READY);
    CHECK(Bc250SourceComplete(&s,&q,STATUS_SUCCESS,NULL)==STATUS_INVALID_PARAMETER);
    CHECK(Bc250SourceDrain(&s)==STATUS_DEVICE_BUSY);
    for(n=1;n<16;++n)CHECK(Bc250SourceRelease(&s,&leases[n])==STATUS_SUCCESS);
    Finish(&s);Finish(&d);
}
static void TransitionMatrix(void)
{
    ULONG state,kind;
    for(state=0;state<7;++state)for(kind=1;kind<=6;++kind) {
        BC250_SOURCE s;DEVICE_OBJECT p,l;BC250_SOURCE_REQUEST q={0};BC250_SOURCE_RESOURCES r=Resources();
        BOOLEAN allowed=(kind==1 && (state==0 || state==3)) ||
            ((kind==2 || kind==3 || kind==6) && state==1) || (kind==4 && state==2) || (kind==5 && state==4);
        Init(&s,&p,&l,600+state*6+kind);
        /* Synthetic state fixtures, NOT an OS IRP execution. */
        SourceState(&s,state);s.Current.SourceEpoch=1;s.QueryKind=state==2?2U:state==4?3U:0U;
        CHECK(Bc250SourceBegin(&s,kind,kind==6?1U:0U,&q)==(allowed?STATUS_SUCCESS:STATUS_INVALID_DEVICE_STATE));
        if(allowed) { CHECK(Bc250SourceComplete(&s,&q,STATUS_SUCCESS,kind==1?&r:NULL)==STATUS_SUCCESS);Finish(&s); }
        else { CHECK(s.Fault && !s.PendingAddress);CHECK(Bc250SourceDrain(&s)==STATUS_DATA_ERROR); }
    }
    {
        BC250_SOURCE s;DEVICE_OBJECT p,l;BC250_SOURCE_REQUEST q={0};Init(&s,&p,&l,700);
        irql=2;CHECK(Bc250SourceBegin(&s,1,0,&q)==STATUS_INVALID_DEVICE_STATE);CHECK(!s.PendingAddress);irql=0;
        CHECK(Bc250SourceBegin(&s,1,0,&q)==STATUS_SUCCESS);
        CHECK(Bc250SourceComplete(&s,&q,STATUS_CANCELLED,NULL)==STATUS_SUCCESS);
        CHECK(SourceGetState(&s)==0 && s.Current.PowerState==0);Finish(&s);
    }
}
int main(VOID)
{
    Lifecycle();PendingAndFailures();Faults();IdentityAndCapacity();TransitionMatrix();
    CHECK(irql==0 && !held && waits!=0);
    printf("PASS: source producer %u checks; coherent writer/lease/completion model, 42 transitions; no real IRP/SMP/hardware.\n",checks);return 0;
}
