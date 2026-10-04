/* SPDX-License-Identifier: Apache-2.0
 * ORIGINAL FIXTURE-ONLY introspection, NOT a Native/Leases address API.
 * Zero once; anchored Self/Resource/locked fake MDL/list/Session/facade and
 * valid disjoint buffers remain stable through ALL calls and persistent holds.
 * Exclusive WHOLE-call access, including Resource and associated Session.
 * Caller drops its seed once after Capture; no raw fields/Session/Leases calls
 * except TEST instrumentation. ONLY MlStop allowed in map hooks.
 * Private lock helpers below are available ONLY in the included RAM fixture.
 * No borrowed SG pointer escapes; numbers remain DMA_LOGICAL, never CPU PA/MC.
 */
#ifndef BC250_FIXTURE_BOUND_PAGES_H
#define BC250_FIXTURE_BOUND_PAGES_H
#if !defined(BC250_DMA_ADAPTER_MOCK) || !defined(BC250_DMA_GATE_MOCK) || !defined(BC250_DMA_LEASES_MOCK)
#error Bound pages require ALL RAM fake platforms
#endif
#ifdef _KERNEL_MODE
#error Bound page introspection is NEVER kernel integrated
#endif
#include "../dma-map-leases-20261004/bc250_dma_map_leases.h"
#include "dma-runs-20261003/bc250_dma_runs.h"
#define BC250_BP_WAIT 1U
#define BC250_BP_CAPTURED 2U
#define BC250_BP_REGISTERED 3U
#define BC250_BP_ATTACHED 4U
#define BC250_BP_RELEASED 5U
#define BC250_BP_FAULT 6U
typedef struct BC250_BP_SAMPLE {
    BC250_AD_SPAN Pages[BC250_VM_CPU_MAX_BATCH];
    BC250_AD_U64 Bytes, MaximumLast;
    unsigned int Count;
} BC250_BP_SAMPLE;
typedef struct BC250_BOUND_PAGES {
    const struct BC250_BOUND_PAGES *Self;
    BC250_DMA_LEASES *Resource;
    BC250_DMA_LEASE_HANDLE Hold;
    BC250_BP_SAMPLE Sample;
    BC250_VM_CPU_SESSION *Session;
    BC250_BACKING_HANDLE Backing;
    BC250_MAP_LEASES *Facade;
    unsigned int State, Busy, BackingReleased;
} BC250_BOUND_PAGES;

static __inline BC250_VM_SESSION_STATUS Bc250BpPolicy(void)
{
    if (!Bc250MockExecutionAllowed || !Bc250MockGateExecutionAllowed ||
        !Bc250MockLeasesExecutionAllowed) return BC250_VM_SESSION_INVALID;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) return BC250_VM_SESSION_INVALID;
    return BC250_VM_SESSION_OK;
}
static __inline BC250_VM_SESSION_STATUS Bc250BpGuard(BC250_BOUND_PAGES *s)
{
    BC250_VM_SESSION_STATUS status = Bc250BpPolicy();
    if (status != BC250_VM_SESSION_OK) return status;
    if (!s || s->Self != s || !s->Resource) return BC250_VM_SESSION_INVALID;
    if (s->Busy) return BC250_VM_SESSION_BUSY_RESULT;
    if (s->State == BC250_BP_FAULT) return BC250_VM_SESSION_FAULT;
    if (s->State == BC250_BP_RELEASED) return BC250_VM_SESSION_DEAD_RESULT;
    return BC250_VM_SESSION_OK;
}
/* Private fake source inspection: public Retain has ALREADY created Hold.
 * Gate -> Metadata; validate held identity/context/bounds before ANY element.
 * ALL sample values copied under locks; Finish has no owner access after Leave.
 */
