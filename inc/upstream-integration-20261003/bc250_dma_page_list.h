/* SPDX-License-Identifier: Apache-2.0
 * Independent offline page-list encoder, NOT a Windows DMA/GART mapper.
 * AMD amdgpu_gart_map distinguishes dma_addr[i] from the CPU table pointer;
 * each page can be discontiguous or alias another page. Source provenance is
 * recorded in research/windows/upstream-integration-20261003.
 * This deliberately retains the existing numeric-only NC leaf subset. It does
 * not adopt Linux's GART cache flags, publish PTEs, own memory or touch MMIO.
 * Caller MUST supply real array capacities and stable, exclusively held input
 * and output for the WHOLE call. Claimed capacities/tags are not OS evidence.
 * Concurrent mutation, stale pointers and dishonest capacities are out of scope.
 */
#ifndef BC250_DMA_PAGE_LIST_H
#define BC250_DMA_PAGE_LIST_H
#include <stdint.h>
#include "vm-source-model-20261001/bc250_vm_source_model.h"

/* Pointer geometry only; does not probe addresses or prove accessibility. */
static __inline int Bc250DmaPageListDisjoint(
    const void *input, BC250_GART_U64 input_bytes,
    const void *output, BC250_GART_U64 output_bytes)
{
    uintptr_t input_start, output_start, input_last, output_last;
    if (!input || !output || !input_bytes || !output_bytes) return 0;
    input_start = (uintptr_t)input;
    output_start = (uintptr_t)output;
    if (input_bytes - 1ULL > (BC250_GART_U64)(UINTPTR_MAX - input_start) ||
        output_bytes - 1ULL > (BC250_GART_U64)(UINTPTR_MAX - output_start)) return 0;
    input_last = input_start + (uintptr_t)(input_bytes - 1ULL);
    output_last = output_start + (uintptr_t)(output_bytes - 1ULL);
    return input_last < output_start || output_last < input_start;
}

/* Validates the complete source list BEFORE modifying any destination entry.
 * Returns zero with output untouched on validation failure, under the stable
 * caller-owned-array precondition. The rest of the table is preserved.
 * Repeated DMA pages are permitted: numeric aliasing is not an ownership proof.
 */
static __inline int Bc250DmaPageListEncode(
    const BC250_GART_GEOMETRY *plan, BC250_GART_U64 offset_bytes,
    BC250_GART_U64 length_bytes, const BC250_ADDRESS_SPAN *pages,
    unsigned int page_count, unsigned int input_capacity,
    BC250_GART_U64 maximum_dma_last, unsigned int access,
    BC250_GART_U64 *table, unsigned int table_capacity)
{
    BC250_GART_SLOTS slots;
    BC250_GART_U64 value, first_bytes;
    uintptr_t destination;
    unsigned int i;
    if (!pages || !table || !page_count || page_count > input_capacity ||
        !Bc250GartDescribeSlots(plan, offset_bytes, length_bytes, &slots) ||
        page_count != slots.Count || slots.First >= table_capacity ||
        slots.Count > table_capacity - slots.First || (access & ~15U)) return 0;
    first_bytes = (BC250_GART_U64)slots.First * sizeof(*table);
    destination = (uintptr_t)table;
    if (first_bytes > (BC250_GART_U64)(UINTPTR_MAX - destination)) return 0;
    destination += (uintptr_t)first_bytes;
    if (!Bc250DmaPageListDisjoint(pages,
        (BC250_GART_U64)page_count * sizeof(*pages), (const void *)destination,
        (BC250_GART_U64)slots.Count * sizeof(*table))) return 0;
    for (i = 0; i < page_count; ++i) {
        if (!Bc250VmEncodeSystemLeaf(&pages[i], maximum_dma_last, access, &value))
            return 0;
    }
    for (i = 0; i < page_count; ++i) {
        /* Stable input was validated above; no fallible callback or allocation. */
        (void)Bc250VmEncodeSystemLeaf(&pages[i], maximum_dma_last, access, &value);
        table[slots.First + i] = value;
    }
    return 1;
}
#endif
