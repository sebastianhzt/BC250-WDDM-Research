/* SPDX-License-Identifier: Apache-2.0
 * Independent RAM-only controls for the actual shared CPU tree backend.
 * Heap callbacks are test-owned; no Windows driver/device or GPU access.
 * Expectations use division/remainder and literal field facts independently.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "vm-cpu-backend-20261003/bc250_vm_cpu_backend.h"

static unsigned int assertions;
static const BC250_VM_CPU_JOURNAL empty_journal = {0};
#define CHECK(c) do { ++assertions; if (!(c)) { \
    fprintf(stderr, "FAIL line %u: %s\n", (unsigned int)__LINE__, #c); exit(1); } } while (0)

typedef struct TEST_BLOCK {
    BC250_VM_CPU_NODE *Node;
    unsigned int Live;
} TEST_BLOCK;

typedef struct TEST_HEAP {
    TEST_BLOCK Block[2048];
    unsigned int Blocks, Live, Attempts, FailAllocation, Frees;
    unsigned int FailWrite, HookCalls, Reentry, NestedCalls;
    BC250_VM_CPU_CONTEXT *Context;
    BC250_VM_CPU_JOURNAL NestedJournal;
    BC250_ADDRESS_SPAN NestedPage;
} TEST_HEAP;

static void NestedBusy(TEST_HEAP *heap)
{
    BC250_GART_U64 value = 0xBADULL;
    if (!heap->Reentry) return;
    ++heap->NestedCalls;
    CHECK(Bc250VmCpuInit(heap->Context, &heap->Context->Layout, 0, 1,
        NULL, NULL, NULL) == BC250_VM_CPU_BUSY_RESULT);
    CHECK(Bc250VmCpuMap(heap->Context, 0, &heap->NestedPage, 1, 1, 3,
        &heap->NestedJournal, NULL, NULL) == BC250_VM_CPU_BUSY_RESULT);
    CHECK(Bc250VmCpuRead(heap->Context, 0, &value) == BC250_VM_CPU_BUSY_RESULT);
    CHECK(value == 0xBADULL);
    CHECK(Bc250VmCpuShutdown(heap->Context) == BC250_VM_CPU_BUSY_RESULT);
}

static BC250_VM_CPU_NODE *Allocate(void *opaque, size_t bytes)
{
    TEST_HEAP *heap = opaque;
    BC250_VM_CPU_NODE *node;
    CHECK(bytes == sizeof(*node));
    ++heap->Attempts;
    NestedBusy(heap);
    if (heap->Attempts == heap->FailAllocation) return NULL;
    CHECK(heap->Blocks < 2048);
    /* Oversized fresh allocation permits a whole journal-sized overlap fixture
     * at node start without claiming bytes beyond the real allocation. */
    node = calloc(1, bytes + sizeof(BC250_VM_CPU_JOURNAL));
    CHECK(node != NULL);
    heap->Block[heap->Blocks].Node = node;
    heap->Block[heap->Blocks].Live = 1;
    ++heap->Blocks; ++heap->Live;
    return node;
}

static void Release(void *opaque, BC250_VM_CPU_NODE *node)
{
    TEST_HEAP *heap = opaque;
    unsigned int i;
    NestedBusy(heap);
    for (i = 0; i < heap->Blocks; ++i) if (heap->Block[i].Node == node) {
        CHECK(heap->Block[i].Live);
        heap->Block[i].Live = 0;
        CHECK(heap->Live != 0);
        --heap->Live; ++heap->Frees;
        /* Keep retired allocations until fixture end: dereference-after-free
         * becomes a deterministic poisoned-node failure instead of heap luck. */
        memset(node, 0xDD, sizeof(*node));
        return;
    }
    CHECK(0); /* foreign/borrowed nodes must NEVER reach this callback */
}

static int AfterWrite(void *opaque, unsigned int count)
{
    TEST_HEAP *heap = opaque;
    ++heap->HookCalls;
    CHECK(count == heap->HookCalls);
    NestedBusy(heap);
    return count != heap->FailWrite;
}

