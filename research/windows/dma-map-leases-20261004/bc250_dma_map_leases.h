/* SPDX-License-Identifier: Apache-2.0
 * ORIGINAL RAM-only association, NOT a real DMA/VM binding.
 * Synthetic CPU pages are independent inputs, never obtained from Native.
 * Exclusive whole-call access; stable externally anchored objects/buffers,
 * including locked MDL/provider storage; facade, Resource, Session/nodes and
 * all buffers disjoint. Caller seeds an already acquired fake lease and
 * registered synthetic backing with NO existing CPU mappings. After Init,
 * no raw Session/Leases calls/fields, except caller dropping its seed once.
 * ONLY facade Stop from callbacks; BUSY is reentry detection, NOT SMP safety.
 * No reset/restart. FAULT retains uncertain obligations, no public recovery.
 */
#ifndef BC250_DMA_MAP_LEASES_H
#define BC250_DMA_MAP_LEASES_H
#if !defined(BC250_DMA_ADAPTER_MOCK) || !defined(BC250_DMA_GATE_MOCK) || !defined(BC250_DMA_LEASES_MOCK)
#error Map lease association requires ALL RAM fake platforms
#endif
#ifdef _KERNEL_MODE
#error CPU map lease association is NEVER kernel integrated
#endif
#include "../dma-leases-20261004/bc250_dma_leases.h"
#include "vm-cpu-session-20261003/bc250_vm_cpu_session.h"
extern BOOLEAN Bc250MockLeasesExecutionAllowed;

