/* SPDX-License-Identifier: Apache-2.0
 * Domain-aware adapter against predecessor RAM allocator/oracles only.
 * Raw inspection/corruption below is test-only, not permitted facade usage.
 */
#define main Bc250PreviousCpuBackendTests
#include "../vm-cpu-backend-20261003/test-vm-cpu-backend.c"
#undef main
#include "domain-backend-20261003/bc250_vm_domain_backend.h"

/* Minimal original session fixture wrappers. The full historical session test
 * is run separately by the runner; it has its own nested main renaming. */
typedef struct SESSION_HEAP {
    TEST_HEAP Base;
    BC250_VM_CPU_SESSION *Session;
    unsigned int FacadeReentries;
} SESSION_HEAP;
static void SessionNested(SESSION_HEAP *heap)
{
    BC250_BACKING_HANDLE backing = {0}, output;
    BC250_VM_SESSION_MAPPING_HANDLE mapping = {0}, mapping_output;
    BC250_VM_LAYOUT layout = {0};
    if (!heap->Base.Reentry) return;
    ++heap->FacadeReentries;
    CHECK(Bc250VmSessionInit(heap->Session, &layout, 0, 1, NULL, NULL, NULL) == BC250_VM_SESSION_BUSY_RESULT);
    CHECK(Bc250VmSessionRelease(heap->Session, &backing) == BC250_VM_SESSION_BUSY_RESULT);
    CHECK(Bc250VmSessionUnmap(heap->Session, &mapping, &heap->Base.NestedJournal, NULL, NULL) == BC250_VM_SESSION_BUSY_RESULT);
    CHECK(Bc250VmSessionShutdown(heap->Session) == BC250_VM_SESSION_BUSY_RESULT);
    CHECK(Bc250VmDomainRegister(heap->Session, NULL, 0, 0, &output) == BC250_VM_SESSION_BUSY_RESULT);
    CHECK(Bc250VmDomainMap(heap->Session, NULL, NULL, 0, &mapping_output, NULL, NULL, NULL) == BC250_VM_SESSION_BUSY_RESULT);
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
static void StartSession(SESSION_HEAP *heap, BC250_VM_CPU_SESSION *session)
{
    BC250_VM_LAYOUT layout;
    memset(heap, 0, sizeof(*heap)); memset(session, 0, sizeof(*session));
    heap->Session = session; heap->Base.Context = &session->Backend;
    CHECK(Bc250VmPlanLayout(1, 9, 1ULL << 36, &layout));
    heap->Base.Reentry = 1;
    CHECK(Bc250VmSessionInit(session, &layout, BC250_VM_MAX_ADDRESS, 256,
        SessionAllocate, SessionFree, heap) == BC250_VM_SESSION_OK);
    CHECK(heap->Base.Live == 1 && session->Backend.OwnedNodes == 1 && heap->FacadeReentries == 1);
    heap->Base.Attempts = 0; heap->Base.Reentry = 0;
}
static void EndSession(SESSION_HEAP *heap, BC250_VM_CPU_SESSION *session)
{
    heap->Base.Reentry = 1;
    CHECK(Bc250VmSessionShutdown(session) == BC250_VM_SESSION_OK);
    CHECK(session->Backend.Root == NULL && heap->Base.Live == 0);
    FinishHeap(&heap->Base);
}
static void SameExceptLeaseCounter(const BC250_VM_CPU_SESSION *session, BC250_VM_CPU_SESSION *before)
{
    CHECK(session->Backing.LastLeaseId == before->Backing.LastLeaseId + 1ULL);
    before->Backing.LastLeaseId = session->Backing.LastLeaseId;
    CHECK(memcmp(session, before, sizeof(*session)) == 0);
}

typedef struct DOMAIN_SNAPSHOT {
    BC250_VM_CPU_SESSION Session;
    BC250_GART_U64 Digest;
    unsigned int Attempts, Frees, Live, Hooks;
} DOMAIN_SNAPSHOT;

static void DomainSpan(BC250_AD_SPAN *span, unsigned int domain,
    BC250_AD_U64 start, BC250_AD_U64 bytes)
{
    memset(span, 0, sizeof(*span));
    span->Domain = domain; span->Start = start; span->Bytes = bytes;
    span->Last = start + bytes - 1ULL;
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
static void RejectRegister(SESSION_HEAP *heap, BC250_VM_CPU_SESSION *session,
    const BC250_AD_SPAN *pages, unsigned int count, unsigned int capacity,
    BC250_BACKING_HANDLE *out, BC250_VM_SESSION_STATUS expected)
{
    DOMAIN_SNAPSHOT before;
    unsigned char saved[sizeof(*out)];
    unsigned char saved_pages[64 * sizeof(BC250_AD_SPAN)];
    size_t page_bytes = pages && count && count <= 64 ? (size_t)count * sizeof(*pages) : 0;
    if (out) memcpy(saved, out, sizeof(*out));
    if (page_bytes) memcpy(saved_pages, pages, page_bytes);
    Snapshot(heap, session, &before);
    CHECK(Bc250VmDomainRegister(session, pages, count, capacity, out) == expected);
    if (out) CHECK(memcmp(saved, out, sizeof(*out)) == 0);
    if (page_bytes) CHECK(memcmp(saved_pages, pages, page_bytes) == 0);
    Unchanged(heap, session, &before);
}
static void RejectMap(SESSION_HEAP *heap, BC250_VM_CPU_SESSION *session,
    const BC250_AD_SPAN *va, const BC250_BACKING_HANDLE *handle, unsigned int access,
    BC250_VM_SESSION_MAPPING_HANDLE *out, BC250_VM_CPU_JOURNAL *journal,
    BC250_VM_SESSION_STATUS expected)
{
    DOMAIN_SNAPSHOT before;
    unsigned char saved[sizeof(*out)];
    unsigned char saved_journal[sizeof(*journal)];
    if (out) memcpy(saved, out, sizeof(*out));
    if (journal) memcpy(saved_journal, journal, sizeof(*journal));
    Snapshot(heap, session, &before);
    CHECK(Bc250VmDomainMap(session, va, handle, access, out, journal, NULL, NULL) == expected);
    if (out) CHECK(memcmp(saved, out, sizeof(*out)) == 0);
    if (journal) CHECK(memcmp(saved_journal, journal, sizeof(*journal)) == 0);
    Unchanged(heap, session, &before);
}
static void RejectPreview(const BC250_AD_LAYOUT *layout, const BC250_AD_SPAN *input, BC250_AD_SPAN *out)
{
    unsigned char saved[sizeof(*out)];
    if (out) memcpy(saved, out, sizeof(*out));
    CHECK(!Bc250VmDomainPreviewVram(layout, input, out));
    if (out) CHECK(memcmp(saved, out, sizeof(*out)) == 0);
}
static void TestPreviewAndTags(void)
{
    SESSION_HEAP heap;
    BC250_VM_CPU_SESSION session;
    BC250_AD_LAYOUT layout, mutable_layout;
    BC250_AD_SPAN input, output, malformed;
    BC250_BACKING_HANDLE backing;
    union { BC250_AD_LAYOUT Layout; BC250_AD_SPAN Span; unsigned char Bytes[64]; } alias;
    unsigned int tag;
    StartSession(&heap, &session);
    CHECK(Bc250AdLayoutInit(0xF400000000ULL, 0x10000000ULL, 0x40000000ULL, &layout));
    DomainSpan(&input, BC250_AD_VRAM_OFFSET, 0x18000000ULL, 4096);
    CHECK(Bc250VmDomainPreviewVram(&layout, &input, &output));
    CHECK(output.Domain == BC250_AD_FB_PHYSICAL && output.Start == 0x28000000ULL && output.Bytes == 4096 && output.Last == 0x28000FFFULL);
    memset(&backing, 0xA5, sizeof(backing));
    /* FB=3 collides numerically with predecessor DMA=3, but is NOT admitted. */
    RejectRegister(&heap, &session, &output, 1, 1, &backing, BC250_VM_SESSION_INVALID);
    for (tag = 0; tag <= 8; ++tag) {
        DomainSpan(&input, tag, 0x10000000ULL, 4096);
        if (tag == BC250_AD_VRAM_OFFSET || tag == BC250_AD_FB_PHYSICAL)
            CHECK(Bc250VmDomainPreviewVram(&layout, &input, &output));
        else RejectPreview(&layout, &input, &output);
    }
    DomainSpan(&input, BC250_AD_VRAM_MC, layout.McBase + 4096, 4096);
    CHECK(Bc250VmDomainPreviewVram(&layout, &input, &output)); CHECK(output.Start == layout.FbPhysicalBase + 4096);
    malformed = input; malformed.Domain = 0xFFFFFFFFU; RejectPreview(&layout, &malformed, &output);
    malformed = input; ++malformed.Last; RejectPreview(&layout, &malformed, &output);
    malformed = input; malformed.Bytes = 0; RejectPreview(&layout, &malformed, &output);
    malformed = input; --malformed.Bytes; --malformed.Last; RejectPreview(&layout, &malformed, &output);
    DomainSpan(&malformed, BC250_AD_VRAM_MC, layout.McBase + 1, 4096); RejectPreview(&layout, &malformed, &output);
    DomainSpan(&malformed, BC250_AD_VRAM_OFFSET, ~0ULL - 4095, 8192); RejectPreview(&layout, &malformed, &output);
    mutable_layout = layout; mutable_layout.McBase |= 1; RejectPreview(&mutable_layout, &input, &output);
    mutable_layout = layout; mutable_layout.VramBytes = 0; RejectPreview(&mutable_layout, &input, &output);
    RejectPreview(NULL, &input, &output); RejectPreview(&layout, NULL, &output); RejectPreview(&layout, &input, NULL);
    RejectPreview(&layout, &input, &input);
    memset(&alias, 0xA5, sizeof(alias)); memcpy(&alias.Layout, &layout, sizeof(layout));
    RejectPreview(&alias.Layout, &input, &alias.Span);
    EndSession(&heap, &session);
}
static void TestRegisterAdmission(void)
{
    SESSION_HEAP heap;
    BC250_VM_CPU_SESSION session;
    BC250_AD_SPAN pages[65], saved[65], malformed[65];
    BC250_BACKING_HANDLE backing, out;
    unsigned int i, tag;
    StartSession(&heap, &session);
    for (i = 0; i < 65; ++i) DomainSpan(&pages[i], BC250_AD_DMA_LOGICAL, ((BC250_AD_U64)i + 1) * 4096, 4096);
    memcpy(saved, pages, sizeof(pages)); memset(&out, 0xA5, sizeof(out));
    for (tag = 0; tag <= 8; ++tag) {
        if (tag == BC250_AD_DMA_LOGICAL) continue;
        memcpy(malformed, pages, sizeof(pages)); malformed[1].Domain = tag;
        RejectRegister(&heap, &session, malformed, 2, 2, &out, BC250_VM_SESSION_INVALID);
        CHECK(memcmp(pages, saved, sizeof(pages)) == 0);
    }
    memcpy(malformed, pages, sizeof(pages)); malformed[1].Domain = 0xFFFFFFFFU;
    RejectRegister(&heap, &session, malformed, 2, 2, &out, BC250_VM_SESSION_INVALID);
    memcpy(malformed, pages, sizeof(pages)); ++malformed[1].Last;
    RejectRegister(&heap, &session, malformed, 2, 2, &out, BC250_VM_SESSION_INVALID);
    memcpy(malformed, pages, sizeof(pages)); DomainSpan(&malformed[1], 5, pages[1].Start + 1, 4096);
    RejectRegister(&heap, &session, malformed, 2, 2, &out, BC250_VM_SESSION_INVALID);
    memcpy(malformed, pages, sizeof(pages)); DomainSpan(&malformed[1], 5, ~0ULL - 4095, 8192);
    RejectRegister(&heap, &session, malformed, 2, 2, &out, BC250_VM_SESSION_INVALID);
    memcpy(malformed, pages, sizeof(pages)); DomainSpan(&malformed[1], 5, pages[1].Start, 8192);
    RejectRegister(&heap, &session, malformed, 2, 2, &out, BC250_VM_SESSION_INVALID);
    memcpy(malformed, pages, sizeof(pages)); DomainSpan(&malformed[1], 5, pages[1].Start, 0);
    RejectRegister(&heap, &session, malformed, 2, 2, &out, BC250_VM_SESSION_INVALID);
    RejectRegister(&heap, &session, pages, 2, 1, &out, BC250_VM_SESSION_INVALID);
    RejectRegister(&heap, &session, pages, 0, 2, &out, BC250_VM_SESSION_INVALID);
    RejectRegister(&heap, &session, pages, 65, 65, &out, BC250_VM_SESSION_INVALID);
    RejectRegister(&heap, &session, pages, 0xFFFFFFFFU, 65, &out, BC250_VM_SESSION_INVALID);
    RejectRegister(&heap, &session, NULL, 1, 1, &out, BC250_VM_SESSION_INVALID);
    RejectRegister(&heap, &session, pages, 1, 1, NULL, BC250_VM_SESSION_INVALID);
    RejectRegister(&heap, &session, pages, 2, 2, (BC250_BACKING_HANDLE *)pages, BC250_VM_SESSION_INVALID);
    CHECK(memcmp(pages, saved, sizeof(pages)) == 0);
    RejectRegister(&heap, &session, (BC250_AD_SPAN *)&session, 1, 1, &out, BC250_VM_SESSION_INVALID);
    RejectRegister(&heap, &session, (BC250_AD_SPAN *)session.Backend.Root, 1, 1, &out, BC250_VM_SESSION_INVALID);
    RejectRegister(&heap, &session, pages, 1, 1, (BC250_BACKING_HANDLE *)&session, BC250_VM_SESSION_INVALID);
    RejectRegister(&heap, &session, pages, 1, 1, (BC250_BACKING_HANDLE *)session.Backend.Root, BC250_VM_SESSION_INVALID);
    {
        unsigned char saved_out[sizeof(out)];
        memcpy(saved_out, &out, sizeof(out));
        CHECK(Bc250VmDomainRegister(NULL, pages, 1, 1, &out) == BC250_VM_SESSION_INVALID);
        CHECK(memcmp(saved_out, &out, sizeof(out)) == 0);
    }
    memcpy(malformed, pages, sizeof(pages)); malformed[63].Domain = BC250_AD_FB_PHYSICAL;
    RejectRegister(&heap, &session, malformed, 64, 64, &out, BC250_VM_SESSION_INVALID);
    CHECK(memcmp(pages, saved, sizeof(pages)) == 0);
    RejectRegister(&heap, &session, pages, 64, 63, &out, BC250_VM_SESSION_INVALID);
    CHECK(Bc250VmDomainRegister(&session, pages, 64, 65, &backing) == BC250_VM_SESSION_OK);
    CHECK(session.Backing.Records[backing.Slot].PageCount == 64);
    for (i = 0; i < 64; ++i)
        CHECK(session.Backing.Records[backing.Slot].Pages[i].Start == pages[i].Start &&
            session.Backing.Records[backing.Slot].Pages[i].Domain == BC250_ADDRESS_DMA_LOGICAL);
    CHECK(Bc250VmSessionRelease(&session, &backing) == BC250_VM_SESSION_OK);
    CHECK(Bc250VmDomainRegister(&session, pages, 2, 2, &backing) == BC250_VM_SESSION_OK);
    CHECK(session.Backing.Records[backing.Slot].PageCount == 2);
    for (i = 0; i < 2; ++i) {
        const BC250_ADDRESS_SPAN *old = &session.Backing.Records[backing.Slot].Pages[i];
        CHECK(old->Domain == BC250_ADDRESS_DMA_LOGICAL && old->Domain == 3);
        CHECK(old->Start == pages[i].Start && old->Bytes == 4096 && old->Last == pages[i].Last);
    }
    CHECK(memcmp(pages, saved, sizeof(pages)) == 0);
    CHECK(Bc250VmSessionRelease(&session, &backing) == BC250_VM_SESSION_OK);
    EndSession(&heap, &session);
}
static void TestTypedMap(void)
{
    SESSION_HEAP heap, other_heap;
    BC250_VM_CPU_SESSION session, other;
    BC250_AD_SPAN pages[2], va, bad;
    BC250_BACKING_HANDLE backing, stale;
    BC250_VM_SESSION_MAPPING_HANDLE first, alias, out;
    BC250_VM_CPU_JOURNAL journal;
    BC250_ADDRESS_SPAN expected_page;
    BC250_VM_CPU_NODE *leaf;
    union { BC250_AD_SPAN Va; BC250_VM_CPU_JOURNAL Journal;
        BC250_BACKING_HANDLE Handle; BC250_VM_SESSION_MAPPING_HANDLE Output; } overlap;
    unsigned int tag;
    StartSession(&heap, &session); StartSession(&other_heap, &other);
    DomainSpan(&pages[0], 5, 0x61000, 4096); pages[1] = pages[0];
    CHECK(Bc250VmDomainRegister(&session, pages, 2, 2, &backing) == BC250_VM_SESSION_OK);
    DomainSpan(&va, 6, (1ULL << 21) - 4096, 8192); memset(&out, 0xA5, sizeof(out));
    memset(&journal, 0xA5, sizeof(journal));
    for (tag = 0; tag <= 8; ++tag) {
        if (tag == BC250_AD_GPU_VIRTUAL) continue;
        bad = va; bad.Domain = tag;
        RejectMap(&heap, &session, &bad, &backing, 15, &out, &journal, BC250_VM_SESSION_INVALID);
    }
    bad = va; bad.Domain = 0xFFFFFFFFU; RejectMap(&heap, &session, &bad, &backing, 3, &out, &journal, BC250_VM_SESSION_INVALID);
    bad = va; ++bad.Last; RejectMap(&heap, &session, &bad, &backing, 3, &out, &journal, BC250_VM_SESSION_INVALID);
    DomainSpan(&bad, 6, va.Start, 4096); RejectMap(&heap, &session, &bad, &backing, 3, &out, &journal, BC250_VM_SESSION_INVALID);
    DomainSpan(&bad, 6, va.Start + 1, 8192); RejectMap(&heap, &session, &bad, &backing, 3, &out, &journal, BC250_VM_SESSION_INVALID);
    DomainSpan(&bad, 6, (1ULL << 48) - 4096, 8192); RejectMap(&heap, &session, &bad, &backing, 3, &out, &journal, BC250_VM_SESSION_INVALID);
    DomainSpan(&bad, 6, ~0ULL - 4095, 8192); RejectMap(&heap, &session, &bad, &backing, 3, &out, &journal, BC250_VM_SESSION_INVALID);
    RejectMap(&heap, &session, &va, &backing, 16, &out, &journal, BC250_VM_SESSION_INVALID);
    RejectMap(&heap, &session, NULL, &backing, 3, &out, &journal, BC250_VM_SESSION_INVALID);
    RejectMap(&heap, &session, &va, NULL, 3, &out, &journal, BC250_VM_SESSION_INVALID);
    RejectMap(&heap, &session, &va, &backing, 3, NULL, &journal, BC250_VM_SESSION_INVALID);
    RejectMap(&heap, &session, &va, &backing, 3, &out, NULL, BC250_VM_SESSION_INVALID);
    {
        unsigned char saved_out[sizeof(out)], saved_journal[sizeof(journal)];
        memcpy(saved_out, &out, sizeof(out)); memcpy(saved_journal, &journal, sizeof(journal));
        CHECK(Bc250VmDomainMap(NULL, &va, &backing, 3, &out, &journal, NULL, NULL) == BC250_VM_SESSION_INVALID);
        CHECK(memcmp(saved_out, &out, sizeof(out)) == 0 && memcmp(saved_journal, &journal, sizeof(journal)) == 0);
    }
    stale = backing; ++stale.Generation;
    RejectMap(&heap, &session, &va, &stale, 3, &out, &journal, BC250_VM_SESSION_STALE);
    RejectMap(&other_heap, &other, &va, &backing, 3, &out, &journal, BC250_VM_SESSION_STALE);
    RejectMap(&heap, &session, (BC250_AD_SPAN *)&session, &backing, 3, &out, &journal, BC250_VM_SESSION_INVALID);
    RejectMap(&heap, &session, (BC250_AD_SPAN *)session.Backend.Root, &backing, 3, &out, &journal, BC250_VM_SESSION_INVALID);
    RejectMap(&heap, &session, &va, (BC250_BACKING_HANDLE *)&session, 3, &out, &journal, BC250_VM_SESSION_INVALID);
    RejectMap(&heap, &session, &va, &backing, 3, (BC250_VM_SESSION_MAPPING_HANDLE *)&session, &journal, BC250_VM_SESSION_INVALID);
    RejectMap(&heap, &session, &va, &backing, 3, &out, (BC250_VM_CPU_JOURNAL *)session.Backend.Root, BC250_VM_SESSION_INVALID);
    RejectMap(&heap, &session, &va, &backing, 3, (BC250_VM_SESSION_MAPPING_HANDLE *)&va, &journal, BC250_VM_SESSION_INVALID);
    memset(&overlap, 0xA5, sizeof(overlap)); memcpy(&overlap.Va, &va, sizeof(va));
    RejectMap(&heap, &session, &overlap.Va, &backing, 3, &out, &overlap.Journal, BC250_VM_SESSION_INVALID);
    RejectMap(&heap, &session, &overlap.Va, &overlap.Handle, 3, &out, &journal, BC250_VM_SESSION_INVALID);
    memset(&overlap, 0xA5, sizeof(overlap)); memcpy(&overlap.Handle, &backing, sizeof(backing));
    RejectMap(&heap, &session, &va, &overlap.Handle, 3, &overlap.Output, &journal, BC250_VM_SESSION_INVALID);
    memset(&overlap, 0xA5, sizeof(overlap));
    RejectMap(&heap, &session, &va, &backing, 3, &overlap.Output, &overlap.Journal, BC250_VM_SESSION_INVALID);
    CHECK(Bc250VmDomainMap(&session, &va, &backing, 15, &first, &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    Page(&expected_page, pages[0].Start); CheckValue(&session.Backend, va.Start, Expected(&expected_page, 15));
    CheckValue(&session.Backend, va.Start + 4096, Expected(&expected_page, 15));
    DomainSpan(&bad, 6, va.Start + 4096, 8192);
    RejectMap(&heap, &session, &bad, &backing, 15, &out, &journal, BC250_VM_SESSION_IN_USE);
    DomainSpan(&bad, 6, 1ULL << 39, 8192);
    CHECK(Bc250VmDomainMap(&session, &bad, &backing, 15, &alias, &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    CHECK(session.Backing.Records[backing.Slot].References == 2 && first.Id != alias.Id);
    CHECK(Bc250VmSessionRelease(&session, &backing) == BC250_VM_SESSION_IN_USE);
    leaf = IndependentLeaf(&session.Backend, va.Start);
    leaf->Data.Values[99] = Expected(&expected_page, 15);
    RejectMap(&heap, &session, &bad, &backing, 15, &out, &journal, BC250_VM_SESSION_CORRUPT);
    leaf->Data.Values[99] = 0;
    CHECK(Bc250VmSessionUnmap(&session, &first, &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    CHECK(Bc250VmSessionUnmap(&session, &first, &journal, NULL, NULL) == BC250_VM_SESSION_STALE);
    CHECK(session.Backing.Records[backing.Slot].References == 1);
    CheckValue(&session.Backend, bad.Start, Expected(&expected_page, 15));
    CHECK(Bc250VmSessionUnmap(&session, &alias, &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    CHECK(Bc250VmSessionRelease(&session, &backing) == BC250_VM_SESSION_OK);
    RejectMap(&heap, &session, &va, &backing, 3, &out, &journal, BC250_VM_SESSION_STALE);
    EndSession(&heap, &session); EndSession(&other_heap, &other);
}
static int DomainAfterWrite(void *opaque, unsigned int count)
{
    SESSION_HEAP *heap = opaque;
    BC250_BACKING_HANDLE backing_out;
    BC250_VM_SESSION_MAPPING_HANDLE mapping_out;
    unsigned char saved_backing[sizeof(backing_out)], saved_mapping[sizeof(mapping_out)];
    memset(&backing_out, 0xA5, sizeof(backing_out)); memcpy(saved_backing, &backing_out, sizeof(backing_out));
    memset(&mapping_out, 0xA5, sizeof(mapping_out)); memcpy(saved_mapping, &mapping_out, sizeof(mapping_out));
    CHECK(Bc250VmDomainRegister(heap->Session, NULL, 0, 0, &backing_out) == BC250_VM_SESSION_BUSY_RESULT);
    CHECK(Bc250VmDomainMap(heap->Session, NULL, NULL, 0, &mapping_out, NULL, NULL, NULL) == BC250_VM_SESSION_BUSY_RESULT);
    CHECK(memcmp(saved_backing, &backing_out, sizeof(backing_out)) == 0 && memcmp(saved_mapping, &mapping_out, sizeof(mapping_out)) == 0);
    return SessionAfterWrite(opaque, count);
}
static void TestFaultsAndLimits(void)
{
    SESSION_HEAP heap;
    BC250_VM_CPU_SESSION session;
    BC250_AD_SPAN pages[2], va;
    BC250_BACKING_HANDLE backing, registrations[8], out_backing;
    BC250_VM_SESSION_MAPPING_HANDLE mapping, out, aliases[16];
    BC250_VM_CPU_JOURNAL journal;
    DOMAIN_SNAPSHOT before;
    unsigned char saved[sizeof(out)];
    unsigned int failure, i;
    StartSession(&heap, &session); DomainSpan(&pages[0], 5, 0x51000, 4096); DomainSpan(&pages[1], 5, 0x61000, 4096);
    CHECK(Bc250VmDomainRegister(&session, pages, 2, 2, &backing) == BC250_VM_SESSION_OK);
    DomainSpan(&va, 6, (1ULL << 39) - 4096, 8192);
    memset(&out, 0xA5, sizeof(out)); memcpy(saved, &out, sizeof(out));
    for (failure = 1; failure <= 2; ++failure) {
        Snapshot(&heap, &session, &before);
        heap.Base.FailWrite = failure; heap.Base.HookCalls = 0; heap.Base.Reentry = 1;
        CHECK(Bc250VmDomainMap(&session, &va, &backing, 15, &out, &journal, DomainAfterWrite, &heap) == BC250_VM_SESSION_FAULT);
        CHECK(memcmp(saved, &out, sizeof(out)) == 0 && Digest(&session.Backend) == before.Digest && heap.Base.Live == before.Live);
        CHECK(heap.Base.HookCalls == failure && session.Backing.Records[backing.Slot].References == 0);
        SameExceptLeaseCounter(&session, &before.Session);
        CHECK(memcmp(&journal, &empty_journal, sizeof(journal)) == 0);
    }
    heap.Base.FailWrite = 0; heap.Base.HookCalls = 0;
    CHECK(Bc250VmDomainMap(&session, &va, &backing, 15, &mapping, &journal, DomainAfterWrite, &heap) == BC250_VM_SESSION_OK);
    CHECK(heap.Base.HookCalls == 2 && session.Backing.Records[backing.Slot].References == 1);
    CHECK(Bc250VmSessionUnmap(&session, &mapping, &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    CHECK(Bc250VmSessionRelease(&session, &backing) == BC250_VM_SESSION_OK);
    EndSession(&heap, &session);
    StartSession(&heap, &session);
    CHECK(Bc250VmDomainRegister(&session, pages, 2, 2, &backing) == BC250_VM_SESSION_OK);
    Snapshot(&heap, &session, &before); heap.Base.FailAllocation = 1;
    CHECK(Bc250VmDomainMap(&session, &va, &backing, 3, &out, &journal, NULL, NULL) == BC250_VM_SESSION_NO_MEMORY);
    CHECK(memcmp(saved, &out, sizeof(out)) == 0 && Digest(&session.Backend) == before.Digest && heap.Base.Live == 1);
    SameExceptLeaseCounter(&session, &before.Session);
    CHECK(session.Backing.Records[backing.Slot].References == 0);
    CHECK(Bc250VmSessionRelease(&session, &backing) == BC250_VM_SESSION_OK); EndSession(&heap, &session);
    StartSession(&heap, &session);
    for (i = 0; i < 8; ++i) CHECK(Bc250VmDomainRegister(&session, pages, 1, 1, &registrations[i]) == BC250_VM_SESSION_OK);
    memset(&out_backing, 0xA5, sizeof(out_backing));
    RejectRegister(&heap, &session, pages, 1, 1, &out_backing, BC250_VM_SESSION_FULL);
    for (i = 0; i < 16; ++i) {
        DomainSpan(&va, 6, (BC250_AD_U64)i * 4096, 4096);
        CHECK(Bc250VmDomainMap(&session, &va, &registrations[0], 3, &aliases[i], &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    }
    DomainSpan(&va, 6, 16ULL * 4096, 4096);
    RejectMap(&heap, &session, &va, &registrations[0], 3, &out, &journal, BC250_VM_SESSION_FULL);
    CHECK(session.Backing.Records[registrations[0].Slot].References == 16);
    for (i = 0; i < 16; ++i) CHECK(Bc250VmSessionUnmap(&session, &aliases[i], &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    for (i = 0; i < 8; ++i) CHECK(Bc250VmSessionRelease(&session, &registrations[i]) == BC250_VM_SESSION_OK);
    EndSession(&heap, &session);
}
int main(void)
{
    unsigned int baseline;
    CHECK(Bc250PreviousCpuBackendTests() == 0); baseline = assertions;
    TestPreviewAndTags(); TestRegisterAdmission(); TestTypedMap(); TestFaultsAndLimits();
    printf("PASS: %u additional typed-domain CPU assertions (%u total); tags are declarations, NOT OS/GPU ownership\n", assertions - baseline, assertions);
    return 0;
}
