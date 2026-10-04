/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BC250_PNP_CAPTURE_H
#define BC250_PNP_CAPTURE_H
#if defined(BC250_PNP_CAPTURE_MOCK) || defined(BC250_PNP_CAPTURE_TYPES_MOCK)
#include "../pci-pnp-publisher-20261004/mock-publisher-wdm.h"
#else
#include <wdm.h>
#endif
#include "../pci-pnp-contract-20261004/bc250_pci_contract.h"
#include "capture-wire-layout.h"
#define BC250_CAPTURE_SIGNATURE 0x43415031U
#define BC250_CAPTURE_SCHEMA 1U
#define BC250_CAPTURE_D0 1U
#define BC250_CAPTURE_COUNTER_LIMIT 0x7fffffffU
typedef struct BC250_CAPTURE_INPUT {
    ULONG Version,StructSize,SourceGeneration,SourceEpoch,PowerState,InterlocksOff;
    UCHAR Wire[BC250_CAPTURE_WIRE_BYTES];
} BC250_CAPTURE_INPUT;
typedef char BC250CaptureInputSize[(sizeof(BC250_CAPTURE_INPUT)==384)?1:-1];
typedef struct BC250_CAPTURE_CACHE BC250_CAPTURE_CACHE;
typedef struct BC250_CAPTURE_FRAME {
    BC250_CAPTURE_CACHE *Origin; /* INTERNAL trusted token, never user ABI. */
    ULONGLONG ScopeId,Revision;
    BC250_CAPTURE_INPUT Input;
    BC250_PCI_RESOURCES Resources; /* IDs = model Revision, NOT raw PnP IDs. */
} BC250_CAPTURE_FRAME;
struct BC250_CAPTURE_CACHE {
    KSPIN_LOCK Lock;
    ULONGLONG ScopeId,Revision;
    ULONG Signature,Present,Fault;
    BC250_CAPTURE_FRAME Current;
};
/* Native FALSE, no driver integration or activation switch.
 * Root externally anchored before all APIs through all returns, never reset.
 * ScopeId external unique nonwrapping lifetime ID, never reused across Roots.
 * Every source state/epoch/power/resource/interlock writer MUST participate
 * in ONE source serialization domain and lifetime contract. Input is a trusted,
 * stable immutable producer copy made in that domain while PDO/backing storage
 * is anchored. Commit's cache lock does NOT make split source reads coherent.
 * Producer is exclusive per source; no field changes/aliasing during Commit.
 * This provider/anchor does NOT exist in Build22: wire alone has no epoch or
 * power, and state writers are not all under its resource snapshot lock.
 * Cache/inputs/outputs/code resident/aligned/kernel-owned/nonaliasing.
 * No user pointers, IOCTL, PDO dereference, callbacks, waits, PCI or MMIO.
 * State writers model source tags as nonnegative 31-bit bounded counters;
 * exhaustion/reuse rejected, not extending signed-wrap counters to 64-bit.
 * Epoch zero is accepted only for non-Started metadata. Epoch strictly
 * increases for the same source binding; new SourceGeneration
 * can start a fresh epoch. CacheRevision increments every committed transition.
 * Resources Generation/Epoch are BOTH synthetic Revision scoped to this Root,
 * separate from SourceGeneration/SourceEpoch, to fit one-shot publisher renewal.
 * Read/Validate/ResourcesToPublish are historical checks, not state reservations:
 * transition can occur after return. A future backend must revalidate the
 * source token/lifetime at every stage and close/drain old Publisher bindings.
 * No actual PnP/power routing/completion, PDO refs, allocator, SMP or ownership.
 * PowerState uses DEVICE_POWER_STATE numeric values (0 unknown, 1 D0,
 * 2 D1, 3 D2, 4 D3), NOT a hardware observation from this model.
 * Any admitted Commit failure permanently faults the Cache and clears its
 * previous frame. No recovery/reset; external owner must close old bindings.
 * Legal IRP transition ordering is the future producer's responsibility.
 */
NTSTATUS Bc250CaptureInit(BC250_CAPTURE_CACHE *,ULONGLONG);
NTSTATUS Bc250CaptureCommit(BC250_CAPTURE_CACHE *,const BC250_CAPTURE_INPUT *);
NTSTATUS Bc250CaptureRead(BC250_CAPTURE_CACHE *,BC250_CAPTURE_FRAME *);
NTSTATUS Bc250CaptureValidate(BC250_CAPTURE_CACHE *,const BC250_CAPTURE_FRAME *);
NTSTATUS Bc250CaptureResources(BC250_CAPTURE_CACHE *,const BC250_CAPTURE_FRAME *,BC250_PCI_RESOURCES *);
#endif
