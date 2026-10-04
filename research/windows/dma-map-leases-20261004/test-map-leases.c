/* SPDX-License-Identifier: Apache-2.0
 * Original deterministic association tests; raw inspections/lock injection
 * below are TEST-ONLY, not permissions for facade clients or recovery.
 */
#define frees mlDmaFrees
#define live mlDmaLive
#define main Bc250FrozenLeaseTests
#include "../dma-leases-20261004/test-leases.c"
#undef main
#undef frees
#undef live
#undef owner
#undef CHECK
#define main Bc250FrozenTreeTests
#include "../vm-cpu-backend-20261003/test-vm-cpu-backend.c"
#undef main
#include "bc250_dma_map_leases.h"

typedef struct MAP_WORLD {
    TEST_HEAP Heap;
    BC250_VM_CPU_SESSION Session;
    BC250_MAP_LEASES Facade;
    BC250_DMA_LEASE_HANDLE Seed;
    BC250_BACKING_HANDLE Backing;
} MAP_WORLD;
static MAP_WORLD *mapWorld;
static unsigned int mapStopAtHook, mapHoldMetadata, mapCorruptLimit, mapHookCalls;
static BC250_VM_CPU_JOURNAL mapNestedJournal;

static void StartMapWorld(MAP_WORLD *w, unsigned int keepSeed)
{
    BC250_VM_LAYOUT layout;
    BC250_ADDRESS_SPAN pages[2];
    memset(w, 0, sizeof(*w)); ResetLeases(); OpenLeases(); w->Seed = AcquireLease();
    mapWorld = w; mapStopAtHook = mapHoldMetadata = mapCorruptLimit = mapHookCalls = 0;
    CHECK(Bc250VmPlanLayout(BC250_VM_PDB2, 9, 1ULL << 30, &layout));
    w->Heap.Context = &w->Session.Backend;
    CHECK(Bc250VmSessionInit(&w->Session, &layout, 0x0000FFFFFFFFFFFFULL, 128,
        Allocate, Release, &w->Heap) == BC250_VM_SESSION_OK);
    /* Deliberately NOT the fake SG address: independent numeric inputs. */
    Page(&pages[0], 0xB00000); Page(&pages[1], 0x50000);
    CHECK(Bc250VmSessionRegister(&w->Session, pages, 2, 2, &w->Backing) == BC250_VM_SESSION_OK);
    CHECK(Bc250MlInit(&w->Facade, &leaseFixture, &w->Seed, &w->Session, &w->Backing) == BC250_VM_SESSION_OK);
    CHECK(leaseFixture.References == 2);
    if (!keepSeed) CHECK(Bc250DmaLeasesDrop(&leaseFixture, &w->Seed) == STATUS_SUCCESS);
    w->Heap.Attempts = 0;
}
static void EndMapWorld(MAP_WORLD *w)
{
    CHECK(Bc250MlRetire(&w->Facade) == BC250_VM_SESSION_OK);
    CHECK(w->Facade.State == BC250_ML_DEAD && w->Facade.BackingReleased && w->Facade.RootDropped);
    CHECK(Bc250MlRetire(&w->Facade) == BC250_VM_SESSION_DEAD_RESULT);
    CHECK(Bc250VmSessionShutdown(&w->Session) == BC250_VM_SESSION_OK);
    CHECK(mlDmaFrees == 1 && adapterPuts == 1); CheckRetired(); FinishHeap(&w->Heap);
    mapWorld = NULL;
}
static int MapHook(void *context, unsigned int count)
{
    MAP_WORLD *w = context;
    BC250_MAP_LEASE_HANDLE out = {0}, input = {0};
    ++mapHookCalls;
    CHECK(Bc250MlMap(&w->Facade, 0, 3, &out, &mapNestedJournal, NULL, NULL) == BC250_VM_SESSION_BUSY_RESULT);
    CHECK(Bc250MlUnmap(&w->Facade, &input, &mapNestedJournal, NULL, NULL) == BC250_VM_SESSION_BUSY_RESULT);
    CHECK(Bc250MlRetire(&w->Facade) == BC250_VM_SESSION_BUSY_RESULT);
    CHECK(Bc250MlDrain(&w->Facade) == BC250_VM_SESSION_BUSY_RESULT);
    CHECK(mlDmaLive && !mlDmaFrees && !adapterPuts && leaseFixture.References >= 2);
    if (mapStopAtHook) CHECK(Bc250MlStop(&w->Facade) == BC250_VM_SESSION_OK);
    if (mapHoldMetadata && !leaseFixture.Metadata.Held) {
        threadIndex = 1; CHECK(Bc250DmaLeasesLock(&leaseFixture) == STATUS_SUCCESS); threadIndex = 0;
    }
    if (mapCorruptLimit) w->Session.Backend.MaximumDmaLast = 0; /* Negative fixture only. */
    return AfterWrite(&w->Heap, count);
}
static void UnlockMapMetadata(void)
{
    threadIndex = 1; Bc250DmaLeasesUnlock(&leaseFixture); threadIndex = 0; mapHoldMetadata = 0;
}
static void TestMapDrainAndRollback(void)
{
    MAP_WORLD *w = calloc(1, sizeof(*w));
    BC250_MAP_LEASE_HANDLE first = {0}, second = {0}, failed = {0}, stale;
    BC250_VM_CPU_JOURNAL journal;
    BC250_GART_U64 digest;
    unsigned int failure;
    CHECK(w != NULL); StartMapWorld(w, 0);
    digest = Digest(&w->Session.Backend); w->Heap.FailAllocation = 1;
    CHECK(Bc250MlMap(&w->Facade, 0x1FF000, 15, &failed, &journal, NULL, NULL) == BC250_VM_SESSION_NO_MEMORY);
    CHECK(!failed.Facade && leaseFixture.References == 1 && mlDmaLive && !mlDmaFrees);
    CHECK(Digest(&w->Session.Backend) == digest); w->Heap.FailAllocation = 0;
    for (failure = 1; failure <= 2; ++failure) {
        w->Heap.HookCalls = 0; w->Heap.FailWrite = failure;
        CHECK(Bc250MlMap(&w->Facade, 0x1FF000, 15, &failed, &journal, MapHook, w) == BC250_VM_SESSION_FAULT);
        CHECK(w->Facade.State == BC250_ML_ACTIVE && !failed.Facade && leaseFixture.References == 1);
        CHECK(Digest(&w->Session.Backend) == digest && Bc250MlCheck(&w->Facade) == BC250_VM_SESSION_OK);
    }
    w->Heap.FailWrite = w->Heap.HookCalls = 0;
    CHECK(Bc250MlMap(&w->Facade, 0x1FF000, 15, &first, &journal, MapHook, w) == BC250_VM_SESSION_OK);
    CHECK(leaseFixture.References == 2);
    w->Heap.HookCalls = 0;
    CHECK(Bc250MlMap(&w->Facade, 0x800000, 15, &second, &journal, MapHook, w) == BC250_VM_SESSION_OK);
    CHECK(leaseFixture.References == 3 && w->Session.Backing.Records[w->Backing.Slot].References == 2);
    CheckValue(&w->Session.Backend, 0x1FF000, Expected(&w->Session.Backing.Records[w->Backing.Slot].Pages[0], 15));
    CHECK(Bc250MlRetire(&w->Facade) == BC250_VM_SESSION_IN_USE);
    CHECK(Bc250DmaLeasesRetire(&leaseFixture) == STATUS_DEVICE_BUSY && mlDmaLive && !mlDmaFrees);
    CHECK(Bc250MlMap(&w->Facade, 0xA00000, 3, &failed, &journal, NULL, NULL) == BC250_VM_SESSION_IN_USE);
    digest = Digest(&w->Session.Backend);
    for (failure = 1; failure <= 2; ++failure) {
        w->Heap.HookCalls = 0; w->Heap.FailWrite = failure;
        CHECK(Bc250MlUnmap(&w->Facade, &first, &journal, MapHook, w) == BC250_VM_SESSION_FAULT);
        CHECK(Digest(&w->Session.Backend) == digest && leaseFixture.References == 3 && !mlDmaFrees);
        CHECK(Bc250MlCheck(&w->Facade) == BC250_VM_SESSION_OK);
    }
    w->Heap.FailWrite = w->Heap.HookCalls = 0; stale = first;
    CHECK(Bc250MlUnmap(&w->Facade, &first, &journal, MapHook, w) == BC250_VM_SESSION_OK);
    CHECK(Bc250MlUnmap(&w->Facade, &stale, &journal, NULL, NULL) == BC250_VM_SESSION_STALE);
    CHECK(leaseFixture.References == 2 && mlDmaLive && !mlDmaFrees);
    w->Heap.HookCalls = 0;
    CHECK(Bc250MlUnmap(&w->Facade, &second, &journal, MapHook, w) == BC250_VM_SESSION_OK);
    CHECK(leaseFixture.References == 1 && mlDmaLive && !mlDmaFrees);
    EndMapWorld(w); free(w);
}
static void TestPendingDropAndRetireRetries(void)
{
    MAP_WORLD *w = calloc(1, sizeof(*w));
    BC250_MAP_LEASE_HANDLE mapped = {0}, failed = {0};
    BC250_VM_CPU_JOURNAL journal;
    unsigned int scenario;
    CHECK(w != NULL);
    for (scenario = 0; scenario < 2; ++scenario) {
        StartMapWorld(w, 0); w->Heap.HookCalls = 0;
        if (!scenario) CHECK(Bc250MlMap(&w->Facade, 0x400000, 3, &mapped, &journal, NULL, NULL) == BC250_VM_SESSION_OK);
        mapHoldMetadata = 1;
        if (scenario) {
            w->Heap.FailWrite = 1;
            CHECK(Bc250MlMap(&w->Facade, 0x400000, 3, &failed, &journal, MapHook, w) == BC250_VM_SESSION_BUSY_RESULT);
            CHECK(!failed.Facade);
        } else CHECK(Bc250MlUnmap(&w->Facade, &mapped, &journal, MapHook, w) == BC250_VM_SESSION_BUSY_RESULT);
        CHECK(w->Facade.State == BC250_ML_STOPPED && leaseFixture.References == 2 && mlDmaLive && !mlDmaFrees);
        CHECK(Bc250VmSessionNonzero(w->Session.Backend.Root) == 0);
        CHECK(w->Facade.Records[0].Phase == BC250_ML_PENDING_DROP && !w->Facade.Records[0].Cpu.Session);
        CHECK(Bc250MlRetire(&w->Facade) == BC250_VM_SESSION_BUSY_RESULT && !mlDmaFrees);
        UnlockMapMetadata(); w->Heap.FailWrite = w->Heap.HookCalls = 0;
        if (!scenario) {
            unsigned int oldHooks = mapHookCalls;
            CHECK(Bc250MlUnmap(&w->Facade, &mapped, &journal, MapHook, w) == BC250_VM_SESSION_OK);
            CHECK(oldHooks == mapHookCalls); /* Drop retry never repeats CPU Unmap. */
        } else CHECK(Bc250MlDrain(&w->Facade) == BC250_VM_SESSION_OK);
        CHECK(leaseFixture.References == 1 && mlDmaLive && !mlDmaFrees);
        EndMapWorld(w); memset(&mapped, 0, sizeof(mapped));
    }
    StartMapWorld(w, 1); /* External seed deliberately remains live. */
    CHECK(Bc250MlRetire(&w->Facade) == BC250_VM_SESSION_BUSY_RESULT);
    CHECK(w->Facade.BackingReleased && w->Facade.RootDropped && leaseFixture.References == 1 && !mlDmaFrees);
    CHECK(Bc250DmaLeasesDrop(&leaseFixture, &w->Seed) == STATUS_SUCCESS);
    EndMapWorld(w);
    StartMapWorld(w, 0); /* Backing release succeeds, root Drop blocked. */
    threadIndex = 1; CHECK(Bc250DmaLeasesLock(&leaseFixture) == STATUS_SUCCESS); threadIndex = 0;
    CHECK(Bc250MlRetire(&w->Facade) == BC250_VM_SESSION_BUSY_RESULT);
    CHECK(w->Facade.BackingReleased && !w->Facade.RootDropped && leaseFixture.References == 1 && !mlDmaFrees);
    UnlockMapMetadata(); EndMapWorld(w); free(w);
}
static void TestStopAndCapacity(void)
{
    MAP_WORLD *w = calloc(1, sizeof(*w));
    BC250_MAP_LEASE_HANDLE handles[16] = {{0}}, old, rejected = {0};
    BC250_VM_CPU_JOURNAL journal;
    unsigned int i;
    CHECK(w != NULL); StartMapWorld(w, 0);
    for (i = 0; i < 15; ++i)
        CHECK(Bc250MlMap(&w->Facade, 0x400000 + (BC250_GART_U64)i * 0x4000, 3,
            &handles[i], &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    CHECK(leaseFixture.References == 16);
    CHECK(Bc250MlMap(&w->Facade, 0x800000, 3, &handles[15], &journal, NULL, NULL) == BC250_VM_SESSION_FULL);
    CHECK(!handles[15].Facade && leaseFixture.References == 16);
    old = handles[1]; CHECK(Bc250MlUnmap(&w->Facade, &handles[1], &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    memset(&handles[1], 0, sizeof(handles[1]));
    CHECK(Bc250MlMap(&w->Facade, 0x800000, 3, &handles[1], &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    CHECK(handles[1].Slot == old.Slot && handles[1].Id > old.Id);
    CHECK(Bc250MlUnmap(&w->Facade, &old, &journal, NULL, NULL) == BC250_VM_SESSION_STALE);
    CHECK(Bc250MlMap(&w->Facade, 0x900000, 3, &handles[0], &journal, NULL, NULL) == BC250_VM_SESSION_INVALID);
    for (i = 0; i < 15; ++i) CHECK(Bc250MlUnmap(&w->Facade, &handles[i], &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    EndMapWorld(w);
    StartMapWorld(w, 0); mapStopAtHook = 1; w->Heap.HookCalls = 0; memset(&handles[0], 0, sizeof(handles[0]));
    CHECK(Bc250MlMap(&w->Facade, 0x400000, 3, &handles[0], &journal, MapHook, w) == BC250_VM_SESSION_OK);
    CHECK(w->Facade.State == BC250_ML_STOPPED && leaseFixture.References == 2 && !mlDmaFrees);
    CHECK(Bc250MlMap(&w->Facade, 0x800000, 3, &rejected, &journal, NULL, NULL) == BC250_VM_SESSION_IN_USE);
    CHECK(Bc250MlUnmap(&w->Facade, &handles[0], &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    EndMapWorld(w); free(w);
}
static void TestAliasesAndFaultRetention(void)
{
    MAP_WORLD *w = calloc(1, sizeof(*w));
    BC250_MAP_LEASE_HANDLE mapped = {0}, out = {0};
    BC250_VM_CPU_JOURNAL journal;
    CHECK(w != NULL); StartMapWorld(w, 0);
    CHECK(Bc250MlMap(&w->Facade, 0, 3, (BC250_MAP_LEASE_HANDLE *)&w->Facade.Root, &journal, NULL, NULL) == BC250_VM_SESSION_INVALID);
    CHECK(Bc250MlMap(&w->Facade, 0, 3, (BC250_MAP_LEASE_HANDLE *)&leaseFixture.Metadata, &journal, NULL, NULL) == BC250_VM_SESSION_INVALID);
    CHECK(Bc250MlMap(&w->Facade, 0, 3, (BC250_MAP_LEASE_HANDLE *)&w->Session, &journal, NULL, NULL) == BC250_VM_SESSION_INVALID);
    CHECK(Bc250MlMap(&w->Facade, 0, 3, (BC250_MAP_LEASE_HANDLE *)w->Session.Backend.Root, &journal, NULL, NULL) == BC250_VM_SESSION_INVALID);
    CHECK(Bc250MlMap(&w->Facade, 0, 3, (BC250_MAP_LEASE_HANDLE *)&journal, &journal, NULL, NULL) == BC250_VM_SESSION_INVALID);
    CHECK(leaseFixture.References == 1 && !mlDmaFrees);
    w->Facade.LastId = BC250_GART_U64_MAX; /* Isolated fixture instrumentation. */
    CHECK(Bc250MlMap(&w->Facade, 0, 3, &out, &journal, NULL, NULL) == BC250_VM_SESSION_EXHAUSTED);
    EndMapWorld(w);
    StartMapWorld(w, 0);
    CHECK(Bc250MlMap(&w->Facade, 0x400000, 3, &mapped, &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    Bc250MockLeasesExecutionAllowed = FALSE;
    CHECK(Bc250MlUnmap(&w->Facade, &mapped, &journal, NULL, NULL) == BC250_VM_SESSION_INVALID);
    CHECK(leaseFixture.References == 2 && Bc250VmSessionNonzero(w->Session.Backend.Root) == 2 && !mlDmaFrees);
    Bc250MockLeasesExecutionAllowed = TRUE;
    CHECK(Bc250MlUnmap(&w->Facade, &mapped, &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    EndMapWorld(w);
    StartMapWorld(w, 0); mapCorruptLimit = 1; w->Heap.FailWrite = 1;
    CHECK(Bc250MlMap(&w->Facade, 0x400000, 3, &out, &journal, MapHook, w) == BC250_VM_SESSION_FAULT);
    CHECK(w->Facade.State == BC250_ML_FAULT && leaseFixture.References == 2 && mlDmaLive && !mlDmaFrees);
    CHECK(Bc250MlRetire(&w->Facade) == BC250_VM_SESSION_FAULT);
    /* Destroy isolated RAM world without invoking transport cleanup; NOT recovery. */
    w->Session.Backend.MaximumDmaLast = 0x0000FFFFFFFFFFFFULL;
    CHECK(Bc250VmSessionRelease(&w->Session, &w->Backing) == BC250_VM_SESSION_OK);
    CHECK(Bc250VmSessionShutdown(&w->Session) == BC250_VM_SESSION_OK);
    FinishHeap(&w->Heap); mapWorld = NULL; free(w);
}
int main(void)
{
    unsigned int baseline = assertions;
    TestMapDrainAndRollback(); TestPendingDropAndRetireRetries();
    TestStopAndCapacity(); TestAliasesAndFaultRetention();
    printf("PASS: %u association assertions; real source with RAM fake leases + synthetic CPU maps; NO Windows/GPU mapping or DMA ownership proof\n", assertions - baseline);
    return 0;
}
