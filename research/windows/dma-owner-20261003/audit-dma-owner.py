"""Simulated DMA-owner source audit. Not Windows resource/IRQL/GPU validation."""
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
BASE = 'e4c9e605eb8c06c1bcf0368d6d2621c75bc373b0'
HEADER = 'inc/dma-owner-20261003/bc250_dma_owner.h'
PREFIX = 'research/windows/dma-owner-20261003/'
FILES = {HEADER} | {PREFIX + name for name in ('test-dma-owner.c',
    'verify-offline.py', 'audit-dma-owner.py', 'README.txt', 'primary-source.json')}
HEADER_SHA256 = '96dd05bee0d152447241c45b431d7fff7adf4718b0d5cdfd20cd7e507effee1c'
spec = importlib.util.spec_from_file_location('domain_auditor',
    ROOT / 'research/windows/domain-backend-20261003/audit-domain-backend.py')
previous = importlib.util.module_from_spec(spec)
spec.loader.exec_module(previous)


def normalize(source):
    return source.replace('static __inline BC250_DMA_OWNER_DELIVERY_RESULT ',
        'static __inline int ').replace('static __inline void ', 'static __inline int ')


def check_source(source):
    calls = set(re.findall(r'\b([A-Za-z_]\w*)\s*\(', previous.stripped(source)))
    allowed = {'if', 'sizeof', 'memcmp', 'memset', 'void', 'Put',
        'Bc250DmaOwnerCheck', 'Bc250DmaOwnerAway', 'Bc250DmaOwnerTicketCheck',
        'Bc250DmaOwnerIdle', 'Bc250DmaOwnerInit', 'Bc250DmaOwnerBegin',
        'Bc250DmaOwnerCancel', 'Bc250DmaOwnerResolveNoResource',
        'Bc250DmaOwnerDeliver', 'Bc250DmaOwnerMap', 'Bc250DmaOwnerUnmap',
        'Bc250DmaOwnerRelease', 'Bc250DmaOwnerShutdown', 'Bc250VmSessionCheck',
        'Bc250VmSessionBufferAway', 'Bc250BackingHandleMatches',
        'Bc250DmaPageListDisjoint', 'Bc250VmDomainRegisterRuns',
        'Bc250VmDomainMap', 'Bc250VmSessionUnmap', 'Bc250VmSessionRelease'}
    if calls - allowed: raise ValueError('Unexpected operation: ' + str(calls - allowed))
    def body(name): return previous.function(normalize(source), name)
    check, ticket, begin = (body('Bc250DmaOwner' + name) for name in ('Check', 'TicketCheck', 'Begin'))
    resolve, deliver, mapping, unmap, release, shutdown = (
        body('Bc250DmaOwner' + name) for name in ('ResolveNoResource', 'Deliver', 'Map', 'Unmap', 'Release', 'Shutdown'))
    sections = (
        (check, 'if (owner->Busy) return BC250_VM_SESSION_BUSY_RESULT;'),
        (check, 'if (owner->State == BC250_DMA_OWNER_DEAD) return BC250_VM_SESSION_DEAD_RESULT;'),
        (check, 'Bc250VmSessionBufferAway(owner->Session, owner, sizeof(*owner))'),
        (ticket, 'ticket->Id && ticket->Id == owner->LastId'),
        (begin, 'bytes > 64ULL * 4096ULL'),
        (begin, 'write_to_device > 1U || !Bc250DmaOwnerAway(owner, out, sizeof(*out))'),
        (begin, 'if (owner->LastId == BC250_GART_U64_MAX) return BC250_VM_SESSION_EXHAUSTED;'),
        (resolve, 'owner->State != BC250_DMA_OWNER_PENDING && owner->State != BC250_DMA_OWNER_CANCEL_WAIT'),
        (deliver, 'result.Status = status; result.Consumed = 0U;'),
        (deliver, 'if (!cookie) { result.Status = BC250_VM_SESSION_INVALID; return result; }'),
        (deliver, 'owner->Busy = 1U; result.Consumed = 1U;'),
        (deliver, 'owner->Put(owner->ProviderContext, cookie, owner->WriteToDevice);'),
        (mapping, 'if (owner->State != BC250_DMA_OWNER_READY) return BC250_VM_SESSION_IN_USE;'),
        (mapping, 'Bc250DmaPageListDisjoint(ticket, sizeof(*ticket), out, sizeof(*out))'),
        (unmap, 'record->Lease.Slot != owner->Backing.Slot'),
        (unmap, 'record->Lease.Generation != owner->Backing.Generation'),
        (release, 'status = Bc250VmSessionRelease(owner->Session, &owner->Backing);'),
        (release, 'if (status == BC250_VM_SESSION_OK)'),
        (shutdown, 'if (owner->State != BC250_DMA_OWNER_IDLE) return BC250_VM_SESSION_IN_USE;'),
    )
    for section, token in sections:
        if token not in section: raise ValueError('Guard missing: ' + token)
    if deliver.index('owner->Busy = 1U; result.Consumed = 1U;') > deliver.index('owner->Put('):
        raise ValueError('Reentrant Put before ownership/BUSY')
    if release.index('status = Bc250VmSessionRelease(') > release.index('owner->Put('):
        raise ValueError('Put before unregister')
    if 'owner->LastId = 0' in source or 'memset(owner,' in source:
        raise ValueError('Owner identity reset')
    return sections, sorted(calls)


