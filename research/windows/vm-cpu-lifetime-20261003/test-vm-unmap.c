/* SPDX-License-Identifier: Apache-2.0
 * Actual shared unmap extension against the established RAM-only heap fixture.
 * No OS/device calls; zeroing CPU metadata is NOT GPU unmapping/TLB retirement.
 * Retired allocator blocks remain poisoned until fixture teardown, making any
 * post-release metadata write detectable. The predecessor suite runs as well.
 */
#define main Bc250PreviousCpuBackendTests
#include "../vm-cpu-backend-20261003/test-vm-cpu-backend.c"
#undef main
#include "vm-cpu-lifetime-20261003/bc250_vm_cpu_unmap.h"

static void UnmapNestedBusy(TEST_HEAP *heap)
{
    if (!heap->Reentry) return;
    CHECK(Bc250VmCpuUnmap(heap->Context, 0, 1, &heap->NestedJournal,
        NULL, NULL) == BC250_VM_CPU_BUSY_RESULT);
}

static void UnmapRelease(void *opaque, BC250_VM_CPU_NODE *node)
{
    TEST_HEAP *heap = opaque;
    UnmapNestedBusy(heap);
    Release(opaque, node); /* Also checks Init/Map/Read/Shutdown reentry. */
}

static int UnmapAfterWrite(void *opaque, unsigned int count)
{
    TEST_HEAP *heap = opaque;
    UnmapNestedBusy(heap);
    return AfterWrite(opaque, count);
}

static void StartUnmap(TEST_HEAP *heap, BC250_VM_CPU_CONTEXT *ctx,
    unsigned int root, BC250_GART_U64 max_pfn)
{
    Start(heap, ctx, root, max_pfn, 256);
    ctx->Free = UnmapRelease;
}

static void ZeroRead(BC250_VM_CPU_CONTEXT *ctx, BC250_GART_U64 va)
{
    BC250_GART_U64 value = ~0ULL;
    CHECK(Bc250VmCpuRead(ctx, va, &value) == BC250_VM_CPU_OK);
    CHECK(value == 0);
}

static void UnmapBoundaries(void)
{
    const BC250_GART_U64 boundaries[] = {4096, 1ULL << 21, 1ULL << 30, 1ULL << 39};
    TEST_HEAP heap;
    BC250_VM_CPU_CONTEXT ctx;
    BC250_VM_CPU_JOURNAL journal;
    BC250_ADDRESS_SPAN pages[64];
    unsigned int root, boundary, i;
    for (i = 0; i < 64; ++i)
        Page(&pages[i], ((BC250_GART_U64)((i * 17) % 29) + 1) * 4096);
    for (root = 1; root <= 3; ++root) {
        BC250_GART_U64 max_pfn = root == 1 ? 1ULL << 36 :
                                  root == 2 ? 1ULL << 27 : 1ULL << 18;
        for (boundary = 0; boundary < 4; ++boundary) {
            BC250_GART_U64 va = boundaries[boundary] - 4096;
            unsigned int attempts, frees, count = 64;
            BC250_VM_CPU_NODE *retained;
            if (va / 4096 >= max_pfn || count > max_pfn - va / 4096) continue;
            StartUnmap(&heap, &ctx, root, max_pfn); retained = ctx.Root;
            CHECK(Bc250VmCpuMap(&ctx, va, pages, count, 64, 15,
                &journal, NULL, NULL) == BC250_VM_CPU_OK);
            attempts = heap.Attempts; frees = heap.Frees;
            heap.Reentry = 1; heap.HookCalls = 0;
            CHECK(Bc250VmCpuUnmap(&ctx, va, count, &journal,
                UnmapAfterWrite, &heap) == BC250_VM_CPU_OK);
            CHECK(heap.HookCalls == count && heap.Attempts == attempts);
            CHECK(heap.Frees > frees);
            CHECK(ctx.Root == retained && ctx.OwnedNodes == 1 && heap.Live == 1);
            CHECK(ctx.State == BC250_VM_CPU_READY);
            CHECK(memcmp(&journal, &empty_journal, sizeof(journal)) == 0);
            for (i = 0; i < count; ++i) ZeroRead(&ctx, va + (BC250_GART_U64)i * 4096);
            frees = heap.Frees; heap.HookCalls = 0;
            CHECK(Bc250VmCpuUnmap(&ctx, va, count, &journal,
                UnmapAfterWrite, &heap) == BC250_VM_CPU_OK);
            CHECK(heap.HookCalls == count && heap.Frees == frees && heap.Attempts == attempts);
            CHECK(ctx.Root == retained && ctx.OwnedNodes == 1 && heap.Live == 1);
            End(&heap, &ctx);
        }
    }
    StartUnmap(&heap, &ctx, 1, 1ULL << 36);
    Page(&pages[0], 0x0000FFFFFFFFF000ULL);
    CHECK(Bc250VmCpuMap(&ctx, 0x0000FFFFFFFFF000ULL, pages, 1, 64, 15,
        &journal, NULL, NULL) == BC250_VM_CPU_OK);
    CHECK(Bc250VmCpuUnmap(&ctx, 0x0000FFFFFFFFF000ULL, 1, &journal,
        NULL, NULL) == BC250_VM_CPU_OK);
    CHECK(ctx.OwnedNodes == 1 && heap.Live == 1);
    ZeroRead(&ctx, 0x0000FFFFFFFFF000ULL);
    End(&heap, &ctx);
}

