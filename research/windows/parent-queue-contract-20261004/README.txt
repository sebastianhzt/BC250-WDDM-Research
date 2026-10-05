Parent / module / private queue -- WINDOWS DESIGN checkpoint -- 2026-10-04
SPDX-License-Identifier: Apache-2.0

THIS IS A DESIGN + FINITE ABSTRACT MODEL, NOT A NEW DRIVER OR NATIVE ADAPTER.
No SYS/INF/CAT/sign/package/install/reboot. Build22 installed unchanged and
Build11/21/22 sources/backups intact. No PCI/MMIO/GPU/VRAM/GART/ring/DMA.
Frozen child-lifetime/permit/Source/completion implementations are unchanged.
No new queue accepts actual IRPs; every current route queue_authorized=false.

1. Actual baseline, not the legacy root KMD
Installed build22 candidate source is diagnostic-package-build22-20261004/source.
bc250_diagnostic_driver.c provides TWO named zero-extension CDO endpoints;
they must be classified BEFORE interpreting a PCI FDO extension. FDO goes to
amdbc250_dream_pnp.c: unnamed attached FDO, extension RemoveLock, global
binding push lock, identity-only classification at DISPATCH. Started PDO lookup
already acquires remove lock WHILE shared binding publication lock held, then
refs PDO/lower. REMOVE unpublishes under exclusive binding lock, forwards,
IoReleaseRemoveLockAndWait, detaches/deletes. Power currently pass-through.
This audit identifies integration seams, NOT a new runtime bug or OS proof.
Source of legacy src/kmd/amdbc250_dream_kmd.c is NOT the installed entrypoint.

2. Proposed terminal anchor hierarchy (conditional on legitimate WDM calls)
FDO owns nonpaged stable Parent registry in extension; Child is separate.
Ordinary FDO dispatch: OS-supplied DeviceObject/IRP entry validity precedes
remove-lock acquisition. Do not acquire a ref through an already stale pointer.
CDO query: endpoint OS entry anchors module/CDO; PASSIVE shared binding lock
guards lookup until successful FDO remove-lock acquisition + explicit FDO ref.
At DISPATCH do not take existing PASSIVE binding push lock or global naked FDO.
Asynchronous envelope: stable nonpaged allocation, exact sealed child HOLD and
explicit Self-FDO ref BEFORE publishing Context/registration/queue. Add tags
for EACH independent dispatch/initial worker/completion worker/callback need.
Parent/token/envelope remain distinct from Child retirement. FDO/PDO/lower
refs preserve software object storage, NOT physical GPU access availability.

IoSetCompletionRoutineEx on owning Self-FDO protects completion MODULE return
under its documented contract; it does NOT retain arbitrary Context storage.
Envelope/ref must do that. Successful registration MUST be followed by forward.
IoQueueWorkItem contributes a SYSTEM FDO reference. That reference is distinct
from driver's RemoveLock and explicit envelope ref, and survives work body
cleanup through the kernel callback-return boundary. IoFreeWorkItem inside
dequeued callback is not proof that callback/module returned. Use Io work items,
not ExQueueWorkItem as an imagined unload guarantee. No ObReference(DriverObject)
trick or another self-referential registry creates a module lifetime proof.
Dispatch bootstrap/code-return validity remains an explicit architectural
OS-entry assumption to audit, not established by our model or a driver bool.

Trailing cleanup proposal: finish ALL child access and capture output -> LifeExit
-> ReleaseHold -> capture/use only stable envelope/Parent -> free envelope and
drop explicit Self-FDO ref at final Parent access -> stack-only return protected
by selected OS DDI contract. Never touch Parent/envelope after last protecting
ref drop. Actual wrapper ordering, IRQL, cancel and return chain still unbuilt.
System return events in model are ENVIRONMENT axioms, never driver-set fields.