static void FinishHeap(TEST_HEAP *heap)
{
    unsigned char retired[sizeof(BC250_VM_CPU_NODE)];
    unsigned int i;
    memset(retired, 0xDD, sizeof(retired));
    CHECK(heap->Live == 0);
    for (i = 0; i < heap->Blocks; ++i) {
        CHECK(!heap->Block[i].Live);
        CHECK(memcmp(heap->Block[i].Node, retired, sizeof(retired)) == 0);
        free(heap->Block[i].Node);
    }
}

static void Page(BC250_ADDRESS_SPAN *page, BC250_GART_U64 dma)
{
    CHECK(Bc250DescribeAddressNumbers(BC250_ADDRESS_DMA_LOGICAL, dma,
        4096, 0x0000FFFFFFFFFFFFULL, page));
}

static BC250_GART_U64 Expected(const BC250_ADDRESS_SPAN *page, unsigned int access)
{
    BC250_GART_U64 flags = 3;
    if (access & 1) flags |= 0x20;
    if (access & 2) flags |= 0x40;
    if (access & 4) flags |= 0x10;
    if (access & 8) flags |= 4;
    return page->Start | flags;
}

static void Start(TEST_HEAP *heap, BC250_VM_CPU_CONTEXT *ctx,
                  unsigned int root, BC250_GART_U64 max_pfn, unsigned int max_nodes)
{
    BC250_VM_LAYOUT layout;
    memset(heap, 0, sizeof(*heap)); memset(ctx, 0, sizeof(*ctx));
    heap->Context = ctx;
    heap->Reentry = 1; /* Includes root allocation BEFORE ordinary metadata exists. */
    Page(&heap->NestedPage, 0x123000);
    CHECK(Bc250VmPlanLayout(root, 9, max_pfn, &layout));
    CHECK(Bc250VmCpuInit(ctx, &layout, 0x0000FFFFFFFFFFFFULL, max_nodes,
        Allocate, Release, heap) == BC250_VM_CPU_OK);
    CHECK(ctx->Root != NULL && heap->Live == 1 && ctx->OwnedNodes == 1);
    CHECK(ctx->State == 1);
    CHECK(heap->NestedCalls == 1);
    heap->Reentry = 0;
    heap->Attempts = 0;
}

static void End(TEST_HEAP *heap, BC250_VM_CPU_CONTEXT *ctx)
{
    unsigned int frees;
    CHECK(Bc250VmCpuShutdown(ctx) == BC250_VM_CPU_OK);
    CHECK(ctx->Root == NULL && ctx->OwnedNodes == 0 && ctx->State == 0);
    frees = heap->Frees;
    CHECK(Bc250VmCpuShutdown(ctx) == BC250_VM_CPU_OK);
    CHECK(heap->Frees == frees);
    FinishHeap(heap);
}

static BC250_GART_U64 Mix(BC250_GART_U64 digest, BC250_GART_U64 value)
{
    return (digest ^ value) * 1099511628211ULL;
}

static BC250_GART_U64 TreeDigest(const BC250_VM_CPU_NODE *node,
                                BC250_GART_U64 digest)
{
    unsigned int i;
    digest = Mix(digest, (BC250_GART_U64)(uintptr_t)node);
    digest = Mix(digest, (BC250_GART_U64)(uintptr_t)node->OriginOwner);
    digest = Mix(digest, (BC250_GART_U64)(uintptr_t)node->Parent);
    digest = Mix(digest, node->ParentSlot); digest = Mix(digest, node->Level);
    CHECK(node->Level >= 1 && node->Level <= 4);
    for (i = 0; i < 512; ++i) {
        if (node->Level == 4) digest = Mix(digest, node->Data.Values[i]);
        else {
            digest = Mix(digest, (BC250_GART_U64)(uintptr_t)node->Data.Children[i]);
            if (node->Data.Children[i]) digest = TreeDigest(node->Data.Children[i], digest);
        }
    }
    return digest;
}

static BC250_GART_U64 Digest(const BC250_VM_CPU_CONTEXT *ctx)
{
    return TreeDigest(ctx->Root, Mix(1469598103934665603ULL, ctx->OwnedNodes));
}

