"""Scoped successor source audit; not a whole-driver safety certificate."""
import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
BASE = 'ef3332ef75ed368d2d9984990f55bfd00bff3c74'
POLICY = 'inc/passive-port-20261003/bc250_passive_policy.h'
FLAG = 'BC250_PASSIVE_GPU_RUNTIME_ENABLED'
KMD = 'src/kmd/amdbc250_dream_kmd.c'
FW = 'src/kmd/amdbc250_dream_fw_load.c'
HW = 'inc/amdbc250_dream_hw.h'
PRODUCTION = {
    KMD: '6a559f990d48f5864f2675440a2709f323e02b085cf563ae4c78025e1aa0f085',
    FW: 'a291f9dea2d92451b36c727deb0e4dfee0f92069ae539e8a22575121810bc00e',
    HW: '1d98c6fa628b78038c81a9cd5389a184a121a6a39c161d447f3f56020ebce987',
    'src/kmd/amdbc250_dream_vm.c': '9a739e8da26fc47005797540ccc60e518163463102463b660da5fb30e8e7525a',
    'src/kmd/amdbc250_dream_hw_init.c': '9523a2389f0ca293cb4d198d0449f8c1f3fac04ac090008f2c990965c48aa6d0',
    'src/kmd/amdbc250_dream_hw_init_extended.c': '5d467186d4b39d8df300cec663c22a9bc023fb1f42780edeee1f9589fa17cc62',
    'src/kmd/amdbc250_dream_power.c': '950e21f5c67c0be16a755da23534f6064443e06f74df78ce2c37ee978a02ec3d',
}
STAGE = 'research/windows/passive-port-20261003/'
NEW_FILES = {POLICY} | {STAGE + name for name in (
    'README.txt', 'audit-passive-port.py', 'verify-offline.py',
    'test-hardening.c', 'test-policy.c')}
UNITS = tuple('amdbc250_' + stem + '.c' for stem in (
    'dream_kmd', 'dream_hw_init', 'dream_hw_init_extended', 'dream_power',
    'dream_vm', 'psp', 'dream_fw_load', 'dream_psp_fw_load', 'dream_golden',
    'dream_hdp', 'dream_rlc', 'dream_vbios', 'dream_kmd_ddi_stubs'))
WRITERS = ('DreamV3WritePm4Type0', 'DreamV3WritePm4Type3', 'DreamV3WriteEopFence')


def names(words):
    return words.split()


GUARDS = {
    KMD: names('DreamV3AllocVidMem DreamV3DisplayWritesEnabled DreamV3DdiStartDevice '
        'DreamV3DdiResetDevice DreamV3DdiInterruptRoutine DreamV3DdiDpcRoutine '
        'DreamV3DdiCreateAllocation DreamV3SwPm4Process DreamV3SubmitGfxRing '
        'DreamV3DdiSubmitCommand DreamV3DdiPresent DreamV3DdiRender DreamV3DdiCommitVidPn '
        'DreamV3DdiSetVidPnSourceAddress DreamV3DdiSetVidPnSourceVisibility DreamV3DdiEscape '
        'DreamV3DeviceControl DreamV3SdmaCopyBuffer DreamV3SdmaFillBuffer DreamV3TdrReset'),
    FW: names('DreamV3LoadAllFirmware DreamV3HaltAllEngines'),
    'src/kmd/amdbc250_dream_vm.c': names('DreamV3GartInitialize DreamV3GartMapPage '
        'DreamV3GartUnmapPage DreamV3GartAllocateRange DreamV3VmInitialize DreamV3VmShutdown '
        'DreamV3VmAllocatePageTable DreamV3VmFreePageTable DreamV3VmCreateContext '
        'DreamV3VmDestroyContext DreamV3VmMapRange DreamV3VmInsertMapping DreamV3VmUnmapRange '
        'DreamV3VmInvalidateTLB DreamV3VmConfigureSystemAperture DreamV3DdiBuildPagingBuffer '
        'DreamV3VmEvictMemory DreamV3VmRestoreMemory'),
    'src/kmd/amdbc250_dream_hw_init.c': names('DreamV3HwInitialize DreamV3HwReset '
        'DreamV3HwShutdown DreamV3HdpFlush DreamV3HwInitFence DreamV3HwInitGfxRing '
        'DreamV3HwInitIhRing DreamV3HwInitSdmaRing DreamV3PspHardwareInit DreamV3HwInitDisplay '
        'DreamV3InitCommandProcessor DreamV3InitMemoryController DreamV3ReadTemperature '
        'DreamV3WaitForRegister DreamV3AllocateContiguousMemory'),
    'src/kmd/amdbc250_dream_hw_init_extended.c': names('DreamV3HwInitializeExtended DreamV3UseExtendedInit'),
    'src/kmd/amdbc250_dream_power.c': names('SmnRead SmnWrite DreamV3SmuSendMessage '
        'DreamV3SmuWaitForResponse DreamV3SmuInitialize DreamV3SmuWakeGfx DreamV3SmuShutdown '
        'DreamV3SetPowerStateD0 DreamV3SetPowerStateD3 DreamV3DdiSetPowerState '
        'DreamV3SetGpuClockMhz DreamV3SetMemoryClockMhz DreamV3UpdateClocks '
        'DreamV3ReadAllThermalSensors DreamV3UpdateFanSpeed DreamV3CheckThermalThrottle '
        'DreamV3GetPowerUsage DreamV3SetPowerLimit DreamV3GetTelemetry DreamV3DdiNotifyAcpiEvent'),
}


