/* SPDX-License-Identifier: Apache-2.0
 * CPU-only sparse VM storage backend, NOT a GPU mapper or 4KiB DMA table.
 * Reuses the bounded source codec; directory links are CPU pointers, not PDEs.
 * The context MUST start zeroed. Caller owns exclusive access for EVERY call,
 * including callbacks: BUSY rejects API reentry, NOT concurrent/raw mutation.
 * Allocate returns fresh, valid, disjoint metadata of requested sizeof(node).
 * Free is infallible; callbacks must not mutate the context/tree/input/journal.
 * All buffers/nodes are caller-valid, disjoint and stable for the whole call.
 * No OS allocation, kernel consumer, MMIO, DMA proof or runtime permission.
 */
#ifndef BC250_VM_CPU_BACKEND_H
#define BC250_VM_CPU_BACKEND_H
#include <stddef.h>
#include <string.h>
#include "upstream-integration-20261003/bc250_dma_page_list.h"

#define BC250_VM_CPU_MAX_BATCH 64U
#define BC250_VM_CPU_MAX_LINKS (BC250_VM_CPU_MAX_BATCH * 3U)
#define BC250_VM_CPU_EMPTY 0U
#define BC250_VM_CPU_READY 1U
#define BC250_VM_CPU_BUSY 2U
typedef enum BC250_VM_CPU_STATUS {
    BC250_VM_CPU_OK, BC250_VM_CPU_INVALID, BC250_VM_CPU_BUSY_RESULT,
    BC250_VM_CPU_NO_MEMORY, BC250_VM_CPU_FAULT, BC250_VM_CPU_CORRUPT
} BC250_VM_CPU_STATUS;
typedef struct BC250_VM_CPU_NODE BC250_VM_CPU_NODE;
typedef struct BC250_VM_CPU_CONTEXT BC250_VM_CPU_CONTEXT;
struct BC250_VM_CPU_NODE {
    BC250_VM_CPU_CONTEXT *OriginOwner;
    BC250_VM_CPU_NODE *Parent;
    unsigned int ParentSlot, Level;
    union { BC250_VM_CPU_NODE *Children[512]; BC250_GART_U64 Values[512]; } Data;
};
typedef BC250_VM_CPU_NODE *(*BC250_VM_CPU_ALLOC)(void *, size_t);
typedef void (*BC250_VM_CPU_FREE)(void *, BC250_VM_CPU_NODE *);
typedef int (*BC250_VM_CPU_AFTER_WRITE)(void *, unsigned int);
struct BC250_VM_CPU_CONTEXT {
    BC250_VM_LAYOUT Layout;
    BC250_GART_U64 MaximumDmaLast;
    unsigned int MaximumNodes, OwnedNodes, State;
    BC250_VM_CPU_NODE *Root;
    BC250_VM_CPU_ALLOC Allocate;
    BC250_VM_CPU_FREE Free;
    void *CallbackContext;
};
typedef struct BC250_VM_CPU_JOURNAL {
    BC250_GART_U64 Encoded[64];
    struct { BC250_GART_U64 *Slot; BC250_GART_U64 OldValue; } Leaves[64];
    struct { BC250_VM_CPU_NODE *Parent, *Child; unsigned int Slot; } Links[192];
    unsigned int LeafCount, LinkCount;
} BC250_VM_CPU_JOURNAL;

static __inline int Bc250VmCpuNodeMatches(const BC250_VM_CPU_CONTEXT *ctx,
    const BC250_VM_CPU_NODE *node, const BC250_VM_CPU_NODE *parent,
    unsigned int slot, unsigned int level)
{
    return node && node->OriginOwner == ctx && node->Level == level &&
        node->Parent == parent && node->ParentSlot == slot;
}

/* Strict levels + unique parent/slot prohibit cycles and same-owner aliases.
 * Pointer validity remains a caller contract: no probing untrusted addresses.
 */
