/* SPDX-License-Identifier: Apache-2.0
 * Original CPU-only facade over the reviewed CPU tree and numeric backing
 * ledger. NOT a Windows DMA owner, GPU page table or runtime driver consumer.
 * Map covers an entire registered descriptor list; Unmap removes one complete
 * mapping. Numeric aliases acquire independent occurrence leases. No overwrite,
 * partial unmap, GPU publication, OS allocation or external code is introduced.
 *
 * Caller MUST provide a zeroed session once, at a stable address, valid stable
 * disjoint buffers, honest allocator and infallible Free, and exclusive access
 * for the WHOLE call. BUSY handles synchronous API reentry, not thread safety.
 * Callbacks may reenter this facade (BUSY), but must not directly read/mutate
 * internals or retain internal handles/pointers. Direct tree/backing API calls
 * and raw field edits are prohibited after initialization. Public struct fields
 * are implementation storage, NOT independently usable resource handles.
 * Validation detects supported corrupt fixtures; it cannot probe bad pointers
 * or prove OS ownership. Tokens/IDs are not unforgeable capabilities.
 *
 * Failed Map may advance LastLeaseId (never rewind) after obtaining then dropping
 * its lease. Tree/mappings/reference counts are restored; output stays untouched.
 * Failed Unmap preserves its mapping AND lease. Shutdown requires every backing
 * explicitly released and every mapping unmapped; then enters terminal DEAD.
 * Never reset/reinitialize/reuse this address while any old handle may exist.
 */
#ifndef BC250_VM_CPU_SESSION_H
#define BC250_VM_CPU_SESSION_H
#include "vm-cpu-lifetime-20261003/bc250_vm_cpu_unmap.h"
#include "vm-cpu-lifetime-20261003/bc250_vm_cpu_backing_refs.h"

#define BC250_VM_SESSION_MAX_MAPPINGS 16U
#define BC250_VM_SESSION_EMPTY 0U
#define BC250_VM_SESSION_READY 1U
#define BC250_VM_SESSION_BUSY 2U
#define BC250_VM_SESSION_DEAD 3U
typedef enum BC250_VM_SESSION_STATUS {
    BC250_VM_SESSION_OK, BC250_VM_SESSION_INVALID, BC250_VM_SESSION_BUSY_RESULT,
    BC250_VM_SESSION_NO_MEMORY, BC250_VM_SESSION_FAULT, BC250_VM_SESSION_CORRUPT,
    BC250_VM_SESSION_FULL, BC250_VM_SESSION_STALE, BC250_VM_SESSION_EXHAUSTED,
    BC250_VM_SESSION_IN_USE, BC250_VM_SESSION_DEAD_RESULT
} BC250_VM_SESSION_STATUS;
typedef struct BC250_VM_CPU_SESSION BC250_VM_CPU_SESSION;
typedef struct BC250_VM_SESSION_MAPPING_HANDLE {
    const BC250_VM_CPU_SESSION *Session;
    BC250_GART_U64 Id;
    unsigned int Slot;
} BC250_VM_SESSION_MAPPING_HANDLE;
typedef struct BC250_VM_SESSION_MAPPING {
    BC250_BACKING_TOKEN Lease;
    BC250_GART_U64 Va, Id;
    unsigned int Live, Count, Access;
} BC250_VM_SESSION_MAPPING;
struct BC250_VM_CPU_SESSION {
    BC250_VM_CPU_CONTEXT Backend;
    BC250_BACKING_POOL Backing;
    BC250_VM_SESSION_MAPPING Mappings[BC250_VM_SESSION_MAX_MAPPINGS];
    BC250_GART_U64 LastMappingId;
    unsigned int State;
};

