"""Eight new gate sources only; all predecessor sources remain frozen."""
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
BASE = '8ee55bec5a352e141d1eeb8a814bc04a5f54524c'
PREFIX = 'research/windows/dma-gate-20261004/'
FILES = {PREFIX + name for name in ('bc250_dma_gate.c', 'bc250_dma_gate.h',
    'mock-gate-wdm.h', 'test-gate.c', 'audit-gate.py', 'verify-offline.py',
    'README.txt', 'primary-source.json')}
PINS = {'bc250_dma_gate.c': '0d0552845e2254581cecc15dbac96454c2fd96fb5c40da45c796341926dc570b',
    'bc250_dma_gate.h': '8866f42b5e059d1e42f6f7f6c1e718594a0414dadc1389c6c084e8f6705ce3b4'}
spec = importlib.util.spec_from_file_location('domain_auditor',
    ROOT / 'research/windows/domain-backend-20261003/audit-domain-backend.py')
previous = importlib.util.module_from_spec(spec); spec.loader.exec_module(previous)


def normalize(source):
    for kind in ('BOOLEAN', 'NTSTATUS', 'LONG'):
        source = source.replace('static ' + kind + ' ', 'static __inline int ')
    return source.replace('NTSTATUS Bc250DmaGate', 'static __inline int Bc250DmaGate')


def check_source(source):
    source = normalize(source)
    allowed = {'if', 'sizeof', 'RtlZeroMemory', 'RtlCompareMemory', 'KeGetCurrentIrql',
        'KeGetCurrentThread', 'KeInitializeMutex', 'KeWaitForSingleObject', 'KeReleaseMutex',
        'KeEnterCriticalRegion', 'KeLeaveCriticalRegion', 'ExInitializeRundownProtection',
        'ExAcquireRundownProtection', 'ExReleaseRundownProtection',
        'ExWaitForRundownProtectionRelease', 'InterlockedCompareExchange', 'InterlockedExchange',
        'InterlockedCompareExchangePointer', 'InterlockedExchangePointer'}
    allowed |= {'Bc250DmaGate' + suffix for suffix in ('Policy', 'Guard', 'State', 'Away',
        'Init', 'Enter', 'Leave', 'Stop', 'Quiesce')}
    calls = set(re.findall(r'\b([A-Za-z_]\w*)\s*\(', previous.stripped(source)))
    if calls - allowed: raise ValueError('Unexpected gate operation: ' + str(calls - allowed))
    def body(suffix): return previous.function(source, 'Bc250DmaGate' + suffix)
    sections = (
        (body('Policy'), '#ifdef BC250_DMA_GATE_MOCK'),
        (body('Policy'), 'return FALSE;'),
        (body('Guard'), 'if (!Bc250DmaGatePolicy()) return STATUS_NOT_SUPPORTED;'),
        (body('Guard'), 'KeGetCurrentIrql() != PASSIVE_LEVEL'),
        (body('Away'), 'b > maximum - (bytes - 1U)'),
        (body('Init'), 'RtlCompareMemory(gate, &zero, sizeof(zero)) != sizeof(zero)'),
        (body('Init'), 'ExInitializeRundownProtection(&gate->Rundown);'),
        (body('Enter'), 'if (ticket->Active || ticket->Gate) return STATUS_INVALID_DEVICE_STATE;'),
        (body('Enter'), 'InterlockedCompareExchangePointer(&gate->OwnerThread, NULL, NULL) == thread'),
        (body('Enter'), 'KeEnterCriticalRegion();'),
        (body('Enter'), '!ExAcquireRundownProtection(&gate->Rundown)'),
        (body('Enter'), 'timeout.QuadPart = 0;'),
        (body('Enter'), 'KeWaitForSingleObject(&gate->Mutex, Executive, KernelMode, FALSE, &timeout)'),
        (body('Enter'), 'if (status == STATUS_TIMEOUT)'),
        (body('Enter'), 'if (status != STATUS_SUCCESS) {'),
        (body('Enter'), 'InterlockedExchange(&gate->State, BC250_DMA_GATE_FAULT);'),
        (body('Enter'), 'gate->LastId == ~(ULONGLONG)0'),
        (body('Leave'), 'ticket->Gate != gate || ticket->Active != 1U || ticket->Thread != thread'),
        (body('Leave'), 'gate->Ticket != ticket || ticket->Id != gate->LastId'),
        (body('Leave'), 'RtlZeroMemory(ticket, sizeof(*ticket));'),
        (body('Leave'), 'KeReleaseMutex(&gate->Mutex, FALSE);'),
        (body('Leave'), 'ExReleaseRundownProtection(&gate->Rundown);'),
        (body('Stop'), 'BC250_DMA_GATE_STOPPING, BC250_DMA_GATE_RUNNING'),
        (body('Quiesce'), 'InterlockedCompareExchangePointer(&gate->OwnerThread, NULL, NULL) == KeGetCurrentThread()'),
        (body('Quiesce'), 'BC250_DMA_GATE_QUIESCING, BC250_DMA_GATE_STOPPING'),
        (body('Quiesce'), 'if (state != BC250_DMA_GATE_STOPPING) return STATUS_INVALID_DEVICE_STATE;'),
        (body('Quiesce'), 'ExWaitForRundownProtectionRelease(&gate->Rundown);'),
    )
    for section, token in sections:
        if section.count(token) != 1 or section not in source: raise ValueError('Guard missing: ' + token)
    enter, leave, quiesce = body('Enter'), body('Leave'), body('Quiesce')
    if enter.count('Bc250DmaGateState(gate) != BC250_DMA_GATE_RUNNING') != 2:
        raise ValueError('Admission needs checks before and after mutex')
    if not (enter.index('KeEnterCriticalRegion(') < enter.index('ExAcquireRundownProtection(') <
            enter.index('KeWaitForSingleObject(')):
        raise ValueError('Acquire ordering')
    if not (leave.index('RtlZeroMemory(ticket') < leave.index('KeReleaseMutex(') <
            leave.index('ExReleaseRundownProtection(') < leave.index('KeLeaveCriticalRegion(')):
        raise ValueError('Release ordering')
    tail = previous.stripped(leave.split('ExReleaseRundownProtection(&gate->Rundown);', 1)[1])
    if 'gate' in tail or 'ticket' in tail: raise ValueError('Access after final release')
    if quiesce.index('ExWaitForRundownProtectionRelease(') > quiesce.index('BC250_DMA_GATE_CLOSED'):
        raise ValueError('Closed before rundown')
    for suffix in ('Init', 'Enter', 'Leave', 'Stop', 'Quiesce'):
        if body(suffix).count('Bc250DmaGateGuard(gate)') != 1: raise ValueError('Policy bypass')
    if re.search(r'Ke\w*(?:Mutex|Rundown)\(', previous.stripped(body('Stop'))):
        raise ValueError('Stop must not wait or release')
    return sections, sorted(calls)