static BC250_VM_CPU_NODE *IndependentLeaf(BC250_VM_CPU_CONTEXT *ctx, BC250_GART_U64 va)
{
    const BC250_GART_U64 span[5] = {0, 1ULL << 39, 1ULL << 30, 1ULL << 21, 4096};
    BC250_VM_CPU_NODE *node = ctx->Root;
    unsigned int level;
    for (level = ctx->Layout.Root; level < 4; ++level) {
        unsigned int index = (unsigned int)((va / span[level]) % 512);
        CHECK(node->OriginOwner == ctx && node->Level == level);
        CHECK(node->Data.Children[index] != NULL);
        node = node->Data.Children[index];
    }
    CHECK(node->Level == 4 && node->OriginOwner == ctx);
    return node;
}

static void CheckValue(BC250_VM_CPU_CONTEXT *ctx, BC250_GART_U64 va, BC250_GART_U64 expected)
{
    BC250_GART_U64 read = ~0ULL;
    BC250_VM_CPU_NODE *leaf = IndependentLeaf(ctx, va);
    CHECK(leaf->Data.Values[(unsigned int)((va / 4096) % 512)] == expected);
    CHECK(Bc250VmCpuRead(ctx, va, &read) == BC250_VM_CPU_OK);
    CHECK(read == expected);
}

static void BoundaryAndShuffled(void)
{
    TEST_HEAP heap;
    BC250_VM_CPU_CONTEXT ctx;
    BC250_VM_CPU_JOURNAL journal;
    BC250_ADDRESS_SPAN pages[64];
    const BC250_GART_U64 boundaries[] = {4096, 1ULL << 21, 1ULL << 30, 1ULL << 39};
    unsigned int root, boundary, round, i;
    for (root = 1; root <= 3; ++root) {
        BC250_GART_U64 max_pfn = root == 1 ? 1ULL << 36 :
                                  root == 2 ? 1ULL << 27 : 1ULL << 18;
        Start(&heap, &ctx, root, max_pfn, 256);
        for (boundary = 0; boundary < 4; ++boundary) {
            BC250_GART_U64 va = boundaries[boundary] - 4096;
            if (boundaries[boundary] / 4096 >= max_pfn) continue;
            Page(&pages[0], 0x100000 + (BC250_GART_U64)boundary * 0x2000);
            Page(&pages[1], 0x810000 - (BC250_GART_U64)boundary * 0x5000);
            CHECK(Bc250VmCpuMap(&ctx, va, pages, 2, 64, 15,
                &journal, NULL, NULL) == BC250_VM_CPU_OK);
            CheckValue(&ctx, va, Expected(&pages[0], 15));
            CheckValue(&ctx, va + 4096, Expected(&pages[1], 15));
        }
        End(&heap, &ctx);
    }
    Start(&heap, &ctx, 1, 1ULL << 36, 256);
    for (round = 0; round < 32; ++round) {
        BC250_GART_U64 va = (1ULL << 30) - 4096;
        unsigned int access = round % 16;
        for (i = 0; i < 64; ++i)
            Page(&pages[i], ((BC250_GART_U64)((i * 17 + round) % 29) + 1) * 4096);
        CHECK(Bc250VmCpuMap(&ctx, va, pages, 64, 64, access,
            &journal, NULL, NULL) == BC250_VM_CPU_OK);
        for (i = 0; i < 64; ++i) CheckValue(&ctx, va + (BC250_GART_U64)i * 4096,
            Expected(&pages[i], access));
        CHECK(heap.Live == ctx.OwnedNodes);
    }
    Page(&pages[0], 0x0000FFFFFFFFF000ULL);
    CHECK(Bc250VmCpuMap(&ctx, 0x0000FFFFFFFFF000ULL, pages, 1, 64, 15,
        &journal, NULL, NULL) == BC250_VM_CPU_OK);
    CheckValue(&ctx, 0x0000FFFFFFFFF000ULL, 0x0000FFFFFFFFF077ULL);
    End(&heap, &ctx);
}

