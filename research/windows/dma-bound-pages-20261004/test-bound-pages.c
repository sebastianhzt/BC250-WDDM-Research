/* SPDX-License-Identifier: Apache-2.0
 * NEW isolated fake worlds only. Direct field edits and lock injection below
 * are negative TEST instrumentation, never recovery or public consumer APIs.
 */
#define frees bpDmaFrees
#define live bpDmaLive
#define main BpFrozenLeaseTests
#include "../dma-leases-20261004/test-leases.c"
#undef main
#undef frees
#undef live
#undef owner
#undef CHECK
#define main BpFrozenTreeTests
#include "../vm-cpu-backend-20261003/test-vm-cpu-backend.c"
#undef main
#include "fixture-bound-pages.h"
typedef struct BOUND_WORLD {
    TEST_HEAP Heap;
    BC250_VM_CPU_SESSION Session;
    BC250_MAP_LEASES Facade;
    BC250_BOUND_PAGES Source;
    BC250_DMA_LEASE_HANDLE Seed;
} BOUND_WORLD;

static void BeginBound(BOUND_WORLD *w, unsigned int pages)
{
    BC250_VM_LAYOUT layout;
    memset(w, 0, sizeof(*w)); ResetLeases(); OpenLeases();
    list.NumberOfElements = pages == 4 ? 3 : 1;
    list.Elements[0].Address.QuadPart = 0xB00000;
    list.Elements[0].Length = pages == 4 ? 8192 : pages * 4096;
    if (pages == 4) {
        list.Elements[1].Address.QuadPart = 0x50000; list.Elements[1].Length = 4096;
        list.Elements[2].Address.QuadPart = 0xB00000; list.Elements[2].Length = 4096;
    }
    CHECK(Bc250DmaLeasesAcquire(&leaseFixture, &mdl, 0, pages * 4096, TRUE, &w->Seed) == STATUS_SUCCESS);
    CHECK(Bc250BpCapture(&w->Source, &leaseFixture, &w->Seed) == BC250_VM_SESSION_OK);
    CHECK(w->Source.Sample.Count == pages && w->Source.Sample.Bytes == (ULONGLONG)pages * 4096);
    CHECK(w->Source.State == BC250_BP_CAPTURED && leaseFixture.References == 2);
    CHECK(Bc250DmaLeasesDrop(&leaseFixture, &w->Seed) == STATUS_SUCCESS);
    CHECK(Bc250VmPlanLayout(BC250_VM_PDB2, 9, 1ULL << 30, &layout));
    w->Heap.Context = &w->Session.Backend;
    CHECK(Bc250VmSessionInit(&w->Session, &layout, 0x0000FFFFFFFFFFFFULL, 128,
        Allocate, Release, &w->Heap) == BC250_VM_SESSION_OK);
    w->Heap.Attempts = 0;
}
static void AttachBound(BOUND_WORLD *w)
{
    CHECK(Bc250BpRegister(&w->Source, &w->Session) == BC250_VM_SESSION_OK);
    CHECK(Bc250BpBackingMatches(&w->Source));
    CHECK(Bc250BpAttach(&w->Source, &w->Facade) == BC250_VM_SESSION_OK);
    CHECK(w->Source.State == BC250_BP_ATTACHED && leaseFixture.References == 2);
}
static void FinishBound(BOUND_WORLD *w)
{
    CHECK(Bc250BpRelease(&w->Source) == BC250_VM_SESSION_IN_USE);
    CHECK(Bc250MlRetire(&w->Facade) == BC250_VM_SESSION_BUSY_RESULT);
    CHECK(w->Facade.BackingReleased && w->Facade.RootDropped && leaseFixture.References == 1 && !bpDmaFrees);
    CHECK(Bc250BpRelease(&w->Source) == BC250_VM_SESSION_OK);
    CHECK(!leaseFixture.References && bpDmaLive && !bpDmaFrees);
    CHECK(Bc250BpRelease(&w->Source) == BC250_VM_SESSION_DEAD_RESULT);
    CHECK(Bc250MlRetire(&w->Facade) == BC250_VM_SESSION_OK);
    CHECK(bpDmaFrees == 1 && adapterPuts == 1); CheckRetired();
    CHECK(Bc250VmSessionShutdown(&w->Session) == BC250_VM_SESSION_OK); FinishHeap(&w->Heap);
}
static int BoundHook(void *context, unsigned int count)
{
    BOUND_WORLD *w = context;
    CHECK(Bc250BpRelease(&w->Source) == BC250_VM_SESSION_BUSY_RESULT);
    CHECK(bpDmaLive && !bpDmaFrees && leaseFixture.References >= 3);
    return AfterWrite(&w->Heap, count);
}
static void TestActualCapturedValues(void)
{
    BOUND_WORLD *w = calloc(1, sizeof(*w));
    BC250_MAP_LEASE_HANDLE first = {0}, alias = {0}, failed = {0};
    BC250_VM_CPU_JOURNAL journal;
    BC250_GART_U64 digest;
    unsigned int i, failure;
    const BC250_GART_U64 expected[4] = {0xB00000, 0xB01000, 0x50000, 0xB00000};
    CHECK(w != NULL); BeginBound(w, 4);
    for (i = 0; i < 4; ++i) {
        CHECK(w->Source.Sample.Pages[i].Domain == BC250_AD_DMA_LOGICAL);
        CHECK(w->Source.Sample.Pages[i].Start == expected[i] && w->Source.Sample.Pages[i].Bytes == 4096);
    }
    AttachBound(w);
    CHECK(Bc250BpRegister(&w->Source, &w->Session) == BC250_VM_SESSION_INVALID);
    digest = Digest(&w->Session.Backend);
    CHECK(Bc250BpMap(&w->Source, 0x1FF000, 15, (BC250_MAP_LEASE_HANDLE *)&w->Source,
        &journal, NULL, NULL) == BC250_VM_SESSION_INVALID);
    CHECK(Bc250BpMap(&w->Source, 0x1FF000, 15, &failed,
        (BC250_VM_CPU_JOURNAL *)&w->Source, NULL, NULL) == BC250_VM_SESSION_INVALID);
    CHECK(leaseFixture.References == 2 && Digest(&w->Session.Backend) == digest && !failed.Facade);
    digest = Digest(&w->Session.Backend); w->Heap.FailAllocation = 1;
    CHECK(Bc250BpMap(&w->Source, 0x1FF000, 15, &failed, &journal, NULL, NULL) == BC250_VM_SESSION_NO_MEMORY);
    CHECK(!failed.Facade && leaseFixture.References == 2 && Digest(&w->Session.Backend) == digest);
    w->Heap.FailAllocation = 0;
    for (failure = 1; failure <= 4; ++failure) {
        w->Heap.FailWrite = failure; w->Heap.HookCalls = 0;
        CHECK(Bc250BpMap(&w->Source, 0x1FF000, 15, &failed, &journal, BoundHook, w) == BC250_VM_SESSION_FAULT);
        CHECK(leaseFixture.References == 2 && !failed.Facade && Digest(&w->Session.Backend) == digest);
    }
    w->Heap.FailWrite = w->Heap.HookCalls = 0;
    CHECK(Bc250BpMap(&w->Source, 0x1FF000, 15, &first, &journal, BoundHook, w) == BC250_VM_SESSION_OK);
    CHECK(Bc250BpMap(&w->Source, 0x800000, 15, &alias, &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    CHECK(leaseFixture.References == 4 && bpDmaLive && !bpDmaFrees);
    for (i = 0; i < 4; ++i) {
        CheckValue(&w->Session.Backend, 0x1FF000 + (ULONGLONG)i * 4096, expected[i] | 0x77);
        CheckValue(&w->Session.Backend, 0x800000 + (ULONGLONG)i * 4096, expected[i] | 0x77);
    }
    CHECK(Bc250BpRelease(&w->Source) == BC250_VM_SESSION_IN_USE);
    CHECK(Bc250MlRetire(&w->Facade) == BC250_VM_SESSION_IN_USE);
    digest = Digest(&w->Session.Backend); w->Heap.FailWrite = 2; w->Heap.HookCalls = 0;
    CHECK(Bc250MlUnmap(&w->Facade, &first, &journal, AfterWrite, &w->Heap) == BC250_VM_SESSION_FAULT);
    CHECK(leaseFixture.References == 4 && Digest(&w->Session.Backend) == digest);
    CHECK(Bc250BpRelease(&w->Source) == BC250_VM_SESSION_IN_USE);
    w->Heap.FailWrite = w->Heap.HookCalls = 0;
    CHECK(Bc250MlUnmap(&w->Facade, &first, &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    CHECK(Bc250MlUnmap(&w->Facade, &alias, &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    FinishBound(w); free(w);
}
static void TestReleaseRetriesAndBounds(void)
{
    BOUND_WORLD *w = calloc(1, sizeof(*w));
    BC250_MAP_LEASE_HANDLE map = {0};
    BC250_VM_CPU_JOURNAL journal;
    unsigned int i;
    CHECK(w != NULL); BeginBound(w, 64); AttachBound(w);
    CHECK(Bc250BpMap(&w->Source, 0x1FF000, 3, &map, &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    for (i = 0; i < 64; ++i) CheckValue(&w->Session.Backend, 0x1FF000 + (ULONGLONG)i * 4096,
        (0xB00000 + (ULONGLONG)i * 4096) | 0x63);
    CHECK(Bc250MlUnmap(&w->Facade, &map, &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    CHECK(Bc250MlRetire(&w->Facade) == BC250_VM_SESSION_BUSY_RESULT);
    threadIndex = 1; CHECK(Bc250DmaLeasesLock(&leaseFixture) == STATUS_SUCCESS); threadIndex = 0;
    CHECK(Bc250BpRelease(&w->Source) == BC250_VM_SESSION_BUSY_RESULT);
    CHECK(w->Source.State == BC250_BP_ATTACHED && w->Source.Hold.Id && leaseFixture.References == 1 && !bpDmaFrees);
    threadIndex = 1; Bc250DmaLeasesUnlock(&leaseFixture); threadIndex = 0;
    CHECK(Bc250BpRelease(&w->Source) == BC250_VM_SESSION_OK);
    CHECK(bpDmaLive && !bpDmaFrees);
    CHECK(Bc250MlRetire(&w->Facade) == BC250_VM_SESSION_OK); CheckRetired();
    CHECK(Bc250VmSessionShutdown(&w->Session) == BC250_VM_SESSION_OK); FinishHeap(&w->Heap);
    BeginBound(w, 1);
    w->Session.Backend.MaximumDmaLast = 0xFFFFF; /* Consistent empty Session, narrower than captured DMA. */
    CHECK(Bc250BpRegister(&w->Source, &w->Session) == BC250_VM_SESSION_INVALID);
    CHECK(w->Source.State == BC250_BP_CAPTURED && !w->Source.Session && leaseFixture.References == 1);
    CHECK(Bc250BpRelease(&w->Source) == BC250_VM_SESSION_OK);
    CHECK(Bc250DmaLeasesRetire(&leaseFixture) == STATUS_SUCCESS); CheckRetired();
    CHECK(Bc250VmSessionShutdown(&w->Session) == BC250_VM_SESSION_OK); FinishHeap(&w->Heap);
    BeginBound(w, 1); CHECK(Bc250BpRegister(&w->Source, &w->Session) == BC250_VM_SESSION_OK);
    threadIndex = 1; CHECK(Bc250DmaLeasesLock(&leaseFixture) == STATUS_SUCCESS); threadIndex = 0;
    CHECK(Bc250BpRelease(&w->Source) == BC250_VM_SESSION_BUSY_RESULT);
    CHECK(w->Source.BackingReleased && leaseFixture.References == 1 && !bpDmaFrees);
    threadIndex = 1; Bc250DmaLeasesUnlock(&leaseFixture); threadIndex = 0;
    CHECK(Bc250BpRelease(&w->Source) == BC250_VM_SESSION_OK); /* No second backing release. */
    CHECK(Bc250DmaLeasesRetire(&leaseFixture) == STATUS_SUCCESS); CheckRetired();
    CHECK(Bc250VmSessionShutdown(&w->Session) == BC250_VM_SESSION_OK); FinishHeap(&w->Heap); free(w);
}
static void TestCapturedRejections(void)
{
    BOUND_WORLD *w = calloc(1, sizeof(*w));
    unsigned int scenario;
    CHECK(w != NULL);
    for (scenario = 0; scenario < 9; ++scenario) {
        BC250_BP_SAMPLE preserved;
        BeginBound(w, 4); preserved = w->Source.Sample;
        switch (scenario) {
        case 0: list.Elements[0].Address.QuadPart += 4096; break; /* Copied bytes independent; stale provenance. */
        case 1: list.NumberOfElements = 65; break;
        case 2: list.Elements[0].Length = 4095; break;
        case 3: list.Elements[0].Address.QuadPart = 0x0001000000000000ULL; break;
        case 4: leaseFixture.PinnedListBytes -= sizeof(list.Elements[0]); break;
        case 5: w->Source.Sample.Pages[1].Domain = BC250_AD_FB_PHYSICAL; break;
        case 6: ++w->Source.Hold.Id; break;
        case 7: CHECK(Bc250DmaLeasesStop(&leaseFixture) == STATUS_SUCCESS); break;
        default: leaseFixture.Native.AddressWidth = 0; break;
        }
        if (scenario != 5) CHECK(memcmp(&preserved, &w->Source.Sample, sizeof(preserved)) == 0);
        CHECK(Bc250BpRegister(&w->Source, &w->Session) == BC250_VM_SESSION_FAULT);
        CHECK(w->Source.State == BC250_BP_FAULT && leaseFixture.References == 1 && bpDmaLive && !bpDmaFrees);
        CHECK(Bc250BpRelease(&w->Source) == BC250_VM_SESSION_FAULT);
        CHECK(!w->Source.Backing.Pool);
        CHECK(Bc250VmSessionShutdown(&w->Session) == BC250_VM_SESSION_OK); FinishHeap(&w->Heap);
        /* Discard isolated RAM world WITHOUT transport cleanup, NOT recovery. */
    }
    BeginBound(w, 1);
    {
        BC250_BOUND_PAGES copied = w->Source;
        unsigned int oldCalls, oldGateCalls;
        CHECK(Bc250BpRelease(&copied) == BC250_VM_SESSION_INVALID);
        oldCalls = calls; oldGateCalls = gateCalls;
        Bc250MockLeasesExecutionAllowed = FALSE;
        CHECK(Bc250BpRegister(&w->Source, &w->Session) == BC250_VM_SESSION_INVALID);
        CHECK(calls == oldCalls && gateCalls == oldGateCalls);
        Bc250MockLeasesExecutionAllowed = TRUE;
    }
    CHECK(Bc250BpRelease(&w->Source) == BC250_VM_SESSION_OK);
    CHECK(Bc250DmaLeasesRetire(&leaseFixture) == STATUS_SUCCESS); CheckRetired();
    CHECK(Bc250VmSessionShutdown(&w->Session) == BC250_VM_SESSION_OK); FinishHeap(&w->Heap);
    memset(w, 0, sizeof(*w)); ResetLeases(); OpenLeases(); w->Seed = AcquireLease();
    stopAtAcquire = 1;
    CHECK(Bc250BpCapture(&w->Source, &leaseFixture, &w->Seed) == BC250_VM_SESSION_IN_USE);
    CHECK(w->Source.State == BC250_BP_WAIT && !w->Source.Hold.Owner && leaseFixture.References == 1);
    stopAtAcquire = 0;
    CHECK(Bc250BpRelease(&w->Source) == BC250_VM_SESSION_OK);
    CHECK(Bc250DmaLeasesDrop(&leaseFixture, &w->Seed) == STATUS_SUCCESS);
    CHECK(Bc250DmaLeasesRetire(&leaseFixture) == STATUS_SUCCESS); CheckRetired(); free(w);
}
int main(void)
{
    TestActualCapturedValues(); TestReleaseRetriesAndBounds(); TestCapturedRejections();
    printf("PASS: %u bound-page assertions; acquired FAKE SG -> typed pages -> CPU maps; NO real DMA/GPU/W2P proof\n", assertions);
    return 0;
}