#define BC250_ML_ACTIVE 1U
#define BC250_ML_STOPPED 2U
#define BC250_ML_FAULT 3U
#define BC250_ML_DEAD 4U
#define BC250_ML_MAPPED 1U
#define BC250_ML_PENDING_DROP 2U
typedef struct BC250_MAP_LEASES BC250_MAP_LEASES;
typedef struct BC250_MAP_LEASE_HANDLE {
    const BC250_MAP_LEASES *Facade;
    BC250_GART_U64 Id;
    unsigned int Slot;
} BC250_MAP_LEASE_HANDLE;
typedef struct BC250_MAP_LEASE_RECORD {
    BC250_DMA_LEASE_HANDLE Dma;
    BC250_VM_SESSION_MAPPING_HANDLE Cpu;
    BC250_GART_U64 Id;
    unsigned int Phase;
} BC250_MAP_LEASE_RECORD;
struct BC250_MAP_LEASES {
    BC250_DMA_LEASES *Resource;
    BC250_VM_CPU_SESSION *Session;
    BC250_BACKING_HANDLE Backing;
    BC250_DMA_LEASE_HANDLE Root;
    BC250_MAP_LEASE_RECORD Records[16];
    BC250_GART_U64 LastId;
    unsigned int State, Busy, BackingReleased, RootDropped;
};
static __inline BC250_VM_SESSION_STATUS Bc250MlGuard(BC250_MAP_LEASES *m)
{
    if (!Bc250MockExecutionAllowed || !Bc250MockGateExecutionAllowed ||
        !Bc250MockLeasesExecutionAllowed) return BC250_VM_SESSION_INVALID;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL || !m) return BC250_VM_SESSION_INVALID;
    if (m->Busy) return BC250_VM_SESSION_BUSY_RESULT;
    if (m->State == BC250_ML_FAULT) return BC250_VM_SESSION_FAULT;
    if (m->State == BC250_ML_DEAD) return BC250_VM_SESSION_DEAD_RESULT;
    return BC250_VM_SESSION_OK;
}
static __inline int Bc250MlAway(BC250_MAP_LEASES *m, const void *buffer, size_t bytes)
{
    return Bc250DmaPageListDisjoint(m, sizeof(*m), buffer, bytes) &&
        Bc250DmaPageListDisjoint(m->Resource, sizeof(*m->Resource), buffer, bytes) &&
        Bc250VmSessionBufferAway(m->Session, buffer, bytes);
}
static __inline BC250_VM_SESSION_STATUS Bc250MlNativeStatus(NTSTATUS status)
{
    if (status == STATUS_SUCCESS) return BC250_VM_SESSION_OK;
    if (status == STATUS_DEVICE_BUSY) return BC250_VM_SESSION_BUSY_RESULT;
    if (status == STATUS_INSUFFICIENT_RESOURCES) return BC250_VM_SESSION_FULL;
    if (status == STATUS_CANCELLED) return BC250_VM_SESSION_IN_USE;
    return BC250_VM_SESSION_FAULT;
}
static __inline BC250_VM_SESSION_STATUS Bc250MlCheck(BC250_MAP_LEASES *m)
{
    unsigned int i, j, cpuCount = 0, recordCount = 0;
    BC250_VM_SESSION_STATUS status = Bc250MlGuard(m);
    if (status != BC250_VM_SESSION_OK) return status;
    if ((m->State != BC250_ML_ACTIVE && m->State != BC250_ML_STOPPED) ||
        !m->Resource || !m->Session || m->BackingReleased > 1U || m->RootDropped > 1U ||
        !Bc250DmaPageListDisjoint(m, sizeof(*m), m->Resource, sizeof(*m->Resource)) ||
        !Bc250VmSessionBufferAway(m->Session, m, sizeof(*m)) ||
        !Bc250VmSessionBufferAway(m->Session, m->Resource, sizeof(*m->Resource)))
        return BC250_VM_SESSION_INVALID;
    status = Bc250VmSessionCheck(m->Session);
    if (status != BC250_VM_SESSION_OK) return status;
    if (!m->BackingReleased && !Bc250BackingHandleMatches(&m->Session->Backing, &m->Backing))
        return BC250_VM_SESSION_CORRUPT;
    if (!m->RootDropped && (m->Root.Owner != m->Resource || !m->Root.Id))
        return BC250_VM_SESSION_CORRUPT;
    for (i = 0; i < 16; ++i) {
        const BC250_MAP_LEASE_RECORD *r = &m->Records[i];
        if (!r->Phase) continue;
        if (!r->Id || r->Id > m->LastId || r->Dma.Owner != m->Resource || !r->Dma.Id ||
            r->Dma.Slot >= BC250_DMA_LEASES_SLOTS || m->BackingReleased || m->RootDropped)
            return BC250_VM_SESSION_CORRUPT;
        for (j = 0; j < i; ++j)
            if (m->Records[j].Phase && (m->Records[j].Id == r->Id || m->Records[j].Dma.Id == r->Dma.Id))
                return BC250_VM_SESSION_CORRUPT;
        if (r->Phase == BC250_ML_MAPPED) {
            const BC250_VM_SESSION_MAPPING *cpu;
            if (r->Cpu.Session != m->Session || r->Cpu.Slot >= BC250_VM_SESSION_MAX_MAPPINGS || !r->Cpu.Id)
                return BC250_VM_SESSION_CORRUPT;
            cpu = &m->Session->Mappings[r->Cpu.Slot];
            if (!cpu->Live || cpu->Id != r->Cpu.Id || cpu->Lease.Slot != m->Backing.Slot ||
                cpu->Lease.Generation != m->Backing.Generation) return BC250_VM_SESSION_CORRUPT;
            for (j = 0; j < i; ++j) if (m->Records[j].Phase == BC250_ML_MAPPED &&
                m->Records[j].Cpu.Id == r->Cpu.Id) return BC250_VM_SESSION_CORRUPT;
            ++recordCount;
        } else if (r->Phase != BC250_ML_PENDING_DROP || r->Cpu.Session || r->Cpu.Id)
            return BC250_VM_SESSION_CORRUPT;
    }
    for (i = 0; i < BC250_VM_SESSION_MAX_MAPPINGS; ++i) if (m->Session->Mappings[i].Live) ++cpuCount;
    return cpuCount == recordCount ? BC250_VM_SESSION_OK : BC250_VM_SESSION_CORRUPT;
}
static __inline BC250_VM_SESSION_STATUS Bc250MlStop(BC250_MAP_LEASES *m)
{
    NTSTATUS status;
    if (!Bc250MockExecutionAllowed || !Bc250MockGateExecutionAllowed || !Bc250MockLeasesExecutionAllowed ||
        KeGetCurrentIrql() != PASSIVE_LEVEL || !m) return BC250_VM_SESSION_INVALID;
    if (m->State != BC250_ML_ACTIVE && m->State != BC250_ML_STOPPED) return BC250_VM_SESSION_IN_USE;
    m->State = BC250_ML_STOPPED;
    status = Bc250DmaLeasesStop(m->Resource);
    if (status != STATUS_SUCCESS) m->State = BC250_ML_FAULT;
    return Bc250MlNativeStatus(status); /* No metadata drop or CPU tree mutation. */
}
static __inline BC250_VM_SESSION_STATUS Bc250MlInit(BC250_MAP_LEASES *m,
    BC250_DMA_LEASES *resource, const BC250_DMA_LEASE_HANDLE *seed,
    BC250_VM_CPU_SESSION *session, const BC250_BACKING_HANDLE *backing)
{
    BC250_MAP_LEASES zero;
    NTSTATUS native;
    unsigned int i;
    BC250_VM_SESSION_STATUS status = Bc250MlGuard(m);
    if (status != BC250_VM_SESSION_OK) return status;
    memset(&zero, 0, sizeof(zero));
    if (memcmp(m, &zero, sizeof(zero)) || !resource || !seed || !session || !backing)
        return BC250_VM_SESSION_INVALID;
    status = Bc250VmSessionCheck(session);
    if (status != BC250_VM_SESSION_OK) return status;
    if (!Bc250VmSessionBufferAway(session, m, sizeof(*m)) ||
        !Bc250VmSessionBufferAway(session, resource, sizeof(*resource)) ||
        !Bc250VmSessionBufferAway(session, seed, sizeof(*seed)) ||
        !Bc250VmSessionBufferAway(session, backing, sizeof(*backing)) ||
        !Bc250DmaPageListDisjoint(m, sizeof(*m), resource, sizeof(*resource)) ||
        !Bc250DmaPageListDisjoint(m, sizeof(*m), seed, sizeof(*seed)) ||
        !Bc250DmaPageListDisjoint(m, sizeof(*m), backing, sizeof(*backing)) ||
        !Bc250DmaPageListDisjoint(resource, sizeof(*resource), seed, sizeof(*seed)) ||
        !Bc250DmaPageListDisjoint(resource, sizeof(*resource), backing, sizeof(*backing)) ||
        !Bc250DmaPageListDisjoint(seed, sizeof(*seed), backing, sizeof(*backing)) ||
        !Bc250BackingHandleMatches(&session->Backing, backing)) return BC250_VM_SESSION_INVALID;
    for (i = 0; i < BC250_VM_SESSION_MAX_MAPPINGS; ++i)
        if (session->Mappings[i].Live) return BC250_VM_SESSION_IN_USE;
    m->Resource = resource; m->Session = session; m->Backing = *backing;
    m->Busy = 1U;
    native = Bc250DmaLeasesRetain(resource, seed, &m->Root);
    m->State = native == STATUS_SUCCESS ? BC250_ML_ACTIVE : BC250_ML_FAULT;
    m->Busy = 0U;
    return Bc250MlNativeStatus(native); /* Even error-after-publication preserves Root. */
}
/* Internal, Busy-held; no CPU map exists for this pending reference.
 * BUSY preserves its handle and stops new maps. Other errors quarantine. */
