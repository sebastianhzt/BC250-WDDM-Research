/* SPDX-License-Identifier: Apache-2.0
 * Original SERIALIZED RAM-only resource-lifetime contract, NOT a Windows DMA
 * owner/adapter, MDL pin, cancellation API, IRQL protocol or GPU consumer.
 * Begin/Deliver/ResolveNoResource are simulated provider events. A matching
 * Deliver with non-NULL cookie consumes the resource even if runs are invalid;
 * stale/duplicate/BUSY/NULL-cookie deliveries leave pre-call ownership unchanged.
 * Consumed=0 NEVER returns a previously accepted resource to the provider.
 * Provider must deliver once, or confirm no resource AND no future callback.
 * Cancellation alone is not that confirmation. Keep owner/session/provider and
 * request context alive until resolution. Release is trusted and infallible.
 * Shut down every owner before destroying the shared CPU session.
 *
 * Zeroed stable owner once, exclusive access across all APIs and callbacks,
 * valid/stable/disjoint buffers, honest provider; no direct session/field edits
 * for this owner's backing. Callback cannot mutate/retain internals. BUSY is
 * synchronous reentry protection, NOT thread synchronization. Handles are not
 * security capabilities. No reset/reinit/reuse while old tickets may exist.
 * Resource can NEVER be exposed to hardware: CPU Unmap is NOT GPU quiescence.
 */
#ifndef BC250_DMA_OWNER_H
#define BC250_DMA_OWNER_H
#include "dma-runs-20261003/bc250_dma_runs.h"

#define BC250_DMA_OWNER_IDLE 1U
#define BC250_DMA_OWNER_PENDING 2U
#define BC250_DMA_OWNER_CANCEL_WAIT 3U
#define BC250_DMA_OWNER_READY 4U
#define BC250_DMA_OWNER_DRAINING 5U
#define BC250_DMA_OWNER_DEAD 6U
typedef struct BC250_DMA_OWNER BC250_DMA_OWNER;
typedef struct BC250_DMA_OWNER_TICKET {
    const BC250_DMA_OWNER *Owner;
    BC250_AD_U64 Id;
} BC250_DMA_OWNER_TICKET;
typedef struct BC250_DMA_OWNER_DELIVERY_RESULT {
    BC250_VM_SESSION_STATUS Status;
    unsigned int Consumed;
} BC250_DMA_OWNER_DELIVERY_RESULT;
typedef void (*BC250_DMA_OWNER_PUT)(void *context, void *cookie, unsigned int write_to_device);
struct BC250_DMA_OWNER {
    BC250_VM_CPU_SESSION *Session;
    BC250_DMA_OWNER_PUT Put;
    void *ProviderContext, *Cookie;
    BC250_BACKING_HANDLE Backing;
    BC250_AD_U64 LastId, ExpectedBytes;
    unsigned int State, Busy, WriteToDevice;
};

