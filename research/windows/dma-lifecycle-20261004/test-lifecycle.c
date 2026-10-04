/* SPDX-License-Identifier: Apache-2.0
 * Instrumented RAM world only. Fixture teardown is NOT Windows recovery.
 */
#define BC250_DMA_ADAPTER_MOCK 1
#define main Bc250CpuBackendRegression
#include "../vm-cpu-backend-20261003/test-vm-cpu-backend.c"
#undef main
#include "../dma-windows-20261004/bc250_dma_adapter.c"
#include "bc250_dma_lifecycle.h"

typedef struct BRIDGE_PROVIDER {
    BC250_DMA_BRIDGE *Bridge;
    DEVICE_OBJECT Pdo;
    DMA_ADAPTER Adapter;
    DMA_OPERATIONS Operations;
    SCATTER_GATHER_LIST List;
    MDL Mdl;
    unsigned int Live, Gets, Frees, Puts, Reentry, NoAdapter, BadListOnFailure, Teardown;
    NTSTATUS GetStatus;
} BRIDGE_PROVIDER;
typedef struct BRIDGE_FIXTURE {
    TEST_HEAP Heap;
    BC250_VM_CPU_SESSION Session;
    BC250_DMA_BRIDGE Bridge[2];
    BRIDGE_PROVIDER Provider[2];
    DEVICE_DESCRIPTION Description;
} BRIDGE_FIXTURE;
static BRIDGE_FIXTURE *world;
static BC250_DMA_LIFECYCLE *lifeEvents[2];
static unsigned int stopAtGet;
static void LifeEvent(BRIDGE_PROVIDER *provider);
static unsigned int irql, irqlCalls, faultAtIrql;
BOOLEAN Bc250MockExecutionAllowed = TRUE;