static __inline BC250_VM_SESSION_STATUS Bc250VmSessionCpuStatus(BC250_VM_CPU_STATUS status)
{
    switch (status) {
    case BC250_VM_CPU_OK: return BC250_VM_SESSION_OK;
    case BC250_VM_CPU_INVALID: return BC250_VM_SESSION_INVALID;
    case BC250_VM_CPU_BUSY_RESULT: return BC250_VM_SESSION_BUSY_RESULT;
    case BC250_VM_CPU_NO_MEMORY: return BC250_VM_SESSION_NO_MEMORY;
    case BC250_VM_CPU_FAULT: return BC250_VM_SESSION_FAULT;
    default: return BC250_VM_SESSION_CORRUPT;
    }
}
static __inline BC250_VM_SESSION_STATUS Bc250VmSessionBackingStatus(BC250_BACKING_STATUS status)
{
    switch (status) {
    case BC250_BACKING_OK: return BC250_VM_SESSION_OK;
    case BC250_BACKING_INVALID: return BC250_VM_SESSION_INVALID;
    case BC250_BACKING_FULL: return BC250_VM_SESSION_FULL;
    case BC250_BACKING_BUSY: return BC250_VM_SESSION_IN_USE;
    case BC250_BACKING_STALE: return BC250_VM_SESSION_STALE;
    case BC250_BACKING_EXHAUSTED: return BC250_VM_SESSION_EXHAUSTED;
    default: return BC250_VM_SESSION_CORRUPT;
    }
}

/* Internal: only after whole-tree validation; no full-tree rewalk per page. */
static __inline const BC250_GART_U64 *Bc250VmSessionLeaf(
    const BC250_VM_CPU_SESSION *session, BC250_GART_U64 va)
{
    BC250_VM_INDICES indices;
    const BC250_VM_CPU_NODE *node = session->Backend.Root;
    unsigned int level;
    if (!Bc250VmDecode(&session->Backend.Layout, va, &indices)) return NULL;
    for (level = session->Backend.Layout.Root; level < BC250_VM_PTB; ++level) {
        node = node->Data.Children[indices.Index[level]];
        if (!node) return NULL;
    }
    return &node->Data.Values[indices.Index[BC250_VM_PTB]];
}
/* Internal: validated depth/ownership/tree only; maximum 4096*512 words. */
static __inline unsigned int Bc250VmSessionNonzero(const BC250_VM_CPU_NODE *node)
{
    unsigned int i, count = 0U;
    if (node->Level == BC250_VM_PTB) {
        for (i = 0; i < 512U; ++i) if (node->Data.Values[i]) ++count;
    } else {
        for (i = 0; i < 512U; ++i)
            if (node->Data.Children[i]) count += Bc250VmSessionNonzero(node->Data.Children[i]);
    }
    return count;
}
static __inline unsigned int Bc250VmSessionLeaseIndex(const BC250_VM_CPU_SESSION *session,
    const BC250_BACKING_TOKEN *token)
{
    unsigned int i;
    if (token->Pool != &session->Backing || token->Slot >= BC250_BACKING_SLOTS ||
        !token->Generation || !token->LeaseId) return BC250_BACKING_LEASES;
    for (i = 0; i < BC250_BACKING_LEASES; ++i) {
        const BC250_BACKING_LEASE *lease = &session->Backing.Leases[i];
        if (lease->Live && lease->Slot == token->Slot && lease->Generation == token->Generation &&
            lease->Id == token->LeaseId) return i;
    }
    return BC250_BACKING_LEASES;
}

/* Full tree + registry + bijection, including rejection of hidden leaf words.
 * This is metadata consistency, not validation of Windows/GPU backing lifetime.
 */
