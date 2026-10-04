/* SPDX-License-Identifier: Apache-2.0
 * Original simulated provider + CPU session fixtures. No Windows/device APIs.
 * Direct internal inspection/corruption below is ONLY test instrumentation.
 */
#define main Bc250PreviousCpuBackendTests
#include "../vm-cpu-backend-20261003/test-vm-cpu-backend.c"
#undef main
#include "dma-owner-20261003/bc250_dma_owner.h"

typedef struct OWNER_RESOURCE { unsigned int Live, Puts, Direction; } OWNER_RESOURCE;
typedef struct OWNER_PROVIDER {
    BC250_DMA_OWNER *Owner;
    OWNER_RESOURCE Resources[80];
    unsigned int Issued, Puts, Reentries;
} OWNER_PROVIDER;
typedef struct OWNER_FIXTURE {
    TEST_HEAP Heap;
    BC250_VM_CPU_SESSION Session;
    BC250_DMA_OWNER Owner;
    OWNER_PROVIDER Provider;
} OWNER_FIXTURE;
static void OwnerSpan(BC250_AD_SPAN *span, unsigned int tag, BC250_AD_U64 start, BC250_AD_U64 bytes)
{
    memset(span, 0, sizeof(*span)); span->Domain = tag;
    span->Start = start; span->Bytes = bytes; span->Last = start + bytes - 1ULL;
}
static void OwnerBusyChecks(OWNER_PROVIDER *provider)
{
    BC250_DMA_OWNER_TICKET ticket = {0}, output;
    BC250_VM_SESSION_MAPPING_HANDLE mapping = {0}, mapping_output;
    BC250_VM_CPU_JOURNAL journal;
    BC250_DMA_OWNER_DELIVERY_RESULT result;
    unsigned char saved_ticket[sizeof(output)], saved_mapping[sizeof(mapping_output)];
    memset(&output, 0xA5, sizeof(output)); memcpy(saved_ticket, &output, sizeof(output));
    memset(&mapping_output, 0xA5, sizeof(mapping_output)); memcpy(saved_mapping, &mapping_output, sizeof(mapping_output));
    CHECK(Bc250DmaOwnerInit(provider->Owner, NULL, NULL, NULL) == BC250_VM_SESSION_BUSY_RESULT);
    CHECK(Bc250DmaOwnerBegin(provider->Owner, 4096, 0, &output) == BC250_VM_SESSION_BUSY_RESULT);
    CHECK(Bc250DmaOwnerCancel(provider->Owner, &ticket) == BC250_VM_SESSION_BUSY_RESULT);
    CHECK(Bc250DmaOwnerResolveNoResource(provider->Owner, &ticket) == BC250_VM_SESSION_BUSY_RESULT);
    result = Bc250DmaOwnerDeliver(provider->Owner, &ticket, NULL, NULL, 0, 0);
    CHECK(result.Status == BC250_VM_SESSION_BUSY_RESULT && !result.Consumed);
    CHECK(Bc250DmaOwnerMap(provider->Owner, &ticket, NULL, 0, &mapping_output, &journal, NULL, NULL) == BC250_VM_SESSION_BUSY_RESULT);
    CHECK(Bc250DmaOwnerUnmap(provider->Owner, &ticket, &mapping, &journal, NULL, NULL) == BC250_VM_SESSION_BUSY_RESULT);
    CHECK(Bc250DmaOwnerRelease(provider->Owner, &ticket) == BC250_VM_SESSION_BUSY_RESULT);
    CHECK(Bc250DmaOwnerShutdown(provider->Owner) == BC250_VM_SESSION_BUSY_RESULT);
    CHECK(memcmp(saved_ticket, &output, sizeof(output)) == 0 &&
        memcmp(saved_mapping, &mapping_output, sizeof(mapping_output)) == 0);
    ++provider->Reentries;
}
static void ProviderPut(void *opaque, void *cookie, unsigned int direction)
{
    OWNER_PROVIDER *provider = opaque;
    OWNER_RESOURCE *resource = cookie;
    unsigned int i, found = 0;
    for (i = 0; i < provider->Issued; ++i) if (resource == &provider->Resources[i]) found = 1;
    CHECK(found && resource->Live == 1 && !resource->Puts && resource->Direction == direction);
    OwnerBusyChecks(provider);
    resource->Live = 0; ++resource->Puts; ++provider->Puts;
}
static OWNER_RESOURCE *ProviderIssue(OWNER_PROVIDER *provider, unsigned int direction)
{
    OWNER_RESOURCE *resource;
    CHECK(provider->Issued < 80); resource = &provider->Resources[provider->Issued++];
    resource->Live = 1; resource->Direction = direction;
    return resource;
}
static int OwnerHook(void *opaque, unsigned int count)
{
    OWNER_FIXTURE *fixture = opaque;
    OwnerBusyChecks(&fixture->Provider);
    return AfterWrite(&fixture->Heap, count);
}
static void OwnerStart(OWNER_FIXTURE *fixture)
{
    BC250_VM_LAYOUT layout;
    memset(fixture, 0, sizeof(*fixture)); fixture->Heap.Context = &fixture->Session.Backend;
    CHECK(Bc250VmPlanLayout(1, 9, 1ULL << 36, &layout));
    CHECK(Bc250VmSessionInit(&fixture->Session, &layout, BC250_VM_MAX_ADDRESS, 256,
        Allocate, Release, &fixture->Heap) == BC250_VM_SESSION_OK);
    fixture->Heap.Attempts = 0; fixture->Provider.Owner = &fixture->Owner;
    CHECK(Bc250DmaOwnerInit(&fixture->Owner, &fixture->Session, ProviderPut, &fixture->Provider) == BC250_VM_SESSION_OK);
    CHECK(Bc250DmaOwnerCheck(&fixture->Owner) == BC250_VM_SESSION_OK);
}
static void OwnerEnd(OWNER_FIXTURE *fixture)
{
    unsigned int i;
    CHECK(fixture->Owner.State == BC250_DMA_OWNER_IDLE && fixture->Provider.Issued == fixture->Provider.Puts);
    for (i = 0; i < fixture->Provider.Issued; ++i)
        CHECK(!fixture->Provider.Resources[i].Live && fixture->Provider.Resources[i].Puts == 1);
    CHECK(Bc250DmaOwnerShutdown(&fixture->Owner) == BC250_VM_SESSION_OK);
    CHECK(Bc250DmaOwnerShutdown(&fixture->Owner) == BC250_VM_SESSION_DEAD_RESULT);
    CHECK(Bc250DmaOwnerInit(&fixture->Owner, &fixture->Session, ProviderPut, &fixture->Provider) == BC250_VM_SESSION_DEAD_RESULT);
    CHECK(Bc250VmSessionShutdown(&fixture->Session) == BC250_VM_SESSION_OK);
    FinishHeap(&fixture->Heap);
}
static BC250_DMA_OWNER_TICKET OwnerBegin(OWNER_FIXTURE *fixture, unsigned int direction)
{
    BC250_DMA_OWNER_TICKET ticket;
    CHECK(Bc250DmaOwnerBegin(&fixture->Owner, 16384, direction, &ticket) == BC250_VM_SESSION_OK);
    CHECK(ticket.Owner == &fixture->Owner && ticket.Id == fixture->Owner.LastId);
    return ticket;
}
static void OwnerDeliver(BC250_DMA_OWNER *owner, const BC250_DMA_OWNER_TICKET *ticket,
    void *cookie, const BC250_AD_SPAN *runs, unsigned int count, unsigned int capacity,
    BC250_VM_SESSION_STATUS expected, unsigned int consumed)
{
    BC250_DMA_OWNER_DELIVERY_RESULT result = Bc250DmaOwnerDeliver(owner, ticket, cookie, runs, count, capacity);
    CHECK(result.Status == expected && result.Consumed == consumed);
}
static void OwnerRuns(BC250_AD_SPAN runs[2])
{
    OwnerSpan(&runs[0], BC250_AD_DMA_LOGICAL, 0x90000, 8192);
    OwnerSpan(&runs[1], BC250_AD_DMA_LOGICAL, 0x31000, 8192);
}
static void TestPendingResolution(void)
{
    OWNER_FIXTURE fixture;
    BC250_DMA_OWNER_TICKET ticket, stale, second, out;
    BC250_AD_SPAN runs[2];
    OWNER_RESOURCE *resource;
    unsigned int direction, scenario;
    OwnerRuns(runs);
    for (direction = 0; direction <= 1; ++direction) for (scenario = 0; scenario < 4; ++scenario) {
        OwnerStart(&fixture); ticket = OwnerBegin(&fixture, direction); stale = ticket;
        CHECK(Bc250DmaOwnerShutdown(&fixture.Owner) == BC250_VM_SESSION_IN_USE);
        CHECK(Bc250DmaOwnerRelease(&fixture.Owner, &ticket) == BC250_VM_SESSION_IN_USE);
        CHECK(Bc250DmaOwnerBegin(&fixture.Owner, 4096, direction, &out) == BC250_VM_SESSION_IN_USE);
        if (scenario & 1) {
            CHECK(Bc250DmaOwnerCancel(&fixture.Owner, &ticket) == BC250_VM_SESSION_OK);
            CHECK(Bc250DmaOwnerCancel(&fixture.Owner, &ticket) == BC250_VM_SESSION_OK);
            CHECK(fixture.Owner.State == BC250_DMA_OWNER_CANCEL_WAIT && !fixture.Provider.Puts);
        }
        if (scenario < 2) {
            CHECK(Bc250DmaOwnerResolveNoResource(&fixture.Owner, &ticket) == BC250_VM_SESSION_OK);
            CHECK(!fixture.Provider.Puts);
        } else {
            resource = ProviderIssue(&fixture.Provider, direction);
            OwnerDeliver(&fixture.Owner, &ticket, NULL, runs, 2, 2, BC250_VM_SESSION_INVALID, 0);
            CHECK(resource->Live && fixture.Owner.State != BC250_DMA_OWNER_IDLE);
            OwnerDeliver(&fixture.Owner, &ticket, resource, runs, 2, 2, BC250_VM_SESSION_OK, 1);
            if (scenario == 2) {
                CHECK(fixture.Owner.State == BC250_DMA_OWNER_READY && resource->Live && !fixture.Provider.Puts);
                CHECK(Bc250DmaOwnerResolveNoResource(&fixture.Owner, &ticket) == BC250_VM_SESSION_IN_USE);
                /* Duplicate callback must NOT release the resource held by owner. */
                OwnerDeliver(&fixture.Owner, &ticket, resource, runs, 2, 2, BC250_VM_SESSION_STALE, 0);
                CHECK(resource->Live && !fixture.Provider.Puts);
                CHECK(Bc250DmaOwnerCancel(&fixture.Owner, &ticket) == BC250_VM_SESSION_OK);
                CHECK(fixture.Owner.State == BC250_DMA_OWNER_DRAINING && resource->Live);
                CHECK(Bc250DmaOwnerRelease(&fixture.Owner, &ticket) == BC250_VM_SESSION_OK);
            } else CHECK(fixture.Owner.State == BC250_DMA_OWNER_IDLE && !resource->Live && fixture.Provider.Puts == 1);
            OwnerDeliver(&fixture.Owner, &ticket, resource, runs, 2, 2, BC250_VM_SESSION_STALE, 0);
            CHECK(Bc250DmaOwnerRelease(&fixture.Owner, &ticket) == BC250_VM_SESSION_STALE);
        }
        second = OwnerBegin(&fixture, direction); CHECK(second.Id == stale.Id + 1);
        resource = ProviderIssue(&fixture.Provider, direction);
        OwnerDeliver(&fixture.Owner, &stale, resource, runs, 2, 2, BC250_VM_SESSION_STALE, 0);
        CHECK(resource->Live); /* rejected delivery stays with provider */
        CHECK(Bc250DmaOwnerCancel(&fixture.Owner, &second) == BC250_VM_SESSION_OK);
        OwnerDeliver(&fixture.Owner, &second, resource, NULL, 0, 0, BC250_VM_SESSION_OK, 1);
        OwnerEnd(&fixture);
    }
}
static void TestInvalidDeliveredRuns(void)
{
    OWNER_FIXTURE fixture;
    BC250_DMA_OWNER_TICKET ticket;
    BC250_AD_SPAN runs[2], bad[2];
    OWNER_RESOURCE *resource;
    BC250_GART_U64 digest;
    unsigned int scenario;
    OwnerStart(&fixture); OwnerRuns(runs);
    for (scenario = 0; scenario < 10; ++scenario) {
        ticket = OwnerBegin(&fixture, scenario & 1); resource = ProviderIssue(&fixture.Provider, scenario & 1);
        digest = Digest(&fixture.Session.Backend); memcpy(bad, runs, sizeof(bad));
        if (scenario == 0) bad[1].Domain = BC250_AD_FB_PHYSICAL;
        if (scenario == 1) ++bad[1].Last;
        if (scenario == 2) OwnerSpan(&bad[1], 5, 0x31001, 8192);
        if (scenario == 3) OwnerSpan(&bad[1], 5, 0x31000, 4096);
        if (scenario == 4) OwnerSpan(&bad[1], 5, ~0ULL - 4095, 8192);
        OwnerDeliver(&fixture.Owner, &ticket, resource,
            scenario == 5 ? NULL : scenario == 8 ? (BC250_AD_SPAN *)&fixture.Owner :
            scenario == 9 ? (BC250_AD_SPAN *)fixture.Session.Backend.Root : bad,
            scenario == 6 ? 65 : 2, scenario == 7 ? 1 : 65, BC250_VM_SESSION_INVALID, 1);
        CHECK(!resource->Live && fixture.Owner.State == BC250_DMA_OWNER_IDLE &&
            Digest(&fixture.Session.Backend) == digest && fixture.Provider.Puts == scenario + 1);
        CHECK(Bc250DmaOwnerCheck(&fixture.Owner) == BC250_VM_SESSION_OK);
    }
    OwnerEnd(&fixture);
}
static void TestMappedCancellation(void)
{
    OWNER_FIXTURE fixture;
    BC250_DMA_OWNER_TICKET ticket;
    BC250_AD_SPAN runs[2], va, other_va;
    OWNER_RESOURCE *resource;
    BC250_VM_SESSION_MAPPING_HANDLE first, alias, out;
    BC250_VM_CPU_JOURNAL journal;
    BC250_GART_U64 digest;
    unsigned char saved[sizeof(out)];
    unsigned int failed_write;
    OwnerStart(&fixture); OwnerRuns(runs); ticket = OwnerBegin(&fixture, 1);
    resource = ProviderIssue(&fixture.Provider, 1);
    OwnerDeliver(&fixture.Owner, &ticket, resource, runs, 2, 2, BC250_VM_SESSION_OK, 1);
    OwnerSpan(&va, 6, (1ULL << 21) - 4096, 16384); OwnerSpan(&other_va, 6, 1ULL << 39, 16384);
    memset(&out, 0xA5, sizeof(out)); memcpy(saved, &out, sizeof(out));
    digest = Digest(&fixture.Session.Backend); fixture.Heap.FailAllocation = 1;
    CHECK(Bc250DmaOwnerMap(&fixture.Owner, &ticket, &va, 15, &out, &journal, NULL, NULL) == BC250_VM_SESSION_NO_MEMORY);
    CHECK(memcmp(saved, &out, sizeof(out)) == 0 && Digest(&fixture.Session.Backend) == digest &&
        resource->Live && !fixture.Provider.Puts && !fixture.Owner.Busy &&
        !fixture.Session.Backing.Records[fixture.Owner.Backing.Slot].References);
    fixture.Heap.FailAllocation = 0;
    for (failed_write = 1; failed_write <= 4; ++failed_write) {
        digest = Digest(&fixture.Session.Backend); fixture.Heap.FailWrite = failed_write; fixture.Heap.HookCalls = 0;
        CHECK(Bc250DmaOwnerMap(&fixture.Owner, &ticket, &va, 15, &out, &journal, OwnerHook, &fixture) == BC250_VM_SESSION_FAULT);
        CHECK(memcmp(saved, &out, sizeof(out)) == 0 && Digest(&fixture.Session.Backend) == digest);
        CHECK(resource->Live && !fixture.Provider.Puts && !fixture.Owner.Busy &&
            !fixture.Session.Backing.Records[fixture.Owner.Backing.Slot].References);
    }
    fixture.Heap.FailWrite = 0; fixture.Heap.HookCalls = 0;
    CHECK(Bc250DmaOwnerMap(&fixture.Owner, &ticket, &va, 15, &first, &journal, OwnerHook, &fixture) == BC250_VM_SESSION_OK);
    CHECK(Bc250DmaOwnerMap(&fixture.Owner, &ticket, &other_va, 3, &alias, &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    CHECK(Bc250DmaOwnerRelease(&fixture.Owner, &ticket) == BC250_VM_SESSION_IN_USE);
    CHECK(Bc250DmaOwnerCancel(&fixture.Owner, &ticket) == BC250_VM_SESSION_OK);
    CHECK(fixture.Owner.State == BC250_DMA_OWNER_DRAINING);
    CHECK(Bc250DmaOwnerMap(&fixture.Owner, &ticket, &va, 15, &out, &journal, NULL, NULL) == BC250_VM_SESSION_IN_USE);
    for (failed_write = 1; failed_write <= 4; ++failed_write) {
        digest = Digest(&fixture.Session.Backend); fixture.Heap.FailWrite = failed_write; fixture.Heap.HookCalls = 0;
        CHECK(Bc250DmaOwnerUnmap(&fixture.Owner, &ticket, &first, &journal, OwnerHook, &fixture) == BC250_VM_SESSION_FAULT);
        CHECK(Digest(&fixture.Session.Backend) == digest && resource->Live && !fixture.Provider.Puts);
        CHECK(fixture.Session.Backing.Records[fixture.Owner.Backing.Slot].References == 2);
    }
    fixture.Heap.FailWrite = 0; fixture.Heap.HookCalls = 0;
    CHECK(Bc250DmaOwnerUnmap(&fixture.Owner, &ticket, &first, &journal, OwnerHook, &fixture) == BC250_VM_SESSION_OK);
    CHECK(Bc250DmaOwnerRelease(&fixture.Owner, &ticket) == BC250_VM_SESSION_IN_USE && resource->Live);
    CHECK(Bc250DmaOwnerUnmap(&fixture.Owner, &ticket, &first, &journal, NULL, NULL) == BC250_VM_SESSION_STALE);
    CHECK(Bc250DmaOwnerUnmap(&fixture.Owner, &ticket, &alias, &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    CHECK(Bc250DmaOwnerRelease(&fixture.Owner, &ticket) == BC250_VM_SESSION_OK);
    CHECK(!resource->Live && fixture.Provider.Puts == 1 && fixture.Provider.Reentries >= 1);
    OwnerEnd(&fixture);
}
static void TestSharedSessionAndCapacity(void)
{
    OWNER_FIXTURE fixture;
    BC250_DMA_OWNER other = {0};
    OWNER_PROVIDER provider = {0};
    BC250_DMA_OWNER_TICKET first, second, wrong;
    BC250_AD_SPAN runs[2], va;
    BC250_BACKING_HANDLE occupied[6];
    BC250_VM_SESSION_MAPPING_HANDLE mapping;
    BC250_VM_CPU_JOURNAL journal;
    OWNER_RESOURCE *a, *b;
    unsigned int i;
    OwnerStart(&fixture); OwnerRuns(runs); provider.Owner = &other;
    CHECK(Bc250DmaOwnerInit(&other, &fixture.Session, ProviderPut, &provider) == BC250_VM_SESSION_OK);
    first = OwnerBegin(&fixture, 0);
    CHECK(Bc250DmaOwnerBegin(&other, 16384, 1, &second) == BC250_VM_SESSION_OK);
    a = ProviderIssue(&fixture.Provider, 0); b = ProviderIssue(&provider, 1);
    wrong = first; wrong.Owner = &other;
    OwnerDeliver(&fixture.Owner, &wrong, a, runs, 2, 2, BC250_VM_SESSION_STALE, 0);
    CHECK(a->Live);
    OwnerDeliver(&fixture.Owner, &first, a, runs, 2, 2, BC250_VM_SESSION_OK, 1);
    OwnerDeliver(&other, &second, b, runs, 2, 2, BC250_VM_SESSION_OK, 1);
    OwnerSpan(&va, 6, 0, 16384);
    CHECK(Bc250DmaOwnerMap(&other, &second, &va, 15, &mapping, &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    CHECK(Bc250DmaOwnerUnmap(&fixture.Owner, &first, &mapping, &journal, NULL, NULL) == BC250_VM_SESSION_STALE);
    CHECK(b->Live && !provider.Puts);
    for (i = 0; i < 6; ++i) CHECK(Bc250VmDomainRegisterRuns(&fixture.Session, runs, 2, 2, 16384, &occupied[i]) == BC250_VM_SESSION_OK);
    CHECK(Bc250DmaOwnerRelease(&fixture.Owner, &first) == BC250_VM_SESSION_OK);
    first = OwnerBegin(&fixture, 0); a = ProviderIssue(&fixture.Provider, 0);
    OwnerDeliver(&fixture.Owner, &first, a, runs, 2, 2, BC250_VM_SESSION_OK, 1);
    {
        BC250_DMA_OWNER third = {0}; OWNER_PROVIDER third_provider = {0}; BC250_DMA_OWNER_TICKET ticket;
        OWNER_RESOURCE *resource; third_provider.Owner = &third;
        CHECK(Bc250DmaOwnerInit(&third, &fixture.Session, ProviderPut, &third_provider) == BC250_VM_SESSION_OK);
        CHECK(Bc250DmaOwnerBegin(&third, 16384, 0, &ticket) == BC250_VM_SESSION_OK);
        resource = ProviderIssue(&third_provider, 0);
        OwnerDeliver(&third, &ticket, resource, runs, 2, 2, BC250_VM_SESSION_FULL, 1);
        CHECK(!resource->Live && third_provider.Puts == 1);
        CHECK(Bc250DmaOwnerShutdown(&third) == BC250_VM_SESSION_OK);
    }
    CHECK(Bc250DmaOwnerRelease(&fixture.Owner, &first) == BC250_VM_SESSION_OK);
    CHECK(Bc250DmaOwnerUnmap(&other, &second, &mapping, &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    CHECK(Bc250DmaOwnerRelease(&other, &second) == BC250_VM_SESSION_OK && provider.Puts == 1);
    CHECK(Bc250DmaOwnerShutdown(&other) == BC250_VM_SESSION_OK);
    for (i = 0; i < 6; ++i) CHECK(Bc250VmSessionRelease(&fixture.Session, &occupied[i]) == BC250_VM_SESSION_OK);
    OwnerEnd(&fixture);
}
static void TestBeginGuards(void)
{
    OWNER_FIXTURE fixture;
    BC250_DMA_OWNER before, zero = {0};
    BC250_DMA_OWNER_TICKET out;
    unsigned char saved[sizeof(out)];
    BC250_AD_U64 invalid_bytes[] = {0, 1, 4095, 4097, 65ULL * 4096, ~0ULL};
    unsigned int i;
    OwnerStart(&fixture); memset(&out, 0xA5, sizeof(out)); memcpy(saved, &out, sizeof(out)); before = fixture.Owner;
    for (i = 0; i < sizeof(invalid_bytes) / sizeof(invalid_bytes[0]); ++i)
        CHECK(Bc250DmaOwnerBegin(&fixture.Owner, invalid_bytes[i], 0, &out) == BC250_VM_SESSION_INVALID);
    CHECK(Bc250DmaOwnerBegin(&fixture.Owner, 4096, 2, &out) == BC250_VM_SESSION_INVALID);
    CHECK(Bc250DmaOwnerBegin(&fixture.Owner, 4096, 0, NULL) == BC250_VM_SESSION_INVALID);
    CHECK(Bc250DmaOwnerBegin(&fixture.Owner, 4096, 0, (BC250_DMA_OWNER_TICKET *)&fixture.Owner) == BC250_VM_SESSION_INVALID);
    CHECK(Bc250DmaOwnerBegin(&fixture.Owner, 4096, 0, (BC250_DMA_OWNER_TICKET *)&fixture.Session) == BC250_VM_SESSION_INVALID);
    CHECK(Bc250DmaOwnerBegin(&fixture.Owner, 4096, 0, (BC250_DMA_OWNER_TICKET *)fixture.Session.Backend.Root) == BC250_VM_SESSION_INVALID);
    CHECK(Bc250DmaOwnerBegin(NULL, 4096, 0, &out) == BC250_VM_SESSION_INVALID);
    CHECK(Bc250DmaOwnerBegin(&zero, 4096, 0, &out) == BC250_VM_SESSION_INVALID);
    CHECK(memcmp(&fixture.Owner, &before, sizeof(before)) == 0 && memcmp(saved, &out, sizeof(out)) == 0);
    for (i = 1; i <= 64; ++i) {
        CHECK(Bc250DmaOwnerBegin(&fixture.Owner, (BC250_AD_U64)i * 4096, i & 1, &out) == BC250_VM_SESSION_OK);
        CHECK(Bc250DmaOwnerResolveNoResource(&fixture.Owner, &out) == BC250_VM_SESSION_OK);
        CHECK(fixture.Owner.LastId == i);
    }
    memset(&out, 0xA5, sizeof(out)); memcpy(saved, &out, sizeof(out));
    fixture.Owner.LastId = ~0ULL; before = fixture.Owner; /* corruption/exhaustion test-only fixture */
    CHECK(Bc250DmaOwnerBegin(&fixture.Owner, 4096, 0, &out) == BC250_VM_SESSION_EXHAUSTED);
    CHECK(memcmp(&fixture.Owner, &before, sizeof(before)) == 0 && memcmp(saved, &out, sizeof(out)) == 0);
    OwnerEnd(&fixture);
}
static void TestOwnerBufferGuards(void)
{
    OWNER_FIXTURE fixture;
    BC250_DMA_OWNER before;
    BC250_DMA_OWNER_TICKET ticket;
    BC250_AD_SPAN runs[2], va;
    BC250_VM_SESSION_MAPPING_HANDLE out, mapping;
    BC250_VM_CPU_JOURNAL journal;
    BC250_GART_U64 digest;
    unsigned char saved_out[sizeof(out)], saved_journal[sizeof(journal)];
    OwnerStart(&fixture); OwnerRuns(runs); ticket = OwnerBegin(&fixture, 0);
    OwnerDeliver(&fixture.Owner, &ticket, ProviderIssue(&fixture.Provider, 0), runs, 2, 2, BC250_VM_SESSION_OK, 1);
    OwnerSpan(&va, 6, 0, 16384); memset(&out, 0xA5, sizeof(out)); memset(&journal, 0xA5, sizeof(journal));
    before = fixture.Owner; digest = Digest(&fixture.Session.Backend);
    memcpy(saved_out, &out, sizeof(out)); memcpy(saved_journal, &journal, sizeof(journal));
    CHECK(Bc250DmaOwnerMap(&fixture.Owner, &ticket, &va, 3, (BC250_VM_SESSION_MAPPING_HANDLE *)&fixture.Owner,
        &journal, NULL, NULL) == BC250_VM_SESSION_INVALID);
    CHECK(Bc250DmaOwnerMap(&fixture.Owner, &ticket, (BC250_AD_SPAN *)&fixture.Owner, 3,
        &out, &journal, NULL, NULL) == BC250_VM_SESSION_INVALID);
    CHECK(Bc250DmaOwnerMap(&fixture.Owner, &ticket, &va, 3, &out,
        (BC250_VM_CPU_JOURNAL *)&fixture.Owner, NULL, NULL) == BC250_VM_SESSION_INVALID);
    CHECK(Bc250DmaOwnerMap(&fixture.Owner, &ticket, &va, 3, (BC250_VM_SESSION_MAPPING_HANDLE *)&ticket,
        &journal, NULL, NULL) == BC250_VM_SESSION_INVALID);
    CHECK(Bc250DmaOwnerMap(&fixture.Owner, &ticket, &va, 3, &out,
        (BC250_VM_CPU_JOURNAL *)&ticket, NULL, NULL) == BC250_VM_SESSION_INVALID);
    CHECK(Bc250DmaOwnerMap(&fixture.Owner, (BC250_DMA_OWNER_TICKET *)&fixture.Owner, &va, 3,
        &out, &journal, NULL, NULL) == BC250_VM_SESSION_INVALID);
    CHECK(memcmp(&fixture.Owner, &before, sizeof(before)) == 0 &&
        Digest(&fixture.Session.Backend) == digest && memcmp(saved_out, &out, sizeof(out)) == 0 &&
        memcmp(saved_journal, &journal, sizeof(journal)) == 0 && !fixture.Provider.Puts);
    CHECK(Bc250DmaOwnerMap(&fixture.Owner, &ticket, &va, 3, &mapping, &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    before = fixture.Owner; digest = Digest(&fixture.Session.Backend);
    CHECK(Bc250DmaOwnerUnmap(&fixture.Owner, &ticket, &mapping,
        (BC250_VM_CPU_JOURNAL *)&fixture.Owner, NULL, NULL) == BC250_VM_SESSION_INVALID);
    CHECK(Bc250DmaOwnerUnmap(&fixture.Owner, &ticket, &mapping,
        (BC250_VM_CPU_JOURNAL *)&ticket, NULL, NULL) == BC250_VM_SESSION_INVALID);
    CHECK(Bc250DmaOwnerUnmap(&fixture.Owner, &ticket, (BC250_VM_SESSION_MAPPING_HANDLE *)&fixture.Owner,
        &journal, NULL, NULL) == BC250_VM_SESSION_INVALID);
    CHECK(memcmp(&fixture.Owner, &before, sizeof(before)) == 0 && Digest(&fixture.Session.Backend) == digest);
    fixture.Owner.Cookie = NULL; /* test-only metadata corruption */
    CHECK(Bc250DmaOwnerRelease(&fixture.Owner, &ticket) == BC250_VM_SESSION_CORRUPT && !fixture.Provider.Puts);
    fixture.Owner = before;
    CHECK(Bc250DmaOwnerUnmap(&fixture.Owner, &ticket, &mapping, &journal, NULL, NULL) == BC250_VM_SESSION_OK);
    CHECK(Bc250DmaOwnerRelease(&fixture.Owner, &ticket) == BC250_VM_SESSION_OK);
    OwnerEnd(&fixture);
}
int main(void)
{
    unsigned int baseline;
    CHECK(Bc250PreviousCpuBackendTests() == 0); baseline = assertions;
    TestPendingResolution(); TestInvalidDeliveredRuns(); TestMappedCancellation();
    TestSharedSessionAndCapacity(); TestBeginGuards(); TestOwnerBufferGuards();
    printf("PASS: %u additional simulated DMA-owner assertions (%u total); no Windows DMA/IRQL/GPU validation\n", assertions - baseline, assertions);
    return 0;
}
