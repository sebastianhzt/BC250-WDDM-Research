/* SPDX-License-Identifier: Apache-2.0
 * NEW RAM-only association. PUBLIC Snapshot API, no private resource access.
 * Aggregate snapshot ref protects ALL numeric CPU maps until backing removal.
 * NOT a per-map OS DMA ref, actual consumer, GPU mapping or W2P proof.
 * Zero once, stable Self; entire wrapper/resource/session/nodes/provider storage
 * and buffers trusted, disjoint, externally anchored before ALL calls through
 * ALL returns + snapshot/map/retirement debts. Exclusive WHOLE bundle calls.
 * After Init attempt ONLY these facade APIs; caller may Drop seed once even
 * after failed Init, but NEVER raw SnapshotRelease/Session/resource access.
 * ONLY Stop from hooks. All snapshot fields immutable, no retarget/copy/reset.
 * Unknown FAULT retains obligations and anchors; no public recovery.
 */
#ifndef BC250_SNAPSHOT_CPU_H
#define BC250_SNAPSHOT_CPU_H
#if !defined(BC250_DMA_ADAPTER_MOCK) || !defined(BC250_DMA_GATE_MOCK) || !defined(BC250_DMA_CAPTURE_MOCK)
#error Snapshot CPU association requires ALL RAM fake platforms
#endif
#ifdef _KERNEL_MODE
#error Snapshot CPU association is NEVER kernel integrated
#endif
#include "../dma-capture-20261004/bc250_dma_capture.h"
#include "domain-backend-20261003/bc250_vm_domain_backend.h"
extern BOOLEAN Bc250MockCaptureExecutionAllowed;
#define BC250_SC_ACTIVE 1U
#define BC250_SC_STOPPED 2U
#define BC250_SC_FAULT 3U
#define BC250_SC_DEAD 4U
typedef struct BC250_SNAPSHOT_CPU {
    const struct BC250_SNAPSHOT_CPU *Self;
    BC250_DMA_CAPTURE *Resource;
    BC250_VM_CPU_SESSION *Session;
    BC250_DMA_CAPTURE_SNAPSHOT Snapshot;
    BC250_DMA_CAPTURE_REQUEST Expected;
    BC250_BACKING_HANDLE Backing;
    unsigned int State, Busy, HasSnapshot, HasBacking, BackingReleased, SnapshotReleased;
} BC250_SNAPSHOT_CPU;