static __inline int Bc250VmSessionConsistent(const BC250_VM_CPU_SESSION *session)
{
    BC250_GART_U64 used_leases = 0ULL;
    unsigned int i, j, expected_words = 0U;
    if (!session || session->Backend.State != BC250_VM_CPU_READY ||
        !Bc250VmCpuValidateTree(&session->Backend) || !Bc250BackingConsistent(&session->Backing)) return 0;
    for (i = 0; i < BC250_VM_SESSION_MAX_MAPPINGS; ++i) {
        const BC250_VM_SESSION_MAPPING *mapping = &session->Mappings[i];
        const BC250_BACKING_RECORD *backing;
        BC250_GART_U64 pfn;
        unsigned int lease_index;
        if (!mapping->Live) continue;
        if (mapping->Live != 1U || !mapping->Id || mapping->Id > session->LastMappingId ||
            !mapping->Count || mapping->Count > BC250_VM_CPU_MAX_BATCH ||
            (mapping->Va & 4095ULL) || (mapping->Access & ~15U)) return 0;
        pfn = mapping->Va >> 12U;
        if (pfn >= session->Backend.Layout.MaxPfn ||
            mapping->Count > session->Backend.Layout.MaxPfn - pfn) return 0;
        lease_index = Bc250VmSessionLeaseIndex(session, &mapping->Lease);
        if (lease_index >= BC250_BACKING_LEASES || (used_leases & (1ULL << lease_index))) return 0;
        used_leases |= 1ULL << lease_index;
        backing = &session->Backing.Records[mapping->Lease.Slot];
        if (mapping->Count != backing->PageCount || backing->MaximumDmaLast != session->Backend.MaximumDmaLast)
            return 0;
        for (j = 0; j < i; ++j) {
            const BC250_VM_SESSION_MAPPING *prior = &session->Mappings[j];
            if (prior->Live) {
                BC250_GART_U64 prior_pfn = prior->Va >> 12U;
                if (mapping->Id == prior->Id ||
                    (pfn < prior_pfn + prior->Count && prior_pfn < pfn + mapping->Count)) return 0;
            }
        }
        for (j = 0; j < mapping->Count; ++j) {
            BC250_GART_U64 expected;
            const BC250_GART_U64 *actual = Bc250VmSessionLeaf(session, mapping->Va + (BC250_GART_U64)j * 4096ULL);
            if (!actual || !Bc250VmEncodeSystemLeaf(&backing->Pages[j], session->Backend.MaximumDmaLast,
                mapping->Access, &expected) || *actual != expected) return 0;
        }
        expected_words += mapping->Count;
    }
    for (i = 0; i < BC250_BACKING_LEASES; ++i)
        if (!!session->Backing.Leases[i].Live != !!(used_leases & (1ULL << i))) return 0;
    /* Unmapped registered backing records must use the same numeric limit too. */
    for (i = 0; i < BC250_BACKING_SLOTS; ++i)
        if (session->Backing.Records[i].Registered &&
            session->Backing.Records[i].MaximumDmaLast != session->Backend.MaximumDmaLast) return 0;
    return Bc250VmSessionNonzero(session->Backend.Root) == expected_words;
}
static __inline BC250_VM_SESSION_STATUS Bc250VmSessionCheck(const BC250_VM_CPU_SESSION *session)
{
    if (!session) return BC250_VM_SESSION_INVALID;
    if (session->State == BC250_VM_SESSION_BUSY) return BC250_VM_SESSION_BUSY_RESULT;
    if (session->State == BC250_VM_SESSION_DEAD) return BC250_VM_SESSION_DEAD_RESULT;
    if (session->State != BC250_VM_SESSION_READY) return BC250_VM_SESSION_INVALID;
    return Bc250VmSessionConsistent(session) ? BC250_VM_SESSION_OK : BC250_VM_SESSION_CORRUPT;
}
/* Internal: session's tree was validated and caller has exclusive access. */
static __inline int Bc250VmSessionBufferAway(const BC250_VM_CPU_SESSION *session,
    const void *buffer, BC250_GART_U64 bytes)
{
    return Bc250DmaPageListDisjoint(session, sizeof(*session), buffer, bytes) &&
        Bc250VmCpuBufferAwayFromNodes(session->Backend.Root, buffer, bytes);
}
/* Internal infallible commit/rollback primitive; index was prevalidated, with
 * no intervening direct pool writes permitted. Consume exactly one occurrence. */
