/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BC250_DMA_RESOURCE_CANDIDATE_H
#define BC250_DMA_RESOURCE_CANDIDATE_H
#include "../dma-gate-20261004/bc250_dma_gate.h"
#include "../dma-windows-20261004/bc250_dma_adapter.h"
#define BC250_DMA_RESOURCE_SIGNATURE 0x52455331U
#define BC250_DMA_RESOURCE_ACTIVE 0L
#define BC250_DMA_RESOURCE_RETIRING 1L
#define BC250_DMA_RESOURCE_RETIRED 2L
#define BC250_DMA_RESOURCE_FAULT 3L

/* Aligned resident storage zeroed ONCE; Init exclusive. Externally anchored
 * bundle/PDO/locked MDL remain valid through ALL API returns, including
 * rejected calls and the retiring coordinator. No raw Gate/Native access,
 * pointer republication, retry Init, nested gates, thread exit while held,
 * or Quiesce/Retire from in-progress calls/hooks. ONLY Stop is legal in hooks.
 * Calls are same-thread nonarbitrary PASSIVE, critical-region compatible.
 *
 * This bundle has NO address/handle/span export, map refs, GPU jobs, callbacks
 * or consumers beyond its own synchronous call. NEVER attach a bridge/map
 * or access Native.List from outside. Retiring after rundown is safe ONLY
 * under this restricted no-consumer contract, NOT a WDDM cleanup protocol.
 * Successful Acquire reports allocation at its linearization point, NOT a
 * promise the resource remains HELD after Stop or when caller sees return.
 */
typedef struct BC250_DMA_RESOURCE {
    BC250_DMA_GATE Gate;
    BC250_WIN_DMA Native;
    volatile LONG Retirement;
    ULONG Signature;
} BC250_DMA_RESOURCE;
NTSTATUS Bc250DmaResourceInit(BC250_DMA_RESOURCE *, PDEVICE_OBJECT, const DEVICE_DESCRIPTION *);
NTSTATUS Bc250DmaResourceAcquire(BC250_DMA_RESOURCE *, PMDL, ULONGLONG, ULONG, BOOLEAN);
NTSTATUS Bc250DmaResourceRelease(BC250_DMA_RESOURCE *);
NTSTATUS Bc250DmaResourceStop(BC250_DMA_RESOURCE *);
NTSTATUS Bc250DmaResourceRetire(BC250_DMA_RESOURCE *);
#endif
