"""Seven original RAM association sources; protected predecessors unchanged."""
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
BASE = 'fc7acd61e906f72dde9b6cf03cd2b8fd81f6577b'
PREFIX = 'research/windows/dma-map-leases-20261004/'
FILES = {PREFIX + name for name in ('bc250_dma_map_leases.h', 'test-map-leases.c',
    'audit-map-leases.py', 'verify-offline.py', 'README.txt', 'UPSTREAM-REVIEW.txt', 'primary-source.json')}
HEADER_SHA256 = 'd70f961e63a7f2c2eff6ef9e47498b2d442d07b07f8489cf1b6b71f37f8677aa'
spec = importlib.util.spec_from_file_location('domain_auditor',
    ROOT / 'research/windows/domain-backend-20261003/audit-domain-backend.py')
previous = importlib.util.module_from_spec(spec); spec.loader.exec_module(previous)


def normalize(source):
    return source.replace('static __inline BC250_VM_SESSION_STATUS ', 'static __inline int ')


def check_source(source):
    source = normalize(source)
    allowed = {'if', 'for', 'sizeof', 'defined', 'memset', 'memcmp', 'KeGetCurrentIrql',
        'Bc250DmaPageListDisjoint', 'Bc250VmSessionBufferAway', 'Bc250VmSessionCheck',
        'Bc250BackingHandleMatches', 'Bc250VmSessionMap', 'Bc250VmSessionUnmap', 'Bc250VmSessionRelease',
        'Bc250DmaLeasesRetain', 'Bc250DmaLeasesDrop', 'Bc250DmaLeasesStop', 'Bc250DmaLeasesRetire'}
    allowed |= {'Bc250Ml' + suffix for suffix in ('Guard', 'Away', 'NativeStatus', 'Check',
        'Stop', 'Init', 'DropPending', 'Map', 'Unmap', 'Drain', 'Retire')}
    calls = set(re.findall(r'\b([A-Za-z_]\w*)\s*\(', previous.stripped(source)))
    if calls - allowed: raise ValueError('Unexpected operation: ' + str(calls - allowed))
    if re.search(r'->(?:Native|Gate|Metadata|List|HeldMdl|TransferContext)\b', previous.stripped(source)):
        raise ValueError('Raw native/gate/private address access')
    for token in ('#ifdef _KERNEL_MODE', '#error CPU map lease association is NEVER kernel integrated',
            '!defined(BC250_DMA_ADAPTER_MOCK)', '!defined(BC250_DMA_GATE_MOCK)', '!defined(BC250_DMA_LEASES_MOCK)'):
        if token not in source: raise ValueError('RAM isolation bypass')
    def body(suffix): return previous.function(source, 'Bc250Ml' + suffix)
    tokens = {
        'Guard': ('!Bc250MockExecutionAllowed || !Bc250MockGateExecutionAllowed ||',
            '!Bc250MockLeasesExecutionAllowed', 'KeGetCurrentIrql() != PASSIVE_LEVEL',
            'm->Busy', 'm->State == BC250_ML_FAULT'),
        'Away': ('Bc250DmaPageListDisjoint(m, sizeof(*m), buffer, bytes)',
            'Bc250DmaPageListDisjoint(m->Resource, sizeof(*m->Resource), buffer, bytes)',
            'Bc250VmSessionBufferAway(m->Session, buffer, bytes)'),
        'Check': ('!m->Resource || !m->Session', 'r->Id > m->LastId',
            'm->BackingReleased || m->RootDropped', 'cpu->Id != r->Cpu.Id',
            'cpu->Lease.Generation != m->Backing.Generation', 'r->Phase != BC250_ML_PENDING_DROP',
            'cpuCount == recordCount'),
        'Init': ('memcmp(m, &zero, sizeof(zero))', 'Bc250VmSessionBufferAway(session, resource, sizeof(*resource))',
            'session->Mappings[i].Live', 'native = Bc250DmaLeasesRetain(resource, seed, &m->Root);'),
        'DropPending': ('native = Bc250DmaLeasesDrop(m->Resource, &r->Dma);',
            'native == STATUS_DEVICE_BUSY', 'm->State = BC250_ML_STOPPED',
            'memset(r, 0, sizeof(*r))'),
        'Map': ('m->State != BC250_ML_ACTIVE', 'memcmp(out, &zero, sizeof(zero))',
            'm->LastId == BC250_GART_U64_MAX', 'slot == 16', 'r->Id = ++m->LastId',
            'native = Bc250DmaLeasesRetain(m->Resource, &m->Root, &r->Dma);',
            'r->Dma.Owner', 'Bc250MlNativeStatus(native) == BC250_VM_SESSION_FAULT',
            'status = Bc250VmSessionMap(', 'Bc250VmSessionCheck(m->Session) != BC250_VM_SESSION_OK',
            'BC250_VM_SESSION_STATUS drop = Bc250MlDropPending(m, slot);'),
        'Unmap': ('input->Slot >= 16', 'm->Records[input->Slot].Id != input->Id',
            'r->Phase == BC250_ML_MAPPED', 'status = Bc250VmSessionUnmap(',
            'if (status != BC250_VM_SESSION_OK) {\n            if (Bc250VmSessionCheck', 'memset(&r->Cpu, 0, sizeof(r->Cpu));',
            'status = Bc250MlDropPending(m, input->Slot);'),
        'Drain': ('m->Records[i].Phase == BC250_ML_PENDING_DROP', 'status = Bc250MlDropPending(m, i);'),
        'Retire': ('status = Bc250MlDrain(m);',
            'if (m->Records[i].Phase) return BC250_VM_SESSION_IN_USE;',
            '!m->BackingReleased', 'status = Bc250VmSessionRelease(m->Session, &m->Backing);',
            'm->BackingReleased = 1U', '!m->RootDropped',
            'native = Bc250DmaLeasesDrop(m->Resource, &m->Root);',
            'm->RootDropped = 1U', 'native = Bc250DmaLeasesRetire(m->Resource);',
            'm->State = BC250_ML_DEAD')
    }
    sections = [(body(suffix), token) for suffix, entries in tokens.items() for token in entries]
    for section, token in sections:
        if section.count(token) != 1 or section not in source: raise ValueError('Guard missing: ' + token)
    def ordered(section, *items):
        positions = [section.index(item) for item in items]
        if positions != sorted(positions) or len(set(positions)) != len(positions):
            raise ValueError('Safety ordering changed')
    ordered(body('Map'), 'Bc250DmaLeasesRetain(', 'Bc250VmSessionMap(', 'BC250_ML_MAPPED', '*out = result')
    ordered(body('Unmap'), 'Bc250VmSessionUnmap(', 'memset(&r->Cpu', 'Bc250MlDropPending(')
    ordered(body('Retire'), 'Bc250MlDrain(', 'if (m->Records[i].Phase)',
        'Bc250VmSessionRelease(', 'Bc250DmaLeasesDrop(', 'Bc250DmaLeasesRetire(')
    if 'Bc250DmaLeasesDrop' in previous.stripped(body('Stop')) or 'Bc250VmSession' in previous.stripped(body('Stop')):
        raise ValueError('Stop must not drop/mutate CPU maps')
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
    if hashes[PREFIX + 'bc250_dma_map_leases.h'] != HEADER_SHA256: raise ValueError('Reviewed header changed')
    source = normalize((HERE / 'bc250_dma_map_leases.h').read_text(encoding='utf-8'))
    sections, calls = check_source(source)
    for section, token in sections:
        mutant = source.replace(section, section.replace(token, 'REMOVED_GUARD', 1), 1)
        try: check_source(mutant)
        except ValueError: continue
        raise ValueError('Negative control accepted')
    readme = (HERE / 'README.txt').read_text(encoding='utf-8')
    for token in ('Build11', 'NO SYS/INF/CAT', 'CREDITS.txt', 'lifetime externo',
            'NO prueba de concurrencia real', 'SINTETICOS', 'PENDING_DROP', 'BackingReleased', 'RootDropped'):
        if token not in readme: raise ValueError('Scope/attribution missing')
    provenance = json.loads((HERE / 'primary-source.json').read_text(encoding='utf-8'))
    if provenance['source_baseline'] != BASE or len(provenance['local_sources']) != 5:
        raise ValueError('Provenance mismatch')
    for relative in provenance['local_sources']:
        if not (ROOT / relative).is_file(): raise ValueError('Protected source missing')
    if provenance['upstream']['implementation_copied'] is not False: raise ValueError('Unexpected external import')
    runner = (HERE / 'verify-offline.py').read_text(encoding='utf-8')
    for token in ("for name in ('CL', '_CL_')", "'/UBC250_DMA_LEASES_MOCK'",
            'len(history) != 48', 'len(tests) != 21', 'len(artifacts) != 25'):
        if token not in runner: raise ValueError('Build isolation missing')
    if "str(HERE / 'bc250_dma_map_leases.h')" in runner: raise ValueError('RAM facade compiled kernel')
    result = dict(scope='RAM_CPU_MAP_LEASE_ASSOCIATION_SOURCE_ONLY', baseline=BASE, new_source_sha256=hashes,
        source_guard_negative_controls=len(sections), operation_inventory=calls,
        predecessors_unchanged=True, driver_integrated=False, real_map_consumers_supported=False,
        synthetic_pages_prove_dma_binding=False, real_concurrency_validated=False, installable_package=False)
    (out / 'AUDIT.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('PASS: seven original sources, %d guard mutations; RAM association only' % len(sections))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True)
    audit(parser.parse_args().output)
