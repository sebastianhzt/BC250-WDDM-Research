/* SPDX-License-Identifier: Apache-2.0 */
#define BC250_PCI_CONTRACT_MOCK
#include <stdio.h>
#include <stdlib.h>
#include "bc250_pci_contract.c"
static unsigned checks;
#define CHECK(c) do { ++checks; if(!(c)){fprintf(stderr,"FAIL line %d\n",__LINE__);exit(2);} } while(0)
typedef struct FAKE {
    BC250_PCI_SESSION Session;
    BC250_PCI_OPS Ops;
    BC250_PCI_OBSERVATION Output;
    BC250_PCI_RESOURCES Resources;
    UCHAR Header[64];
    unsigned LeaseRefs, BusRefs, Acquires, Queries, Reads, Drops, Releases, References, Forbidden;
    unsigned Event, Mutation, CheckCalls, ShortLength, Malformed;
    NTSTATUS AcquireStatus, QueryStatus;
} FAKE;
static int Empty(const VOID *p,size_t n)
{
    const UCHAR *bytes=(const UCHAR *)p;
    size_t i;
    for(i=0;i<n;++i) if(bytes[i])return 0;
    return 1;
}
static VOID Mutate(FAKE *f,unsigned point)
{
    if(f->Event==point) f->Mutation=1; /* including simulated STOP->START ABA */
}
static NTSTATUS Acquire(PVOID c,BC250_PCI_LEASE *lease)
{
    FAKE *f=(FAKE *)c; ++f->Acquires;
    Mutate(f,1);
    if(f->AcquireStatus!=0) return f->AcquireStatus;
    ++f->LeaseRefs;
    lease->Anchor=f;
    lease->Generation=1;
    lease->Epoch=7;
    if(f->Malformed==8) lease->Epoch=~(ULONGLONG)0;
    return 0;
}
static BOOLEAN Still(PVOID c,const BC250_PCI_LEASE *lease)
{
    FAKE *f=(FAKE *)c;
    CHECK(f->LeaseRefs==1 && lease->Anchor==f);
    ++f->CheckCalls;
    Mutate(f,10+f->CheckCalls);
    return f->Mutation ? FALSE:TRUE;
}
static VOID Release(PVOID c,BC250_PCI_LEASE *lease)
{
    FAKE *f=(FAKE *)c;
    CHECK(f->BusRefs==0 && f->LeaseRefs==1 && lease->Anchor==f);
    --f->LeaseRefs; ++f->Releases;
    CHECK(Bc250PciObserve(&f->Session,&f->Output,0)==STATUS_DEVICE_BUSY);
    CHECK(Empty(&f->Output,sizeof(f->Output)));
}
static VOID Reference(PVOID c)
{
    FAKE *f=(FAKE *)c; ++f->References; ++f->BusRefs;
}
static VOID Dereference(PVOID c)
{
    FAKE *f=(FAKE *)c;
    CHECK(f->LeaseRefs==1 && f->BusRefs==1);
    Mutate(f,4);
    --f->BusRefs; ++f->Drops;
    CHECK(Bc250PciObserve(&f->Session,&f->Output,0)==STATUS_DEVICE_BUSY);
}
static ULONG Read(PVOID c,ULONG space,PVOID buffer,ULONG offset,ULONG length)
{
    FAKE *f=(FAKE *)c;
    CHECK(f->LeaseRefs==1 && f->BusRefs==1 && space==0 && offset==0 && length==64);
    ++f->Reads; Mutate(f,3);
    RtlCopyMemory(buffer,f->Header,64); /* even short return must not publish */
    CHECK(Bc250PciObserve(&f->Session,&f->Output,0)==STATUS_DEVICE_BUSY);
    return f->ShortLength;
}
static ULONG ForbiddenData(PVOID c,ULONG space,PVOID b,ULONG off,ULONG n)
{
    FAKE *f=(FAKE *)c;
    (void)space;(void)b;(void)off;(void)n;++f->Forbidden; return 0;
}
static PVOID ForbiddenDma(PVOID c,PVOID d,ULONG *n)
{
    FAKE *f=(FAKE *)c;(void)d;(void)n;++f->Forbidden;return NULL;
}
static BOOLEAN ForbiddenTranslate(PVOID c,ULONGLONG a,ULONG *s,ULONGLONG *b)
{
    FAKE *f=(FAKE *)c;(void)a;(void)s;(void)b;++f->Forbidden;return FALSE;
}
static NTSTATUS Query(PVOID c,const BC250_PCI_LEASE *lease,BUS_INTERFACE_STANDARD *bus)
{
    FAKE *f=(FAKE *)c;
    CHECK(f->LeaseRefs==1 && lease->Anchor==f && Empty(bus,sizeof(*bus)));
    ++f->Queries; Mutate(f,2);
    if(f->QueryStatus!=0) {
        if(f->Malformed==7) bus->Version=1;
        return f->QueryStatus;
    }
    ++f->BusRefs; /* already referenced by fake provider; no Reference caller */
    bus->Size=(USHORT)sizeof(*bus);bus->Version=1;bus->Context=f;
    bus->InterfaceReference=Reference;bus->InterfaceDereference=Dereference;
    bus->GetBusData=Read;bus->SetBusData=ForbiddenData;
    bus->GetDmaAdapter=ForbiddenDma;bus->TranslateBusAddress=ForbiddenTranslate;
    switch(f->Malformed){
    case 1:bus->Size=0;break;
    case 2:bus->Version=2;break;
    case 3:bus->InterfaceReference=NULL;break;
    case 4:bus->GetBusData=NULL;break;
    case 5:bus->InterfaceDereference=NULL;break;
    default:break;
    }
    return 0;
}
static VOID Put32(UCHAR *h,unsigned offset,ULONG value)
{
    unsigned j;
    for(j=0;j<4;++j)h[offset+j]=(UCHAR)(value>>(8*j));
}
static VOID Setup(FAKE *f)
{
    RtlZeroMemory(f,sizeof(*f));
    f->Ops.Context=f; f->Ops.Acquire=Acquire; f->Ops.Release=Release;
    f->Ops.StillStarted=Still;f->Ops.Query=Query;
    f->Header[0]=2;f->Header[1]=0x10;f->Header[2]=0xfe;f->Header[3]=0x13;f->Header[11]=3;
    Put32(f->Header,16,0xc000000cU); /* BAR0 64-bit; BAR1 high=0 */
    Put32(f->Header,24,0xd000000cU); /* BAR2 64-bit; BAR3 high=0 */
    Put32(f->Header,32,0xef01);     /* BAR4 IO, deliberately no correlation */
    Put32(f->Header,36,0xfe800000U);/* BAR5 MEM32 */
    f->ShortLength=64;
    f->Resources.Generation=1;f->Resources.Epoch=7;
    f->Resources.Started=1;f->Resources.Valid=1;
    f->Resources.DescriptorCount=9;f->Resources.MemoryCount=3;
    f->Resources.Memory[0].RawBase=0xc0000000U;f->Resources.Memory[0].Length=0x10000000;
    f->Resources.Memory[1].RawBase=0xd0000000U;f->Resources.Memory[1].Length=0x200000;
    f->Resources.Memory[2].RawBase=0xfe800000U;f->Resources.Memory[2].Length=0x80000;
    f->Resources.Memory[0].Ordinal=0;f->Resources.Memory[1].Ordinal=2;f->Resources.Memory[2].Ordinal=6;
    f->Resources.Memory[0].RawFlags=0x84;f->Resources.Memory[1].RawFlags=0x84;f->Resources.Memory[2].RawFlags=0x80;
    f->Resources.Memory[0].TranslatedFlags=0x84;f->Resources.Memory[1].TranslatedFlags=0x84;f->Resources.Memory[2].TranslatedFlags=0x80;
    {unsigned j;for(j=0;j<3;++j)f->Resources.Memory[j].TranslatedBase=f->Resources.Memory[j].RawBase;}
    CHECK(Bc250PciSessionInit(&f->Session,&f->Ops,0)==0);
}
static VOID One(FAKE *f,NTSTATUS expected)
{
    BC250_PCI_OBSERVATION output;
    memset(&output,0xa5,sizeof(output));
    CHECK(Bc250PciObserve(&f->Session,&output,0)==expected);
    CHECK(f->References==0 && f->Forbidden==0);
    if(f->Session.State==BC250_PCI_QUARANTINED){
        CHECK(Empty(&output,sizeof(output)));
        CHECK(f->Session.LeaseHeld || f->AcquireStatus>=0);
    }else{
        CHECK(f->LeaseRefs==0 && f->BusRefs==0);
        CHECK(Empty(&f->Session.Bus,sizeof(f->Session.Bus)));
        CHECK(Empty(&f->Session.Lease,sizeof(f->Session.Lease)));
        if(expected!=0)CHECK(Empty(&output,sizeof(output)));
        else {CHECK(output.Valid==1 && output.Generation==1 && output.Epoch==7); CHECK(memcmp(output.Header,f->Header,64)==0);}
    }
    f->Output=output;
    CHECK(Bc250PciObserve(&f->Session,&output,0)==STATUS_DEVICE_NOT_READY);
    CHECK(Empty(&output,sizeof(output)));
}
int main(VOID)
{
    FAKE f;
    unsigned event,malformed,n,bit,byte;
    BC250_PCI_CORRELATION correlation;
    const unsigned events[]={0,1,2,3,4,11,12,13,14};
    for(event=0;event<sizeof(events)/sizeof(events[0]);++event)
      for(malformed=0;malformed<=8;++malformed)
       for(n=0;n<=65;++n){
        NTSTATUS expected;
        Setup(&f); f.Event=events[event]; f.Malformed=malformed;f.ShortLength=n;
        if(malformed==6 || malformed==7) f.QueryStatus=STATUS_NOT_SUPPORTED;
        if(malformed==7 || malformed==5)expected=STATUS_DATA_ERROR;
        else if(events[event]==1 || events[event]==11 || malformed==8)expected=STATUS_DEVICE_NOT_READY;
        else if(malformed==6)expected=STATUS_NOT_SUPPORTED;
        else if(malformed>=1 && malformed<=4)expected=STATUS_DATA_ERROR;
        else if(events[event]==2 || events[event]==12)expected=STATUS_DEVICE_NOT_READY;
        else if(n!=64)expected=STATUS_DATA_ERROR;
        else if(events[event]==3 || events[event]==4 || events[event]==13 || events[event]==14)expected=STATUS_DEVICE_NOT_READY;
        else expected=0;
        /* Early stale check prevents query, even malformed returned interface. */
        if(events[event]==1 || events[event]==11 || malformed==8)expected=STATUS_DEVICE_NOT_READY;
        One(&f,expected);
      }
    Setup(&f);f.AcquireStatus=STATUS_NOT_SUPPORTED;One(&f,STATUS_NOT_SUPPORTED);
    Setup(&f);f.AcquireStatus=0x103;One(&f,STATUS_DATA_ERROR);
    Setup(&f);f.QueryStatus=0x103;One(&f,STATUS_DATA_ERROR);
    Setup(&f);One(&f,0);
    CHECK(Bc250PciCorrelate(&f.Output,&f.Resources,&correlation)==0);
    CHECK(correlation.Valid==1 && correlation.Count==3 && correlation.VramOwnership==0 && correlation.DmaAuthorized==0);
    CHECK(correlation.Matches[0].Bar==0 && correlation.Matches[1].Bar==2 && correlation.Matches[2].Bar==5);
    /* Raw addresses stay bus-domain; translated may differ. PnP supplied size. */
    f.Resources.Memory[0].TranslatedBase=0x100000000ULL;
    CHECK(Bc250PciCorrelate(&f.Output,&f.Resources,&correlation)==0);
    CHECK(correlation.Matches[0].RawBase==0xc0000000 && correlation.Matches[0].TranslatedBase==0x100000000ULL);
    for(byte=0;byte<64;++byte)for(bit=0;bit<8;++bit){
        BC250_PCI_OBSERVATION bad=f.Output;
        bad.Header[byte]^=(UCHAR)(1U<<bit);
        if(Bc250PciCorrelate(&bad,&f.Resources,&correlation)==0)
            CHECK(correlation.VramOwnership==0 && correlation.DmaAuthorized==0 && correlation.Count==3);
        else CHECK(Empty(&correlation,sizeof(correlation)));
    }
    for(n=0;n<18;++n){
        BC250_PCI_RESOURCES bad=f.Resources;
        switch(n){
        case 0:bad.Epoch++;break;case 1:bad.Generation++;break;
        case 2:bad.Started=0;break;case 3:bad.Valid=0;break;
        case 4:bad.MemoryCount=9;break;case 5:bad.MemoryCount=0;break;
        case 6:bad.DescriptorCount=33;break;case 7:bad.Memory[0].Length=0;break;
        case 8:bad.Memory[0].Length=~(ULONGLONG)0;break;
        case 9:bad.Memory[0].Ordinal=9;break;case 10:bad.Memory[1].Ordinal=0;break;
        case 11:bad.Memory[0].RawFlags^=4;break;case 12:bad.Memory[0].RawFlags=(USHORT)(bad.Memory[0].RawFlags&~0x80U);break;
        case 13:bad.Memory[0].RawBase=0;break;case 14:bad.Memory[0].RawBase++;break;
        case 15:bad.Memory[1].RawBase=bad.Memory[0].RawBase;break;
        case 16:bad.Memory[1].TranslatedBase=bad.Memory[0].TranslatedBase;break;
        case 17:bad.Memory[0].TranslatedBase=~(ULONGLONG)0;break;
        }
        CHECK(Bc250PciCorrelate(&f.Output,&bad,&correlation)!=0);
        CHECK(Empty(&correlation,sizeof(correlation)));
    }
    Setup(&f);One(&f,0);
    {BC250_PCI_OBSERVATION bad=f.Output;Put32(bad.Header,36,0xfe800004U);
     CHECK(Bc250PciCorrelate(&bad,&f.Resources,&correlation)!=0);CHECK(Empty(&correlation,sizeof(correlation)));}
    {BC250_PCI_SESSION zero={0};BC250_PCI_OBSERVATION out;
     CHECK(Bc250PciSessionInit(&zero,&f.Ops,1)==STATUS_INVALID_PARAMETER);
     CHECK(Bc250PciObserve(&f.Session,&out,1)==STATUS_INVALID_PARAMETER);
     CHECK(Empty(&out,sizeof(out)));}
    printf("PASS: %u checks; lifecycle/correlation RAM fakes, no devices or GPU ownership.\n",checks);
    return 0;
}
