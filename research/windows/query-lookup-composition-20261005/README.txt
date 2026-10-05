Exact Build22 lookup -> Env/Life -> OS-query candidate: RAM ONLY

Seven new test/runner/instruction files. No native component or frozen source
edited, no SYS/CAT/INF/install/hardware. Build22 acquire/release bodies and typedef
are still mechanically extracted from pinned files without rewriting.
RAM bridge reuses reduced Env IRP/status types; extra fake query stack lives in
an observer packet outside that IRP. It does NOT test Windows ABI or scheduling.

Protected lookup keeps its original remove tag while Env takes own Self ref,
second tag and child Hold. Persistent separate Query/Call/receipt/reference storage
keeps every obligation valid through send/wait/read/dereference and ALL returns.
Known cleanup: query interface -> Life Call -> Env Hold/tag/ref -> original lookup.
Query UNKNOWN instead retains ALL of them plus issuer/module bootstrap. Retry
rejects before zeroing the live lookup reference. No forced drain/recovery.
Unknown fixtures abandoned externally anchored, NOT recovered/freed.

Serial STOP/REMOVE hooks after lookup/ref/handoff/body, in build/send/wait/read/
interface dereference. Simulate immediate or delayed IRP poison/completion.
No publication/spin/critical region allowed at query DDIs/wait. Backend reentry
is rejected by a synchronous fixture Busy field, NOT an SMP admission primitive.
Malformed success cleanup, partial failure/unknown final/wait violation, allocation
failure, known-reference unwind, output suppression and retained roots inspected.

IMPORTANT integration finding: frozen Bc250DmaGateEnter keeps a critical region
and mutex throughout the operation. That gate is NOT composed with this blocking
query, whose contract requires kernel APCs enabled. CompositePermit steps likewise
promise bounded metadata work, not an indefinite query wait. Neither is silently
reused or enabled here. Actual operation admission/PnP/power participation remains
unimplemented. State checks are not reservations or physical availability proof.
No real Windows query, thread/module lifetime, SMP, liveness or timeout validated.

Run verify-offline.bat with quoted absolute Python only after exact PRE review;
VS2022 x64/WDK26100. Frozen regressions, source maps and includes retained.
No Linux recapture. Next: query-compatible admission without APC-disable across
wait, participating actual lifecycle and signed isolated diagnostic review.
