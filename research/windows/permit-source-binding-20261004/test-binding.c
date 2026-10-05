/* SPDX-License-Identifier: Apache-2.0 */
/* TEST WORLD ONLY. Reuse frozen Source/permit, no exported native adapter.
 * All roots, tokens and fake refs retained through ALL fixture calls/returns.
 * Hand-sequenced interleavings, NOT SMP, OS IRPs, hardware or actual deferral. */
#define BC250_PERMIT_MOCK 1
#define main FrozenSourceFixtures
#include "../pnp-source-20261004/test-source.c"
#undef main
#include "../composite-permit-20261004/bc250_composite_permit.c"
BOOLEAN Bc250PermitMockAllowed=TRUE;
#define MODEL_INTERLOCKS 10U
#define MODEL_CLOSE 11U
typedef struct MODEL_BINDING {
    BC250_SOURCE Source;
    BC250_PERMIT Permit;
    BC250_CAPTURE_INPUT Published;
    ULONGLONG Revision;
    ULONG Mutations,PhysicalPresent;
} MODEL_BINDING;
typedef struct MODEL_WRITER {
    BC250_PERMIT_WRITER Permit;
    BC250_SOURCE_REQUEST Request;
    MODEL_BINDING *Owner;
    ULONG Op,Arg,Claimed,Pending;
} MODEL_WRITER;
static unsigned fixtures;

/* Participation maps EVERY frozen Source tuple mutation API in this fixture:
 * Init exclusive/unpublished; Begin/Complete one claimed writer; Transition,
 * Interlocks, Close after claim. Drain is separate fixture retirement, never
 * inferred from Inspect. No actual Build22 writer routes modified. */
static NTSTATUS ModelAdmit(MODEL_BINDING *m,MODEL_WRITER *w,ULONG op,ULONG arg)
{
    NTSTATUS st;
    if(!SourceZero(w,sizeof(*w)) || op<1 || op>MODEL_CLOSE ||
        (op==BC250_SOURCE_POWER?(arg<1 || arg>4):op==MODEL_INTERLOCKS?arg>1:arg!=0))
        return STATUS_INVALID_PARAMETER;
    st=Bc250PermitWriteAdmit(&m->Permit,op<=6?op:6,&w->Permit);
    if(st==STATUS_SUCCESS){w->Owner=m;w->Op=op;w->Arg=arg;}
    return st;
}
/* Publishing inspects Source directly ONLY under exclusive fixture sequencing
 * and a successfully claimed writer. Not a production atomic tuple provider. */