static void UnmapAliasAndPreservedNeighbour(void)
{
    TEST_HEAP heap;
    BC250_VM_CPU_CONTEXT ctx;
    BC250_VM_CPU_JOURNAL journal;
    BC250_ADDRESS_SPAN same_dma, neighbour;
    unsigned int attempts, frees;
    BC250_VM_CPU_NODE *retained;
    StartUnmap(&heap, &ctx, 1, 1ULL << 36); retained = ctx.Root;
    Page(&same_dma, 0x22000); Page(&neighbour, 0x93000);
    CHECK(Bc250VmCpuMap(&ctx, 0, &same_dma, 1, 1, 3,
        &journal, NULL, NULL) == BC250_VM_CPU_OK);
    CHECK(Bc250VmCpuMap(&ctx, 4096, &neighbour, 1, 1, 15,
        &journal, NULL, NULL) == BC250_VM_CPU_OK);
    CHECK(Bc250VmCpuMap(&ctx, 1ULL << 39, &same_dma, 1, 1, 3,
        &journal, NULL, NULL) == BC250_VM_CPU_OK);
    CHECK(ctx.OwnedNodes == 7 && heap.Live == 7);
    attempts = heap.Attempts; frees = heap.Frees;
    CHECK(Bc250VmCpuUnmap(&ctx, 0, 1, &journal, NULL, NULL) == BC250_VM_CPU_OK);
    CHECK(heap.Frees == frees && heap.Live == 7 && ctx.OwnedNodes == 7);
    ZeroRead(&ctx, 0);
    CheckValue(&ctx, 4096, Expected(&neighbour, 15));
    CheckValue(&ctx, 1ULL << 39, Expected(&same_dma, 3));
    CHECK(Bc250VmCpuUnmap(&ctx, 4096, 1, &journal, NULL, NULL) == BC250_VM_CPU_OK);
    CHECK(heap.Frees == frees + 3 && heap.Live == 4 && ctx.OwnedNodes == 4);
    CHECK(ctx.Root->Data.Children[0] == NULL);
    CheckValue(&ctx, 1ULL << 39, Expected(&same_dma, 3));
    CHECK(Bc250VmCpuUnmap(&ctx, 1ULL << 39, 1, &journal,
        NULL, NULL) == BC250_VM_CPU_OK);
    CHECK(ctx.Root == retained && heap.Live == 1 && ctx.OwnedNodes == 1);
    CHECK(heap.Frees == frees + 6 && heap.Attempts == attempts);
    End(&heap, &ctx);
}

static void UnmapFaultsEveryPage(void)
{
    TEST_HEAP heap;
    BC250_VM_CPU_CONTEXT ctx;
    BC250_VM_CPU_JOURNAL journal;
    BC250_ADDRESS_SPAN pages[64];
    const BC250_GART_U64 va = (1ULL << 39) - 4096;
    unsigned int i, failure, shape;
    for (i = 0; i < 64; ++i)
        Page(&pages[i], ((BC250_GART_U64)((i * 7) % 19) + 1) * 4096);
    /* Full list, one present page with remaining paths absent, and all absent. */
    for (shape = 0; shape < 3; ++shape) {
        StartUnmap(&heap, &ctx, 1, 1ULL << 36);
        if (shape < 2) CHECK(Bc250VmCpuMap(&ctx, va, pages, shape ? 1 : 64, 64, 15,
            &journal, NULL, NULL) == BC250_VM_CPU_OK);
        for (failure = 1; failure <= 64; ++failure) {
            BC250_GART_U64 before = Digest(&ctx);
            unsigned int live = heap.Live, frees = heap.Frees, attempts = heap.Attempts;
            heap.FailWrite = failure; heap.HookCalls = 0; heap.Reentry = 1;
            CHECK(Bc250VmCpuUnmap(&ctx, va, 64, &journal,
                UnmapAfterWrite, &heap) == BC250_VM_CPU_FAULT);
            CHECK(heap.HookCalls == failure && heap.Frees == frees && heap.Attempts == attempts);
            CHECK(heap.Live == live && ctx.OwnedNodes == live && Digest(&ctx) == before);
            CHECK(ctx.State == BC250_VM_CPU_READY);
            CHECK(memcmp(&journal, &empty_journal, sizeof(journal)) == 0);
            for (i = 0; i < 64; ++i) {
                if (shape == 0 || (shape == 1 && i == 0))
                    CheckValue(&ctx, va + (BC250_GART_U64)i * 4096, Expected(&pages[i], 15));
                else ZeroRead(&ctx, va + (BC250_GART_U64)i * 4096);
            }
        }
        heap.FailWrite = 0; heap.HookCalls = 0;
        CHECK(Bc250VmCpuUnmap(&ctx, va, 64, &journal,
            UnmapAfterWrite, &heap) == BC250_VM_CPU_OK);
        CHECK(heap.HookCalls == 64 && heap.Live == 1 && ctx.OwnedNodes == 1);
        End(&heap, &ctx);
    }
}