static void BridgeReentry(BRIDGE_PROVIDER *provider)
{
    BC250_DMA_BRIDGE *b = provider->Bridge;
    if (!provider->Reentry || provider->Teardown) return;
    CHECK(Bc250DmaBridgeInit(b, NULL, NULL, NULL) == BC250_VM_SESSION_BUSY_RESULT);
    CHECK(Bc250DmaBridgeBegin(b, 4096, 0) == BC250_VM_SESSION_BUSY_RESULT);
    CHECK(Bc250DmaBridgeAcquire(b, NULL, 0) == BC250_VM_SESSION_BUSY_RESULT);
    CHECK(Bc250DmaBridgePublish(b) == BC250_VM_SESSION_BUSY_RESULT);
    CHECK(Bc250DmaBridgeCancel(b) == BC250_VM_SESSION_BUSY_RESULT);
    CHECK(Bc250DmaBridgeMap(b, NULL, 0, NULL, NULL, NULL, NULL) == BC250_VM_SESSION_BUSY_RESULT);
    CHECK(Bc250DmaBridgeUnmap(b, NULL, NULL, NULL, NULL) == BC250_VM_SESSION_BUSY_RESULT);
    CHECK(Bc250DmaBridgeRelease(b) == BC250_VM_SESSION_BUSY_RESULT);
    CHECK(Bc250DmaBridgeClose(b) == BC250_VM_SESSION_BUSY_RESULT);
}
static BRIDGE_PROVIDER *ProviderFor(PDMA_ADAPTER adapter)
{
    unsigned int i;
    for (i = 0; i < 2; ++i) if (adapter == &world->Provider[i].Adapter) return &world->Provider[i];
    CHECK(0); return NULL;
}
unsigned char KeGetCurrentIrql(void)
{
    ++irqlCalls;
    return (unsigned char)(faultAtIrql && irqlCalls == faultAtIrql ? 1U : irql);
}
static void FakePut(PDMA_ADAPTER adapter)
{
    BRIDGE_PROVIDER *p = ProviderFor(adapter);
    CHECK(!p->Live && !p->Puts); LifeEvent(p); BridgeReentry(p); ++p->Puts;
}
static NTSTATUS FakeInitialize(PDMA_ADAPTER adapter, void *context)
{
    BRIDGE_PROVIDER *p = ProviderFor(adapter);
    CHECK(context == p->Bridge->Transport.TransferContext && !p->Live);
    BridgeReentry(p); return STATUS_SUCCESS;
}
static NTSTATUS FakeGet(PDMA_ADAPTER adapter, PDEVICE_OBJECT device, void *context,
    PMDL mdl, ULONGLONG offset, ULONG bytes, ULONG flags, void *callback,
    void *callbackContext, BOOLEAN direction, void *completion, void *completionContext,
    PSCATTER_GATHER_LIST *out)
{
    BRIDGE_PROVIDER *p = ProviderFor(adapter);
    CHECK(device == &p->Pdo && context == p->Bridge->Transport.TransferContext && mdl == &p->Mdl);
    CHECK(offset == 0 && bytes == p->Bridge->Owner.ExpectedBytes && direction == p->Bridge->Owner.WriteToDevice);
    CHECK(flags == DMA_SYNCHRONOUS_CALLBACK && !callback && !callbackContext && !completion && !completionContext);
    CHECK(out == &p->Bridge->Transport.List && !*out && !p->Live && !p->Gets);
    if (stopAtGet) { CHECK(Bc250DmaLifeRequestStop(lifeEvents[0]) == BC250_VM_SESSION_OK); }
    LifeEvent(p); BridgeReentry(p); ++p->Gets;
    if (p->GetStatus == STATUS_SUCCESS || p->BadListOnFailure) { p->Live = 1; *out = &p->List; }
    return p->GetStatus;
}
static void FakeFree(PDMA_ADAPTER adapter, IO_ALLOCATION_ACTION action)
{
    BRIDGE_PROVIDER *p = ProviderFor(adapter);
    CHECK(action == DeallocateObject && p->Live == 1 && !p->Frees);
    if (!p->Teardown) {
        CHECK(p->Bridge->Busy);
        CHECK(!Bc250BackingHandleMatches(&world->Session.Backing, &p->Bridge->Owner.Backing));
    }
    LifeEvent(p); BridgeReentry(p); ++p->Frees; p->Live = 0;
    memset(&p->List, 0xDD, sizeof(p->List)); /* Retired list must not be read again. */
}
PDMA_ADAPTER IoGetDmaAdapter(PDEVICE_OBJECT device, DEVICE_DESCRIPTION *description, ULONG *registers)
{
    unsigned int i;
    CHECK(description->Version == DEVICE_DESCRIPTION_VERSION3 && irql == 0);
    for (i = 0; i < 2; ++i) if (device == &world->Provider[i].Pdo) {
        BRIDGE_PROVIDER *p = &world->Provider[i]; BridgeReentry(p); *registers = 64;
        return p->NoAdapter ? NULL : &p->Adapter;
    }
    CHECK(0); return NULL;
}
static void Span(BC250_AD_SPAN *span, BC250_AD_U64 va, BC250_AD_U64 bytes)
{
    memset(span, 0, sizeof(*span)); span->Domain = BC250_AD_GPU_VIRTUAL;
    span->Start = va; span->Bytes = bytes; span->Last = va + bytes - 1;
}
static void StartWorld(BRIDGE_FIXTURE *fixture, BC250_AD_U64 maximum)
{
    BC250_VM_LAYOUT layout;
    unsigned int i;
    memset(fixture, 0, sizeof(*fixture)); world = fixture;
    irql = irqlCalls = faultAtIrql = 0; lifeEvents[0] = lifeEvents[1] = NULL; stopAtGet = 0; Bc250MockExecutionAllowed = TRUE;
    fixture->Heap.Context = &fixture->Session.Backend;
    CHECK(Bc250VmPlanLayout(1, 9, 1ULL << 36, &layout));
    CHECK(Bc250VmSessionInit(&fixture->Session, &layout, maximum, 256,
        Allocate, Release, &fixture->Heap) == BC250_VM_SESSION_OK);
    fixture->Heap.Attempts = 0;
    fixture->Description.Version = DEVICE_DESCRIPTION_VERSION3;
    fixture->Description.Master = fixture->Description.ScatterGather = TRUE;
    fixture->Description.InterfaceType = PCIBus; fixture->Description.DmaAddressWidth = 48;
    fixture->Description.MaximumLength = BC250_WIN_DMA_MAX_BYTES;
    for (i = 0; i < 2; ++i) {
        BRIDGE_PROVIDER *p = &fixture->Provider[i];
        p->Bridge = &fixture->Bridge[i]; p->Reentry = 1;
        p->Adapter.Version = 1; p->Adapter.DmaOperations = &p->Operations;
        p->Operations.Size = sizeof(p->Operations); p->Operations.PutDmaAdapter = FakePut;
        p->Operations.InitializeDmaTransferContext = FakeInitialize;
        p->Operations.GetScatterGatherListEx = FakeGet; p->Operations.FreeAdapterObject = FakeFree;
        p->Mdl.MdlFlags = MDL_PAGES_LOCKED; p->Mdl.ByteCount = BC250_WIN_DMA_MAX_BYTES;
        p->List.NumberOfElements = 2;
        p->List.Elements[0].Address.QuadPart = 0x90000; p->List.Elements[0].Length = 8192;
        p->List.Elements[1].Address.QuadPart = 0x31000; p->List.Elements[1].Length = 8192;
        p->GetStatus = STATUS_SUCCESS;
    }
}
static void OpenBridge(unsigned int i)
{
    CHECK(Bc250DmaBridgeInit(&world->Bridge[i], &world->Session, &world->Provider[i].Pdo,
        &world->Description) == BC250_VM_SESSION_OK);
}
static void ReadyBridge(unsigned int i)
{
    BC250_DMA_BRIDGE *b = &world->Bridge[i]; BRIDGE_PROVIDER *p = &world->Provider[i];
    OpenBridge(i); CHECK(Bc250DmaBridgeBegin(b, 16384, i) == BC250_VM_SESSION_OK);
    CHECK(Bc250DmaBridgeAcquire(b, &p->Mdl, 0) == BC250_VM_SESSION_OK);
    CHECK(p->Live && !p->Frees && b->Owner.State == BC250_DMA_OWNER_PENDING);
    CHECK(Bc250DmaBridgePublish(b) == BC250_VM_SESSION_OK);
    CHECK(Bc250DmaBridgePublish(b) == BC250_VM_SESSION_IN_USE && p->Live && !p->Frees);
    CHECK(b->Owner.State == BC250_DMA_OWNER_READY && p->Live && !p->Frees);
    CHECK(world->Session.Backing.Records[b->Owner.Backing.Slot].Pages[0].Start == 0x90000);
    CHECK(world->Session.Backing.Records[b->Owner.Backing.Slot].Pages[1].Start == 0x91000);
    CHECK(world->Session.Backing.Records[b->Owner.Backing.Slot].Pages[2].Start == 0x31000);
}
static void EndWorld(void)
{
    unsigned int i;
    for (i = 0; i < 2; ++i) if (world->Bridge[i].State) {
        if (world->Bridge[i].State != BC250_DMA_BRIDGE_DEAD)
            CHECK(Bc250DmaBridgeClose(&world->Bridge[i]) == BC250_VM_SESSION_OK);
        CHECK(Bc250DmaBridgeClose(&world->Bridge[i]) == BC250_VM_SESSION_DEAD_RESULT);
        CHECK(!world->Provider[i].Live);
    }
    CHECK(Bc250VmSessionShutdown(&world->Session) == BC250_VM_SESSION_OK);
    FinishHeap(&world->Heap); world = NULL;
}
static int BridgeHook(void *context, unsigned int writes)
{
    BRIDGE_PROVIDER *p = context; BridgeReentry(p);
    return AfterWrite(&world->Heap, writes);
}