static __inline NTSTATUS Bc250BpRead(BC250_BOUND_PAGES *s, BC250_BP_SAMPLE *out)
{
    BC250_DMA_LEASES *resource;
    BC250_DMA_GATE_TICKET ticket = {0};
    BC250_AD_SPAN runs[BC250_VM_CPU_MAX_BATCH];
    BC250_BP_SAMPLE result;
    PSCATTER_GATHER_LIST sg;
    ULONG i, count;
    NTSTATUS status;
    if (Bc250BpPolicy() != BC250_VM_SESSION_OK) return STATUS_NOT_SUPPORTED;
    if (!s || s->Self != s || !s->Resource || s->Busy != 1U || !out)
        return STATUS_INVALID_PARAMETER;
    resource = s->Resource;
    status = Bc250DmaLeasesAdmit(resource, &ticket);
    if (status != STATUS_SUCCESS) return status;
    status = Bc250DmaLeasesLock(resource);
    if (status == STATUS_SUCCESS) {
        status = Bc250DmaLeasesCheck(resource);
        if (status == STATUS_SUCCESS) status = Bc250DmaLeasesMatch(resource, &s->Hold);
        if (status == STATUS_SUCCESS && (!Bc250DmaLeasesBuffer(resource, s, sizeof(*s)) ||
            !Bc250DmaLeasesBuffer(resource, out, sizeof(*out)) ||
            !Bc250DmaLeasesDisjoint(s, sizeof(*s), out, sizeof(*out)) ||
            resource->Native.State != BC250_WIN_DMA_HELD || resource->Native.Busy ||
            !resource->Native.RequestIssued || resource->Native.Cancelled ||
            resource->PinnedList != resource->Native.List ||
            resource->PinnedMdl != resource->Native.HeldMdl ||
            !resource->Native.AddressWidth || resource->Native.AddressWidth > 48U))
            status = STATUS_INVALID_DEVICE_STATE;
        if (status == STATUS_SUCCESS) {
            sg = resource->PinnedList; count = sg->NumberOfElements;
            if (!count || count > BC250_VM_CPU_MAX_BATCH ||
                count > sizeof(sg->Elements) / sizeof(sg->Elements[0]) ||
                resource->PinnedListBytes != FIELD_OFFSET(SCATTER_GATHER_LIST, Elements) +
                    (SIZE_T)count * sizeof(sg->Elements[0]))
                status = STATUS_INVALID_DEVICE_STATE;
            else {
                memset(runs, 0, sizeof(runs)); memset(&result, 0, sizeof(result));
                result.MaximumLast = (1ULL << resource->Native.AddressWidth) - 1ULL;
                for (i = 0; i < count; ++i) {
                    runs[i].Domain = BC250_AD_DMA_LOGICAL;
                    runs[i].Start = (ULONGLONG)sg->Elements[i].Address.QuadPart;
                    runs[i].Bytes = sg->Elements[i].Length;
                    if (!runs[i].Bytes || runs[i].Start > result.MaximumLast ||
                        runs[i].Bytes - 1ULL > result.MaximumLast - runs[i].Start ||
                        result.Bytes > resource->Native.MaximumLength ||
                        runs[i].Bytes > resource->Native.MaximumLength - result.Bytes) {
                        status = STATUS_INVALID_DEVICE_STATE; break;
                    }
                    runs[i].Last = runs[i].Start + runs[i].Bytes - 1ULL;
                    result.Bytes += runs[i].Bytes;
                }
                if (status == STATUS_SUCCESS && !Bc250DmaRunsExpand(runs, count, count,
                    result.Bytes, result.MaximumLast, result.Pages,
                    BC250_VM_CPU_MAX_BATCH, &result.Count)) status = STATUS_INVALID_DEVICE_STATE;
                if (status == STATUS_SUCCESS) *out = result;
            }
        }
        Bc250DmaLeasesUnlock(resource);
    }
    return Bc250DmaLeasesFinish(resource, &ticket, status);
}
static __inline BC250_VM_SESSION_STATUS Bc250BpCapture(BC250_BOUND_PAGES *s,
    BC250_DMA_LEASES *resource, const BC250_DMA_LEASE_HANDLE *seed)
{
    BC250_BOUND_PAGES zero;
    NTSTATUS native;
    BC250_VM_SESSION_STATUS status = Bc250BpPolicy();
    if (status != BC250_VM_SESSION_OK) return status;
    memset(&zero, 0, sizeof(zero));
    if (!s || !resource || !seed || memcmp(s, &zero, sizeof(zero)) ||
        !Bc250DmaPageListDisjoint(s, sizeof(*s), resource, sizeof(*resource)) ||
        !Bc250DmaPageListDisjoint(s, sizeof(*s), seed, sizeof(*seed))) return BC250_VM_SESSION_INVALID;
    /* Valid full Source cannot overlap provider buffers: caller precondition,
     * also rechecked under locks. No arbitrary/untrusted pointer acceptance. */
    s->Self = s; s->Resource = resource; s->State = BC250_BP_WAIT; s->Busy = 1U;
    native = Bc250DmaLeasesRetain(resource, seed, &s->Hold);
    if (native == STATUS_SUCCESS) {
        BC250_BP_SAMPLE sample;
        native = Bc250BpRead(s, &sample);
        if (native == STATUS_SUCCESS) { s->Sample = sample; s->State = BC250_BP_CAPTURED; }
        else s->State = BC250_BP_FAULT; /* Hold retained, no automatic cleanup. */
    } else if (s->Hold.Owner || Bc250MlNativeStatus(native) == BC250_VM_SESSION_FAULT)
        s->State = BC250_BP_FAULT;
    s->Busy = 0U; return Bc250MlNativeStatus(native);
}
static __inline int Bc250BpBackingMatches(BC250_BOUND_PAGES *s)
{
    const BC250_BACKING_RECORD *r;
    unsigned int i;
    if (!s->Session || !s->Sample.Count || s->Sample.Count > BC250_VM_CPU_MAX_BATCH ||
        !Bc250BackingHandleMatches(&s->Session->Backing, &s->Backing)) return 0;
    r = &s->Session->Backing.Records[s->Backing.Slot];
    if (r->PageCount != s->Sample.Count) return 0;
    for (i = 0; i < r->PageCount; ++i) {
        const BC250_ADDRESS_SPAN *page = &r->Pages[i];
        if (!Bc250VmDomainSpanValid(&s->Sample.Pages[i], BC250_AD_DMA_LOGICAL, s->Sample.MaximumLast) ||
            s->Sample.Pages[i].Bytes != 4096ULL || page->Domain != BC250_ADDRESS_DMA_LOGICAL ||
            page->Start != s->Sample.Pages[i].Start || page->Bytes != s->Sample.Pages[i].Bytes ||
            page->Last != s->Sample.Pages[i].Last) return 0;
    }
    return 1;
}
static __inline BC250_VM_SESSION_STATUS Bc250BpRegister(BC250_BOUND_PAGES *s,
    BC250_VM_CPU_SESSION *session)
{
    BC250_BP_SAMPLE fresh;
    unsigned int i;
    NTSTATUS native;
    BC250_VM_SESSION_STATUS status = Bc250BpGuard(s);
    if (status != BC250_VM_SESSION_OK) return status;
    if (s->State != BC250_BP_CAPTURED || !session ||
        !Bc250VmSessionBufferAway(session, s, sizeof(*s)) ||
        !Bc250VmSessionBufferAway(session, s->Resource, sizeof(*s->Resource))) return BC250_VM_SESSION_INVALID;
    status = Bc250VmSessionCheck(session);
    if (status != BC250_VM_SESSION_OK) return status;
    for (i = 0; i < 16; ++i) if (session->Mappings[i].Live) return BC250_VM_SESSION_IN_USE;
    s->Busy = 1U;
    native = Bc250BpRead(s, &fresh);
    if (native != STATUS_SUCCESS || memcmp(&fresh, &s->Sample, sizeof(fresh))) {
        s->State = BC250_BP_FAULT; s->Busy = 0U; return BC250_VM_SESSION_FAULT;
    }
    s->Session = session;
    status = Bc250VmDomainRegister(session, s->Sample.Pages, s->Sample.Count,
        s->Sample.Count, &s->Backing);
    if (status == BC250_VM_SESSION_OK) s->State = BC250_BP_REGISTERED;
    else if (s->Backing.Pool || Bc250VmSessionCheck(session) != BC250_VM_SESSION_OK) s->State = BC250_BP_FAULT;
    else s->Session = NULL; /* Known no-backing failure; retain Capture for retry/Drop. */
    s->Busy = 0U; return status;
}
static __inline BC250_VM_SESSION_STATUS Bc250BpAttach(BC250_BOUND_PAGES *s,
    BC250_MAP_LEASES *facade)
{
    BC250_VM_SESSION_STATUS status = Bc250BpGuard(s);
    if (status != BC250_VM_SESSION_OK) return status;
    if (s->State != BC250_BP_REGISTERED || !facade || !Bc250BpBackingMatches(s) ||
        !Bc250VmSessionBufferAway(s->Session, facade, sizeof(*facade)) ||
        !Bc250DmaPageListDisjoint(s, sizeof(*s), facade, sizeof(*facade)) ||
        !Bc250DmaPageListDisjoint(s->Resource, sizeof(*s->Resource), facade, sizeof(*facade)))
        return BC250_VM_SESSION_INVALID;
    s->Busy = 1U; s->Facade = facade;
    status = Bc250MlInit(facade, s->Resource, &s->Hold, s->Session, &s->Backing);
    if (status == BC250_VM_SESSION_OK) s->State = BC250_BP_ATTACHED;
    else s->State = BC250_BP_FAULT; /* Failed Init is not reset/recovery. */
    s->Busy = 0U; return status;
}
static __inline BC250_VM_SESSION_STATUS Bc250BpMap(BC250_BOUND_PAGES *s,
    BC250_GART_U64 va, unsigned int access, BC250_MAP_LEASE_HANDLE *out,
    BC250_VM_CPU_JOURNAL *journal, BC250_VM_CPU_AFTER_WRITE hook, void *context)
{
    BC250_VM_SESSION_STATUS status = Bc250BpGuard(s);
    if (status != BC250_VM_SESSION_OK) return status;
    if (s->State != BC250_BP_ATTACHED || !s->Facade || !Bc250BpBackingMatches(s) ||
        !Bc250DmaPageListDisjoint(s, sizeof(*s), out, sizeof(*out)) ||
        !Bc250DmaPageListDisjoint(s, sizeof(*s), journal, sizeof(*journal))) return BC250_VM_SESSION_INVALID;
    s->Busy = 1U;
    status = Bc250MlMap(s->Facade, va, access, out, journal, hook, context);
    s->Busy = 0U; return status;
}
/* No native cleanup here. A successful final Drop STILL leaves native HELD;
 * caller retries MlRetire (attached) or LeasesRetire (unassociated) afterward. */
