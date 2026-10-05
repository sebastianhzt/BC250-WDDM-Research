Intake + Source + completion bridge composition fixtures -- 2026-10-04
SPDX-License-Identifier: Apache-2.0

RAM TESTS ONLY. No new native API/export or driver integration. All frozen
production policies remain FALSE. NO SYS/INF/CAT, IOCTL, install or reboot.
Test-composition.c includes frozen completion test fixture with main renamed,
then frozen intake implementation. No predecessor copy/edit; original fake
platform and Source/bridge are reused exactly. Original completion fixtures
run again before new tests; runner also repeats all prior separate suites.

Historical sample protocol (test helper ModelSample only)
Inspect intake -> require idle/current epoch -> acquire exact Source lease ->
validate Source -> revalidate Source -> release known lease -> check original
intake epoch still idle -> copy historical output, otherwise zero.
Same scope/generation required for intended pair; root/address/all-returns
lifetime remains externally anchored and never proved merely by same IDs.
Lease always released on observed validation/pair/guard failure. No nested
root locks; predecessor APIs acquire their own short locks independently.

Window tests
28 stage/action tuples: seven explicit sequential injection points and four
actions (DISPATCH admission, admission+waiting abandon ABA, guard close,
Source-only close). Early guard blocks even though Source is still Ready;
idle ABA is rejected by changed intake epoch. Close/ticket obligations do not
silently clear. Output poison confirms no partial data accepted on failure.
Hooks are deterministic RAM interleavings, NOT actual thread/SMP execution.
Worker hasn't claimed or forwarded yet when initial intake window is tested.

Important negative proof, deliberately tested
Action AFTER final intake check can occur before helper returns: successful
historical output does not reserve state. Source-only close AFTER last Source
validation is likewise not caught by subsequent intake check when the writer
bypasses intake. Tests explicitly EXPECT these historical successes, confirm
next observation blocks, and do NOT call any hardware/backend on success.
Thus the sample is NOT a global atomic snapshot or a readiness/availability
proof. Future ALL writers must participate in composite admission protocol,
and future backend needs a reviewed held permit across its operation. Adding
more historical checks cannot fix this post-validation window by itself.

Bridge/retirement matrix
72 tuples: lower immediate/delayed, worker early/delayed, independent lower
return SUCCESS/PENDING/error, final SUCCESS/error, normal/Close/overlap.
DISPATCH admit -> PASSIVE metadata Claim -> frozen PASSIVE bridge dispatch.
No actual initial OS dispatch worker or IRP queue added. Intake remains claimed
until caller has independently established ALL simulated returns and bridge
known DONE/zero tags/request/work/IRP + Source ticket cleanup. Source may
restore D0 in model before intake retires: composed sample still refuses it.
Lower IoCallDriver return is not final status; actual captured completion is
used to retire. Known stale cleanup zeros original ticket after close without
reopening the guard. Two bridge remove-lock tags preserve early-worker timing.

ModelRetire is TEST ONLY, not a production teardown API. allReturnsKnown is a
fixture witness from externally-owned serial execution, NOT a boolean a real
caller may assert or infer from Inspect/HeldTags. Existing Root/code/module
external ALL-calls/returns anchor still required, also failed/quarantined calls.
Raw Source pending fields read only by exclusive single-thread test witness;
not a new production field-inspection permission or source/IRP lifetime proof.

Setup and quarantine
Five pre-forward failures: each remove-lock acquisition, work allocation,
completion registration and source admission after close. Taken/one completion
and known cleanup verified before metadata retirement. Three quarantines:
nonfinal PENDING, duplicate callback, tampered Source ticket. No forced retire,
free, cancel, re-open or release unknown obligation; claimed guard stays blocked.
Discarding a mock world is not kernel cleanup. Native-disabled and mismatched
pair cases refuse output and leave no acquired Source lease behind.

Verification
verify-offline.bat <absolute-python.exe>, VS2022 x64/WDK26100.
96 stages /O2 + /Od /W4 /WX /GS:32 RAM EXE +16 predecessor WDK OBJ /c.
Six new files and125 predecessor files hashed; no candidate WDK object because
there is NO new production component here. WDK macro exclusions and symbol
checks cover all eight frozen units, old closed completion /Od forward import
only allowed exception. No link/sign/install, MMIO/PCI/VRAM/DMA/hardware.
No OS IRPs, queued work, true RemoveLock/rundown/module/pool/SMP proof.
Build22 stays installed unchanged; W2P DISARMED/VRAM+GART+MC proof absent.

Next gate
Review held composite permit and participation of EVERY writer, with close/
admission/operation lifetime and no waits at DISPATCH. Separately classify
real power/PnP dependencies and cancel-safe ownership before actual initial
deferral wrapper. No universal FIFO inferred; Source alone remains insufficient.
Only after those gates evaluate a new isolated DISARMED diagnostic package.
Source-only development, no main/release/PR/binaries/private evidence.