static __inline void Bc250VmSessionConsumeLease(BC250_VM_CPU_SESSION *session, unsigned int index)
{
    BC250_BACKING_LEASE *lease = &session->Backing.Leases[index];
    --session->Backing.Records[lease->Slot].References;
    memset(lease, 0, sizeof(*lease));
}

static __inline BC250_VM_SESSION_STATUS Bc250VmSessionInit(BC250_VM_CPU_SESSION *session,
    const BC250_VM_LAYOUT *layout, BC250_GART_U64 maximum_dma_last,
    unsigned int maximum_nodes, BC250_VM_CPU_ALLOC allocate, BC250_VM_CPU_FREE release,
    void *callback_context)
{
    BC250_VM_CPU_SESSION zero;
    BC250_VM_CPU_STATUS status;
    if (!session) return BC250_VM_SESSION_INVALID;
    if (session->State == BC250_VM_SESSION_BUSY) return BC250_VM_SESSION_BUSY_RESULT;
    if (session->State == BC250_VM_SESSION_DEAD) return BC250_VM_SESSION_DEAD_RESULT;
    memset(&zero, 0, sizeof(zero));
    if (!layout || memcmp(session, &zero, sizeof(zero)) ||
        !Bc250DmaPageListDisjoint(session, sizeof(*session), layout, sizeof(*layout))) return BC250_VM_SESSION_INVALID;
    session->State = BC250_VM_SESSION_BUSY;
    status = Bc250VmCpuInit(&session->Backend, layout, maximum_dma_last, maximum_nodes,
        allocate, release, callback_context);
    session->State = status == BC250_VM_CPU_OK ? BC250_VM_SESSION_READY : BC250_VM_SESSION_EMPTY;
    return Bc250VmSessionCpuStatus(status);
}

static __inline BC250_VM_SESSION_STATUS Bc250VmSessionRegister(BC250_VM_CPU_SESSION *session,
    const BC250_ADDRESS_SPAN *pages, unsigned int count, unsigned int capacity,
    BC250_BACKING_HANDLE *out)
{
    BC250_VM_SESSION_STATUS status = Bc250VmSessionCheck(session);
    BC250_BACKING_STATUS backing_status;
    if (status != BC250_VM_SESSION_OK) return status;
    if (!pages || !out || !count || count > BC250_BACKING_PAGES || count > capacity ||
        !Bc250VmSessionBufferAway(session, pages, (BC250_GART_U64)count * sizeof(*pages)) ||
        !Bc250VmSessionBufferAway(session, out, sizeof(*out)) ||
        !Bc250DmaPageListDisjoint(pages, (BC250_GART_U64)count * sizeof(*pages), out, sizeof(*out)))
        return BC250_VM_SESSION_INVALID;
    session->State = BC250_VM_SESSION_BUSY;
    backing_status = Bc250BackingRegister(&session->Backing, pages, count, capacity,
        session->Backend.MaximumDmaLast, out);
    session->State = BC250_VM_SESSION_READY;
    return Bc250VmSessionBackingStatus(backing_status);
}
static __inline BC250_VM_SESSION_STATUS Bc250VmSessionRelease(BC250_VM_CPU_SESSION *session,
    const BC250_BACKING_HANDLE *handle)
{
    BC250_VM_SESSION_STATUS status = Bc250VmSessionCheck(session);
    BC250_BACKING_STATUS backing_status;
    if (status != BC250_VM_SESSION_OK) return status;
    if (!handle || !Bc250VmSessionBufferAway(session, handle, sizeof(*handle))) return BC250_VM_SESSION_INVALID;
    session->State = BC250_VM_SESSION_BUSY;
    backing_status = Bc250BackingRelease(&session->Backing, handle);
    session->State = BC250_VM_SESSION_READY;
    return Bc250VmSessionBackingStatus(backing_status);
}

