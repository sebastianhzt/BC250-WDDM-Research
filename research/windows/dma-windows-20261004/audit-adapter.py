"""Eight-file successor audit: source restrictions, not Windows/GPU proof."""
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
BASE = 'e6d222580e0d6e7c9b8a1afe780e3c5bad0c02dc'
PREFIX = 'research/windows/dma-windows-20261004/'
FILES = {PREFIX + name for name in ('bc250_dma_adapter.c', 'bc250_dma_adapter.h',
    'mock-wdm.h', 'test-adapter.c', 'audit-adapter.py', 'verify-offline.py',
    'README.txt', 'primary-source.json')}
SOURCE_SHA256 = 'e8905ef2ac418b6f7d9c09203df782fcaf54dfcadf771d3fc5a03103442d16ed'
spec = importlib.util.spec_from_file_location('previous_auditor',
    ROOT / 'research/windows/domain-backend-20261003/audit-domain-backend.py')
previous = importlib.util.module_from_spec(spec)
spec.loader.exec_module(previous)


def check_source(source):
    allowed = {'if', 'for', 'sizeof', 'Bc250WinDmaPolicy', 'Bc250WinDmaGuard',
        'Bc250WinDmaAway', 'Bc250WinDmaOpen', 'Bc250WinDmaAcquire',
        'Bc250WinDmaListValid', 'Bc250WinDmaDropHeld', 'Bc250WinDmaRelease',
        'Bc250WinDmaCancel', 'Bc250WinDmaClose', 'KeGetCurrentIrql',
        'NT_SUCCESS', 'IoGetDmaAdapter', 'FIELD_OFFSET', 'Put', 'Get',
        'Free', 'Initialize', 'MmGetMdlByteOffset', 'MmGetMdlByteCount'}
    calls = set(re.findall(r'\b([A-Za-z_]\w*)\s*\(', previous.stripped(source)))
    if calls - allowed: raise ValueError('Unexpected operation: ' + str(calls - allowed))
    guards = (
        'return FALSE; /* Production execution gate: permanently closed. */',
        'if (!Bc250WinDmaPolicy()) return STATUS_NOT_SUPPORTED;',
        'if (KeGetCurrentIrql() != PASSIVE_LEVEL) return STATUS_INVALID_DEVICE_STATE;',
        'if (owner->Busy) return STATUS_DEVICE_BUSY;',
        'copy.Version != DEVICE_DESCRIPTION_VERSION3',
        'copy.Master != TRUE',
        '!copy.DmaAddressWidth || copy.DmaAddressWidth > 48U',
        'operations->Size < FIELD_OFFSET(DMA_OPERATIONS, PutDmaAdapter)',
        'operations->Size < FIELD_OFFSET(DMA_OPERATIONS, FreeAdapterObject)',
        'registers < copy.MaximumLength / 4096U',
        'owner->Busy = 1U; owner->State = BC250_WIN_DMA_OPENING;',
        'if (owner->Cancelled) return STATUS_CANCELLED;',
        'if (owner->RequestIssued) return STATUS_INVALID_DEVICE_STATE;',
        'owner->RequestIssued = 1U; /* This context is never offered for a second request. */',
        '!Bc250WinDmaAway(owner, mdl, sizeof(*mdl)) || mdl->Next',
        '!(mdl->MdlFlags & MDL_PAGES_LOCKED)',
        'bytes > MmGetMdlByteCount(mdl) - offset',
        'offset, bytes, DMA_SYNCHRONOUS_CALLBACK, NULL, NULL, writeToDevice,',
        'NULL, NULL, &owner->List);',
        'if (NT_SUCCESS(status) || owner->List)',
        'list->NumberOfElements > 64U',
        '(ULONGLONG)length - 1ULL > limit - address',
        'return total == bytes;',
        'owner->Free(owner->Adapter, DeallocateObject);',
        'owner->Busy = 1U; Bc250WinDmaDropHeld(owner); owner->Busy = 0;',
        'owner->Busy = 1U; owner->Cancelled = 1U;',
        'owner->Busy = 1U; owner->Put(owner->Adapter);',
    )
    for token in guards:
        if source.count(token) != 1: raise ValueError('Guard missing/duplicated: ' + token)
    if source.count('NTSTATUS status = Bc250WinDmaGuard(owner);') != 5:
        raise ValueError('Public entrypoint bypasses guard')
    if not re.search(r'#ifdef BC250_DMA_ADAPTER_MOCK\s+return Bc250MockExecutionAllowed;[^\n]*\n#else\s+return FALSE;[^\n]*\n#endif', source):
        raise ValueError('Production policy branch changed')
    return guards, sorted(calls)


