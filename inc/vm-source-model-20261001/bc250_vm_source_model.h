/* SPDX-License-Identifier: Apache-2.0
 * Offline arithmetic/encoding subset from pinned AMD facts, NOT a GPU mapper.
 * Only 9-bit leaves, PDB2/PDB1/PDB0 roots, system DMA numbers, NC and 4 KiB pages.
 * Tags/limits are caller descriptions; no Windows DMA ownership is established.
 * No VRAM conversion, translate_further, huge pages, TMZ, fragment or memory-type policy.
 * No kernel consumer or permission to publish these results exists.
 */
#ifndef BC250_VM_SOURCE_MODEL_H
#define BC250_VM_SOURCE_MODEL_H
#include "gart-geometry-20261001/bc250_gart_geometry.h"

#define BC250_VM_PDB2 1U
#define BC250_VM_PDB1 2U
#define BC250_VM_PDB0 3U
#define BC250_VM_PTB 4U
#define BC250_VM_PAGE_SHIFT 12U
#define BC250_VM_PTE_VALID (1ULL << 0)
#define BC250_VM_PTE_SYSTEM (1ULL << 1)
#define BC250_VM_PTE_SNOOPED (1ULL << 2)
#define BC250_VM_PTE_TMZ (1ULL << 3)
#define BC250_VM_PTE_EXECUTABLE (1ULL << 4)
#define BC250_VM_PTE_READABLE (1ULL << 5)
#define BC250_VM_PTE_WRITEABLE (1ULL << 6)
#define BC250_VM_PTE_ADDR_MASK 0x0000FFFFFFFFF000ULL
#define BC250_VM_PDE_ADDR_MASK 0x0000FFFFFFFFFFC0ULL
#define BC250_VM_MAX_ADDRESS 0x0000FFFFFFFFFFFFULL

#define BC250_VM_ACCESS_READ 1U
#define BC250_VM_ACCESS_WRITE 2U
#define BC250_VM_ACCESS_EXECUTE 4U
#define BC250_VM_ACCESS_SNOOP 8U

typedef struct BC250_VM_LAYOUT {
    unsigned int Root;
    unsigned int LeafBits;
    unsigned int RootEntries;
    BC250_GART_U64 MaxPfn; /* exclusive, already in GPU PAGE numbers */
} BC250_VM_LAYOUT;

static __inline unsigned int Bc250VmPfnShift(unsigned int level)
{
    if (level == BC250_VM_PTB) return 0U;
    if (level < BC250_VM_PDB2 || level > BC250_VM_PDB0) return 0xFFFFFFFFU;
    return 9U * (BC250_VM_PDB0 - level) + 9U;
}

static __inline int Bc250VmPlanLayout(unsigned int root, unsigned int leaf_bits,
                                    BC250_GART_U64 max_pfn, BC250_VM_LAYOUT *out)
{
    BC250_VM_LAYOUT plan = {0};
    unsigned int shift;
    if (!out || root < BC250_VM_PDB2 || root > BC250_VM_PDB0 || leaf_bits != 9U ||
        !max_pfn || max_pfn > (1ULL << 36)) return 0;
    shift = Bc250VmPfnShift(root);
    if (max_pfn > (1ULL << (shift + 9U))) return 0;
    plan.Root = root; plan.LeafBits = leaf_bits; plan.MaxPfn = max_pfn;
    plan.RootEntries = (unsigned int)((max_pfn - 1ULL) / (1ULL << shift) + 1ULL);
    *out = plan;
    return 1;
}

typedef struct BC250_VM_INDICES {
    unsigned int Index[5]; /* AMD level ordinals; entries before root are zero */
    unsigned int Offset;
} BC250_VM_INDICES;

static __inline int Bc250VmDecode(const BC250_VM_LAYOUT *layout, BC250_GART_U64 va,
                                BC250_VM_INDICES *out)
{
    BC250_VM_LAYOUT check;
    BC250_VM_INDICES indices = {{0}, 0};
    BC250_GART_U64 pfn = va >> BC250_VM_PAGE_SHIFT;
    unsigned int level;
    if (!out || !layout || !Bc250VmPlanLayout(layout->Root, layout->LeafBits,
        layout->MaxPfn, &check) || check.RootEntries != layout->RootEntries ||
        pfn >= layout->MaxPfn) return 0;
    for (level = layout->Root; level <= BC250_VM_PTB; ++level) {
        BC250_GART_U64 index = pfn >> Bc250VmPfnShift(level);
        if (level != layout->Root) index &= 511ULL;
        indices.Index[level] = (unsigned int)index;
    }
    indices.Offset = (unsigned int)(va & 4095ULL);
    *out = indices;
    return 1;
}

/* Stronger than source PDE 64-byte alignment: this subset uses 4 KiB tables.
 * Caller must obtain a real DMA mapping before any future runtime use.
 */
static __inline int Bc250VmSystemDmaPageNumbers(const BC250_ADDRESS_SPAN *dma,
                                               BC250_GART_U64 maximum_dma_last)
{
    return dma && dma->Bytes == 4096ULL && !(dma->Start & 4095ULL) &&
        !(dma->Start & ~BC250_VM_PTE_ADDR_MASK) &&
        maximum_dma_last <= BC250_VM_MAX_ADDRESS &&
        Bc250AddressNumbersMatchDomain(dma, BC250_ADDRESS_DMA_LOGICAL, maximum_dma_last);
}

static __inline int Bc250VmEncodeSystemLeaf(const BC250_ADDRESS_SPAN *dma,
    BC250_GART_U64 maximum_dma_last, unsigned int access, BC250_GART_U64 *out)
{
    BC250_GART_U64 value;
    if (!out || (access & ~15U) || !Bc250VmSystemDmaPageNumbers(dma, maximum_dma_last)) return 0;
    value = dma->Start | BC250_VM_PTE_VALID | BC250_VM_PTE_SYSTEM; /* NC=0 */
    if (access & BC250_VM_ACCESS_READ) value |= BC250_VM_PTE_READABLE;
    if (access & BC250_VM_ACCESS_WRITE) value |= BC250_VM_PTE_WRITEABLE;
    if (access & BC250_VM_ACCESS_EXECUTE) value |= BC250_VM_PTE_EXECUTABLE;
    if (access & BC250_VM_ACCESS_SNOOP) value |= BC250_VM_PTE_SNOOPED;
    *out = value;
    return 1;
}

static __inline int Bc250VmEncodeSystemDirectory(const BC250_ADDRESS_SPAN *dma,
    BC250_GART_U64 maximum_dma_last, unsigned int level, unsigned int translate_further,
    BC250_GART_U64 *out)
{
    if (!out || level < BC250_VM_PDB2 || level > BC250_VM_PDB0 || translate_further ||
        !Bc250VmSystemDmaPageNumbers(dma, maximum_dma_last)) return 0;
    *out = dma->Start | BC250_VM_PTE_VALID | BC250_VM_PTE_SYSTEM;
    return 1;
}

#endif
