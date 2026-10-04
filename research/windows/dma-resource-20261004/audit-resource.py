"""Seven original composition sources; protected predecessors unchanged."""
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
BASE = '43f7fffba6d85021f537c1c09305bcbe49569b89'
PREFIX = 'research/windows/dma-resource-20261004/'
FILES = {PREFIX + name for name in ('bc250_dma_resource.c', 'bc250_dma_resource.h',
    'test-resource.c', 'audit-resource.py', 'verify-offline.py', 'README.txt', 'primary-source.json')}
PINS = {'bc250_dma_resource.c': '8be474a4a020c32bb284430a1a90b37e3d66e30df9ddf34e33a96e480fc1bc32',
    'bc250_dma_resource.h': '4e93a11eb27c18c9203324f7be3e194047f07c44d1bcd08a6fa2160f53e48548'}
spec = importlib.util.spec_from_file_location('domain_auditor',
    ROOT / 'research/windows/domain-backend-20261003/audit-domain-backend.py')
previous = importlib.util.module_from_spec(spec); spec.loader.exec_module(previous)


def normalize(source):
    for kind in ('BOOLEAN', 'NTSTATUS'):
        source = source.replace('static ' + kind + ' ', 'static __inline int ')
    return source.replace('NTSTATUS Bc250DmaResource', 'static __inline int Bc250DmaResource')