static __inline BC250_VM_SESSION_STATUS Bc250ScPolicy(void)
{
    if (!Bc250MockExecutionAllowed || !Bc250MockGateExecutionAllowed ||
        !Bc250MockCaptureExecutionAllowed) return BC250_VM_SESSION_INVALID;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) return BC250_VM_SESSION_INVALID;
    return BC250_VM_SESSION_OK;
}
static __inline BC250_VM_SESSION_STATUS Bc250ScGuard(BC250_SNAPSHOT_CPU *m)
{
    BC250_VM_SESSION_STATUS status = Bc250ScPolicy();
    if (status != BC250_VM_SESSION_OK) return status;
    if (!m || m->Self != m || !m->Resource || !m->Session) return BC250_VM_SESSION_INVALID;
    if (m->Busy) return BC250_VM_SESSION_BUSY_RESULT;
    if (m->State == BC250_SC_FAULT) return BC250_VM_SESSION_FAULT;
    if (m->State == BC250_SC_DEAD) return BC250_VM_SESSION_DEAD_RESULT;
    return BC250_VM_SESSION_OK;
}
static __inline BC250_VM_SESSION_STATUS Bc250ScNativeStatus(NTSTATUS status)
{
    if (status == STATUS_SUCCESS) return BC250_VM_SESSION_OK;
    if (status == STATUS_DEVICE_BUSY) return BC250_VM_SESSION_BUSY_RESULT;
    if (status == STATUS_INSUFFICIENT_RESOURCES) return BC250_VM_SESSION_FULL;
    if (status == STATUS_CANCELLED) return BC250_VM_SESSION_IN_USE;
    if (status == STATUS_INVALID_PARAMETER || status == STATUS_NOT_SUPPORTED) return BC250_VM_SESSION_INVALID;
    return BC250_VM_SESSION_FAULT;
}
static __inline int Bc250ScAway(BC250_SNAPSHOT_CPU *m, const void *buffer, size_t bytes)
{
    return Bc250DmaPageListDisjoint(m, sizeof(*m), buffer, bytes) &&
        Bc250DmaPageListDisjoint(m->Resource, sizeof(*m->Resource), buffer, bytes) &&
        Bc250VmSessionBufferAway(m->Session, buffer, bytes);
}
static __inline int Bc250ScSnapshotValid(BC250_SNAPSHOT_CPU *m)
{
    const BC250_DMA_CAPTURE_SNAPSHOT *s = &m->Snapshot;
    unsigned int i;
    if (!m->HasSnapshot || m->SnapshotReleased || s->Self != s ||
        s->State != BC250_DMA_CAPTURE_SNAPSHOT_LIVE || s->Reference.Owner != m->Resource ||
        !s->Reference.Id || s->Reference.Slot >= BC250_DMA_CAPTURE_SLOTS ||
        !s->PageCount || s->PageCount > BC250_DMA_CAPTURE_MAX_PAGES ||
        !s->ElementCount || s->ElementCount > s->PageCount ||
        s->Request.Offset != m->Expected.Offset || s->Request.Bytes != m->Expected.Bytes ||
        s->Request.Direction != m->Expected.Direction || s->Request.Direction > TRUE ||
        (s->Request.Offset & 4095ULL) || s->Request.Bytes != (ULONGLONG)s->PageCount * 4096ULL ||
        !s->MaximumLast || s->MaximumLast > BC250_AD_MAX48 ||
        (s->MaximumLast & (s->MaximumLast + 1ULL))) return 0;
    for (i = 0; i < s->PageCount; ++i)
        if (!Bc250VmDomainSpanValid(&s->Pages[i], BC250_AD_DMA_LOGICAL, s->MaximumLast) ||
            s->Pages[i].Bytes != 4096ULL || (s->Pages[i].Start & 4095ULL)) return 0;
    return 1;
}
static __inline int Bc250ScBackingMatches(BC250_SNAPSHOT_CPU *m)
{
    const BC250_BACKING_RECORD *r;
    unsigned int i;
    if (!m->HasBacking || m->BackingReleased || !Bc250ScSnapshotValid(m) ||
        !Bc250BackingHandleMatches(&m->Session->Backing, &m->Backing)) return 0;
    r = &m->Session->Backing.Records[m->Backing.Slot];
    if (r->PageCount != m->Snapshot.PageCount) return 0;
    for (i = 0; i < r->PageCount; ++i)
        if (r->Pages[i].Domain != BC250_ADDRESS_DMA_LOGICAL ||
            r->Pages[i].Start != m->Snapshot.Pages[i].Start ||
            r->Pages[i].Bytes != m->Snapshot.Pages[i].Bytes ||
            r->Pages[i].Last != m->Snapshot.Pages[i].Last) return 0;
    return 1;
}
static __inline BC250_VM_SESSION_STATUS Bc250ScCheck(BC250_SNAPSHOT_CPU *m)
{
    static const BC250_DMA_CAPTURE_SNAPSHOT zero = {0};
    unsigned int i;
    BC250_VM_SESSION_STATUS status = Bc250ScGuard(m);
    if (status != BC250_VM_SESSION_OK) return status;
    if ((m->State != BC250_SC_ACTIVE && m->State != BC250_SC_STOPPED) ||
        m->HasSnapshot > 1U || m->HasBacking > 1U || m->BackingReleased > 1U || m->SnapshotReleased > 1U ||
        !Bc250DmaPageListDisjoint(m, sizeof(*m), m->Resource, sizeof(*m->Resource)) ||
        !Bc250VmSessionBufferAway(m->Session, m, sizeof(*m)) ||
        !Bc250VmSessionBufferAway(m->Session, m->Resource, sizeof(*m->Resource))) return BC250_VM_SESSION_CORRUPT;
    status = Bc250VmSessionCheck(m->Session);
    if (status != BC250_VM_SESSION_OK) return status;
    if (m->HasSnapshot) {
        if (!m->SnapshotReleased) {
            if (!Bc250ScSnapshotValid(m)) return BC250_VM_SESSION_CORRUPT;
        } else if (m->Snapshot.Self != &m->Snapshot ||
            m->Snapshot.State != BC250_DMA_CAPTURE_SNAPSHOT_RELEASED ||
            m->Snapshot.Reference.Owner || m->Snapshot.Reference.Id || m->Snapshot.Reference.Slot)
            return BC250_VM_SESSION_CORRUPT;
    } else if (m->SnapshotReleased || memcmp(&m->Snapshot, &zero, sizeof(zero)))
        return BC250_VM_SESSION_CORRUPT;
    if (m->HasBacking) {
        if (!m->BackingReleased) {
            if (!Bc250ScBackingMatches(m)) return BC250_VM_SESSION_CORRUPT;
        } else if (Bc250BackingHandleMatches(&m->Session->Backing, &m->Backing))
            return BC250_VM_SESSION_CORRUPT;
    } else if (m->Backing.Pool || m->BackingReleased) return BC250_VM_SESSION_CORRUPT;
    for (i = 0; i < BC250_VM_SESSION_MAX_MAPPINGS; ++i) if (m->Session->Mappings[i].Live) {
        const BC250_VM_SESSION_MAPPING *r = &m->Session->Mappings[i];
        if (!m->HasBacking || m->BackingReleased || m->SnapshotReleased ||
            r->Lease.Slot != m->Backing.Slot || r->Lease.Generation != m->Backing.Generation)
            return BC250_VM_SESSION_CORRUPT;
    }
    if (m->State == BC250_SC_ACTIVE && (!m->HasBacking || m->BackingReleased || m->SnapshotReleased))
        return BC250_VM_SESSION_CORRUPT;
    return BC250_VM_SESSION_OK;
}
static __inline BC250_VM_SESSION_STATUS Bc250ScStop(BC250_SNAPSHOT_CPU *m)
{
    NTSTATUS native;
    if (Bc250ScPolicy() != BC250_VM_SESSION_OK || !m || m->Self != m || !m->Resource)
        return BC250_VM_SESSION_INVALID;
    if (m->State != BC250_SC_ACTIVE && m->State != BC250_SC_STOPPED) return BC250_VM_SESSION_IN_USE;
    m->State = BC250_SC_STOPPED;
    native = Bc250DmaCaptureStop(m->Resource);
    if (native != STATUS_SUCCESS) m->State = BC250_SC_FAULT;
    return Bc250ScNativeStatus(native); /* Latch only: no snapshot drop or CPU mutation. */
}
static __inline BC250_VM_SESSION_STATUS Bc250ScInit(BC250_SNAPSHOT_CPU *m,
    BC250_DMA_CAPTURE *resource, const BC250_DMA_CAPTURE_HANDLE *seed,
    const BC250_DMA_CAPTURE_REQUEST *expected, BC250_VM_CPU_SESSION *session)
{
    static const BC250_SNAPSHOT_CPU zero = {0};
    static const BC250_DMA_CAPTURE_SNAPSHOT empty = {0};
    unsigned int i;
    NTSTATUS native;
    BC250_VM_SESSION_STATUS status = Bc250ScPolicy();
    if (status != BC250_VM_SESSION_OK) return status;
    if (!m || !resource || !seed || !expected || !session || memcmp(m, &zero, sizeof(zero)))
        return BC250_VM_SESSION_INVALID;
    status = Bc250VmSessionCheck(session);
    if (status != BC250_VM_SESSION_OK) return status;
    if (!Bc250VmSessionBufferAway(session, m, sizeof(*m)) ||
        !Bc250VmSessionBufferAway(session, resource, sizeof(*resource)) ||
        !Bc250VmSessionBufferAway(session, seed, sizeof(*seed)) ||
        !Bc250VmSessionBufferAway(session, expected, sizeof(*expected)) ||
        !Bc250DmaPageListDisjoint(m, sizeof(*m), resource, sizeof(*resource)) ||
        !Bc250DmaPageListDisjoint(m, sizeof(*m), seed, sizeof(*seed)) ||
        !Bc250DmaPageListDisjoint(m, sizeof(*m), expected, sizeof(*expected)) ||
        !Bc250DmaPageListDisjoint(resource, sizeof(*resource), seed, sizeof(*seed)) ||
        !Bc250DmaPageListDisjoint(resource, sizeof(*resource), expected, sizeof(*expected)))
        return BC250_VM_SESSION_INVALID;
    for (i = 0; i < BC250_VM_SESSION_MAX_MAPPINGS; ++i)
        if (session->Mappings[i].Live) return BC250_VM_SESSION_IN_USE;
    for (i = 0; i < BC250_BACKING_SLOTS; ++i)
        if (session->Backing.Records[i].Registered) return BC250_VM_SESSION_IN_USE;
    m->Self = m; m->Resource = resource; m->Session = session; m->Expected = *expected;
    m->State = BC250_SC_ACTIVE; m->Busy = 1U;
    native = Bc250DmaCaptureSnapshot(resource, seed, expected, &m->Snapshot);
    if (m->Snapshot.Reference.Owner) m->HasSnapshot = 1U;
    if (native != STATUS_SUCCESS) {
        if (m->HasSnapshot || memcmp(&m->Snapshot, &empty, sizeof(empty)) ||
            Bc250ScNativeStatus(native) == BC250_VM_SESSION_FAULT) m->State = BC250_SC_FAULT;
        else if (m->State != BC250_SC_FAULT) m->State = BC250_SC_STOPPED;
        m->Busy = 0U; return Bc250ScNativeStatus(native);
    }
    if (m->State == BC250_SC_FAULT || !Bc250ScSnapshotValid(m)) {
        m->State = BC250_SC_FAULT; m->Busy = 0U; return BC250_VM_SESSION_FAULT;
    }
    if (session->Backend.MaximumDmaLast > m->Snapshot.MaximumLast) status = BC250_VM_SESSION_INVALID;
    else status = Bc250VmDomainRegister(session, m->Snapshot.Pages, m->Snapshot.PageCount,
        m->Snapshot.PageCount, &m->Backing);
    if (status == BC250_VM_SESSION_OK) m->HasBacking = 1U;
    else if (m->Backing.Pool || Bc250VmSessionCheck(session) != BC250_VM_SESSION_OK) m->State = BC250_SC_FAULT;
    else m->State = BC250_SC_STOPPED;
    m->Busy = 0U; return status;
}
static __inline BC250_VM_SESSION_STATUS Bc250ScMap(BC250_SNAPSHOT_CPU *m,
    BC250_GART_U64 va, unsigned int access, BC250_VM_SESSION_MAPPING_HANDLE *out,
    BC250_VM_CPU_JOURNAL *journal, BC250_VM_CPU_AFTER_WRITE hook, void *context)
{
    BC250_VM_SESSION_STATUS status = Bc250ScCheck(m);
    if (status != BC250_VM_SESSION_OK) {
        if (status == BC250_VM_SESSION_CORRUPT) m->State = BC250_SC_FAULT;
        return status;
    }
    if (m->State != BC250_SC_ACTIVE) return BC250_VM_SESSION_IN_USE;
    if (!Bc250ScAway(m, out, sizeof(*out)) || !Bc250ScAway(m, journal, sizeof(*journal)))
        return BC250_VM_SESSION_INVALID;
    m->Busy = 1U;
    status = Bc250VmSessionMap(m->Session, va, &m->Backing, access, out, journal, hook, context);
    if (Bc250VmSessionCheck(m->Session) != BC250_VM_SESSION_OK) m->State = BC250_SC_FAULT;
    m->Busy = 0U;
    return m->State == BC250_SC_FAULT ? BC250_VM_SESSION_FAULT : status;
}
static __inline BC250_VM_SESSION_STATUS Bc250ScUnmap(BC250_SNAPSHOT_CPU *m,
    const BC250_VM_SESSION_MAPPING_HANDLE *input, BC250_VM_CPU_JOURNAL *journal,
    BC250_VM_CPU_AFTER_WRITE hook, void *context)
{
    BC250_VM_SESSION_STATUS status = Bc250ScCheck(m);
    if (status != BC250_VM_SESSION_OK) {
        if (status == BC250_VM_SESSION_CORRUPT) m->State = BC250_SC_FAULT;
        return status;
    }
    if (!Bc250ScAway(m, input, sizeof(*input)) || !Bc250ScAway(m, journal, sizeof(*journal)))
        return BC250_VM_SESSION_INVALID;
    m->Busy = 1U;
    status = Bc250VmSessionUnmap(m->Session, input, journal, hook, context);
    if (Bc250VmSessionCheck(m->Session) != BC250_VM_SESSION_OK) m->State = BC250_SC_FAULT;
    m->Busy = 0U;
    return m->State == BC250_SC_FAULT ? BC250_VM_SESSION_FAULT : status;
}
static __inline BC250_VM_SESSION_STATUS Bc250ScRetire(BC250_SNAPSHOT_CPU *m)
{
    unsigned int i;
    NTSTATUS native;
    BC250_VM_SESSION_STATUS status = Bc250ScCheck(m);
    if (status != BC250_VM_SESSION_OK) {
        if (status == BC250_VM_SESSION_CORRUPT) m->State = BC250_SC_FAULT;
        return status;
    }
    status = Bc250ScStop(m);
    if (status != BC250_VM_SESSION_OK) return status;
    for (i = 0; i < BC250_VM_SESSION_MAX_MAPPINGS; ++i)
        if (m->Session->Mappings[i].Live) return BC250_VM_SESSION_IN_USE;
    m->Busy = 1U;
    if (m->HasBacking && !m->BackingReleased) {
        status = Bc250VmSessionRelease(m->Session, &m->Backing);
        if (status == BC250_VM_SESSION_OK) m->BackingReleased = 1U;
    }
    if (status == BC250_VM_SESSION_OK && m->HasSnapshot && !m->SnapshotReleased) {
        native = Bc250DmaCaptureSnapshotRelease(m->Resource, &m->Snapshot);
        status = Bc250ScNativeStatus(native);
        if (native == STATUS_SUCCESS) m->SnapshotReleased = 1U;
    }
    if (status == BC250_VM_SESSION_OK) {
        native = Bc250DmaCaptureRetire(m->Resource); status = Bc250ScNativeStatus(native);
        if (native == STATUS_SUCCESS) m->State = BC250_SC_DEAD;
        else if (native != STATUS_DEVICE_BUSY) m->State = BC250_SC_FAULT;
    }
    if (status == BC250_VM_SESSION_FAULT || status == BC250_VM_SESSION_CORRUPT ||
        status == BC250_VM_SESSION_INVALID) m->State = BC250_SC_FAULT;
    m->Busy = 0U; return status;
}
#endif
