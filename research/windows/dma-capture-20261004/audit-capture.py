"""Seven-file request-owned snapshot variant; production FALSE and no real consumers."""
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
BASE = '192f4de02055db0f8597050968255404779794c7'
PREFIX = 'research/windows/dma-capture-20261004/'
FILES = {PREFIX + name for name in ('bc250_dma_capture.c', 'bc250_dma_capture.h',
    'test-capture.c', 'audit-capture.py', 'verify-offline.py', 'README.txt', 'primary-source.json')}
PINS = {'bc250_dma_capture.c': '22f9cd6126eecb543fd5ac15ad7d4c3e0f083486ee3135dec124b778937fd0bd',
    'bc250_dma_capture.h': 'f727aacc13c4f446c67320884ae2f4a2242bb5d25a03155eeaa624f0bd145fa9'}
spec = importlib.util.spec_from_file_location('domain_auditor',
    ROOT / 'research/windows/domain-backend-20261003/audit-domain-backend.py')
previous = importlib.util.module_from_spec(spec); spec.loader.exec_module(previous)


def normalize(source):
    for kind in ('BOOLEAN', 'NTSTATUS', 'void'):
        source = source.replace('static ' + kind + ' ', 'static __inline int ')
    return source.replace('NTSTATUS Bc250DmaCapture', 'static __inline int Bc250DmaCapture')


