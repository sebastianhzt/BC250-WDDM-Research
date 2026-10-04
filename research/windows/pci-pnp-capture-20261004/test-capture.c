/* SPDX-License-Identifier: Apache-2.0 */
#define BC250_PNP_CAPTURE_MOCK 1
#define BC250_PCI_TYPES_MOCK 1
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "bc250_pnp_capture.c"
BOOLEAN Bc250CaptureMockAllowed=TRUE;
static unsigned checks,locks;
static KIRQL irql;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"FAIL line %u: %s\n",(unsigned)__LINE__,#x);exit(1); } } while(0)
unsigned char KeGetCurrentIrql(VOID) { return irql; }
SIZE_T RtlCompareMemory(const VOID *a,const VOID *b,SIZE_T n)
{ SIZE_T k;const UCHAR *x=a,*y=b;for(k=0;k<n && x[k]==y[k];++k){}return k; }
VOID KeInitializeSpinLock(KSPIN_LOCK *p) { p->Initialized=1;p->Held=0; }
VOID KeAcquireSpinLock(KSPIN_LOCK *p,KIRQL *old)
{ CHECK(p->Initialized && !p->Held && irql==0);*old=irql;irql=2;p->Held=1;++locks; }
VOID KeReleaseSpinLock(KSPIN_LOCK *p,KIRQL old)
{ CHECK(p->Held && irql==2 && old==0);p->Held=0;irql=old; }
static void W32(UCHAR *p,ULONG v)
{ unsigned n;for(n=0;n<4;++n)p[n]=(UCHAR)(v>>(n*8)); }
static void W64(UCHAR *p,ULONGLONG v)
{ W32(p,(ULONG)v);W32(p+4,(ULONG)(v>>32)); }
static BC250_CAPTURE_INPUT Input(ULONG state,ULONG power,ULONG off,ULONG snapshot)
{
    BC250_CAPTURE_INPUT i;unsigned n;
    const ULONGLONG bases[]={0xc0000000ULL,0xd0000000ULL,0xfe800000ULL};
    const ULONGLONG lengths[]={0x10000000ULL,0x200000ULL,0x80000ULL};
    const ULONG ordinals[]={0,2,6},flags[]={0x84,0x84,0x80};
    memset(&i,0,sizeof(i));i.Version=1;i.StructSize=sizeof(i);
    i.SourceGeneration=1;i.SourceEpoch=1;i.PowerState=power;i.InterlocksOff=off;
    W32(i.Wire+BC250_CW_VERSION,2);W32(i.Wire+BC250_CW_SIZE,360);
    W32(i.Wire+BC250_CW_BUILD,22);W32(i.Wire+BC250_CW_STATUS,(ULONG)STATUS_DEVICE_NOT_READY);
    W32(i.Wire+BC250_CW_BLOCKERS,0x1cU|(snapshot==0?1U:snapshot==2?2U:0U));
    W32(i.Wire+BC250_CW_STATE,state);W32(i.Wire+BC250_CW_SNAPSHOT,snapshot);
    W32(i.Wire+BC250_CW_DESCRIPTORS,9);W32(i.Wire+BC250_CW_COUNT,snapshot==1?3U:0U);
    W32(i.Wire+BC250_CW_GENERATION,1);
    if(snapshot==1)for(n=0;n<3;++n) {
        UCHAR *m=i.Wire+BC250_CW_MEMORY+n*BC250_CW_ENTRY_BYTES;
        W64(m+BC250_CW_RAW,bases[n]);W64(m+BC250_CW_TRANSLATED,bases[n]);
        W64(m+BC250_CW_LENGTH,lengths[n]);W32(m+BC250_CW_RAW_FLAGS,flags[n]);
        W32(m+BC250_CW_TRANSLATED_FLAGS,flags[n]);W32(m+BC250_CW_ORDINAL,ordinals[n]);
    }
    return i; /* Synthetic values shaped like evidence; NOT a live observation. */
}
static void Init(BC250_CAPTURE_CACHE *c,ULONGLONG scope)
{ memset(c,0,sizeof(*c));CHECK(Bc250CaptureInit(c,scope)==STATUS_SUCCESS); }
static void Zero(const VOID *p,SIZE_T n)
{ const UCHAR *b=p;SIZE_T k;for(k=0;k<n;++k)CHECK(b[k]==0); }
static void Unready(BC250_CAPTURE_CACHE *c,const BC250_CAPTURE_FRAME *old)
{
    BC250_CAPTURE_FRAME f;BC250_PCI_RESOURCES r;
    memset(&f,0xa5,sizeof(f));memset(&r,0xa5,sizeof(r));
    CHECK(Bc250CaptureRead(c,&f)==STATUS_DEVICE_NOT_READY);Zero(&f,sizeof(f));
    CHECK(Bc250CaptureValidate(c,old)==STATUS_DEVICE_NOT_READY);
    CHECK(Bc250CaptureResources(c,old,&r)==STATUS_DEVICE_NOT_READY);Zero(&r,sizeof(r));
}
static void Matrix(void)
{
    ULONG s,p,o,v;ULONGLONG scope=10;
    for(s=0;s<7;++s)for(p=0;p<5;++p)for(o=0;o<2;++o)for(v=0;v<3;++v) {
        BC250_CAPTURE_CACHE c;BC250_CAPTURE_FRAME f;BC250_PCI_RESOURCES r;
        BC250_CAPTURE_INPUT i=Input(s,p,o,v);BOOLEAN ready=s==1 && p==1 && o==1 && v==1;
        Init(&c,scope++);CHECK(Bc250CaptureCommit(&c,&i)==STATUS_SUCCESS);
        CHECK(c.Revision==1 && c.Present && !c.Fault);
        memset(&f,0xa5,sizeof(f));
        CHECK(Bc250CaptureRead(&c,&f)==(ready?STATUS_SUCCESS:STATUS_DEVICE_NOT_READY));
        if(ready) {
            CHECK(Bc250CaptureValidate(&c,&f)==STATUS_SUCCESS);
            CHECK(Bc250CaptureResources(&c,&f,&r)==STATUS_SUCCESS);
            CHECK(r.Generation==1 && r.Epoch==1 && r.MemoryCount==3 && r.Valid==1);
            CHECK(r.Memory[1].Ordinal==2 && r.Memory[1].RawFlags==0x84);
        } else Zero(&f,sizeof(f));
    }
}
static void Transitions(void)
{
    BC250_CAPTURE_CACHE c,d;BC250_CAPTURE_INPUT i=Input(1,1,1,1);
    BC250_CAPTURE_FRAME a,b,t;BC250_PCI_RESOURCES r;
    Init(&c,1000);Init(&d,1001);CHECK(Bc250CaptureCommit(&c,&i)==STATUS_SUCCESS);
    CHECK(Bc250CaptureRead(&c,&a)==STATUS_SUCCESS);
    i.SourceEpoch=2;W32(i.Wire+BC250_CW_STATE,2);
    CHECK(Bc250CaptureCommit(&c,&i)==STATUS_SUCCESS);Unready(&c,&a);
    i.SourceEpoch=3;W32(i.Wire+BC250_CW_STATE,1);
    CHECK(Bc250CaptureCommit(&c,&i)==STATUS_SUCCESS);
    CHECK(Bc250CaptureRead(&c,&b)==STATUS_SUCCESS);
    CHECK(a.Input.SourceGeneration==b.Input.SourceGeneration && b.Input.SourceEpoch==3);
    CHECK(Bc250CaptureValidate(&c,&a)==STATUS_DEVICE_NOT_READY);
    CHECK(b.Resources.Generation>a.Resources.Generation && b.Resources.Epoch>a.Resources.Epoch);
    i.SourceEpoch=4;i.PowerState=4;CHECK(Bc250CaptureCommit(&c,&i)==STATUS_SUCCESS);Unready(&c,&b);
    i.SourceEpoch=5;i.PowerState=1;CHECK(Bc250CaptureCommit(&c,&i)==STATUS_SUCCESS);
    CHECK(Bc250CaptureRead(&c,&b)==STATUS_SUCCESS && b.Revision==5);
    i.SourceGeneration=2;i.SourceEpoch=1;W32(i.Wire+BC250_CW_GENERATION,2);
    CHECK(Bc250CaptureCommit(&c,&i)==STATUS_SUCCESS);
    CHECK(Bc250CaptureRead(&c,&b)==STATUS_SUCCESS && b.Revision==6);
    CHECK(Bc250CaptureCommit(&d,&i)==STATUS_SUCCESS);
    CHECK(Bc250CaptureValidate(&d,&b)==STATUS_DEVICE_NOT_READY);
    t=b;t.Revision--;CHECK(Bc250CaptureValidate(&c,&t)==STATUS_DEVICE_NOT_READY);
    t=b;t.ScopeId++;CHECK(Bc250CaptureValidate(&c,&t)==STATUS_DEVICE_NOT_READY);
    t=b;t.Input.Wire[BC250_CW_MEMORY]^=1;CHECK(Bc250CaptureValidate(&c,&t)==STATUS_DEVICE_NOT_READY);
    t=b;t.Resources.Memory[0].RawBase++;
    memset(&r,0xa5,sizeof(r));CHECK(Bc250CaptureResources(&c,&t,&r)==STATUS_DEVICE_NOT_READY);Zero(&r,sizeof(r));
    CHECK(Bc250CaptureResources(&c,&b,&r)==STATUS_SUCCESS);
    CHECK(memcmp(&r,&c.Current.Resources,sizeof(r))==0);
    /* Replay even the identical tuple permanently poisons this cache. */
    CHECK(Bc250CaptureCommit(&c,&i)==STATUS_INVALID_DEVICE_STATE);CHECK(c.Fault && !c.Present);
    Unready(&c,&b);i.SourceEpoch=2;
    CHECK(Bc250CaptureCommit(&c,&i)==STATUS_INVALID_DEVICE_STATE);
    CHECK(Bc250CaptureInit(&c,1002)==STATUS_INVALID_PARAMETER);
}
static void Malformed(void)
{
    unsigned n;
    for(n=0;n<35;++n) {
        BC250_CAPTURE_CACHE c;BC250_CAPTURE_FRAME f;
        BC250_CAPTURE_INPUT i=Input(1,1,1,1);UCHAR *m=i.Wire+BC250_CW_MEMORY;
        Init(&c,2000+n);CHECK(Bc250CaptureCommit(&c,&i)==STATUS_SUCCESS);
        CHECK(Bc250CaptureRead(&c,&f)==STATUS_SUCCESS);i.SourceEpoch=2;
        switch(n) {
        case 0:i.Version=0;break;case 1:i.StructSize--;break;
        case 2:i.SourceGeneration=0;break;case 3:i.SourceGeneration=BC250_CAPTURE_COUNTER_LIMIT;break;
        case 4:i.SourceEpoch=BC250_CAPTURE_COUNTER_LIMIT;break;case 5:i.PowerState=5;break;
        case 6:i.InterlocksOff=2;break;case 7:W32(i.Wire+BC250_CW_VERSION,1);break;
        case 8:W32(i.Wire+BC250_CW_SIZE,359);break;case 9:W32(i.Wire+BC250_CW_BUILD,21);break;
        case 10:W32(i.Wire+BC250_CW_STATUS,0);break;case 11:W32(i.Wire+BC250_CW_GENERATION,2);break;
        case 12:W32(i.Wire+BC250_CW_STATE,7);break;case 13:i.SourceEpoch=0;break;
        case 14:W32(i.Wire+BC250_CW_BLOCKERS,0x18);break;case 15:W32(i.Wire+BC250_CW_BLOCKERS,0x3c);break;
        case 16:W32(i.Wire+BC250_CW_SNAPSHOT,3);break;case 17:W32(i.Wire+BC250_CW_DESCRIPTORS,33);break;
        case 18:W32(i.Wire+BC250_CW_COUNT,9);break;case 19:W32(i.Wire+BC250_CW_DESCRIPTORS,0);break;
        case 20:W32(i.Wire+BC250_CW_BLOCKERS,0x1d);break;case 21:W32(m+BC250_CW_RAW_FLAGS,0x10084);break;
        case 22:W32(m+BC250_CW_TRANSLATED_FLAGS,0x10080);break;case 23:W32(m+BC250_CW_RESERVED,1);break;
        case 24:W32(m+BC250_CW_ORDINAL,9);break;
        case 25:W32(m+BC250_CW_ENTRY_BYTES+BC250_CW_ORDINAL,0);break;
        case 26:W64(m+BC250_CW_RAW,0);break;case 27:W64(m+BC250_CW_LENGTH,0);break;
        case 28:W64(m+BC250_CW_LENGTH,0x8000000000000000ULL);break;
        case 29:W64(m+BC250_CW_RAW,0x7fffffffffffffffULL);break;
        case 30:W64(m+BC250_CW_TRANSLATED,0x8000000000000000ULL);break;
        case 31:W64(m+BC250_CW_ENTRY_BYTES+BC250_CW_RAW,0xc0001000ULL);break;
        case 32:W64(m+BC250_CW_ENTRY_BYTES+BC250_CW_TRANSLATED,0xc0001000ULL);break;
        case 33:m[3*BC250_CW_ENTRY_BYTES]=1;break;
        case 34:W32(i.Wire+BC250_CW_SNAPSHOT,0);break;
        }
        CHECK(Bc250CaptureCommit(&c,&i)==STATUS_DATA_ERROR);
        CHECK(c.Fault && !c.Present);Unready(&c,&f);
        i=Input(1,1,1,1);i.SourceEpoch=3;
        CHECK(Bc250CaptureCommit(&c,&i)==STATUS_INVALID_DEVICE_STATE);
    }
}
static void Boundaries(void)
{
    BC250_CAPTURE_CACHE c;BC250_CAPTURE_FRAME f;BC250_CAPTURE_INPUT i=Input(1,1,1,1);
    Init(&c,3000);CHECK(Bc250CaptureCommit(&c,&i)==STATUS_SUCCESS);CHECK(Bc250CaptureRead(&c,&f)==STATUS_SUCCESS);
    c.Revision=~(ULONGLONG)0-1U;i.SourceEpoch=2;
    CHECK(Bc250CaptureCommit(&c,&i)==STATUS_INVALID_DEVICE_STATE);Unready(&c,&f);
    CHECK(c.Revision==~(ULONGLONG)0-1U && c.Fault);
    Init(&c,3001);CHECK(Bc250CaptureCommit(&c,&i)==STATUS_SUCCESS);CHECK(Bc250CaptureRead(&c,&f)==STATUS_SUCCESS);
    CHECK(Bc250CaptureCommit(&c,NULL)==STATUS_INVALID_PARAMETER);Unready(&c,&f);
    Init(&c,3002);i=Input(0,0,1,0);i.SourceEpoch=0;
    CHECK(Bc250CaptureCommit(&c,&i)==STATUS_SUCCESS);CHECK(!CaptureReady(&c.Current));
    Init(&c,3003);i=Input(1,1,1,1);i.SourceGeneration=2;W32(i.Wire+BC250_CW_GENERATION,2);
    CHECK(Bc250CaptureCommit(&c,&i)==STATUS_SUCCESS);CHECK(Bc250CaptureRead(&c,&f)==STATUS_SUCCESS);
    i.SourceGeneration=1;i.SourceEpoch=2;W32(i.Wire+BC250_CW_GENERATION,1);
    CHECK(Bc250CaptureCommit(&c,&i)==STATUS_INVALID_DEVICE_STATE);Unready(&c,&f);
    /* Legacy resource bytes alone cannot supply missing source epoch/power. */
    Init(&c,3004);i=Input(1,1,1,1);i.SourceEpoch=0;i.PowerState=0;
    CHECK(Bc250CaptureCommit(&c,&i)==STATUS_DATA_ERROR);
    /* Logical split-read counterexample, not a Windows/SMP execution test. */
    i=Input(1,1,1,1);W32(i.Wire+BC250_CW_STATE,2);i.SourceEpoch=1;
    CHECK(Wire32(i.Wire+BC250_CW_GENERATION)==i.SourceGeneration);
    CHECK(Wire32(i.Wire+BC250_CW_STATE)==2 && i.SourceEpoch==1);
    /* Cache lock cannot detect stale epoch fabricated from another query.
       Only a coherent source producer can prevent this tuple upstream. */
    Init(&c,3005);CHECK(Bc250CaptureCommit(&c,&i)==STATUS_SUCCESS);CHECK(!CaptureReady(&c.Current));
    irql=1;CHECK(Bc250CaptureRead(&c,&f)==STATUS_INVALID_DEVICE_STATE);Zero(&f,sizeof(f));irql=0;
    memset(&c,0,sizeof(c));CHECK(Bc250CaptureInit(&c,0)==STATUS_INVALID_PARAMETER);
    CHECK(Bc250CaptureInit(&c,~(ULONGLONG)0)==STATUS_INVALID_PARAMETER);
    CHECK(Bc250CaptureInit(NULL,5)==STATUS_INVALID_PARAMETER);
}
int main(VOID)
{
    Matrix();Transitions();Malformed();Boundaries();CHECK(irql==0 && locks!=0);
    printf("PASS: capture model %u checks; 210 metadata combinations, poison/ABA/power/ABI-shape. No hardware or real SMP.\n",checks);
    return 0;
}
