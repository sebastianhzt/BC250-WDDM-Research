/* SPDX-License-Identifier: Apache-2.0
 * CPU-only transactional removal. No GPU, DMA or OS backing operation.
 * Inherits all exclusive access / valid pointer / infallible Free contracts
 * of bc250_vm_cpu_backend.h. Hooks run once per requested page, including
 * holes. Cancellation ends BEFORE pruning; pruning cannot be cancelled.
 */
#ifndef BC250_VM_CPU_UNMAP_H
#define BC250_VM_CPU_UNMAP_H
#include "vm-cpu-backend-20261003/bc250_vm_cpu_backend.h"

/* Internal: the entire tree was validated while BUSY, before any write/free.
 * Unlink before Free; never dereference a freed node. A nonzero leaf word is
 * retained even if its VALID bit is unset. Root is retained by the caller.
 */
static __inline int Bc250VmCpuPruneEmpty(BC250_VM_CPU_CONTEXT *ctx,
    BC250_VM_CPU_NODE *node)
{
    unsigned int i;
    int empty = 1;
    if (node->Level == BC250_VM_PTB) {
        for (i = 0U; i < 512U; ++i)
            if (node->Data.Values[i] != 0ULL) empty = 0;
    } else {
        for (i = 0U; i < 512U; ++i) {
            BC250_VM_CPU_NODE *child = node->Data.Children[i];
            if (child && Bc250VmCpuPruneEmpty(ctx, child)) {
                node->Data.Children[i] = NULL;
                ctx->Free(ctx->CallbackContext, child);
                --ctx->OwnedNodes;
            }
            if (node->Data.Children[i]) empty = 0;
        }
    }
    return empty;
}

static __inline BC250_VM_CPU_STATUS Bc250VmCpuUnmap(
    BC250_VM_CPU_CONTEXT *ctx, BC250_GART_U64 first_va, unsigned int count,
    BC250_VM_CPU_JOURNAL *journal, BC250_VM_CPU_AFTER_WRITE after_write,
    void *hook_context)
{
    BC250_VM_CPU_STATUS result = BC250_VM_CPU_INVALID;
    BC250_GART_U64 first_pfn = first_va >> 12U;
    unsigned int page, level;
    int journal_started = 0;
    if (!ctx) return BC250_VM_CPU_INVALID;
    if (ctx->State == BC250_VM_CPU_BUSY) return BC250_VM_CPU_BUSY_RESULT;
    if (ctx->State != BC250_VM_CPU_READY || !journal || !count ||
        count > BC250_VM_CPU_MAX_BATCH || (first_va & 4095ULL) ||
        first_pfn >= ctx->Layout.MaxPfn || count > ctx->Layout.MaxPfn - first_pfn ||
        !Bc250DmaPageListDisjoint(journal, sizeof(*journal), ctx, sizeof(*ctx)))
        return BC250_VM_CPU_INVALID;
    ctx->State = BC250_VM_CPU_BUSY;
    if (!Bc250VmCpuValidateTree(ctx)) { result = BC250_VM_CPU_CORRUPT; goto done; }
    if (!Bc250VmCpuBufferAwayFromNodes(ctx->Root, journal, sizeof(*journal))) goto done;
    memset(journal, 0, sizeof(*journal));
    journal_started = 1;
    /* Prepare every existing slot before the first clear; holes allocate nothing. */
    for (page = 0U; page < count; ++page) {
        BC250_VM_INDICES indices;
        BC250_VM_CPU_NODE *node = ctx->Root;
        if (!Bc250VmDecode(&ctx->Layout,
            first_va + (BC250_GART_U64)page * 4096ULL, &indices)) goto rollback;
        for (level = ctx->Layout.Root; level < BC250_VM_PTB && node; ++level)
            node = node->Data.Children[indices.Index[level]];
        if (node) {
            journal->Leaves[page].Slot = &node->Data.Values[indices.Index[BC250_VM_PTB]];
            journal->Leaves[page].OldValue = *journal->Leaves[page].Slot;
        }
        ++journal->LeafCount;
    }
    for (page = 0U; page < count; ++page) {
        if (journal->Leaves[page].Slot) *journal->Leaves[page].Slot = 0ULL;
        if (after_write && !after_write(hook_context, page + 1U)) {
            result = BC250_VM_CPU_FAULT; goto rollback;
        }
    }
    /* Commit point: no fallible operation remains. Only owned, zero nodes
     * are freed, postorder, including previously empty unrelated branches. */
    (void)Bc250VmCpuPruneEmpty(ctx, ctx->Root);
    result = BC250_VM_CPU_OK;
    goto done;
rollback:
    for (page = 0U; page < journal->LeafCount; ++page)
        if (journal->Leaves[page].Slot)
            *journal->Leaves[page].Slot = journal->Leaves[page].OldValue;
done:
    if (journal_started) memset(journal, 0, sizeof(*journal));
    ctx->State = BC250_VM_CPU_READY;
    return result;
}
#endif