/* The shared fixtures above are original repo-owned test code, reused from the
 * frozen bridge stage; provider hooks now observe the lifecycle facade too. */
static void LifeEvent(BRIDGE_PROVIDER *provider)
{
    unsigned int i;
    for (i = 0; i < 2; ++i) {
        BC250_DMA_LIFECYCLE *life = lifeEvents[i];
        if (!life || life->Bridge != provider->Bridge || provider->Teardown) continue;
        CHECK(life->Busy);
        CHECK(Bc250DmaLifeEnter(life, BC250_DMA_LIFE_MAP, NULL) == BC250_VM_SESSION_BUSY_RESULT);
        CHECK(Bc250DmaLifeEnd(life, NULL) == BC250_VM_SESSION_BUSY_RESULT);
        CHECK(Bc250DmaLifeDrain(life) == BC250_VM_SESSION_BUSY_RESULT);
        CHECK(Bc250DmaLifeMap(life, NULL, NULL, 0, NULL, NULL, NULL, NULL) == BC250_VM_SESSION_BUSY_RESULT);
        CHECK(Bc250DmaLifeRequestStop(life) == BC250_VM_SESSION_OK);
    }
}
static void AttachLife(BC250_DMA_LIFECYCLE *life, unsigned int index)
{
    memset(life, 0, sizeof(*life));
    CHECK(Bc250DmaLifeInit(life, &world->Bridge[index]) == BC250_VM_SESSION_OK);
    lifeEvents[index] = life;
}
static BC250_DMA_LIFE_TICKET EnterLife(BC250_DMA_LIFECYCLE *life, unsigned int kind)
{
    BC250_DMA_LIFE_TICKET ticket;
    CHECK(Bc250DmaLifeEnter(life, kind, &ticket) == BC250_VM_SESSION_OK);
    return ticket;
}
static int StopHook(void *context, unsigned int writes)
{
    BC250_DMA_LIFECYCLE *life = context;
    CHECK(Bc250DmaLifeRequestStop(life) == BC250_VM_SESSION_OK);
    CHECK(Bc250DmaLifeDrain(life) == BC250_VM_SESSION_BUSY_RESULT);
    CHECK(Bc250DmaLifeEnter(life, BC250_DMA_LIFE_START, NULL) == BC250_VM_SESSION_BUSY_RESULT);
    return BridgeHook(&world->Provider[0], writes);
}
static void RetireLife(BC250_DMA_LIFECYCLE *life)
{
    CHECK(Bc250DmaLifeRequestStop(life) == BC250_VM_SESSION_OK);
    CHECK(Bc250DmaLifeDrain(life) == BC250_VM_SESSION_OK);
    CHECK(life->State == BC250_DMA_LIFE_REMOVED && !life->Active);
    CHECK(Bc250DmaLifeRequestStop(life) == BC250_VM_SESSION_DEAD_RESULT);
    CHECK(Bc250DmaLifeDrain(life) == BC250_VM_SESSION_DEAD_RESULT);
}
static void TestStopDuringStart(void)
{
    BRIDGE_FIXTURE fixture; BC250_DMA_LIFECYCLE life;
    BC250_DMA_LIFE_TICKET ticket, out;
    StartWorld(&fixture, BC250_VM_MAX_ADDRESS); OpenBridge(0); AttachLife(&life, 0);
    ticket = EnterLife(&life, BC250_DMA_LIFE_START); stopAtGet = 1;
    CHECK(Bc250DmaLifeStart(&life, &ticket, &fixture.Provider[0].Mdl, 0, 16384, 0) == BC250_VM_SESSION_OK);
    CHECK(life.State == BC250_DMA_LIFE_STOPPING && life.Active == 1 && fixture.Provider[0].Live);
    CHECK(Bc250DmaLifeEnter(&life, BC250_DMA_LIFE_MAP, &out) == BC250_VM_SESSION_IN_USE);
    CHECK(Bc250DmaLifeStart(&life, &ticket, &fixture.Provider[0].Mdl, 0, 16384, 0) == BC250_VM_SESSION_IN_USE);
    CHECK(Bc250DmaLifeDrain(&life) == BC250_VM_SESSION_IN_USE && !fixture.Provider[0].Frees);
    CHECK(Bc250DmaLifeEnd(&life, &ticket) == BC250_VM_SESSION_OK);
    CHECK(Bc250DmaLifeEnd(&life, &ticket) == BC250_VM_SESSION_STALE);
    RetireLife(&life); CHECK(fixture.Provider[0].Frees == 1 && fixture.Provider[0].Puts == 1);
    EndWorld();
}
static void TestCallsVersusMaps(void)
{
    BRIDGE_FIXTURE fixture; BC250_DMA_LIFECYCLE life;
    BC250_DMA_LIFE_TICKET first, second, ticket, rejected;
    BC250_VM_SESSION_MAPPING_HANDLE mapA, mapB;
    BC250_VM_CPU_JOURNAL journal;
    BC250_AD_SPAN va;
    BC250_AD_U64 digest;
    unsigned int failure;
    StartWorld(&fixture, BC250_VM_MAX_ADDRESS); ReadyBridge(0); AttachLife(&life, 0);
    first = EnterLife(&life, BC250_DMA_LIFE_MAP); second = EnterLife(&life, BC250_DMA_LIFE_MAP);
    CHECK(Bc250DmaLifeRequestStop(&life) == BC250_VM_SESSION_OK);
    CHECK(Bc250DmaLifeEnter(&life, BC250_DMA_LIFE_MAP, &rejected) == BC250_VM_SESSION_IN_USE);
    /* Already admitted work may finish after stop. No new token is admitted. */
    Span(&va, (1ULL << 21) - 4096, 16384);
    CHECK(Bc250DmaLifeMap(&life, &first, &va, 15, &mapA, &journal, StopHook, &life) == BC250_VM_SESSION_OK);
    fixture.Heap.HookCalls = 0; Span(&va, 1ULL << 39, 16384);
    CHECK(Bc250DmaLifeMap(&life, &second, &va, 3, &mapB, &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    CHECK(Bc250DmaLifeEnd(&life, &first) == BC250_VM_SESSION_OK);
    CHECK(Bc250DmaLifeEnd(&life, &second) == BC250_VM_SESSION_OK);
    CHECK(!life.Active && fixture.Session.Backing.Records[fixture.Bridge[0].Owner.Backing.Slot].References == 2);
    CHECK(Bc250DmaLifeDrain(&life) == BC250_VM_SESSION_IN_USE);
    CHECK(life.State == BC250_DMA_LIFE_DRAINING && fixture.Provider[0].Live && !fixture.Provider[0].Frees);
    for (failure = 1; failure <= 4; ++failure) {
        ticket = EnterLife(&life, BC250_DMA_LIFE_UNMAP);
        digest = Digest(&fixture.Session.Backend); fixture.Heap.FailWrite = failure; fixture.Heap.HookCalls = 0;
        CHECK(Bc250DmaLifeUnmap(&life, &ticket, &mapA, &journal, StopHook, &life) == BC250_VM_SESSION_FAULT);
        CHECK(Digest(&fixture.Session.Backend) == digest && fixture.Provider[0].Live && !fixture.Provider[0].Frees);
        CHECK(fixture.Session.Backing.Records[fixture.Bridge[0].Owner.Backing.Slot].References == 2);
        CHECK(Bc250DmaLifeDrain(&life) == BC250_VM_SESSION_IN_USE);
        CHECK(Bc250DmaLifeEnd(&life, &ticket) == BC250_VM_SESSION_OK);
    }
    fixture.Heap.FailWrite = fixture.Heap.HookCalls = 0; ticket = EnterLife(&life, BC250_DMA_LIFE_UNMAP);
    CHECK(Bc250DmaLifeUnmap(&life, &ticket, &mapA, &journal, StopHook, &life) == BC250_VM_SESSION_OK);
    CHECK(Bc250DmaLifeEnd(&life, &ticket) == BC250_VM_SESSION_OK);
    CHECK(Bc250DmaLifeDrain(&life) == BC250_VM_SESSION_IN_USE && fixture.Provider[0].Live);
    ticket = EnterLife(&life, BC250_DMA_LIFE_UNMAP);
    CHECK(Bc250DmaLifeUnmap(&life, &ticket, &mapB, &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    CHECK(Bc250DmaLifeDrain(&life) == BC250_VM_SESSION_IN_USE && !fixture.Provider[0].Frees);
    CHECK(Bc250DmaLifeEnd(&life, &ticket) == BC250_VM_SESSION_OK);
    RetireLife(&life); EndWorld();
}
static void TestTicketGuards(void)
{
    BRIDGE_FIXTURE fixture; BC250_DMA_LIFECYCLE life;
    BC250_DMA_LIFE_TICKET tickets[BC250_DMA_LIFE_SLOTS], out, stale, wrong;
    BC250_VM_SESSION_MAPPING_HANDLE mapping;
    BC250_VM_CPU_JOURNAL journal;
    BC250_AD_SPAN va;
    unsigned char sentinel[sizeof(out)];
    unsigned int i;
    StartWorld(&fixture, BC250_VM_MAX_ADDRESS); ReadyBridge(0); AttachLife(&life, 0);
    memset(&out, 0xA5, sizeof(out)); memcpy(sentinel, &out, sizeof(out));
    for (i = 0; i < BC250_DMA_LIFE_SLOTS; ++i) tickets[i] = EnterLife(&life, BC250_DMA_LIFE_MAP);
    CHECK(Bc250DmaLifeEnter(&life, BC250_DMA_LIFE_MAP, &out) == BC250_VM_SESSION_FULL);
    CHECK(!memcmp(sentinel, &out, sizeof(out)));
    wrong = tickets[0]; ++wrong.Id;
    CHECK(Bc250DmaLifeEnd(&life, &wrong) == BC250_VM_SESSION_STALE && life.Active == 16);
    stale = tickets[0]; CHECK(Bc250DmaLifeEnd(&life, &tickets[0]) == BC250_VM_SESSION_OK);
    out = EnterLife(&life, BC250_DMA_LIFE_UNMAP);
    CHECK(out.Slot == stale.Slot && out.Id > stale.Id);
    CHECK(Bc250DmaLifeEnd(&life, &stale) == BC250_VM_SESSION_STALE && life.Active == 16);
    CHECK(Bc250DmaLifeRequestStop(&life) == BC250_VM_SESSION_OK);
    CHECK(Bc250DmaLifeDrain(&life) == BC250_VM_SESSION_IN_USE && !fixture.Provider[0].Frees);
    Span(&va, 0, 16384);
    CHECK(Bc250DmaLifeMap(&life, &tickets[1], &va, 3, (BC250_VM_SESSION_MAPPING_HANDLE *)&tickets[1],
        &journal, NULL, NULL) == BC250_VM_SESSION_INVALID);
    CHECK(Bc250DmaLifeMap(&life, &tickets[1], &va, 3, &mapping,
        (BC250_VM_CPU_JOURNAL *)&life, NULL, NULL) == BC250_VM_SESSION_INVALID);
    CHECK(!life.Records[tickets[1].Slot].Used);
    CHECK(Bc250DmaLifeEnd(&life, &out) == BC250_VM_SESSION_OK);
    for (i = 1; i < BC250_DMA_LIFE_SLOTS; ++i) CHECK(Bc250DmaLifeEnd(&life, &tickets[i]) == BC250_VM_SESSION_OK);
    /* Unused tickets still require End; this is cancellation of admitted work. */
    RetireLife(&life);
    CHECK(Bc250DmaLifeEnter(&life, BC250_DMA_LIFE_UNMAP, &out) == BC250_VM_SESSION_DEAD_RESULT);
    EndWorld();
    StartWorld(&fixture, BC250_VM_MAX_ADDRESS); ReadyBridge(0); AttachLife(&life, 0);
    life.LastId = BC250_GART_U64_MAX; /* Test-only monotonic exhaustion fixture. */
    CHECK(Bc250DmaLifeEnter(&life, BC250_DMA_LIFE_MAP, &out) == BC250_VM_SESSION_EXHAUSTED);
    RetireLife(&life); EndWorld();
}
static void TestStopStagesAndIsolation(void)
{
    BRIDGE_FIXTURE fixture; BC250_DMA_LIFECYCLE life[2];
    BC250_DMA_LIFE_TICKET token;
    unsigned int stage;
    for (stage = 0; stage < 3; ++stage) {
        StartWorld(&fixture, BC250_VM_MAX_ADDRESS); OpenBridge(0);
        if (stage >= 1) CHECK(Bc250DmaBridgeBegin(&fixture.Bridge[0], 16384, 0) == BC250_VM_SESSION_OK);
        if (stage >= 2) CHECK(Bc250DmaBridgeAcquire(&fixture.Bridge[0], &fixture.Provider[0].Mdl, 0) == BC250_VM_SESSION_OK);
        AttachLife(&life[0], 0); RetireLife(&life[0]);
        CHECK(fixture.Provider[0].Frees == (stage >= 2 ? 1U : 0U)); EndWorld();
    }
    StartWorld(&fixture, BC250_VM_MAX_ADDRESS); ReadyBridge(0); ReadyBridge(1);
    AttachLife(&life[0], 0); AttachLife(&life[1], 1);
    token = EnterLife(&life[1], BC250_DMA_LIFE_MAP);
    RetireLife(&life[0]);
    CHECK(fixture.Provider[1].Live && !fixture.Provider[1].Frees && life[1].Active == 1);
    CHECK(Bc250DmaLifeEnd(&life[1], &token) == BC250_VM_SESSION_OK);
    RetireLife(&life[1]); EndWorld();
}
static void TestFaultPreservesProvider(void)
{
    BRIDGE_FIXTURE fixture; BC250_DMA_LIFECYCLE life;
    BC250_DMA_LIFE_TICKET token;
    StartWorld(&fixture, BC250_VM_MAX_ADDRESS); OpenBridge(0); AttachLife(&life, 0);
    token = EnterLife(&life, BC250_DMA_LIFE_START); fixture.Provider[0].GetStatus = STATUS_PENDING;
    CHECK(Bc250DmaLifeStart(&life, &token, &fixture.Provider[0].Mdl, 0, 16384, 0) == BC250_VM_SESSION_FAULT);
    CHECK(life.State == BC250_DMA_LIFE_FAULT && life.Active == 1);
    CHECK(Bc250DmaLifeEnd(&life, &token) == BC250_VM_SESSION_OK && !life.Active);
    CHECK(Bc250DmaLifeDrain(&life) == BC250_VM_SESSION_FAULT && !fixture.Provider[0].Puts);
    CHECK(fixture.Bridge[0].Transport.HeldMdl == &fixture.Provider[0].Mdl);
    /* Explicit destruction of an intentionally broken, callback-free RAM world,
     * NOT public recovery or proof a Windows pending request is finished. */
    lifeEvents[0] = NULL;
    CHECK(Bc250DmaOwnerResolveNoResource(&fixture.Bridge[0].Owner, &fixture.Bridge[0].Ticket) == BC250_VM_SESSION_OK);
    CHECK(Bc250DmaOwnerShutdown(&fixture.Bridge[0].Owner) == BC250_VM_SESSION_OK);
    CHECK(Bc250VmSessionShutdown(&fixture.Session) == BC250_VM_SESSION_OK);
    FinishHeap(&fixture.Heap); world = NULL;
}
static void TestPolicyAndMetadata(void)
{
    BRIDGE_FIXTURE fixture; BC250_DMA_LIFECYCLE life, snapshot;
    BC250_DMA_LIFE_TICKET token, wrong;
    StartWorld(&fixture, BC250_VM_MAX_ADDRESS); ReadyBridge(0); AttachLife(&life, 0);
    CHECK(Bc250DmaLifeInit(&life, &fixture.Bridge[0]) == BC250_VM_SESSION_INVALID);
    CHECK(Bc250DmaLifeDrain(&life) == BC250_VM_SESSION_IN_USE);
    token = EnterLife(&life, BC250_DMA_LIFE_MAP); snapshot = life;
    Bc250MockExecutionAllowed = FALSE;
    CHECK(Bc250DmaLifeRequestStop(&life) == BC250_VM_SESSION_INVALID);
    CHECK(Bc250DmaLifeEnd(&life, &token) == BC250_VM_SESSION_INVALID);
    CHECK(!memcmp(&snapshot, &life, sizeof(life)));
    Bc250MockExecutionAllowed = TRUE; irql = 1;
    CHECK(Bc250DmaLifeRequestStop(&life) == BC250_VM_SESSION_INVALID);
    CHECK(Bc250DmaLifeDrain(&life) == BC250_VM_SESSION_INVALID);
    CHECK(!memcmp(&snapshot, &life, sizeof(life))); irql = 0;
    ++life.Active;
    CHECK(Bc250DmaLifeRequestStop(&life) == BC250_VM_SESSION_CORRUPT);
    CHECK(Bc250DmaLifeEnd(&life, &token) == BC250_VM_SESSION_CORRUPT);
    CHECK(!fixture.Provider[0].Frees); life = snapshot; /* Instrumented RAM corruption only. */
    wrong = token; wrong.Life = NULL;
    CHECK(Bc250DmaLifeEnd(&life, &wrong) == BC250_VM_SESSION_STALE);
    CHECK(Bc250DmaLifeEnd(&life, &token) == BC250_VM_SESSION_OK);
    RetireLife(&life); EndWorld();
}
int main(void)
{
    unsigned int baseline;
    CHECK(Bc250CpuBackendRegression() == 0); baseline = assertions;
    TestStopDuringStart(); TestCallsVersusMaps(); TestTicketGuards();
    TestStopStagesAndIsolation(); TestFaultPreservesProvider(); TestPolicyAndMetadata();
    printf("PASS: %u additional lifecycle assertions (%u total); STOP/drain RAM events, NOT Windows PnP/rundown\n",
        assertions - baseline, assertions);
    return 0;
}
