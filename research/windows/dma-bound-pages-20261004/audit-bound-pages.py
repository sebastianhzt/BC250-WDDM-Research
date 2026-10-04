"""Exact six original fixture-only sources; frozen driver and predecessors unchanged."""
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
BASE = '2116cfb83eb772d3553e7ed7b10cc500177d0dfb'
PREFIX = 'research/windows/dma-bound-pages-20261004/'
FILES = {PREFIX + name for name in ('fixture-bound-pages.h', 'test-bound-pages.c',
    'audit-bound-pages.py', 'verify-offline.py', 'README.txt', 'primary-source.json')}
HEADER_SHA256 = 'a018288ce8bb1278610c717b95ee21013f1877c68027898e643b23ea5bbf91c3'
spec = importlib.util.spec_from_file_location('domain_auditor',
    ROOT / 'research/windows/domain-backend-20261003/audit-domain-backend.py')
previous = importlib.util.module_from_spec(spec); spec.loader.exec_module(previous)


def normalize(source):
    return source.replace('static __inline NTSTATUS ', 'static __inline int ').replace(
        'static __inline BC250_VM_SESSION_STATUS ', 'static __inline int ')


def check_source(source):
    source = normalize(source)
    allowed = {'if', 'for', 'sizeof', 'defined', 'memset', 'memcmp', 'FIELD_OFFSET',
        'KeGetCurrentIrql', 'Bc250DmaPageListDisjoint', 'Bc250VmSessionBufferAway',
        'Bc250VmSessionCheck', 'Bc250BackingHandleMatches', 'Bc250VmSessionRelease',
        'Bc250VmDomainSpanValid', 'Bc250VmDomainRegister', 'Bc250DmaRunsExpand',
        'Bc250DmaLeasesRetain', 'Bc250DmaLeasesDrop', 'Bc250DmaLeasesAdmit',
        'Bc250DmaLeasesLock', 'Bc250DmaLeasesCheck', 'Bc250DmaLeasesMatch',
        'Bc250DmaLeasesBuffer', 'Bc250DmaLeasesDisjoint', 'Bc250DmaLeasesUnlock',
        'Bc250DmaLeasesFinish', 'Bc250MlNativeStatus', 'Bc250MlInit', 'Bc250MlMap', 'Bc250MlCheck'}
    allowed |= {'Bc250Bp' + suffix for suffix in ('Policy', 'Guard', 'Read', 'Capture',
        'BackingMatches', 'Register', 'Attach', 'Map', 'Release')}
    calls = set(re.findall(r'\b([A-Za-z_]\w*)\s*\(', previous.stripped(source)))
    if calls - allowed: raise ValueError('Unexpected operation: ' + str(calls - allowed))
    for token in ('#ifdef _KERNEL_MODE', '#error Bound page introspection is NEVER kernel integrated',
            '!defined(BC250_DMA_ADAPTER_MOCK)', '!defined(BC250_DMA_GATE_MOCK)', '!defined(BC250_DMA_LEASES_MOCK)'):
        if token not in source: raise ValueError('Fixture-only isolation bypass')
    def body(suffix): return previous.function(source, 'Bc250Bp' + suffix)
    tokens = {
        'Policy': ('!Bc250MockExecutionAllowed || !Bc250MockGateExecutionAllowed ||',
            '!Bc250MockLeasesExecutionAllowed', 'KeGetCurrentIrql() != PASSIVE_LEVEL'),
        'Guard': ('Bc250BpPolicy()', 's->Self != s', 's->Busy',
            's->State == BC250_BP_FAULT', 's->State == BC250_BP_RELEASED'),
        'Read': ('Bc250BpPolicy() != BC250_VM_SESSION_OK', 's->Self != s', 's->Busy != 1U',
            'Bc250DmaLeasesAdmit(resource, &ticket)', 'Bc250DmaLeasesLock(resource)',
            'Bc250DmaLeasesCheck(resource)', 'Bc250DmaLeasesMatch(resource, &s->Hold)',
            'Bc250DmaLeasesBuffer(resource, s, sizeof(*s))',
            'Bc250DmaLeasesBuffer(resource, out, sizeof(*out))',
            'Bc250DmaLeasesDisjoint(s, sizeof(*s), out, sizeof(*out))',
            'resource->Native.State != BC250_WIN_DMA_HELD', 'resource->Native.Busy',
            '!resource->Native.RequestIssued', 'resource->Native.Cancelled',
            'resource->PinnedList != resource->Native.List',
            'resource->PinnedMdl != resource->Native.HeldMdl',
            '!resource->Native.AddressWidth || resource->Native.AddressWidth > 48U',
            'sg = resource->PinnedList; count = sg->NumberOfElements;',
            '!count || count > BC250_VM_CPU_MAX_BATCH',
            'count > sizeof(sg->Elements) / sizeof(sg->Elements[0])',
            'resource->PinnedListBytes != FIELD_OFFSET(SCATTER_GATHER_LIST, Elements)',
            'runs[i].Domain = BC250_AD_DMA_LOGICAL',
            '!runs[i].Bytes || runs[i].Start > result.MaximumLast',
            'runs[i].Bytes - 1ULL > result.MaximumLast - runs[i].Start',
            'result.Bytes > resource->Native.MaximumLength',
            'runs[i].Bytes > resource->Native.MaximumLength - result.Bytes',
            'Bc250DmaRunsExpand(', 'if (status == STATUS_SUCCESS) *out = result;',
            'Bc250DmaLeasesUnlock(resource)', 'Bc250DmaLeasesFinish(resource, &ticket, status)'),
        'Capture': ('Bc250BpPolicy()', 'memcmp(s, &zero, sizeof(zero))',
            'Bc250DmaPageListDisjoint(s, sizeof(*s), resource, sizeof(*resource))',
            'Bc250DmaPageListDisjoint(s, sizeof(*s), seed, sizeof(*seed))',
            'Bc250DmaLeasesRetain(resource, seed, &s->Hold)', 'Bc250BpRead(s, &sample)',
            's->Sample = sample; s->State = BC250_BP_CAPTURED', 'else s->State = BC250_BP_FAULT'),
        'BackingMatches': ('Bc250BackingHandleMatches(&s->Session->Backing, &s->Backing)',
            'r->PageCount != s->Sample.Count', 'Bc250VmDomainSpanValid(',
            'page->Domain != BC250_ADDRESS_DMA_LOGICAL', 'page->Start != s->Sample.Pages[i].Start',
            'page->Bytes != s->Sample.Pages[i].Bytes', 'page->Last != s->Sample.Pages[i].Last'),
        'Register': ('Bc250BpGuard(s)', 's->State != BC250_BP_CAPTURED',
            'session->Mappings[i].Live', 'Bc250BpRead(s, &fresh)',
            'memcmp(&fresh, &s->Sample, sizeof(fresh))', 'Bc250VmDomainRegister(',
            's->State = BC250_BP_REGISTERED', 'else s->Session = NULL'),
        'Attach': ('Bc250BpGuard(s)', '!Bc250BpBackingMatches(s)', 'Bc250MlInit(',
            's->State = BC250_BP_ATTACHED', 'else s->State = BC250_BP_FAULT'),
        'Map': ('Bc250BpGuard(s)', '!Bc250BpBackingMatches(s)',
            'Bc250DmaPageListDisjoint(s, sizeof(*s), out, sizeof(*out))',
            'Bc250DmaPageListDisjoint(s, sizeof(*s), journal, sizeof(*journal))',
            's->Busy = 1U', 'Bc250MlMap(', 's->Busy = 0U'),
        'Release': ('Bc250BpGuard(s)', 'm->State != BC250_ML_STOPPED',
            '!m->BackingReleased || !m->RootDropped', 'm->Records[i].Phase',
            's->Session->Mappings[i].Live', '!s->BackingReleased',
            '!Bc250BpBackingMatches(s)', 'Bc250VmSessionRelease(s->Session, &s->Backing)',
            's->BackingReleased = 1U', 'Bc250DmaLeasesDrop(s->Resource, &s->Hold)',
            's->State = BC250_BP_RELEASED', 'native != STATUS_DEVICE_BUSY')
    }
    sections = [(body(suffix), token) for suffix, entries in tokens.items() for token in entries]
    for section, token in sections:
        if section.count(token) != 1: raise ValueError('Guard missing: ' + token)
    def ordered(section, *items):
        positions = [section.index(item) for item in items]
        if positions != sorted(positions) or len(set(positions)) != len(positions):
            raise ValueError('Safety ordering changed')
    ordered(body('Capture'), 'Bc250DmaLeasesRetain(', 'Bc250BpRead(', 's->Sample = sample')
    ordered(body('Read'), 'Bc250BpPolicy(', 'Bc250DmaLeasesAdmit(', 'Bc250DmaLeasesLock(',
        'Bc250DmaLeasesMatch(', 'sg = resource->PinnedList', '*out = result',
        'Bc250DmaLeasesUnlock(', 'Bc250DmaLeasesFinish(')
    ordered(body('Register'), 'Bc250BpRead(', 'memcmp(&fresh', 'Bc250VmDomainRegister(')
    ordered(body('Release'), 'm->Records[i].Phase', 's->Session->Mappings[i].Live',
        'Bc250VmSessionRelease(', 'Bc250DmaLeasesDrop(')
    for suffix in ('Policy', 'Guard', 'Capture', 'BackingMatches', 'Register', 'Attach', 'Map', 'Release'):
        if re.search(r'->(?:Native|PinnedList|PinnedMdl|Metadata|Gate)\b', previous.stripped(body(suffix))):
            raise ValueError('Private DMA data outside fixture Read')
    return sections, sorted(calls)