static void AllocationFailures(void)
{
    const BC250_GART_U64 boundaries[] = {1ULL << 21, 1ULL << 30, 1ULL << 39};
    BC250_ADDRESS_SPAN pages[64], seed;
    BC250_VM_CPU_JOURNAL journal;
    TEST_HEAP heap;
    BC250_VM_CPU_CONTEXT ctx;
    unsigned int i, boundary, total, failure;
    for (i = 0; i < 64; ++i) Page(&pages[i], ((BC250_GART_U64)i * 13 + 1) * 4096);
    Page(&seed, 0xA00000);
    for (boundary = 0; boundary < 3; ++boundary) {
        BC250_GART_U64 va = boundaries[boundary] - 4096;
        Start(&heap, &ctx, 1, 1ULL << 36, 256);
        CHECK(Bc250VmCpuMap(&ctx, 0, &seed, 1, 1, 3,
            &journal, NULL, NULL) == BC250_VM_CPU_OK);
        heap.Attempts = 0;
        CHECK(Bc250VmCpuMap(&ctx, va, pages, 64, 64, 7,
            &journal, NULL, NULL) == BC250_VM_CPU_OK);
        total = heap.Attempts; CHECK(total != 0);
        End(&heap, &ctx);
        for (failure = 1; failure <= total; ++failure) {
            BC250_GART_U64 before;
            unsigned int live, frees;
            Start(&heap, &ctx, 1, 1ULL << 36, 256);
            CHECK(Bc250VmCpuMap(&ctx, 0, &seed, 1, 1, 3,
                &journal, NULL, NULL) == BC250_VM_CPU_OK);
            before = Digest(&ctx); live = heap.Live; frees = heap.Frees;
            heap.Attempts = 0; heap.FailAllocation = failure;
            heap.Reentry = 1;
            CHECK(Bc250VmCpuMap(&ctx, va, pages, 64, 64, 7,
                &journal, NULL, NULL) == BC250_VM_CPU_NO_MEMORY);
            CHECK(heap.Attempts == failure);
            CHECK(journal.LinkCount == 0 && journal.LeafCount == 0);
            CHECK(memcmp(&journal, &empty_journal, sizeof(journal)) == 0);
            CHECK(heap.Live == live && ctx.OwnedNodes == live);
            CHECK(heap.Frees - frees == failure - 1);
            CHECK(Digest(&ctx) == before && ctx.State == 1);
            CHECK(heap.NestedCalls != 0);
            CheckValue(&ctx, 0, Expected(&seed, 3));
            heap.FailAllocation = 0; heap.Reentry = 0;
            CHECK(Bc250VmCpuMap(&ctx, va, pages, 64, 64, 7,
                &journal, NULL, NULL) == BC250_VM_CPU_OK);
            End(&heap, &ctx);
        }
    }
}

static void AfterWriteFailures(void)
{
    TEST_HEAP heap;
    BC250_VM_CPU_CONTEXT ctx;
    BC250_VM_CPU_JOURNAL journal;
    BC250_ADDRESS_SPAN original[64], changed[64];
    unsigned int i, failure, new_branch;
    Start(&heap, &ctx, 1, 1ULL << 36, 256);
    for (i = 0; i < 64; ++i) {
        Page(&original[i], ((BC250_GART_U64)i * 3 + 1) * 4096);
        Page(&changed[i], ((BC250_GART_U64)((i * 19) % 31) + 0x1000) * 4096);
    }
    CHECK(Bc250VmCpuMap(&ctx, 0x1FF000, original, 64, 64, 3,
        &journal, NULL, NULL) == BC250_VM_CPU_OK);
    for (new_branch = 0; new_branch < 2; ++new_branch) {
        BC250_GART_U64 va = new_branch ? (1ULL << 39) - 4096 : 0x1FF000ULL;
        for (failure = 1; failure <= 64; ++failure) {
            BC250_GART_U64 before = Digest(&ctx);
            unsigned int live = heap.Live;
            heap.FailWrite = failure; heap.HookCalls = 0; heap.Reentry = 1;
            CHECK(Bc250VmCpuMap(&ctx, va, changed, 64, 64, 15,
                &journal, AfterWrite, &heap) == BC250_VM_CPU_FAULT);
            CHECK(heap.HookCalls == failure);
            CHECK(journal.LinkCount == 0 && journal.LeafCount == 0);
            CHECK(memcmp(&journal, &empty_journal, sizeof(journal)) == 0);
            CHECK(heap.Live == live && ctx.OwnedNodes == live && ctx.State == 1);
            CHECK(Digest(&ctx) == before);
            for (i = 0; i < 64; ++i) CheckValue(&ctx,
                0x1FF000ULL + (BC250_GART_U64)i * 4096, Expected(&original[i], 3));
        }
    }
    heap.FailWrite = 0; heap.HookCalls = 0; heap.Reentry = 1;
    CHECK(Bc250VmCpuMap(&ctx, 0x1FF000, changed, 64, 64, 15,
        &journal, AfterWrite, &heap) == BC250_VM_CPU_OK);
    CHECK(heap.HookCalls == 64 && heap.NestedCalls != 0);
    CHECK(memcmp(&journal, &empty_journal, sizeof(journal)) == 0);
    for (i = 0; i < 64; ++i) CheckValue(&ctx,
        0x1FF000ULL + (BC250_GART_U64)i * 4096, Expected(&changed[i], 15));
    End(&heap, &ctx);
}

