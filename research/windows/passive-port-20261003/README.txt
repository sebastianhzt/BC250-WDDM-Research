PASSIVE SOURCE PORT / OFFLINE ACCEPTANCE ONLY - 2026-10-03

This successor audit is based on ef3332ef75ed368d2d9984990f55bfd00bff3c74.
The historical research/offline-20261003 CPU auditor/runner is unchanged.
The inherited driver is NOT certified passive as a whole and is NOT INSTALLABLE
from this stage. DriverEntry and legacy resource ownership still need a separate
review. Do not run build.bat, install a package, load an SYS, or enable registers.
Inherited blockers include DriverEntry deleting a device found by name and the
unload handler being set only in StartDevice, which this stage now rejects.
Even loading this stage is forbidden. A future standalone passive control
service would need its own device names/admin ACL, metadata-only IOCTLs and
CPU-only cleanup/rollback review; it is not implemented in this stage.

Scope: bounded firmware file reads, unsigned PM4 header construction, bounded
CPU packet writes, removal of four unowned SMU DMA whitelist messages, and 77
first-action barriers across firmware/VM/hardware/power/KMD entry points.
The shared source policy is zero and compiler overrides are forbidden.
No firmware blobs are added/replaced. Navi12 names are inherited, not validated.
No W2P VRAM ownership, real DMA addresses, GART translation, SDMA, Vulkan or
display functionality is claimed. CPU models are not integrated into the kernel.

After independent code review, use Windows x64 Native Tools Prompt for VS 2022:
  python -B research\windows\passive-port-20261003\verify-offline.py
Optional environment: BC250_WDK_ROOT, BC250_WDK_VERSION (default 10.0.26100.0).
No administrator privilege is needed. WDK headers are required only for /c.

The runner uses the new audit, regenerates the unchanged source catalog, runs
nine original RAM-only model tests, mechanically extracts the current firmware
reader/three PM4 writers/four packet macros/MAX_FW_SIZE into RAM fake-API tests,
tests the real zero policy and rejects an explicit compiler override, and compiles
all thirteen KMD translation units from build.bat with /c only. No driver linking,
signing, packaging, installation, registry writes or hardware handles occur.
The firmware fixture is derived from the lab's upstream-integration test; its
MAX_FW_SIZE is now extracted from production rather than duplicated in the test.

Acceptance requires a NEW successful run and its local output/RESULT.json.
All commands, exit codes, stdout/stderr, audit inputs and artifact hashes are
recorded under a unique output/passive-port-* directory; no binary/log export.
A failure stops subsequent stages and cannot produce RESULT.json. The expected
policy-override compile failure is separately recorded and must contain the
policy's error text; it is not silently treated as an ordinary successful build.

New source provenance: Apache-2.0 as the existing project; upstream production
copyrights remain intact. Existing inherited blobs/unsafe scripts are not copied,
relicensed or sanitized by this stage. This is source-only progress, not a kernel
runtime validation, security proof or permission to replace the stable build 11.