def audit(output):
    out = Path(output).resolve()
    out.relative_to((ROOT / 'output').resolve())
    if not out.is_dir(): raise ValueError('Existing scoped output folder required')
    previous.git('merge-base', '--is-ancestor', BASE, 'HEAD')
    changed = set(previous.git('diff', '--name-only', '-z', BASE).split('\0')) - {''}
    untracked = set(previous.git('ls-files', '--others', '--exclude-standard', '-z').split('\0')) - {''}
    if changed | untracked != FILES:
        raise ValueError('Exact six-file scope mismatch: ' + str(sorted((changed | untracked) ^ FILES)))
    hashes = {}
    for relative in sorted(FILES):
        path = ROOT / relative
        if not path.is_file() or path.is_symlink(): raise ValueError('Not regular source: ' + relative)
        data = path.read_bytes(); text = data.decode('utf-8')
        if b'\0' in data or re.search(r'-----BEGIN [A-Z ]*PRIVATE KEY-----|gh[pousr]_[A-Za-z0-9]{36,}|[A-Za-z]:[\\/]Users[\\/]', text):
            raise ValueError('Private or binary export')
        hashes[relative] = hashlib.sha256(data).hexdigest()
    if hashes[HEADER] != HEADER_SHA256: raise ValueError('Reviewed header changed')
    source = (ROOT / HEADER).read_text(encoding='utf-8')
    sections, calls = check_source(source)
    normalized = normalize(source).replace('static __inline BC250_VM_SESSION_STATUS ', 'static __inline int ')
    for section, token in sections:
        if section.count(token) != 1 or section not in normalized: raise ValueError('Negative control target invalid')
        mutant = normalized.replace(section, section.replace(token, 'REMOVED_CHECK', 1), 1)
        try: check_source(mutant)
        except ValueError: continue
        raise ValueError('Source guard negative control accepted')
    readme = (HERE / 'README.txt').read_text(encoding='utf-8')
    for token in ('SOLO RAM', 'CREDITS.txt', 'NO lock', 'Consumed=0', 'FreeAdapterObject', 'Build11'):
        if token not in readme: raise ValueError('Scope/ownership/attribution missing')
    provenance = json.loads((HERE / 'primary-source.json').read_text(encoding='utf-8'))
    if len(provenance['sources']) != 3 or any(not item['url'].startswith('https://learn.microsoft.com/en-us/windows-hardware/drivers/') for item in provenance['sources']):
        raise ValueError('Primary Windows references missing')
    result = dict(scope='SIMULATED_DMA_OWNER_ONLY', baseline=BASE,
        protected_implementation_unchanged=True, new_source_sha256=hashes,
        operation_inventory=calls, source_guard_negative_controls=len(sections),
        windows_dma_owner=False, irql_or_concurrency_validated=False,
        driver_loaded=False, installable_package=False, hardware_tested=False,
        w2p_memory_ownership=False, copied_external_implementation=False)
    (out / 'AUDIT.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('PASS: six new files only; 19 source guard negative controls; provider simulated, no Windows DMA APIs')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True)
    audit(parser.parse_args().output)
