/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BC250_PNP_COMPLETION_H
#define BC250_PNP_COMPLETION_H
#if defined(BC250_PNP_COMPLETION_MOCK) || defined(BC250_PNP_COMPLETION_TYPES_MOCK)
#include "mock-completion-wdm.h"
#else
#include <wdm.h>
#endif
#include "../pnp-source-20261004/bc250_pnp_source.h"
#define BC250_COMPLETION_SIGNATURE 0x434f4d31U
#define BC250_COMPLETION_PREPARED 1U
#define BC250_COMPLETION_REGISTERED 2U
#define BC250_COMPLETION_QUEUED 3U
#define BC250_COMPLETION_RUNNING 4U
#define BC250_COMPLETION_DONE 5U
#define BC250_COMPLETION_FAULT 6U
#define BC250_COMPLETION_DISPATCH_TAG 1U
#define BC250_COMPLETION_WORKER_TAG 2U
typedef struct BC250_COMPLETION {
    KSPIN_LOCK Lock;
    ULONG Signature,Phase,HeldTags,RequestOwned;
    UCHAR DispatchTag,WorkerTag;
    PDEVICE_OBJECT Fdo,Lower;
    PIO_REMOVE_LOCK RemoveLock;
    PIRP Irp;
    PIO_WORKITEM Work;
    BC250_SOURCE *Source;
    BC250_SOURCE_REQUEST Request;
    BC250_SOURCE_RESOURCES Resources;
    ULONG HaveResources;
    NTSTATUS FinalStatus,SourceStatus;
    ULONG_PTR FinalInformation;
} BC250_COMPLETION;
typedef struct BC250_COMPLETION_STATUS {
    ULONG Phase,HeldTags,RequestOwned,WorkPresent,IrpPresent;
    NTSTATUS SourceStatus;
} BC250_COMPLETION_STATUS;
/* Isolated prototype, native FALSE. No installed driver integration.
 * Context zero/init/run ONCE; never reset/reuse. Incoming caller-owned IRP with
 * this driver's valid stack location. Kind/target/resources from FUTURE trusted
 * classifier + bounded START parser, NOT a user command or independent query.
 * Dispatch is PASSIVE ONLY; real DISPATCH dispatch deferral is not implemented.
 * Context/Source/FDO/Lower/remove-lock/module/code/inputs externally anchored
 * BEFORE all calls through ALL dispatch/completion/worker returns, even failures.
 * OS remove-lock initialized by caller; no teardown wait inside this helper.
 * Remove lock does not replace that publication/module/storage lifetime anchor.
 * Taken FALSE: helper took no IRP ownership, caller must handle error/forwarding.
 * Taken TRUE: helper completed a pre-forward error once OR owns forwarded IRP
 * until worker completes; forwarded dispatch ALWAYS marks+returns PENDING.
 * Separate dispatch+worker tags: worker can finish before IoCallDriver returns.
 * Registration SUCCESS MUST be followed by IoCallDriver. Work allocated before
 * registration; queue is VOID/no failure status. Completion <=DISPATCH performs
 * no SourceComplete, no waits, no IRP/context access AFTER IoQueueWorkItem.
 * Worker PASSIVE owns retained IRP, preserves captured status/information,
 * SourceComplete -> IoCompleteRequest -> FreeWork -> worker-tag release.
 * No IRP access after IoCompleteRequest; no context access after LAST tag release
 * in each path. External ALL-returns anchor still required for code/storage.
 * SourceDrain alone is NOT sufficient: removal must drain bridge/remove-lock
 * and all API/callback attempts before unpublishing/reclaiming any storage.
 * All buffers resident, immutable, stable and nonaliasing; one dispatch per
 * context/IRP; helpers/callbacks only entered under trusted original ownership.
 * Fault/unknown/nonfinal completion quarantines known obligations; no guessed
 * cancel/release/free. No backend queue cancellation/recovery or timeout exists.
 */
NTSTATUS Bc250CompletionDispatch(BC250_COMPLETION *,BC250_SOURCE *,PDEVICE_OBJECT,PDEVICE_OBJECT,
    PIO_REMOVE_LOCK,PIRP,ULONG,ULONG,const BC250_SOURCE_RESOURCES *,BOOLEAN *);
IO_COMPLETION_ROUTINE Bc250CompletionCallback;
IO_WORKITEM_ROUTINE Bc250CompletionWorker;
NTSTATUS Bc250CompletionInspect(BC250_COMPLETION *,BC250_COMPLETION_STATUS *);
#endif