3. Cancellation scopes MUST stay separate
Only a FUTURE specifically classified private cancellable request may use CSQ.
All existing PnP/power IRPs keep their stack-specific semantics and current
pass-through paths. CANCEL_STOP/CANCEL_REMOVE != generic IRP cancellation.
No whole-stack FIFO held until lower pending START/power completes: dependency
cycles can prevent needed IRPs from executing. Source one-operation metadata
is not a valid OS scheduling policy or permission to fail an arbitrary IRP.

Private prequeue plan: preallocate resident envelope/work/ref/remove tags,
reserve child holds, mark pending BEFORE ownership transfer, insert via CSQ.
After insertion never inspect IRP (inline precancel may already complete it).
Schedule a one-shot wakeup via independent Parent/work envelope even when
IRP was inline-cancelled; worker may run empty. This avoids a lost wakeup and
does not treat a second status/IRP inspection as ownership. Real CSQ callbacks,
queue lock order/insert rejection handling and allocation unwinds need review.
Model abstracts atomic arbitration: csq_cancel OR dequeue wins, never both.
IoCsqRemoveNextIrp returns uncancelled queued IRP; cancellation after dequeue
belongs to the PRIVATE worker's explicit policy, not a CSQ-owned completion.
After successful completion registration, mandatory forward persists even if
close/cancel occurs; after forward lower completion owns final status. Do not
call IoCancelIrp arbitrarily on incoming IRP you did not create. Cancel bit or
IoCancelIrp boolean is not terminal completion and does not grant local free.
Abstract model's lower completion omits exact bridge second-worker sequence;
that is independently tested in frozen lifetime-completion, not reimplemented.

4. Removal/unload ordering and quarantines
Close admission/unpublish -> route REMOVE down first -> passive remove-lock
drain outside all locks and dependent workers -> known Child/dependency cleanup
-> detach lower -> request FDO deletion once. Object deletion may be pending
while SYSTEM work/FDO refs remain; module can remain pinned by completion DDI
until its OS return even after final Parent storage is no longer used.
No reset/reopen of a drained RemoveLock; new FDO/identity for new incarnation.
Named CDO lifetime/unload, handles, AddDevice failure, and module publication
must be covered separately, not solved by the PCI FDO RemoveLock alone.
Unknown obligations KEEP known refs/tags/holds; model labels blocked states.
This is NOT an acceptable completed production REMOVE/unload policy: unknown
recovery/progress needs explicit design before adapter activation. No timeout
free, fabricated successful REMOVE or indefinite worker wait is implemented.

5. Executable evidence and limitations
model.py is immutable finite one-request DESIGN graph, not compiled native C.
test-model.py explores every reachable abstract state/transition, rejects unsafe
ownership paths, checks mutation counterexamples, and saves four targeted trace
classes: inline CSQ precancel, dequeue-vs-cancel, Child retirement before kernel
return, and unknown quarantine. Environment ordering/ref contracts are ASSUMED.
Atomic events don't prove real CSQ synchronization/SMP, linearization, OS module
lifetime, liveness/fairness, pointer safety or actual IRP flags/ABI/classification.
No hardware reservation/readiness or initial deferral is implemented.
verify-offline repeats model with normal Python and -O (checks don't use assert)
and all132 frozen regression stages:46 RAM EXE +20 WDK /c native-FALSE OBJ.
New model contributes2 stages +2 accepted JSON reports (134 total/68 artifacts).
Eight candidate sources +167 predecessor sources are protected before/after.
All raw outputs remain local ignored output; source-only development publication.

Next bounded implementation gate
Model an EXACT private-request queue-envelope handoff and allocation unwind in
RAM using CSQ-shaped fake callbacks, separately from PnP/power. Then implement
and compile a reviewed native-FALSE Self-FDO envelope-reference adapter before
linking any new DISARMED package. Parent/module DDIs + initial deferral + stack
policy and unknown recovery remain prerequisites, not solved by this checkpoint.

Primary Microsoft sources and explicit source audit hashes:primary-source.json.
