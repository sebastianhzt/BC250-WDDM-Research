"""Six-file RAM lifecycle audit; predecessors and installed driver untouched."""
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
BASE = '43f826dbadbab8bf9dd0d4b606e1bfb5d2ef82b6'
PREFIX = 'research/windows/dma-lifecycle-20261004/'
FILES = {PREFIX + name for name in ('bc250_dma_lifecycle.h', 'test-lifecycle.c',
    'audit-lifecycle.py', 'verify-offline.py', 'README.txt', 'primary-source.json')}
HEADER_SHA256 = 'e9fc7472b6ddefb62b0ba21f2825ab52ea5f04979106bf67130917b4cc0e9faf'
spec = importlib.util.spec_from_file_location('domain_auditor',
    ROOT / 'research/windows/domain-backend-20261003/audit-domain-backend.py')
previous = importlib.util.module_from_spec(spec); spec.loader.exec_module(previous)


def normalize(source):
    return source.replace('static __inline BC250_VM_SESSION_STATUS ', 'static __inline int ')


def check_source(source):
    source = normalize(source)
    allowed = {'if', 'for', 'sizeof', 'memcmp', 'memset', 'KeGetCurrentIrql',
        'Bc250DmaPageListDisjoint', 'Bc250DmaBridgeAway', 'Bc250DmaBridgeCheck',
        'Bc250DmaBridgeBegin', 'Bc250DmaBridgeAcquire', 'Bc250DmaBridgePublish',
        'Bc250DmaBridgeMap', 'Bc250DmaBridgeUnmap', 'Bc250DmaBridgeCancel',
        'Bc250DmaBridgeRelease', 'Bc250DmaBridgeClose'}
    allowed |= {'Bc250DmaLife' + suffix for suffix in ('Meta', 'Guard', 'Away', 'Init',
        'Enter', 'Ticket', 'End', 'RequestStop', 'CallCheck', 'Finish', 'Start', 'Map', 'Unmap', 'Drain')}
    calls = set(re.findall(r'\b([A-Za-z_]\w*)\s*\(', previous.stripped(source)))
    if calls - allowed: raise ValueError('Unexpected lifecycle operation: ' + str(calls - allowed))
    def body(suffix): return previous.function(source, 'Bc250DmaLife' + suffix)
    sections = (
        (source, '#include "../dma-bridge-20261004/bc250_dma_bridge.h"'),
        (body('Meta'), '!Bc250MockExecutionAllowed || KeGetCurrentIrql() != PASSIVE_LEVEL || !life'),
        (body('Meta'), 'record->Used > 1U'),
        (body('Meta'), 'life->Records[j].Id == record->Id'),
        (body('Meta'), 'count == life->Active'),
        (body('Guard'), 'if (life->Busy) return BC250_VM_SESSION_BUSY_RESULT;'),
        (body('Guard'), 'if (life->State == BC250_DMA_LIFE_REMOVED) return BC250_VM_SESSION_DEAD_RESULT;'),
        (body('Guard'), 'if (life->State == BC250_DMA_LIFE_FAULT) return BC250_VM_SESSION_FAULT;'),
        (body('Away'), 'Bc250DmaPageListDisjoint(life, sizeof(*life), buffer, bytes)'),
        (body('Init'), 'if (memcmp(life, &zero, sizeof(zero))) return BC250_VM_SESSION_INVALID;'),
        (body('Init'), '!Bc250DmaBridgeAway(bridge, life, sizeof(*life))'),
        (body('Enter'), 'life->State != BC250_DMA_LIFE_RUNNING && kind != BC250_DMA_LIFE_UNMAP'),
        (body('Enter'), 'life->LastId == BC250_GART_U64_MAX'),
        (body('Enter'), 'if (i == BC250_DMA_LIFE_SLOTS) return BC250_VM_SESSION_FULL;'),
        (body('Ticket'), 'ticket->Life != life || ticket->Slot >= BC250_DMA_LIFE_SLOTS'),
        (body('Ticket'), 'life->Records[ticket->Slot].Id != ticket->Id'),
        (body('End'), 'if (life->Busy) return BC250_VM_SESSION_BUSY_RESULT;'),
        (body('End'), '--life->Active;'),
        (body('RequestStop'), 'if (life->State == BC250_DMA_LIFE_RUNNING) life->State = BC250_DMA_LIFE_STOPPING;'),
        (body('CallCheck'), 'life->Records[ticket->Slot].Kind != kind || life->Records[ticket->Slot].Used'),
        (body('Finish'), 'life->Bridge->State == BC250_DMA_BRIDGE_FAULT || status == BC250_VM_SESSION_CORRUPT'),
        (body('Start'), 'life->Records[ticket->Slot].Used = 1U; life->Busy = 1U;'),
        (body('Map'), '!Bc250DmaLifeAway(life, out, sizeof(*out))'),
        (body('Map'), '!Bc250DmaPageListDisjoint(ticket, sizeof(*ticket), journal, sizeof(*journal))'),
        (body('Unmap'), '!Bc250DmaLifeAway(life, mapping, sizeof(*mapping))'),
        (body('Unmap'), 'life->Records[ticket->Slot].Used = 1U; life->Busy = 1U;'),
        (body('Drain'), 'if (life->Active) return BC250_VM_SESSION_IN_USE;'),
        (body('Drain'), 'life->Bridge->Owner.State == BC250_DMA_OWNER_DRAINING'),
        (body('Drain'), 'if (status == BC250_VM_SESSION_OK) life->State = BC250_DMA_LIFE_REMOVED;'),
    )
    for section, token in sections:
        if section.count(token) != 1 or section not in source: raise ValueError('Guard missing: ' + token)
    drain = body('Drain')
    if not (drain.index('if (life->Active)') < drain.index('Bc250DmaBridgeCancel(') <
            drain.index('Bc250DmaBridgeRelease(') < drain.index('Bc250DmaBridgeClose(')):
        raise ValueError('Retirement ordering changed')
    for suffix in ('RequestStop', 'End'):
        if re.search(r'Bc250DmaBridge\w*\(', previous.stripped(body(suffix))):
            raise ValueError('Stop/End must never touch provider')
    if source.count('life->State = BC250_DMA_LIFE_RUNNING') != 1:
        raise ValueError('Restart is not implemented')
    return sections, sorted(calls)