static __inline int Bc250VmCpuValidateNode(const BC250_VM_CPU_CONTEXT *ctx,
    const BC250_VM_CPU_NODE *node, const BC250_VM_CPU_NODE *parent,
    unsigned int slot, unsigned int level, unsigned int *seen)
{
    unsigned int i, entries;
    if (!Bc250VmCpuNodeMatches(ctx, node, parent, slot, level) ||
        *seen >= ctx->OwnedNodes) return 0;
    ++*seen;
    if (level == BC250_VM_PTB) return 1;
    entries = parent ? 512U : ctx->Layout.RootEntries;
    for (i = 0; i < 512U; ++i) {
        if (node->Data.Children[i]) {
            if (i >= entries || !Bc250VmCpuValidateNode(ctx,
                node->Data.Children[i], node, i, level + 1U, seen)) return 0;
        }
    }
    return 1;
}

static __inline int Bc250VmCpuValidateTree(const BC250_VM_CPU_CONTEXT *ctx)
{
    BC250_VM_LAYOUT layout;
    unsigned int seen = 0U;
    if (!ctx || !ctx->Allocate || !ctx->Free || !ctx->MaximumNodes ||
        ctx->MaximumNodes > 4096U || !ctx->OwnedNodes ||
        ctx->OwnedNodes > ctx->MaximumNodes || ctx->MaximumDmaLast > BC250_VM_MAX_ADDRESS ||
        !Bc250VmPlanLayout(ctx->Layout.Root, ctx->Layout.LeafBits,
            ctx->Layout.MaxPfn, &layout) || layout.RootEntries != ctx->Layout.RootEntries)
        return 0;
    return Bc250VmCpuValidateNode(ctx, ctx->Root, NULL, 0U, layout.Root, &seen) &&
        seen == ctx->OwnedNodes;
}

/* Internal helper: caller has validated the entire immutable tree first. */
static __inline int Bc250VmCpuBufferAwayFromNodes(const BC250_VM_CPU_NODE *node,
    const void *buffer, BC250_GART_U64 bytes)
{
    unsigned int i;
    if (!Bc250DmaPageListDisjoint(node, sizeof(*node), buffer, bytes)) return 0;
    if (node->Level < BC250_VM_PTB) {
        for (i = 0U; i < 512U; ++i) {
            if (node->Data.Children[i] && !Bc250VmCpuBufferAwayFromNodes(
                node->Data.Children[i], buffer, bytes)) return 0;
        }
    }
    return 1;
}

static __inline BC250_VM_CPU_STATUS Bc250VmCpuInit(BC250_VM_CPU_CONTEXT *ctx,
    const BC250_VM_LAYOUT *layout, BC250_GART_U64 maximum_dma_last,
    unsigned int maximum_nodes, BC250_VM_CPU_ALLOC allocate,
    BC250_VM_CPU_FREE release, void *callback_context)
{
    BC250_VM_LAYOUT verified;
    BC250_VM_CPU_NODE *node;
    if (!ctx || !layout) return BC250_VM_CPU_INVALID;
    if (ctx->State == BC250_VM_CPU_BUSY) return BC250_VM_CPU_BUSY_RESULT;
    if (ctx->State != BC250_VM_CPU_EMPTY || ctx->Root || ctx->OwnedNodes ||
        !allocate || !release || !maximum_nodes || maximum_nodes > 4096U ||
        maximum_dma_last > BC250_VM_MAX_ADDRESS ||
        !Bc250VmPlanLayout(layout->Root, layout->LeafBits, layout->MaxPfn, &verified) ||
        verified.RootEntries != layout->RootEntries) return BC250_VM_CPU_INVALID;
    ctx->State = BC250_VM_CPU_BUSY;
    node = allocate(callback_context, sizeof(*node));
    if (!node) { ctx->State = BC250_VM_CPU_EMPTY; return BC250_VM_CPU_NO_MEMORY; }
    memset(node, 0, sizeof(*node));
    node->OriginOwner = ctx; node->Level = verified.Root;
    ctx->Layout = verified; ctx->MaximumDmaLast = maximum_dma_last;
    ctx->MaximumNodes = maximum_nodes; ctx->OwnedNodes = 1U; ctx->Root = node;
    ctx->Allocate = allocate; ctx->Free = release; ctx->CallbackContext = callback_context;
    ctx->State = BC250_VM_CPU_READY;
    return BC250_VM_CPU_OK;
}

