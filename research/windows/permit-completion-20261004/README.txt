Permit / deferred completion composition fixtures -- 2026-10-04
SPDX-License-Identifier: Apache-2.0

RAM TEST ONLY. Frozen permit, Source and completion bridge included unchanged.
Fake platform derived from frozen completion test; new observers/wrappers are
TEST instrumentation, NOT exported native APIs or production wrappers.
All roots/context/module/code/fake objects/IRPs retained through ALL attempts
and returns by an external serial fixture. No actual OS lifetime mechanism.
Native policies FALSE; Build22 unchanged; no SYS/INF/CAT/sign/install/reboot.

Protocol in this test world
Source initialized/readied exclusively BEFORE any gate publication. One gate
revision then represents that trusted tuple. Permit writer admitted <=DISPATCH;
Claim PASSIVE waits by returning BUSY (no loop) while reader pins/steps exist.
Bridge Dispatch called ONLY after Claim, holds that writer through SourceBegin,
lower completion, SourceComplete and any SourceClose fault/setup/close path.
No reader can enter while that claimed writer exists. Reader releases Source
lease before last permit pin: bridge cannot begin between those releases.
Bridge registration/finalization code, tag semantics and native FALSE unchanged.

Observe fake forward/queue/callback/worker/complete/free/tag-release boundaries.
Keep writer and gate unpublished even when Source request has already cleaned,
SourceReady true, bridge says DONE or all remove tags have reached zero.
In particular worker may complete before IoCallDriver/queue/callback return.
ModelRetire requires EXTERNAL serial-fixture ALL-returns witness AND dispatch
returned, no active instrumented depths, bridge DONE, no held tags/request/
IRP/work, Source request zero/no pending, fake work freed and lock count zero.
Witness FALSE alone rejects early retirement; deliberately incorrect TRUE
witness with pending bridge still rejected. Actual global all-attempts/returns
lifetime anchoring is NOT implemented by these depth counters or snapshots.
Known no-ownership Taken FALSE path retains original caller-owned fake IRP,
never invents completion/free. Known pre-forward failure cannot republish.
Only forwarded exact final SUCCESS + SourceStatus SUCCESS + SourceReady may
publish revision2 after known cleanup/returns. Lower dispatch return independent
from final status: SUCCESS/PENDING/CANCELLED return values do not decide publish.
Closed/fault gate never reopens, original writer known cleanup allowed.
Fault/unknown/nonfinal callback/request retains Source/bridge/work/tag/permit
obligations in quarantine. Closed gate with Source root still open likewise
does NOT grant a new SourceClose mutation or root free; refs retained.

Limits
Only POWER D0 bridge transaction tested in new composition; frozen regression
includes START/parser and other Source transitions. ALL actual driver writers,
OS initial deferral/queue/IRP cancellation, dependency classification, remove
lock waits, producer provider/module lifetime and SMP still not integrated.
Cancel status simulated, NOT IoCancelIrp or cancel-safe queue implementation.
Observers inspect resident fake state only. No pointer/context/IRP access from
production code after ownership boundary; TEST observers externally anchored.
Returned sampled tuple is historical after release; bounded metadata use only.
Metadata pins do not prevent physical surprise removal or authorize GPU I/O.
No mapping, PCI/MMIO/ports, VRAM ownership, MC/GART translation, DMA or firmware.
No forced quarantine recovery/timeout/reinitialization or real teardown coordinator.

Verification
72 timing tuples:2 lower inline/delayed x2 worker inline/delayed x3 lower
dispatch returns x2 final SUCCESS/CANCELLED x3 normal/logical-close/overlap.
One reader-drain case,6 setup/policy failures,4 unknown/quarantine cases.
Boundary witnesses include Source-clean-before-completion, DONE-before-free,
zero-tags-before-worker-return; never sufficient for early retirement.
116 stages /O2+/Od /W4 /WX /GS:40 RAM EXE +18 frozen native-FALSE WDK OBJ /c.
6 candidate inputs,146 frozen predecessor files hashed before/after acceptance.
Only frozen completion-Od allowed IofCallDriver import, never executed natively.
No new native unit or installed driver package. All prior regression suites run.

Next gate
Design an explicit root/context publication+ALL-attempts/all-returns lifetime
protocol before native adapter consideration; zero tags/SourceDrain alone are
not a reclamation proof. Initial dispatch deferral, stack PnP/power dependency
and cancellation policy must be specified, not inferred from one-writer BUSY.
W2P physical ownership/reservation and GPU/MC/GART proof remain separate gates.
Source-only development; no main/release/PR or binaries/private evidence upload.
