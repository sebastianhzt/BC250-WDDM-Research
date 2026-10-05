Self-FDO envelope reference adapter -- isolated/native FALSE -- 2026-10-04
SPDX-License-Identifier: Apache-2.0

NO DRIVER/PACKAGE/INSTALL/REBOOT. Build22 SYS unchanged, backups/predecessors
unchanged. No PCI/MMIO/VRAM/GART/firmware/ring/DMA or actual OS async requests.
New candidate compiled WDK /c ONLY, not linked. All four production API entries
return NOT_SUPPORTED BEFORE any root/receipt/platform access, no enable knob.
MOCK permits RAM fake refs/remove tags/frozen Life; NOT actual OS execution.

bc250_envelope_reference.c/h implement an exact-address receipt registry with
eight independent seals, monotonic IDs, separate PREPARING/ACTIVE/RELEASING
phases. Init exclusive zero parent PASSIVE once; Acquire/Release/Close <=D.
Reserve address under registry spinlock, capture Self/RemoveLock/Life -> unlock
-> ObReferenceObject(Self) -> IoAcquireRemoveLock(receipt address) -> LifeHold.
Then publish ACTIVE. No nested Life/registry lock, DDI calls outside spinlock.
Caller supplies valid owner Self FDO, initialized remove lock, published frozen
Life and nonpaged resident independent roots/receipts. Initial lookup/bootstrap
must ALREADY be protected through ALL attempts/returns before first root read;
this adapter cannot reference a stale pointer back to life or validate owner.

Failure unwinds exact acquired resources only: remove-lock failure clears
receipt/slot then Self dereference; Hold failure releases successful remove
tag, erases receipt/slot then dereferences. No output/root/envelope access after
last dereference: only captured local status or constant return. Acquisition
reserved BEFORE Close may finish; Close denies new reservations, no reset.
No object allocation, async publishing, CSQ registration, actual IRP forwarding
or module-code anchor. Object retention is not hardware availability/module pin.

Release validates independent byte seal + exact address BEFORE any cleanup and
claims RELEASING while no external calls run under lock. Copied/tampered/replayed
receipt cannot release resources. Caller uses exact &receipt.Hold only for
LifeEnterHeld/Exit; must not independently release it or mutate/copy receipt.
LifeReleaseHold first: live derived call -> BUSY, preserves ref/tag/Hold for
known retry after body Exit. Other failure retains known obligations and closes
faulted registry. After success child may already detach; cleanup remains
Parent-only. Release remove tag -> erase receipt/slot -> final Self dereference
-> stack-only return under caller's EXTERNAL code/OS return anchor.
Unlike the prior pure queue fixture, this adapter keeps remove tag until Hold
release succeeds, so a BUSY derived child call cannot lose its remove protection.
Receipt storage survives all release helper returns independently of child.

Tests use frozen MOCK Life and fake Ob/remove APIs, serial events, refs and tag
accounting. They cover levels0/2, registry Close and Life Close inside acquisition,
delete-pending remove-lock rejection, exact unwind, derived-call BUSY/retry,
capacity8, copied identities, five tampered fields, same-address old-ID replay,
IRQL3 rejection, disabled policy and counter-exhaustion/no-reset.
Child poisoned INSIDE last Life hold helper while remove tag and Self ref remain;
after tag cleanup fake deletion marks baseline FDO ref gone; receipt and registry
are erased BEFORE last Self dereference poisons Parent/Life/receipts. Only code
return remains. This tests ordering, NOT real object deletion, UAF or OS unload.
Unknown identity cases terminate external test world retaining resources; no
native UNKNOWN remove/unload recovery implemented. No SMP/ABA storage reuse or
adversarial trusted-caller mutation proof. Tokens/code/refs have external roots.

Runner: ALL138 predecessor stages plus two new tests /O2 &/Od (8 stages) and
new native-FALSE unit /c +symbol inspection both profiles (4 stages)=150.
52 RAM EXE +22 WDK OBJ +2 frozen model JSON=76 accepted artifacts.
Nine candidate sources +181 frozen predecessor sources hashed before/after.
/W4 /WX /GS; production mocks /U20, no hidden CL/_CL_/LINK flags. Optimized
envelope unit must not retain object/remove/Life imports. Native /Od may retain
unreachable typed DDI imports but no API executes them (closed-policy RAM test).
Use complete fork checkout + VS2022 x64/WDK10.0.26100.0/Python:
verify-offline.bat <python.exe>; unique ignored output/envelope-reference-*.
AGENTS requires independent PREBUILD then POSTBUILD before source publication.
Only nine sources to development; no main/PR/release/binaries/private captures.

Hardware roadmap -- gates, NOT dates or authorization
Installed Build22 metadata tests are not direct GPU/PCI hardware reads; its PCI
query reports NOT_SUPPORTED/0 bytes. First bounded hardware candidate should
be synchronous PCI identity/config through OS bus interface ONLY, without BAR
probe writes, MmMap, MMIO or DMA. Requires real protected FDO lookup/entry/ref
and completion/cleanup review, coherent START/power/removal admission, exact
returned-byte/error bounds, trusted diagnostic endpoint DACL, signed isolated
package + reviewed rollback/recovery and deliberate manual test selection.
This does NOT require enabling a generic async queue or proving VRAM first;
keep it synchronous/bounded to avoid extending to unsolved async wrappers.
New adapter alone does NOT fulfill these integration gates or supply package.
MMIO reads remain a separate whitelist/availability gate; unknown reads can
hang this BC-250. Active SDMA/DMA additionally needs Windows-owned restorable
memory, validated CPU/GPU/MC/GART translation, firmware semantics, hardware
sequencing/timeouts/recovery. No reliable calendar estimate for active tests.

Next: compose actual protected diagnostic lookup + this adapter in RAM, then
review a separately isolated synchronous OS bus PCI provider/candidate package.
Any async use still needs OS work/completion module anchor, cancellation,
initial deferral, stack dependency and UNKNOWN recovery review before linking.
No additional Linux capture requested by this checkpoint.
