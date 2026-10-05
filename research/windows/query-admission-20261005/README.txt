Query-compatible one-shot admission candidate, native FALSE

New independently resident domain/ticket with exact-address independent seal.
Enter/Check require PASSIVE and normal kernel APCs enabled. Only short metadata
spinlock, restored before return; no wait, callback, mutex or critical-region
retention through OS query/read/dereference. Native policy FALSE before pointers
and DDIs, without an activation switch. Six APIs compiled /c only, no driver link.

One successful Enter ever per domain. Reentry rejects BEFORE resetting any live
lookup/query storage. Close <=DISPATCH adds one monotonic reason; existing work
may still be inside the provider. Check is historical, not atomic availability.
Known Leave follows all body/query/provider cleanup; no reopen/reset/drain/free.
Query UNKNOWN quarantines and retains ticket plus original lookup/Env/Life/Call
and ALL issuer/module/storage anchors. Unknown Env receipt also keeps admission,
but known completed query/Call/lookup cleanup may unwind under remaining Env.
Ticket copy/tampering cannot authorize release. Domain/ticket APIs do NOT supply
parent/OS/module lifetime. Counts or successful Leave never permit destruction.

RAM composition uses pinned exact Build22 lookup bodies and frozen query/reader,
fake DDIs and serial STOP/REMOVE close hooks. Hooks close domain BEFORE metadata
transition, not actual driver dispatch participants. Frozen lookup's point7
inside publication lock remains covered by frozen suite only, not the new hook
matrix; no nested publication/spin lock is introduced for that artificial hook.
Returned output now suppressed on failed cleanup too; historical-captures counter
still includes a read made BEFORE tampered-receipt cleanup fails, not success.
Synchronous queries remain indefinite, cannot be aborted/cancelled by Close.
Scope/Generation/Epoch are trusted incarnation IDs, not proof of physical state.

Not implemented: real PnP/power participants, OS query forwarding, domain lookup
publication/rundown, issuer/module lifetime, restart/new incarnation retirement,
SMP scheduling, liveness, hardware access, VRAM ownership or DMA. No SYS/INF/CAT,
signature/install/registry change. Build22 installed and Build11 preserved.

Run verify-offline.bat with quoted absolute Python only after exact Code Reviewer
PRE for EVERY build; independent POST before publishing source-only development.
Next: examine real Build22 closure/execution paths and root lifetime before an
isolated signed candidate. First hardware scope remains OS PCI config only.