static __inline BC250_VM_CPU_STATUS Bc250VmCpuMap(BC250_VM_CPU_CONTEXT *ctx,
    BC250_GART_U64 first_va, const BC250_ADDRESS_SPAN *pages,
    unsigned int count, unsigned int input_capacity, unsigned int access,
    BC250_VM_CPU_JOURNAL *journal, BC250_VM_CPU_AFTER_WRITE after_write, void *hook_context)
{
    BC250_GART_GEOMETRY geometry;
    BC250_VM_INDICES indices;
    BC250_VM_CPU_STATUS result = BC250_VM_CPU_INVALID;
    BC250_GART_U64 first_pfn = first_va >> 12U;
    unsigned int page, level;
    int journal_started = 0;
    if (!ctx) return BC250_VM_CPU_INVALID;
    if (ctx->State == BC250_VM_CPU_BUSY) return BC250_VM_CPU_BUSY_RESULT;
    if (ctx->State != BC250_VM_CPU_READY || !journal || !pages || !count ||
        count > BC250_VM_CPU_MAX_BATCH || count > input_capacity || (first_va & 4095ULL) ||
        first_pfn >= ctx->Layout.MaxPfn || count > ctx->Layout.MaxPfn - first_pfn ||
        !Bc250DmaPageListDisjoint(journal, sizeof(*journal), ctx, sizeof(*ctx)) ||
        !Bc250DmaPageListDisjoint(pages, (BC250_GART_U64)count * sizeof(*pages), journal, sizeof(*journal)) ||
        !Bc250DmaPageListDisjoint(pages, (BC250_GART_U64)count * sizeof(*pages), ctx, sizeof(*ctx)))
        return BC250_VM_CPU_INVALID;
    ctx->State = BC250_VM_CPU_BUSY;
    if (!Bc250VmCpuValidateTree(ctx)) { result = BC250_VM_CPU_CORRUPT; goto done; }
    if (!Bc250VmCpuBufferAwayFromNodes(ctx->Root, journal, sizeof(*journal)) ||
        !Bc250VmCpuBufferAwayFromNodes(ctx->Root, pages, (BC250_GART_U64)count * sizeof(*pages)))
        goto done;
    memset(journal, 0, sizeof(*journal));
    journal_started = 1;
    if (!Bc250GartPlan(count, count, 4096U, (BC250_GART_U64)count * 4096ULL, &geometry) ||
        !Bc250DmaPageListEncode(&geometry, 0ULL, (BC250_GART_U64)count * 4096ULL,
            pages, count, input_capacity, ctx->MaximumDmaLast, access,
            journal->Encoded, BC250_VM_CPU_MAX_BATCH)) goto done;
    /* Prepare every path first, recording previous leaves without writing them.
     * Target VAs are consecutive unique pages, never duplicate leaf slots. */
    for (page = 0U; page < count; ++page) {
        BC250_VM_CPU_NODE *node = ctx->Root;
        if (!Bc250VmDecode(&ctx->Layout, first_va + (BC250_GART_U64)page * 4096ULL, &indices))
            goto rollback;
        for (level = ctx->Layout.Root; level < BC250_VM_PTB; ++level) {
            unsigned int slot = indices.Index[level];
            if (!node->Data.Children[slot]) {
                BC250_VM_CPU_NODE *created;
                if (ctx->OwnedNodes >= ctx->MaximumNodes || journal->LinkCount >= BC250_VM_CPU_MAX_LINKS) {
                    result = BC250_VM_CPU_NO_MEMORY; goto rollback;
                }
                created = ctx->Allocate(ctx->CallbackContext, sizeof(*created));
                if (!created) { result = BC250_VM_CPU_NO_MEMORY; goto rollback; }
                memset(created, 0, sizeof(*created));
                created->OriginOwner = ctx; created->Parent = node;
                created->ParentSlot = slot; created->Level = level + 1U;
                journal->Links[journal->LinkCount].Parent = node;
                journal->Links[journal->LinkCount].Child = created;
                journal->Links[journal->LinkCount].Slot = slot;
                ++journal->LinkCount; ++ctx->OwnedNodes;
                node->Data.Children[slot] = created;
            }
            node = node->Data.Children[slot];
        }
        journal->Leaves[page].Slot = &node->Data.Values[indices.Index[BC250_VM_PTB]];
        journal->Leaves[page].OldValue = *journal->Leaves[page].Slot;
        ++journal->LeafCount;
    }
    for (page = 0U; page < count; ++page) {
        *journal->Leaves[page].Slot = journal->Encoded[page];
        if (after_write && !after_write(hook_context, page + 1U)) {
            result = BC250_VM_CPU_FAULT; goto rollback;
        }
    }
    result = BC250_VM_CPU_OK;
    goto done;
rollback:
    /* Restore ALL old leaves before freeing any new owned storage. */
    while (journal->LeafCount) {
        --journal->LeafCount;
        *journal->Leaves[journal->LeafCount].Slot = journal->Leaves[journal->LeafCount].OldValue;
    }
    while (journal->LinkCount) {
        --journal->LinkCount;
        journal->Links[journal->LinkCount].Parent->Data.Children[journal->Links[journal->LinkCount].Slot] = NULL;
        ctx->Free(ctx->CallbackContext, journal->Links[journal->LinkCount].Child);
        --ctx->OwnedNodes;
    }
done:
    /* Scratch is not a persistent owner handle; remove stale freed pointers. */
    if (journal_started) memset(journal, 0, sizeof(*journal));
    ctx->State = BC250_VM_CPU_READY;
    return result;
}

