/* SPDX-License-Identifier: Apache-2.0
 * Actual CPU facade against the predecessor RAM allocator/oracles.
 * No Windows/device/GPU APIs; facade leases are numeric metadata only.
 * Direct predecessor reads and raw negative fixtures below are test-only
 * inspection outside callbacks, NOT permission for facade clients to use them.
 */
#define main Bc250PreviousCpuBackendTests
#include "../vm-cpu-backend-20261003/test-vm-cpu-backend.c"
#undef main
#include "vm-cpu-session-20261003/bc250_vm_cpu_session.h"

typedef struct SESSION_HEAP {
    TEST_HEAP Base;
    BC250_VM_CPU_SESSION *Session;
    unsigned int FacadeReentries;
} SESSION_HEAP;

static void SessionNested(SESSION_HEAP *heap)
{
    BC250_BACKING_HANDLE backing = {0}, out_backing;
    BC250_VM_SESSION_MAPPING_HANDLE mapping = {0}, out_mapping;
    BC250_VM_LAYOUT dummy_layout = {0};
    if (!heap->Base.Reentry) return;
    ++heap->FacadeReentries;
    CHECK(Bc250VmSessionInit(heap->Session, &dummy_layout,
        0, 1, NULL, NULL, NULL) == BC250_VM_SESSION_BUSY_RESULT);
    CHECK(Bc250VmSessionRegister(heap->Session, &heap->Base.NestedPage, 1, 1,
        &out_backing) == BC250_VM_SESSION_BUSY_RESULT);
    CHECK(Bc250VmSessionRelease(heap->Session, &backing) == BC250_VM_SESSION_BUSY_RESULT);
    CHECK(Bc250VmSessionMap(heap->Session, 0, &backing, 3, &out_mapping,
        &heap->Base.NestedJournal, NULL, NULL) == BC250_VM_SESSION_BUSY_RESULT);
    CHECK(Bc250VmSessionUnmap(heap->Session, &mapping, &heap->Base.NestedJournal,
        NULL, NULL) == BC250_VM_SESSION_BUSY_RESULT);
    CHECK(Bc250VmSessionShutdown(heap->Session) == BC250_VM_SESSION_BUSY_RESULT);
}

static BC250_VM_CPU_NODE *SessionAllocate(void *opaque, size_t bytes)
{
    SESSION_HEAP *heap = opaque;
    BC250_VM_CPU_NODE *node;
    unsigned int reentry = heap->Base.Reentry;
    SessionNested(heap); heap->Base.Reentry = 0;
    node = Allocate(&heap->Base, bytes); heap->Base.Reentry = reentry;
    return node;
}

static void SessionFree(void *opaque, BC250_VM_CPU_NODE *node)
{
    SESSION_HEAP *heap = opaque;
    unsigned int reentry = heap->Base.Reentry;
    SessionNested(heap); heap->Base.Reentry = 0;
    Release(&heap->Base, node); heap->Base.Reentry = reentry;
}

static int SessionAfterWrite(void *opaque, unsigned int count)
{
    SESSION_HEAP *heap = opaque;
    unsigned int reentry = heap->Base.Reentry;
    int result;
    SessionNested(heap); heap->Base.Reentry = 0;
    result = AfterWrite(&heap->Base, count); heap->Base.Reentry = reentry;
    return result;
}

static void StartSessionLayout(SESSION_HEAP *heap, BC250_VM_CPU_SESSION *session,
                               unsigned int root, BC250_GART_U64 max_pfn)
{
    BC250_VM_LAYOUT layout;
    memset(heap, 0, sizeof(*heap)); memset(session, 0, sizeof(*session));
    heap->Session = session; heap->Base.Context = &session->Backend;
    Page(&heap->Base.NestedPage, 0x51000);
    CHECK(Bc250VmPlanLayout(root, 9, max_pfn, &layout));
    heap->Base.Reentry = 1;
    CHECK(Bc250VmSessionInit(session, &layout, BC250_VM_MAX_ADDRESS, 256,
        SessionAllocate, SessionFree, heap) == BC250_VM_SESSION_OK);
    CHECK(heap->Base.Live == 1 && session->Backend.OwnedNodes == 1);
    CHECK(heap->FacadeReentries == 1);
    heap->Base.Attempts = 0; heap->Base.Reentry = 0;
}