static __inline BC250_VM_SESSION_STATUS Bc250DmaOwnerCheck(const BC250_DMA_OWNER *owner)
{
    BC250_BACKING_HANDLE empty = {0};
    BC250_VM_SESSION_STATUS status;
    if (!owner) return BC250_VM_SESSION_INVALID;
    if (owner->Busy) return BC250_VM_SESSION_BUSY_RESULT;
    if (owner->State == BC250_DMA_OWNER_DEAD) return BC250_VM_SESSION_DEAD_RESULT;
    if (!owner->Session || !owner->Put || owner->State < BC250_DMA_OWNER_IDLE ||
        owner->State > BC250_DMA_OWNER_DRAINING) return BC250_VM_SESSION_INVALID;
    status = Bc250VmSessionCheck(owner->Session);
    if (status != BC250_VM_SESSION_OK) return status;
    if (!Bc250VmSessionBufferAway(owner->Session, owner, sizeof(*owner))) return BC250_VM_SESSION_CORRUPT;
    if (owner->State == BC250_DMA_OWNER_IDLE)
        return !owner->Cookie && !owner->ExpectedBytes && !owner->WriteToDevice &&
            !memcmp(&owner->Backing, &empty, sizeof(empty)) ? BC250_VM_SESSION_OK : BC250_VM_SESSION_CORRUPT;
    if (!owner->LastId || !owner->ExpectedBytes || (owner->ExpectedBytes & 4095ULL) ||
        owner->ExpectedBytes > 64ULL * 4096ULL || owner->WriteToDevice > 1U) return BC250_VM_SESSION_CORRUPT;
    if (owner->State == BC250_DMA_OWNER_PENDING || owner->State == BC250_DMA_OWNER_CANCEL_WAIT)
        return !owner->Cookie && !memcmp(&owner->Backing, &empty, sizeof(empty)) ?
            BC250_VM_SESSION_OK : BC250_VM_SESSION_CORRUPT;
    if (!owner->Cookie || !Bc250BackingHandleMatches(&owner->Session->Backing, &owner->Backing) ||
        (BC250_AD_U64)owner->Session->Backing.Records[owner->Backing.Slot].PageCount * 4096ULL != owner->ExpectedBytes)
        return BC250_VM_SESSION_CORRUPT;
    return BC250_VM_SESSION_OK;
}
static __inline int Bc250DmaOwnerAway(const BC250_DMA_OWNER *owner, const void *buffer, BC250_AD_U64 bytes)
{
    return Bc250DmaPageListDisjoint(owner, sizeof(*owner), buffer, bytes) &&
        Bc250VmSessionBufferAway(owner->Session, buffer, bytes);
}
static __inline BC250_VM_SESSION_STATUS Bc250DmaOwnerTicketCheck(const BC250_DMA_OWNER *owner,
    const BC250_DMA_OWNER_TICKET *ticket)
{
    BC250_VM_SESSION_STATUS status = Bc250DmaOwnerCheck(owner);
    if (status != BC250_VM_SESSION_OK) return status;
    if (!ticket || !Bc250DmaOwnerAway(owner, ticket, sizeof(*ticket))) return BC250_VM_SESSION_INVALID;
    return owner->State != BC250_DMA_OWNER_IDLE && ticket->Owner == owner &&
        ticket->Id && ticket->Id == owner->LastId ? BC250_VM_SESSION_OK : BC250_VM_SESSION_STALE;
}
/* Internal: only after matched request resolution; preserve monotonically
 * consumed LastId and stable provider/session identity. */
static __inline void Bc250DmaOwnerIdle(BC250_DMA_OWNER *owner)
{
    owner->Cookie = NULL; owner->ExpectedBytes = 0; owner->WriteToDevice = 0;
    memset(&owner->Backing, 0, sizeof(owner->Backing)); owner->State = BC250_DMA_OWNER_IDLE;
}
static __inline BC250_VM_SESSION_STATUS Bc250DmaOwnerInit(BC250_DMA_OWNER *owner,
    BC250_VM_CPU_SESSION *session, BC250_DMA_OWNER_PUT put, void *context)
{
    BC250_DMA_OWNER zero;
    BC250_VM_SESSION_STATUS status;
    if (!owner) return BC250_VM_SESSION_INVALID;
    if (owner->Busy) return BC250_VM_SESSION_BUSY_RESULT;
    if (owner->State == BC250_DMA_OWNER_DEAD) return BC250_VM_SESSION_DEAD_RESULT;
    memset(&zero, 0, sizeof(zero));
    if (memcmp(owner, &zero, sizeof(zero)) || !put) return BC250_VM_SESSION_INVALID;
    status = Bc250VmSessionCheck(session);
    if (status != BC250_VM_SESSION_OK) return status;
    if (!Bc250VmSessionBufferAway(session, owner, sizeof(*owner))) return BC250_VM_SESSION_INVALID;
    owner->Session = session; owner->Put = put; owner->ProviderContext = context;
    owner->State = BC250_DMA_OWNER_IDLE;
    return BC250_VM_SESSION_OK;
}
static __inline BC250_VM_SESSION_STATUS Bc250DmaOwnerBegin(BC250_DMA_OWNER *owner,
    BC250_AD_U64 bytes, unsigned int write_to_device, BC250_DMA_OWNER_TICKET *out)
{
    BC250_DMA_OWNER_TICKET result = {0};
    BC250_VM_SESSION_STATUS status = Bc250DmaOwnerCheck(owner);
    if (status != BC250_VM_SESSION_OK) return status;
    if (!out || !bytes || (bytes & 4095ULL) || bytes > 64ULL * 4096ULL ||
        write_to_device > 1U || !Bc250DmaOwnerAway(owner, out, sizeof(*out))) return BC250_VM_SESSION_INVALID;
    if (owner->State != BC250_DMA_OWNER_IDLE) return BC250_VM_SESSION_IN_USE;
    if (owner->LastId == BC250_GART_U64_MAX) return BC250_VM_SESSION_EXHAUSTED;
    result.Owner = owner; result.Id = ++owner->LastId;
    owner->ExpectedBytes = bytes; owner->WriteToDevice = write_to_device;
    owner->State = BC250_DMA_OWNER_PENDING; *out = result;
    return BC250_VM_SESSION_OK;
}
static __inline BC250_VM_SESSION_STATUS Bc250DmaOwnerCancel(BC250_DMA_OWNER *owner,
    const BC250_DMA_OWNER_TICKET *ticket)
{
    BC250_VM_SESSION_STATUS status = Bc250DmaOwnerTicketCheck(owner, ticket);
    if (status != BC250_VM_SESSION_OK) return status;
    if (owner->State == BC250_DMA_OWNER_PENDING) owner->State = BC250_DMA_OWNER_CANCEL_WAIT;
    else if (owner->State == BC250_DMA_OWNER_READY) owner->State = BC250_DMA_OWNER_DRAINING;
    return BC250_VM_SESSION_OK; /* repeated request is idempotent, not an OS cancel */
}
/* Simulated provider proof: allocation failed or cancellation completed with
 * no resource and no possible future delivery. Never infer this from timeout. */
