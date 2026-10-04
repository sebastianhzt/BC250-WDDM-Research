BUILD 21 -- ISOLATED WDM DIAGNOSTIC CANDIDATE -- 2026-10-04
=======================================================

STATUS
Source prepared for independent PREBUILD review. No installed-driver change.
The complete accelerated KMD/UMD is NOT packaged by this step. This is not a
WDDM miniport, display renderer, Vulkan driver or an accelerated successor.
Binding the Display device to this candidate can remove its current display
driver. Screen continuity cannot be proven offline. DO NOT INSTALL yet.
Build 11 and the private laboratory are preserved. Root build.bat is unsafe
for this workflow because it automatically installs: do not run it.

DESIGN
Only two C translation units are linked: a new original minimal WDM entry/
dispatcher and the selected existing local PnP module. The latter captures
START_DEVICE raw/translated resource pairs, clears them on STOP/SURPRISE/
REMOVE, and holds remove locks/references. No resources are mapped, no GPU
registers read, no firmware loaded and no DMA allocation/job is submitted.
PCI_CONFIG returns NOT_SUPPORTED with zero header bytes. The experimental
PCI read prototype is removed, not merely left waiting behind a runtime flag.
All four query payloads preserve DISARMED/no-memory-owner/no-DMA information.

Both named endpoints use separate private class GUIDs, administrator/SYSTEM
default SDDL and FILE_DEVICE_SECURE_OPEN. Nonempty CREATE paths are rejected.
Effective ACLs may be overridden by registry settings and require live checks.
Normal endpoint: PNP (128-byte ABI v1), W2P (96-byte ABI v1).
Research endpoint: resources (360-byte ABI v2), disabled PCI (96-byte ABI v1).
No other IOCTL is admitted, including any old GET/READ/WRITE/init/SDMA/PM4.
Every major-function route classifies objects before reading an extension:
zero-size control endpoints never enter the PnP extension parser. FDO identity
comparison is atomic and dereference-free, also at DISPATCH_LEVEL; it is NOT
an object reference. Incoming OS device/IRP lifetime and remove locks remain
required. Completion registration uses IoSetCompletionRoutineEx, checks its
failure and forwards after every successful registration.

Entry rollback deletes only objects/links created by this driver instance.
It does not overwrite a device extension or allocate shared GPU state. Both
endpoints are initialized before clearing DO_DEVICE_INITIALIZING. The build
marker is written last; it does not replace a runtime ABI/SYS hash check.
The minimal INF copies only the SYS and sets seven service-local interlocks
to zero. No firmware, UMD, fabricated acceleration capability, MSFTProxy,
global VBS/HVCI/TDR override or security downgrade is included.

WHAT IS NOT ENABLED
All previous source models, bounded loaders/PM4 helpers and register catalogs
remain preserved in development. They are not silently connected to hardware
by this diagnostic package. There is no new Windows VRAM owner, valid GART,
GPU/MC translation, verified firmware execution or GPU stop/fence protocol.
The RAM owner/router model from the previous step is not a WDM lifetime proof.

OFFLINE VERIFICATION (only after independent PREBUILD approval)
From an x64 VS2022 Native Tools prompt:
  python -B research\windows\diagnostic-package-20261004\verify-build.py
Or use verify-build.bat with one argument: absolute path to python.exe.
No administrator rights are needed for compilation. Each run creates a new
output/diagnostic-build21-* folder and records commands, return codes, stdout,
stderr, source/artefact/compiler hashes. Failure cannot produce RESULT.json.
KMD and tools compile /W4 /WX. The two kernel units are fully linked into an
UNSIGNED SYS. Explicit /GS and GsDriverEntry/BufferOverflowFastFailK preserve
the WDK security-cookie entry wrapper. dumpbin records imports, PE headers and
load config; the link map permits independent entry/cookie review. Known
GPU/DMA/display imports are rejected.
Five preflight EXEs are compiled but NOT run. CPU-only policy tests run /O2
and /Od (524301 checks per profile). A verbatim extracted dispatcher is tested
with fake access/IRQL/metadata APIs (921987 checks per profile); this is NOT
real IRQL, ACL, PnP, callback, cancellation or concurrency validation.
The root sources and private lab remain unchanged. Imported ABI/tool/PnP
baseline hashes and selected candidate hashes are in provenance.json.

BEFORE AN INSTALLATION RECOMMENDATION
Require POSTBUILD review of the exact SYS/imports and recorded stages, a
separate no-auto-install packager, test signature and complete CAT membership,
verified current recovery/Build11 checkpoint, and a supervised screen/PnP/ACL
test plan. No signing/install command is authorized by a compiler PASS alone.
This folder has no installation script. Signed legacy builds 13/14 are not
fallback candidates; retain Build11 as the recovery version.

ATTRIBUTION / PRIMARY REFERENCES
Inherited project: Keshas-dev and contributors, root Apache-2.0 license and
original notices preserved. PnP/ABI query progress is selected from the local
research laboratory; entry/policy/tests/runner/INF are original additions by
Sebastian. D-Ogi and MetalCyan remain research references only: no implementation
or firmware from them is copied or relicensed in this diagnostic subset.
See ../domain-backend-20261003/CREDITS.txt for the existing source boundaries.
https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/using-remove-locks
https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/controlling-device-access
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-iosetcompletionroutineex
