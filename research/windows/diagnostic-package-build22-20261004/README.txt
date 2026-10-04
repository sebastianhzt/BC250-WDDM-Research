BUILD 22 -- ISOLATED WDM METADATA CORRECTION -- 2026-10-04
======================================================
STATUS / SCOPE
Diagnostic candidate only; not a WDDM miniport, renderer or Vulkan driver.
No installed-driver change by the build or packager. Build11 and Build21
sources/packages are preserved; Build21 is the currently tested installation.
Binding this Display package can interrupt video. Offline tests/signatures
do not establish screen, PnP, concurrency or kernel-policy acceptance.
Never use root build.bat: it auto-installs an unrelated legacy driver.

CHANGES RELATIVE TO BUILD21
PCI read remains physically absent: STATUS_NOT_SUPPORTED, zero BytesRead,
zero Header, READ_DISABLED and DMA_NOT_AUTHORIZED. The metadata response now
samples PnpState and BindingGeneration with the shared binding lock at
PASSIVE_LEVEL. Null binding is explicitly NOT_STARTED; unsampled/invalid
signature is UNKNOWN (0xFFFFFFFF), not a false zero. Resource generation is
also read within its binding-lock scope. No ABI size/version change.
The PCI tool rejects unknown/mismatched schema/build/state/generation,
nonzero PCI bytes/header, invalid reserved/tail fields and unsafe blockers.
Its former unreachable PCI decoder/BAR parser is removed entirely.
Equal observations mean metadata agreement ONLY. Several PnP state writers
operate outside BindingLock; equal values cannot exclude STOP/START ABA,
an unexported StateEpoch change or all transitions. Not a global snapshot.
No inference from Linux offsets to Windows BAR identity or VRAM ownership.

UNCHANGED SAFETY BOUNDARY
Only minimal entry+selected passive PnP translation units are linked.
Two administrator/SYSTEM endpoints, rejected subpaths, bounded buffers,
four metadata queries, no active GPU commands. No register/PCI reads,
MMIO, memory mapping/allocation, DMA, firmware, rings, scheduler, UMD,
WDDM/display registration or global security/BCD changes. Seven INF
interlocks remain zero. No new ownership/GPU-MC/GART proof is asserted.

OFFLINE CAMPAIGN -- REQUIRE INDEPENDENT PREBUILD REVIEW BEFORE EVERY BUILD
From x64 VS2022 Native Tools, Python3:
  python -B research\windows\diagnostic-package-build22-20261004\verify-build.py
Or verify-build.bat with the absolute python.exe path as its only argument.
Unique output/diagnostic-build22-* directories; never load/install.
23 stages, /W4 /WX /GS, GsDriverEntry+BufferOverflowFastFailK.
dumpbin/imports/headers/loadconfig/link map retained for exact-binary review.
Per /O2 and /Od: 524301 policy/ABI checks, 921987 extracted-dispatch checks
and 383660 extracted production-metadata checks. Windows lock/IRQL/atomic
services are RAM FAKES, not tests of actual synchronization, device lifecycle
or IRQL. Tests cover serial START/query-stop/cancel/STOP/query-remove/cancel/
SURPRISE/DELETE/null/invalid binding/UNKNOWN, lock-acquisition mutations,
schema/state/generation disagreement, buffers/padding and closed PCI headers.
An explicit ABA example demonstrates the limitation rather than hiding it.
Five live diagnostic EXEs are COMPILED ONLY; never run by this campaign.
Source14/snapshot20 pins, preserved Build21 raw19 and earlier15 LF pins
checked before/after. No predecessor or private laboratory edits.

AFTER POSTBUILD
package-candidate.ps1 accepts only reviewed unsigned SYS SHA, exact stages/
artifacts/snapshot/source hashes and an existing current-user signer.
Signs a fresh local copy and CAT, verifies signer and SYS/INF membership.
Never creates/exports keys, changes certificate stores or installs/loads.
Requires POSTSIGN review before manual trial instructions. Keep recovery
packages and validate actual installed SYS+runtime after any user-led reboot.
No production Microsoft signature or acceleration claim.

ATTRIBUTION
Apache-2.0 inherited Keshas-dev project notices retained. Sebastian authored
the isolated entry/policy/INF/runner and metadata correction/fixtures.
D-Ogi and MetalCyan remain research references; no code/firmware from either
is copied into this diagnostic subset. Existing credit boundaries unchanged.
See ../domain-backend-20261003/CREDITS.txt and root LICENSE.
Microsoft primary references:
https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/using-remove-locks
https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/controlling-device-access
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-iosetcompletionroutineex