def audit(output):
    out = Path(output).resolve(); out.relative_to((ROOT / 'output').resolve())
    if not out.is_dir(): raise ValueError('Scoped output required')
    previous.git('merge-base', '--is-ancestor', BASE, 'HEAD')
    changed = set(previous.git('diff', '--name-only', '-z', BASE).split('\0')) - {''}
    untracked = set(previous.git('ls-files', '--others', '--exclude-standard', '-z').split('\0')) - {''}
    if changed | untracked != FILES: raise ValueError('Exact six-file source-only scope mismatch')
    hashes = {}
    for relative in sorted(FILES):
        path = ROOT / relative
        if not path.is_file() or path.is_symlink(): raise ValueError('Regular source required')
        data = path.read_bytes(); text = data.decode('utf-8')
        canonical = data.replace(b'\r\n', b'\n')
        if b'\0' in data or b'\r' in canonical or not canonical.endswith(b'\n') or canonical.endswith(b'\n\n'):
            raise ValueError('UTF-8 single-final-newline format required')
        if path.suffix in ('.c', '.h') and data != canonical:
            raise ValueError('Pinned C/H sources require LF per frozen gitattributes')
        if re.search(r'-----BEGIN [A-Z ]*PRIVATE KEY-----|gh[pousr]_[A-Za-z0-9]{36,}|[A-Za-z]:[\\/]Users[\\/]', text):
            raise ValueError('Private/binary export')
        hashes[relative] = hashlib.sha256(data).hexdigest()
    if hashes[PREFIX + 'fixture-bound-pages.h'] != HEADER_SHA256: raise ValueError('Reviewed fixture changed')
    source = normalize((HERE / 'fixture-bound-pages.h').read_text(encoding='utf-8'))
    sections, calls = check_source(source)
    for section, token in sections:
        mutant = source.replace(section, section.replace(token, 'REMOVED_GUARD', 1), 1)
        try: check_source(mutant)
        except ValueError: continue
        raise ValueError('Negative control accepted')
    readme = (HERE / 'README.txt').read_text(encoding='utf-8')
    for token in ('Build11', 'NO SYS/INF/CAT', 'CREDITS.txt', 'lifetime externo',
            'EXCLUSIVO', 'FALSA', 'PENDING_DROP', 'BackingReleased', 'RootDropped',
            '22 suites RAM, 50 etapas', 'NO API de direcciones'):
        if token not in readme: raise ValueError('Scope/attribution missing')
    provenance = json.loads((HERE / 'primary-source.json').read_text(encoding='utf-8'))
    if provenance['source_baseline'] != BASE or len(provenance['local_sources']) != 5:
        raise ValueError('Provenance mismatch')
    for relative in provenance['local_sources']:
        if not (ROOT / relative).is_file(): raise ValueError('Protected source missing')
    for key in ('implementation_copied_from_external', 'production_execution_allowed',
            'real_dma_page_binding_verified', 'hardware_tested'):
        if provenance[key] is not False: raise ValueError('Unexpected integration/import claim')
    runner = (HERE / 'verify-offline.py').read_text(encoding='utf-8')
    for token in ("for name in ('CL', '_CL_')", "'/UBC250_DMA_LEASES_MOCK'",
            'len(history) != 50', 'len(tests) != 22', 'len(artifacts) != 26',
            'fake_sg_to_cpu_provenance_tested=True, real_dma_page_binding_verified=False'):
        if token not in runner: raise ValueError('Build isolation missing')
    if "str(HERE / 'fixture-bound-pages.h')" in runner: raise ValueError('Fixture compiled kernel')
    result = dict(scope='FAKE_SG_BOUND_PAGES_SOURCE_ONLY', baseline=BASE, new_source_sha256=hashes,
        source_guard_negative_controls=len(sections), operation_inventory=calls,
        predecessors_unchanged=True, driver_integrated=False, real_dma_page_binding_verified=False,
        hardware_tested=False, installable_package=False)
    (out / 'AUDIT.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('PASS: six original fixture-only sources, %d guard mutations; no real DMA provenance' % len(sections))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True)
    audit(parser.parse_args().output)