def check_source(source):
    source = normalize(source)
    allowed = {'if', 'sizeof', 'RtlZeroMemory', 'RtlCompareMemory', 'KeGetCurrentIrql',
        'KeGetCurrentThread', 'InterlockedCompareExchange', 'InterlockedExchange',
        'InterlockedCompareExchangePointer', 'Bc250DmaGateInit', 'Bc250DmaGateEnter',
        'Bc250DmaGateLeave', 'Bc250DmaGateStop', 'Bc250DmaGateQuiesce',
        'Bc250WinDmaOpen', 'Bc250WinDmaAcquire', 'Bc250WinDmaRelease',
        'Bc250WinDmaCancel', 'Bc250WinDmaClose'}
    allowed |= {'Bc250DmaResource' + suffix for suffix in ('Policy', 'Guard', 'Away',
        'Admit', 'Finish', 'Init', 'Acquire', 'Release', 'Stop', 'Retire')}
    calls = set(re.findall(r'\b([A-Za-z_]\w*)\s*\(', previous.stripped(source)))
    if calls - allowed: raise ValueError('Unexpected resource operation: ' + str(calls - allowed))
    def body(suffix): return previous.function(source, 'Bc250DmaResource' + suffix)
    sections = (
        (body('Policy'), '#ifdef BC250_DMA_RESOURCE_MOCK'),
        (body('Policy'), 'return FALSE;'),
        (body('Guard'), 'if (!Bc250DmaResourcePolicy()) return STATUS_NOT_SUPPORTED;'),
        (body('Guard'), 'KeGetCurrentIrql() != PASSIVE_LEVEL'),
        (body('Away'), 'a > maximum - (sizeof(*resource) - 1U)'),
        (body('Admit'), 'resource->Retirement, 0, 0) != BC250_DMA_RESOURCE_ACTIVE'),
        (body('Admit'), 'status = Bc250DmaGateEnter(&resource->Gate, ticket);'),
        (body('Admit'), 'resource->Gate.State, 0, 0) == BC250_DMA_GATE_FAULT'),
        (body('Finish'), 'resource->Native.State == BC250_WIN_DMA_QUARANTINED'),
        (body('Finish'), 'Bc250DmaGateStop(&resource->Gate);'),
        (body('Finish'), 'leave = Bc250DmaGateLeave(&resource->Gate, ticket);'),
        (body('Finish'), 'if (leave != STATUS_SUCCESS) {'),
        (body('Init'), 'RtlCompareMemory(resource, &zero, sizeof(zero)) != sizeof(zero)'),
        (body('Init'), '!Bc250DmaResourceAway(resource, description, sizeof(*description))'),
        (body('Init'), 'status = Bc250DmaGateInit(&resource->Gate);'),
        (body('Init'), 'status = Bc250WinDmaOpen(&resource->Native, pdo, description);'),
        (body('Acquire'), '!Bc250DmaResourceAway(resource, mdl, sizeof(*mdl))'),
        (body('Acquire'), 'status = Bc250WinDmaAcquire(&resource->Native, mdl, offset, bytes, direction);'),
        (body('Release'), 'status = Bc250WinDmaRelease(&resource->Native);'),
        (body('Stop'), 'return Bc250DmaGateStop(&resource->Gate);'),
        (body('Retire'), 'resource->Gate.OwnerThread, NULL, NULL) == KeGetCurrentThread()'),
        (body('Retire'), 'BC250_DMA_RESOURCE_RETIRING,\n        BC250_DMA_RESOURCE_ACTIVE'),
        (body('Retire'), 'if (retirement == BC250_DMA_RESOURCE_RETIRING) return STATUS_DEVICE_BUSY;'),
        (body('Retire'), 'status = Bc250DmaGateQuiesce(&resource->Gate);'),
        (body('Retire'), 'resource->Retirement, 0, 0) == BC250_DMA_RESOURCE_FAULT'),
        (body('Retire'), 'RtlCompareMemory(&resource->Native, &zero, sizeof(zero)) != sizeof(zero)'),
        (body('Retire'), 'resource->Native.State != BC250_WIN_DMA_OPEN && resource->Native.State != BC250_WIN_DMA_HELD'),
        (body('Retire'), 'status = Bc250WinDmaCancel(&resource->Native);'),
        (body('Retire'), 'if (status == STATUS_SUCCESS) status = Bc250WinDmaClose(&resource->Native);'),
        (body('Retire'), 'InterlockedExchange(&resource->Retirement, BC250_DMA_RESOURCE_RETIRED);'),
    )
    for section, token in sections:
        if section.count(token) != 1 or section not in source: raise ValueError('Guard missing: ' + token)
    finish, retire = body('Finish'), body('Retire')
    if finish.index('BC250_WIN_DMA_QUARANTINED') > finish.index('Bc250DmaGateLeave('):
        raise ValueError('Fault must be published before Leave')
    if not (retire.index('Bc250DmaGateQuiesce(') < retire.index('resource->Retirement, 0, 0)') <
            retire.index('Bc250WinDmaCancel(') < retire.index('Bc250WinDmaClose(') < retire.index('BC250_DMA_RESOURCE_RETIRED')):
        raise ValueError('Rundown/fault/cleanup ordering changed')
    if 'Native' in previous.stripped(body('Stop')): raise ValueError('Stop must not touch Native')
    for suffix in ('Init', 'Acquire', 'Release'):
        section = body(suffix)
        if section.index('Bc250DmaResourceAdmit(') > section.index('Bc250WinDma'):
            raise ValueError('Native call before admission')
        if 'return Bc250DmaResourceFinish(resource, &ticket, status);' not in section:
            raise ValueError('Protected completion missing')
    for suffix in ('Init', 'Acquire', 'Release', 'Stop', 'Retire'):
        if body(suffix).count('Bc250DmaResourceGuard(resource)') != 1: raise ValueError('Policy bypass')
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
        if hashes[PREFIX + name] != digest: raise ValueError('Reviewed resource changed')
    source = normalize((HERE / 'bc250_dma_resource.c').read_text(encoding='utf-8'))
    sections, calls = check_source(source)
    for section, token in sections:
        mutant = source.replace(section, section.replace(token, 'REMOVED_GUARD', 1), 1)
        try: check_source(mutant)
        except ValueError: continue
        raise ValueError('Negative control accepted')
    readme = (HERE / 'README.txt').read_text(encoding='utf-8')
    for token in ('Build11', 'NO SYS/INF/CAT', 'CREDITS.txt', 'lifetime externo',
            'NO prueba de concurrencia real', 'NO consumidores', 'SOLO Stop'):
        if token not in readme: raise ValueError('Scope/attribution missing')
    provenance = json.loads((HERE / 'primary-source.json').read_text(encoding='utf-8'))
    if provenance['source_baseline'] != BASE or len(provenance['local_sources']) != 6:
        raise ValueError('Provenance mismatch')
    for relative in provenance['local_sources']:
        if not (ROOT / relative).is_file(): raise ValueError('Protected source missing')
    runner = (HERE / 'verify-offline.py').read_text(encoding='utf-8')
    for token in ("for name in ('CL', '_CL_')", "'/UBC250_DMA_RESOURCE_MOCK'",
            "'/UBC250_DMA_GATE_MOCK'", "'/UBC250_DMA_ADAPTER_MOCK'", 'len(history) != 43'):
        if token not in runner: raise ValueError('Build isolation missing')
    result = dict(scope='WINDOWS_DMA_RESOURCE_SOURCE_FAKE_ONLY', baseline=BASE, new_source_sha256=hashes,
        source_guard_negative_controls=len(sections), operation_inventory=calls,
        predecessors_unchanged=True, resource_driver_integrated=False, map_consumers_supported=False,
        real_rundown_or_concurrency_validated=False, hardware_tested=False, installable_package=False)
    (out / 'AUDIT.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('PASS: seven new sources, %d guard negative controls; composed source, fake DDIs' % len(sections))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True)
    audit(parser.parse_args().output)