static __inline BC250_VM_SESSION_STATUS Bc250VmSessionMap(BC250_VM_CPU_SESSION *session,
    BC250_GART_U64 va, const BC250_BACKING_HANDLE *handle, unsigned int access,
    BC250_VM_SESSION_MAPPING_HANDLE *out, BC250_VM_CPU_JOURNAL *journal,
    BC250_VM_CPU_AFTER_WRITE after_write, void *hook_context)
{
    BC250_VM_SESSION_STATUS status = Bc250VmSessionCheck(session);
    BC250_VM_CPU_STATUS cpu_status;
    BC250_BACKING_STATUS backing_status;
    BC250_BACKING_TOKEN token = {0};
    BC250_VM_SESSION_MAPPING_HANDLE result = {0};
    const BC250_BACKING_RECORD *backing;
    BC250_VM_SESSION_MAPPING *mapping;
    BC250_GART_U64 pfn = va >> 12U;
    unsigned int slot = BC250_VM_SESSION_MAX_MAPPINGS, lease_index, i, count;
    if (status != BC250_VM_SESSION_OK) return status;
    if (!handle || !out || !journal || (va & 4095ULL) || (access & ~15U) ||
        !Bc250VmSessionBufferAway(session, handle, sizeof(*handle)) ||
        !Bc250VmSessionBufferAway(session, out, sizeof(*out)) ||
        !Bc250VmSessionBufferAway(session, journal, sizeof(*journal)) ||
        !Bc250DmaPageListDisjoint(handle, sizeof(*handle), out, sizeof(*out)) ||
        !Bc250DmaPageListDisjoint(handle, sizeof(*handle), journal, sizeof(*journal)) ||
        !Bc250DmaPageListDisjoint(out, sizeof(*out), journal, sizeof(*journal))) return BC250_VM_SESSION_INVALID;
    if (!Bc250BackingHandleMatches(&session->Backing, handle)) return BC250_VM_SESSION_STALE;
    backing = &session->Backing.Records[handle->Slot]; count = backing->PageCount;
    if (pfn >= session->Backend.Layout.MaxPfn || count > session->Backend.Layout.MaxPfn - pfn)
        return BC250_VM_SESSION_INVALID;
    for (i = 0; i < BC250_VM_SESSION_MAX_MAPPINGS; ++i) {
        const BC250_VM_SESSION_MAPPING *old = &session->Mappings[i];
        if (!old->Live) { if (slot == BC250_VM_SESSION_MAX_MAPPINGS) slot = i; }
        else {
            BC250_GART_U64 old_pfn = old->Va >> 12U;
            if (pfn < old_pfn + old->Count && old_pfn < pfn + count) return BC250_VM_SESSION_IN_USE;
        }
    }
    if (slot == BC250_VM_SESSION_MAX_MAPPINGS) return BC250_VM_SESSION_FULL;
    if (session->LastMappingId == BC250_GART_U64_MAX) return BC250_VM_SESSION_EXHAUSTED;
    for (lease_index = 0; lease_index < BC250_BACKING_LEASES; ++lease_index)
        if (!session->Backing.Leases[lease_index].Live) break;
    if (lease_index == BC250_BACKING_LEASES) return BC250_VM_SESSION_FULL;
    session->State = BC250_VM_SESSION_BUSY;
    backing_status = Bc250BackingAcquire(&session->Backing, handle, &token);
    if (backing_status != BC250_BACKING_OK) {
        session->State = BC250_VM_SESSION_READY;
        return Bc250VmSessionBackingStatus(backing_status);
    }
    /* Acquire chose the first free lease index under the same exclusive call.
     * Its ownership is retained until successful Unmap, or rolled back below. */
    cpu_status = Bc250VmCpuMap(&session->Backend, va, backing->Pages, count, count,
        access, journal, after_write, hook_context);
    if (cpu_status != BC250_VM_CPU_OK) {
        Bc250VmSessionConsumeLease(session, lease_index);
        session->State = BC250_VM_SESSION_READY;
        return Bc250VmSessionCpuStatus(cpu_status);
    }
    mapping = &session->Mappings[slot];
    mapping->Lease = token; mapping->Va = va; mapping->Count = count; mapping->Access = access;
    mapping->Id = ++session->LastMappingId; mapping->Live = 1U;
    result.Session = session; result.Slot = slot; result.Id = mapping->Id;
    *out = result;
    session->State = BC250_VM_SESSION_READY;
    return BC250_VM_SESSION_OK;
}

