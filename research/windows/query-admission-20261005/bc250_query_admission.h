/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BC250_QUERY_ADMISSION_H
#define BC250_QUERY_ADMISSION_H
#if defined(BC250_QUERY_ADMIT_MOCK) || defined(BC250_QUERY_ADMIT_TYPES_MOCK)
#include "../pci-pnp-publisher-20261004/mock-publisher-wdm.h"
BOOLEAN KeAreApcsDisabled(VOID);
extern BOOLEAN Bc250QueryAdmitMockAllowed;
#else
#include <wdm.h>
#endif
#define BC250_QA_SIGNATURE 0x51414431U
#define BC250_QA_QUERY_STOP 1U
#define BC250_QA_QUERY_REMOVE 2U
#define BC250_QA_STOP 4U
#define BC250_QA_SURPRISE 8U
#define BC250_QA_POWER_DOWN 16U
#define BC250_QA_REMOVE 32U
typedef struct BC250_QUERY_ADMISSION BC250_QUERY_ADMISSION;
typedef struct BC250_QUERY_TICKET {
    BC250_QUERY_ADMISSION *Domain;
    ULONGLONG Scope,Generation,Epoch,Id;
} BC250_QUERY_TICKET;
struct BC250_QUERY_ADMISSION {
    KSPIN_LOCK Lock;
    ULONG Signature,Reasons,Used,Quarantined;
    ULONGLONG Scope,Generation,Epoch;
    BC250_QUERY_TICKET *Address;
    BC250_QUERY_TICKET Seal;
};
/* Native FALSE, no activation switch. Metadata-only one-shot admission.
 * Init once exclusively unpublished; resident aligned trusted nonaliasing
 * storage, independent of request/child payload. Scope/Generation/Epoch are
 * nonzero immutable trusted incarnation IDs, not a hardware observation.
 * Parent/domain/token/module/issuer independently anchored BEFORE every API
 * entry through ALL returns, including unsuccessful entrants and cleanup.
 * Domain does NOT acquire OS refs/remove tags/rundown or lifetime permission.
 * Enter/Check: PASSIVE with normal kernel APCs enabled. Only short bookkeeping
 * spinlock; released/restores IRQL before return. No callbacks/waits/mutex or
 * critical region across query. One successful Enter EVER per domain, stable
 * exact-address ticket sealed independently; never copy/edit/reuse active token.
 * Close <=DISPATCH, monotonic one reason bit. Participating writers must close
 * BEFORE accepting lifecycle transitions. Already admitted work may continue
 * inside OS/provider; Check is historical, not atomic hardware availability.
 * No real PnP/power participant or source validation supplied here. Cancellation
 * closes admission only, never cancels an IRP or an in-progress provider call.
 * Leave <=DISPATCH ONLY after ALL body/query/read/interface cleanup is known
 * complete. Query UNKNOWN uses Quarantine and retains ticket + ALL roots,
 * including issuing thread if IRP outstanding. Missing/tampered ticket retains
 * obligations; no guessed release/reset/drain/reopen/recovery/free operation.
 * Used remains set after known Leave. New incarnation needs new independently
 * anchored domain and retirement/publication protocol NOT implemented here.
 * No fields/counts/Leave result are parent/module reclaim authority. No SMP,
 * liveness, real Windows references, physical PCI or hardware proof tested.
 */
NTSTATUS Bc250QueryAdmitInit(BC250_QUERY_ADMISSION *,ULONGLONG,ULONGLONG,ULONGLONG);
NTSTATUS Bc250QueryAdmitEnter(BC250_QUERY_ADMISSION *,BC250_QUERY_TICKET *);
NTSTATUS Bc250QueryAdmitCheck(BC250_QUERY_ADMISSION *,const BC250_QUERY_TICKET *);
NTSTATUS Bc250QueryAdmitClose(BC250_QUERY_ADMISSION *,ULONG);
NTSTATUS Bc250QueryAdmitLeave(BC250_QUERY_ADMISSION *,BC250_QUERY_TICKET *);
NTSTATUS Bc250QueryAdmitQuarantine(BC250_QUERY_ADMISSION *,const BC250_QUERY_TICKET *);
#endif