static void UnmapRawNonzeroAndEmptyBranches(void)
{
    TEST_HEAP heap;
    BC250_VM_CPU_CONTEXT ctx;
    BC250_VM_CPU_JOURNAL journal;
    BC250_ADDRESS_SPAN page;
    BC250_VM_CPU_NODE *leaf, *retained;
    unsigned int frees, attempts;
    StartUnmap(&heap, &ctx, 1, 1ULL << 36); retained = ctx.Root;
    Page(&page, 0x35000);
    CHECK(Bc250VmCpuMap(&ctx, 0, &page, 1, 1, 3,
        &journal, NULL, NULL) == BC250_VM_CPU_OK);
    CHECK(Bc250VmCpuMap(&ctx, 4096, &page, 1, 1, 3,
        &journal, NULL, NULL) == BC250_VM_CPU_OK);
    leaf = IndependentLeaf(&ctx, 4096);
    /* Exclusive test-fixture edit BETWEEN calls. A raw nonzero word with
     * VALID clear is not permission for the backend to discard that storage. */
    leaf->Data.Values[1] = 0x80000000ULL;
    CHECK(!(leaf->Data.Values[1] & 1ULL));
    frees = heap.Frees; attempts = heap.Attempts;
    CHECK(Bc250VmCpuUnmap(&ctx, 0, 1, &journal, NULL, NULL) == BC250_VM_CPU_OK);
    CHECK(heap.Frees == frees && ctx.OwnedNodes == 4 && heap.Live == 4);
    CheckValue(&ctx, 4096, 0x80000000ULL); ZeroRead(&ctx, 0);
    CHECK(Bc250VmCpuUnmap(&ctx, 4096, 1, &journal, NULL, NULL) == BC250_VM_CPU_OK);
    CHECK(heap.Frees == frees + 3 && ctx.OwnedNodes == 1 && heap.Live == 1);
    CHECK(ctx.Root == retained && heap.Attempts == attempts);

    /* The documented postcommit sweep can prune an unrelated already-empty
     * owned branch. It must not allocate, and still retains the root. */
    CHECK(Bc250VmCpuMap(&ctx, 0, &page, 1, 1, 3,
        &journal, NULL, NULL) == BC250_VM_CPU_OK);
    CHECK(Bc250VmCpuMap(&ctx, 1ULL << 39, &page, 1, 1, 3,
        &journal, NULL, NULL) == BC250_VM_CPU_OK);
    CHECK(ctx.OwnedNodes == 7 && heap.Live == 7);
    leaf = IndependentLeaf(&ctx, 1ULL << 39); leaf->Data.Values[0] = 0;
    frees = heap.Frees; attempts = heap.Attempts;
    CHECK(Bc250VmCpuUnmap(&ctx, 0, 1, &journal, NULL, NULL) == BC250_VM_CPU_OK);
    CHECK(heap.Frees == frees + 6 && ctx.OwnedNodes == 1 && heap.Live == 1);
    CHECK(ctx.Root == retained && heap.Attempts == attempts);
    End(&heap, &ctx);
}

