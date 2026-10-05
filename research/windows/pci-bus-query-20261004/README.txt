OS bus QUERY_INTERFACE candidate: native FALSE, NOT installable

One-shot IoBuildSynchronousFsdRequest(IRP_MJ_PNP) + GUID_BUS_INTERFACE_STANDARD
V1 query, through the caller's already-referenced next-lower device. The OS
owns/frees this IRP. No manual IoFreeIrp/IoCompleteRequest/custom completion.
Never touch IRP or its stack after IoCallDriver (can already be freed inline).
Always wait on the stable session event, final IO_STATUS_BLOCK is authoritative
even when the dispatch return differs. No alertable wait/timeouts/thread exit;
byte-bounded read does NOT bound query time or cancel an outstanding request.

Query success provides one interface ref. The frozen 64-byte reader uses it;
cleanup dereferences once under external retained roots and checks for STOP
during cleanup before returning historical data. No extra InterfaceReference.
Known malformed success with dereference callback is cleaned; failed query
with nonzero bus, missing cleanup callback or unexpected wait/finality retains
UNKNOWN session/caller roots. No retry/reset/free/recovery for unknown ownership.
Event/status/interface are persistent Session members, never ephemeral locals.

Caller must supply PASSIVE APC-enabled context, protected lookup of Lower,
FDO/child/module bootstrap through ALL attempts/returns, actual operation
admission and stable trusted nonaliasing storage. Before/After are checks NOT
atomic reservations/physical availability. NO real dispatch/admission linking
or bus interface acquired here: DDIs execute only through serial RAM fakes.
Do not call on own FDO/top-of-stack: avoid recursive query forwarding/deadlock.
Don't hold publication/spin locks or require the issuer's APC to complete.

RAM tests: inline/delayed completion, dispatch/final status disagreement,
no pointer use after fake OS IRP poison, allocation/guard/IRQL failures,
malformed bus, count/STOP/cancel cleanup, unknown final status/wait violation.
Unknown fixtures abandoned under external anchors, NOT recovered/freed.
WDK /c checks native prototypes; /O2 returns NOT_SUPPORTED, /Od may retain
unreachable IofCallDriver import (allowed only alongside frozen completion-Od).

No SYS/CAT/INF/sign/install/reboot, MMIO/BAR/VRAM/firmware/GART/SDMA/DMA.
No Linux recapture required. Build22/Build11/main unchanged. PRE Code Reviewer
before each build, POST before publishing; verify-offline.bat absolute Python
argument, VS2022 x64/WDK26100. Frozen regressions and source hashes retained.
Next: compose actual protected lookup and operation admission with this query
before a separately reviewed signed diagnostic package and hardware test.
