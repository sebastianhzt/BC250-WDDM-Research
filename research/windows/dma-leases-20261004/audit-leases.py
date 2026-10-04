"""Seven-file persistent metadata lease variant; no production execution."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import sys

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
BASE = '6c224edafaedff3f95e18ea2e3ec30b45f48229c'
PREFIX = 'research/windows/dma-leases-20261004/'
FILES = {PREFIX + name for name in ('bc250_dma_leases.c', 'bc250_dma_leases.h',
    'test-leases.c', 'audit-leases.py', 'verify-offline.py', 'README.txt', 'primary-source.json')}
PINS = {'bc250_dma_leases.c': '6911d0b737c94ea9cf5f9e6e5f288969a0c5c5c96a37a841d48790e62ad4c289',
    'bc250_dma_leases.h': 'fd71e42f2ab5887630444f2517feb6e41ca2a6fa5c961b791366132a3175ac67'}
spec = importlib.util.spec_from_file_location('domain_auditor',
    ROOT / 'research/windows/domain-backend-20261003/audit-domain-backend.py')
previous = importlib.util.module_from_spec(spec); spec.loader.exec_module(previous)


def normalize(source):
    for kind in ('BOOLEAN', 'NTSTATUS', 'void'):
        source = source.replace('static ' + kind + ' ', 'static __inline int ')
    return source.replace('NTSTATUS Bc250DmaLeases', 'static __inline int Bc250DmaLeases')


def check_source(source):
    source = normalize(source)
    allowed = {'if', 'for', 'sizeof', 'FIELD_OFFSET', 'RtlZeroMemory', 'RtlCompareMemory',
        'KeGetCurrentIrql', 'KeGetCurrentThread', 'KeEnterCriticalRegion', 'KeLeaveCriticalRegion',
        'KeWaitForSingleObject', 'KeInitializeMutex', 'KeReleaseMutex', 'InterlockedCompareExchange',
        'InterlockedExchange', 'InterlockedCompareExchangePointer', 'InterlockedExchangePointer',
        'Bc250DmaGateInit', 'Bc250DmaGateEnter', 'Bc250DmaGateLeave', 'Bc250DmaGateStop',
        'Bc250DmaGateQuiesce', 'Bc250WinDmaOpen', 'Bc250WinDmaAcquire',
        'Bc250WinDmaCancel', 'Bc250WinDmaClose'}
    allowed |= {'Bc250DmaLeases' + suffix for suffix in ('Policy', 'Guard', 'Disjoint', 'Away',
        'Fault', 'Lock', 'Unlock', 'Check', 'Buffer', 'EmptyOut', 'Match', 'New',
        'Admit', 'Finish', 'Init', 'Acquire', 'Retain', 'Drop', 'Stop', 'Retire')}
    calls = set(re.findall(r'\b([A-Za-z_]\w*)\s*\(', previous.stripped(source)))
    if calls - allowed: raise ValueError('Unexpected operation: ' + str(calls - allowed))
    def body(suffix): return previous.function(source, 'Bc250DmaLeases' + suffix)
    tokens = {
        'Policy': ('#ifdef BC250_DMA_LEASES_MOCK', 'return FALSE;'),
        'Guard': ('if (!Bc250DmaLeasesPolicy()) return STATUS_NOT_SUPPORTED;',
            'KeGetCurrentIrql() != PASSIVE_LEVEL'),
        'Disjoint': ('x > maximum - (an - 1U)', 'y > maximum - (bn - 1U)'),
        'Lock': ('&owner->MetadataThread, NULL, NULL) == thread', 'timeout.QuadPart = 0;',
            'KeEnterCriticalRegion();', 'KeWaitForSingleObject(&owner->Metadata, Executive, KernelMode, FALSE, &timeout);',
            'status == STATUS_TIMEOUT', 'Bc250DmaLeasesFault(owner);', 'InterlockedExchangePointer(&owner->MetadataThread, thread);'),
        'Unlock': ('InterlockedExchangePointer(&owner->MetadataThread, NULL);',
            'KeReleaseMutex(&owner->Metadata, FALSE);', 'KeLeaveCriticalRegion();'),
        'Check': ('record->Live != 1U', 'record->Id > owner->LastId', 'count != owner->References',
            'owner->Records[j].Id == record->Id', '!owner->PinnedMdl || !owner->PinnedList || !owner->PinnedListBytes'),
        'Buffer': ('Bc250DmaLeasesAway(owner, buffer, bytes)',
            'Bc250DmaLeasesDisjoint(owner->PinnedMdl, sizeof(*owner->PinnedMdl), buffer, bytes)',
            'Bc250DmaLeasesDisjoint(owner->PinnedList, owner->PinnedListBytes, buffer, bytes)'),
        'EmptyOut': ('RtlCompareMemory(out, &zero, sizeof(zero)) == sizeof(zero)',),
        'Match': ('handle->Owner != owner', 'handle->Slot >= BC250_DMA_LEASES_SLOTS',
            'owner->Records[handle->Slot].Id != handle->Id'),
        'New': ('!Bc250DmaLeasesEmptyOut(out)', 'owner->LastId == ~(ULONGLONG)0',
            'i == BC250_DMA_LEASES_SLOTS', '++owner->LastId', '++owner->References', '*out = handle;'),
        'Admit': ('owner->State, 0, 0) != BC250_DMA_LEASES_ACTIVE',
            'status = Bc250DmaGateEnter(&owner->Gate, ticket);'),
        'Finish': ('owner->Native.State == BC250_WIN_DMA_QUARANTINED',
            'leave = Bc250DmaGateLeave(&owner->Gate, ticket);', 'if (leave != STATUS_SUCCESS)'),
        'Init': ('RtlCompareMemory(owner, &zero, sizeof(zero)) != sizeof(zero)',
            'Bc250DmaLeasesAway(owner, description, sizeof(*description))',
            'KeInitializeMutex(&owner->Metadata, 0);', 'Bc250WinDmaOpen(&owner->Native, pdo, description)'),
        'Acquire': ('Bc250DmaLeasesDisjoint(mdl, sizeof(*mdl), out, sizeof(*out))',
            '!Bc250DmaLeasesEmptyOut(out)', 'owner->State, 0, 0) == BC250_DMA_LEASES_FAULT',
            'Bc250WinDmaAcquire(&owner->Native, mdl, offset, bytes, direction)',
            'Bc250DmaLeasesAway(owner, owner->Native.List, listBytes)', 'status = Bc250DmaLeasesNew(owner, out);'),
        'Retain': ('Bc250DmaLeasesDisjoint(input, sizeof(*input), out, sizeof(*out))',
            'owner->State, 0, 0) == BC250_DMA_LEASES_FAULT', 'owner->Native.State != BC250_WIN_DMA_HELD',
            'status = Bc250DmaLeasesMatch(owner, input);', 'status = Bc250DmaLeasesNew(owner, out);'),
        'Drop': ('status = Bc250DmaLeasesMatch(owner, input);',
            'RtlZeroMemory(&owner->Records[input->Slot], sizeof(owner->Records[input->Slot]));', '--owner->References;'),
        'Stop': ('BC250_DMA_LEASES_STOPPING, BC250_DMA_LEASES_ACTIVE',
            'state == BC250_DMA_LEASES_DRAINED', 'status = Bc250DmaGateStop(&owner->Gate);',
            'owner->Gate.State, 0, 0) == BC250_DMA_GATE_CLOSED',
            'current == BC250_DMA_LEASES_RETIRED'),
        'Retire': ('owner->Gate.OwnerThread, NULL, NULL) == KeGetCurrentThread()',
            'owner->MetadataThread, NULL, NULL) == KeGetCurrentThread()',
            'InterlockedCompareExchange(&owner->Coordinator, 1, 0)',
            'status = Bc250DmaGateQuiesce(&owner->Gate);',
            'owner->State, 0, 0) != BC250_DMA_LEASES_DRAINED',
            'else if (owner->References) status = STATUS_DEVICE_BUSY;',
            'RtlCompareMemory(&owner->Native, &zero, sizeof(zero)) != sizeof(zero)',
            'owner->Native.State != BC250_WIN_DMA_OPEN && owner->Native.State != BC250_WIN_DMA_HELD',
            'status = Bc250WinDmaCancel(&owner->Native);',
            'if (status == STATUS_SUCCESS) status = Bc250WinDmaClose(&owner->Native);',
            'InterlockedExchange(&owner->State, BC250_DMA_LEASES_RETIRED);')
    }
    sections = [(body(suffix), token) for suffix, entries in tokens.items() for token in entries]
    for suffix in ('Init', 'Acquire', 'Retain', 'Drop', 'Stop', 'Retire'):
        sections.append((body(suffix), 'Bc250DmaLeasesGuard(owner)'))
    for section, token in sections:
        if section.count(token) != 1 or section not in source: raise ValueError('Guard missing: ' + token)
    def ordered(section, *items):
        positions = [section.index(item) for item in items]
        if positions != sorted(positions) or len(set(positions)) != len(positions):
            raise ValueError('Safety ordering changed: ' + str(items))
    ordered(body('Finish'), 'BC250_WIN_DMA_QUARANTINED', 'Bc250DmaGateLeave(')
    for suffix in ('Acquire', 'Retain'):
        ordered(body(suffix), 'Bc250DmaLeasesAdmit(', 'Bc250DmaLeasesLock(',
            'Bc250DmaLeasesNew(', 'Bc250DmaLeasesUnlock(', 'Bc250DmaLeasesFinish(')
    ordered(body('Retire'), 'Bc250DmaGateQuiesce(', 'Bc250DmaLeasesLock(',
        'owner->State, 0, 0) != BC250_DMA_LEASES_DRAINED', 'else if (owner->References)',
        'Bc250WinDmaCancel(', 'Bc250WinDmaClose(', 'BC250_DMA_LEASES_RETIRED')
    ordered(body('Drop'), 'Bc250DmaLeasesMatch(', 'RtlZeroMemory(', '--owner->References')
    for suffix in ('Drop', 'Stop'):
        if 'Bc250WinDma' in previous.stripped(body(suffix)) or 'Bc250DmaGateEnter' in previous.stripped(body(suffix)):
            raise ValueError('Implicit cleanup/admission from Drop/Stop')
    return sections, sorted(calls)


def audit(output):
    out = Path(output).resolve(); out.relative_to((ROOT / 'output').resolve())
    if not out.is_dir(): raise ValueError('Scoped output required')
    previous.git('merge-base', '--is-ancestor', BASE, 'HEAD')
    changed = set(previous.git('diff', '--name-only', '-z', BASE).split('\0')) - {''}
    untracked = set(previous.git('ls-files', '--others', '--exclude-standard', '-z').split('\0')) - {''}
    if changed | untracked != FILES: raise ValueError('Exact seven-file scope mismatch')
    hashes = {}
    for relative in sorted(FILES):
        path = ROOT / relative
        if not path.is_file() or path.is_symlink(): raise ValueError('Regular source required')
        data = path.read_bytes(); text = data.decode('utf-8')
        if b'\0' in data or re.search(r'-----BEGIN [A-Z ]*PRIVATE KEY-----|gh[pousr]_[A-Za-z0-9]{36,}|[A-Za-z]:[\\/]Users[\\/]', text):
            raise ValueError('Private/binary export')
        hashes[relative] = hashlib.sha256(data).hexdigest()
    for name, digest in PINS.items():
        if hashes[PREFIX + name] != digest: raise ValueError('Reviewed lease source changed')
    source = normalize((HERE / 'bc250_dma_leases.c').read_text(encoding='utf-8'))
    sections, calls = check_source(source)
    for section, token in sections:
        mutant = source.replace(section, section.replace(token, 'REMOVED_GUARD', 1), 1)
        try: check_source(mutant)
        except ValueError: continue
        raise ValueError('Negative control accepted')
    readme = (HERE / 'README.txt').read_text(encoding='utf-8')
    for token in ('Build11', 'NO SYS/INF/CAT', 'CREDITS.txt', 'lifetime externo',
            'NO prueba de concurrencia real', 'NO consumidores reales', 'SOLO Stop', 'Drop nunca libera'):
        if token not in readme: raise ValueError('Scope/attribution missing')
    provenance = json.loads((HERE / 'primary-source.json').read_text(encoding='utf-8'))
    if provenance['source_baseline'] != BASE or len(provenance['local_sources']) != 6:
        raise ValueError('Provenance mismatch')
    for relative in provenance['local_sources']:
        if not (ROOT / relative).is_file(): raise ValueError('Protected source missing')
    runner = (HERE / 'verify-offline.py').read_text(encoding='utf-8')
    for token in ("for name in ('CL', '_CL_')", "'/UBC250_DMA_LEASES_MOCK'",
            "'/UBC250_DMA_GATE_MOCK'", "'/UBC250_DMA_ADAPTER_MOCK'", 'len(history) != 46'):
        if token not in runner: raise ValueError('Build isolation missing')
    result = dict(scope='WINDOWS_DMA_LEASES_SOURCE_FAKE_ONLY', baseline=BASE, new_source_sha256=hashes,
        source_guard_negative_controls=len(sections), operation_inventory=calls,
        predecessors_unchanged=True, lease_driver_integrated=False, real_map_consumers_supported=False,
        real_rundown_or_concurrency_validated=False, hardware_tested=False, installable_package=False)
    (out / 'AUDIT.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('PASS: seven new sources, %d guard negative controls; fake lease metadata only' % len(sections))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True)
    audit(parser.parse_args().output)
