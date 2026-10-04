/* SPDX-License-Identifier: Apache-2.0
 * Arithmetic-only CPU planner: 4 KiB GPU pages and 8-byte GART entries.
 * No PTE encoding, DMA mapping, ownership, GPU address-width or MMIO validation.
 * Address domain tags describe caller intent, NOT trusted mapping evidence.
 */
#ifndef BC250_GART_GEOMETRY_H
#define BC250_GART_GEOMETRY_H

typedef unsigned long long BC250_GART_U64;
#define BC250_GART_PAGE_BYTES 4096U
#define BC250_GART_ENTRY_BYTES 8U
#define BC250_GART_U32_MAX 0xFFFFFFFFU
#define BC250_GART_U64_MAX (~0ULL)

typedef struct BC250_GART_GEOMETRY {
    unsigned int Entries;
    unsigned int TableBytes;
    unsigned int AllocationBytes;
    unsigned int BitmapWords;
    BC250_GART_U64 CoverageBytes;
} BC250_GART_GEOMETRY;

static __inline int Bc250GartPlan(
    unsigned int entries, unsigned int bitmap_capacity_entries,
    unsigned int maximum_allocation_bytes, BC250_GART_U64 requested_coverage,
    BC250_GART_GEOMETRY *out)
{
    BC250_GART_GEOMETRY plan;
    BC250_GART_U64 table_bytes, allocation_bytes, coverage;
    if (!out || !entries || entries > bitmap_capacity_entries) return 0;
    table_bytes = (BC250_GART_U64)entries * BC250_GART_ENTRY_BYTES;
    allocation_bytes = (table_bytes + BC250_GART_PAGE_BYTES - 1ULL) &
                       ~(BC250_GART_U64)(BC250_GART_PAGE_BYTES - 1U);
    coverage = (BC250_GART_U64)entries * BC250_GART_PAGE_BYTES;
    if (allocation_bytes > BC250_GART_U32_MAX ||
        allocation_bytes > maximum_allocation_bytes || coverage != requested_coverage)
        return 0;
    plan.Entries = entries;
    plan.TableBytes = (unsigned int)table_bytes;
    plan.AllocationBytes = (unsigned int)allocation_bytes;
    /* Quotient/remainder avoids entries+31 overflowing UINT32. */
    plan.BitmapWords = entries / 32U + ((entries % 32U) != 0U);
    plan.CoverageBytes = coverage;
    *out = plan;
    return 1;
}

/* Self-consistency only. Does not validate a buffer's actual capacity/owner. */
static __inline int Bc250GartGeometryConsistent(const BC250_GART_GEOMETRY *plan)
{
    BC250_GART_GEOMETRY rebuilt;
    return plan && Bc250GartPlan(plan->Entries, plan->Entries, BC250_GART_U32_MAX,
        plan->CoverageBytes, &rebuilt) &&
        plan->TableBytes == rebuilt.TableBytes &&
        plan->AllocationBytes == rebuilt.AllocationBytes &&
        plan->BitmapWords == rebuilt.BitmapWords;
}

typedef struct BC250_GART_SLOTS {
    unsigned int First;
    unsigned int Count;
    unsigned int TableByteOffset;
    unsigned int TableByteCount;
} BC250_GART_SLOTS;

/* No table access: describes the slots for a page-aligned covered subrange. */
static __inline int Bc250GartDescribeSlots(
    const BC250_GART_GEOMETRY *plan, BC250_GART_U64 offset_bytes,
    BC250_GART_U64 length_bytes, BC250_GART_SLOTS *out)
{
    BC250_GART_SLOTS slots;
    BC250_GART_U64 first, count;
    if (!out || !Bc250GartGeometryConsistent(plan) || !length_bytes ||
        (offset_bytes & (BC250_GART_PAGE_BYTES - 1U)) ||
        (length_bytes & (BC250_GART_PAGE_BYTES - 1U)) ||
        offset_bytes >= plan->CoverageBytes ||
        length_bytes > plan->CoverageBytes - offset_bytes) return 0;
    first = offset_bytes / BC250_GART_PAGE_BYTES;
    count = length_bytes / BC250_GART_PAGE_BYTES;
    slots.First = (unsigned int)first;
    slots.Count = (unsigned int)count;
    slots.TableByteOffset = (unsigned int)(first * BC250_GART_ENTRY_BYTES);
    slots.TableByteCount = (unsigned int)(count * BC250_GART_ENTRY_BYTES);
    *out = slots;
    return 1;
}

#define BC250_ADDRESS_CPU_VIRTUAL 1U
#define BC250_ADDRESS_CPU_PHYSICAL 2U
#define BC250_ADDRESS_DMA_LOGICAL 3U
#define BC250_ADDRESS_GPU_VIRTUAL 4U
#define BC250_ADDRESS_VRAM_MC 5U

typedef struct BC250_ADDRESS_SPAN {
    unsigned int Domain;
    BC250_GART_U64 Start;
    BC250_GART_U64 Bytes;
    BC250_GART_U64 Last; /* inclusive: avoids wrapping an exclusive UINT64 end */
} BC250_ADDRESS_SPAN;

static __inline int Bc250AddressDomainKnown(unsigned int domain)
{
    return domain >= BC250_ADDRESS_CPU_VIRTUAL && domain <= BC250_ADDRESS_VRAM_MC;
}

/* Numeric bounds only. maximum_last is caller policy, NOT hardware discovery. */
static __inline int Bc250DescribeAddressNumbers(
    unsigned int domain, BC250_GART_U64 start, BC250_GART_U64 bytes,
    BC250_GART_U64 maximum_last, BC250_ADDRESS_SPAN *out)
{
    BC250_ADDRESS_SPAN span;
    if (!out || !Bc250AddressDomainKnown(domain) || !bytes || start > maximum_last ||
        bytes - 1ULL > maximum_last - start) return 0;
    span.Domain = domain;
    span.Start = start;
    span.Bytes = bytes;
    span.Last = start + (bytes - 1ULL);
    *out = span;
    return 1;
}

/* Same numeric address in a different domain is deliberately NOT interchangeable. */
static __inline int Bc250AddressNumbersMatchDomain(
    const BC250_ADDRESS_SPAN *span, unsigned int expected_domain,
    BC250_GART_U64 maximum_last)
{
    BC250_ADDRESS_SPAN rebuilt;
    return span && span->Domain == expected_domain &&
        Bc250DescribeAddressNumbers(expected_domain, span->Start, span->Bytes,
                                    maximum_last, &rebuilt) && span->Last == rebuilt.Last;
}

#endif
