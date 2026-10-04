/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BC250_DMA_ADAPTER_CANDIDATE_H
#define BC250_DMA_ADAPTER_CANDIDATE_H
#ifdef BC250_DMA_ADAPTER_MOCK
#include "mock-wdm.h"
#else
#include <wdm.h>
#endif

#define BC250_WIN_DMA_OPEN 1U
#define BC250_WIN_DMA_HELD 2U
#define BC250_WIN_DMA_CLOSED 3U
#define BC250_WIN_DMA_QUARANTINED 4U
#define BC250_WIN_DMA_OPENING 5U
#define BC250_WIN_DMA_MAX_BYTES (64U * 4096U)

/* Zero once, nonpaged, stable identity. Caller EXCLUSIVELY serializes calls.
 * Busy detects synchronous reentry, NOT concurrent threads or PnP rundown.
 * Trusted PDO/description/locked single MDL remain alive until release/close.
 * Fields are internal storage: do not inspect/edit List or reuse its context
 * as a capability. No address API and no transfer initiated in this candidate.
 * At most ONE allocation request per owner; create a new owner for retry.
 */
typedef struct BC250_WIN_DMA {
    ULONG State, Busy, Cancelled, RequestIssued, MaximumLength, AddressWidth;
    PDMA_ADAPTER Adapter;
    PDEVICE_OBJECT Pdo;
    PPUT_DMA_ADAPTER Put;
    PINITIALIZE_DMA_TRANSFER_CONTEXT Initialize;
    PGET_SCATTER_GATHER_LIST_EX Get;
    PFREE_ADAPTER_OBJECT Free;
    PSCATTER_GATHER_LIST List;
    PMDL HeldMdl;
    ULONGLONG TransferContext[(DMA_TRANSFER_CONTEXT_SIZE_V1 + 7U) / 8U];
} BC250_WIN_DMA;

NTSTATUS Bc250WinDmaOpen(BC250_WIN_DMA *, PDEVICE_OBJECT, const DEVICE_DESCRIPTION *);
NTSTATUS Bc250WinDmaAcquire(BC250_WIN_DMA *, PMDL, ULONGLONG, ULONG, BOOLEAN);
NTSTATUS Bc250WinDmaRelease(BC250_WIN_DMA *);
NTSTATUS Bc250WinDmaCancel(BC250_WIN_DMA *);
NTSTATUS Bc250WinDmaClose(BC250_WIN_DMA *);
#endif
