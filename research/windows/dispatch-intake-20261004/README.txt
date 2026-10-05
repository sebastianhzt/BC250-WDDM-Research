DISPATCH intake metadata guard -- 2026-10-04
SPDX-License-Identifier: Apache-2.0

Native FALSE. This substep is NOT an actual initial dispatch deferral wrapper.
No IRP, work item, Source, completion or RemoveLock calls; NO SYS/INF/CAT.
WDK /c declaration checks only; RAM mocks exercise short spinlock/IRQL model.

Why not a universal FIFO of real PnP/power IRPs?
Power up/down ordering and dependencies need stack-specific review. Serializing
an entire lower pending START/power operation can prevent a dependent IRP from
running. The frozen Source supports one operation and faults on overlap; we
must not turn its metadata constraint into an OS IRP scheduling policy.

Implemented
Init unpublished PASSIVE; one stable exact-address immutable metadata ticket.
Admit <=DISPATCH blocks the intake observation immediately, before a FUTURE
worker enqueue. Initial work has not been claimed, let alone forwarded yet.
Claim PASSIVE moves WAITING->CLAIMED once. Finish PASSIVE only terminal known
completion after caller establishes bridge/Source cleanup; PENDING cannot
retire. Waiting Abandon <=DISPATCH cleans only known metadata, never queued
OS work, IRP cancellation or RemoveLock. Claimed Abandon refuses cleanup.
Second overlapping admission rejects second metadata ticket, permanently
closes/faults root, preserving first exact obligation for known cleanup.
Close never discards obligations. Known Finish/Abandon can zero matching
ticket after Close/Fault, without restoring open/Ready. Counter exhaustion
closes/faults without wrapping; exact scope/generation/id/epoch/kind/target
seal and exact address reject copied/cross-root/tampered tickets.
Output CheckIdleAtEpoch success means historical metadata-idle only, NOT
hardware readiness/ownership/access or a reservation after return. Admission,
claim, final retirement and Close change epoch; old idle observation fails.

Lifetime/inputs
External ALL-attempts/all-returns root/ticket/code anchor mandatory before
EVERY call, even rejected calls. No ownership refs/free/reopen/reset here.
Resident trusted stable nonaliasing buffers and exclusive per-ticket calls
across roots. Scope unique, generation positive below signed counter limit.
Root initialized once; no caller field edits/recycling during life. Fixtures
seed private freshly initialized roots for exhaustion tests only.
One lock protects observable tuple; no Source/DDI/platform call under lock.
Caller still owns ALL IRP/work/remove-lock obligations. Rejection status is
NOT permission to complete/fail a real power IRP. No real packet accepted.
Unknown/tampered ticket intentionally pinned; no forced cleanup/recovery.

Important missing integration
The guard does NOT invalidate frozen Source directly. SourceReady can remain
true in isolation. No existing backend consumes this guard yet. Future ALL
backend stages must compose guard + Source validation under reviewed admission
and lifetime protocol; historical checks alone cannot reserve the state.
No queue/dequeue/IoMarkIrpPending/cancel-safe queue/classifier/parser, no actual
DISPATCH->PASSIVE work scheduling, no PoCallDriver choice/order, no final
OS cancel policy, no PnP/power overlap recovery or root publication/remove.
Close/idle are not drain/reclaim proofs. Real worker/RemoveLock/cancellation
and module/code lifetime await a separate reviewed implementation.
Prior completion bridge remains untouched, PASSIVE initial dispatch only.
Build22 stays installed unchanged; W2P DISARMED and VRAM/GART/MC proof absent.

Verification
verify-offline.bat <absolute-python.exe>, VS2022 x64/WDK26100.
92 stages /O2 + /Od /W4 /WX /GS:30 RAM EXE +16 WDK OBJ, no driver link.
9 new source files and116 predecessor files protected by SHA256.
36000 operation cycles,144 kind/phase/close overlap tuples, simulated IRQL
0/1/2 admissions, PASSIVE claim/final guard, abandon vs claim, stale epochs,
unknown/copy/tamper/cross-root, invalid kind/target and counter exhaustion.
Eight native APIs close without platform calls; prior completion/Source/
capture/ABI/publisher/admission/PCI/gate regressions repeat both profiles.
The old completion /Od forward import is only exception allowed in that closed
predecessor unit, never executed in kernel. New intake has no such import.
Tests are deterministic RAM fixtures, NOT SMP, OS IRPs or physical hardware.

Next gate
Define trusted classifier and stack-specific power/PnP dependency policy,
cancel-safe ownership plus preallocated work/remove-lock ALL-return anchors;
compose guard with Source/bridge without leaving stale Ready consumers.
Only after review consider real initial deferral in a new DISARMED package.
Source-only development, no main/releases/PR/binaries/private evidence.
