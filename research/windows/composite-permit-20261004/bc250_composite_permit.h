/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BC250_COMPOSITE_PERMIT_H
#define BC250_COMPOSITE_PERMIT_H
#if defined(BC250_PERMIT_MOCK) || defined(BC250_PERMIT_TYPES_MOCK)
#include "../pci-pnp-publisher-20261004/mock-publisher-wdm.h"
#else
#include <wdm.h>
#endif
#define BC250_PERMIT_SIGNATURE 0x504d5431U
#define BC250_PERMIT_SLOTS 8U
#define BC250_PERMIT_LIMIT 0x7fffffffU
typedef struct BC250_PERMIT BC250_PERMIT;
typedef struct BC250_PERMIT_READER {
    BC250_PERMIT *Root;
    PVOID Thread;
    ULONGLONG ScopeId,Id,Revision;
    ULONG Generation,Epoch;
} BC250_PERMIT_READER;
typedef struct BC250_PERMIT_WRITER {
    BC250_PERMIT *Root;
    ULONGLONG ScopeId,Id;
    ULONG Generation,Epoch,Kind;
} BC250_PERMIT_WRITER;
typedef struct BC250_PERMIT_SLOT {
    BC250_PERMIT_READER *Address;
    BC250_PERMIT_READER Seal;
    ULONG Step;
} BC250_PERMIT_SLOT;
struct BC250_PERMIT {
    KSPIN_LOCK Lock;
    ULONG Signature,Generation,Epoch,Closing,Fault,Published,Pins,Steps,WriterPhase;
    ULONGLONG ScopeId,LastId,Revision;
    BC250_PERMIT_WRITER *WriterAddress;
    BC250_PERMIT_WRITER WriterSeal;
    BC250_PERMIT_SLOT Readers[BC250_PERMIT_SLOTS];
};
typedef struct BC250_PERMIT_SNAPSHOT {
    ULONGLONG Revision,WriterId;
    ULONG Generation,Epoch,Closing,Fault,Published,Pins,Steps,WriterPhase;
} BC250_PERMIT_SNAPSHOT;
/* Native FALSE. Central PARTICIPATING metadata gate, NOT hardware authority.
 * No bound Source/intake/bridge or real tuple provider/writer integration yet.
 * Revision is a synthetic publication label, NOT a physical/GPU/MC address.
 * ALL metadata writers must Admit BEFORE invalidation/queue/forward, then Claim
 * only after every reader pin drains. Admit <=DISPATCH revokes new steps, never
 * waits or clears pins/active steps. Claim PASSIVE returns BUSY while pins held.
 * Already-entered synchronous bounded metadata step may finish against retained
 * revision; no participant may mutate the represented data until successful
 * writer Claim. Physical surprise removal can still happen and is NOT prevented.
 * Caller owns queue/reschedule/cancel and all OS IRP/DMA/RemoveLock obligations.
 * Writer Finish only after terminal known writer/bridge/Source cleanup; PENDING
 * never retires. Exact SUCCESS with strictly newer valid revision publishes;
 * error leaves unpublished, warning/info/invalid revision fault/quarantine.
 * Waiting writer Abandon cleans metadata only, leaves unpublished; not OS cancel.
 * Acquire PASSIVE pins immutable exact-address/same-thread reader seal.
 * StepEnter/Leave and Release <=DISPATCH: Enter checks under same gate lock,
 * then RETURNS WITHOUT HOLDING SPINLOCK. Leave permits known revoked cleanup.
 * Release refuses active step, never guesses unknown/copied/tampered identity.
 * Keep pin through every synchronous bounded metadata step/ALL related returns.
 * No asynchronous cross-thread backend/DMA lifetime protocol is supplied here.
 * No reader thread exit with live pin. No root reset/reopen/forced cleanup.
 * Root/code/tokens/inputs trusted resident stable nonaliasing. Exclusive token
 * operations for ENTIRE lifetime across roots. Root unpublished Init once;
 * external storage/module ALL-attempts/all-returns anchor before EVERY API,
 * including failed calls, still REQUIRED. Pins/Inspect do NOT provide it.
 * Close revokes new steps immediately but preserves known obligations; snapshot
 * pins0/steps0/noWriter is NOT free/drain/detach/availability authorization.
 */
NTSTATUS Bc250PermitInit(BC250_PERMIT *,ULONGLONG,ULONG);
NTSTATUS Bc250PermitWriteAdmit(BC250_PERMIT *,ULONG,BC250_PERMIT_WRITER *);
NTSTATUS Bc250PermitWriteClaim(BC250_PERMIT *,const BC250_PERMIT_WRITER *);
NTSTATUS Bc250PermitWriteFinish(BC250_PERMIT *,BC250_PERMIT_WRITER *,NTSTATUS,ULONGLONG);
NTSTATUS Bc250PermitWriteAbandon(BC250_PERMIT *,BC250_PERMIT_WRITER *);
NTSTATUS Bc250PermitAcquire(BC250_PERMIT *,BC250_PERMIT_READER *);
NTSTATUS Bc250PermitStepEnter(BC250_PERMIT *,const BC250_PERMIT_READER *,ULONGLONG *);
NTSTATUS Bc250PermitStepLeave(BC250_PERMIT *,const BC250_PERMIT_READER *);
NTSTATUS Bc250PermitRelease(BC250_PERMIT *,BC250_PERMIT_READER *);
NTSTATUS Bc250PermitClose(BC250_PERMIT *);
NTSTATUS Bc250PermitInspect(BC250_PERMIT *,BC250_PERMIT_SNAPSHOT *);
#endif