static __inline BC250_VM_SESSION_STATUS Bc250MlDropPending(BC250_MAP_LEASES *m, unsigned int slot)
{
    BC250_MAP_LEASE_RECORD *r = &m->Records[slot];
    NTSTATUS native = Bc250DmaLeasesDrop(m->Resource, &r->Dma);
    if (native == STATUS_SUCCESS) memset(r, 0, sizeof(*r));
    else if (native == STATUS_DEVICE_BUSY) {
        m->State = BC250_ML_STOPPED;
        if (Bc250DmaLeasesStop(m->Resource) != STATUS_SUCCESS) m->State = BC250_ML_FAULT;
    } else m->State = BC250_ML_FAULT;
    return m->State == BC250_ML_FAULT ? BC250_VM_SESSION_FAULT : Bc250MlNativeStatus(native);
}
static __inline BC250_VM_SESSION_STATUS Bc250MlMap(BC250_MAP_LEASES *m, BC250_GART_U64 va,
    unsigned int access, BC250_MAP_LEASE_HANDLE *out, BC250_VM_CPU_JOURNAL *journal,
    BC250_VM_CPU_AFTER_WRITE hook, void *context)
{
    BC250_MAP_LEASE_HANDLE zero = {0}, result = {0};
    BC250_MAP_LEASE_RECORD *r;
    unsigned int slot;
    NTSTATUS native;
    BC250_VM_SESSION_STATUS status = Bc250MlCheck(m);
    if (status != BC250_VM_SESSION_OK) return status;
    if (m->State != BC250_ML_ACTIVE) return BC250_VM_SESSION_IN_USE;
    if (!Bc250MlAway(m, out, sizeof(*out)) || !Bc250MlAway(m, journal, sizeof(*journal)) ||
        !Bc250DmaPageListDisjoint(out, sizeof(*out), journal, sizeof(*journal)) ||
        memcmp(out, &zero, sizeof(zero))) return BC250_VM_SESSION_INVALID;
    if (m->LastId == BC250_GART_U64_MAX) return BC250_VM_SESSION_EXHAUSTED;
    for (slot = 0; slot < 16; ++slot) if (!m->Records[slot].Phase) break;
    if (slot == 16) return BC250_VM_SESSION_FULL;
    m->Busy = 1U; r = &m->Records[slot]; r->Id = ++m->LastId;
    native = Bc250DmaLeasesRetain(m->Resource, &m->Root, &r->Dma);
    if (native != STATUS_SUCCESS) {
        if (r->Dma.Owner) { r->Phase = BC250_ML_PENDING_DROP; m->State = BC250_ML_FAULT; }
        else memset(r, 0, sizeof(*r));
        if (Bc250MlNativeStatus(native) == BC250_VM_SESSION_FAULT) m->State = BC250_ML_FAULT;
        m->Busy = 0U; return Bc250MlNativeStatus(native);
    }
    r->Phase = BC250_ML_PENDING_DROP; /* Counted BEFORE any CPU mutation. */
    status = Bc250VmSessionMap(m->Session, va, &m->Backing, access, &r->Cpu, journal, hook, context);
    if (status == BC250_VM_SESSION_OK) {
        r->Phase = BC250_ML_MAPPED;
        result.Facade = m; result.Id = r->Id; result.Slot = slot; *out = result;
    } else if (Bc250VmSessionCheck(m->Session) != BC250_VM_SESSION_OK) m->State = BC250_ML_FAULT;
    else {
        BC250_VM_SESSION_STATUS drop = Bc250MlDropPending(m, slot);
        if (drop != BC250_VM_SESSION_OK) status = drop; /* Preserve inaccessible pending obligation. */
    }
    if (m->State == BC250_ML_FAULT) status = BC250_VM_SESSION_FAULT;
    m->Busy = 0U; return status;
}
static __inline BC250_VM_SESSION_STATUS Bc250MlUnmap(BC250_MAP_LEASES *m,
    const BC250_MAP_LEASE_HANDLE *input, BC250_VM_CPU_JOURNAL *journal,
    BC250_VM_CPU_AFTER_WRITE hook, void *context)
{
    BC250_MAP_LEASE_RECORD *r;
    BC250_VM_SESSION_STATUS status = Bc250MlCheck(m);
    if (status != BC250_VM_SESSION_OK) return status;
    if (!Bc250MlAway(m, input, sizeof(*input)) || !Bc250MlAway(m, journal, sizeof(*journal)) ||
        !Bc250DmaPageListDisjoint(input, sizeof(*input), journal, sizeof(*journal)))
        return BC250_VM_SESSION_INVALID;
    if (input->Facade != m || input->Slot >= 16 || !input->Id ||
        !m->Records[input->Slot].Phase || m->Records[input->Slot].Id != input->Id)
        return BC250_VM_SESSION_STALE;
    m->Busy = 1U; r = &m->Records[input->Slot];
    if (r->Phase == BC250_ML_MAPPED) {
        status = Bc250VmSessionUnmap(m->Session, &r->Cpu, journal, hook, context);
        if (status != BC250_VM_SESSION_OK) {
            if (Bc250VmSessionCheck(m->Session) != BC250_VM_SESSION_OK) m->State = BC250_ML_FAULT;
            m->Busy = 0U; return m->State == BC250_ML_FAULT ? BC250_VM_SESSION_FAULT : status;
        }
        memset(&r->Cpu, 0, sizeof(r->Cpu)); r->Phase = BC250_ML_PENDING_DROP;
    }
    status = Bc250MlDropPending(m, input->Slot); /* AFTER complete CPU Unmap only. */
    m->Busy = 0U; return status;
}
static __inline BC250_VM_SESSION_STATUS Bc250MlDrain(BC250_MAP_LEASES *m)
{
    unsigned int i;
    BC250_VM_SESSION_STATUS status = Bc250MlCheck(m);
    if (status != BC250_VM_SESSION_OK) return status;
    m->Busy = 1U;
    for (i = 0; i < 16; ++i) if (m->Records[i].Phase == BC250_ML_PENDING_DROP) {
        status = Bc250MlDropPending(m, i);
        if (status != BC250_VM_SESSION_OK) break;
    }
    m->Busy = 0U; return status;
}
static __inline BC250_VM_SESSION_STATUS Bc250MlRetire(BC250_MAP_LEASES *m)
{
    unsigned int i;
    NTSTATUS native;
    BC250_VM_SESSION_STATUS status = Bc250MlCheck(m);
    if (status != BC250_VM_SESSION_OK) return status;
    status = Bc250MlStop(m);
    if (status != BC250_VM_SESSION_OK) return status;
    status = Bc250MlDrain(m);
    if (status != BC250_VM_SESSION_OK) return status;
    for (i = 0; i < 16; ++i) if (m->Records[i].Phase) return BC250_VM_SESSION_IN_USE;
    m->Busy = 1U;
    if (!m->BackingReleased) {
        status = Bc250VmSessionRelease(m->Session, &m->Backing);
        if (status == BC250_VM_SESSION_OK) m->BackingReleased = 1U;
    }
    if (status == BC250_VM_SESSION_OK && !m->RootDropped) {
        native = Bc250DmaLeasesDrop(m->Resource, &m->Root);
        status = Bc250MlNativeStatus(native);
        if (native == STATUS_SUCCESS) { m->RootDropped = 1U; memset(&m->Root, 0, sizeof(m->Root)); }
    }
    if (status == BC250_VM_SESSION_OK) {
        native = Bc250DmaLeasesRetire(m->Resource); status = Bc250MlNativeStatus(native);
        if (native == STATUS_SUCCESS) m->State = BC250_ML_DEAD;
    }
    if (status == BC250_VM_SESSION_FAULT || status == BC250_VM_SESSION_CORRUPT) m->State = BC250_ML_FAULT;
    m->Busy = 0U; return status;
}
#endif
