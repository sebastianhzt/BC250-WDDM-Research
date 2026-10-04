/* SPDX-License-Identifier: Apache-2.0
 * Serialized RAM event model, NOT Windows PnP/rundown, locks or IRP cancellation.
 * Attach once to one exclusively owned bridge. After attachment, access the
 * bridge ONLY via this facade; objects/session/buffers stay stable and disjoint.
 * Every admitted ticket is ended exactly once, even after failure or stop.
 * RequestStop is the ONLY event allowed from a synchronous in-call hook. It
 * latches admission closure, does not touch the bridge or free resources.
 */
#ifndef BC250_DMA_LIFECYCLE_H
#define BC250_DMA_LIFECYCLE_H
#include "../dma-bridge-20261004/bc250_dma_bridge.h"

#define BC250_DMA_LIFE_SIGNATURE 0x4C494631U
#define BC250_DMA_LIFE_RUNNING 1U
#define BC250_DMA_LIFE_STOPPING 2U
#define BC250_DMA_LIFE_DRAINING 3U
#define BC250_DMA_LIFE_REMOVED 4U
#define BC250_DMA_LIFE_FAULT 5U
#define BC250_DMA_LIFE_MAP 1U
#define BC250_DMA_LIFE_UNMAP 2U
#define BC250_DMA_LIFE_START 3U
#define BC250_DMA_LIFE_SLOTS 16U
typedef struct BC250_DMA_LIFECYCLE BC250_DMA_LIFECYCLE;
typedef struct BC250_DMA_LIFE_TICKET {
    const BC250_DMA_LIFECYCLE *Life;
    BC250_AD_U64 Id;
    unsigned int Slot;
} BC250_DMA_LIFE_TICKET;
typedef struct BC250_DMA_LIFE_RECORD {
    BC250_AD_U64 Id;
    unsigned int Live, Kind, Used;
} BC250_DMA_LIFE_RECORD;
struct BC250_DMA_LIFECYCLE {
    BC250_DMA_BRIDGE *Bridge;
    BC250_DMA_LIFE_RECORD Records[BC250_DMA_LIFE_SLOTS];
    BC250_AD_U64 LastId;
    unsigned int Signature, State, Busy, Active;
};

