/* SPDX-License-Identifier: Apache-2.0
 * Original RAM fixtures/oracles only; raw metadata inspection is test-only.
 */
#define main Bc250PreviousCpuBackendTests
#include "../vm-cpu-backend-20261003/test-vm-cpu-backend.c"
#undef main
#include "dma-runs-20261003/bc250_dma_runs.h"

/* Flat predecessor inclusion avoids redefining main through nested test files.
 * Minimal original session fixture; domain regressions run as a separate EXE. */
typedef struct SESSION_HEAP {
    TEST_HEAP Base;
    BC250_VM_CPU_SESSION *Session;
} SESSION_HEAP;
typedef struct DOMAIN_SNAPSHOT {
    BC250_VM_CPU_SESSION Session;
    BC250_GART_U64 Digest;
    unsigned int Attempts, Frees, Live, Hooks;
} DOMAIN_SNAPSHOT;
static BC250_VM_CPU_NODE *SessionAllocate(void *opaque, size_t bytes)
{
    SESSION_HEAP *heap = opaque;
    return Allocate(&heap->Base, bytes);
}
static void SessionFree(void *opaque, BC250_VM_CPU_NODE *node)
{
    SESSION_HEAP *heap = opaque;
    Release(&heap->Base, node);
}
static int SessionAfterWrite(void *opaque, unsigned int count)
{
    SESSION_HEAP *heap = opaque;
    return AfterWrite(&heap->Base, count);
}
static void StartSession(SESSION_HEAP *heap, BC250_VM_CPU_SESSION *session)
{
    BC250_VM_LAYOUT layout;
    memset(heap, 0, sizeof(*heap)); memset(session, 0, sizeof(*session));
    heap->Session = session; heap->Base.Context = &session->Backend;
    CHECK(Bc250VmPlanLayout(1, 9, 1ULL << 36, &layout));
    CHECK(Bc250VmSessionInit(session, &layout, BC250_VM_MAX_ADDRESS, 256,
        SessionAllocate, SessionFree, heap) == BC250_VM_SESSION_OK);
    CHECK(heap->Base.Live == 1 && session->Backend.OwnedNodes == 1);
    heap->Base.Attempts = 0;
}
static void EndSession(SESSION_HEAP *heap, BC250_VM_CPU_SESSION *session)
{
    CHECK(Bc250VmSessionShutdown(session) == BC250_VM_SESSION_OK);
    CHECK(!session->Backend.Root && !heap->Base.Live);
    FinishHeap(&heap->Base);
}
static void DomainSpan(BC250_AD_SPAN *span, unsigned int domain,
    BC250_AD_U64 start, BC250_AD_U64 bytes)
{
    memset(span, 0, sizeof(*span)); span->Domain = domain;
    span->Start = start; span->Bytes = bytes; span->Last = start + bytes - 1ULL;
}
static void Snapshot(SESSION_HEAP *heap, BC250_VM_CPU_SESSION *session, DOMAIN_SNAPSHOT *out)
{
    memcpy(&out->Session, session, sizeof(*session)); out->Digest = Digest(&session->Backend);
    out->Attempts = heap->Base.Attempts; out->Frees = heap->Base.Frees;
    out->Live = heap->Base.Live; out->Hooks = heap->Base.HookCalls;
}
static void Unchanged(SESSION_HEAP *heap, BC250_VM_CPU_SESSION *session, const DOMAIN_SNAPSHOT *before)
{
    CHECK(memcmp(session, &before->Session, sizeof(*session)) == 0 && Digest(&session->Backend) == before->Digest);
    CHECK(heap->Base.Attempts == before->Attempts && heap->Base.Frees == before->Frees &&
        heap->Base.Live == before->Live && heap->Base.HookCalls == before->Hooks);
}
static void SameExceptLeaseCounter(const BC250_VM_CPU_SESSION *session, BC250_VM_CPU_SESSION *before)
{
    CHECK(session->Backing.LastLeaseId == before->Backing.LastLeaseId + 1ULL);
    before->Backing.LastLeaseId = session->Backing.LastLeaseId;
    CHECK(memcmp(session, before, sizeof(*session)) == 0);
}