static void UnmapInvalidAndCorrupt(void)
{
    TEST_HEAP heap;
    BC250_VM_CPU_CONTEXT ctx, foreign;
    BC250_VM_CPU_JOURNAL journal;
    BC250_VM_CPU_NODE borrowed;
    BC250_ADDRESS_SPAN page;
    BC250_GART_U64 before;
    unsigned int live, attempts, frees;
    StartUnmap(&heap, &ctx, 1, 1ULL << 36);
    Page(&page, 0x73000);
    CHECK(Bc250VmCpuMap(&ctx, 0, &page, 1, 1, 3,
        &journal, NULL, NULL) == BC250_VM_CPU_OK);
    before = Digest(&ctx); live = heap.Live; attempts = heap.Attempts; frees = heap.Frees;
#define UNMAP_REFUSE(address, count, output_journal) do { \
    CHECK(Bc250VmCpuUnmap(&ctx, address, count, output_journal, NULL, NULL) == \
        BC250_VM_CPU_INVALID); \
    CHECK(Digest(&ctx) == before && heap.Live == live && heap.Frees == frees && \
        heap.Attempts == attempts); } while (0)
    UNMAP_REFUSE(1, 1, &journal);
    UNMAP_REFUSE(0, 0, &journal);
    UNMAP_REFUSE(0, 65, &journal);
    UNMAP_REFUSE(1ULL << 48, 1, &journal);
    UNMAP_REFUSE(0x0000FFFFFFFFF000ULL, 2, &journal);
    UNMAP_REFUSE(~0ULL - 4095ULL, 2, &journal);
    UNMAP_REFUSE(0, 1, NULL);
    UNMAP_REFUSE(0, 1, (BC250_VM_CPU_JOURNAL *)ctx.Root);
    UNMAP_REFUSE(0, 1, (BC250_VM_CPU_JOURNAL *)&ctx);
#undef UNMAP_REFUSE
    CHECK(Bc250VmCpuUnmap(NULL, 0, 1, &journal, NULL, NULL) == BC250_VM_CPU_INVALID);
    memset(&borrowed, 0, sizeof(borrowed));
    borrowed.OriginOwner = &foreign; borrowed.Parent = ctx.Root;
    borrowed.ParentSlot = 511; borrowed.Level = 2;
    ctx.Root->Data.Children[511] = &borrowed;
    heap.HookCalls = 0;
    CHECK(Bc250VmCpuUnmap(&ctx, 0, 1, &journal,
        UnmapAfterWrite, &heap) == BC250_VM_CPU_CORRUPT);
    CHECK(heap.HookCalls == 0 && heap.Live == live && heap.Frees == frees && heap.Attempts == attempts);
    ctx.Root->Data.Children[511] = NULL;
    CHECK(Digest(&ctx) == before);
    ++ctx.OwnedNodes;
    CHECK(Bc250VmCpuUnmap(&ctx, 0, 1, &journal, NULL, NULL) == BC250_VM_CPU_CORRUPT);
    CHECK(heap.Frees == frees && heap.Attempts == attempts); --ctx.OwnedNodes;
    CHECK(Digest(&ctx) == before);
    ctx.State = BC250_VM_CPU_BUSY;
    CHECK(Bc250VmCpuUnmap(&ctx, 0, 1, &journal, NULL, NULL) == BC250_VM_CPU_BUSY_RESULT);
    ctx.State = BC250_VM_CPU_READY;
    CHECK(Digest(&ctx) == before); CheckValue(&ctx, 0, Expected(&page, 3));
    End(&heap, &ctx);

    StartUnmap(&heap, &ctx, 1, (1ULL << 27) + 1);
    CHECK(Bc250VmCpuMap(&ctx, 1ULL << 39, &page, 1, 1, 3,
        &journal, NULL, NULL) == BC250_VM_CPU_OK);
    before = Digest(&ctx); frees = heap.Frees; attempts = heap.Attempts;
    CHECK(Bc250VmCpuUnmap(&ctx, 1ULL << 39, 2, &journal,
        NULL, NULL) == BC250_VM_CPU_INVALID);
    CHECK(Digest(&ctx) == before && heap.Frees == frees && heap.Attempts == attempts);
    CHECK(Bc250VmCpuUnmap(&ctx, 1ULL << 39, 1, &journal,
        NULL, NULL) == BC250_VM_CPU_OK);
    CHECK(ctx.OwnedNodes == 1 && heap.Live == 1);
    End(&heap, &ctx);
}

int main(void)
{
    unsigned int baseline;
    CHECK(Bc250PreviousCpuBackendTests() == 0); baseline = assertions;
    UnmapBoundaries(); UnmapAliasAndPreservedNeighbour();
    UnmapFaultsEveryPage(); UnmapRawNonzeroAndEmptyBranches(); UnmapInvalidAndCorrupt();
    printf("PASS: %u additional CPU-unmap assertions (%u total); NOT GPU/TLB retirement.\n",
        assertions - baseline, assertions);
    return 0;
}
