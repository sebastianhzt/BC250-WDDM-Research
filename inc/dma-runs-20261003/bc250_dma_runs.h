/* SPDX-License-Identifier: Apache-2.0
 * Original CPU-only normalization of caller-declared DMA runs. No Windows
 * SCATTER_GATHER_LIST/MDL consumer, address translation, allocation or GPU use.
 * Caller supplies valid, stable buffers and exclusive access. Numeric tags do
 * not prove ownership. Unaligned/partial pages are rejected, never rounded.
 * Segment order and duplicates are preserved; never sort or infer contiguity.
 */
#ifndef BC250_DMA_RUNS_H
#define BC250_DMA_RUNS_H
#include "domain-backend-20261003/bc250_vm_domain_backend.h"

static __inline int Bc250DmaRunsExpand(const BC250_AD_SPAN *runs,
    unsigned int count, unsigned int capacity, BC250_AD_U64 expected_bytes,
    BC250_AD_U64 maximum_last, BC250_AD_SPAN *pages,
    unsigned int page_capacity, unsigned int *out_count)
{
    BC250_AD_SPAN result[BC250_VM_CPU_MAX_BATCH];
    unsigned int i, j, total = 0U, index = 0U;
    if (!runs || !pages || !out_count || !count || count > BC250_VM_CPU_MAX_BATCH ||
        count > capacity || !expected_bytes || (expected_bytes & 4095ULL) ||
        expected_bytes > (BC250_AD_U64)BC250_VM_CPU_MAX_BATCH * 4096ULL ||
        maximum_last > BC250_AD_MAX48) return 0;
    /* Validate the ENTIRE list before publishing even its first page. */
    for (i = 0; i < count; ++i) {
        BC250_AD_U64 run_pages;
        if (!Bc250VmDomainSpanValid(&runs[i], BC250_AD_DMA_LOGICAL, maximum_last) ||
            (runs[i].Start & 4095ULL) || (runs[i].Bytes & 4095ULL)) return 0;
        run_pages = runs[i].Bytes >> 12U;
        if (run_pages > BC250_VM_CPU_MAX_BATCH - total) return 0;
        total += (unsigned int)run_pages;
    }
    if ((BC250_AD_U64)total * 4096ULL != expected_bytes || total > page_capacity ||
        !Bc250DmaPageListDisjoint(runs, (BC250_AD_U64)count * sizeof(*runs),
            pages, (BC250_AD_U64)total * sizeof(*pages)) ||
        !Bc250DmaPageListDisjoint(runs, (BC250_AD_U64)count * sizeof(*runs), out_count, sizeof(*out_count)) ||
        !Bc250DmaPageListDisjoint(pages, (BC250_AD_U64)total * sizeof(*pages), out_count, sizeof(*out_count))) return 0;
    memset(result, 0, sizeof(result));
    for (i = 0; i < count; ++i) {
        unsigned int run_pages = (unsigned int)(runs[i].Bytes >> 12U);
        for (j = 0; j < run_pages; ++j) {
            result[index].Domain = BC250_AD_DMA_LOGICAL;
            result[index].Start = runs[i].Start + (BC250_AD_U64)j * 4096ULL;
            result[index].Bytes = 4096ULL;
            result[index].Last = result[index].Start + 4095ULL;
            ++index;
        }
    }
    memcpy(pages, result, (size_t)total * sizeof(*pages));
    *out_count = total;
    return 1;
}

/* Registers numeric descriptors only. The caller still owns the OS mapping's
 * lifetime; this function cannot acquire/pin/release any Windows DMA resource.
 */
static __inline BC250_VM_SESSION_STATUS Bc250VmDomainRegisterRuns(
    BC250_VM_CPU_SESSION *session, const BC250_AD_SPAN *runs,
    unsigned int count, unsigned int capacity, BC250_AD_U64 expected_bytes,
    BC250_BACKING_HANDLE *out)
{
    BC250_AD_SPAN pages[BC250_VM_CPU_MAX_BATCH];
    unsigned int page_count;
    BC250_VM_SESSION_STATUS status = Bc250VmSessionCheck(session);
    if (status != BC250_VM_SESSION_OK) return status;
    if (!runs || !out || !count || count > BC250_VM_CPU_MAX_BATCH || count > capacity ||
        !Bc250VmSessionBufferAway(session, runs, (BC250_AD_U64)count * sizeof(*runs)) ||
        !Bc250VmSessionBufferAway(session, out, sizeof(*out)) ||
        !Bc250DmaPageListDisjoint(runs, (BC250_AD_U64)count * sizeof(*runs), out, sizeof(*out)))
        return BC250_VM_SESSION_INVALID;
    if (!Bc250DmaRunsExpand(runs, count, capacity, expected_bytes,
        session->Backend.MaximumDmaLast, pages, BC250_VM_CPU_MAX_BATCH, &page_count))
        return BC250_VM_SESSION_INVALID;
    return Bc250VmDomainRegister(session, pages, page_count, page_count, out);
}
#endif