def audit(output):
    out = Path(output).resolve(); out.relative_to((ROOT / 'output').resolve())
    if not out.is_dir(): raise ValueError('Scoped output required')
    previous.git('merge-base', '--is-ancestor', BASE, 'HEAD')
    changed = set(previous.git('diff', '--name-only', '-z', BASE).split('\0')) - {''}
    untracked = set(previous.git('ls-files', '--others', '--exclude-standard', '-z').split('\0')) - {''}
    if changed | untracked != FILES: raise ValueError('Exact six-file scope mismatch')
    hashes = {}
    for relative in sorted(FILES):
        path = ROOT / relative
        if not path.is_file() or path.is_symlink(): raise ValueError('Regular source required')
        data = path.read_bytes(); text = data.decode('utf-8')
        if b'\0' in data or re.search(r'-----BEGIN [A-Z ]*PRIVATE KEY-----|gh[pousr]_[A-Za-z0-9]{36,}|[A-Za-z]:[\\/]Users[\\/]', text):
            raise ValueError('Private/binary export')
        hashes[relative] = hashlib.sha256(data).hexdigest()
    if hashes[PREFIX + 'bc250_dma_lifecycle.h'] != HEADER_SHA256: raise ValueError('Reviewed lifecycle changed')
    source = normalize((HERE / 'bc250_dma_lifecycle.h').read_text(encoding='utf-8'))
    sections, calls = check_source(source)
    for section, token in sections:
        mutant = source.replace(section, section.replace(token, 'REMOVED_GUARD', 1), 1)
        try: check_source(mutant)
        except ValueError: continue
        raise ValueError('Negative control accepted')
    readme = (HERE / 'README.txt').read_text(encoding='utf-8')
    for token in ('SOLO RAM', 'Build11', 'NO lock', 'NO SYS/INF/CAT', 'CREDITS.txt',
            'tickets sin usar', 'trabajo ya admitido', 'NO prueba de reposo GPU'):
        if token not in readme: raise ValueError('Scope/attribution missing')
    provenance = json.loads((HERE / 'primary-source.json').read_text(encoding='utf-8'))
    if provenance['source_baseline'] != BASE or len(provenance['local_sources']) != 6:
        raise ValueError('Protected local sources missing')
    for relative in provenance['local_sources']:
        if not (ROOT / relative).is_file(): raise ValueError('Missing protected source')
    runner = (HERE / 'verify-offline.py').read_text(encoding='utf-8')
    for token in ("for name in ('CL', '_CL_')", "'/UBC250_DMA_ADAPTER_MOCK'",
            "'/c', '/kernel'", 'len(history) != 37'):
        if token not in runner: raise ValueError('Build isolation missing')
    result = dict(scope='RAM_ONLY_DMA_LIFECYCLE', baseline=BASE, new_source_sha256=hashes,
        source_guard_negative_controls=len(sections), operation_inventory=calls,
        predecessors_unchanged=True, lifecycle_kernel_integrated=False,
        real_pnp_rundown_validated=False, hardware_tested=False, installable_package=False)
    (out / 'AUDIT.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('PASS: six new sources, %d guard negative controls; RAM lifecycle only' % len(sections))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True)
    audit(parser.parse_args().output)