static void StartSession(SESSION_HEAP *heap, BC250_VM_CPU_SESSION *session)
{
    StartSessionLayout(heap, session, 1, 1ULL << 36);
}

static void EndSession(SESSION_HEAP *heap, BC250_VM_CPU_SESSION *session)
{
    heap->Base.Reentry = 1;
    CHECK(Bc250VmSessionShutdown(session) == BC250_VM_SESSION_OK);
    CHECK(session->Backend.Root == NULL && heap->Base.Live == 0);
    FinishHeap(&heap->Base);
}

static void SessionPages(BC250_ADDRESS_SPAN *pages, unsigned int count)
{
    unsigned int i;
    for (i = 0; i < count; ++i)
        Page(&pages[i], ((BC250_GART_U64)((i * 17) % 29) + 1) * 4096);
}

static void SameExceptLeaseCounter(const BC250_VM_CPU_SESSION *session,
                                  BC250_VM_CPU_SESSION *before)
{
    CHECK(session->Backing.LastLeaseId == before->Backing.LastLeaseId + 1ULL);
    before->Backing.LastLeaseId = session->Backing.LastLeaseId;
    CHECK(memcmp(session, before, sizeof(*session)) == 0);
}

static void SessionAliasesAndTerminal(void)
{
    SESSION_HEAP heap, other_heap;
    BC250_VM_CPU_SESSION session, other, before;
    BC250_BACKING_HANDLE backing;
    BC250_VM_SESSION_MAPPING_HANDLE first, second, output, sentinel;
    BC250_VM_CPU_JOURNAL journal;
    BC250_ADDRESS_SPAN pages[2];
    BC250_GART_U64 first_va = (1ULL << 21) - 4096, second_va = 1ULL << 39;
    unsigned int frees, attempts;
    StartSession(&heap, &session); StartSession(&other_heap, &other);
    SessionPages(pages, 2); pages[1] = pages[0];
    CHECK(Bc250VmSessionRegister(&session, pages, 2, 2, &backing) == BC250_VM_SESSION_OK);
    CHECK(Bc250VmSessionMap(&session, first_va, &backing, 15, &first,
        &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    CHECK(Bc250VmSessionMap(&session, second_va, &backing, 15, &second,
        &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    CHECK(first.Session == &session && second.Session == &session && first.Id != second.Id);
    CHECK(session.Backing.Records[backing.Slot].References == 2);
    CHECK(Bc250VmSessionRelease(&session, &backing) == BC250_VM_SESSION_IN_USE);
    CHECK(Bc250VmSessionShutdown(&session) == BC250_VM_SESSION_IN_USE);
    memset(&output, 0xA5, sizeof(output)); memcpy(&sentinel, &output, sizeof(output));
    memcpy(&before, &session, sizeof(session));
    CHECK(Bc250VmSessionMap(&session, first_va + 4096, &backing, 15, &output,
        &journal, NULL, NULL) == BC250_VM_SESSION_IN_USE);
    CHECK(memcmp(&output, &sentinel, sizeof(output)) == 0);
    CHECK(memcmp(&session, &before, sizeof(session)) == 0);
    CHECK(Bc250VmSessionMap(&other, 0, &backing, 3, &output,
        &journal, NULL, NULL) == BC250_VM_SESSION_STALE);
    CHECK(Bc250VmSessionRelease(&other, &backing) == BC250_VM_SESSION_STALE);
    CHECK(Bc250VmSessionUnmap(&other, &first, &journal, NULL, NULL) == BC250_VM_SESSION_STALE);
    CHECK(Bc250VmSessionUnmap(&session, &first, &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    CHECK(session.Backing.Records[backing.Slot].References == 1);
    CHECK(Bc250VmSessionUnmap(&session, &first, &journal, NULL, NULL) == BC250_VM_SESSION_STALE);
    CheckValue(&session.Backend, second_va, Expected(&pages[0], 15));
    CheckValue(&session.Backend, second_va + 4096, Expected(&pages[1], 15));
    CHECK(Bc250VmSessionRelease(&session, &backing) == BC250_VM_SESSION_IN_USE);
    CHECK(Bc250VmSessionUnmap(&session, &second, &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    CHECK(session.Backing.Records[backing.Slot].References == 0);
    CHECK(session.Backend.OwnedNodes == 1 && heap.Base.Live == 1);
    CHECK(Bc250VmSessionShutdown(&session) == BC250_VM_SESSION_IN_USE);
    CHECK(Bc250VmSessionRelease(&session, &backing) == BC250_VM_SESSION_OK);
    CHECK(Bc250VmSessionRelease(&session, &backing) == BC250_VM_SESSION_STALE);
    EndSession(&heap, &session); EndSession(&other_heap, &other);
    frees = heap.Base.Frees; attempts = heap.Base.Attempts;
    CHECK(Bc250VmSessionInit(&session, &session.Backend.Layout, BC250_VM_MAX_ADDRESS,
        256, SessionAllocate, SessionFree, &heap) == BC250_VM_SESSION_DEAD_RESULT);
    CHECK(Bc250VmSessionRegister(&session, pages, 2, 2, &backing) == BC250_VM_SESSION_DEAD_RESULT);
    CHECK(Bc250VmSessionMap(&session, 0, &backing, 3, &output,
        &journal, NULL, NULL) == BC250_VM_SESSION_DEAD_RESULT);
    CHECK(Bc250VmSessionUnmap(&session, &second, &journal,
        NULL, NULL) == BC250_VM_SESSION_DEAD_RESULT);
    CHECK(Bc250VmSessionRelease(&session, &backing) == BC250_VM_SESSION_DEAD_RESULT);
    CHECK(heap.Base.Frees == frees && heap.Base.Attempts == attempts);
}

static void SessionFaults(void)
{
    SESSION_HEAP heap;
    BC250_VM_CPU_SESSION session, before;
    BC250_BACKING_HANDLE backing;
    BC250_VM_SESSION_MAPPING_HANDLE mapping, alias, output, sentinel;
    BC250_VM_CPU_JOURNAL journal;
    BC250_ADDRESS_SPAN pages[64];
    BC250_GART_U64 va = (1ULL << 39) - 4096;
    unsigned int failure, i;
    StartSession(&heap, &session); SessionPages(pages, 64);
    CHECK(Bc250VmSessionRegister(&session, pages, 64, 64, &backing) == BC250_VM_SESSION_OK);
    CHECK(Bc250VmSessionMap(&session, 0, &backing, 15, &alias,
        &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    for (failure = 1; failure <= 64; ++failure) {
        BC250_GART_U64 digest = Digest(&session.Backend);
        unsigned int live = heap.Base.Live;
        memcpy(&before, &session, sizeof(session));
        memset(&output, 0xA5, sizeof(output)); memcpy(&sentinel, &output, sizeof(output));
        heap.Base.FailWrite = failure; heap.Base.HookCalls = 0; heap.Base.Reentry = 1;
        CHECK(Bc250VmSessionMap(&session, va, &backing, 15, &output,
            &journal, SessionAfterWrite, &heap) == BC250_VM_SESSION_FAULT);
        CHECK(heap.Base.HookCalls == failure && heap.Base.Live == live);
        CHECK(Digest(&session.Backend) == digest);
        CHECK(memcmp(&output, &sentinel, sizeof(output)) == 0);
        CHECK(session.Backing.Records[backing.Slot].References == 1);
        SameExceptLeaseCounter(&session, &before);
        CHECK(memcmp(&journal, &empty_journal, sizeof(journal)) == 0);
        CheckValue(&session.Backend, 0, Expected(&pages[0], 15));
    }
    heap.Base.FailWrite = 0; heap.Base.HookCalls = 0;
    CHECK(Bc250VmSessionMap(&session, va, &backing, 15, &mapping,
        &journal, SessionAfterWrite, &heap) == BC250_VM_SESSION_OK);
    CHECK(heap.Base.HookCalls == 64 && session.Backing.Records[backing.Slot].References == 2);
    for (failure = 1; failure <= 64; ++failure) {
        BC250_GART_U64 digest = Digest(&session.Backend);
        unsigned int frees = heap.Base.Frees, attempts = heap.Base.Attempts;
        memcpy(&before, &session, sizeof(session));
        heap.Base.FailWrite = failure; heap.Base.HookCalls = 0;
        CHECK(Bc250VmSessionUnmap(&session, &mapping, &journal,
            SessionAfterWrite, &heap) == BC250_VM_SESSION_FAULT);
        CHECK(heap.Base.HookCalls == failure && heap.Base.Frees == frees && heap.Base.Attempts == attempts);
        CHECK(Digest(&session.Backend) == digest && memcmp(&session, &before, sizeof(session)) == 0);
        CHECK(session.Backing.Records[backing.Slot].References == 2);
        CheckValue(&session.Backend, 0, Expected(&pages[0], 15));
        for (i = 0; i < 64; ++i) CheckValue(&session.Backend,
            va + (BC250_GART_U64)i * 4096, Expected(&pages[i], 15));
    }
    heap.Base.FailWrite = 0; heap.Base.HookCalls = 0;
    CHECK(Bc250VmSessionUnmap(&session, &mapping, &journal,
        SessionAfterWrite, &heap) == BC250_VM_SESSION_OK);
    CHECK(session.Backing.Records[backing.Slot].References == 1);
    CheckValue(&session.Backend, 0, Expected(&pages[0], 15));
    CHECK(Bc250VmSessionUnmap(&session, &alias, &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    CHECK(session.Backing.Records[backing.Slot].References == 0 && heap.Base.Live == 1);
    CHECK(Bc250VmSessionRelease(&session, &backing) == BC250_VM_SESSION_OK);
    EndSession(&heap, &session);
}

static void SessionAllocationFailures(void)
{
    SESSION_HEAP heap;
    BC250_VM_CPU_SESSION session, before;
    BC250_BACKING_HANDLE backing;
    BC250_VM_SESSION_MAPPING_HANDLE mapping, output, sentinel;
    BC250_VM_CPU_JOURNAL journal;
    BC250_ADDRESS_SPAN pages[64];
    unsigned int total, failure;
    const BC250_GART_U64 va = (1ULL << 39) - 4096;
    SessionPages(pages, 64); StartSession(&heap, &session);
    CHECK(Bc250VmSessionRegister(&session, pages, 64, 64, &backing) == BC250_VM_SESSION_OK);
    CHECK(Bc250VmSessionMap(&session, va, &backing, 3, &mapping,
        &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    total = heap.Base.Attempts; CHECK(total != 0);
    CHECK(Bc250VmSessionUnmap(&session, &mapping, &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    CHECK(Bc250VmSessionRelease(&session, &backing) == BC250_VM_SESSION_OK);
    EndSession(&heap, &session);
    for (failure = 1; failure <= total; ++failure) {
        BC250_GART_U64 digest;
        StartSession(&heap, &session);
        CHECK(Bc250VmSessionRegister(&session, pages, 64, 64, &backing) == BC250_VM_SESSION_OK);
        memcpy(&before, &session, sizeof(session)); digest = Digest(&session.Backend);
        memset(&output, 0xA5, sizeof(output)); memcpy(&sentinel, &output, sizeof(output));
        heap.Base.FailAllocation = failure; heap.Base.Reentry = 1;
        CHECK(Bc250VmSessionMap(&session, va, &backing, 3, &output,
            &journal, NULL, NULL) == BC250_VM_SESSION_NO_MEMORY);
        CHECK(heap.Base.Attempts == failure && heap.Base.Live == 1 && session.Backend.OwnedNodes == 1);
        CHECK(memcmp(&output, &sentinel, sizeof(output)) == 0 && Digest(&session.Backend) == digest);
        SameExceptLeaseCounter(&session, &before);
        CHECK(session.Backing.Records[backing.Slot].References == 0);
        CHECK(Bc250VmSessionRelease(&session, &backing) == BC250_VM_SESSION_OK);
        EndSession(&heap, &session);
    }
}

static void SessionCapacityAndStale(void)
{
    SESSION_HEAP heap;
    BC250_VM_CPU_SESSION session, before;
    BC250_BACKING_HANDLE backings[8], newer, out;
    BC250_VM_SESSION_MAPPING_HANDLE mappings[16], output;
    BC250_VM_CPU_JOURNAL journal;
    BC250_ADDRESS_SPAN page;
    unsigned int i;
    StartSession(&heap, &session); Page(&page, 0x44000);
    for (i = 0; i < 8; ++i)
        CHECK(Bc250VmSessionRegister(&session, &page, 1, 1, &backings[i]) == BC250_VM_SESSION_OK);
    memcpy(&before, &session, sizeof(session));
    CHECK(Bc250VmSessionRegister(&session, &page, 1, 1, &out) == BC250_VM_SESSION_FULL);
    CHECK(memcmp(&session, &before, sizeof(session)) == 0);
    for (i = 0; i < 16; ++i) {
        CHECK(Bc250VmSessionMap(&session, (BC250_GART_U64)i * 4096, &backings[0],
            3, &mappings[i], &journal, NULL, NULL) == BC250_VM_SESSION_OK);
        CHECK(mappings[i].Id == (BC250_GART_U64)i + 1);
    }
    CHECK(session.Backing.Records[backings[0].Slot].References == 16);
    memcpy(&before, &session, sizeof(session));
    CHECK(Bc250VmSessionMap(&session, 16ULL * 4096, &backings[0], 3, &output,
        &journal, NULL, NULL) == BC250_VM_SESSION_FULL);
    CHECK(memcmp(&session, &before, sizeof(session)) == 0);
    for (i = 0; i < 16; ++i)
        CHECK(Bc250VmSessionUnmap(&session, &mappings[(i * 7) % 16], &journal,
            NULL, NULL) == BC250_VM_SESSION_OK);
    CHECK(session.Backing.Records[backings[0].Slot].References == 0);
    CHECK(Bc250VmSessionMap(&session, 0, &backings[0], 3, &output,
        &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    CHECK(output.Id == 17);
    CHECK(Bc250VmSessionUnmap(&session, &mappings[0], &journal,
        NULL, NULL) == BC250_VM_SESSION_STALE);
    CHECK(Bc250VmSessionUnmap(&session, &output, &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    for (i = 0; i < 8; ++i)
        CHECK(Bc250VmSessionRelease(&session, &backings[i]) == BC250_VM_SESSION_OK);
    CHECK(Bc250VmSessionRegister(&session, &page, 1, 1, &newer) == BC250_VM_SESSION_OK);
    CHECK(newer.Slot == backings[0].Slot && newer.Generation == backings[0].Generation + 1);
    CHECK(Bc250VmSessionMap(&session, 0, &backings[0], 3, &output,
        &journal, NULL, NULL) == BC250_VM_SESSION_STALE);
    CHECK(Bc250VmSessionRelease(&session, &newer) == BC250_VM_SESSION_OK);
    session.LastMappingId = ~0ULL; memcpy(&before, &session, sizeof(session));
    CHECK(Bc250VmSessionRegister(&session, &page, 1, 1, &newer) == BC250_VM_SESSION_OK);
    CHECK(Bc250VmSessionMap(&session, 0, &newer, 3, &output,
        &journal, NULL, NULL) == BC250_VM_SESSION_EXHAUSTED);
    CHECK(Bc250VmSessionRelease(&session, &newer) == BC250_VM_SESSION_OK);
    EndSession(&heap, &session);
}

static void SessionCorruptionAndAliasing(void)
{
    SESSION_HEAP heap;
    BC250_VM_CPU_SESSION session, before;
    BC250_BACKING_HANDLE backing;
    BC250_VM_SESSION_MAPPING_HANDLE mapping, output;
    BC250_VM_CPU_JOURNAL journal;
    BC250_ADDRESS_SPAN page;
    BC250_VM_CPU_NODE *leaf;
    unsigned int frees, attempts;
    StartSession(&heap, &session); Page(&page, 0x61000);
    CHECK(Bc250VmSessionRegister(&session, &page, 1, 1, &backing) == BC250_VM_SESSION_OK);
    CHECK(Bc250VmSessionMap(&session, 0, &backing, 3, &mapping,
        &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    leaf = IndependentLeaf(&session.Backend, 0); frees = heap.Base.Frees; attempts = heap.Base.Attempts;
    ++session.Backing.Records[backing.Slot].References;
    CHECK(Bc250VmSessionUnmap(&session, &mapping, &journal, NULL, NULL) == BC250_VM_SESSION_CORRUPT);
    CHECK(heap.Base.Frees == frees && heap.Base.Attempts == attempts);
    --session.Backing.Records[backing.Slot].References;
    leaf->Data.Values[99] = Expected(&page, 3); /* unrecorded raw leaf */
    CHECK(Bc250VmSessionUnmap(&session, &mapping, &journal, NULL, NULL) == BC250_VM_SESSION_CORRUPT);
    CHECK(heap.Base.Frees == frees && leaf->Data.Values[0] == Expected(&page, 3));
    leaf->Data.Values[99] = 0;
    memcpy(&before, &session, sizeof(session));
    memset(&session.Mappings[mapping.Slot], 0, sizeof(session.Mappings[mapping.Slot]));
    CHECK(Bc250VmSessionUnmap(&session, &mapping, &journal, NULL, NULL) == BC250_VM_SESSION_CORRUPT);
    CHECK(heap.Base.Frees == frees && session.Backing.Records[backing.Slot].References == 1);
    memcpy(session.Mappings, before.Mappings, sizeof(session.Mappings));
    memcpy(&before, &session, sizeof(session));
    CHECK(Bc250VmSessionMap(&session, 4096, &backing, 3,
        (BC250_VM_SESSION_MAPPING_HANDLE *)&session, &journal, NULL, NULL) == BC250_VM_SESSION_INVALID);
    CHECK(Bc250VmSessionMap(&session, 4096, &backing, 3, &output,
        (BC250_VM_CPU_JOURNAL *)&session, NULL, NULL) == BC250_VM_SESSION_INVALID);
    CHECK(Bc250VmSessionUnmap(&session, &mapping,
        (BC250_VM_CPU_JOURNAL *)session.Backend.Root, NULL, NULL) == BC250_VM_SESSION_INVALID);
    CHECK(Bc250VmSessionRegister(&session, session.Backing.Records[backing.Slot].Pages,
        1, 1, &backing) == BC250_VM_SESSION_INVALID);
    CHECK(Bc250VmSessionRegister(&session, &page, 1, 1,
        (BC250_BACKING_HANDLE *)&session) == BC250_VM_SESSION_INVALID);
    CHECK(memcmp(&session, &before, sizeof(session)) == 0);
    CHECK(Bc250VmSessionUnmap(&session, &mapping, &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    CHECK(Bc250VmSessionRelease(&session, &backing) == BC250_VM_SESSION_OK);
    EndSession(&heap, &session);
}

static void SessionBoundsAndInvalidControls(void)
{
    static const BC250_GART_U64 limits[3] = {
        1ULL << 36, (1ULL << 18) + 1ULL, (1ULL << 9) + 1ULL
    };
    unsigned int root;
    for (root = 1; root <= 3; ++root) {
        SESSION_HEAP heap;
        BC250_VM_CPU_SESSION session, before;
        BC250_BACKING_HANDLE backing, failed, sentinel_backing;
        BC250_VM_SESSION_MAPPING_HANDLE output, sentinel, stale;
        BC250_VM_CPU_JOURNAL journal;
        BC250_ADDRESS_SPAN pages[2], malformed[2];
        BC250_GART_U64 va = (limits[root - 1] - 2ULL) * 4096ULL, digest;
        unsigned int attempts;
        StartSessionLayout(&heap, &session, root, limits[root - 1]);
        SessionPages(pages, 2); memcpy(malformed, pages, sizeof(pages));
        malformed[1].Bytes = 8192; /* a whole-list rejection, not partial copy */
        memset(&failed, 0xA5, sizeof(failed)); memcpy(&sentinel_backing, &failed, sizeof(failed));
        memcpy(&before, &session, sizeof(session)); attempts = heap.Base.Attempts;
        CHECK(Bc250VmSessionRegister(&session, malformed, 2, 2, &failed) == BC250_VM_SESSION_INVALID);
        CHECK(Bc250VmSessionRegister(&session, pages, 2, 1, &failed) == BC250_VM_SESSION_INVALID);
        CHECK(Bc250VmSessionRegister(&session, pages, 0, 2, &failed) == BC250_VM_SESSION_INVALID);
        CHECK(memcmp(&failed, &sentinel_backing, sizeof(failed)) == 0);
        CHECK(memcmp(&session, &before, sizeof(session)) == 0 && heap.Base.Attempts == attempts);
        CHECK(Bc250VmSessionRegister(&session, pages, 2, 2, &backing) == BC250_VM_SESSION_OK);
        memcpy(&before, &session, sizeof(session)); digest = Digest(&session.Backend);
        memset(&output, 0xA5, sizeof(output)); memcpy(&sentinel, &output, sizeof(output));
        CHECK(Bc250VmSessionMap(&session, va + 4096, &backing, 3, &output,
            &journal, NULL, NULL) == BC250_VM_SESSION_INVALID);
        CHECK(Bc250VmSessionMap(&session, limits[root - 1] * 4096ULL, &backing, 3, &output,
            &journal, NULL, NULL) == BC250_VM_SESSION_INVALID);
        CHECK(Bc250VmSessionMap(&session, va + 1, &backing, 3, &output,
            &journal, NULL, NULL) == BC250_VM_SESSION_INVALID);
        CHECK(Bc250VmSessionMap(&session, va, &backing, 16, &output,
            &journal, NULL, NULL) == BC250_VM_SESSION_INVALID);
        CHECK(memcmp(&output, &sentinel, sizeof(output)) == 0 && Digest(&session.Backend) == digest);
        CHECK(memcmp(&session, &before, sizeof(session)) == 0 && heap.Base.Attempts == attempts);
        CHECK(Bc250VmSessionMap(&session, va, &backing, 3, &output,
            &journal, NULL, NULL) == BC250_VM_SESSION_OK);
        CheckValue(&session.Backend, va, Expected(&pages[0], 3));
        CheckValue(&session.Backend, va + 4096, Expected(&pages[1], 3));
        stale = output; stale.Slot = BC250_VM_SESSION_MAX_MAPPINGS;
        CHECK(Bc250VmSessionUnmap(&session, &stale, &journal, NULL, NULL) == BC250_VM_SESSION_STALE);
        stale = output; ++stale.Id;
        CHECK(Bc250VmSessionUnmap(&session, &stale, &journal, NULL, NULL) == BC250_VM_SESSION_STALE);
        CHECK(Bc250VmSessionUnmap(&session, &output, &journal, NULL, NULL) == BC250_VM_SESSION_OK);
        CHECK(session.Backend.OwnedNodes == 1 && heap.Base.Live == 1);
        CHECK(Bc250VmSessionRelease(&session, &backing) == BC250_VM_SESSION_OK);
        EndSession(&heap, &session);
    }
}

int main(void)
{
    unsigned int baseline;
    CHECK(Bc250PreviousCpuBackendTests() == 0); baseline = assertions;
    SessionAliasesAndTerminal(); SessionFaults(); SessionAllocationFailures();
    SessionCapacityAndStale(); SessionCorruptionAndAliasing(); SessionBoundsAndInvalidControls();
    printf("PASS: %u additional CPU session assertions (%u total); NOT Windows/GPU ownership.\n",
        assertions - baseline, assertions);
    return 0;
}
