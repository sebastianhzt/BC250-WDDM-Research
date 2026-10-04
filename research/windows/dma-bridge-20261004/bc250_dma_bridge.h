/* SPDX-License-Identifier: Apache-2.0
 * Original RAM-only connection of the existing candidate and CPU owner.
 * The only permitted transport is the fake platform. NEVER compile in kernel.
 * Caller exclusively serializes calls, keeps all objects/buffers stable and
 * disjoint, and never bypasses this facade or exposes resources to hardware.
 */
#ifndef BC250_DMA_BRIDGE_H
#define BC250_DMA_BRIDGE_H
#ifndef BC250_DMA_ADAPTER_MOCK
#error DMA bridge requires the RAM fake platform
#endif
#ifdef _KERNEL_MODE
#error CPU DMA bridge is not kernel integrated
#endif
#include "../dma-windows-20261004/bc250_dma_adapter.h"
#include "dma-owner-20261003/bc250_dma_owner.h"

#define BC250_DMA_BRIDGE_ACTIVE 1U
#define BC250_DMA_BRIDGE_FAULT 2U
#define BC250_DMA_BRIDGE_DEAD 3U
typedef struct BC250_DMA_BRIDGE {
    BC250_DMA_OWNER Owner;
    BC250_WIN_DMA Transport;
    BC250_DMA_OWNER_TICKET Ticket;
    BC250_AD_SPAN Runs[64];
    unsigned int State, Busy, Begun, Returned;
    NTSTATUS NativeStatus;
} BC250_DMA_BRIDGE;

static __inline BC250_VM_SESSION_STATUS Bc250DmaBridgeGuard(BC250_DMA_BRIDGE *bridge)
{
    if (!Bc250MockExecutionAllowed) return BC250_VM_SESSION_INVALID;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) return BC250_VM_SESSION_INVALID;
    if (!bridge) return BC250_VM_SESSION_INVALID;
    if (bridge->Busy) return BC250_VM_SESSION_BUSY_RESULT;
    if (bridge->State == BC250_DMA_BRIDGE_DEAD) return BC250_VM_SESSION_DEAD_RESULT;
    if (bridge->State == BC250_DMA_BRIDGE_FAULT) return BC250_VM_SESSION_FAULT;
    return BC250_VM_SESSION_OK;
}
static __inline int Bc250DmaBridgeAway(BC250_DMA_BRIDGE *bridge,
    const void *buffer, BC250_AD_U64 bytes)
{
    return Bc250DmaPageListDisjoint(bridge, sizeof(*bridge), buffer, bytes) &&
        Bc250VmSessionBufferAway(bridge->Owner.Session, buffer, bytes);
}
static __inline BC250_VM_SESSION_STATUS Bc250DmaBridgeCheck(BC250_DMA_BRIDGE *bridge)
{
    BC250_VM_SESSION_STATUS status = Bc250DmaBridgeGuard(bridge);
    if (status != BC250_VM_SESSION_OK) return status;
    if (bridge->State != BC250_DMA_BRIDGE_ACTIVE ||
        !Bc250VmSessionBufferAway(bridge->Owner.Session, bridge, sizeof(*bridge)))
        return BC250_VM_SESSION_INVALID;
    status = Bc250DmaOwnerCheck(&bridge->Owner);
    if (status != BC250_VM_SESSION_OK) return status;
    if (bridge->Begun > 1U || bridge->Returned > 1U ||
        !bridge->Transport.AddressWidth || bridge->Transport.AddressWidth > 48U)
        return BC250_VM_SESSION_CORRUPT;
    if (bridge->Owner.State == BC250_DMA_OWNER_IDLE) {
        if (bridge->Transport.State != BC250_WIN_DMA_OPEN || bridge->Transport.List ||
            bridge->Transport.HeldMdl) return BC250_VM_SESSION_CORRUPT;
    } else if (bridge->Owner.State == BC250_DMA_OWNER_PENDING) {
        if (bridge->Transport.State != BC250_WIN_DMA_OPEN &&
            bridge->Transport.State != BC250_WIN_DMA_HELD) return BC250_VM_SESSION_CORRUPT;
    } else if (bridge->Owner.State == BC250_DMA_OWNER_READY ||
        bridge->Owner.State == BC250_DMA_OWNER_DRAINING) {
        if (bridge->Transport.State != BC250_WIN_DMA_HELD || bridge->Returned ||
            bridge->Owner.Cookie != &bridge->Transport) return BC250_VM_SESSION_CORRUPT;
    } else return BC250_VM_SESSION_CORRUPT;
    if (bridge->Transport.State == BC250_WIN_DMA_HELD &&
        (!bridge->Transport.List || !bridge->Transport.HeldMdl)) return BC250_VM_SESSION_CORRUPT;
    return BC250_VM_SESSION_OK;
}
/* Adapt the CPU owner's infallible Put contract. Only its matching consumed
 * cookie may reach the transport. An unexpected native cleanup failure cannot
 * restore a numeric backing already unregistered; preserve the transport and
 * report FAULT instead of claiming release or forgetting the held resource.
 */
