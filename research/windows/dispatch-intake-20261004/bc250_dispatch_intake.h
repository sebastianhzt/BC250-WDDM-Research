/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BC250_DISPATCH_INTAKE_H
#define BC250_DISPATCH_INTAKE_H
#if defined(BC250_INTAKE_MOCK) || defined(BC250_INTAKE_TYPES_MOCK)
#include "../pci-pnp-publisher-20261004/mock-publisher-wdm.h"
#else
#include <wdm.h>
#endif
#define BC250_INTAKE_SIGNATURE 0x494e5431U
#define BC250_INTAKE_LIMIT 0x7fffffffU
#define BC250_INTAKE_WAITING 1U
#define BC250_INTAKE_CLAIMED 2U
typedef struct BC250_INTAKE BC250_INTAKE;
typedef struct BC250_INTAKE_TICKET {
    BC250_INTAKE *Root;
    ULONGLONG ScopeId,Id;
    ULONG Generation,Epoch,Kind,Target;
} BC250_INTAKE_TICKET;
struct BC250_INTAKE {
    KSPIN_LOCK Lock;
    ULONG Signature,Generation,Epoch,Closing,Fault,Phase;
    ULONGLONG ScopeId,LastId;
    BC250_INTAKE_TICKET *Address;
    BC250_INTAKE_TICKET Seal;
    NTSTATUS LastFinal;
};
typedef struct BC250_INTAKE_SNAPSHOT {
    ULONGLONG ScopeId,PendingId;
    ULONG Generation,Epoch,Closing,Fault,Phase,MetadataIdle;
    NTSTATUS LastFinal;
} BC250_INTAKE_SNAPSHOT;
/* Native FALSE; metadata only, NOT an IRP queue, power dispatcher or hardware gate.
 * Initialize root once, unpublished; root/ticket/code external ALL-returns anchor
 * BEFORE EVERY attempt, also rejected/quarantined calls. No root free/reuse here.
 * Resident trusted nonaliasing inputs/outputs; ticket zero before Admit, stable
 * exact address and immutable until matching known Finish/Abandon zeros it.
 * Exclusive per-ticket API access across roots for its entire lifetime.
 * Admit <=DISPATCH records ONE metadata obligation before future enqueue;
 * overlap closes/faults root and rejects new ticket without losing old one.
 * Caller retains ALL OS IRP/work/remove-lock responsibilities. No IRP ownership
 * transfers, no STATUS_PENDING OS decision, no lower forward in these APIs.
 * Claim PASSIVE models worker taking metadata obligation, not workqueue dequeue.
 * Abandon <=DISPATCH only WAITING metadata; never cancels queued OS work/IRP.
 * Finish PASSIVE only CLAIMED after caller has established final completion AND
 * all related bridge/Source cleanup, not merely IoCallDriver return. PENDING is
 * not final. Unknown/copied/tampered ticket must not force guessed cleanup.
 * Close never clears pending. Known matching cleanup allowed after Close/Fault.
 * Root does NOT alter frozen Source. Future ALL backend stages must compose
 * intake guard + Source validation BEFORE any integration, with actual producer
 * admission/IRQL/teardown proof. Current SourceReady alone remains insufficient.
 * CheckIdleAtEpoch success is a historical no-pending observation, NOT Ready,
 * reservation, a lifetime claim or permission for PCI/VRAM/DMA; no post-return
 * guarantee. No FIFO policy: real PnP/power dependency/cancel policy still absent.
 */
NTSTATUS Bc250IntakeInit(BC250_INTAKE *,ULONGLONG,ULONG);
NTSTATUS Bc250IntakeAdmit(BC250_INTAKE *,ULONG,ULONG,BC250_INTAKE_TICKET *);
NTSTATUS Bc250IntakeClaim(BC250_INTAKE *,const BC250_INTAKE_TICKET *);
NTSTATUS Bc250IntakeAbandon(BC250_INTAKE *,BC250_INTAKE_TICKET *);
NTSTATUS Bc250IntakeFinish(BC250_INTAKE *,BC250_INTAKE_TICKET *,NTSTATUS);
NTSTATUS Bc250IntakeClose(BC250_INTAKE *);
NTSTATUS Bc250IntakeInspect(BC250_INTAKE *,BC250_INTAKE_SNAPSHOT *);
NTSTATUS Bc250IntakeCheckIdleAtEpoch(BC250_INTAKE *,ULONG);
#endif
