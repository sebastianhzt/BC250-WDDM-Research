/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BC250_DMA_LEASES_CANDIDATE_H
#define BC250_DMA_LEASES_CANDIDATE_H
#include "../dma-gate-20261004/bc250_dma_gate.h"
#include "../dma-windows-20261004/bc250_dma_adapter.h"
#define BC250_DMA_LEASES_SIGNATURE 0x4C534531U
#define BC250_DMA_LEASES_ACTIVE 1L
#define BC250_DMA_LEASES_STOPPING 2L
#define BC250_DMA_LEASES_DRAINED 3L
#define BC250_DMA_LEASES_RETIRED 4L
#define BC250_DMA_LEASES_FAULT 5L
#define BC250_DMA_LEASES_SLOTS 16U
typedef struct BC250_DMA_LEASES BC250_DMA_LEASES;
typedef struct BC250_DMA_LEASE_HANDLE {
    const BC250_DMA_LEASES *Owner;
    ULONGLONG Id;
    ULONG Slot;
} BC250_DMA_LEASE_HANDLE;
typedef struct BC250_DMA_LEASE_RECORD {
    ULONGLONG Id;
    ULONG Live;
} BC250_DMA_LEASE_RECORD;

/* Original standalone VARIANT, not an extension of the no-consumer Resource.
 * Zero ONCE in aligned resident memory, Init exclusive; no reset/restart.
 * Externally anchored bundle/PDO/locked MDL and ALL buffers remain valid before
 * every API entry through ALL returns PLUS every live lease. Handles represent
 * trusted metadata obligations, NOT OS references, GPU handles or security.
 * No Native/Gate/Metadata raw access or pointer publication/deletion protocol.
 * No address export, actual map/VM consumer or GPU work; attaching those is
 * forbidden. Every successful lease is dropped once, possibly after STOP.
 * Same nonarbitrary PASSIVE threads; no nested gates/inverted locks/thread exit
 * with locks held. ONLY Stop may be called from in-progress calls/hooks.
 * Retire coordinator holds no own references/in-flight attempts. Metadata mutex
 * uses try-only; retry BUSY without sleeping while another protected call runs.
 * Gate->Metadata acquisition; Drop only Metadata; Retire waits Gate BEFORE
 * Metadata. DRAINED may retry Retire, never reopen Gate. Drop never frees DMA.
 */
struct BC250_DMA_LEASES {
    BC250_DMA_GATE Gate;
    BC250_WIN_DMA Native;
    KMUTEX Metadata;
    PVOID volatile MetadataThread;
    BC250_DMA_LEASE_RECORD Records[BC250_DMA_LEASES_SLOTS];
    ULONGLONG LastId;
    PMDL PinnedMdl;
    PSCATTER_GATHER_LIST PinnedList;
    SIZE_T PinnedListBytes; /* Private CPU buffer bounds only, NOT a DMA address. */
    ULONG References;
    volatile LONG State, Coordinator;
    ULONG Signature;
};
NTSTATUS Bc250DmaLeasesInit(BC250_DMA_LEASES *, PDEVICE_OBJECT, const DEVICE_DESCRIPTION *);
NTSTATUS Bc250DmaLeasesAcquire(BC250_DMA_LEASES *, PMDL, ULONGLONG, ULONG, BOOLEAN, BC250_DMA_LEASE_HANDLE *);
NTSTATUS Bc250DmaLeasesRetain(BC250_DMA_LEASES *, const BC250_DMA_LEASE_HANDLE *, BC250_DMA_LEASE_HANDLE *);
NTSTATUS Bc250DmaLeasesDrop(BC250_DMA_LEASES *, const BC250_DMA_LEASE_HANDLE *);
NTSTATUS Bc250DmaLeasesStop(BC250_DMA_LEASES *);
NTSTATUS Bc250DmaLeasesRetire(BC250_DMA_LEASES *);
#endif