static void InvalidAndCapacity(void)
{
    TEST_HEAP heap;
    BC250_VM_CPU_CONTEXT ctx;
    BC250_VM_CPU_JOURNAL journal;
    BC250_ADDRESS_SPAN pages[65], saved;
    BC250_GART_U64 before, value;
    unsigned int i, index, malformed;
    Start(&heap, &ctx, 1, 1ULL << 36, 256);
    for (i = 0; i < 65; ++i) Page(&pages[i], ((BC250_GART_U64)i + 1) * 4096);
    before = Digest(&ctx);
#define REFUSE(va, array, count, capacity, access, journal_pointer) do { \
    CHECK(Bc250VmCpuMap(&ctx, va, array, count, capacity, access, journal_pointer, \
        NULL, NULL) == BC250_VM_CPU_INVALID); \
    CHECK(Digest(&ctx) == before && heap.Live == 1 && heap.Attempts == 0); } while (0)
    REFUSE(1, pages, 1, 65, 3, &journal);
    REFUSE(0, pages, 0, 65, 3, &journal);
    REFUSE(0, pages, 65, 65, 3, &journal);
    REFUSE(0, pages, 64, 63, 3, &journal);
    REFUSE(0x0000FFFFFFFFF000ULL, pages, 2, 65, 3, &journal);
    REFUSE(~0ULL - 4095ULL, pages, 2, 65, 3, &journal);
    REFUSE(0, pages, 1, 65, 16, &journal);
    REFUSE(0, NULL, 1, 65, 3, &journal);
    REFUSE(0, pages, 1, 65, 3, NULL);
    REFUSE(0, (const BC250_ADDRESS_SPAN *)&ctx, 1, 1, 3, &journal);
    REFUSE(0, (const BC250_ADDRESS_SPAN *)&journal, 1, 1, 3, &journal);
    for (malformed = 0; malformed < 5; ++malformed) {
        for (index = 0; index < 3; ++index) {
            i = index == 0 ? 0 : index == 1 ? 31 : 63;
            saved = pages[i];
            switch (malformed) {
            case 0: pages[i].Domain = BC250_ADDRESS_CPU_PHYSICAL; break;
            case 1: pages[i].Bytes = 0; break;
            case 2: ++pages[i].Start; ++pages[i].Last; break;
            case 3: --pages[i].Last; break;
            default: pages[i].Start = 1ULL << 48; pages[i].Last = pages[i].Start + 4095; break;
            }
            REFUSE(0, pages, 64, 65, 3, &journal);
            pages[i] = saved;
        }
    }
#undef REFUSE
    value = 0xBADULL;
    CHECK(Bc250VmCpuRead(&ctx, 1ULL << 48, &value) == BC250_VM_CPU_INVALID);
    CHECK(value == 0xBADULL);
    CHECK(Bc250VmCpuRead(&ctx, 0, NULL) == BC250_VM_CPU_INVALID);
    End(&heap, &ctx);
    Start(&heap, &ctx, 1, 1ULL << 36, 3);
    before = Digest(&ctx);
    CHECK(Bc250VmCpuMap(&ctx, 0, pages, 1, 65, 3,
        &journal, NULL, NULL) == BC250_VM_CPU_NO_MEMORY);
    CHECK(Digest(&ctx) == before && ctx.OwnedNodes == 1 && heap.Live == 1);
    End(&heap, &ctx);
}