static __inline BC250_VM_SESSION_STATUS Bc250VmSessionUnmap(BC250_VM_CPU_SESSION *session,
    const BC250_VM_SESSION_MAPPING_HANDLE *handle, BC250_VM_CPU_JOURNAL *journal,
    BC250_VM_CPU_AFTER_WRITE after_write, void *hook_context)
{
    BC250_VM_SESSION_STATUS status = Bc250VmSessionCheck(session);
    BC250_VM_SESSION_MAPPING *mapping;
    BC250_VM_CPU_STATUS cpu_status;
    unsigned int lease_index;
    if (status != BC250_VM_SESSION_OK) return status;
    if (!handle || !journal || !Bc250VmSessionBufferAway(session, handle, sizeof(*handle)) ||
        !Bc250VmSessionBufferAway(session, journal, sizeof(*journal)) ||
        !Bc250DmaPageListDisjoint(handle, sizeof(*handle), journal, sizeof(*journal))) return BC250_VM_SESSION_INVALID;
    if (handle->Session != session || handle->Slot >= BC250_VM_SESSION_MAX_MAPPINGS || !handle->Id)
        return BC250_VM_SESSION_STALE;
    mapping = &session->Mappings[handle->Slot];
    if (!mapping->Live || mapping->Id != handle->Id) return BC250_VM_SESSION_STALE;
    lease_index = Bc250VmSessionLeaseIndex(session, &mapping->Lease);
    if (lease_index >= BC250_BACKING_LEASES) return BC250_VM_SESSION_CORRUPT;
    session->State = BC250_VM_SESSION_BUSY;
    cpu_status = Bc250VmCpuUnmap(&session->Backend, mapping->Va, mapping->Count,
        journal, after_write, hook_context);
    if (cpu_status == BC250_VM_CPU_OK) {
        /* No callback or fallible step after tree commit. This exact lease was
         * validated before clearing any leaf; no other alias is decremented. */
        Bc250VmSessionConsumeLease(session, lease_index);
        memset(mapping, 0, sizeof(*mapping));
    }
    session->State = BC250_VM_SESSION_READY;
    return Bc250VmSessionCpuStatus(cpu_status);
}

static __inline BC250_VM_SESSION_STATUS Bc250VmSessionShutdown(BC250_VM_CPU_SESSION *session)
{
    BC250_VM_SESSION_STATUS status = Bc250VmSessionCheck(session);
    BC250_VM_CPU_STATUS cpu_status;
    unsigned int i;
    if (status != BC250_VM_SESSION_OK) return status;
    for (i = 0; i < BC250_BACKING_SLOTS; ++i)
        if (session->Backing.Records[i].Registered) return BC250_VM_SESSION_IN_USE;
    for (i = 0; i < BC250_VM_SESSION_MAX_MAPPINGS; ++i)
        if (session->Mappings[i].Live) return BC250_VM_SESSION_IN_USE;
    for (i = 0; i < BC250_BACKING_LEASES; ++i)
        if (session->Backing.Leases[i].Live) return BC250_VM_SESSION_IN_USE;
    session->State = BC250_VM_SESSION_BUSY;
    cpu_status = Bc250VmCpuShutdown(&session->Backend);
    session->State = cpu_status == BC250_VM_CPU_OK ? BC250_VM_SESSION_DEAD : BC250_VM_SESSION_READY;
    return Bc250VmSessionCpuStatus(cpu_status);
}
#endif
