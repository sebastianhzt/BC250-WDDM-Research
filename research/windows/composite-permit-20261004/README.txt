Composite participant metadata permit candidate -- 2026-10-04
SPDX-License-Identifier: Apache-2.0

Purpose in the BC-250 project
Prepare resource/control-plane ownership before a future DISARMED integration:
detect stale state, keep known obligations until completion, and coordinate
participating metadata readers/writers instead of treating a snapshot as a
reservation. This does NOT solve VRAM reservation, GPU/MC translation, GART,
firmware bootstrap, SDMA execution, WDDM acceleration or Vulkan GPU rendering.
Build22 unchanged. New native policy always FALSE; RAM execution only, WDK /c.

Protocol
Init unpublished once, initially no published revision. Writer Admit <=DISPATCH
revokes new reads/steps immediately, records exact writer ticket and epoch.
Existing reader pins/entered steps are retained, not forcibly canceled/freed.
Writer Claim PASSIVE returns BUSY while ANY pins or steps exist, no wait/spin
loop. Participating writer cannot mutate represented metadata before Claim.
Reader Acquire PASSIVE creates independently sealed exact-address/same-thread
pin, capacity8. StepEnter <=DISPATCH grants bounded synchronous metadata step
under the central lock, then RELEASES spinlock before returning. No long-lived
spinlock across use. StepLeave then Release finish known reader obligations,
also after writer intent/Close/revocation. Release refuses an active step.
Admitted writer blocks new steps; old reader may finish only already-entered
step then must release pin. No refresh into a newer revision allowed.
Once all reader pins drain, Claim can succeed; exact terminal SUCCESS with
strictly newer valid synthetic revision publishes. PENDING never retires.
Known error leaves unpublished; warning/info or bad revision faults closed.
Waiting Abandon cleans metadata only, leaves unpublished, not queued-work or
IRP cancellation. Overlap faults closed retaining original writer ticket/pins.
Close revokes new admission but never clears known obligations or restores
publication. Counters cannot wrap. Unknown token remains pinned/quarantined.

Participation and physical limits
Revision is a synthetic publication label, NOT Source tuple validation or a
CPU/GPU/MC address. No Source/intake/bridge bound into this candidate yet.
ALL writers of future represented data must participate; an unregistered
writer can change external data despite a granted metadata step. Negative RAM
fixture explicitly demonstrates that bypass limit WITHOUT touching Root/hardware.
Physical surprise removal, power loss, PCI availability and asynchronous DMA
can happen outside the gate. Pin does NOT authorize hardware or prevent them.
No real root refs/pool/module/remove-lock/OS queue/classifier/parser/writer
provider/producer/power dependency/cancel-safe/reschedule/timeout supplied.
No universal FIFO inferred from BUSY or one-writer metadata constraint.

Lifetime
External root/token/code/storage ALL-attempts/returns anchor before EVERY API,
including failures, still mandatory. Pin/Inspect/pins0 never prove it or permit
root free/detach. Inputs kernel-trusted resident stable aligned nonaliasing.
Init exclusive unpublished; no reset/reopen/field edits. Scope unique/generation
bounded. Tokens immutable and exclusive across roots for entire lifetime;
reader never leaves its thread or exits with live pin. No cross-thread async
backend completion protocol. Finish only after independently known writer,
Source/bridge/IRP cleanup; this metadata API itself proves none of those.
Each observable change uses short central spinlock, no nested locks/callbacks,
waits/allocation/DDIs under it. GetCurrentThread before spinlock; bounded scans8.
Quarantine retains obligations deliberately; no forced unknown cleanup.

Verification
verify-offline.bat <absolute-python.exe>, VS2022 x64/WDK26100.
108 stages /O2 + /Od /W4 /WX /GS:36 RAM EXE +18 WDK OBJ /c.
9 candidate files and131 frozen predecessor files hashed; all regressions.
2000 rounds x8 reader pins/active steps, writer intent at DISPATCH, no change
until reader drain, revocation/release, terminal publication; close/overlap,
copy/cross-root/wrong-thread/tamper, error/warning/info/bad revision/counters.
Eleven APIs native FALSE, closed test zero platform calls/zero outputs.
No driver link/SYS/INF/CAT/sign/install/reboot/IOCTL/PCI/MMIO/VRAM/DMA.
No actual Windows SMP/IRPs/RemoveLock/rundown/physical resource test. Existing
closed completion-Od forward import only exception; all other units reject it.

Next gate
Bind producer tuple and ALL writer routes to central gate with validated
participation/lease+operation lifetimes, not a historical ready boolean.
Demonstrate Source tuple stable during permitted step and mutation blocked
until release, including delayed/failed/stale completion and removal intent.
Then stack-specific PnP/power/cancel/initial-deferral ownership and real resource
parser/publication before evaluating another isolated DISARMED package.
Source-only development, no main/release/PR/binaries/private evidence.