static void ForeignAndCorruption(void)
{
    TEST_HEAP heap;
    BC250_VM_CPU_CONTEXT ctx, foreign_owner;
    BC250_VM_CPU_JOURNAL journal;
    BC250_VM_CPU_NODE borrowed, *child;
    BC250_ADDRESS_SPAN page;
    BC250_GART_U64 value;
    unsigned int live, frees, saved_slot, saved_level;
    Start(&heap, &ctx, 1, 1ULL << 36, 256);
    Page(&page, 0x11000);
    CHECK(Bc250VmCpuMap(&ctx, 0, &page, 1, 1, 3,
        &journal, NULL, NULL) == BC250_VM_CPU_OK);
    child = ctx.Root->Data.Children[0]; CHECK(child != NULL);
    live = heap.Live; frees = heap.Frees;
    memset(&borrowed, 0, sizeof(borrowed));
    borrowed.OriginOwner = &foreign_owner;
    borrowed.Parent = ctx.Root; borrowed.ParentSlot = 511; borrowed.Level = 2;
    ctx.Root->Data.Children[511] = &borrowed;
    value = 0xBAD;
    CHECK(Bc250VmCpuRead(&ctx, 511ULL << 39, &value) == BC250_VM_CPU_CORRUPT);
    CHECK(value == 0xBAD);
    CHECK(Bc250VmCpuMap(&ctx, 511ULL << 39, &page, 1, 1, 3,
        &journal, NULL, NULL) == BC250_VM_CPU_CORRUPT);
    CHECK(Bc250VmCpuShutdown(&ctx) == BC250_VM_CPU_CORRUPT);
    CHECK(ctx.Root != NULL && ctx.State == 1 && heap.Live == live && heap.Frees == frees);
    ctx.Root->Data.Children[511] = NULL;
    saved_slot = child->ParentSlot; child->ParentSlot = 1;
    CHECK(Bc250VmCpuShutdown(&ctx) == BC250_VM_CPU_CORRUPT);
    CHECK(heap.Live == live && heap.Frees == frees); child->ParentSlot = saved_slot;
    saved_level = child->Level; child->Level = 4;
    CHECK(Bc250VmCpuShutdown(&ctx) == BC250_VM_CPU_CORRUPT);
    CHECK(heap.Live == live && heap.Frees == frees); child->Level = saved_level;
    ctx.Root->Data.Children[511] = child; /* same-owner alias with mismatched parent slot */
    CHECK(Bc250VmCpuShutdown(&ctx) == BC250_VM_CPU_CORRUPT);
    CHECK(heap.Live == live && heap.Frees == frees); ctx.Root->Data.Children[511] = NULL;
    ctx.Root->Data.Children[511] = ctx.Root; /* obvious cycle must refuse, not recurse forever */
    CHECK(Bc250VmCpuShutdown(&ctx) == BC250_VM_CPU_CORRUPT);
    CHECK(heap.Live == live && heap.Frees == frees); ctx.Root->Data.Children[511] = NULL;
    CheckValue(&ctx, 0, Expected(&page, 3));
    End(&heap, &ctx);
}