static NTSTATUS ModelRetireWriter(MODEL_BINDING *m,MODEL_WRITER *w,NTSTATUS final,NTSTATUS sourceStatus)
{
    NTSTATUS st;BOOLEAN publish;
    CHECK(w->Owner==m && w->Claimed && !w->Pending);
    CHECK(SourceZero(&w->Request,sizeof(w->Request)));
    publish=final==STATUS_SUCCESS && sourceStatus==STATUS_SUCCESS && SourceReady(&m->Source);
    if(publish){
        CHECK(m->Revision<~(ULONGLONG)0-1U);
        m->Published=m->Source.Current;++m->Revision;
        CHECK(m->Published.SourceGeneration==m->Permit.Generation && m->Source.ScopeId==m->Permit.ScopeId);
    }
    if(m->Source.Closing || m->Source.Fault)CHECK(Bc250PermitClose(&m->Permit)==STATUS_SUCCESS);
    st=Bc250PermitWriteFinish(&m->Permit,&w->Permit,publish?STATUS_SUCCESS:STATUS_CANCELLED,
        publish?m->Revision:0);
    /* Only known zero ticket means metadata cleanup happened. Never guess free. */
    if(SourceZero(&w->Permit,sizeof(w->Permit)))RtlZeroMemory(w,sizeof(*w));
    return st;
}
static NTSTATUS ModelClaim(MODEL_BINDING *m,MODEL_WRITER *w)
{
    NTSTATUS st,retired;
    if(w->Owner!=m || !w->Op || w->Claimed)return STATUS_INVALID_PARAMETER;
    st=Bc250PermitWriteClaim(&m->Permit,&w->Permit);if(st!=STATUS_SUCCESS)return st;
    w->Claimed=1;++m->Mutations;
    if(w->Op<=6){
        st=Bc250SourceBegin(&m->Source,w->Op,w->Arg,&w->Request);
        if(st==STATUS_SUCCESS){w->Pending=1;return STATUS_SUCCESS;}
    }else if(w->Op<=9)st=Bc250SourceTransition(&m->Source,w->Op);
    else if(w->Op==MODEL_INTERLOCKS)st=Bc250SourceInterlocks(&m->Source,w->Arg);
    else st=Bc250SourceClose(&m->Source);
    CHECK(SourceZero(&w->Request,sizeof(w->Request)));
    retired=ModelRetireWriter(m,w,st,st);
    CHECK(retired==STATUS_SUCCESS || retired==STATUS_DEVICE_NOT_READY);
    return st;
}
static NTSTATUS ModelComplete(MODEL_BINDING *m,MODEL_WRITER *w,NTSTATUS final,
    const BC250_SOURCE_RESOURCES *resources)
{
    NTSTATUS st,retired;
    if(w->Owner!=m || !w->Claimed || !w->Pending)return STATUS_INVALID_PARAMETER;
    st=Bc250SourceComplete(&m->Source,&w->Request,final,resources);
    if(!SourceZero(&w->Request,sizeof(w->Request))){
        if(st!=STATUS_DEVICE_BUSY)CHECK(Bc250PermitClose(&m->Permit)==STATUS_SUCCESS);
        return st; /* unknown or nonfinal retains BOTH obligations */
    }
    w->Pending=0;retired=ModelRetireWriter(m,w,final,st);
    CHECK(retired==STATUS_SUCCESS || retired==STATUS_DEVICE_NOT_READY);
    return st;
}
static VOID ModelTransaction(MODEL_BINDING *m,ULONG op,ULONG arg)
{
    MODEL_WRITER w={0};BC250_SOURCE_RESOURCES r=Resources();
    CHECK(ModelAdmit(m,&w,op,arg)==STATUS_SUCCESS);
    CHECK(ModelClaim(m,&w)==STATUS_SUCCESS);
    if(w.Pending)CHECK(ModelComplete(m,&w,STATUS_SUCCESS,op==1?&r:NULL)==STATUS_SUCCESS);
    CHECK(SourceZero(&w,sizeof(w)));
}
static VOID ModelInit(MODEL_BINDING *m,DEVICE_OBJECT *p,DEVICE_OBJECT *l,ULONGLONG scope)
{
    RtlZeroMemory(m,sizeof(*m));Init(&m->Source,p,l,scope);
    CHECK(Bc250PermitInit(&m->Permit,scope,1)==STATUS_SUCCESS);
    m->PhysicalPresent=1;++fixtures;
    ModelTransaction(m,1,0);CHECK(!m->Permit.Published && !SourceReady(&m->Source));
    ModelTransaction(m,MODEL_INTERLOCKS,1);CHECK(!m->Permit.Published);
    ModelTransaction(m,6,1);CHECK(m->Permit.Published && SourceReady(&m->Source) && m->Revision==1);
}
static VOID ModelDispose(MODEL_BINDING *m)
{
    MODEL_WRITER w={0};BC250_PERMIT_SNAPSHOT snap;
    CHECK(!m->Source.PendingAddress && !m->Source.Rundown.Count);
    CHECK(Bc250PermitInspect(&m->Permit,&snap)==STATUS_SUCCESS && !snap.Pins && !snap.Steps && !snap.WriterPhase);
    if(!m->Source.Closing){
        CHECK(!snap.Closing && !snap.Fault);CHECK(ModelAdmit(m,&w,MODEL_CLOSE,0)==STATUS_SUCCESS);
        CHECK(ModelClaim(m,&w)==STATUS_SUCCESS);
    }
    CHECK(Bc250PermitClose(&m->Permit)==STATUS_SUCCESS);
    /* Serial fixture owns ALL returns; snapshot alone would NOT authorize this. */
    CHECK(Bc250SourceDrain(&m->Source)==STATUS_SUCCESS);
    CHECK(m->Source.Retirement==2 && !m->Source.Pdo && !m->Source.Lower);
}
static VOID ReaderHook(MODEL_BINDING *m,MODEL_WRITER *w,ULONG here,ULONG at,ULONG op,ULONG arg)
{
    BC250_CAPTURE_INPUT before;ULONG count;
    if(here!=at)return;
    before=m->Source.Current;count=m->Mutations;irql=2;
    CHECK(ModelAdmit(m,w,op,arg)==STATUS_SUCCESS);irql=0;
    if(op==8 || op==9 || op==MODEL_CLOSE)m->PhysicalPresent=0; /* physical event independent of metadata */
    CHECK(ModelClaim(m,w)==STATUS_DEVICE_BUSY);
    CHECK(m->Mutations==count && !m->Source.PendingAddress);
    CHECK(RtlCompareMemory(&before,&m->Source.Current,sizeof(before))==sizeof(before));
    CHECK(!m->Permit.Published && w->Owner==m);
}
static NTSTATUS ModelRead(MODEL_BINDING *m,BC250_CAPTURE_INPUT *out,MODEL_WRITER *w,
    ULONG at,ULONG op,ULONG arg)
{
    BC250_PERMIT_READER pin={0};BC250_SOURCE_LEASE lease={0};BC250_CAPTURE_INPUT value,tail;
    ULONGLONG revision=0;NTSTATUS st;BOOLEAN entered=FALSE;
    RtlZeroMemory(out,sizeof(*out));RtlZeroMemory(&value,sizeof(value));
    st=Bc250PermitAcquire(&m->Permit,&pin);if(st!=STATUS_SUCCESS)return st;
    ReaderHook(m,w,1,at,op,arg);
    st=Bc250SourceAcquire(&m->Source,&lease);
    if(st!=STATUS_SUCCESS){CHECK(Bc250PermitRelease(&m->Permit,&pin)==STATUS_SUCCESS);return st;}
    ReaderHook(m,w,2,at,op,arg);
    st=Bc250PermitStepEnter(&m->Permit,&pin,&revision);
    if(st==STATUS_SUCCESS){
        entered=TRUE;CHECK(!held && !irql);ReaderHook(m,w,3,at,op,arg);
        st=Bc250SourceValidate(&m->Source,&lease,&value);ReaderHook(m,w,4,at,op,arg);
        if(st==STATUS_SUCCESS){
            CHECK(revision==m->Revision && pin.ScopeId==lease.ScopeId);
            CHECK(value.SourceGeneration==pin.Generation);
            CHECK(RtlCompareMemory(&value,&m->Published,sizeof(value))==sizeof(value));
            /* Bounded synchronous metadata use only. No hardware permission. */
            CHECK(Bc250SourceValidate(&m->Source,&lease,&tail)==STATUS_SUCCESS);
            CHECK(RtlCompareMemory(&value,&tail,sizeof(value))==sizeof(value));
        }
    }
    if(entered)CHECK(Bc250PermitStepLeave(&m->Permit,&pin)==STATUS_SUCCESS);
    ReaderHook(m,w,5,at,op,arg);
    CHECK(Bc250SourceRelease(&m->Source,&lease)==STATUS_SUCCESS);
    ReaderHook(m,w,6,at,op,arg); /* Source lease gone; permit pin MUST still block */
    CHECK(Bc250PermitRelease(&m->Permit,&pin)==STATUS_SUCCESS);
    if(st==STATUS_SUCCESS)*out=value; /* historical once pin released, never authority */
    return st;
}
static VOID Interleavings(VOID)
{
    const ULONG ops[]={2,3,6,6,MODEL_INTERLOCKS,MODEL_INTERLOCKS,7,8,9,MODEL_CLOSE};
    const ULONG args[]={0,0,1,4,0,1,0,0,0,0};ULONG n,at;
    for(n=0;n<10;++n)for(at=1;at<=6;++at){
        MODEL_BINDING m;MODEL_WRITER w={0};DEVICE_OBJECT p,l;BC250_CAPTURE_INPUT out;
        ModelInit(&m,&p,&l,1000+n*6+at);
        CHECK(ModelRead(&m,&out,&w,at,ops[n],args[n])==(at<=2?STATUS_DEVICE_NOT_READY:STATUS_SUCCESS));
        CHECK(at<=2?SourceZero(&out,sizeof(out)):!SourceZero(&out,sizeof(out)));
        CHECK(!m.Source.Rundown.Count && m.Permit.Pins==0 && !m.Permit.Steps);
        CHECK(ModelClaim(&m,&w)==STATUS_SUCCESS);
        if(w.Pending)CHECK(ModelComplete(&m,&w,STATUS_SUCCESS,NULL)==STATUS_SUCCESS);
        CHECK(SourceZero(&w,sizeof(w)));
        CHECK((BOOLEAN)m.Permit.Published==SourceReady(&m.Source));
        if(n>=7){CHECK(!m.PhysicalPresent);CHECK(at<=2 || out.PowerState==1);}
        ModelDispose(&m);
    }
}
static VOID StateSequences(VOID)
{
    ULONG n;for(n=0;n<2;++n){
        MODEL_BINDING m;MODEL_WRITER w={0};DEVICE_OBJECT p,l;BC250_CAPTURE_INPUT out,old;
        ModelInit(&m,&p,&l,2000+n);
        CHECK(ModelRead(&m,&old,&w,0,0,0)==STATUS_SUCCESS);
        ModelTransaction(&m,n?3U:2U,0);CHECK(!m.Permit.Published);
        CHECK(ModelRead(&m,&out,&w,0,0,0)==STATUS_DEVICE_NOT_READY && SourceZero(&out,sizeof(out)));
        ModelTransaction(&m,n?5U:4U,0);CHECK(m.Permit.Published);
        CHECK(ModelRead(&m,&out,&w,0,0,0)==STATUS_SUCCESS && out.SourceEpoch>old.SourceEpoch);
        ModelTransaction(&m,7,0);CHECK(!m.Permit.Published);
        ModelTransaction(&m,1,0);CHECK(!m.Permit.Published && m.Source.Current.PowerState==0);
        ModelTransaction(&m,6,1);CHECK(m.Permit.Published);
        ModelTransaction(&m,MODEL_INTERLOCKS,0);CHECK(!m.Permit.Published);
        ModelTransaction(&m,MODEL_INTERLOCKS,1);CHECK(m.Permit.Published);
        ModelDispose(&m);
    }
}
static VOID PendingAndFaults(VOID)
{
    ULONG n;for(n=0;n<7;++n){
        MODEL_BINDING m;MODEL_WRITER w={0},copy;DEVICE_OBJECT p,l;BC250_CAPTURE_INPUT out;
        NTSTATUS final,expected;unsigned before;
        ModelInit(&m,&p,&l,3000+n);before=derefs;
        CHECK(ModelAdmit(&m,&w,6,1)==STATUS_SUCCESS && ModelClaim(&m,&w)==STATUS_SUCCESS);
        CHECK(ModelComplete(&m,&w,STATUS_PENDING,NULL)==STATUS_DEVICE_BUSY && w.Pending && w.Claimed);
        CHECK(m.Source.PendingAddress==&w.Request && m.Source.Rundown.Count==1 && m.Permit.WriterPhase==2);
        CHECK(ModelRead(&m,&out,&copy,0,0,0)==STATUS_DEVICE_NOT_READY && SourceZero(&out,sizeof(out)));
        if(n==0 || n==1){final=n?STATUS_CANCELLED:STATUS_SUCCESS;expected=STATUS_SUCCESS;}
        else if(n==2 || n==3){final=n==2?(NTSTATUS)0x80000005U:(NTSTATUS)1;expected=STATUS_DATA_ERROR;}
        else if(n==4){
            /* Existing CLAIMED writer owns all Source mutation: no readers exist.
             * Real removal forwarding/order/cancellation is NOT implemented. */
            CHECK(!m.Permit.Pins && !m.Permit.Steps);irql=2;
            CHECK(Bc250PermitClose(&m.Permit)==STATUS_SUCCESS);
            CHECK(Bc250SourceClose(&m.Source)==STATUS_SUCCESS);irql=0;
            final=STATUS_SUCCESS;expected=STATUS_DEVICE_NOT_READY;
        }else{
            copy=w;
            CHECK(ModelComplete(&m,&copy,STATUS_SUCCESS,NULL)==STATUS_INVALID_PARAMETER);
            CHECK(m.Permit.Closing && m.Permit.WriterAddress==&w.Permit);
            CHECK(m.Source.PendingAddress==&w.Request && m.Source.Rundown.Count==1);
            if(n==6){
                w.Request.Id++; /* hostile fixture ONLY; no guessed repair */
                CHECK(ModelComplete(&m,&w,STATUS_SUCCESS,NULL)==STATUS_INVALID_PARAMETER);
                CHECK(w.Pending && w.Claimed && m.Permit.WriterAddress==&w.Permit);
                CHECK(Bc250SourceClose(&m.Source)==STATUS_SUCCESS);
                CHECK(Bc250SourceDrain(&m.Source)==STATUS_DEVICE_BUSY && derefs==before);
                continue; /* RAM test world retains unknown obligations */
            }
            final=STATUS_SUCCESS;expected=STATUS_SUCCESS;
        }
        threadId=2;CHECK(ModelComplete(&m,&w,final,NULL)==expected);threadId=1;
        CHECK(SourceZero(&w,sizeof(w)) && !m.Source.PendingAddress && !m.Source.Rundown.Count);
        CHECK(m.Permit.Published==(n==0?1U:0U));
        if(n==2 || n==3){
            CHECK(m.Source.Fault && Bc250SourceDrain(&m.Source)==STATUS_DATA_ERROR && derefs==before);
        }else if(n==1){
            CHECK(m.Source.Current.PowerState==0);ModelTransaction(&m,6,1);ModelDispose(&m);
        }else if(n==5){
            /* Closed permit cannot start a NEW writer. Fixture still owns all
             * returns, must quarantine live Source root rather than bypass close. */
            CHECK(m.Permit.Closing && !m.Source.Closing && derefs==before);
        }else ModelDispose(&m);
    }
}
static VOID ManyReadersAndCycles(VOID)
{
    MODEL_BINDING m;MODEL_WRITER w={0};DEVICE_OBJECT p,l;
    BC250_PERMIT_READER pins[8]={0};BC250_SOURCE_LEASE leases[8]={0};BC250_CAPTURE_INPUT out;
    ULONG round,n;ULONGLONG revision;
    ModelInit(&m,&p,&l,4000);
    for(round=0;round<128;++round){
        for(n=0;n<8;++n){
            CHECK(Bc250PermitAcquire(&m.Permit,&pins[n])==STATUS_SUCCESS);
            CHECK(Bc250SourceAcquire(&m.Source,&leases[n])==STATUS_SUCCESS);
            CHECK(Bc250PermitStepEnter(&m.Permit,&pins[n],&revision)==STATUS_SUCCESS);
        }
        irql=2;CHECK(ModelAdmit(&m,&w,6,1)==STATUS_SUCCESS);irql=0;
        for(n=0;n<8;++n){
            CHECK(ModelClaim(&m,&w)==STATUS_DEVICE_BUSY);
            CHECK(Bc250SourceValidate(&m.Source,&leases[n],&out)==STATUS_SUCCESS);
            CHECK(RtlCompareMemory(&out,&m.Published,sizeof(out))==sizeof(out));
            CHECK(Bc250PermitStepLeave(&m.Permit,&pins[n])==STATUS_SUCCESS);
            CHECK(Bc250SourceRelease(&m.Source,&leases[n])==STATUS_SUCCESS);
            CHECK(ModelClaim(&m,&w)==STATUS_DEVICE_BUSY); /* pin survives Source lease */
            CHECK(Bc250PermitStepEnter(&m.Permit,&pins[n],&revision)==STATUS_DEVICE_NOT_READY && revision==0);
            CHECK(Bc250PermitRelease(&m.Permit,&pins[n])==STATUS_SUCCESS);
        }
        CHECK(ModelClaim(&m,&w)==STATUS_SUCCESS);
        CHECK(ModelComplete(&m,&w,STATUS_SUCCESS,NULL)==STATUS_SUCCESS && m.Permit.Published);
        CHECK(m.Revision==round+2U && !m.Source.Rundown.Count);
    }
    ModelDispose(&m);
}
static VOID BypassAndBoundary(VOID)
{
    MODEL_BINDING m;MODEL_WRITER w={0};DEVICE_OBJECT p,l;BC250_PERMIT_READER pin={0};
    BC250_SOURCE_LEASE lease={0};BC250_CAPTURE_INPUT out,history;ULONGLONG revision;
    ModelInit(&m,&p,&l,5000);
    CHECK(Bc250PermitAcquire(&m.Permit,&pin)==STATUS_SUCCESS);
    CHECK(Bc250SourceAcquire(&m.Source,&lease)==STATUS_SUCCESS);
    CHECK(Bc250PermitStepEnter(&m.Permit,&pin,&revision)==STATUS_SUCCESS);
    CHECK(Bc250SourceValidate(&m.Source,&lease,&history)==STATUS_SUCCESS);
    CHECK(Bc250SourceInterlocks(&m.Source,0)==STATUS_SUCCESS); /* DELIBERATE nonparticipant negative fixture */
    CHECK(Bc250SourceValidate(&m.Source,&lease,&out)==STATUS_DEVICE_NOT_READY && SourceZero(&out,sizeof(out)));
    CHECK(m.Permit.Published && m.Permit.Revision==revision); /* gate ALONE cannot detect bypass */
    CHECK(Bc250PermitStepLeave(&m.Permit,&pin)==STATUS_SUCCESS);
    CHECK(Bc250SourceRelease(&m.Source,&lease)==STATUS_SUCCESS);
    CHECK(Bc250PermitRelease(&m.Permit,&pin)==STATUS_SUCCESS);
    ModelTransaction(&m,MODEL_INTERLOCKS,1);
    CHECK(ModelRead(&m,&history,&w,0,0,0)==STATUS_SUCCESS);
    ModelTransaction(&m,MODEL_INTERLOCKS,0); /* legal mutation AFTER read's final pin release */
    CHECK(history.InterlocksOff==1 && m.Source.Current.InterlocksOff==0 && !m.Permit.Published);
    /* Returned output remains historical; use MUST occur INSIDE entered step. */
    ModelDispose(&m);
}
static VOID PolicyAndOverlap(VOID)
{
    MODEL_BINDING m;MODEL_WRITER w={0},other={0};DEVICE_OBJECT p,l;
    BC250_CAPTURE_INPUT out,before;ULONG mutations;
    ModelInit(&m,&p,&l,6000);before=m.Source.Current;mutations=m.Mutations;
    CHECK(ModelAdmit(&m,&w,12,0)==STATUS_INVALID_PARAMETER && SourceZero(&w,sizeof(w)));
    CHECK(ModelAdmit(&m,&w,6,0)==STATUS_INVALID_PARAMETER && SourceZero(&w,sizeof(w)));
    Bc250PermitMockAllowed=FALSE;
    CHECK(ModelAdmit(&m,&w,6,1)==STATUS_NOT_SUPPORTED && SourceZero(&w,sizeof(w)));
    CHECK(ModelRead(&m,&out,&w,0,0,0)==STATUS_NOT_SUPPORTED && SourceZero(&out,sizeof(out)));
    Bc250PermitMockAllowed=TRUE;
    CHECK(ModelAdmit(&m,&w,6,1)==STATUS_SUCCESS);
    CHECK(ModelAdmit(&m,&other,2,0)==STATUS_DEVICE_BUSY && SourceZero(&other,sizeof(other)));
    CHECK(ModelClaim(&m,&w)==STATUS_DEVICE_NOT_READY && m.Mutations==mutations);
    CHECK(RtlCompareMemory(&before,&m.Source.Current,sizeof(before))==sizeof(before));
    CHECK(Bc250PermitWriteAbandon(&m.Permit,&w.Permit)==STATUS_DEVICE_NOT_READY);
    CHECK(!m.Permit.WriterAddress && !m.Source.PendingAddress && !m.Source.Rundown.Count);
    /* Faulted gate/Source-ready pair deliberately quarantined, never bypassed
     * by calling new Source writers without claimed permit. No fake OS cleanup. */
}
static VOID CloseAbandonAndSourceDeny(VOID)
{
    ULONG n;for(n=0;n<4;++n){
        MODEL_BINDING m;MODEL_WRITER w={0};DEVICE_OBJECT p,l;BC250_PERMIT_READER pin={0};
        BC250_SOURCE_LEASE lease={0};BC250_CAPTURE_INPUT out,before;ULONGLONG revision;
        ModelInit(&m,&p,&l,7000+n);before=m.Source.Current;
        if(n<2){
            CHECK(Bc250PermitAcquire(&m.Permit,&pin)==STATUS_SUCCESS);
            CHECK(Bc250SourceAcquire(&m.Source,&lease)==STATUS_SUCCESS);
            CHECK(Bc250PermitStepEnter(&m.Permit,&pin,&revision)==STATUS_SUCCESS);
            irql=2;
            if(n==0)CHECK(Bc250PermitClose(&m.Permit)==STATUS_SUCCESS);
            else{
                CHECK(ModelAdmit(&m,&w,6,1)==STATUS_SUCCESS);
                CHECK(Bc250PermitWriteAbandon(&m.Permit,&w.Permit)==STATUS_SUCCESS);
                CHECK(SourceZero(&w.Request,sizeof(w.Request)) && !w.Claimed && !w.Pending);
                RtlZeroMemory(&w,sizeof(w)); /* known metadata-only abandonment; no OS IRP existed */
            }
            irql=0;
            CHECK(Bc250SourceValidate(&m.Source,&lease,&out)==STATUS_SUCCESS);
            CHECK(RtlCompareMemory(&out,&before,sizeof(out))==sizeof(out));
            CHECK(Bc250PermitStepLeave(&m.Permit,&pin)==STATUS_SUCCESS);
            CHECK(Bc250PermitStepEnter(&m.Permit,&pin,&revision)==STATUS_DEVICE_NOT_READY && revision==0);
            CHECK(Bc250SourceRelease(&m.Source,&lease)==STATUS_SUCCESS);
            CHECK(Bc250PermitRelease(&m.Permit,&pin)==STATUS_SUCCESS);
            if(n==0){
                CHECK(ModelAdmit(&m,&w,MODEL_CLOSE,0)==STATUS_DEVICE_NOT_READY);
                CHECK(SourceReady(&m.Source) && !m.Source.Rundown.Count);
                continue; /* closed gate does not authorize NEW Source mutation/free; quarantine */
            }
            ModelTransaction(&m,MODEL_INTERLOCKS,1);CHECK(m.Permit.Published && m.Revision==2);
            ModelDispose(&m);
        }else{
            CHECK(ModelAdmit(&m,&w,n==2?6U:1U,n==2?1U:0U)==STATUS_SUCCESS);
            if(n==2)Bc250SourceMockAllowed=FALSE;
            CHECK(ModelClaim(&m,&w)==(n==2?STATUS_NOT_SUPPORTED:STATUS_INVALID_DEVICE_STATE));
            Bc250SourceMockAllowed=TRUE;
            CHECK(SourceZero(&w,sizeof(w)) && !m.Source.PendingAddress && !m.Permit.Published);
            if(n==2){
                CHECK(RtlCompareMemory(&before,&m.Source.Current,sizeof(before))==sizeof(before));ModelDispose(&m);
            }else CHECK(m.Source.Fault && m.Permit.Closing && Bc250SourceDrain(&m.Source)==STATUS_DATA_ERROR);
        }
    }
}
int main(VOID)
{
    unsigned baseline;CHECK(FrozenSourceFixtures()==0);baseline=checks;
    Interleavings();StateSequences();PendingAndFaults();ManyReadersAndCycles();
    BypassAndBoundary();PolicyAndOverlap();CloseAbandonAndSourceDeny();CHECK(!held && !irql && threadId==1);
    printf("PASS: permit Source binding %u checks, %u fixtures; participating tuple stable through bounded step, no native wrapper/SMP/hardware.\n",checks-baseline,fixtures);
    return 0;
}