def stripped(text):
    # Preserve offsets for mechanical extraction; braces in comments/strings are inert.
    pattern = r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    return re.sub(pattern, lambda m: ''.join('\n' if c == '\n' else ' ' for c in m[0]), text, flags=re.S)


def function(text, name):
    clean = stripped(text)
    pattern = (r'(?m)^(?:static\s+)?(?:NTSTATUS|VOID|void|BOOLEAN|ULONG|PVOID|UINT32|LONG)\s+'
        r'(?:APIENTRY\s+)?' + re.escape(name) + r'\s*\([^;{}]*\)\s*\{')
    matches = list(re.finditer(pattern, clean))
    if len(matches) != 1:
        raise ValueError('Expected one definition: ' + name)
    start = matches[0].start()
    opening = matches[0].end() - 1
    depth = 1
    for end in range(opening + 1, len(clean)):
        depth += (clean[end] == '{') - (clean[end] == '}')
        if depth == 0:
            return text[start:end + 1]
    raise ValueError('Unbalanced definition: ' + name)


def expected_return(body, name):
    if name == 'DreamV3GartAllocateRange': return 'return 0xFFFFFFFFUL;'
    if name == 'SmnRead': return 'return 0xFFFFFFFFUL;'
    if name == 'DreamV3ReadTemperature': return 'return -1;'
    kind = re.match(r'(?:static\s+)?(NTSTATUS|VOID|void|BOOLEAN|PVOID)\b', body)
    if not kind: raise ValueError('Unrecognized guard return: ' + name)
    return {'NTSTATUS': 'return STATUS_NOT_SUPPORTED;', 'VOID': 'return;',
            'void': 'return;', 'BOOLEAN': 'return FALSE;', 'PVOID': 'return NULL;'}[kind[1]]


