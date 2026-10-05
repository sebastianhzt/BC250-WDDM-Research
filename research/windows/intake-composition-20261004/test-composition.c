/* SPDX-License-Identifier: Apache-2.0 */
/* RAM-only protocol harness: reuse frozen fake platform and implementations.
 * NO new native export, OS wrapper, IRP queue or backend authorization. */
#define BC250_INTAKE_MOCK 1
#define main FrozenCompletionFixtures
#include "../pnp-completion-20261004/test-completion.c"
#undef main
#include "../dispatch-intake-20261004/bc250_dispatch_intake.c"
BOOLEAN Bc250IntakeMockAllowed=TRUE;
static BC250_INTAKE_TICKET injected;
static unsigned compositionCases;

/* Hooks are explicitly sequenced RAM interleavings, NOT thread/SMP injection. */
static VOID SampleHook(BC250_INTAKE *g,BC250_SOURCE *s,ULONG here,ULONG stage,ULONG action)
{
    if(here!=stage)return;
    irql=2;
    if(action==1 || action==2){
        CHECK(Bc250IntakeAdmit(g,6,1,&injected)==STATUS_SUCCESS);
        if(action==2)CHECK(Bc250IntakeAbandon(g,&injected)==STATUS_SUCCESS);
    }
    if(action==3)CHECK(Bc250IntakeClose(g)==STATUS_SUCCESS);
    if(action==4)CHECK(Bc250SourceClose(s)==STATUS_SUCCESS);
    irql=0;
}
/* Historical sampling protocol in TEST only. Explicitly not a global atomic
 * snapshot or reservation; source-only writers can race after final validation.
 * All fixture storage externally anchored through every attempt/return. */
static NTSTATUS ModelSample(BC250_INTAKE *g,BC250_SOURCE *s,BC250_CAPTURE_INPUT *out,ULONG stage,ULONG action)
{
    BC250_INTAKE_SNAPSHOT gs;BC250_SOURCE_LEASE lease={0};BC250_CAPTURE_INPUT value;
    NTSTATUS st,releaseStatus;
    RtlZeroMemory(out,sizeof(*out));RtlZeroMemory(&value,sizeof(value));
    st=Bc250IntakeInspect(g,&gs);if(st!=STATUS_SUCCESS)return st;
    SampleHook(g,s,1,stage,action);
    if(!gs.MetadataIdle)return STATUS_DEVICE_NOT_READY;
    st=Bc250IntakeCheckIdleAtEpoch(g,gs.Epoch);if(st!=STATUS_SUCCESS)return st;
    SampleHook(g,s,2,stage,action);
    st=Bc250SourceAcquire(s,&lease);if(st!=STATUS_SUCCESS)return st;
    SampleHook(g,s,3,stage,action);
    st=Bc250SourceValidate(s,&lease,&value);
    SampleHook(g,s,4,stage,action);
    if(st==STATUS_SUCCESS)st=Bc250SourceValidate(s,&lease,&value);
    if(st==STATUS_SUCCESS && (lease.ScopeId!=gs.ScopeId || value.SourceGeneration!=gs.Generation))st=STATUS_DATA_ERROR;
    SampleHook(g,s,5,stage,action);
    releaseStatus=Bc250SourceRelease(s,&lease);
    CHECK(releaseStatus==STATUS_SUCCESS && SourceZero(&lease,sizeof(lease)));
    SampleHook(g,s,6,stage,action);
    if(st==STATUS_SUCCESS)st=Bc250IntakeCheckIdleAtEpoch(g,gs.Epoch);
    SampleHook(g,s,7,stage,action);
    if(st==STATUS_SUCCESS)*out=value;
    return st;
}
/* Fixture-only retirement witness. NOT a production reclaim helper. The serial
 * test owns every context until all simulated callbacks/dispatch/worker return.
 * Caller cannot infer that real all-returns witness from Inspect/HeldTags. */
