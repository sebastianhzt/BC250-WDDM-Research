Deferred completion + RemoveLock isolated prototype -- 2026-10-04
SPDX-License-Identifier: Apache-2.0

STATUS: Native FALSE. No installed driver changes or hardware/IOCTL/PCI reads.
Real WDK declarations are compiled only as standalone OBJ; RAM fakes exercise
IRP/remove-lock/work-item sequencing. No SYS/INF/CAT, link/sign/install/reboot.

Protocol
--------
Caller supplies resident one-shot context, incoming borrowed IRP with its OWN
valid driver stack location, initialized FDO remove-lock, trusted classified
source kind/target and fixed START resource packet. Real IRP classifier and
CM_RESOURCE_LIST parser are absent; no user buffers or commands accepted.
External anchor covers context/Source/FDO/Lower/module/code before entry through
ALL dispatch/completion/worker returns, including failed attempts/quarantine.
This external publication/module lifetime solution is NOT provided here.

Dispatch is PASSIVE only (frozen SourceBegin restriction). Policy/parameter
rejection sets Taken=FALSE and never touches/completes IRP: caller retains it.
Accepted context sets Taken=TRUE. Every setup error then completes incoming IRP
once locally with error/zero information, or keeps explicit quarantined source
obligations if their cleanup cannot be established. It never silently transfers
IRP ownership back. Use zero/init/dispatch ONCE, no reset/reuse/field mutation.

Acquire two separate RemoveLock tags: dispatch until IoCallDriver RETURN;
worker/request until SourceComplete and retained IRP completion+work cleanup.
Work item allocated BEFORE SourceBegin/registration/forward. SourceBegin
invalidates the source before forward; registration failure closes source
before completing known ticket, not a fabricated query rollback to Ready.
IoCopyCurrentIrpStackLocationToNext before IoSetCompletionRoutineEx; never skip
stack while marking pending. Exact registration SUCCESS implies mandatory
IoCallDriver with no intervening fallible branch. Mark pending BEFORE forward,
ALWAYS return STATUS_PENDING for forwarded flow, independent of lower return.

Completion <=DISPATCH copies terminal IRP Status/Information under context lock,
and marks pending if PendingReturned. It queues preallocated work outside lock,
returns MORE_PROCESSING_REQUIRED to retain IRP. NO IRP/context/work access after
IoQueueWorkItem: worker may already finish before that call returns. Queue is
VOID, not a failable-status API. Actual failure injection is work allocation,
remove-lock acquisition or completion registration BEFORE forward, not an
invented queue error. Duplicate/wrong-IRP/nonfinal STATUS_PENDING quarantines
known obligations, closes source, never guesses finality or double-queues.

Worker PASSIVE transitions QUEUED->RUNNING once, calls frozen SourceComplete
outside context lock; known ticket zeroing is checked before releasing owned
obligations. Stale completion cleans known source ref, never resurrects Ready.
Unknown/tampered ticket retains IRP/work/remove lock in permanent quarantine.
Known completion copies captured lower final status/information into IRP,
IoCompleteRequest -> IoFreeWorkItem -> release worker tag, each outside lock.
No IRP access after complete; free only from dequeued worker or pre-forward
unused work. No context access after final tag release in each finishing path.
Dispatcher tag still protects when worker finishes inline before IoCall return.

All observable context metadata updates after init use its short spinlock.
No nested Source lock, Source APIs, allocation, forwarding, completion, work
queue/free or remove-lock operation under context lock. Buffers kernel-trusted,
resident/aligned/stable/nonaliasing. One original incoming IRP per context.
Inspect is historical metadata only, NOT a reclaim/teardown authorization.
HeldTags records bookkeeping (not an atomic lifetime proof during DDI calls).
Removal must wait bridge/remove-lock + external ALL-calls anchor; SourceDrain
alone can finish before worker completes retained IRP and is insufficient.
No self-wait or IoReleaseRemoveLockAndWait in this prototype; that belongs to
the future REMOVE dispatcher with its separately owned remove IRP and anchor.

Boundaries still open
---------------------
This is not integrated into Build22 or an OS driver stack. Incoming IRP stack,
Kind/target classification and copied START packet are trusted preconditions.
Power dispatch can itself arrive DISPATCH: initial dispatch deferral is still
absent; do not install this PASSIVE dispatcher as the real power dispatcher.
The frozen source permits only ONE pending operation total. Real PnP/power
overlap needs reviewed scheduling/serialization before any integration.
Source cancel semantics are conservative, not a complete Windows CANCEL_IRP
status policy. Forwarded final lower status is preserved by this prototype.
No cancellation of queued work or real IoCancelIrp, timeout, quarantine recovery,
actual root/module publication, FDO allocator/free, pool management, DriverUnload
coordination, physical availability, DMA, VRAM/GART/MC proof or engines.
Native policy FALSE in dispatcher/callback/worker/inspect, no activation switch.

Verification
------------
verify-offline.bat <absolute-python.exe>, VS2022 Community x64/WDK26100.
80 stages /O2 + /Od, /W4 /WX /GS:26 RAM EXE +14 WDK OBJ, no SYS link.
10 candidate files and106 predecessor files frozen by SHA-256. RAM tests cover
24 lower-immediate/delayed x worker-early/delayed x lower-return x close tuples,
partial RemoveLock acquisition, alloc/registration failure, failed ownership
admission, success/error/warning/info, stale completion, unknown-finality and
duplicate callbacks/tampered source tickets. Early worker is simulated during
queue callback (cross-thread logical interleave), NOT a Windows SMP proof.
Fake IRP completion poisons status to catch production accesses after ownership
ends; fixture counters remain test-owned metadata, not actual live OS IRPs.
Closed-policy test proves zero platform/IRP/Source calls. All frozen regressions
source/capture/ABI/publisher/admission/PCI/gate repeat both configurations.
WDK /Od may retain IofCallDriver import ONLY for this native-FALSE prototype;
runner permits it explicitly here, rejects in other units, still rejects DMA,
MMIO/map/port/register imports everywhere. No actual OS IRP is ever forwarded.
RESULT reports actual_os_irp_forwarded=false, actual_work_item_queued=false,
actual_remove_lock_executed=false and other unproved capabilities explicitly.

Next gate
---------
Review initial DISPATCH dispatch deferral and overlap/cancel ownership, then
bounded START parser + root/module publication and REMOVE coordinator. Only
after those gates evaluate a new DISARMED diagnostic package. W2P unchanged;
no enable SDMA/GART/DMA. Source-only development; no main/release/binaries.
Original predecessor notices retained; new prototype is local research.