static void KnownStorageOverlap(void)
{
    TEST_HEAP heap;
    BC250_VM_CPU_CONTEXT ctx;
    BC250_VM_CPU_JOURNAL journal;
    BC250_VM_CPU_NODE *leaf;
    BC250_ADDRESS_SPAN page;
    BC250_GART_U64 before, maximum;
    Start(&heap, &ctx, 1, 1ULL << 36, 256);
    Page(&page, 0x22000);
    CHECK(Bc250VmCpuMap(&ctx, 0, &page, 1, 1, 3,
        &journal, NULL, NULL) == BC250_VM_CPU_OK);
    leaf = IndependentLeaf(&ctx, 0); before = Digest(&ctx);
    CHECK(Bc250VmCpuMap(&ctx, 0, (const BC250_ADDRESS_SPAN *)leaf, 1, 1, 3,
        &journal, NULL, NULL) == BC250_VM_CPU_INVALID);
    CHECK(Digest(&ctx) == before);
    CHECK(Bc250VmCpuMap(&ctx, 0, &page, 1, 1, 3,
        (BC250_VM_CPU_JOURNAL *)ctx.Root, NULL, NULL) == BC250_VM_CPU_INVALID);
    CHECK(Digest(&ctx) == before);
    CHECK(Bc250VmCpuRead(&ctx, 0, &leaf->Data.Values[0]) == BC250_VM_CPU_INVALID);
    CHECK(Digest(&ctx) == before);
    maximum = ctx.MaximumDmaLast;
    CHECK(Bc250VmCpuRead(&ctx, 0, &ctx.MaximumDmaLast) == BC250_VM_CPU_INVALID);
    CHECK(ctx.MaximumDmaLast == maximum && Digest(&ctx) == before);
    CHECK(Bc250VmCpuMap(&ctx, 0, &page, 1, 1, 3,
        (BC250_VM_CPU_JOURNAL *)&ctx, NULL, NULL) == BC250_VM_CPU_INVALID);
    CHECK(Digest(&ctx) == before);
    End(&heap, &ctx);
}

static void InitFailureAndPartialRoot(void)
{
    TEST_HEAP heap;
    BC250_VM_CPU_CONTEXT ctx;
    BC250_VM_LAYOUT layout;
    BC250_VM_CPU_JOURNAL journal;
    BC250_ADDRESS_SPAN page;
    BC250_GART_U64 value = 0xBAD;
    memset(&heap, 0, sizeof(heap)); memset(&ctx, 0, sizeof(ctx));
    heap.Context = &ctx; heap.FailAllocation = 1; heap.Reentry = 1;
    CHECK(Bc250VmPlanLayout(1, 9, 1ULL << 36, &layout));
    CHECK(Bc250VmCpuInit(&ctx, &layout, 0x0000FFFFFFFFFFFFULL, 256,
        Allocate, Release, &heap) == BC250_VM_CPU_NO_MEMORY);
    CHECK(heap.Attempts == 1 && heap.Live == 0 && ctx.Root == NULL && ctx.OwnedNodes == 0);
    CHECK(heap.NestedCalls == 1);
    CHECK(Bc250VmCpuShutdown(&ctx) == BC250_VM_CPU_OK); FinishHeap(&heap);
    Start(&heap, &ctx, 1, (1ULL << 27) + 1, 256);
    Page(&page, 0x55000);
    CHECK(Bc250VmCpuMap(&ctx, 1ULL << 39, &page, 1, 1, 3,
        &journal, NULL, NULL) == BC250_VM_CPU_OK);
    CheckValue(&ctx, 1ULL << 39, Expected(&page, 3));
    {
        BC250_VM_CPU_NODE *owned_child = ctx.Root->Data.Children[1];
        unsigned int live = heap.Live, frees = heap.Frees, owned = ctx.OwnedNodes;
        CHECK(ctx.Layout.RootEntries == 2 && owned_child != NULL);
        ctx.Root->Data.Children[2] = owned_child; /* outside this partial root */
        CHECK(Bc250VmCpuShutdown(&ctx) == BC250_VM_CPU_CORRUPT);
        CHECK(heap.Live == live && heap.Frees == frees && ctx.Root != NULL);
        ctx.Root->Data.Children[2] = NULL;
        ctx.OwnedNodes = owned + 1;
        CHECK(Bc250VmCpuShutdown(&ctx) == BC250_VM_CPU_CORRUPT);
        CHECK(heap.Live == live && heap.Frees == frees && ctx.Root != NULL);
        ctx.OwnedNodes = owned;
        CheckValue(&ctx, 1ULL << 39, Expected(&page, 3));
    }
    CHECK(Bc250VmCpuMap(&ctx, (1ULL << 39) + 4096, &page, 1, 1, 3,
        &journal, NULL, NULL) == BC250_VM_CPU_INVALID);
    CHECK(Bc250VmCpuRead(&ctx, (1ULL << 39) + 4096, &value) == BC250_VM_CPU_INVALID);
    CHECK(value == 0xBAD);
    End(&heap, &ctx);
}

