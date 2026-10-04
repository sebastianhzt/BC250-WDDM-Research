/* SPDX-License-Identifier: Apache-2.0
 * Original domain-aware adapter to the CPU-only session. No OS/GPU consumer.
 * Caller inherits the session's exclusive-access, valid/stable/disjoint buffer
 * and callback contracts. Tags are numeric caller declarations, not evidence.
 * FB/MC/offset arithmetic previews NEVER authorize SYSTEM_DMA registration.
 * Admitted Map failures inherit session rollback (LastLeaseId may advance).
 */
#ifndef BC250_VM_DOMAIN_BACKEND_H
#define BC250_VM_DOMAIN_BACKEND_H
#include "address-domains-20261003/bc250_address_domains.h"
#include "vm-cpu-session-20261003/bc250_vm_cpu_session.h"

static __inline int Bc250VmDomainSpanValid(const BC250_AD_SPAN *span,
    unsigned int expected_domain, BC250_AD_U64 maximum_last)
{
    return span && span->Domain == expected_domain && span->Bytes &&
        span->Start <= maximum_last && span->Bytes - 1ULL <= maximum_last - span->Start &&
        span->Last == span->Start + (span->Bytes - 1ULL);
}

/* Arithmetic-only preview; output is explicitly FB_PHYSICAL, never DMA. */
static __inline int Bc250VmDomainPreviewVram(const BC250_AD_LAYOUT *layout,
    const BC250_AD_SPAN *input, BC250_AD_SPAN *output)
{
    if (!layout || !input || !output || !input->Bytes || (input->Bytes & 4095ULL) ||
        !Bc250VmDomainSpanValid(input, input->Domain, BC250_AD_MAX48) ||
        !Bc250DmaPageListDisjoint(layout, sizeof(*layout), output, sizeof(*output)) ||
        !Bc250DmaPageListDisjoint(input, sizeof(*input), output, sizeof(*output))) return 0;
    return Bc250AdConvert(layout, input->Domain, input->Start, input->Bytes,
        4096ULL, BC250_AD_FB_PHYSICAL, output);
}

static __inline BC250_VM_SESSION_STATUS Bc250VmDomainRegister(
    BC250_VM_CPU_SESSION *session, const BC250_AD_SPAN *pages,
    unsigned int count, unsigned int capacity, BC250_BACKING_HANDLE *out)
{
    BC250_ADDRESS_SPAN translated[BC250_VM_CPU_MAX_BATCH];
    BC250_VM_SESSION_STATUS status = Bc250VmSessionCheck(session);
    unsigned int i;
    if (status != BC250_VM_SESSION_OK) return status;
    if (!pages || !out || !count || count > BC250_VM_CPU_MAX_BATCH || count > capacity ||
        !Bc250VmSessionBufferAway(session, pages, (BC250_GART_U64)count * sizeof(*pages)) ||
        !Bc250VmSessionBufferAway(session, out, sizeof(*out)) ||
        !Bc250DmaPageListDisjoint(pages, (BC250_GART_U64)count * sizeof(*pages), out, sizeof(*out)))
        return BC250_VM_SESSION_INVALID;
    memset(translated, 0, sizeof(translated));
    for (i = 0; i < count; ++i) {
        if (!Bc250VmDomainSpanValid(&pages[i], BC250_AD_DMA_LOGICAL, session->Backend.MaximumDmaLast) ||
            pages[i].Bytes != 4096ULL || (pages[i].Start & 4095ULL)) return BC250_VM_SESSION_INVALID;
        /* Vocabulary bridge ONLY: AD DMA=5, predecessor DMA=3. No address change. */
        translated[i].Domain = BC250_ADDRESS_DMA_LOGICAL;
        translated[i].Start = pages[i].Start;
        translated[i].Bytes = pages[i].Bytes;
        translated[i].Last = pages[i].Last;
    }
    return Bc250VmSessionRegister(session, translated, count, count, out);
}

static __inline BC250_VM_SESSION_STATUS Bc250VmDomainMap(
    BC250_VM_CPU_SESSION *session, const BC250_AD_SPAN *va,
    const BC250_BACKING_HANDLE *handle, unsigned int access,
    BC250_VM_SESSION_MAPPING_HANDLE *out, BC250_VM_CPU_JOURNAL *journal,
    BC250_VM_CPU_AFTER_WRITE after_write, void *hook_context)
{
    BC250_VM_SESSION_STATUS status = Bc250VmSessionCheck(session);
    BC250_GART_U64 maximum_va_last;
    unsigned int count;
    if (status != BC250_VM_SESSION_OK) return status;
    if (!va || !handle || !out || !journal ||
        !Bc250VmSessionBufferAway(session, va, sizeof(*va)) ||
        !Bc250VmSessionBufferAway(session, handle, sizeof(*handle)) ||
        !Bc250DmaPageListDisjoint(va, sizeof(*va), handle, sizeof(*handle)) ||
        !Bc250DmaPageListDisjoint(va, sizeof(*va), out, sizeof(*out)) ||
        !Bc250DmaPageListDisjoint(va, sizeof(*va), journal, sizeof(*journal)))
        return BC250_VM_SESSION_INVALID;
    if (!Bc250BackingHandleMatches(&session->Backing, handle)) return BC250_VM_SESSION_STALE;
    count = session->Backing.Records[handle->Slot].PageCount;
    maximum_va_last = session->Backend.Layout.MaxPfn * 4096ULL - 1ULL;
    if (!Bc250VmDomainSpanValid(va, BC250_AD_GPU_VIRTUAL, maximum_va_last) ||
        (va->Start & 4095ULL) || va->Bytes != (BC250_GART_U64)count * 4096ULL)
        return BC250_VM_SESSION_INVALID;
    /* Predecessor validates all remaining buffers, permissions and lifetime. */
    return Bc250VmSessionMap(session, va->Start, handle, access, out,
        journal, after_write, hook_context);
}
#endif
