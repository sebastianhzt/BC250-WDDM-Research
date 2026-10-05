Stable-parent child context lifetime candidate -- 2026-10-04
SPDX-License-Identifier: Apache-2.0

Purpose
Replace a free-standing ALL-returns boolean for CHILD PAYLOAD retention with
registered call/continuation tokens and structured body-return boundaries.
Native FALSE. New candidate compiled RAM tests and standalone WDK /c only.
Build22 unchanged; no installed driver link/SYS/INF/CAT/sign/install/reboot.
Previous completion composition remains FROZEN and still uses fixture witness:
this new primitive is not yet wired into its real dispatch/callback/worker.

Stable Parent is NOT magically self-protected
Domain is a SEPARATE resident stable parent registry, NOT in child allocation.
Parent/module/code/tokens/envelopes/output storage must outlive EVERY attempt
and ALL returns (including failures and final Release/Detach). OS authority for
that lifetime is NOT implemented. A future FDO/module/unload protocol required.
Tokens never stored inside payload being detached; cross-thread handoff only
under independent caller OS/token ownership. Inputs kernel-trusted immutable,
stable aligned nonaliasing. No arbitrary pointers from users/IOCTL.
This moves child retention to a stable parent, does not eliminate that axiom.

Protocol
Publish opaque child pointer under parent lock, never dereference child in APIs.
Enter looks up by key (not naked child pointer) before any child access and
registers exact independently sealed call token. Hold reserves known future
callback/work obligations BEFORE registration/forward. EnterHeld after closing
allows ONLY known continuation cleanup, not new work or hardware permission.
Derived calls increment hold.Users; ReleaseHold BUSY until derived calls exit.
Close unpublishes: new key lookups/Holds denied, existing calls/holds retained.
Structured wrapper performs body, error handling and final output copy while
call token held; Exit invoked AFTER all child accesses/body returns.
Exit drops final child retention under parent lock, then accesses Parent only.
Detach PASSIVE BUSY with any call/hold. After Close and known drained slots it
transfers opaque pointer; caller can reclaim child once Detach returns, even
if a different parent-only Exit helper has not returned. Parent/token/code stay
alive independently. Test poisons fake child in that exact helper-return window.
Unknown/copied/cross-domain/tampered tokens rejected, original slot retained;
quarantine no guessed repair/free. Counters bounded/capacity16. Fresh key AND
generation strictly increase after detach; no ABA identity reuse or root reset.
No lock held across body, no nested locks/waits/callbacks/payload deref under it.

Limits/negative cases
Early caller Exit while body still active CAN defeat any such reference protocol;
explicit negative fixture demonstrates this without native free/UAF. Caller
wrapper boundary must be trusted and audited; counter cannot infer call stack.
Registry references do NOT serialize Source data or prove readiness/ownership.
No actual memory allocation, references, OS IRP queue/cancel/RemoveLock drain,
producer provider, module/FDO refs, initial deferral, real teardown or SMP tested.
Known asynchronous holds are METADATA reservations, not callback authentication,
registration/queue/cancel guarantee or GPU mapping/DMA retention/physical presence.
Faulted/unknown obligations retained indefinitely; no timeout/recovery invented.
This is not a replacement for Windows rundown APIs or their IRQL/thread rules.

Tests
Structured success/error/close/rejected lookup paths; no child read before grant.
Child poison/detach before final Exit helper return, Parent still stable.
Four callback/worker/dispatch return orderings with held cleanup after close;
no early hold release, no lookup after unpublish, no detach with future holds.
1000 renewal cycles x8holds+8calls, capacity16, counter/key/generation nonreuse.
Copy/cross-domain/tamper/zeroed token unknown quarantine; IRQL/policy/counter
exhaustion; wrong early caller drop negative fixture. Ten APIs native FALSE
in blocked test with ZERO platform calls and zero outputs.
128 stages /O2+/Od /W4 /WX /GS:44 RAM EXE +20 native-FALSE WDK OBJ /c.
9 candidate files and152 predecessor files hashed before/after; all regressions.
Native /U18 mock macros; only frozen completion-Od forward import exception.
No physical PCI/MMIO/VRAM/ports/firmware/rings/DMA or acceleration activation.

Next gate
Compose this stable-parent/token protocol with permit/Source/bridge and prove
exact handoff/release for dispatch, callback, queued worker and failures; then
design actual parent/module/envelope anchors and OS cancellation/dependency
policy. Do not replace fixture witness merely with Inspect counts or a boolean.
Only separately reviewed native-FALSE adapter before future DISARMED package.
W2P physical VRAM ownership/reservation, GPU/MC/GART translation remain separate.
Source-only development; no main/release/PR/binaries/private evidence publication.