def audit(output):
    out = Path(output).resolve(); out.relative_to((ROOT / 'output').resolve())
    if not out.is_dir(): raise ValueError('Scoped output required')
    previous.git('merge-base', '--is-ancestor', BASE, 'HEAD')
    changed = set(previous.git('diff', '--name-only', '-z', BASE).split('\0')) - {''}
    untracked = set(previous.git('ls-files', '--others', '--exclude-standard', '-z').split('\0')) - {''}
    if changed | untracked != FILES: raise ValueError('Exact eight-file scope mismatch')
    hashes = {}
    for relative in sorted(FILES):
        path = ROOT / relative
        if not path.is_file() or path.is_symlink(): raise ValueError('Regular source required')
        data = path.read_bytes(); text = data.decode('utf-8')
        if b'\0' in data or re.search(r'-----BEGIN [A-Z ]*PRIVATE KEY-----|gh[pousr]_[A-Za-z0-9]{36,}|[A-Za-z]:[\\/]Users[\\/]', text):
            raise ValueError('Private/binary export')
        hashes[relative] = hashlib.sha256(data).hexdigest()
    for name, digest in PINS.items():
        if hashes[PREFIX + name] != digest: raise ValueError('Reviewed gate changed')
    source = normalize((HERE / 'bc250_dma_gate.c').read_text(encoding='utf-8'))
    sections, calls = check_source(source)
    for section, token in sections:
        mutant = source.replace(section, section.replace(token, 'REMOVED_GUARD', 1), 1)
        try: check_source(mutant)
        except ValueError: continue
        raise ValueError('Negative control accepted')
    readme = (HERE / 'README.txt').read_text(encoding='utf-8')
    for token in ('Build11', 'NO SYS/INF/CAT', 'CREDITS.txt', 'lifetime externo',
            'NO prueba de concurrencia real', 'NO permiso de liberar DMA', 'SOLO Stop'):
        if token not in readme: raise ValueError('Scope/attribution missing')
    provenance = json.loads((HERE / 'primary-source.json').read_text(encoding='utf-8'))
    if provenance['source_baseline'] != BASE or len(provenance['primary_references']) != 8:
        raise ValueError('Provenance mismatch')
    if any(not url.startswith('https://learn.microsoft.com/') for url in provenance['primary_references']):
        raise ValueError('Non-primary reference')
    runner = (HERE / 'verify-offline.py').read_text(encoding='utf-8')
    for token in ("for name in ('CL', '_CL_')", "'/UBC250_DMA_GATE_MOCK'",
            "'/UBC250_DMA_ADAPTER_MOCK'", "'/c', '/kernel'", 'len(history) != 40'):
        if token not in runner: raise ValueError('Build isolation missing')
    result = dict(scope='WINDOWS_DMA_GATE_SOURCE_FAKE_ONLY', baseline=BASE, new_source_sha256=hashes,
        source_guard_negative_controls=len(sections), operation_inventory=calls,
        predecessors_unchanged=True, gate_driver_integrated=False,
        real_rundown_or_concurrency_validated=False, hardware_tested=False, installable_package=False)
    (out / 'AUDIT.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('PASS: eight new sources, %d guard negative controls; actual gate source, fake DDIs' % len(sections))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True)
    audit(parser.parse_args().output)
