PCI/PnP coherent metadata capture model -- 2026-10-04
SPDX-License-Identifier: Apache-2.0

STATUS: CPU-only research, native policy FALSE. No installable driver.
Installed Build22, Build21/Build11 return packages and frozen predecessors
are outside this change. No PCI query, IOCTL, MMIO, GPU/DMA/firmware/ring write.

What this step adds
-------------------
A bounded metadata cache and conversion into the frozen PCI_RESOURCES model.
Input combines the frozen Build22 resource v2 wire bytes with SourceGeneration,
SourceEpoch, power and interlocks supplied by a FUTURE trusted source producer.
One short KSPIN_LOCK protects the complete committed tuple and model Revision.
Read requires Started + D0 + interlocks OFF + valid nonempty resource snapshot.
Validation and conversion compare the exact current frame under that lock.
Conversion copies cache-owned resources, never arbitrary caller resources.
All failed outputs are zero. Commit failure after policy/guard admission faults
the cache permanently, clears the old frame, and prevents reuse. Revision,
source generation and epoch never wrap. Reinit/reset/recovery is unsupported.
This means the cache must be abandoned on faults, not silently repaired.

Source capture gap (observed in frozen Build22 source, not hardware)
------------------------------------------------------------------
source/inc/amdbc250_ioctl.h resource v2 has no StateEpoch or power state.
source/src/kmd/amdbc250_dream_pnp.c includes state/epoch writers outside the
resource snapshot BindingLock. The power dispatcher is passthrough, not a
coherent cached D-state source. Two separate queries or a new cache lock do
NOT fix that. The model assumes a future producer creates one immutable input
under one serialization domain shared by ALL state/resource/power/interlock
writers, while the real PDO/backing lifetime is anchored. No such producer
has been integrated into Build22 here. Flags narrowed to USHORT are range
checked first; the original UINT32 wire fields are retained in the frame.

Identity and lifetime contract
------------------------------
ScopeId is an external unique, nonreused 64-bit cache lifetime ID. Root storage
is anchored before API entry through ALL returns, including failed ones.
Inputs/outputs/code must be resident, aligned, kernel-owned, nonaliasing;
source input immutable during Commit. APIs admit PASSIVE_LEVEL only in mocks.
SourceGeneration is positive and < 0x7fffffff. SourceEpoch is < 0x7fffffff,
zero allowed only when not Started; strictly increases in the same generation.
New source generation can begin a new epoch. Legal IRP transitions, real
reference acquisition, completion and stop/remove/power ordering are NOT
implemented by this metadata adapter. No callbacks/waits occur under its lock.
Power numeric values follow DEVICE_POWER_STATE: 0 unknown (blocks), 1 D0,
2 D1, 3 D2, 4 D3. These are synthetic inputs, not measured device power.
Revision increases for each accepted update. Converted Resources.Generation
and Epoch are BOTH Revision, scoped to this cache. They are synthetic publisher
renewal IDs, not raw PnP generations, real lifetime authority or DMA permission.
Even unchanged-state cancel/power ABA requires a new source epoch/revision.
Read/Validate/Resources are historical observations, not state reservations.
A transition can occur immediately after return. A future bridge must protect
and revalidate actual source lifetime/state throughout interface operations and
close/drain previous publisher bindings. Faulting this cache does not by itself
close a previously prepared publisher, dereference a PDO or cancel an IRP.
There is no bridge from this cache into real Prepare/Publish or device dispatch.

Verification
------------
verify-offline.bat <absolute-python.exe> selects VS2022 Community x64.
verify-offline.py rejects hidden CL/_CL_/LINK flags, freezes source hashes and
85 predecessor files, runs nine suites at /O2 and /Od with /W4 /WX, and compiles
five standalone WDK units at both profiles without linking a SYS. Actual wire
layout is checked against the original Windows header, not fake ABI types.
RAM matrix: 7 PnP states x 5 power inputs x 2 interlock values x 3 snapshots.
Also poison, replay, stop/cancel ABA, power ABA, source epoch restart, cross-root
tokens, tampering, malformed UINT32 flags/ranges/ordinals and counter exhaustion.
The reduced fake spinlock checks logical lock discipline, NOT Windows SMP,
scheduler, real PDO refs or concurrent PnP execution. A split-read counterexample
shows source coherence cannot be inferred from equal BindingGeneration alone;
it is a logical fixture, not a real race replay. Native-closed APIs deny calls.
Expected complete campaign: 56 stages, 18 RAM EXE + 10 WDK OBJ, no warnings.
RESULT.json records exact inputs/artifacts and explicit unproved capabilities.
No executable is intended for hardware diagnostics. Output is private/ignored.

Next gate
---------
Design and review the real single-domain source producer, PDO/storage anchoring,
IRP completion/status ordering and lock hierarchy before driver integration.
Do not claim W2P solved: BAR/VRAM ownership, GART/MC translation, PCI availability
and real lifetime protection are still unproved. Keep active engines DISARMED.
Only source goes to development; no main/release/driver package in this step.
Original predecessor attribution/notices stay intact. This new model is local
research, not code copied from D-Ogi or MetalCyan.