static void RejectRuns(const BC250_AD_SPAN *runs, unsigned int count,
    unsigned int capacity, BC250_AD_U64 bytes, BC250_AD_U64 limit,
    BC250_AD_SPAN *pages, unsigned int page_capacity, unsigned int *out_count)
{
    unsigned char saved[64 * sizeof(BC250_AD_SPAN)], saved_count[sizeof(*out_count)];
    size_t output_bytes = pages ? (size_t)page_capacity * sizeof(*pages) : 0;
    CHECK(page_capacity <= 64);
    if (output_bytes) memcpy(saved, pages, output_bytes);
    if (out_count) memcpy(saved_count, out_count, sizeof(*out_count));
    CHECK(!Bc250DmaRunsExpand(runs, count, capacity, bytes, limit, pages, page_capacity, out_count));
    if (output_bytes) CHECK(memcmp(saved, pages, output_bytes) == 0);
    if (out_count) CHECK(memcmp(saved_count, out_count, sizeof(*out_count)) == 0);
}
static void TestRunPartitions(void)
{
    BC250_AD_SPAN runs[64], saved[64], pages[65];
    unsigned int n, split, i, j, index, output_count;
    for (n = 1; n <= 64; ++n) {
        /* Every two-run partition, including a single run and nonmonotonic
         * addresses. Expected pages are computed independently of the adapter. */
        for (split = 1; split <= n; ++split) {
            unsigned int count = split == n ? 1U : 2U;
            memset(runs, 0, sizeof(runs));
            DomainSpan(&runs[0], 5, 0x900000ULL, (BC250_AD_U64)split * 4096);
            if (count == 2) DomainSpan(&runs[1], 5, 0x100000ULL, (BC250_AD_U64)(n - split) * 4096);
            memcpy(saved, runs, sizeof(runs)); memset(pages, 0xA5, sizeof(pages)); output_count = 0xA5A5A5A5U;
            CHECK(Bc250DmaRunsExpand(runs, count, 64, (BC250_AD_U64)n * 4096,
                BC250_AD_MAX48, pages, 64, &output_count));
            CHECK(output_count == n && memcmp(saved, runs, sizeof(runs)) == 0);
            index = 0;
            for (i = 0; i < count; ++i) {
                unsigned int length = i ? n - split : split;
                for (j = 0; j < length; ++j, ++index) {
                    BC250_AD_U64 expected = (i ? 0x100000ULL : 0x900000ULL) + (BC250_AD_U64)j * 4096;
                    CHECK(pages[index].Domain == 5 && pages[index].Start == expected &&
                        pages[index].Bytes == 4096 && pages[index].Last == expected + 4095);
                }
            }
            for (i = n; i < 65; ++i) {
                BC250_AD_SPAN sentinel; memset(&sentinel, 0xA5, sizeof(sentinel));
                CHECK(memcmp(&pages[i], &sentinel, sizeof(sentinel)) == 0);
            }
            RejectRuns(runs, count, count, (BC250_AD_U64)n * 4096,
                BC250_AD_MAX48, pages, n - 1, &output_count);
        }
        /* Fragmented duplicated page addresses are preserved, not deduped. */
        for (i = 0; i < n; ++i) DomainSpan(&runs[i], 5, 0x2000, 4096);
        CHECK(Bc250DmaRunsExpand(runs, n, n, (BC250_AD_U64)n * 4096,
            BC250_AD_MAX48, pages, n, &output_count));
        CHECK(output_count == n);
        for (i = 0; i < n; ++i) CHECK(pages[i].Start == 0x2000);
    }
}
static void TestBadRuns(void)
{
    BC250_AD_SPAN runs[65], bad[65], pages[64];
    unsigned int count = 0xA5A5A5A5U, i, tag;
    for (i = 0; i < 65; ++i) DomainSpan(&runs[i], 5, (BC250_AD_U64)i * 4096, 4096);
    memset(pages, 0xA5, sizeof(pages));
    for (tag = 0; tag <= 8; ++tag) {
        if (tag == 5) continue;
        memcpy(bad, runs, sizeof(runs)); bad[63].Domain = tag;
        RejectRuns(bad, 64, 64, 64ULL * 4096, BC250_AD_MAX48, pages, 64, &count);
    }
    memcpy(bad, runs, sizeof(runs)); ++bad[63].Last;
    RejectRuns(bad, 64, 64, 64ULL * 4096, BC250_AD_MAX48, pages, 64, &count);
    memcpy(bad, runs, sizeof(runs)); bad[63].Domain = ~0U;
    RejectRuns(bad, 64, 64, 64ULL * 4096, BC250_AD_MAX48, pages, 64, &count);
    memcpy(bad, runs, sizeof(runs)); DomainSpan(&bad[63], 5, 0x2000, 8192);
    RejectRuns(bad, 64, 64, 64ULL * 4096, BC250_AD_MAX48, pages, 64, &count);
    memcpy(bad, runs, sizeof(runs)); DomainSpan(&bad[63], 5, 0x2001, 4096);
    RejectRuns(bad, 64, 64, 64ULL * 4096, BC250_AD_MAX48, pages, 64, &count);
    memcpy(bad, runs, sizeof(runs)); DomainSpan(&bad[63], 5, 0x2000, 4095);
    RejectRuns(bad, 64, 64, 64ULL * 4096, BC250_AD_MAX48, pages, 64, &count);
    DomainSpan(&bad[0], 5, 0, 0);
    RejectRuns(bad, 1, 1, 4096, BC250_AD_MAX48, pages, 64, &count);
    DomainSpan(&bad[0], 5, 0, 65ULL * 4096);
    RejectRuns(bad, 1, 1, 64ULL * 4096, BC250_AD_MAX48, pages, 64, &count);
    DomainSpan(&bad[0], 5, 0, 1ULL << 48);
    RejectRuns(bad, 1, 1, 4096, BC250_AD_MAX48, pages, 64, &count);
    DomainSpan(&bad[0], 5, BC250_AD_MAX48 - 4095, 4096);
    CHECK(Bc250DmaRunsExpand(bad, 1, 1, 4096, BC250_AD_MAX48, pages, 64, &count));
    CHECK(count == 1 && pages[0].Last == BC250_AD_MAX48);
    RejectRuns(bad, 1, 1, 4096, BC250_AD_MAX48 - 1, pages, 64, &count);
    DomainSpan(&bad[0], 5, ~0ULL - 4095, 8192);
    RejectRuns(bad, 1, 1, 8192, BC250_AD_MAX48, pages, 64, &count);
    RejectRuns(runs, 1, 1, 4096, ~0ULL, pages, 64, &count);
    RejectRuns(runs, 1, 1, 0, BC250_AD_MAX48, pages, 64, &count);
    RejectRuns(runs, 1, 1, 4095, BC250_AD_MAX48, pages, 64, &count);
    RejectRuns(runs, 1, 1, 8192, BC250_AD_MAX48, pages, 64, &count);
    RejectRuns(runs, 0, 1, 4096, BC250_AD_MAX48, pages, 64, &count);
    RejectRuns(runs, 65, 65, 4096, BC250_AD_MAX48, pages, 64, &count);
    RejectRuns(runs, ~0U, 65, 4096, BC250_AD_MAX48, pages, 64, &count);
    RejectRuns(runs, 2, 1, 8192, BC250_AD_MAX48, pages, 64, &count);
    RejectRuns(NULL, 1, 1, 4096, BC250_AD_MAX48, pages, 64, &count);
    RejectRuns(runs, 1, 1, 4096, BC250_AD_MAX48, NULL, 0, &count);
    RejectRuns(runs, 1, 1, 4096, BC250_AD_MAX48, pages, 64, NULL);
    RejectRuns(runs, 1, 1, 4096, BC250_AD_MAX48, runs, 1, &count);
    RejectRuns(runs, 1, 1, 4096, BC250_AD_MAX48, pages, 1, (unsigned int *)runs);
    RejectRuns(runs, 1, 1, 4096, BC250_AD_MAX48, pages, 1, (unsigned int *)pages);
    /* Partial overlap, not merely equal addresses. */
    RejectRuns(runs, 2, 2, 8192, BC250_AD_MAX48, runs + 1, 2, &count);
}
static int RunsAfterWrite(void *opaque, unsigned int count)
{
    SESSION_HEAP *heap = opaque;
    BC250_BACKING_HANDLE output;
    unsigned char saved[sizeof(output)];
    memset(&output, 0xA5, sizeof(output)); memcpy(saved, &output, sizeof(output));
    CHECK(Bc250VmDomainRegisterRuns(heap->Session, NULL, 0, 0, 0, &output) == BC250_VM_SESSION_BUSY_RESULT);
    CHECK(memcmp(saved, &output, sizeof(output)) == 0);
    return SessionAfterWrite(opaque, count);
}
static void RejectRunRegistration(SESSION_HEAP *heap, BC250_VM_CPU_SESSION *session,
    const BC250_AD_SPAN *runs, unsigned int count, BC250_AD_U64 bytes,
    BC250_BACKING_HANDLE *out, BC250_VM_SESSION_STATUS expected)
{
    DOMAIN_SNAPSHOT before;
    unsigned char saved[sizeof(*out)];
    if (out) memcpy(saved, out, sizeof(*out));
    Snapshot(heap, session, &before);
    CHECK(Bc250VmDomainRegisterRuns(session, runs, count, count, bytes, out) == expected);
    if (out) CHECK(memcmp(saved, out, sizeof(*out)) == 0);
    Unchanged(heap, session, &before);
}
static void TestRunSession(void)
{
    SESSION_HEAP heap;
    BC250_VM_CPU_SESSION session;
    BC250_AD_SPAN runs[2], bad[2], va;
    BC250_BACKING_HANDLE backing, stale, output, full[8];
    BC250_VM_SESSION_MAPPING_HANDLE mapping;
    BC250_VM_CPU_JOURNAL journal;
    DOMAIN_SNAPSHOT before;
    BC250_ADDRESS_SPAN expected;
    unsigned int i;
    StartSession(&heap, &session);
    DomainSpan(&runs[0], 5, 0x80000, 8192); DomainSpan(&runs[1], 5, 0x31000, 8192);
    memset(&output, 0xA5, sizeof(output)); memcpy(bad, runs, sizeof(runs)); bad[1].Domain = 3;
    RejectRunRegistration(&heap, &session, bad, 2, 16384, &output, BC250_VM_SESSION_INVALID);
    RejectRunRegistration(&heap, &session, runs, 2, 8192, &output, BC250_VM_SESSION_INVALID);
    RejectRunRegistration(&heap, &session, runs, 2, 16384, (BC250_BACKING_HANDLE *)runs, BC250_VM_SESSION_INVALID);
    RejectRunRegistration(&heap, &session, (BC250_AD_SPAN *)&session, 1, 4096, &output, BC250_VM_SESSION_INVALID);
    RejectRunRegistration(&heap, &session, (BC250_AD_SPAN *)session.Backend.Root, 1, 4096, &output, BC250_VM_SESSION_INVALID);
    RejectRunRegistration(&heap, &session, runs, 2, 16384, (BC250_BACKING_HANDLE *)&session, BC250_VM_SESSION_INVALID);
    RejectRunRegistration(&heap, &session, runs, 2, 16384, NULL, BC250_VM_SESSION_INVALID);
    RejectRunRegistration(&heap, &session, NULL, 1, 4096, &output, BC250_VM_SESSION_INVALID);
    RejectRunRegistration(&heap, &session, runs, 0, 4096, &output, BC250_VM_SESSION_INVALID);
    RejectRunRegistration(&heap, &session, runs, 2, 16384, (BC250_BACKING_HANDLE *)session.Backend.Root, BC250_VM_SESSION_INVALID);
    {
        unsigned char saved[sizeof(output)];
        memcpy(saved, &output, sizeof(output)); Snapshot(&heap, &session, &before);
        CHECK(Bc250VmDomainRegisterRuns(NULL, runs, 2, 2, 16384, &output) == BC250_VM_SESSION_INVALID);
        CHECK(Bc250VmDomainRegisterRuns(&session, runs, 2, 1, 16384, &output) == BC250_VM_SESSION_INVALID);
        CHECK(Bc250VmDomainRegisterRuns(&session, runs, 1, 0, 8192, &output) == BC250_VM_SESSION_INVALID);
        CHECK(Bc250VmDomainRegisterRuns(&session, runs, 65, 65, 16384, &output) == BC250_VM_SESSION_INVALID);
        CHECK(memcmp(saved, &output, sizeof(output)) == 0); Unchanged(&heap, &session, &before);
    }
    CHECK(Bc250VmDomainRegisterRuns(&session, runs, 2, 2, 16384, &backing) == BC250_VM_SESSION_OK);
    stale = backing;
    DomainSpan(&va, 6, (1ULL << 21) - 4096, 16384);
    Snapshot(&heap, &session, &before); heap.Base.FailWrite = 3; heap.Base.HookCalls = 0;
    CHECK(Bc250VmDomainMap(&session, &va, &backing, 15, &mapping, &journal, RunsAfterWrite, &heap) == BC250_VM_SESSION_FAULT);
    CHECK(Digest(&session.Backend) == before.Digest && heap.Base.Live == before.Live);
    SameExceptLeaseCounter(&session, &before.Session);
    heap.Base.FailWrite = 0; heap.Base.HookCalls = 0;
    CHECK(Bc250VmDomainMap(&session, &va, &backing, 15, &mapping, &journal, RunsAfterWrite, &heap) == BC250_VM_SESSION_OK);
    for (i = 0; i < 4; ++i) {
        Page(&expected, i < 2 ? 0x80000ULL + (BC250_AD_U64)i * 4096 : 0x31000ULL + (BC250_AD_U64)(i - 2) * 4096);
        CheckValue(&session.Backend, va.Start + (BC250_AD_U64)i * 4096, Expected(&expected, 15));
    }
    CHECK(Bc250VmSessionRelease(&session, &backing) == BC250_VM_SESSION_IN_USE);
    heap.Base.FailWrite = 2; heap.Base.HookCalls = 0; Snapshot(&heap, &session, &before);
    CHECK(Bc250VmSessionUnmap(&session, &mapping, &journal, RunsAfterWrite, &heap) == BC250_VM_SESSION_FAULT);
    CHECK(Digest(&session.Backend) == before.Digest && memcmp(&session, &before.Session, sizeof(session)) == 0);
    heap.Base.FailWrite = 0; heap.Base.HookCalls = 0;
    CHECK(Bc250VmSessionUnmap(&session, &mapping, &journal, RunsAfterWrite, &heap) == BC250_VM_SESSION_OK);
    CHECK(Bc250VmSessionRelease(&session, &backing) == BC250_VM_SESSION_OK);
    CHECK(Bc250VmSessionRelease(&session, &stale) == BC250_VM_SESSION_STALE);
    for (i = 0; i < 8; ++i) CHECK(Bc250VmDomainRegisterRuns(&session, runs, 2, 2, 16384, &full[i]) == BC250_VM_SESSION_OK);
    RejectRunRegistration(&heap, &session, runs, 2, 16384, &output, BC250_VM_SESSION_FULL);
    for (i = 0; i < 8; ++i) CHECK(Bc250VmSessionRelease(&session, &full[i]) == BC250_VM_SESSION_OK);
    EndSession(&heap, &session);
    CHECK(Bc250VmDomainRegisterRuns(&session, NULL, 0, 0, 0, &output) == BC250_VM_SESSION_DEAD_RESULT);
}
int main(void)
{
    unsigned int baseline;
    CHECK(Bc250PreviousCpuBackendTests() == 0); baseline = assertions;
    TestRunPartitions(); TestBadRuns(); TestRunSession();
    printf("PASS: %u additional DMA-run RAM assertions (%u total); NO OS mapping or GPU permission\n", assertions - baseline, assertions);
    return 0;
}
