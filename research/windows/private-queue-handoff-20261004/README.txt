Private-request queue handoff -- RAM fixture -- 2026-10-04
SPDX-License-Identifier: Apache-2.0

NO NEW DRIVER/NATIVE ADAPTER. No SYS/INF/CAT/sign/package/install/reboot.
Installed Build22 unchanged; Build11/21/22 and all predecessor sources intact.
No PCI/MMIO/VRAM/GART/firmware/ring/DMA, real IRP, real CSQ or work item.
No new current driver route accepts requests. PnP/power/diagnostic kinds rejected.

This checkpoint implements test-handoff.c: a one-private-request RAM protocol
fixture calling the existing frozen MOCK Life component. The fixture is NOT
code suitable for linking into a driver and not a substitute for Windows CSQ.
Real CSQ performs synchronization; here fake insert/peek/remove/lock/cancel
callbacks provide deterministic serial ownership arbitration. Parent, module,
request pool, child pool, envelopes and all token storage are EXTERNALLY
anchored through every attempt and return. Fake ref/remove tags are receipts,
NOT ObReferenceObject/IoAcquireRemoveLock, actual allocations or module pins.
No real pointer-free/UAF, SMP, race or OS-return guarantee is proved.

1. Exact handoff boundary exercised
Dispatch LifeEnter precedes any child access. Prepare TWO independent
envelopes: request cleanup and one-shot worker wakeup. Eight acquisition steps:
request envelope allocation receipt, ref, tag, child hold; wake allocation
receipt, ref, tag, child hold. All before mark/insert/scheduling. The request
envelope retains its exact request pointer outside the child; it is cleared
at completion before poison, never guessed from queue status. Each acquired
receipt is unwound on failure; pre-transfer failure completes only locally
owned private request, returns failure, and schedules NOTHING. Close can
reject either hold after earlier preparation succeeds; those receipts unwind.
Rejected classifications and closed admission leave request to the caller.

Fixture pending is marked BEFORE the void-shaped fake CSQ insert. Completion can occur
inside insert BEFORE dispatch resumes, with the fake request bytes poisoned.
Precancel bypasses the insert callback, matching the documented CSQ branch;
ordinary insertion uses insert/peek/remove callbacks under the fake queue lock.
Real IoCsqInsertIrp itself marks inserted IRPs pending; this fixture premarks
before ANY transfer to also cover precancel, not as a proof of actual OS ABI.
Dispatch never touches request again. Queue wakeup uses independent Parent/
wake envelope, even if request was inline-cancelled and queue is empty.
No post-transfer allocation/fallible work-item queue call is invented.
Fake queue can run worker inline; body may release its resources before the
queue call returns. Dispatch retains separate LifeCall until its own last
child access/output ends. Captured local STATUS_PENDING is returned; completed
request status is never read afterward. This is NOT an actual IRP pending ABI.

Queue callbacks only manipulate the slot under their queue lock. Completion,
Life APIs and resource cleanup occur outside that lock; fake spinlock rejects
nesting. Cancellation arbitrates against dequeue: only one receives ownership.
Cancellation after dequeue does not regain request through CSQ. PRIVATE worker
policy may observe a later cancel bit and locally complete, since this fixture
NEVER forwards its request. No incoming PnP/power or forwarded IRP may inherit
that rule. Mandatory forward/second completion worker from prior checkpoints
remain frozen and independent, NOT integrated into this new queue fixture.

Cancel callback uses DISPATCH-level fake context, initial worker PASSIVE.
Request callback/worker enters via pre-reserved Hold BEFORE child use, exits
after terminal completion and final child access, then releases child Hold.
Tag cleanup occurs before last Hold; explicit envelope ref/receipt persists
through helper return and final Parent-only cleanup. Delayed worker test retires
and poisons child INSIDE the last Hold release helper after its lock unlock,
before caller returns. This tests ordering, NOT an OS anchor or unload proof.

2. Cases and limits
Timing matrix: 6 cancel placements x6 close placements x2 worker timings=72.
Cancel placements: none, precancel, after insert, before dequeue, after dequeue
(CSQ loses), and private-owned cancellation. Close placements mirror points.
8 failure points x9 close placements=72 setup unwind cases. Additionally eight
Close-only hold rejection cases, three classifier rejections, closed admission
and one tampered request Hold quarantine. Expected fixtures total157/profile.
Completed requests poisoned before callbacks return; successful child detach
poisons child. Duplicate queue cancel/dequeue gets empty; no second completion.
Observation counters/checks are assertions of this serial fixture, not events
captured from real hardware/Windows. No allocation fail AFTER insertion is
modelled because all needed resources were prepared first.

Unknown exact identity is never guessed: tampered request Hold blocks child
entry, completion and release; its known ref/tag/receipt and exact request
identity are retained in the independent request envelope. Worker can release
its own unrelated known resources. Child detach remains BUSY.
End of test destroys the externally anchored world only after fake frames
returned; NOT an UNKNOWN production recovery/remove/unload policy.
Capacity one, no fairness/multiple producers/coalescing proof. Close does not
retroactively abort accepted continuation; cancellation remains independent.
No publication lookup, real FDO lifetime, native CSQ cancel synchronization,
allocation IRQL, actual remove lock/module, initial dispatch deferral, power
dependencies or full completion-bridge composition implemented.

3. Reproduce and publication
verify-offline.bat <python.exe> initializes VS2022 x64 then runs campaign.
Requires complete repository, MSVC and WDK10.0.26100.0; output unique ignored
output/private-queue-handoff-*. Two new RAM EXE (/O2,/Od) plus ALL134 frozen
stages: total138,48 RAM EXE +20 native-FALSE WDK /c OBJ +2 model JSON=70.
Six new sources and175 frozen predecessor sources hashed before/after.
/W4 /WX /GS, no hidden CL/_CL_/LINK options; protected files unchanged.
Only six reviewed sources go to development; no binary/private evidence/main.
AGENTS requires independent PREBUILD Code Reviewer before every campaign.

Next gate: reviewed native-FALSE Self-FDO envelope-reference adapter and its
exact acquire/failure/last-drop wrappers. Must cover OS-entry bootstrap,
nonpaged storage/IRQL/module return and UNKNOWN recovery BEFORE actual driver
linking. This checkpoint does not permit DMA/W2P memory ownership.
Primary Microsoft CSQ semantics are linked in primary-source.json; no copied
third-party implementation or new firmware added. Existing attribution intact.