static __inline BC250_VM_SESSION_STATUS Bc250DmaOwnerResolveNoResource(BC250_DMA_OWNER *owner,
    const BC250_DMA_OWNER_TICKET *ticket)
{
    BC250_VM_SESSION_STATUS status = Bc250DmaOwnerTicketCheck(owner, ticket);
    if (status != BC250_VM_SESSION_OK) return status;
    if (owner->State != BC250_DMA_OWNER_PENDING && owner->State != BC250_DMA_OWNER_CANCEL_WAIT)
        return BC250_VM_SESSION_IN_USE;
    Bc250DmaOwnerIdle(owner);
    return BC250_VM_SESSION_OK;
}
static __inline BC250_DMA_OWNER_DELIVERY_RESULT Bc250DmaOwnerDeliver(BC250_DMA_OWNER *owner,
    const BC250_DMA_OWNER_TICKET *ticket, void *cookie,
    const BC250_AD_SPAN *runs, unsigned int count, unsigned int capacity)
{
    BC250_BACKING_HANDLE backing = {0};
    BC250_DMA_OWNER_DELIVERY_RESULT result;
    BC250_VM_SESSION_STATUS status = Bc250DmaOwnerTicketCheck(owner, ticket);
    result.Status = status; result.Consumed = 0U;
    if (status != BC250_VM_SESSION_OK) return result;
    if (owner->State != BC250_DMA_OWNER_PENDING && owner->State != BC250_DMA_OWNER_CANCEL_WAIT) {
        result.Status = BC250_VM_SESSION_STALE; return result;
    }
    if (!cookie) { result.Status = BC250_VM_SESSION_INVALID; return result; }
    /* Ownership transfers HERE. Cancelled and malformed-list completions must
     * release this resource too. Cookie is opaque and never dereferenced here. */
    owner->Busy = 1U; result.Consumed = 1U;
    if (owner->State == BC250_DMA_OWNER_CANCEL_WAIT) status = BC250_VM_SESSION_OK;
    else if (!runs || !count || count > 64U || count > capacity ||
        !Bc250DmaOwnerAway(owner, runs, (BC250_AD_U64)count * sizeof(*runs))) status = BC250_VM_SESSION_INVALID;
    else status = Bc250VmDomainRegisterRuns(owner->Session, runs, count, capacity, owner->ExpectedBytes, &backing);
    if (owner->State == BC250_DMA_OWNER_CANCEL_WAIT || status != BC250_VM_SESSION_OK) {
        owner->Put(owner->ProviderContext, cookie, owner->WriteToDevice);
        Bc250DmaOwnerIdle(owner);
    } else {
        owner->Cookie = cookie; owner->Backing = backing; owner->State = BC250_DMA_OWNER_READY;
    }
    owner->Busy = 0U;
    result.Status = status;
    return result;
}
static __inline BC250_VM_SESSION_STATUS Bc250DmaOwnerMap(BC250_DMA_OWNER *owner,
    const BC250_DMA_OWNER_TICKET *ticket, const BC250_AD_SPAN *va, unsigned int access,
    BC250_VM_SESSION_MAPPING_HANDLE *out, BC250_VM_CPU_JOURNAL *journal,
    BC250_VM_CPU_AFTER_WRITE hook, void *context)
{
    BC250_VM_SESSION_STATUS status = Bc250DmaOwnerTicketCheck(owner, ticket);
    if (status != BC250_VM_SESSION_OK) return status;
    if (owner->State != BC250_DMA_OWNER_READY) return BC250_VM_SESSION_IN_USE;
    if (!va || !out || !journal || !Bc250DmaOwnerAway(owner, va, sizeof(*va)) ||
        !Bc250DmaOwnerAway(owner, out, sizeof(*out)) || !Bc250DmaOwnerAway(owner, journal, sizeof(*journal)) ||
        !Bc250DmaPageListDisjoint(ticket, sizeof(*ticket), out, sizeof(*out)) ||
        !Bc250DmaPageListDisjoint(ticket, sizeof(*ticket), journal, sizeof(*journal))) return BC250_VM_SESSION_INVALID;
    owner->Busy = 1U;
    status = Bc250VmDomainMap(owner->Session, va, &owner->Backing, access, out, journal, hook, context);
    owner->Busy = 0U;
    return status;
}
static __inline BC250_VM_SESSION_STATUS Bc250DmaOwnerUnmap(BC250_DMA_OWNER *owner,
    const BC250_DMA_OWNER_TICKET *ticket, const BC250_VM_SESSION_MAPPING_HANDLE *mapping,
    BC250_VM_CPU_JOURNAL *journal, BC250_VM_CPU_AFTER_WRITE hook, void *context)
{
    const BC250_VM_SESSION_MAPPING *record;
    BC250_VM_SESSION_STATUS status = Bc250DmaOwnerTicketCheck(owner, ticket);
    if (status != BC250_VM_SESSION_OK) return status;
    if (owner->State != BC250_DMA_OWNER_READY && owner->State != BC250_DMA_OWNER_DRAINING)
        return BC250_VM_SESSION_IN_USE;
    if (!mapping || !journal || !Bc250DmaOwnerAway(owner, mapping, sizeof(*mapping)) ||
        !Bc250DmaOwnerAway(owner, journal, sizeof(*journal)) ||
        !Bc250DmaPageListDisjoint(ticket, sizeof(*ticket), journal, sizeof(*journal))) return BC250_VM_SESSION_INVALID;
    if (mapping->Session != owner->Session || mapping->Slot >= BC250_VM_SESSION_MAX_MAPPINGS || !mapping->Id)
        return BC250_VM_SESSION_STALE;
    record = &owner->Session->Mappings[mapping->Slot];
    if (!record->Live || record->Id != mapping->Id || record->Lease.Slot != owner->Backing.Slot ||
        record->Lease.Generation != owner->Backing.Generation) return BC250_VM_SESSION_STALE;
    owner->Busy = 1U;
    status = Bc250VmSessionUnmap(owner->Session, mapping, journal, hook, context);
    owner->Busy = 0U;
    return status;
}
static __inline BC250_VM_SESSION_STATUS Bc250DmaOwnerRelease(BC250_DMA_OWNER *owner,
    const BC250_DMA_OWNER_TICKET *ticket)
{
    BC250_VM_SESSION_STATUS status = Bc250DmaOwnerTicketCheck(owner, ticket);
    if (status != BC250_VM_SESSION_OK) return status;
    if (owner->State != BC250_DMA_OWNER_READY && owner->State != BC250_DMA_OWNER_DRAINING)
        return BC250_VM_SESSION_IN_USE;
    owner->Busy = 1U;
    status = Bc250VmSessionRelease(owner->Session, &owner->Backing);
    if (status == BC250_VM_SESSION_OK) {
        owner->Put(owner->ProviderContext, owner->Cookie, owner->WriteToDevice);
        Bc250DmaOwnerIdle(owner);
    }
    owner->Busy = 0U;
    return status;
}
static __inline BC250_VM_SESSION_STATUS Bc250DmaOwnerShutdown(BC250_DMA_OWNER *owner)
{
    BC250_VM_SESSION_STATUS status = Bc250DmaOwnerCheck(owner);
    if (status != BC250_VM_SESSION_OK) return status;
    if (owner->State != BC250_DMA_OWNER_IDLE) return BC250_VM_SESSION_IN_USE;
    owner->State = BC250_DMA_OWNER_DEAD;
    return BC250_VM_SESSION_OK; /* does not destroy the shared CPU session */
}
#endif
