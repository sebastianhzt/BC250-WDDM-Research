Permit / Source binding fixtures -- 2026-10-04
SPDX-License-Identifier: Apache-2.0

RAM TEST WORLD ONLY. No new native adapter/export or installed driver change.
Frozen composite-permit and pnp-source reused WITHOUT edits. All native policies
remain FALSE. Build22 and return packages untouched; no SYS/INF/CAT/sign/install.

What changed
Fixture ModelBinding pairs one Source and permit, same scope/generation, and
binds a published revision to an exact trusted Source tuple. Every fixture writer
of that tuple participates: Init exclusive/unpublished, Begin/Complete held
claimed writer, Transition/Interlocks/Close only after successful Claim.
Admit <=DISPATCH revokes NEW reads/steps without changing represented Source
tuple. Claim PASSIVE BUSY while ANY permit pin or active step remains. Reader
first pins permit then Source lease then enters bounded synchronous metadata
step. Validate and use tuple INSIDE that step. Leave, release Source lease,
then release permit LAST; a writer cannot begin between the two releases.
Successful known cleanup publishes only if exact final SUCCESS, Source API
SUCCESS and fixture SourceReady tuple; all other outcomes stay unpublished.
Source fault/closing also closes gate. PENDING retains both pending obligations.
Copied/tampered Source completion cannot retire original writer; gate closed
and original request/pin remains quarantined. No guessed repair/free.

Fixture limits
Static Model* helpers are TEST code, not kernel APIs. SourceReady/Current/private
fields used as serial fixture witnesses, NOT a production lock-safe provider or
ALL-returns proof. All roots/code/fake objects/tokens remain anchored by fixture
through ALL attempts/returns. Same-thread reader, no asynchronous DMA lifetime.
Claimed pending writer owns modeled invalidation plus final completion; close
during that interval may use SourceClose, no readers possible in that interval.
An abandoned/copy-fault/overlap gate may retain an otherwise live Source root
permanently: external quarantine/OS cleanup is NOT supplied or guessed here.
Drain called only with independent serial-fixture ownership of ALL returns,
Source closed and no known leases/pending request. Pins0 snapshot is NOT enough.

Physical removal/power can occur independently of the metadata transaction:
fixture PhysicalPresent drops immediately even while prior tuple step finishes.
It is NOT read by any hardware operation (none exists). Deferred tuple mutation
does NOT defer/prevent actual device disappearance, forward OS IRPs, model actual
hardware resource release or authorize hardware I/O. BUSY does NOT define OS
PnP/power dependency/cancellation/queue policy or permit universal FIFO.
Native Build22 writers do NOT participate. Deliberate bypass negative fixture
calls frozen SourceInterlocks outside permit: gate alone misses that mutation,
Source validation catches stale tuple. ALL actual driver routes remain unbound.
Returned output is historical after permit release; only bounded in-step use
is protected against participating metadata writers. No permission at return.

Tests
60 interleavings:10 writer/argument routes x6 read stages (before Source lease,
before step, after step entry, after validation, after leave, after Source release).
Registered writer intent changes no Source tuple until permit readers drain.
128 publication cycles x8 reader pins/steps; mutation blocked even after each
Source lease releases, revoked steps reject old pin, new publication advances.
Start, stop/restart, query/cancel stop and remove, D0/D3, interlocks, logical
close/surprise/remove, delayed PENDING/final, failure/warning/info, cross-thread
writer completion, copied/tampered ticket quarantine, gate close with reader,
known waiting metadata abandon/ABA, policy deny, invalid START and overlap,
unregistered writer bypass and post-release stale output explicitly exercised.
Frozen Source tests included; full frozen regression matrix /O2 and /Od /W4 /WX.
112 stages:38 RAM EXE +18 frozen native-FALSE WDK OBJ /c, no new native unit.
6 candidate inputs,140 frozen predecessor files hashed before/after acceptance.
Only completion-Od has reviewed IofCallDriver import exception, never executed.

Next gate
Bounded metadata use has a test-world retention protocol now. Next evaluate
how initial deferral/cancellation/worker completion lifetimes compose with it,
including external ALL-calls/ALL-returns roots and real PnP/power dependencies.
Only then consider a separately reviewed native-FALSE adapter before any future
DISARMED diagnostic package. W2P physical VRAM ownership/reservation, GPU/MC/GART
translation, firmware/rings/SDMA/acceleration remain unsolved separate gates.
Source-only development publication; no main/release/PR or binary/private upload.
