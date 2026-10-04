/* SPDX-License-Identifier: Apache-2.0
 * Instrumented RAM world only. Fixture teardown is NOT Windows recovery.
 */
#define BC250_DMA_ADAPTER_MOCK 1
#define main Bc250CpuBackendRegression
#include "../vm-cpu-backend-20261003/test-vm-cpu-backend.c"
#undef main
#include "../dma-windows-20261004/bc250_dma_adapter.c"
#include "bc250_dma_bridge.h"

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
    CHECK(!p->Live && !p->Puts); BridgeReentry(p); ++p->Puts;
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
    BridgeReentry(p); ++p->Gets;
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
    BridgeReentry(p); ++p->Frees; p->Live = 0;
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
    irql = irqlCalls = faultAtIrql = 0; Bc250MockExecutionAllowed = TRUE;
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
static void TestReferenceDrain(void)
{
    BRIDGE_FIXTURE fixture;
    BC250_DMA_BRIDGE *b = &fixture.Bridge[0]; BRIDGE_PROVIDER *p = &fixture.Provider[0];
    BC250_VM_SESSION_MAPPING_HANDLE first, second, blocked;
    BC250_VM_CPU_JOURNAL journal;
    BC250_AD_SPAN va, other;
    BC250_AD_U64 digest;
    unsigned int failure;
    StartWorld(&fixture, BC250_VM_MAX_ADDRESS); ReadyBridge(0);
    Span(&va, (1ULL << 21) - 4096, 16384); Span(&other, 1ULL << 39, 16384);
    fixture.Heap.FailAllocation = 1; digest = Digest(&fixture.Session.Backend);
    CHECK(Bc250DmaBridgeMap(b, &va, 15, &blocked, &journal, NULL, NULL) == BC250_VM_SESSION_NO_MEMORY);
    CHECK(Digest(&fixture.Session.Backend) == digest && p->Live && !p->Frees);
    CHECK(!fixture.Session.Backing.Records[b->Owner.Backing.Slot].References);
    fixture.Heap.FailAllocation = 0;
    for (failure = 1; failure <= 4; ++failure) {
        fixture.Heap.FailWrite = failure; fixture.Heap.HookCalls = 0;
        CHECK(Bc250DmaBridgeMap(b, &va, 15, &blocked, &journal, BridgeHook, p) == BC250_VM_SESSION_FAULT);
        CHECK(Digest(&fixture.Session.Backend) == digest && p->Live && !p->Frees);
        CHECK(!fixture.Session.Backing.Records[b->Owner.Backing.Slot].References);
    }
    fixture.Heap.FailWrite = fixture.Heap.HookCalls = 0;
    CHECK(Bc250DmaBridgeMap(b, &va, 15, &first, &journal, BridgeHook, p) == BC250_VM_SESSION_OK);
    CHECK(Bc250DmaBridgeMap(b, &other, 3, &second, &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    CHECK(fixture.Session.Backing.Records[b->Owner.Backing.Slot].References == 2);
    CHECK(Bc250DmaBridgeRelease(b) == BC250_VM_SESSION_IN_USE && p->Live && !p->Frees);
    CHECK(Bc250DmaBridgeClose(b) == BC250_VM_SESSION_IN_USE && !p->Puts);
    CHECK(Bc250DmaBridgeCancel(b) == BC250_VM_SESSION_OK);
    CHECK(Bc250DmaBridgeCancel(b) == BC250_VM_SESSION_OK && p->Live && !p->Frees);
    CHECK(b->Owner.State == BC250_DMA_OWNER_DRAINING && !b->Transport.Cancelled);
    CHECK(Bc250DmaBridgeMap(b, &va, 15, &blocked, &journal, NULL, NULL) == BC250_VM_SESSION_IN_USE);
    for (failure = 1; failure <= 4; ++failure) {
        digest = Digest(&fixture.Session.Backend); fixture.Heap.FailWrite = failure; fixture.Heap.HookCalls = 0;
        CHECK(Bc250DmaBridgeUnmap(b, &first, &journal, BridgeHook, p) == BC250_VM_SESSION_FAULT);
        CHECK(Digest(&fixture.Session.Backend) == digest && p->Live && !p->Frees);
        CHECK(fixture.Session.Backing.Records[b->Owner.Backing.Slot].References == 2);
    }
    fixture.Heap.FailWrite = fixture.Heap.HookCalls = 0;
    CHECK(Bc250DmaBridgeUnmap(b, &first, &journal, BridgeHook, p) == BC250_VM_SESSION_OK);
    CHECK(Bc250DmaBridgeRelease(b) == BC250_VM_SESSION_IN_USE && p->Live);
    CHECK(Bc250DmaBridgeUnmap(b, &first, &journal, NULL, NULL) == BC250_VM_SESSION_STALE);
    CHECK(Bc250DmaBridgeUnmap(b, &second, &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    CHECK(Bc250DmaBridgeRelease(b) == BC250_VM_SESSION_OK);
    CHECK(!p->Live && p->Frees == 1 && b->Returned == 1 && !p->Puts);
    CHECK(Bc250DmaBridgeRelease(b) == BC250_VM_SESSION_STALE && p->Frees == 1);
    CHECK(Bc250DmaBridgeBegin(b, 4096, 0) == BC250_VM_SESSION_IN_USE);
    EndWorld();
}
static void TestCancellationStages(void)
{
    BRIDGE_FIXTURE fixture;
    unsigned int stage, direction;
    for (stage = 0; stage < 4; ++stage) for (direction = 0; direction <= 1; ++direction) {
        BC250_DMA_BRIDGE *b = &fixture.Bridge[direction]; BRIDGE_PROVIDER *p = &fixture.Provider[direction];
        StartWorld(&fixture, BC250_VM_MAX_ADDRESS); OpenBridge(direction);
        if (stage >= 1) CHECK(Bc250DmaBridgeBegin(b, 16384, direction) == BC250_VM_SESSION_OK);
        if (stage >= 2) CHECK(Bc250DmaBridgeAcquire(b, &p->Mdl, 0) == BC250_VM_SESSION_OK);
        if (stage >= 3) CHECK(Bc250DmaBridgePublish(b) == BC250_VM_SESSION_OK);
        CHECK(Bc250DmaBridgeCancel(b) == BC250_VM_SESSION_OK);
        if (stage == 3) {
            CHECK(p->Live && !p->Frees && b->Owner.State == BC250_DMA_OWNER_DRAINING);
            CHECK(Bc250DmaBridgeRelease(b) == BC250_VM_SESSION_OK);
        }
        CHECK(b->Owner.State == BC250_DMA_OWNER_IDLE && !p->Live);
        CHECK(p->Gets == (stage >= 2 ? 1U : 0U) && p->Frees == p->Gets);
        CHECK(Bc250DmaBridgeCancel(b) == BC250_VM_SESSION_OK && p->Frees == p->Gets);
        EndWorld();
    }
}
static void TestFailureAndSharedOwner(void)
{
    BRIDGE_FIXTURE fixture;
    BC250_DMA_BRIDGE *a = &fixture.Bridge[0], *b = &fixture.Bridge[1];
    BC250_VM_SESSION_MAPPING_HANDLE first, second;
    BC250_VM_CPU_JOURNAL journal;
    BC250_AD_SPAN va;
    StartWorld(&fixture, BC250_VM_MAX_ADDRESS); OpenBridge(0);
    CHECK(Bc250DmaBridgeBegin(a, 16384, 0) == BC250_VM_SESSION_OK);
    fixture.Provider[0].GetStatus = STATUS_INSUFFICIENT_RESOURCES;
    CHECK(Bc250DmaBridgeAcquire(a, &fixture.Provider[0].Mdl, 0) == BC250_VM_SESSION_FAULT);
    CHECK(a->Owner.State == BC250_DMA_OWNER_IDLE && !fixture.Provider[0].Frees);
    EndWorld();
    StartWorld(&fixture, 0x1FFF); OpenBridge(0);
    CHECK(Bc250DmaBridgeBegin(a, 16384, 0) == BC250_VM_SESSION_OK);
    CHECK(Bc250DmaBridgeAcquire(a, &fixture.Provider[0].Mdl, 0) == BC250_VM_SESSION_OK);
    CHECK(Bc250DmaBridgePublish(a) == BC250_VM_SESSION_INVALID);
    CHECK(a->Owner.State == BC250_DMA_OWNER_IDLE && fixture.Provider[0].Frees == 1);
    EndWorld();
    StartWorld(&fixture, BC250_VM_MAX_ADDRESS); ReadyBridge(0); ReadyBridge(1);
    Span(&va, 0, 16384);
    CHECK(Bc250DmaBridgeMap(a, &va, 3, &first, &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    Span(&va, 1ULL << 39, 16384);
    CHECK(Bc250DmaBridgeMap(b, &va, 3, &second, &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    CHECK(Bc250DmaBridgeUnmap(a, &second, &journal, NULL, NULL) == BC250_VM_SESSION_STALE);
    CHECK(!fixture.Provider[0].Frees && !fixture.Provider[1].Frees);
    CHECK(Bc250DmaBridgeCancel(a) == BC250_VM_SESSION_OK);
    CHECK(Bc250DmaBridgeUnmap(a, &first, &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    CHECK(Bc250DmaBridgeRelease(a) == BC250_VM_SESSION_OK && fixture.Provider[0].Frees == 1);
    CHECK(fixture.Provider[1].Live && !fixture.Provider[1].Frees);
    CHECK(Bc250DmaBridgeUnmap(b, &second, &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    CHECK(Bc250DmaBridgeRelease(b) == BC250_VM_SESSION_OK); EndWorld();
}
static void TestGuardsAndCleanupFault(void)
{
    BRIDGE_FIXTURE fixture;
    BC250_DMA_BRIDGE *b = &fixture.Bridge[0], before;
    BC250_VM_SESSION_MAPPING_HANDLE mapping;
    BC250_VM_CPU_JOURNAL journal;
    BC250_AD_SPAN va;
    StartWorld(&fixture, BC250_VM_MAX_ADDRESS);
    Bc250MockExecutionAllowed = FALSE;
    CHECK(Bc250DmaBridgeInit(b, &fixture.Session, NULL, NULL) == BC250_VM_SESSION_INVALID && irqlCalls == 0);
    Bc250MockExecutionAllowed = TRUE; OpenBridge(0); before = *b; irql = 1;
    CHECK(Bc250DmaBridgeBegin(b, 4096, 0) == BC250_VM_SESSION_INVALID);
    CHECK(Bc250DmaBridgeCancel(b) == BC250_VM_SESSION_INVALID);
    CHECK(Bc250DmaBridgeClose(b) == BC250_VM_SESSION_INVALID);
    CHECK(!memcmp(b, &before, sizeof(before))); irql = 0;
    CHECK(Bc250DmaBridgeBegin(b, 16384, 0) == BC250_VM_SESSION_OK);
    CHECK(Bc250DmaBridgeAcquire(b, (PMDL)&b->Runs, 0) == BC250_VM_SESSION_INVALID);
    CHECK(Bc250DmaBridgeAcquire(b, &fixture.Provider[0].Mdl, 0) == BC250_VM_SESSION_OK);
    CHECK(Bc250DmaBridgePublish(b) == BC250_VM_SESSION_OK);
    Span(&va, 0, 16384);
    CHECK(Bc250DmaBridgeMap(b, &va, 3, (BC250_VM_SESSION_MAPPING_HANDLE *)&b->Transport,
        &journal, NULL, NULL) == BC250_VM_SESSION_INVALID);
    CHECK(Bc250DmaBridgeMap(b, &va, 3, &mapping,
        (BC250_VM_CPU_JOURNAL *)&b->Runs, NULL, NULL) == BC250_VM_SESSION_INVALID);
    /* Synthetic IRQL failure exactly at native Release guard, after numeric
     * unregister. A void Put must not hide native failure from the facade. */
    faultAtIrql = irqlCalls + 2;
    CHECK(Bc250DmaBridgeRelease(b) == BC250_VM_SESSION_FAULT);
    CHECK(b->State == BC250_DMA_BRIDGE_FAULT && b->Owner.State == BC250_DMA_OWNER_IDLE);
    CHECK(b->Transport.State == BC250_WIN_DMA_HELD && fixture.Provider[0].Live && !fixture.Provider[0].Frees);
    CHECK(Bc250DmaBridgeClose(b) == BC250_VM_SESSION_FAULT && !fixture.Provider[0].Puts);
    /* Explicit test-world teardown, NOT a public bridge recovery operation. */
    faultAtIrql = 0; fixture.Provider[0].Teardown = 1;
    CHECK(Bc250WinDmaRelease(&b->Transport) == STATUS_SUCCESS);
    CHECK(Bc250WinDmaClose(&b->Transport) == STATUS_SUCCESS);
    CHECK(Bc250DmaOwnerShutdown(&b->Owner) == BC250_VM_SESSION_OK);
    CHECK(Bc250VmSessionShutdown(&fixture.Session) == BC250_VM_SESSION_OK);
    FinishHeap(&fixture.Heap); world = NULL;
}
static void TestPublishAndUnknownProvider(void)
{
    BRIDGE_FIXTURE fixture;
    BC250_DMA_BRIDGE *b = &fixture.Bridge[0]; BRIDGE_PROVIDER *p = &fixture.Provider[0];
    unsigned int scenario;
    StartWorld(&fixture, BC250_VM_MAX_ADDRESS); p->NoAdapter = 1;
    CHECK(Bc250DmaBridgeInit(b, &fixture.Session, &p->Pdo, &fixture.Description) == BC250_VM_SESSION_FAULT);
    CHECK(!b->Begun && b->Owner.State == BC250_DMA_OWNER_IDLE && !b->Transport.Adapter);
    CHECK(Bc250DmaBridgeClose(b) == BC250_VM_SESSION_OK && !p->Puts); EndWorld();
    for (scenario = 0; scenario < 2; ++scenario) {
        StartWorld(&fixture, BC250_VM_MAX_ADDRESS); OpenBridge(0);
        CHECK(Bc250DmaBridgeBegin(b, 16384, 0) == BC250_VM_SESSION_OK);
        CHECK(Bc250DmaBridgeAcquire(b, &p->Mdl, 0) == BC250_VM_SESSION_OK);
        if (scenario == 0) b->Transport.List = (PSCATTER_GATHER_LIST)b->Runs; /* overlap injection */
        else {
            b->Transport.AddressWidth = 32; /* narrow declaration + out-of-width list injection */
            p->List.Elements[1].Address.QuadPart = 1LL << 32;
        }
        CHECK(Bc250DmaBridgePublish(b) == BC250_VM_SESSION_INVALID);
        CHECK(b->Owner.State == BC250_DMA_OWNER_IDLE && p->Frees == 1 && b->Returned == 1);
        EndWorld();
    }
    for (scenario = 0; scenario < 3; ++scenario) {
        StartWorld(&fixture, BC250_VM_MAX_ADDRESS); OpenBridge(0);
        CHECK(Bc250DmaBridgeBegin(b, 16384, 0) == BC250_VM_SESSION_OK);
        if (scenario < 2) {
            p->GetStatus = scenario == 0 ? STATUS_PENDING : STATUS_INSUFFICIENT_RESOURCES;
            p->BadListOnFailure = scenario == 1;
            CHECK(Bc250DmaBridgeAcquire(b, &p->Mdl, 0) == BC250_VM_SESSION_FAULT);
        } else {
            CHECK(Bc250DmaBridgeAcquire(b, &p->Mdl, 0) == BC250_VM_SESSION_OK);
            ++b->Ticket.Id; /* stale internal completion identity injection */
            CHECK(Bc250DmaBridgePublish(b) == BC250_VM_SESSION_FAULT);
        }
        CHECK(b->State == BC250_DMA_BRIDGE_FAULT && b->Owner.State == BC250_DMA_OWNER_PENDING);
        CHECK(b->Transport.Adapter == &p->Adapter && b->Transport.HeldMdl == &p->Mdl);
        CHECK(!p->Frees && !p->Puts);
        CHECK(Bc250DmaBridgeClose(b) == BC250_VM_SESSION_FAULT);
        /* Destroy this deliberately broken, callback-free FAKE provider world.
         * This supplies explicit simulator-only no-resource/no-future-event
         * proof; it is NOT inferred from timeout and NOT Windows recovery. */
        p->Live = 0; b->Ticket.Id = b->Owner.LastId;
        CHECK(Bc250DmaOwnerResolveNoResource(&b->Owner, &b->Ticket) == BC250_VM_SESSION_OK);
        CHECK(Bc250DmaOwnerShutdown(&b->Owner) == BC250_VM_SESSION_OK);
        CHECK(Bc250VmSessionShutdown(&fixture.Session) == BC250_VM_SESSION_OK);
        FinishHeap(&fixture.Heap); world = NULL;
    }
}
int main(void)
{
    unsigned int baseline;
    CHECK(Bc250CpuBackendRegression() == 0); baseline = assertions;
    TestReferenceDrain(); TestCancellationStages(); TestFailureAndSharedOwner(); TestGuardsAndCleanupFault();
    TestPublishAndUnknownProvider();
    printf("PASS: %u additional bridge assertions (%u total); reference-retained RAM fakes, NO Windows DMA/GPU\n",
        assertions - baseline, assertions);
    return 0;
}