def audit(output):
    out = Path(output).resolve(); out.relative_to((ROOT / 'output').resolve())
    if not out.is_dir(): raise ValueError('Scoped output directory required')
    previous.git('merge-base', '--is-ancestor', BASE, 'HEAD')
    changed = set(previous.git('diff', '--name-only', '-z', BASE).split('\0')) - {''}
    untracked = set(previous.git('ls-files', '--others', '--exclude-standard', '-z').split('\0')) - {''}
    if changed | untracked != FILES:
        raise ValueError('Exact eight-file scope mismatch: ' + str(sorted((changed | untracked) ^ FILES)))
    hashes = {}
    for relative in sorted(FILES):
        path = ROOT / relative
        if not path.is_file() or path.is_symlink(): raise ValueError('Regular source required')
        data = path.read_bytes(); text = data.decode('utf-8')
        if b'\0' in data or re.search(r'-----BEGIN [A-Z ]*PRIVATE KEY-----|gh[pousr]_[A-Za-z0-9]{36,}|[A-Za-z]:[\\/]Users[\\/]', text):
            raise ValueError('Private/binary export')
        hashes[relative] = hashlib.sha256(data).hexdigest()
    if hashes[PREFIX + 'bc250_dma_adapter.c'] != SOURCE_SHA256:
        raise ValueError('Reviewed source hash changed')
    source = (HERE / 'bc250_dma_adapter.c').read_text(encoding='utf-8')
    guards, calls = check_source(source)
    for token in guards:
        try: check_source(source.replace(token, 'REMOVED_GUARD', 1))
        except ValueError: continue
        raise ValueError('Negative control accepted')
    readme = (HERE / 'README.txt').read_text(encoding='utf-8')
    for token in ('SOLO RAM', 'Build11', 'NO lock', 'QUARANTINED', 'CREDITS.txt', 'NO SYS/INF/CAT'):
        if token not in readme: raise ValueError('Scope/attribution missing')
    provenance = json.loads((HERE / 'primary-source.json').read_text(encoding='utf-8'))
    if len(provenance['sources']) != 6 or any(not item['url'].startswith('https://learn.microsoft.com/en-us/windows-hardware/drivers/') for item in provenance['sources']):
        raise ValueError('Primary documentation missing')
    runner = (HERE / 'verify-offline.py').read_text(encoding='utf-8')
    for token in ("for name in ('CL', '_CL_')", "'/UBC250_DMA_ADAPTER_MOCK'", "'/c', '/kernel'", "len(history) != 33"):
        if token not in runner: raise ValueError('Build isolation guard missing')
    result = dict(scope='ISOLATED_DMA_WINDOWS_SOURCE_ONLY', baseline=BASE,
        protected_implementation_unchanged=True, new_source_sha256=hashes,
        source_guard_negative_controls=len(guards), operation_inventory=calls,
        production_execution_allowed=False, real_windows_dma_tested=False,
        hardware_tested=False, installable_package=False, driver_loaded=False)
    (out / 'AUDIT.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('PASS: eight new files only; 27 guard negative controls; production gate closed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True)
    audit(parser.parse_args().output)