static void InvalidInitControls(void)
{
    TEST_HEAP heap;
    BC250_VM_CPU_CONTEXT ctx, pristine;
    BC250_VM_LAYOUT layout, bad;
    BC250_VM_CPU_JOURNAL journal;
    BC250_ADDRESS_SPAN page;
    BC250_GART_U64 read = ~0ULL;
    unsigned int live, frees;
    memset(&heap, 0, sizeof(heap)); memset(&ctx, 0, sizeof(ctx));
    heap.Context = &ctx; memset(&pristine, 0, sizeof(pristine));
    CHECK(Bc250VmPlanLayout(1, 9, 1ULL << 36, &layout));
#define INIT_REFUSE(context_pointer, layout_pointer, limit, budget, alloc, release) do { \
    CHECK(Bc250VmCpuInit(context_pointer, layout_pointer, limit, budget, alloc, release, \
        &heap) == BC250_VM_CPU_INVALID); \
    CHECK(heap.Attempts == 0 && heap.Live == 0 && heap.Frees == 0); \
    CHECK(memcmp(&ctx, &pristine, sizeof(ctx)) == 0); } while (0)
    INIT_REFUSE(NULL, &layout, BC250_VM_MAX_ADDRESS, 256, Allocate, Release);
    INIT_REFUSE(&ctx, NULL, BC250_VM_MAX_ADDRESS, 256, Allocate, Release);
    bad = layout; --bad.RootEntries;
    INIT_REFUSE(&ctx, &bad, BC250_VM_MAX_ADDRESS, 256, Allocate, Release);
    INIT_REFUSE(&ctx, &layout, BC250_VM_MAX_ADDRESS, 0, Allocate, Release);
    INIT_REFUSE(&ctx, &layout, BC250_VM_MAX_ADDRESS, 4097, Allocate, Release);
    INIT_REFUSE(&ctx, &layout, 1ULL << 48, 256, Allocate, Release);
    INIT_REFUSE(&ctx, &layout, BC250_VM_MAX_ADDRESS, 256, NULL, Release);
    INIT_REFUSE(&ctx, &layout, BC250_VM_MAX_ADDRESS, 256, Allocate, NULL);
#undef INIT_REFUSE
    Start(&heap, &ctx, 1, 1ULL << 36, 256);
    CHECK(Bc250VmCpuRead(&ctx, 0, &read) == BC250_VM_CPU_OK);
    CHECK(read == 0 && heap.Attempts == 0 && heap.Live == 1);
    Page(&page, 0x44000);
    live = heap.Live; frees = heap.Frees;
    ctx.State = 99;
    CHECK(Bc250VmCpuMap(&ctx, 0, &page, 1, 1, 3,
        &journal, NULL, NULL) == BC250_VM_CPU_INVALID);
    read = ~0ULL;
    CHECK(Bc250VmCpuRead(&ctx, 0, &read) == BC250_VM_CPU_INVALID);
    CHECK(read == ~0ULL);
    CHECK(Bc250VmCpuShutdown(&ctx) == BC250_VM_CPU_INVALID);
    CHECK(heap.Live == live && heap.Frees == frees && ctx.State == 99);
    ctx.State = BC250_VM_CPU_EMPTY;
    CHECK(Bc250VmCpuShutdown(&ctx) == BC250_VM_CPU_CORRUPT);
    CHECK(heap.Live == live && heap.Frees == frees && ctx.Root != NULL);
    ctx.State = BC250_VM_CPU_READY;
    End(&heap, &ctx);
}

int main(void)
{
    BoundaryAndShuffled(); AllocationFailures(); AfterWriteFailures();
    InvalidAndCapacity(); ForeignAndCorruption(); KnownStorageOverlap();
    InitFailureAndPartialRoot(); InvalidInitControls();
    printf("PASS: %u CPU tree assertions; NOT DMA/GPU/Windows ownership proof.\n", assertions);
    return 0;
}
