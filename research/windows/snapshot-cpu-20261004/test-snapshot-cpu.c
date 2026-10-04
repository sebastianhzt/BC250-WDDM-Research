/* SPDX-License-Identifier: Apache-2.0
 * Isolated fake worlds; corruption/lock injection is TEST instrumentation,
 * never a public recovery API. Aggregate public snapshot, not per-map DMA.
 */
#define frees scDmaFrees
#define live scDmaLive
#define main ScFrozenCaptureTests
#include "../dma-capture-20261004/test-capture.c"
#undef main
#undef frees
#undef live
#undef owner
#undef CHECK
#define main ScFrozenCpuTests
#include "../vm-cpu-backend-20261003/test-vm-cpu-backend.c"
#undef main
#include "bc250_snapshot_cpu.h"
typedef struct SC_WORLD {
    TEST_HEAP Heap;
    BC250_VM_CPU_SESSION Session;
    BC250_SNAPSHOT_CPU Facade;
    BC250_DMA_CAPTURE_HANDLE Seed;
    BC250_DMA_CAPTURE_REQUEST Expected;
} SC_WORLD;
static void PrepareSc(SC_WORLD *w, unsigned int pages)
{
    BC250_VM_LAYOUT layout;
    memset(w, 0, sizeof(*w)); ResetLeases(); OpenLeases();
    mdl.ByteCount += 8192; /* Fake MDL contains request PLUS nonzero offset. */
    w->Seed = AcquireRequest(pages, 8192, FALSE);
    w->Expected.Offset = 8192; w->Expected.Bytes = pages * 4096; w->Expected.Direction = FALSE;
    CHECK(Bc250VmPlanLayout(BC250_VM_PDB2, 9, 1ULL << 30, &layout));
    w->Heap.Context = &w->Session.Backend;
    CHECK(Bc250VmSessionInit(&w->Session, &layout, 0x0000FFFFFFFFFFFFULL, 128,
        Allocate, Release, &w->Heap) == BC250_VM_SESSION_OK);
    w->Heap.Attempts = 0;
}
static void InitSc(SC_WORLD *w)
{
    CHECK(Bc250ScInit(&w->Facade, &leaseFixture, &w->Seed, &w->Expected, &w->Session) == BC250_VM_SESSION_OK);
    CHECK(Bc250ScCheck(&w->Facade) == BC250_VM_SESSION_OK);
    CHECK(w->Facade.HasSnapshot && w->Facade.HasBacking && leaseFixture.References == 2);
}
static void DropScSeed(SC_WORLD *w)
{
    CHECK(Bc250DmaCaptureDrop(&leaseFixture, &w->Seed) == STATUS_SUCCESS);
    memset(&w->Seed, 0, sizeof(w->Seed)); /* caller-owned, consumed exactly once */
}
static void EndSc(SC_WORLD *w)
{
    CHECK(Bc250ScRetire(&w->Facade) == BC250_VM_SESSION_OK);
    CHECK(w->Facade.State == BC250_SC_DEAD && scDmaFrees == 1 && adapterPuts == 1);
    CHECK(Bc250ScRetire(&w->Facade) == BC250_VM_SESSION_DEAD_RESULT);
    CheckRetired();
    CHECK(Bc250VmSessionShutdown(&w->Session) == BC250_VM_SESSION_OK); FinishHeap(&w->Heap);
}
static int ScHook(void *context, unsigned int count)
{
    SC_WORLD *w = context;
    CHECK(Bc250ScRetire(&w->Facade) == BC250_VM_SESSION_BUSY_RESULT);
    CHECK(scDmaLive && !scDmaFrees && leaseFixture.References == 1);
    return AfterWrite(&w->Heap, count);
}
static int ScStopHook(void *context, unsigned int count)
{
    SC_WORLD *w = context;
    CHECK(Bc250ScRetire(&w->Facade) == BC250_VM_SESSION_BUSY_RESULT);
    CHECK(Bc250ScStop(&w->Facade) == BC250_VM_SESSION_OK);
    return AfterWrite(&w->Heap, count);
}
static void TestScMaps(void)
{
    SC_WORLD *w = calloc(1, sizeof(*w));
    BC250_VM_SESSION_MAPPING_HANDLE first = {0}, alias = {0}, failed = {0};
    BC250_VM_CPU_JOURNAL journal;
    BC250_GART_U64 digest;
    unsigned int i, failure;
    const BC250_GART_U64 expected[4] = {0xB00000, 0xB01000, 0x50000, 0xB00000};
    CHECK(w != NULL); PrepareSc(w, 4); InitSc(w); DropScSeed(w);
    CHECK(w->Facade.Snapshot.Request.Offset == observedGetOffset);
    CHECK(w->Facade.Snapshot.Request.Bytes == observedGetBytes);
    CHECK(w->Facade.Snapshot.Request.Direction == observedGetDirection);
    for (i = 0; i < 4; ++i) CHECK(w->Facade.Snapshot.Pages[i].Start == expected[i]);
    digest = Digest(&w->Session.Backend);
    CHECK(Bc250ScMap(&w->Facade, 0x1FF000, 15, (BC250_VM_SESSION_MAPPING_HANDLE *)&w->Facade,
        &journal, NULL, NULL) == BC250_VM_SESSION_INVALID);
    CHECK(Bc250ScMap(&w->Facade, 0x1FF000, 15, &failed,
        (BC250_VM_CPU_JOURNAL *)&w->Facade, NULL, NULL) == BC250_VM_SESSION_INVALID);
    w->Heap.FailAllocation = 1;
    CHECK(Bc250ScMap(&w->Facade, 0x1FF000, 15, &failed, &journal, NULL, NULL) == BC250_VM_SESSION_NO_MEMORY);
    CHECK(!failed.Session && leaseFixture.References == 1 && Digest(&w->Session.Backend) == digest);
    w->Heap.FailAllocation = 0;
    for (failure = 1; failure <= 4; ++failure) {
        w->Heap.FailWrite = failure; w->Heap.HookCalls = 0;
        CHECK(Bc250ScMap(&w->Facade, 0x1FF000, 15, &failed, &journal, ScHook, w) == BC250_VM_SESSION_FAULT);
        CHECK(w->Facade.State == BC250_SC_ACTIVE && !failed.Session && leaseFixture.References == 1);
        CHECK(Digest(&w->Session.Backend) == digest && Bc250ScCheck(&w->Facade) == BC250_VM_SESSION_OK);
    }
    w->Heap.FailWrite = w->Heap.HookCalls = 0;
    CHECK(Bc250ScMap(&w->Facade, 0x1FF000, 15, &first, &journal, ScHook, w) == BC250_VM_SESSION_OK);
    CHECK(Bc250ScMap(&w->Facade, 0x800000, 15, &alias, &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    CHECK(leaseFixture.References == 1 && w->Session.Backing.Records[w->Facade.Backing.Slot].References == 2);
    for (i = 0; i < 4; ++i) {
        CheckValue(&w->Session.Backend, 0x1FF000 + (ULONGLONG)i * 4096, expected[i] | 0x77);
        CheckValue(&w->Session.Backend, 0x800000 + (ULONGLONG)i * 4096, expected[i] | 0x77);
    }
    CHECK(Bc250ScRetire(&w->Facade) == BC250_VM_SESSION_IN_USE);
    CHECK(!w->Facade.BackingReleased && !w->Facade.SnapshotReleased && !scDmaFrees);
    digest = Digest(&w->Session.Backend); w->Heap.FailWrite = 2; w->Heap.HookCalls = 0;
    CHECK(Bc250ScUnmap(&w->Facade, &first, &journal, AfterWrite, &w->Heap) == BC250_VM_SESSION_FAULT);
    CHECK(leaseFixture.References == 1 && Digest(&w->Session.Backend) == digest);
    CHECK(Bc250ScRetire(&w->Facade) == BC250_VM_SESSION_IN_USE);
    w->Heap.FailWrite = w->Heap.HookCalls = 0;
    CHECK(Bc250ScUnmap(&w->Facade, &first, &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    CHECK(Bc250ScUnmap(&w->Facade, &first, &journal, NULL, NULL) == BC250_VM_SESSION_STALE);
    CHECK(Bc250ScUnmap(&w->Facade, &alias, &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    EndSc(w); free(w);
}
static void TestScRetries(void)
{
    SC_WORLD *w = calloc(1, sizeof(*w));
    BC250_VM_SESSION_MAPPING_HANDLE map = {0}, failed = {0};
    BC250_VM_CPU_JOURNAL journal;
    unsigned int i;
    CHECK(w != NULL); PrepareSc(w, 64); InitSc(w); DropScSeed(w);
    CHECK(Bc250ScMap(&w->Facade, 0x1FF000, 3, &map, &journal, ScStopHook, w) == BC250_VM_SESSION_OK);
    CHECK(w->Facade.State == BC250_SC_STOPPED);
    CHECK(Bc250ScMap(&w->Facade, 0x800000, 3, &failed, &journal, NULL, NULL) == BC250_VM_SESSION_IN_USE);
    for (i = 0; i < 64; ++i) CheckValue(&w->Session.Backend, 0x1FF000 + (ULONGLONG)i * 4096,
        (0xB00000 + (ULONGLONG)i * 4096) | 0x63);
    CHECK(Bc250ScUnmap(&w->Facade, &map, &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    threadIndex = 1; CHECK(Bc250DmaCaptureLock(&leaseFixture) == STATUS_SUCCESS); threadIndex = 0;
    CHECK(Bc250ScRetire(&w->Facade) == BC250_VM_SESSION_BUSY_RESULT);
    CHECK(w->Facade.BackingReleased && !w->Facade.SnapshotReleased && leaseFixture.References == 1 && !scDmaFrees);
    CHECK(Bc250ScCheck(&w->Facade) == BC250_VM_SESSION_OK);
    threadIndex = 1; Bc250DmaCaptureUnlock(&leaseFixture); threadIndex = 0;
    EndSc(w);
    PrepareSc(w, 1); InitSc(w); /* Seed intentionally held through snapshot release. */
    CHECK(Bc250ScRetire(&w->Facade) == BC250_VM_SESSION_BUSY_RESULT);
    CHECK(w->Facade.BackingReleased && w->Facade.SnapshotReleased && leaseFixture.References == 1 && !scDmaFrees);
    CHECK(Bc250ScCheck(&w->Facade) == BC250_VM_SESSION_OK);
    DropScSeed(w); EndSc(w); free(w);
}
static void TestScFailures(void)
{
    SC_WORLD *w = calloc(1, sizeof(*w));
    unsigned int scenario;
    CHECK(w != NULL);
    for (scenario = 0; scenario < 4; ++scenario) {
        PrepareSc(w, 1);
        if (scenario == 0) ++w->Expected.Offset;
        if (scenario == 1) w->Expected.Bytes += 4096;
        if (scenario == 2) w->Expected.Direction = TRUE;
        if (scenario == 3) w->Session.Backend.MaximumDmaLast = 0xFFFFF;
        CHECK(Bc250ScInit(&w->Facade, &leaseFixture, &w->Seed, &w->Expected, &w->Session) == BC250_VM_SESSION_INVALID);
        CHECK(w->Facade.State == BC250_SC_STOPPED && !w->Facade.HasBacking);
        CHECK(w->Facade.HasSnapshot == (scenario == 3 ? 1U : 0U));
        DropScSeed(w); EndSc(w);
    }
    PrepareSc(w, 1); stopAtAcquire = 1;
    CHECK(Bc250ScInit(&w->Facade, &leaseFixture, &w->Seed, &w->Expected, &w->Session) == BC250_VM_SESSION_IN_USE);
    CHECK(!w->Facade.HasSnapshot && !w->Facade.HasBacking);
    stopAtAcquire = 0; DropScSeed(w); EndSc(w);
    for (scenario = 0; scenario < 3; ++scenario) {
        BC250_VM_SESSION_MAPPING_HANDLE map = {0};
        BC250_VM_CPU_JOURNAL journal;
        PrepareSc(w, 1);
        if (!scenario) {
            faultSnapshotPublication = 1;
            CHECK(Bc250ScInit(&w->Facade, &leaseFixture, &w->Seed, &w->Expected, &w->Session) == BC250_VM_SESSION_FAULT);
            CHECK(w->Facade.HasSnapshot && !w->Facade.HasBacking && leaseFixture.References == 2);
            DropScSeed(w);
        } else {
            InitSc(w); DropScSeed(w);
            if (scenario == 1) {
                CHECK(Bc250ScMap(&w->Facade, 0, 3, &map, &journal, NULL, NULL) == BC250_VM_SESSION_OK);
                w->Facade.Snapshot.Pages[0].Domain = BC250_AD_FB_PHYSICAL; /* TEST corruption. */
                CHECK(Bc250ScUnmap(&w->Facade, &map, &journal, NULL, NULL) == BC250_VM_SESSION_CORRUPT);
                CHECK(w->Facade.HasBacking && !w->Facade.BackingReleased && !w->Facade.SnapshotReleased);
            } else {
                faultAtMetadata = 1;
                CHECK(Bc250ScRetire(&w->Facade) == BC250_VM_SESSION_FAULT);
                CHECK(w->Facade.BackingReleased && w->Facade.SnapshotReleased && !leaseFixture.References);
            }
        }
        CHECK(w->Facade.State == BC250_SC_FAULT && scDmaLive && !scDmaFrees && !adapterPuts);
        CHECK(Bc250ScRetire(&w->Facade) == BC250_VM_SESSION_FAULT);
        /* Destroy isolated fake universe, NOT cleanup/recovery of live transport.
         * No further calls using any discarded obligations or poisoned nodes. */
        for (unsigned int n = 0; n < w->Heap.Blocks; ++n) free(w->Heap.Block[n].Node);
    }
    free(w);
}
static void TestScPolicyAndCopies(void)
{
    SC_WORLD *w = calloc(1, sizeof(*w)), *copy = calloc(1, sizeof(*copy));
    unsigned int oldCalls, oldGateCalls, flag;
    CHECK(w && copy); PrepareSc(w, 1); InitSc(w); DropScSeed(w);
    copy->Facade = w->Facade;
    CHECK(Bc250ScRetire(&copy->Facade) == BC250_VM_SESSION_INVALID);
    CHECK(Bc250ScCheck(&w->Facade) == BC250_VM_SESSION_OK);
    for (flag = 0; flag < 3; ++flag) {
        oldCalls = calls; oldGateCalls = gateCalls;
        if (!flag) Bc250MockExecutionAllowed = FALSE;
        if (flag == 1) Bc250MockGateExecutionAllowed = FALSE;
        if (flag == 2) Bc250MockCaptureExecutionAllowed = FALSE;
        CHECK(Bc250ScRetire(&w->Facade) == BC250_VM_SESSION_INVALID);
        CHECK(calls == oldCalls && gateCalls == oldGateCalls);
        Bc250MockExecutionAllowed = Bc250MockGateExecutionAllowed = Bc250MockCaptureExecutionAllowed = TRUE;
    }
    EndSc(w); free(copy); free(w);
}
int main(void)
{
    TestScMaps(); TestScRetries(); TestScFailures(); TestScPolicyAndCopies();
    printf("PASS snapshot CPU association: %u checks; aggregate reference, PUBLIC API, RAM ONLY\n", assertions);
    return 0;
}