static __inline BC250_VM_CPU_STATUS Bc250VmCpuRead(BC250_VM_CPU_CONTEXT *ctx,
    BC250_GART_U64 va, BC250_GART_U64 *value)
{
    BC250_VM_INDICES indices;
    BC250_VM_CPU_NODE *node;
    unsigned int level;
    if (!ctx) return BC250_VM_CPU_INVALID;
    if (ctx->State == BC250_VM_CPU_BUSY) return BC250_VM_CPU_BUSY_RESULT;
    if (ctx->State != BC250_VM_CPU_READY || !value ||
        !Bc250VmDecode(&ctx->Layout, va, &indices)) return BC250_VM_CPU_INVALID;
    if (!Bc250VmCpuValidateTree(ctx)) return BC250_VM_CPU_CORRUPT;
    if (!Bc250DmaPageListDisjoint(ctx, sizeof(*ctx), value, sizeof(*value)) ||
        !Bc250VmCpuBufferAwayFromNodes(ctx->Root, value, sizeof(*value)))
        return BC250_VM_CPU_INVALID;
    node = ctx->Root;
    for (level = ctx->Layout.Root; level < BC250_VM_PTB; ++level) {
        node = node->Data.Children[indices.Index[level]];
        if (!node) { *value = 0ULL; return BC250_VM_CPU_OK; }
    }
    *value = node->Data.Values[indices.Index[BC250_VM_PTB]];
    return BC250_VM_CPU_OK;
}

static __inline void Bc250VmCpuFreeValidated(BC250_VM_CPU_CONTEXT *ctx, BC250_VM_CPU_NODE *node)
{
    unsigned int i;
    if (node->Level < BC250_VM_PTB) {
        for (i = 0U; i < 512U; ++i) {
            if (node->Data.Children[i]) Bc250VmCpuFreeValidated(ctx, node->Data.Children[i]);
        }
    }
    ctx->Free(ctx->CallbackContext, node);
    --ctx->OwnedNodes;
}

static __inline BC250_VM_CPU_STATUS Bc250VmCpuShutdown(BC250_VM_CPU_CONTEXT *ctx)
{
    if (!ctx) return BC250_VM_CPU_INVALID;
    if (ctx->State == BC250_VM_CPU_BUSY) return BC250_VM_CPU_BUSY_RESULT;
    if (ctx->State == BC250_VM_CPU_EMPTY)
        return !ctx->Root && !ctx->OwnedNodes ? BC250_VM_CPU_OK : BC250_VM_CPU_CORRUPT;
    if (ctx->State != BC250_VM_CPU_READY) return BC250_VM_CPU_INVALID;
    ctx->State = BC250_VM_CPU_BUSY;
    if (!Bc250VmCpuValidateTree(ctx)) { ctx->State = BC250_VM_CPU_READY; return BC250_VM_CPU_CORRUPT; }
    /* Validation of the WHOLE tree completed before the FIRST free. */
    Bc250VmCpuFreeValidated(ctx, ctx->Root);
    memset(ctx, 0, sizeof(*ctx));
    return BC250_VM_CPU_OK;
}
#endif