static __inline BC250_VM_SESSION_STATUS Bc250DmaLifeMeta(BC250_DMA_LIFECYCLE *life)
{
    unsigned int i, j, count = 0U;
    if (!Bc250MockExecutionAllowed || KeGetCurrentIrql() != PASSIVE_LEVEL || !life)
        return BC250_VM_SESSION_INVALID;
    if (life->Signature != BC250_DMA_LIFE_SIGNATURE || !life->Bridge ||
        life->State < BC250_DMA_LIFE_RUNNING || life->State > BC250_DMA_LIFE_FAULT)
        return BC250_VM_SESSION_INVALID;
    if (life->Busy > 1U) return BC250_VM_SESSION_CORRUPT;
    for (i = 0; i < BC250_DMA_LIFE_SLOTS; ++i) {
        const BC250_DMA_LIFE_RECORD *record = &life->Records[i];
        if (!record->Live) continue;
        if (record->Live != 1U || !record->Id || record->Id > life->LastId ||
            record->Kind < BC250_DMA_LIFE_MAP || record->Kind > BC250_DMA_LIFE_START ||
            record->Used > 1U) return BC250_VM_SESSION_CORRUPT;
        for (j = 0; j < i; ++j) if (life->Records[j].Live && life->Records[j].Id == record->Id)
            return BC250_VM_SESSION_CORRUPT;
        ++count;
    }
    return count == life->Active ? BC250_VM_SESSION_OK : BC250_VM_SESSION_CORRUPT;
}
static __inline BC250_VM_SESSION_STATUS Bc250DmaLifeGuard(BC250_DMA_LIFECYCLE *life)
{
    BC250_VM_SESSION_STATUS status = Bc250DmaLifeMeta(life);
    if (status != BC250_VM_SESSION_OK) return status;
    if (life->Busy) return BC250_VM_SESSION_BUSY_RESULT;
    if (life->State == BC250_DMA_LIFE_REMOVED) return BC250_VM_SESSION_DEAD_RESULT;
    if (life->State == BC250_DMA_LIFE_FAULT) return BC250_VM_SESSION_FAULT;
    return BC250_VM_SESSION_OK;
}
static __inline int Bc250DmaLifeAway(BC250_DMA_LIFECYCLE *life, const void *buffer, BC250_AD_U64 bytes)
{
    return Bc250DmaPageListDisjoint(life, sizeof(*life), buffer, bytes) &&
        Bc250DmaBridgeAway(life->Bridge, buffer, bytes);
}
static __inline BC250_VM_SESSION_STATUS Bc250DmaLifeInit(BC250_DMA_LIFECYCLE *life,
    BC250_DMA_BRIDGE *bridge)
{
    BC250_DMA_LIFECYCLE zero;
    BC250_VM_SESSION_STATUS status;
    if (!Bc250MockExecutionAllowed || KeGetCurrentIrql() != PASSIVE_LEVEL || !life)
        return BC250_VM_SESSION_INVALID;
    if (life->Busy) return BC250_VM_SESSION_BUSY_RESULT;
    memset(&zero, 0, sizeof(zero));
    if (memcmp(life, &zero, sizeof(zero))) return BC250_VM_SESSION_INVALID;
    status = Bc250DmaBridgeCheck(bridge);
    if (status != BC250_VM_SESSION_OK) return status;
    if (!Bc250DmaBridgeAway(bridge, life, sizeof(*life))) return BC250_VM_SESSION_INVALID;
    life->Bridge = bridge; life->Signature = BC250_DMA_LIFE_SIGNATURE;
    life->State = BC250_DMA_LIFE_RUNNING; return BC250_VM_SESSION_OK;
}
static __inline BC250_VM_SESSION_STATUS Bc250DmaLifeEnter(BC250_DMA_LIFECYCLE *life,
    unsigned int kind, BC250_DMA_LIFE_TICKET *out)
{
    BC250_DMA_LIFE_TICKET ticket = {0};
    unsigned int i;
    BC250_VM_SESSION_STATUS status = Bc250DmaLifeGuard(life);
    if (status != BC250_VM_SESSION_OK) return status;
    if (!out || kind < BC250_DMA_LIFE_MAP || kind > BC250_DMA_LIFE_START ||
        !Bc250DmaLifeAway(life, out, sizeof(*out))) return BC250_VM_SESSION_INVALID;
    if (life->State != BC250_DMA_LIFE_RUNNING && kind != BC250_DMA_LIFE_UNMAP)
        return BC250_VM_SESSION_IN_USE;
    if (life->LastId == BC250_GART_U64_MAX) return BC250_VM_SESSION_EXHAUSTED;
    for (i = 0; i < BC250_DMA_LIFE_SLOTS; ++i) if (!life->Records[i].Live) break;
    if (i == BC250_DMA_LIFE_SLOTS) return BC250_VM_SESSION_FULL;
    ticket.Life = life; ticket.Id = ++life->LastId; ticket.Slot = i;
    life->Records[i].Id = ticket.Id; life->Records[i].Kind = kind;
    life->Records[i].Used = 0U; life->Records[i].Live = 1U; ++life->Active;
    *out = ticket; return BC250_VM_SESSION_OK;
}
static __inline BC250_VM_SESSION_STATUS Bc250DmaLifeTicket(BC250_DMA_LIFECYCLE *life,
    const BC250_DMA_LIFE_TICKET *ticket)
{
    if (!ticket || !Bc250DmaLifeAway(life, ticket, sizeof(*ticket))) return BC250_VM_SESSION_INVALID;
    if (ticket->Life != life || ticket->Slot >= BC250_DMA_LIFE_SLOTS || !ticket->Id ||
        !life->Records[ticket->Slot].Live || life->Records[ticket->Slot].Id != ticket->Id)
        return BC250_VM_SESSION_STALE;
    return BC250_VM_SESSION_OK;
}
static __inline BC250_VM_SESSION_STATUS Bc250DmaLifeEnd(BC250_DMA_LIFECYCLE *life,
    const BC250_DMA_LIFE_TICKET *ticket)
{
    /* End remains available in FAULT: acknowledging caller completion is not
     * resource recovery and must not erase an unknown held bridge/MDL.
     */
    BC250_VM_SESSION_STATUS status = Bc250DmaLifeMeta(life);
    if (status != BC250_VM_SESSION_OK) return status;
    if (life->Busy) return BC250_VM_SESSION_BUSY_RESULT;
    status = Bc250DmaLifeTicket(life, ticket);
    if (status != BC250_VM_SESSION_OK) return status;
    memset(&life->Records[ticket->Slot], 0, sizeof(life->Records[ticket->Slot]));
    --life->Active; return BC250_VM_SESSION_OK;
}
static __inline BC250_VM_SESSION_STATUS Bc250DmaLifeRequestStop(BC250_DMA_LIFECYCLE *life)
{
    BC250_VM_SESSION_STATUS status = Bc250DmaLifeMeta(life);
    if (status != BC250_VM_SESSION_OK) return status;
    if (life->State == BC250_DMA_LIFE_REMOVED) return BC250_VM_SESSION_DEAD_RESULT;
    if (life->State == BC250_DMA_LIFE_FAULT) return BC250_VM_SESSION_FAULT;
    /* Deliberately legal while Busy for a serialized in-call STOP event.
     * Never cancel, release or close the bridge from this hook.
     */
    if (life->State == BC250_DMA_LIFE_RUNNING) life->State = BC250_DMA_LIFE_STOPPING;
    return BC250_VM_SESSION_OK;
}
static __inline BC250_VM_SESSION_STATUS Bc250DmaLifeCallCheck(BC250_DMA_LIFECYCLE *life,
    const BC250_DMA_LIFE_TICKET *ticket, unsigned int kind)
{
    BC250_VM_SESSION_STATUS status = Bc250DmaLifeGuard(life);
    if (status != BC250_VM_SESSION_OK) return status;
    status = Bc250DmaLifeTicket(life, ticket);
    if (status != BC250_VM_SESSION_OK) return status;
    if (life->Records[ticket->Slot].Kind != kind || life->Records[ticket->Slot].Used)
        return BC250_VM_SESSION_IN_USE;
    return BC250_VM_SESSION_OK;
}
static __inline BC250_VM_SESSION_STATUS Bc250DmaLifeFinish(BC250_DMA_LIFECYCLE *life,
    BC250_VM_SESSION_STATUS status)
{
    if (life->Bridge->State == BC250_DMA_BRIDGE_FAULT || status == BC250_VM_SESSION_CORRUPT)
        life->State = BC250_DMA_LIFE_FAULT;
    life->Busy = 0U;
    return life->State == BC250_DMA_LIFE_FAULT ? BC250_VM_SESSION_FAULT : status;
}
static __inline BC250_VM_SESSION_STATUS Bc250DmaLifeStart(BC250_DMA_LIFECYCLE *life,
    const BC250_DMA_LIFE_TICKET *ticket, PMDL mdl, ULONGLONG offset,
    BC250_AD_U64 bytes, unsigned int direction)
{
    BC250_VM_SESSION_STATUS status = Bc250DmaLifeCallCheck(life, ticket, BC250_DMA_LIFE_START);
    if (status != BC250_VM_SESSION_OK) return status;
    if (!mdl || !Bc250DmaLifeAway(life, mdl, sizeof(*mdl))) return BC250_VM_SESSION_INVALID;
    life->Records[ticket->Slot].Used = 1U; life->Busy = 1U;
    status = Bc250DmaBridgeBegin(life->Bridge, bytes, direction);
    if (status == BC250_VM_SESSION_OK) status = Bc250DmaBridgeAcquire(life->Bridge, mdl, offset);
    if (status == BC250_VM_SESSION_OK) status = Bc250DmaBridgePublish(life->Bridge);
    return Bc250DmaLifeFinish(life, status);
}
static __inline BC250_VM_SESSION_STATUS Bc250DmaLifeMap(BC250_DMA_LIFECYCLE *life,
    const BC250_DMA_LIFE_TICKET *ticket, const BC250_AD_SPAN *va, unsigned int access,
    BC250_VM_SESSION_MAPPING_HANDLE *out, BC250_VM_CPU_JOURNAL *journal,
    BC250_VM_CPU_AFTER_WRITE hook, void *context)
{
    BC250_VM_SESSION_STATUS status = Bc250DmaLifeCallCheck(life, ticket, BC250_DMA_LIFE_MAP);
    if (status != BC250_VM_SESSION_OK) return status;
    if (!va || !out || !journal || !Bc250DmaLifeAway(life, va, sizeof(*va)) ||
        !Bc250DmaLifeAway(life, out, sizeof(*out)) || !Bc250DmaLifeAway(life, journal, sizeof(*journal)) ||
        !Bc250DmaPageListDisjoint(ticket, sizeof(*ticket), out, sizeof(*out)) ||
        !Bc250DmaPageListDisjoint(ticket, sizeof(*ticket), journal, sizeof(*journal)))
        return BC250_VM_SESSION_INVALID;
    life->Records[ticket->Slot].Used = 1U; life->Busy = 1U;
    status = Bc250DmaBridgeMap(life->Bridge, va, access, out, journal, hook, context);
    return Bc250DmaLifeFinish(life, status);
}
static __inline BC250_VM_SESSION_STATUS Bc250DmaLifeUnmap(BC250_DMA_LIFECYCLE *life,
    const BC250_DMA_LIFE_TICKET *ticket, const BC250_VM_SESSION_MAPPING_HANDLE *mapping,
    BC250_VM_CPU_JOURNAL *journal, BC250_VM_CPU_AFTER_WRITE hook, void *context)
{
    BC250_VM_SESSION_STATUS status = Bc250DmaLifeCallCheck(life, ticket, BC250_DMA_LIFE_UNMAP);
    if (status != BC250_VM_SESSION_OK) return status;
    if (!mapping || !journal || !Bc250DmaLifeAway(life, mapping, sizeof(*mapping)) ||
        !Bc250DmaLifeAway(life, journal, sizeof(*journal)) ||
        !Bc250DmaPageListDisjoint(ticket, sizeof(*ticket), journal, sizeof(*journal)))
        return BC250_VM_SESSION_INVALID;
    life->Records[ticket->Slot].Used = 1U; life->Busy = 1U;
    status = Bc250DmaBridgeUnmap(life->Bridge, mapping, journal, hook, context);
    return Bc250DmaLifeFinish(life, status);
}
static __inline BC250_VM_SESSION_STATUS Bc250DmaLifeDrain(BC250_DMA_LIFECYCLE *life)
{
    BC250_VM_SESSION_STATUS status = Bc250DmaLifeGuard(life);
    if (status != BC250_VM_SESSION_OK) return status;
    if (life->State == BC250_DMA_LIFE_RUNNING) return BC250_VM_SESSION_IN_USE;
    if (life->Active) return BC250_VM_SESSION_IN_USE;
    life->Busy = 1U;
    status = Bc250DmaBridgeCancel(life->Bridge);
    if (status != BC250_VM_SESSION_OK) return Bc250DmaLifeFinish(life, status);
    life->State = BC250_DMA_LIFE_DRAINING;
    if (life->Bridge->Owner.State == BC250_DMA_OWNER_DRAINING)
        status = Bc250DmaBridgeRelease(life->Bridge);
    if (status != BC250_VM_SESSION_OK) return Bc250DmaLifeFinish(life, status);
    status = Bc250DmaBridgeClose(life->Bridge);
    if (status == BC250_VM_SESSION_OK) life->State = BC250_DMA_LIFE_REMOVED;
    return Bc250DmaLifeFinish(life, status);
}
#endif