def check_source(source):
    source = normalize(source)
    allowed = {'if', 'for', 'sizeof', 'FIELD_OFFSET', 'RtlZeroMemory', 'RtlCompareMemory',
        'KeGetCurrentIrql', 'KeGetCurrentThread', 'KeEnterCriticalRegion', 'KeLeaveCriticalRegion',
        'KeWaitForSingleObject', 'KeInitializeMutex', 'KeReleaseMutex', 'InterlockedCompareExchange',
        'InterlockedExchange', 'InterlockedCompareExchangePointer', 'InterlockedExchangePointer',
        'Bc250DmaGateInit', 'Bc250DmaGateEnter', 'Bc250DmaGateLeave', 'Bc250DmaGateStop',
        'Bc250DmaGateQuiesce', 'Bc250WinDmaOpen', 'Bc250WinDmaAcquire',
        'Bc250WinDmaCancel', 'Bc250WinDmaClose', 'MmGetMdlByteCount', 'MmGetMdlByteOffset'}
    allowed |= {'Bc250DmaCapture' + suffix for suffix in ('Policy', 'Guard', 'Disjoint', 'Away',
        'Fault', 'Lock', 'Unlock', 'Check', 'Buffer', 'EmptyOut', 'Match', 'New',
        'Admit', 'Finish', 'Init', 'Acquire', 'Retain', 'Drop', 'Stop', 'Retire', 'Storage', 'Snapshot', 'SnapshotRelease')}
    calls = set(re.findall(r'\b([A-Za-z_]\w*)\s*\(', previous.stripped(source)))
    if calls - allowed: raise ValueError('Unexpected operation: ' + str(calls - allowed))
    def body(suffix): return previous.function(source, 'Bc250DmaCapture' + suffix)
    tokens = {
        'Policy': ('#ifdef BC250_DMA_CAPTURE_MOCK', 'return FALSE;'),
        'Guard': ('if (!Bc250DmaCapturePolicy()) return STATUS_NOT_SUPPORTED;',
            'KeGetCurrentIrql() != PASSIVE_LEVEL'),
        'Disjoint': ('x > maximum - (an - 1U)', 'y > maximum - (bn - 1U)'),
        'Lock': ('&owner->MetadataThread, NULL, NULL) == thread', 'timeout.QuadPart = 0;',
            'KeEnterCriticalRegion();', 'KeWaitForSingleObject(&owner->Metadata, Executive, KernelMode, FALSE, &timeout);',
            'status == STATUS_TIMEOUT', 'Bc250DmaCaptureFault(owner);', 'InterlockedExchangePointer(&owner->MetadataThread, thread);'),
        'Unlock': ('InterlockedExchangePointer(&owner->MetadataThread, NULL);',
            'KeReleaseMutex(&owner->Metadata, FALSE);', 'KeLeaveCriticalRegion();'),
        'Check': ('record->Live != 1U', 'record->Id > owner->LastId', 'count != owner->References',
            'owner->Records[j].Id == record->Id', '!owner->PinnedMdl || !owner->PinnedList || !owner->PinnedListBytes'),
        'Storage': ('Bc250DmaCaptureAway(owner, buffer, bytes)',
            'Bc250DmaCaptureDisjoint(owner->PinnedMdl, sizeof(*owner->PinnedMdl), buffer, bytes)',
            'Bc250DmaCaptureDisjoint(owner->PinnedList, owner->PinnedListBytes, buffer, bytes)'),
        'Buffer': ('Bc250DmaCaptureStorage(owner, buffer, bytes)', 'owner->Records[i].Live && owner->Records[i].Snapshot &&',
            'Bc250DmaCaptureDisjoint(owner->Records[i].Snapshot, sizeof(BC250_DMA_CAPTURE_SNAPSHOT)'),
        'EmptyOut': ('RtlCompareMemory(out, &zero, sizeof(zero)) == sizeof(zero)',),
        'Match': ('handle->Owner != owner', 'handle->Slot >= BC250_DMA_CAPTURE_SLOTS',
            'owner->Records[handle->Slot].Id != handle->Id'),
        'New': ('!Bc250DmaCaptureEmptyOut(out)', 'owner->LastId == ~(ULONGLONG)0',
            'i == BC250_DMA_CAPTURE_SLOTS', '++owner->LastId', '++owner->References', '*out = handle;'),
        'Admit': ('owner->State, 0, 0) != BC250_DMA_CAPTURE_ACTIVE',
            'status = Bc250DmaGateEnter(&owner->Gate, ticket);'),
        'Finish': ('owner->Native.State == BC250_WIN_DMA_QUARANTINED',
            'leave = Bc250DmaGateLeave(&owner->Gate, ticket);', 'if (leave != STATUS_SUCCESS)'),
        'Init': ('RtlCompareMemory(owner, &zero, sizeof(zero)) != sizeof(zero)',
            'Bc250DmaCaptureAway(owner, description, sizeof(*description))',
            'KeInitializeMutex(&owner->Metadata, 0);', 'Bc250WinDmaOpen(&owner->Native, pdo, description)'),
        'Acquire': ('Bc250DmaCaptureDisjoint(mdl, sizeof(*mdl), out, sizeof(*out))',
            '!Bc250DmaCaptureEmptyOut(out)', 'owner->State, 0, 0) == BC250_DMA_CAPTURE_FAULT',
            'Bc250WinDmaAcquire(&owner->Native, mdl, offset, bytes, direction)',
            'Bc250DmaCaptureAway(owner, owner->Native.List, listBytes)', 'status = Bc250DmaCaptureNew(owner, out);'),
        'Retain': ('Bc250DmaCaptureDisjoint(input, sizeof(*input), out, sizeof(*out))',
            'owner->State, 0, 0) == BC250_DMA_CAPTURE_FAULT', 'owner->Native.State != BC250_WIN_DMA_HELD',
            'status = Bc250DmaCaptureMatch(owner, input);', 'status = Bc250DmaCaptureNew(owner, out);'),
        'Drop': ('status = Bc250DmaCaptureMatch(owner, input);',
            'RtlZeroMemory(&owner->Records[input->Slot], sizeof(owner->Records[input->Slot]));', '--owner->References;'),
        'Stop': ('BC250_DMA_CAPTURE_STOPPING, BC250_DMA_CAPTURE_ACTIVE',
            'state == BC250_DMA_CAPTURE_DRAINED', 'status = Bc250DmaGateStop(&owner->Gate);',
            'owner->Gate.State, 0, 0) == BC250_DMA_GATE_CLOSED',
            'current == BC250_DMA_CAPTURE_RETIRED'),
        'Retire': ('owner->Gate.OwnerThread, NULL, NULL) == KeGetCurrentThread()',
            'owner->MetadataThread, NULL, NULL) == KeGetCurrentThread()',
            'InterlockedCompareExchange(&owner->Coordinator, 1, 0)',
            'status = Bc250DmaGateQuiesce(&owner->Gate);',
            'owner->State, 0, 0) != BC250_DMA_CAPTURE_DRAINED',
            'else if (owner->References) status = STATUS_DEVICE_BUSY;',
            'RtlCompareMemory(&owner->Native, &zero, sizeof(zero)) != sizeof(zero)',
            'owner->Native.State != BC250_WIN_DMA_OPEN && owner->Native.State != BC250_WIN_DMA_HELD',
            'status = Bc250WinDmaCancel(&owner->Native);',
            'if (status == STATUS_SUCCESS) status = Bc250WinDmaClose(&owner->Native);',
            'InterlockedExchange(&owner->State, BC250_DMA_CAPTURE_RETIRED);')
    }
    tokens['Check'] += ('if (record->Snapshot) return STATUS_INVALID_DEVICE_STATE;',
        'owner->Records[j].Snapshot == record->Snapshot', 'owner->RequestValid != 1U',
        'owner->Request.Direction > TRUE', 'owner->RequestWidth > 48U')
    tokens['Acquire'] += ('owner->Request.Offset = offset; owner->Request.Bytes = bytes;',
        'owner->Request.Direction = direction; owner->RequestWidth = owner->Native.AddressWidth;',
        'owner->RequestValid = 1U;')
    for suffix in ('Retain', 'Drop'):
        tokens[suffix] += ('owner->Records[input->Slot].Snapshot',)
    tokens['Retire'] += ('RtlZeroMemory(&owner->Request, sizeof(owner->Request));',
        'owner->RequestValid = owner->RequestWidth = 0U;')
    tokens['Snapshot'] = ('Bc250DmaCaptureDisjoint(seed, sizeof(*seed), out, sizeof(*out))',
        'Bc250DmaCaptureDisjoint(expected, sizeof(*expected), out, sizeof(*out))',
        'Bc250DmaCaptureCheck(owner)', 'Bc250DmaCaptureMatch(owner, seed)',
        'owner->State, 0, 0) == BC250_DMA_CAPTURE_FAULT',
        'Bc250DmaCaptureBuffer(owner, expected, sizeof(*expected))',
        'Bc250DmaCaptureBuffer(owner, out, sizeof(*out))', 'owner->Records[seed->Slot].Snapshot',
        'RtlCompareMemory(out, &zero, sizeof(zero)) != sizeof(zero)',
        'expected->Offset != owner->Request.Offset', 'expected->Bytes != owner->Request.Bytes',
        'expected->Direction != owner->Request.Direction', 'owner->RequestValid != 1U',
        'owner->Native.State != BC250_WIN_DMA_HELD', 'owner->Native.Busy',
        '!owner->Native.RequestIssued', 'owner->Native.Cancelled',
        'owner->PinnedList != owner->Native.List', 'owner->PinnedMdl != owner->Native.HeldMdl',
        'owner->RequestWidth != owner->Native.AddressWidth', 'owner->RequestWidth > 48U',
        'owner->Request.Bytes > owner->Native.MaximumLength', 'owner->PinnedMdl->Next',
        'owner->PinnedMdl->MdlFlags & MDL_PAGES_LOCKED', 'MmGetMdlByteOffset(owner->PinnedMdl) != 0',
        'owner->Request.Offset >= MmGetMdlByteCount(owner->PinnedMdl)',
        'owner->Request.Bytes > MmGetMdlByteCount(owner->PinnedMdl) - owner->Request.Offset',
        '!count || count > BC250_DMA_CAPTURE_MAX_PAGES',
        'owner->PinnedListBytes != FIELD_OFFSET(SCATTER_GATHER_LIST, Elements)',
        '!bytes || (bytes & 4095U) || (start & 4095ULL) || start > limit',
        '(ULONGLONG)bytes - 1ULL > limit - start', 'total > owner->Request.Bytes',
        'bytes > owner->Request.Bytes - total',
        '(bytes / 4096U) > BC250_DMA_CAPTURE_MAX_PAGES - pages', 'total != owner->Request.Bytes',
        'Bc250DmaCaptureNew(owner, &out->Reference)',
        'owner->Records[out->Reference.Slot].Snapshot = out',
        'out->Self = out; out->Request = owner->Request', 'out->MaximumLast = limit',
        'out->PageCount = pages; out->ElementCount = count',
        'destination->Domain = BC250_AD_DMA_LOGICAL',
        'destination->Start = start + (ULONGLONG)page * 4096ULL',
        'out->State = BC250_DMA_CAPTURE_SNAPSHOT_LIVE')
    tokens['SnapshotRelease'] = ('owner->Signature != BC250_DMA_CAPTURE_SIGNATURE',
        'Bc250DmaCaptureAway(owner, snapshot, sizeof(*snapshot))',
        'Bc250DmaCaptureCheck(owner)', 'Bc250DmaCaptureStorage(owner, snapshot, sizeof(*snapshot))',
        'snapshot->Self != snapshot', 'snapshot->State != BC250_DMA_CAPTURE_SNAPSHOT_LIVE',
        'snapshot->Reference.Owner != owner', 'snapshot->Reference.Slot >= BC250_DMA_CAPTURE_SLOTS',
        '!snapshot->Reference.Id', '!owner->Records[slot].Live',
        'owner->Records[slot].Id != snapshot->Reference.Id',
        'owner->Records[slot].Snapshot != snapshot',
        'RtlZeroMemory(&owner->Records[slot], sizeof(owner->Records[slot]))', '--owner->References',
        'RtlZeroMemory(&snapshot->Reference, sizeof(snapshot->Reference))',
        'snapshot->State = BC250_DMA_CAPTURE_SNAPSHOT_RELEASED')
    sections = [(body(suffix), token) for suffix, entries in tokens.items() for token in entries]
    for suffix in ('Init', 'Acquire', 'Retain', 'Drop', 'Stop', 'Retire', 'Snapshot', 'SnapshotRelease'):
        sections.append((body(suffix), 'Bc250DmaCaptureGuard(owner)'))
    for section, token in sections:
        if section.count(token) != 1 or section not in source: raise ValueError('Guard missing: ' + token)
    def ordered(section, *items):
        positions = [section.index(item) for item in items]
        if positions != sorted(positions) or len(set(positions)) != len(positions):
            raise ValueError('Safety ordering changed: ' + str(items))
    ordered(body('Finish'), 'BC250_WIN_DMA_QUARANTINED', 'Bc250DmaGateLeave(')
    for suffix in ('Acquire', 'Retain'):
        ordered(body(suffix), 'Bc250DmaCaptureAdmit(', 'Bc250DmaCaptureLock(',
            'Bc250DmaCaptureNew(', 'Bc250DmaCaptureUnlock(', 'Bc250DmaCaptureFinish(')
    ordered(body('Retire'), 'Bc250DmaGateQuiesce(', 'Bc250DmaCaptureLock(',
        'owner->State, 0, 0) != BC250_DMA_CAPTURE_DRAINED', 'else if (owner->References)',
        'Bc250WinDmaCancel(', 'Bc250WinDmaClose(', 'BC250_DMA_CAPTURE_RETIRED')
    ordered(body('Drop'), 'Bc250DmaCaptureMatch(', 'RtlZeroMemory(', '--owner->References')
    ordered(body('Acquire'), 'Bc250WinDmaAcquire(', 'owner->Request.Offset = offset',
        'owner->RequestValid = 1U', 'Bc250DmaCaptureNew(', 'Bc250DmaCaptureUnlock(', 'Bc250DmaCaptureFinish(')
    ordered(body('Snapshot'), 'Bc250DmaCaptureAdmit(', 'Bc250DmaCaptureLock(',
        'Bc250DmaCaptureMatch(', 'expected->Offset', 'sg = owner->PinnedList',
        'total != owner->Request.Bytes', 'Bc250DmaCaptureNew(', '.Snapshot = out',
        'out->State = BC250_DMA_CAPTURE_SNAPSHOT_LIVE', 'Bc250DmaCaptureUnlock(', 'Bc250DmaCaptureFinish(')
    ordered(body('SnapshotRelease'), 'Bc250DmaCaptureLock(', 'Bc250DmaCaptureCheck(',
        'owner->Records[slot].Snapshot != snapshot', 'RtlZeroMemory(&owner->Records[slot]',
        '--owner->References', 'snapshot->State = BC250_DMA_CAPTURE_SNAPSHOT_RELEASED', 'Bc250DmaCaptureUnlock(')
    if 'sizeof(sg->Elements)' in source or re.search(r'BC250_AD_SPAN\s+\w+\[', source):
        raise ValueError('Fixed provider array capacity or big stack workspace')
    for suffix in ('Drop', 'Stop', 'SnapshotRelease'):
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
        canonical = data.replace(b'\r\n', b'\n')
        if b'\r' in canonical or not canonical.endswith(b'\n') or canonical.endswith(b'\n\n'):
            raise ValueError('UTF8 single-final-newline required')
        if path.suffix in ('.c', '.h') and data != canonical: raise ValueError('Pinned C/H requires LF')
        if b'\0' in data or re.search(r'-----BEGIN [A-Z ]*PRIVATE KEY-----|gh[pousr]_[A-Za-z0-9]{36,}|[A-Za-z]:[\\/]Users[\\/]', text):
            raise ValueError('Private/binary export')
        hashes[relative] = hashlib.sha256(data).hexdigest()
    for name, digest in PINS.items():
        if hashes[PREFIX + name] != digest: raise ValueError('Reviewed capture source changed')
    source = normalize((HERE / 'bc250_dma_capture.c').read_text(encoding='utf-8'))
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
    for key in ('implementation_copied_from_external', 'production_execution_allowed',
            'real_dma_page_binding_verified', 'hardware_tested'):
        if provenance[key] is not False: raise ValueError('Unexpected integration/import claim')
    runner = (HERE / 'verify-offline.py').read_text(encoding='utf-8')
    for token in ("for name in ('CL', '_CL_')", "'/UBC250_DMA_CAPTURE_MOCK'",
            "'/UBC250_DMA_GATE_MOCK'", "'/UBC250_DMA_ADAPTER_MOCK'", 'len(history) != 53', 'len(tests) != 23', 'len(artifacts) != 28'):
        if token not in runner: raise ValueError('Build isolation missing')
    result = dict(scope='WINDOWS_DMA_CAPTURE_REQUEST_SOURCE_FAKE_ONLY', baseline=BASE, new_source_sha256=hashes,
        source_guard_negative_controls=len(sections), operation_inventory=calls,
        predecessors_unchanged=True, capture_driver_integrated=False, real_map_consumers_supported=False,
        real_rundown_or_concurrency_validated=False, hardware_tested=False, installable_package=False)
    (out / 'AUDIT.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('PASS: seven new sources, %d guard negative controls; original request and retained copied SG, fake DDIs only' % len(sections))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True)
    audit(parser.parse_args().output)
