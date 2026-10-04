CPU research export - 2026-10-03

This branch inherits Keshas HEAD 9c657a11de52597b1aca66b8ea7f131c5ad41f00.
The inherited driver is NOT made passive by this export. Its build, install,
registry and hardware tools are not part of these tests. Do not run them to
exercise these models. No production source or INF is changed here.

These original/source-derived models run in ordinary user-process RAM. They
are not kernel consumers, GPU page tables, a Windows VRAM reservation, real
DMA backing, IOMMU support, a GPU scheduler or validation of hardware access.
They do not make W2P complete. The public branch is not the stable build 11
checkpoint, and is not an installable driver update.

Run from an x64 Native Tools Command Prompt for VS 2022:
  python research\offline-20261003\run-cpu-tests.py
Python 3.10+ and MSVC cl.exe must already be available. WDK, administrator
rights, a device handle and driver installation are not required.
Do not run build.bat, test-all.bat or any inherited GPU probe for this suite.

The runner audits the exact export allowlist, confirms inherited tracked
files are unchanged, compiles C tests with /W4 /WX /O2 and runs only its
locally built user-mode executables. Every child return code is checked.
Output is generated under ignored output/offline-cpu-<unique>/; logs are
local artifacts and must not be published. The runner does not commit/push.

Coverage includes catalog constants, selectors, GART geometry, the bounded
numeric VM subset, DMA page-list encoding, the CPU metadata backend,
transactional unmap and numeric backing-reference lifetimes in RAM.
The unmap test also includes its predecessor backend tests as regression.
PM4 bounds and GART backing headers are preserved as research dependencies;
this suite does not claim standalone coverage for those two headers.
Privacy checks recognize selected personal-path and credential patterns;
they are not a guarantee that arbitrary sensitive text can be detected.
Historic laboratory hashes/logs/PASS reports are not new acceptance results.
Only a successful new run in this checkout supplies its own test evidence.

The original publication manifest is explicit. Private captures, BIOS/VBIOS,
firmware blobs, binaries, certificates, signing keys, laboratory snapshots
and personal paths are not exported. Inherited upstream artifacts/history
remain inherited: this export neither audits nor relicenses all upstream.
LICENSE and existing attribution stay intact. No D-Ogi PolyForm implementation
is copied; its DMA design was used as an architectural reference only.
See provenance.json and the numeric source manifests for source identities.

Prepared locally only. Publication requires separate user approval and a
review of the exact final diff. No commit or push is performed by the suite.