static __inline void Bc250DmaBridgePut(void *context, void *cookie, unsigned int direction)
{
    BC250_DMA_BRIDGE *bridge = context;
    if (!bridge->Busy || !bridge->Owner.Busy || cookie != &bridge->Transport ||
        direction != bridge->Owner.WriteToDevice || bridge->Returned ||
        bridge->Transport.State != BC250_WIN_DMA_HELD) {
        bridge->State = BC250_DMA_BRIDGE_FAULT; return;
    }
    bridge->NativeStatus = Bc250WinDmaRelease(&bridge->Transport);
    if (bridge->NativeStatus != STATUS_SUCCESS) bridge->State = BC250_DMA_BRIDGE_FAULT;
    else bridge->Returned = 1U;
}

static __inline BC250_VM_SESSION_STATUS Bc250DmaBridgeInit(BC250_DMA_BRIDGE *bridge,
    BC250_VM_CPU_SESSION *session, PDEVICE_OBJECT pdo, const DEVICE_DESCRIPTION *description)
{
    BC250_DMA_BRIDGE zero;
    BC250_VM_SESSION_STATUS status = Bc250DmaBridgeGuard(bridge);
    if (status != BC250_VM_SESSION_OK) return status;
    memset(&zero, 0, sizeof(zero));
    if (memcmp(bridge, &zero, sizeof(zero))) return BC250_VM_SESSION_INVALID;
    status = Bc250VmSessionCheck(session);
    if (status != BC250_VM_SESSION_OK) return status;
    if (!Bc250VmSessionBufferAway(session, bridge, sizeof(*bridge)) || !description ||
        !Bc250DmaPageListDisjoint(bridge, sizeof(*bridge), description, sizeof(*description)) ||
        !Bc250VmSessionBufferAway(session, description, sizeof(*description))) return BC250_VM_SESSION_INVALID;
    bridge->Busy = 1U;
    status = Bc250DmaOwnerInit(&bridge->Owner, session, Bc250DmaBridgePut, bridge);
    if (status == BC250_VM_SESSION_OK) {
        bridge->NativeStatus = Bc250WinDmaOpen(&bridge->Transport, pdo, description);
        bridge->State = bridge->NativeStatus == STATUS_SUCCESS ?
            BC250_DMA_BRIDGE_ACTIVE : BC250_DMA_BRIDGE_FAULT;
        if (bridge->State == BC250_DMA_BRIDGE_FAULT) status = BC250_VM_SESSION_FAULT;
    }
    bridge->Busy = 0U; return status;
}
static __inline BC250_VM_SESSION_STATUS Bc250DmaBridgeBegin(BC250_DMA_BRIDGE *bridge,
    BC250_AD_U64 bytes, unsigned int direction)
{
    BC250_VM_SESSION_STATUS status = Bc250DmaBridgeCheck(bridge);
    if (status != BC250_VM_SESSION_OK) return status;
    if (bridge->Begun) return BC250_VM_SESSION_IN_USE;
    if (bytes > bridge->Transport.MaximumLength) return BC250_VM_SESSION_INVALID;
    bridge->Busy = 1U;
    status = Bc250DmaOwnerBegin(&bridge->Owner, bytes, direction, &bridge->Ticket);
    if (status == BC250_VM_SESSION_OK) bridge->Begun = 1U;
    bridge->Busy = 0U; return status;
}
static __inline BC250_VM_SESSION_STATUS Bc250DmaBridgeAcquire(BC250_DMA_BRIDGE *bridge,
    PMDL mdl, ULONGLONG offset)
{
    BC250_VM_SESSION_STATUS status = Bc250DmaBridgeCheck(bridge);
    if (status != BC250_VM_SESSION_OK) return status;
    if (bridge->Owner.State != BC250_DMA_OWNER_PENDING ||
        bridge->Transport.State != BC250_WIN_DMA_OPEN) return BC250_VM_SESSION_IN_USE;
    if (!mdl || !Bc250DmaBridgeAway(bridge, mdl, sizeof(*mdl))) return BC250_VM_SESSION_INVALID;
    bridge->Busy = 1U;
    bridge->NativeStatus = Bc250WinDmaAcquire(&bridge->Transport, mdl, offset,
        (ULONG)bridge->Owner.ExpectedBytes, (BOOLEAN)bridge->Owner.WriteToDevice);
    if (bridge->NativeStatus == STATUS_SUCCESS) status = BC250_VM_SESSION_OK;
    else if (bridge->Transport.State == BC250_WIN_DMA_OPEN && !bridge->Transport.List) {
        /* Exclusive synchronous transport has returned, without a resource.
         * Never apply this resolution to quarantine/pending/unknown state.
         */
        status = Bc250DmaOwnerResolveNoResource(&bridge->Owner, &bridge->Ticket);
        if (status == BC250_VM_SESSION_OK) status = BC250_VM_SESSION_FAULT;
    } else {
        bridge->State = BC250_DMA_BRIDGE_FAULT; status = BC250_VM_SESSION_FAULT;
    }
    bridge->Busy = 0U; return status;
}
static __inline BC250_VM_SESSION_STATUS Bc250DmaBridgePublish(BC250_DMA_BRIDGE *bridge)
{
    BC250_DMA_OWNER_DELIVERY_RESULT result;
    PSCATTER_GATHER_LIST list;
    unsigned int i, count;
    BC250_AD_U64 limit;
    BC250_VM_SESSION_STATUS status = Bc250DmaBridgeCheck(bridge);
    if (status != BC250_VM_SESSION_OK) return status;
    if (bridge->Owner.State != BC250_DMA_OWNER_PENDING ||
        bridge->Transport.State != BC250_WIN_DMA_HELD) return BC250_VM_SESSION_IN_USE;
    bridge->Busy = 1U;
    /* Privileged access to FAKE transport internals for this RAM bridge ONLY.
     * List is trusted/stable; numeric copy never exports a Windows capability.
     */
    list = bridge->Transport.List;
    /* Fake list has a fixed 64-element capacity, unlike the real WDK ABI. */
    count = list && Bc250DmaBridgeAway(bridge, list, sizeof(*list)) &&
        list->NumberOfElements <= 64U ? list->NumberOfElements : 0U;
    limit = (1ULL << bridge->Transport.AddressWidth) - 1ULL;
    memset(bridge->Runs, 0, sizeof(bridge->Runs));
    for (i = 0; i < count; ++i) {
        BC250_AD_SPAN *run = &bridge->Runs[i];
        run->Domain = BC250_AD_DMA_LOGICAL;
        run->Start = (BC250_AD_U64)list->Elements[i].Address.QuadPart;
        run->Bytes = list->Elements[i].Length;
        if (!run->Bytes || run->Start > limit ||
            run->Bytes - 1ULL > limit - run->Start) { count = 0U; break; }
        run->Last = run->Start + run->Bytes - 1ULL;
    }
    result = Bc250DmaOwnerDeliver(&bridge->Owner, &bridge->Ticket, &bridge->Transport,
        count ? bridge->Runs : NULL, count, 64U);
    if (!result.Consumed) bridge->State = BC250_DMA_BRIDGE_FAULT;
    status = bridge->State == BC250_DMA_BRIDGE_FAULT ? BC250_VM_SESSION_FAULT : result.Status;
    bridge->Busy = 0U; return status;
}
static __inline BC250_VM_SESSION_STATUS Bc250DmaBridgeCancel(BC250_DMA_BRIDGE *bridge)
{
    BC250_DMA_OWNER_DELIVERY_RESULT result;
    BC250_VM_SESSION_STATUS status = Bc250DmaBridgeCheck(bridge);
    if (status != BC250_VM_SESSION_OK) return status;
    bridge->Busy = 1U;
    if (bridge->Owner.State == BC250_DMA_OWNER_IDLE) {
        bridge->Begun = 1U;
        bridge->NativeStatus = Bc250WinDmaCancel(&bridge->Transport);
        status = bridge->NativeStatus == STATUS_SUCCESS ? BC250_VM_SESSION_OK : BC250_VM_SESSION_FAULT;
    } else {
        status = Bc250DmaOwnerCancel(&bridge->Owner, &bridge->Ticket);
        if (status == BC250_VM_SESSION_OK && bridge->Owner.State == BC250_DMA_OWNER_CANCEL_WAIT) {
            if (bridge->Transport.State == BC250_WIN_DMA_HELD) {
                result = Bc250DmaOwnerDeliver(&bridge->Owner, &bridge->Ticket,
                    &bridge->Transport, NULL, 0U, 0U);
                status = result.Consumed ? result.Status : BC250_VM_SESSION_FAULT;
            } else if (bridge->Transport.State == BC250_WIN_DMA_OPEN) {
                bridge->NativeStatus = Bc250WinDmaCancel(&bridge->Transport);
                status = bridge->NativeStatus == STATUS_SUCCESS ?
                    Bc250DmaOwnerResolveNoResource(&bridge->Owner, &bridge->Ticket) : BC250_VM_SESSION_FAULT;
            } else status = BC250_VM_SESSION_FAULT;
        }
        /* READY becomes DRAINING only. Never call native Cancel on a cookie
         * owned by the CPU owner: it would free before mapped leases retire.
         */
    }
    if (status == BC250_VM_SESSION_FAULT) bridge->State = BC250_DMA_BRIDGE_FAULT;
    if (bridge->State == BC250_DMA_BRIDGE_FAULT) status = BC250_VM_SESSION_FAULT;
    bridge->Busy = 0U; return status;
}
static __inline BC250_VM_SESSION_STATUS Bc250DmaBridgeMap(BC250_DMA_BRIDGE *bridge,
    const BC250_AD_SPAN *va, unsigned int access, BC250_VM_SESSION_MAPPING_HANDLE *out,
    BC250_VM_CPU_JOURNAL *journal, BC250_VM_CPU_AFTER_WRITE hook, void *context)
{
    BC250_VM_SESSION_STATUS status = Bc250DmaBridgeCheck(bridge);
    if (status != BC250_VM_SESSION_OK) return status;
    if (!va || !out || !journal || !Bc250DmaBridgeAway(bridge, va, sizeof(*va)) ||
        !Bc250DmaBridgeAway(bridge, out, sizeof(*out)) ||
        !Bc250DmaBridgeAway(bridge, journal, sizeof(*journal))) return BC250_VM_SESSION_INVALID;
    bridge->Busy = 1U;
    status = Bc250DmaOwnerMap(&bridge->Owner, &bridge->Ticket, va, access, out, journal, hook, context);
    bridge->Busy = 0U; return status;
}
static __inline BC250_VM_SESSION_STATUS Bc250DmaBridgeUnmap(BC250_DMA_BRIDGE *bridge,
    const BC250_VM_SESSION_MAPPING_HANDLE *mapping, BC250_VM_CPU_JOURNAL *journal,
    BC250_VM_CPU_AFTER_WRITE hook, void *context)
{
    BC250_VM_SESSION_STATUS status = Bc250DmaBridgeCheck(bridge);
    if (status != BC250_VM_SESSION_OK) return status;
    if (!mapping || !journal || !Bc250DmaBridgeAway(bridge, mapping, sizeof(*mapping)) ||
        !Bc250DmaBridgeAway(bridge, journal, sizeof(*journal))) return BC250_VM_SESSION_INVALID;
    bridge->Busy = 1U;
    status = Bc250DmaOwnerUnmap(&bridge->Owner, &bridge->Ticket, mapping, journal, hook, context);
    bridge->Busy = 0U; return status;
}
static __inline BC250_VM_SESSION_STATUS Bc250DmaBridgeRelease(BC250_DMA_BRIDGE *bridge)
{
    BC250_VM_SESSION_STATUS status = Bc250DmaBridgeCheck(bridge);
    if (status != BC250_VM_SESSION_OK) return status;
    bridge->Busy = 1U;
    status = Bc250DmaOwnerRelease(&bridge->Owner, &bridge->Ticket);
    if (bridge->State == BC250_DMA_BRIDGE_FAULT) status = BC250_VM_SESSION_FAULT;
    bridge->Busy = 0U; return status;
}
static __inline BC250_VM_SESSION_STATUS Bc250DmaBridgeClose(BC250_DMA_BRIDGE *bridge)
{
    BC250_VM_SESSION_STATUS status = Bc250DmaBridgeGuard(bridge);
    if (status == BC250_VM_SESSION_FAULT && !bridge->Begun && !bridge->Transport.Adapter &&
        bridge->Transport.State == 0U) status = BC250_VM_SESSION_OK; /* failed Open, no handle */
    if (status != BC250_VM_SESSION_OK) return status;
    if (bridge->Owner.State != BC250_DMA_OWNER_IDLE ||
        (bridge->Transport.State != BC250_WIN_DMA_OPEN && bridge->Transport.State != 0U))
        return BC250_VM_SESSION_IN_USE;
    status = Bc250DmaOwnerCheck(&bridge->Owner);
    if (status != BC250_VM_SESSION_OK) return status;
    bridge->Busy = 1U;
    if (bridge->Transport.State == BC250_WIN_DMA_OPEN) {
        bridge->NativeStatus = Bc250WinDmaClose(&bridge->Transport);
        if (bridge->NativeStatus != STATUS_SUCCESS) {
            bridge->State = BC250_DMA_BRIDGE_FAULT; bridge->Busy = 0U; return BC250_VM_SESSION_FAULT;
        }
    }
    status = Bc250DmaOwnerShutdown(&bridge->Owner);
    bridge->State = status == BC250_VM_SESSION_OK ? BC250_DMA_BRIDGE_DEAD : BC250_DMA_BRIDGE_FAULT;
    bridge->Busy = 0U; return status;
}
#endif
