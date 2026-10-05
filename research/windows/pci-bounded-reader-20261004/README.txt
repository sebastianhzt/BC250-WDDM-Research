Bounded PCI identity reader candidate, native FALSE, NOT installable

One fixed BUS_INTERFACE_STANDARD.GetBusData call: CONFIG, offset 0, 64 bytes.
No arbitrary offset/length, ROM, SetBusData, BAR sizing, MMIO or DMA callbacks.
Size/version/reference/dereference/read callbacks validated before the call.
Exact byte count and AMD 1002:13fe display/type0 identity required. All enabled
errors zero the complete result; native FALSE touches no pointers or DDIs.
The trusted provider must itself obey buffer length: returned-count validation
does NOT protect against an OS/provider bug that writes beyond that buffer.

The caller supplies an ALREADY referenced interface and independent operation
admission, context/storage/code retention across all attempts and returns.
The routine does not acquire/query/ref/deref the bus or implement real PnP.
Before/After checks are not an atomic reserve and cannot prevent physical
surprise removal. STOP/cancel observed before/after reject output, but do NOT
interrupt an entered synchronous callback. Byte bound is NOT a time bound.
Missing QUERY_INTERFACE and real operation admission remain installation gates.

RAM tests exhaust counts 0..66/max, identity/malformed ABI/callbacks/IRQL,
before/after denial and invalid positive statuses, canaries and output hygiene.
The exact Build22 lookup test is composed with the reader under a Life call:
STOP/REMOVE before read and inside fake GetBusData, known cleanup and unknown
envelope retention. The bus is fabricated, NOT queried from Windows. Closure
participation remains proposed serial fixture code, NOT Build22 integration.
Reference/type extraction and all frozen regressions remain hash-pinned.

Run verify-offline.bat with the absolute Python executable as quoted argument
ONLY after Code Reviewer PRE approval. Requires VS2022 x64 and WDK26100.
Outputs in a fresh ignored directory. No SYS/CAT/INF/sign/install/reboot.
No Linux recapture. Keep installed Build22, Build11 rollback and main unchanged.
Next: OS interface acquisition/known-reference unwind and real read admission,
then a separately reviewed signed DISARMED package. MMIO/SDMA stay blocked.