def check_sources(sources, policy, bounds):
    if not re.search(r'(?m)^#define\s+' + FLAG + r'\s+0U\s*$', policy):
        raise ValueError('Runtime zero policy changed')
    if ('#ifdef ' + FLAG + '\n#error Do not override the passive research policy with compiler flags\n#endif') not in policy:
        raise ValueError('Compiler override refusal missing')
    inventory = []
    for path, members in GUARDS.items():
        source = sources[path]
        if '#include "passive-port-20261003/bc250_passive_policy.h"' not in source:
            raise ValueError('Policy not included: ' + path)
        for name in members:
            body = function(source, name)
            prefix = stripped(body).split('{', 1)[1].lstrip()
            if name == 'DreamV3DeviceControl':
                pattern = (r'if\s*\(!' + FLAG + r'\)\s*\{\s*'
                    r'Irp->IoStatus.Status\s*=\s*STATUS_NOT_SUPPORTED;\s*'
                    r'Irp->IoStatus.Information\s*=\s*0;\s*'
                    r'IoCompleteRequest\(Irp,\s*IO_NO_INCREMENT\);\s*'
                    r'return STATUS_NOT_SUPPORTED;\s*\}')
            else:
                ret = re.escape(expected_return(stripped(body).lstrip(), name))
                pattern = (r'if\s*\(!' + FLAG + r'\)\s*' + ret if path in (KMD, FW)
                    else r'#if\s+!' + FLAG + r'\s+' + ret + r'\s*#endif')
            if not re.match(pattern, prefix):
                raise ValueError('First-action barrier failed: ' + name)
            inventory.append(dict(path=path, function=name))
    if len(inventory) != 77:
        raise ValueError('Unexpected barrier inventory size')
    for name in WRITERS:
        body = function(sources[KMD], name)
        if 'Bc250Pm4Plan(' not in body or body.index('Bc250Pm4Plan(') > body.index('Ring['):
            raise ValueError('PM4 pre-write plan missing: ' + name)
        if 'WPtr + TotalSize' in body or 'Count * sizeof' in body:
            raise ValueError('Unchecked PM4 arithmetic returned')
    if 'payload_dwords > 16384U' not in bounds or 'size - write_pointer' not in bounds:
        raise ValueError('PM4 span/count policy changed')
    loader = function(sources[FW], 'DreamV3LoadFirmwareFromFile')
    for token in ('if (OutData) *OutData = NULL;', 'if (OutSize) *OutSize = 0;',
                  '!FileName || !OutData || !OutSize', 'KeGetCurrentIrql() != PASSIVE_LEVEL',
                  'fileInfo.EndOfFile.QuadPart <= 0', 'fileInfo.EndOfFile.QuadPart > MAX_FW_SIZE',
                  'ioStatus.Information != fileSize', 'FILE_NON_DIRECTORY_FILE'):
        if token not in loader: raise ValueError('Firmware reader drift: ' + token)
    if loader.index('fileInfo.EndOfFile.QuadPart > MAX_FW_SIZE') > loader.index('(ULONG)fileInfo.EndOfFile.QuadPart'):
        raise ValueError('Firmware size cast precedes bound')
    dispatch = function(sources[KMD], 'DreamV3DeviceControl')
    whitelist = dispatch[dispatch.index('static const SMU_CPU_MSG_DESC Whitelist[]'):]
    whitelist = whitelist[:whitelist.index('};')]
    for message in ('SET_DRV_TBL_ADDR_HI', 'SET_DRV_TBL_ADDR_LO', 'TRANSFER_TBL_SMU2DRAM', 'TRANSFER_TBL_DRAM2SMU'):
        if 'AMDBC250_SMU_Q0_' + message in whitelist:
            raise ValueError('Unowned SMU DMA whitelist message returned')
    if '((3U << 30) | (((count) - 1U) << 16) | ((opcode) << 8))' not in sources[HW]:
        raise ValueError('Unsigned packet type3 header missing')
    return inventory


def git(*args):
    result = subprocess.run(['git', '-c', 'safe.directory=' + ROOT.as_posix(), *args],
        cwd=ROOT, capture_output=True, shell=False)
    if result.returncode: raise ValueError('Git check failed: ' + result.stderr.decode(errors='replace'))
    return result.stdout


def macro(text, name):
    match = re.search(r'(?m)^#define\s+' + name + r'\b(?:[^\n]*\\\n)*[^\n]*', text)
    if not match: raise ValueError('Missing production macro: ' + name)
    return match[0]


