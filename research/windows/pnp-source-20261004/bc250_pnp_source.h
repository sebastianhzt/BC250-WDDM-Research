/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BC250_PNP_SOURCE_H
#define BC250_PNP_SOURCE_H
#if defined(BC250_PNP_SOURCE_MOCK) || defined(BC250_PNP_SOURCE_TYPES_MOCK)
#include "mock-source-wdm.h"
#else
#include <wdm.h>
#endif
#include "../pci-pnp-capture-20261004/bc250_pnp_capture.h"
#define BC250_SOURCE_SIGNATURE 0x53524331U
#define BC250_SOURCE_SLOTS 16U
#define BC250_SOURCE_START 1U
#define BC250_SOURCE_QUERY_STOP 2U
#define BC250_SOURCE_QUERY_REMOVE 3U
#define BC250_SOURCE_CANCEL_STOP 4U
#define BC250_SOURCE_CANCEL_REMOVE 5U
#define BC250_SOURCE_POWER 6U
#define BC250_SOURCE_STOP 7U
#define BC250_SOURCE_SURPRISE 8U
#define BC250_SOURCE_REMOVE 9U
typedef struct BC250_SOURCE BC250_SOURCE;
typedef struct BC250_SOURCE_MEMORY {
    ULONGLONG Raw,Translated,Length;
    ULONG RawFlags,TranslatedFlags,Ordinal,Reserved;
} BC250_SOURCE_MEMORY;
typedef struct BC250_SOURCE_RESOURCES {
    ULONG DescriptorCount,MemoryCount;
    BC250_SOURCE_MEMORY Memory[8];
} BC250_SOURCE_RESOURCES;
typedef struct BC250_SOURCE_REQUEST {
    BC250_SOURCE *Source;
    ULONGLONG ScopeId,Id;
    ULONG Generation,Epoch,Kind,TargetPower;
} BC250_SOURCE_REQUEST;
typedef struct BC250_SOURCE_LEASE {
    BC250_SOURCE *Source;
    PVOID Thread;
    ULONGLONG ScopeId,Id;
    BC250_CAPTURE_INPUT Input; /* historical metadata, not hardware authority */
} BC250_SOURCE_LEASE;
typedef struct BC250_SOURCE_SLOT {
    BC250_SOURCE_LEASE *Address;
    PVOID Thread;
    ULONGLONG Id;
    ULONG Generation,Epoch; /* acquisition seal; never taken from lease payload */
} BC250_SOURCE_SLOT;
struct BC250_SOURCE {
    KSPIN_LOCK Lock;
    EX_RUNDOWN_REF Rundown;
    PDEVICE_OBJECT Pdo,Lower;
    ULONGLONG ScopeId,LastId;
    ULONG Signature,Closing,Fault,Retirement,PreviousState,QueryKind;
    BC250_CAPTURE_INPUT Current;
    BC250_SOURCE_REQUEST *PendingAddress;
    BC250_SOURCE_REQUEST Pending;
    BC250_SOURCE_SLOT Slots[BC250_SOURCE_SLOTS];
};
/* Native FALSE. No OS dispatch/IRPs or installed driver integration.
 * Root/inputs/code resident, trusted, immutable during calls, nonaliasing;
 * all Root API attempts externally anchored BEFORE entry through ALL returns.
 * Init exclusive/unpublished: borrowed PDO+Lower already lifetime-anchored.
 * Owns exactly two object refs (also when equal), NOT an FDO remove lock or
 * physical PCI availability proof. Init cannot fail after those VOID refs.
 * Request stable exact address until FINAL Complete; can complete on another
 * thread, under its external IRP/context anchor. One pending request total;
 * overlap faults admission without losing the original cleanup obligation.
 * All tuple writers use Root.Lock. Begin/Complete/Acquire/Validate/Release
 * PASSIVE only; future DISPATCH completions require reviewed deferral, absent.
 * Only Transition/Close admit <=DISPATCH; they never wait or free references.
 * Lease exact-address/same-thread; no thread exit with a lease.
 * One API at a time per request/lease storage for its ENTIRE lifetime, including
 * failed calls, across all Roots. No shared output buffers or zeroing live tokens.
 * Capture is historical. Source validation must precede each future backend stage;
 * it neither reserves state nor protects hardware after return.
 * Close does not erase pending requests or leases. Drain external PASSIVE
 * coordinator with no own lease/inflight call, unpublish Root first; BUSY
 * while registered pending/leases exist. Wait outside lock covers in-flight
 * rundown acquisitions. External anchor still covers failed API returns.
 * Fault retains object refs permanently (quarantine), but known cleanup is
 * allowed. No reset/reopen, caller field edits or forced cleanup of unknown
 * obligations. Snapshot resources must come from a future validated START
 * parser while OS lists valid, not independently queried Build22 metadata.
 */
NTSTATUS Bc250SourceInit(BC250_SOURCE *,ULONGLONG,ULONG,PDEVICE_OBJECT,PDEVICE_OBJECT);
NTSTATUS Bc250SourceBegin(BC250_SOURCE *,ULONG,ULONG,BC250_SOURCE_REQUEST *);
NTSTATUS Bc250SourceComplete(BC250_SOURCE *,BC250_SOURCE_REQUEST *,NTSTATUS,const BC250_SOURCE_RESOURCES *);
NTSTATUS Bc250SourceTransition(BC250_SOURCE *,ULONG);
NTSTATUS Bc250SourceInterlocks(BC250_SOURCE *,ULONG);
NTSTATUS Bc250SourceAcquire(BC250_SOURCE *,BC250_SOURCE_LEASE *);
NTSTATUS Bc250SourceValidate(BC250_SOURCE *,const BC250_SOURCE_LEASE *,BC250_CAPTURE_INPUT *);
NTSTATUS Bc250SourceRelease(BC250_SOURCE *,BC250_SOURCE_LEASE *);
NTSTATUS Bc250SourceClose(BC250_SOURCE *);
NTSTATUS Bc250SourceDrain(BC250_SOURCE *);
#endif
