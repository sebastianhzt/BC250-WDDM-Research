Coherent PnP source producer candidate -- 2026-10-04
SPDX-License-Identifier: Apache-2.0

STATUS: isolated CPU-only candidate. Native execution FALSE. No driver package.
Build22 and all predecessors remain frozen. No hardware/PCI/IOCTL/MMIO/DMA.

Purpose and implementation boundary
-----------------------------------
New source owns one coherent CaptureInput under one KSPIN_LOCK. ALL tuple
writers use that domain; it generates epoch/ID updates rather than merging
independent Build22 queries. The output is shaped like the frozen v2/360-byte
resource ABI and explicitly tagged build22 for the offline adapter, NOT a new
installed build number. A bounded typed START packet retains UINT32 flags,
checks ranges/overlap/ordinals/reserved/tail bytes before encoding the wire.
The OS CM_RESOURCE_LIST parser and real dispatch hooks are NOT implemented.
Init starts with interlocks UNKNOWN/unsafe (0); a separate trusted observation
Interlocks(1) is required. This test input is NOT a measured registry value.
Callers must supply packet from such a future parser while lists are valid.

The source candidate uses WDK object-reference/rundown declarations, but only
fake DDIs execute in RAM tests. It is not a coherent producer in the installed
Build22. No DeviceObject/IRP modification, forwarding, scheduling, completion
registration or freeing FDO storage happens in this source.

Transition table (model, NOT a complete Windows IRP policy)
---------------------------------------------------------
START Begin: only NotStarted/Stopped; clears resources, power UNKNOWN, epoch++.
START final success: validate packet, set Started/resources; power stays UNKNOWN.
START failure: NotStarted/no resources. Invalid successful packet: Fault.
QUERY_STOP/REMOVE Begin: only Started; StopPending/RemovePending, epoch++.
Query success: remains pending, matching cancel becomes admissible.
Query error (severity ERROR): restore pre-query state; old lease still stale.
CANCEL_STOP/REMOVE Begin: only matching successful pending query.
Cancel success: Started; cancel error remains blocked (no IRP status policy).
POWER Begin: only Started; target D0..D3, current power UNKNOWN before forward.
Power success: adopts target; error stays UNKNOWN, never assumes D0 rollback.
STOP: only Started/StopPending; Stopped, clear resources/power, epoch++.
SURPRISE/REMOVE: terminal, invalidate resources/power and close admission.
Interlocks observation: epoch++ even if unchanged, old leases invalidated.
Close: terminal; never discards pending cleanup obligations or leases.
Completion STATUS_PENDING is NOT final: retain request/rundown, return BUSY.
Other informational or WARNING final statuses fault, never make source Ready.
Stale final completion: known cleanup only; no state restoration/publication.
Any invalid event/overlapping request faults admission; original ticket retained.
Fault is permanent quarantine, never reset/reopen; known cleanup still allowed.

Requests, refs and teardown
---------------------------
Root must be externally anchored BEFORE entry through ALL returns, even failed
entrants and teardown coordinators. This does NOT solve root pointer publication.
Init exclusive/unpublished validates everything before two VOID object refs;
there is no fallible operation after those refs, so partial-success rollback is
not applicable. Caller already holds lifetime anchors on PDO and lower object.
Exactly two refs acquired even if PDO==Lower; dereference Lower then PDO exactly
once after known readers/writers/pending requests drained, outside source lock.
Object refs do not anchor FDO extension storage/remove locks, physical PCI
availability, device power, mapped memory, DMA or GPU quiescence.

Begin holds one rundown reference until FINAL matching Complete returns its
known obligation; exact ticket address/id/scope/generation/epoch/kind/target.
No copied or tampered ticket can release another request. Terminal stale Complete
zeros its known ticket then releases rundown outside lock. Nonfinal PENDING
keeps it. Different completion thread allowed under external IRP/context anchor.
Only ONE pending operation total: the real OS permits overlaps; a future bridge
must serialize/defer them or invalidate safely, not forward an untracked request.
This restrictive model is NOT a plug-in replacement for the current dispatcher.

Readers acquire rundown and an exact-address/same-thread lease, max16 slots.
Each slot independently seals acquisition generation/epoch. Replacing a stale
lease payload with Current cannot refresh that seal or validate the old lease.
Same registered Address is rejected under lock. One API at a time per token
storage for its ENTIRE lifetime across Roots, including failed calls. No shared
output buffers or caller zeroing/recycling live tokens outside hostile fixtures.
Validate compares the full current input under lock, copies SOURCE-owned bytes.
Wrong-thread/copied/tampered identity fails without releasing refs. Payload
changes invalidate validation but known trusted identity can still release.
No thread may exit with a live lease. Lease/storage stable until Release return.
Snapshots/epoch checks are historical, not atomic reservations against hardware.
Future backend must protect/revalidate actual source at every stage and close
old publisher bindings. No cache/publisher bridge implementation in this step;
one RAM scenario passes a validated source snapshot into frozen CaptureCommit
outside source lock, then proves cancel ABA invalidates the old source lease.

Begin/Complete/reader/Interlocks APIs PASSIVE only. Transition/Close <=DISPATCH
do bounded metadata updates only. Real power completions may arrive DISPATCH:
reviewed deferral/context ownership is required but NOT implemented here.
References/rundown acquire/release/wait and object refs/derefs outside spinlock;
no callback, wait or allocation under spinlock. Prepare/pass immutable buffers.
Lock hierarchy: external lifetime publication anchor -> source short spinlock;
never call Capture/Publisher/gate/PCI/IRP forwarding while holding source lock.
Drain EXTERNAL PASSIVE coordinator after unpublication, no own in-flight attempt;
BUSY while pending/any lease registered, no blocking on its own references.
Then ExWaitForRundown outside lock handles already in-flight acquire attempts.
Source storage still cannot be freed until external ALL-calls anchor drains.
Fault deliberately retains both object refs forever, even after known cleanup;
do not force free guessed references. This is quarantine, not a recovery path.
Generation external positive <0x7fffffff, unique lifetime ScopeId nonreused;
epoch and 64-bit IDs bounded/no wrap. Counter exhaustion faults permanently.

Verification
------------
verify-offline.bat <absolute-python.exe>, VS2022 Community x64 + WDK26100.
Hidden CL/_CL_/LINK flags refused; /W4 /WX /GS with /O2 and /Od.
68 stages: 22 RAM EXE + 12 standalone WDK OBJ, NO SYS linking/signing/install.
96 predecessor files and10 candidate sources frozen by SHA-256 throughout.
Source tests cover42 state/event fixtures, final/nonfinal/warning/errors,
query/cancel and power ABA, overlap/stale close/completion, pending teardown,
16leases, copied/tampered/crossroot/wrongthread tokens, counter exhaustion,
same-object2refs and Lower->PDO balanced release; faults intentionally retain.
Ten native-closed APIs deny all calls; no platform/reference calls in that test.
Frozen capture/ABI/publisher/admission/PCI/gate regressions also run both profiles.
Fake spinlocks/rundown only check bounded logical discipline; no true SMP,
asynchronous IRP race, real OS reference balance or DISPATCH deferral proof.
RESULT.json lists exact sources/artifacts and all absent capabilities explicitly.

Next gate
---------
Review actual IRP dispatch/RemoveLock and completion deferral design before
integration. Root publication must cover all APIs and queued contexts. Source
must handle real OS overlap without losing tracking. Only then consider another
DISARMED diagnostic package. W2P still needs actual VRAM/GART/MC ownership proof;
no active engines authorized by this source. Source-only development branch,
no main/release. Attribution of predecessors preserved; new code local research.