def run(output):
    destination = Path(output).resolve()
    destination.relative_to((ROOT / 'output').resolve())
    if not destination.is_dir(): raise ValueError('Existing scoped output folder required')
    git('merge-base', '--is-ancestor', BASE, 'HEAD')
    changed = set(git('diff', '--name-only', '-z', BASE).decode().split('\0')) - {''}
    untracked = set(git('ls-files', '--others', '--exclude-standard', '-z').decode().split('\0')) - {''}
    if not changed <= set(PRODUCTION) | NEW_FILES or not untracked <= NEW_FILES:
        raise ValueError('Changed/untracked path outside reviewed port allowlist: ' + str(sorted((changed | untracked) - set(PRODUCTION) - NEW_FILES)))
    if not all((ROOT / path).is_file() for path in NEW_FILES):
        raise ValueError('Missing allowlisted new source')
    for relative in set(PRODUCTION) | NEW_FILES:
        path = ROOT / relative
        if path.is_symlink() or any(parent.is_symlink() for parent in path.parents if parent != ROOT.parent):
            raise ValueError('Symlink in scoped source input: ' + relative)
    for relative in NEW_FILES:
        data = (ROOT / relative).read_bytes()
        if b'\0' in data: raise ValueError('Binary content in source-only allowlist: ' + relative)
        text = data.decode('utf-8')
        if re.search(r'-----BEGIN [A-Z ]*PRIVATE KEY-----|gh[pousr]_[A-Za-z0-9]{36,}|[A-Za-z]:[\\/]Users[\\/]', text):
            raise ValueError('Private material/local user path in new export: ' + relative)
    sources = {}
    for path, expected in PRODUCTION.items():
        data = (ROOT / path).read_bytes()
        if hashlib.sha256(data).hexdigest() != expected: raise ValueError('Reviewed production snapshot changed: ' + path)
        sources[path] = data.decode('utf-8').replace('\r\n', '\n')
    policy = (ROOT / POLICY).read_text(encoding='utf-8')
    # No redefining or undefining the shared flag anywhere else in src/inc.
    for folder in ('src', 'inc'):
        for path in (ROOT / folder).rglob('*'):
            if path.is_file() and path.suffix in ('.h', '.c', '.cpp') and path != ROOT / POLICY:
                if re.search(r'(?m)^\s*#\s*(?:define|undef)\s+' + FLAG + r'\b', stripped(path.read_text(encoding='utf-8'))):
                    raise ValueError('Runtime policy redefinition: ' + path.relative_to(ROOT).as_posix())
    bounds = (ROOT / 'inc/upstream-integration-20261003/bc250_pm4_bounds.h').read_text()
    inventory = check_sources(sources, policy, bounds)
    # Semantic controls are intentionally independent of snapshot hashing.
    dispatch = function(sources[KMD], 'DreamV3DeviceControl')
    dirty_dispatch = dispatch.replace('Irp->IoStatus.Information = 0;',
                                      'Irp->IoStatus.Information = 4;', 1)
    if dirty_dispatch == dispatch:
        raise ValueError('IOCTL completion control did not mutate its target')
    controls = [
        (sources, policy.replace('ENABLED 0U', 'ENABLED 1U'), bounds),
        (dict(sources, **{FW: sources[FW].replace('ioStatus.Information != fileSize', '0')}), policy, bounds),
        (dict(sources, **{KMD: sources[KMD].replace('if (!' + FLAG + ') return STATUS_NOT_SUPPORTED;', 'if (!' + FLAG + ') return STATUS_SUCCESS;', 1)}), policy, bounds),
        (sources, policy, bounds.replace('payload_dwords > 16384U', 'payload_dwords > 0xFFFFFFFFU')),
        (dict(sources, **{KMD: sources[KMD].replace(dispatch, dirty_dispatch, 1)}), policy, bounds),
    ]
    for index, mutant in enumerate(controls):
        try: check_sources(*mutant)
        except ValueError: continue
        raise ValueError('Semantic negative control accepted: ' + str(index))
    barrier_controls = 0
    for path, members in GUARDS.items():
        for name in members:
            body = function(sources[path], name)
            mutated_body = body.replace(FLAG, '1', 1)
            mutant = dict(sources)
            mutant[path] = sources[path].replace(body, mutated_body, 1)
            try: check_sources(mutant, policy, bounds)
            except ValueError:
                barrier_controls += 1
                continue
            raise ValueError('Barrier bypass semantic control accepted: ' + name)
    if barrier_controls != 77: raise ValueError('Incomplete barrier bypass controls')
    build = (ROOT / 'build.bat').read_text()
    start = build.index('cl.exe /c /kernel')
    end = build.index('if errorlevel', start)
    units = tuple(re.findall(r'%SRC_DIR%\\kmd\\([^"\s]+\.c)', build[start:end]))
    if units != UNITS: raise ValueError('KMD compile inventory drift')
    fixture = (HERE / 'test-hardening.c').read_text()
    if '#include "firmware-defines.inc"' not in fixture or '#define MAX_FW_SIZE' in fixture:
        raise ValueError('Fixture duplicates firmware size instead of extracting')
    (destination / 'firmware-under-test.inc').write_text(function(sources[FW], 'DreamV3LoadFirmwareFromFile'), encoding='utf-8')
    (destination / 'firmware-defines.inc').write_text(macro(sources[FW], 'MAX_FW_SIZE') + '\n', encoding='utf-8')
    (destination / 'pm4-under-test.inc').write_text('\n'.join(function(sources[KMD], name) for name in WRITERS), encoding='utf-8')
    (destination / 'pm4-defines.inc').write_text('\n'.join(macro(sources[HW], name) for name in
        ('PM4_TYPE0_HDR', 'PM4_TYPE3_HDR', 'PM4_TYPE2_NOP', 'IT_EVENT_WRITE_EOP')) + '\n', encoding='utf-8')
    result = dict(scope='SCOPED_PASSIVE_SOURCE_PORT', baseline=BASE,
        source_sha256=PRODUCTION, barrier_inventory=inventory, semantic_negative_controls=len(controls),
        independent_barrier_bypass_controls=barrier_controls,
        kernel_compile_units=units, runtime_gpu_permission=False, installable_package=False,
        hardware_tested=False, whole_driver_safety_certified=False)
    (destination / 'AUDIT.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('PASS: protected baseline, 77 first-action barriers and bypass controls, 5 other negative controls; scoped audit only')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True)
    run(parser.parse_args().output)