static NTSTATUS ModelRetire(BC250_INTAKE *g,BC250_INTAKE_TICKET *t,BC250_SOURCE *s,
    BC250_COMPLETION *c,BOOLEAN allReturnsKnown,NTSTATUS final)
{
    BC250_COMPLETION_STATUS observed;NTSTATUS st;
    if(!allReturnsKnown)return STATUS_DEVICE_BUSY;
    st=Bc250CompletionInspect(c,&observed);if(st!=STATUS_SUCCESS)return st;
    if(observed.Phase!=BC250_COMPLETION_DONE || observed.HeldTags || observed.RequestOwned ||
        observed.WorkPresent || observed.IrpPresent || s->PendingAddress ||
        !SourceZero(&c->Request,sizeof(c->Request)))return STATUS_DEVICE_NOT_READY;
    return Bc250IntakeFinish(g,t,final);
}
static VOID CombinedFixture(BC250_INTAKE *g,BC250_SOURCE *s,DEVICE_OBJECT *p,DEVICE_OBJECT *l,
    BC250_COMPLETION *c,IO_REMOVE_LOCK *lock,IRP *irp,ULONGLONG scope)
{
    Fixture(s,p,l,c,lock,irp,scope);ReadySource(s);RtlZeroMemory(g,sizeof(*g));
    CHECK(Bc250IntakeInit(g,scope,1)==STATUS_SUCCESS);RtlZeroMemory(&injected,sizeof(injected));
    ++compositionCases;
}
static VOID SampleInterleavings(VOID)
{
    ULONG stage,action;
    for(stage=1;stage<=7;++stage)for(action=1;action<=4;++action){
        BC250_INTAKE g;BC250_SOURCE s;DEVICE_OBJECT p,l;BC250_COMPLETION c;IO_REMOVE_LOCK lock;IRP irp;
        BC250_CAPTURE_INPUT out;NTSTATUS expected,st;
        CombinedFixture(&g,&s,&p,&l,&c,&lock,&irp,100+stage*4+action);
        expected=(stage==7 || (action==4 && stage>=5))?STATUS_SUCCESS:STATUS_DEVICE_NOT_READY;
        memset(&out,0xff,sizeof(out));st=ModelSample(&g,&s,&out,stage,action);
        CHECK(st==expected);CHECK(st==STATUS_SUCCESS?!SourceZero(&out,sizeof(out)):SourceZero(&out,sizeof(out)));
        CHECK(!s.Rundown.Count && !s.PendingAddress); /* acquired lease always released */
        if(action==1){
            CHECK(SourceReady(&s)); /* Source alone stayed Ready; composed check caught admission until final barrier */
            CHECK(g.Address==&injected);
            CHECK(ModelSample(&g,&s,&out,0,0)==STATUS_DEVICE_NOT_READY && SourceZero(&out,sizeof(out)));
            irql=2;CHECK(Bc250IntakeAbandon(&g,&injected)==STATUS_SUCCESS);irql=0;
        }
        if(action==2)CHECK(!g.Address && SourceReady(&s)); /* ABA idle is not same epoch */
        if(action==3)CHECK(g.Closing && SourceReady(&s));
        if(action==4){
            CHECK(s.Closing);
            CHECK(ModelSample(&g,&s,&out,0,0)==STATUS_DEVICE_NOT_READY && SourceZero(&out,sizeof(out)));
        }
        CHECK(Bc250IntakeClose(&g)==STATUS_SUCCESS);FinishSource(&s);
    }
}
static VOID PairAndPolicy(VOID)
{
    ULONG n;for(n=0;n<4;++n){
        BC250_INTAKE g;BC250_SOURCE s;DEVICE_OBJECT p,l;BC250_COMPLETION c;IO_REMOVE_LOCK lock;IRP irp;
        BC250_CAPTURE_INPUT out;NTSTATUS expected;BC250_INTAKE wrong={0};BC250_INTAKE *sampleGuard=&g;
        CombinedFixture(&g,&s,&p,&l,&c,&lock,&irp,500+n);
        if(n==0){ /* fresh wrong pair, not a caller edit of published roots */
            CHECK(Bc250IntakeInit(&wrong,9000,1)==STATUS_SUCCESS);sampleGuard=&wrong;expected=STATUS_DATA_ERROR;
        }else if(n==1){Bc250IntakeMockAllowed=FALSE;expected=STATUS_NOT_SUPPORTED;}
        else if(n==2){Bc250SourceMockAllowed=FALSE;expected=STATUS_NOT_SUPPORTED;}
        else expected=STATUS_SUCCESS;
        memset(&out,0xff,sizeof(out));CHECK(ModelSample(sampleGuard,&s,&out,0,0)==expected);
        CHECK(expected==STATUS_SUCCESS?!SourceZero(&out,sizeof(out)):SourceZero(&out,sizeof(out)));
        CHECK(!s.Rundown.Count && !s.PendingAddress);
        Bc250IntakeMockAllowed=Bc250SourceMockAllowed=TRUE;
        CHECK(Bc250IntakeClose(&g)==STATUS_SUCCESS);FinishSource(&s);
    }
}
static VOID BridgeMatrix(VOID)
{
    ULONG lm,wm,ret,final,closing;
    for(lm=0;lm<2;++lm)for(wm=0;wm<2;++wm)for(ret=0;ret<3;++ret)
    for(final=0;final<2;++final)for(closing=0;closing<3;++closing){
        BC250_INTAKE g;BC250_INTAKE_TICKET t={0},second={0};BC250_SOURCE s;
        DEVICE_OBJECT p,l;BC250_COMPLETION c;IO_REMOVE_LOCK lock;IRP irp;BOOLEAN taken=FALSE;
        BC250_CAPTURE_INPUT out;ULONGLONG scope=10000+lm*36+wm*18+ret*6+final*3+closing;
        CombinedFixture(&g,&s,&p,&l,&c,&lock,&irp,scope);
        CHECK(ModelSample(&g,&s,&out,0,0)==STATUS_SUCCESS);
        irql=2;CHECK(Bc250IntakeAdmit(&g,6,1,&t)==STATUS_SUCCESS);irql=0;
        CHECK(SourceReady(&s));CHECK(ModelSample(&g,&s,&out,0,0)==STATUS_DEVICE_NOT_READY);
        CHECK(Bc250IntakeClaim(&g,&t)==STATUS_SUCCESS);CHECK(SourceReady(&s));
        CHECK(ModelSample(&g,&s,&out,0,0)==STATUS_DEVICE_NOT_READY);
        inlineLower=lm;inlineWorker=wm;lowerReturn=ret==0?STATUS_SUCCESS:ret==1?STATUS_PENDING:STATUS_CANCELLED;
        lowerFinal=final?STATUS_CANCELLED:STATUS_SUCCESS;
        CHECK(Bc250CompletionDispatch(&c,&s,&p,&l,&lock,&irp,6,1,NULL,&taken)==STATUS_PENDING && taken);
        CHECK(ModelRetire(&g,&t,&s,&c,FALSE,lowerFinal)==STATUS_DEVICE_BUSY && g.Address==&t);
        if(!lm || !wm)CHECK(ModelRetire(&g,&t,&s,&c,TRUE,lowerFinal)==STATUS_DEVICE_NOT_READY);
        if(closing){
            irql=2;
            if(closing==1)CHECK(Bc250IntakeClose(&g)==STATUS_SUCCESS);
            else CHECK(Bc250IntakeAdmit(&g,2,0,&second)==STATUS_DEVICE_BUSY && g.Fault);
            CHECK(Bc250SourceClose(&s)==STATUS_SUCCESS);irql=0;
        }
        if(!lm)LowerComplete(&irp);if(item.Queued)RunWorker();
        CheckDone(&c,&lock,&irp);CHECK(!s.PendingAddress && !s.Rundown.Count);
        CHECK(!lm || !wm || earlyWorkerObserved);
        CHECK(ModelSample(&g,&s,&out,0,0)==STATUS_DEVICE_NOT_READY); /* guard remains claimed even if Source restored D0 */
        CHECK(ModelRetire(&g,&t,&s,&c,TRUE,c.FinalStatus)==(closing?STATUS_DEVICE_NOT_READY:STATUS_SUCCESS));
        CHECK(IntakeZero(&t,sizeof(t)) && !g.Address);
        CHECK(ModelSample(&g,&s,&out,0,0)==(!closing && !final?STATUS_SUCCESS:STATUS_DEVICE_NOT_READY));
        CHECK(!s.Rundown.Count);CHECK(Bc250IntakeClose(&g)==STATUS_SUCCESS);FinishSource(&s);
    }
}
static VOID SetupAndQuarantine(VOID)
{
    ULONG n;for(n=0;n<8;++n){
        BC250_INTAKE g;BC250_INTAKE_TICKET t={0};BC250_SOURCE s;
        DEVICE_OBJECT p,l;BC250_COMPLETION c;IO_REMOVE_LOCK lock;IRP irp;BOOLEAN taken=FALSE;
        BC250_CAPTURE_INPUT out;NTSTATUS st;unsigned oldForwards=forwards;
        CombinedFixture(&g,&s,&p,&l,&c,&lock,&irp,20000+n);
        irql=2;CHECK(Bc250IntakeAdmit(&g,6,1,&t)==STATUS_SUCCESS);irql=0;
        CHECK(Bc250IntakeClaim(&g,&t)==STATUS_SUCCESS);
        if(n<2)lock.RejectAt=n+1;
        if(n==2)failAlloc=1;if(n==3)failRegister=1;
        if(n==4)CHECK(Bc250SourceClose(&s)==STATUS_SUCCESS);
        st=Bc250CompletionDispatch(&c,&s,&p,&l,&lock,&irp,6,1,NULL,&taken);CHECK(taken);
        if(n<=4){
            CHECK(st!=STATUS_PENDING && forwards==oldForwards);CheckDone(&c,&lock,&irp);
            CHECK(ModelRetire(&g,&t,&s,&c,FALSE,st)==STATUS_DEVICE_BUSY);
            CHECK(ModelRetire(&g,&t,&s,&c,TRUE,st)==STATUS_SUCCESS);
            CHECK(!g.Address && IntakeZero(&t,sizeof(t)));
            CHECK(Bc250IntakeClose(&g)==STATUS_SUCCESS);FinishSource(&s);
        }else{
            CHECK(st==STATUS_PENDING);
            if(n==5)lowerFinal=STATUS_PENDING;
            LowerComplete(&irp);
            if(n==6){irql=2;CHECK(Bc250CompletionCallback(NULL,&irp,&c)==STATUS_MORE_PROCESSING_REQUIRED);irql=0;}
            if(n==7)c.Request.Id++; /* malicious fixture mutation, never production permission */
            if(item.Queued)RunWorker();
            CHECK(c.Phase==BC250_COMPLETION_FAULT && lock.Count==1 && !irp.Completed);
            CHECK(ModelRetire(&g,&t,&s,&c,TRUE,STATUS_SUCCESS)==STATUS_DEVICE_NOT_READY);
            CHECK(g.Address==&t && !IntakeZero(&t,sizeof(t)) && s.PendingAddress);
            CHECK(ModelSample(&g,&s,&out,0,0)==STATUS_DEVICE_NOT_READY && SourceZero(&out,sizeof(out)));
            CHECK(Bc250IntakeClose(&g)==STATUS_SUCCESS);
            CHECK(Bc250IntakeAbandon(&g,&t)==STATUS_DEVICE_BUSY);
            /* Pinned metadata/Source/remove-lock/work obligations remain quarantined.
             * Fixture storage discarded only as RAM test world, not kernel cleanup. */
        }
    }
}
int main(VOID)
{
    unsigned baseline;
    CHECK(FrozenCompletionFixtures()==0);baseline=checks;
    SampleInterleavings();PairAndPolicy();BridgeMatrix();SetupAndQuarantine();CHECK(!held && !irql);
    printf("PASS: composition %u checks, %u fixtures; guard/Source/bridge historical model, no real wrapper/SMP/hardware.\n",checks-baseline,compositionCases);
    return 0;
}