static __inline BC250_VM_SESSION_STATUS Bc250BpRelease(BC250_BOUND_PAGES *s)
{
    unsigned int i;
    NTSTATUS native;
    BC250_VM_SESSION_STATUS status = Bc250BpGuard(s);
    if (status != BC250_VM_SESSION_OK) return status;
    if (s->State != BC250_BP_WAIT && s->State != BC250_BP_CAPTURED &&
        s->State != BC250_BP_REGISTERED && s->State != BC250_BP_ATTACHED) return BC250_VM_SESSION_INVALID;
    if (s->State == BC250_BP_ATTACHED) {
        BC250_MAP_LEASES *m = s->Facade;
        if (!m || m->Resource != s->Resource || m->Session != s->Session ||
            m->Backing.Pool != s->Backing.Pool || m->Backing.Slot != s->Backing.Slot ||
            m->Backing.Generation != s->Backing.Generation || m->Busy ||
            m->State != BC250_ML_STOPPED || !m->BackingReleased || !m->RootDropped)
            return BC250_VM_SESSION_IN_USE;
        status = Bc250MlCheck(m);
        if (status != BC250_VM_SESSION_OK) return status;
        for (i = 0; i < 16; ++i) if (m->Records[i].Phase) return BC250_VM_SESSION_IN_USE;
        if (Bc250BackingHandleMatches(&s->Session->Backing, &s->Backing)) return BC250_VM_SESSION_IN_USE;
    }
    if (s->Session) {
        status = Bc250VmSessionCheck(s->Session);
        if (status != BC250_VM_SESSION_OK) return status;
        for (i = 0; i < 16; ++i) if (s->Session->Mappings[i].Live) return BC250_VM_SESSION_IN_USE;
    }
    s->Busy = 1U;
    if (s->State == BC250_BP_REGISTERED && !s->BackingReleased) {
        if (!Bc250BpBackingMatches(s)) { s->State = BC250_BP_FAULT; s->Busy = 0U; return BC250_VM_SESSION_FAULT; }
        status = Bc250VmSessionRelease(s->Session, &s->Backing);
        if (status == BC250_VM_SESSION_OK) s->BackingReleased = 1U;
        else { s->Busy = 0U; return status; }
    }
    native = s->Hold.Owner ? Bc250DmaLeasesDrop(s->Resource, &s->Hold) : STATUS_SUCCESS;
    if (native == STATUS_SUCCESS) { memset(&s->Hold, 0, sizeof(s->Hold)); s->State = BC250_BP_RELEASED; }
    else if (native != STATUS_DEVICE_BUSY) s->State = BC250_BP_FAULT;
    s->Busy = 0U; return Bc250MlNativeStatus(native);
}
#endif
