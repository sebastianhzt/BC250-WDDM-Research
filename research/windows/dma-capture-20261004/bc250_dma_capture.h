/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BC250_DMA_CAPTURE_CANDIDATE_H
#define BC250_DMA_CAPTURE_CANDIDATE_H
#include "../dma-gate-20261004/bc250_dma_gate.h"
#include "../dma-windows-20261004/bc250_dma_adapter.h"
#include "../../../inc/address-domains-20261003/bc250_address_domains.h"
#define BC250_DMA_CAPTURE_SIGNATURE 0x43415031U
#define BC250_DMA_CAPTURE_ACTIVE 1L
#define BC250_DMA_CAPTURE_STOPPING 2L
#define BC250_DMA_CAPTURE_DRAINED 3L
#define BC250_DMA_CAPTURE_RETIRED 4L
#define BC250_DMA_CAPTURE_FAULT 5L
#define BC250_DMA_CAPTURE_SLOTS 16U
typedef struct BC250_DMA_CAPTURE BC250_DMA_CAPTURE;
typedef struct BC250_DMA_CAPTURE_SNAPSHOT BC250_DMA_CAPTURE_SNAPSHOT;
typedef struct BC250_DMA_CAPTURE_HANDLE {
    const BC250_DMA_CAPTURE *Owner;
    ULONGLONG Id;
    ULONG Slot;
} BC250_DMA_CAPTURE_HANDLE;
typedef struct BC250_DMA_CAPTURE_RECORD {
    ULONGLONG Id;
    ULONG Live;
    const BC250_DMA_CAPTURE_SNAPSHOT *Snapshot;
} BC250_DMA_CAPTURE_RECORD;

/* NEW standalone VARIANT, not an extension of frozen Native/Leases guarantees.
 * Zero once, resident/aligned stable identity, Init exclusive; no reset/restart.
 * Whole bundle/PDO/locked MDL, immutable trusted provider SG and ALL buffers are
 * externally anchored BEFORE calls through ALL returns PLUS all references.
 * Snapshot buffers are zero once, anchored until released; terminal, no reuse.
 * PASSIVE trusted threads. Gate->Metadata; Drop/SnapshotRelease metadata-only.
 * Retire waits Gate BEFORE Metadata, refuses while any reference is retained.
 * Only Stop in hooks. No concurrent access to SAME caller buffers or raw fields.
 * Snapshot exports COPIED DMA_LOGICAL numbers only for trusted offline tests;
 * NO pointer SG/MDL escapes, NO actual maps/VM/jobs or new native guarantee.
 * Handles/Self are trusted obligations, not OS refs/security/unforgeable tokens.
 * Production policy FALSE before DDIs. Never link/load/install this candidate.
 */
typedef struct BC250_DMA_CAPTURE_REQUEST {
    ULONGLONG Offset;
    ULONG Bytes, Direction;
} BC250_DMA_CAPTURE_REQUEST;
#define BC250_DMA_CAPTURE_SNAPSHOT_LIVE 1U
#define BC250_DMA_CAPTURE_SNAPSHOT_RELEASED 2U
#define BC250_DMA_CAPTURE_MAX_PAGES 64U
struct BC250_DMA_CAPTURE_SNAPSHOT {
    const BC250_DMA_CAPTURE_SNAPSHOT *Self;
    BC250_DMA_CAPTURE_HANDLE Reference;
    BC250_DMA_CAPTURE_REQUEST Request;
    BC250_AD_SPAN Pages[BC250_DMA_CAPTURE_MAX_PAGES];
    ULONGLONG MaximumLast;
    ULONG PageCount, ElementCount, State;
};
struct BC250_DMA_CAPTURE {
    BC250_DMA_GATE Gate;
    BC250_WIN_DMA Native;
    KMUTEX Metadata;
    PVOID volatile MetadataThread;
    BC250_DMA_CAPTURE_RECORD Records[BC250_DMA_CAPTURE_SLOTS];
    ULONGLONG LastId;
    PMDL PinnedMdl;
    PSCATTER_GATHER_LIST PinnedList;
    SIZE_T PinnedListBytes; /* Private CPU buffer bounds only, NOT a DMA address. */
    BC250_DMA_CAPTURE_REQUEST Request; /* Frozen only after successful Acquire. */
    ULONG RequestValid, RequestWidth;
    ULONG References;
    volatile LONG State, Coordinator;
    ULONG Signature;
};
NTSTATUS Bc250DmaCaptureInit(BC250_DMA_CAPTURE *, PDEVICE_OBJECT, const DEVICE_DESCRIPTION *);
NTSTATUS Bc250DmaCaptureAcquire(BC250_DMA_CAPTURE *, PMDL, ULONGLONG, ULONG, BOOLEAN, BC250_DMA_CAPTURE_HANDLE *);
NTSTATUS Bc250DmaCaptureRetain(BC250_DMA_CAPTURE *, const BC250_DMA_CAPTURE_HANDLE *, BC250_DMA_CAPTURE_HANDLE *);
NTSTATUS Bc250DmaCaptureDrop(BC250_DMA_CAPTURE *, const BC250_DMA_CAPTURE_HANDLE *);
NTSTATUS Bc250DmaCaptureStop(BC250_DMA_CAPTURE *);
NTSTATUS Bc250DmaCaptureRetire(BC250_DMA_CAPTURE *);
NTSTATUS Bc250DmaCaptureSnapshot(BC250_DMA_CAPTURE *, const BC250_DMA_CAPTURE_HANDLE *,
    const BC250_DMA_CAPTURE_REQUEST *, BC250_DMA_CAPTURE_SNAPSHOT *);
NTSTATUS Bc250DmaCaptureSnapshotRelease(BC250_DMA_CAPTURE *, BC250_DMA_CAPTURE_SNAPSHOT *);
#endif
