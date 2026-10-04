"""Six new source files; public snapshot association, RAM ONLY."""
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
BASE = '8bf1f5db881b2e1c92a369f59d5da1a8aa00065c'
PREFIX = 'research/windows/snapshot-cpu-20261004/'
FILES = {PREFIX + name for name in ('bc250_snapshot_cpu.h', 'test-snapshot-cpu.c',
    'audit-snapshot-cpu.py', 'verify-offline.py', 'README.txt', 'primary-source.json')}
HEADER_SHA256 = '895f127a971ebc7f1c50f3ce9c19d1ff4806c2521adac779f0ec2b00bbd0d12d'
spec = importlib.util.spec_from_file_location('domain_auditor',
    ROOT / 'research/windows/domain-backend-20261003/audit-domain-backend.py')
previous = importlib.util.module_from_spec(spec); spec.loader.exec_module(previous)


def normalize(source):
    return source.replace('static __inline BC250_VM_SESSION_STATUS ', 'static __inline int ')


def check_source(source):
    source = normalize(source)
    def body(suffix): return previous.function(source, 'Bc250Sc' + suffix)
    allowed = {'if', 'for', 'sizeof', 'defined', 'memcmp', 'KeGetCurrentIrql',
        'Bc250DmaPageListDisjoint', 'Bc250VmSessionBufferAway', 'Bc250VmDomainSpanValid',
        'Bc250BackingHandleMatches', 'Bc250VmSessionCheck', 'Bc250VmDomainRegister',
        'Bc250VmSessionMap', 'Bc250VmSessionUnmap', 'Bc250VmSessionRelease',
        'Bc250DmaCaptureSnapshot', 'Bc250DmaCaptureSnapshotRelease',
        'Bc250DmaCaptureStop', 'Bc250DmaCaptureRetire'}
    suffixes = ('Policy', 'Guard', 'NativeStatus', 'Away', 'SnapshotValid',
        'BackingMatches', 'Check', 'Stop', 'Init', 'Map', 'Unmap', 'Retire')
    allowed |= {'Bc250Sc' + suffix for suffix in suffixes}
    stripped = previous.stripped(source)
    calls = set(re.findall(r'\b([A-Za-z_]\w*)\s*\(', stripped))
    if calls - allowed: raise ValueError('Unexpected operation: ' + str(calls - allowed))
    if re.search(r'(?:resource|Resource)->(?:Native|Gate|Metadata|Records|Pinned|Request|References)', stripped):
        raise ValueError('Private transport access')
    tokens = {
        'Policy': ('!Bc250MockExecutionAllowed', '!Bc250MockGateExecutionAllowed',
            '!Bc250MockCaptureExecutionAllowed', 'KeGetCurrentIrql() != PASSIVE_LEVEL'),
        'Guard': ('Bc250ScPolicy()', 'm->Self != m', 'if (m->Busy)',
            'm->State == BC250_SC_FAULT', 'm->State == BC250_SC_DEAD'),
        'Away': ('Bc250DmaPageListDisjoint(m, sizeof(*m), buffer, bytes)',
            'Bc250DmaPageListDisjoint(m->Resource, sizeof(*m->Resource), buffer, bytes)',
            'Bc250VmSessionBufferAway(m->Session, buffer, bytes)'),
        'SnapshotValid': ('s->Self != s', 's->Reference.Owner != m->Resource',
            's->Reference.Slot >= BC250_DMA_CAPTURE_SLOTS', 's->PageCount > BC250_DMA_CAPTURE_MAX_PAGES',
            's->Request.Offset != m->Expected.Offset', 's->Request.Bytes != m->Expected.Bytes',
            's->Request.Direction != m->Expected.Direction', 's->Request.Direction > TRUE',
            's->MaximumLast > BC250_AD_MAX48', 's->MaximumLast & (s->MaximumLast + 1ULL)',
            'Bc250VmDomainSpanValid(&s->Pages[i], BC250_AD_DMA_LOGICAL, s->MaximumLast)',
            's->Pages[i].Bytes != 4096ULL', 's->Pages[i].Start & 4095ULL'),
        'BackingMatches': ('!Bc250ScSnapshotValid(m)',
            '!Bc250BackingHandleMatches(&m->Session->Backing, &m->Backing)',
            'r->PageCount != m->Snapshot.PageCount', 'r->Pages[i].Domain != BC250_ADDRESS_DMA_LOGICAL',
            'r->Pages[i].Start != m->Snapshot.Pages[i].Start',
            'r->Pages[i].Bytes != m->Snapshot.Pages[i].Bytes', 'r->Pages[i].Last != m->Snapshot.Pages[i].Last'),
        'Check': ('Bc250ScGuard(m)', 'm->HasSnapshot > 1U', 'm->HasBacking > 1U',
            'm->BackingReleased > 1U', 'm->SnapshotReleased > 1U',
            'Bc250VmSessionBufferAway(m->Session, m, sizeof(*m))',
            'Bc250VmSessionBufferAway(m->Session, m->Resource, sizeof(*m->Resource))',
            'Bc250VmSessionCheck(m->Session)', 'm->Snapshot.State != BC250_DMA_CAPTURE_SNAPSHOT_RELEASED',
            'memcmp(&m->Snapshot, &zero, sizeof(zero))', 'Bc250ScBackingMatches(m)',
            'r->Lease.Slot != m->Backing.Slot', 'r->Lease.Generation != m->Backing.Generation'),
        'Stop': ('Bc250ScPolicy()', 'm->Self != m', 'm->State = BC250_SC_STOPPED',
            'Bc250DmaCaptureStop(m->Resource)', 'if (native != STATUS_SUCCESS) m->State = BC250_SC_FAULT'),
        'Init': ('Bc250ScPolicy()', 'memcmp(m, &zero, sizeof(zero))',
            'status = Bc250VmSessionCheck(session);', 'Bc250VmSessionBufferAway(session, m, sizeof(*m))',
            'Bc250VmSessionBufferAway(session, resource, sizeof(*resource))',
            'Bc250VmSessionBufferAway(session, seed, sizeof(*seed))',
            'Bc250VmSessionBufferAway(session, expected, sizeof(*expected))',
            'Bc250DmaPageListDisjoint(m, sizeof(*m), resource, sizeof(*resource))',
            'Bc250DmaPageListDisjoint(m, sizeof(*m), seed, sizeof(*seed))',
            'Bc250DmaPageListDisjoint(m, sizeof(*m), expected, sizeof(*expected))',
            'Bc250DmaPageListDisjoint(resource, sizeof(*resource), seed, sizeof(*seed))',
            'Bc250DmaPageListDisjoint(resource, sizeof(*resource), expected, sizeof(*expected))',
            'if (session->Mappings[i].Live)', 'if (session->Backing.Records[i].Registered)',
            'm->Self = m', 'm->Expected = *expected', 'm->Busy = 1U',
            'Bc250DmaCaptureSnapshot(resource, seed, expected, &m->Snapshot)',
            'if (m->Snapshot.Reference.Owner) m->HasSnapshot = 1U',
            'memcmp(&m->Snapshot, &empty, sizeof(empty))', '!Bc250ScSnapshotValid(m)',
            'session->Backend.MaximumDmaLast > m->Snapshot.MaximumLast',
            'Bc250VmDomainRegister(session, m->Snapshot.Pages, m->Snapshot.PageCount,',
            'if (status == BC250_VM_SESSION_OK) m->HasBacking = 1U'),
        'Map': ('Bc250ScCheck(m)', 'm->State != BC250_SC_ACTIVE',
            'Bc250ScAway(m, out, sizeof(*out))', 'Bc250ScAway(m, journal, sizeof(*journal))',
            'm->Busy = 1U', 'Bc250VmSessionMap(m->Session, va, &m->Backing,',
            'Bc250VmSessionCheck(m->Session) != BC250_VM_SESSION_OK', 'm->Busy = 0U'),
        'Unmap': ('Bc250ScCheck(m)', 'Bc250ScAway(m, input, sizeof(*input))',
            'Bc250ScAway(m, journal, sizeof(*journal))', 'm->Busy = 1U',
            'Bc250VmSessionUnmap(m->Session, input,', 'm->Busy = 0U'),
        'Retire': ('Bc250ScCheck(m)', 'Bc250ScStop(m)',
            'if (m->Session->Mappings[i].Live) return BC250_VM_SESSION_IN_USE',
            'm->Busy = 1U', 'm->HasBacking && !m->BackingReleased',
            'Bc250VmSessionRelease(m->Session, &m->Backing)',
            'if (status == BC250_VM_SESSION_OK) m->BackingReleased = 1U',
            'm->HasSnapshot && !m->SnapshotReleased',
            'Bc250DmaCaptureSnapshotRelease(m->Resource, &m->Snapshot)',
            'if (native == STATUS_SUCCESS) m->SnapshotReleased = 1U',
            'Bc250DmaCaptureRetire(m->Resource)',
            'if (native == STATUS_SUCCESS) m->State = BC250_SC_DEAD',
            'else if (native != STATUS_DEVICE_BUSY) m->State = BC250_SC_FAULT',
            'm->Busy = 0U')}
    sections = [(body(suffix), token) for suffix, entries in tokens.items() for token in entries]
    for section, token in sections:
        if section.count(token) != 1: raise ValueError('Missing/nonunique guard: ' + token)
    for token in ('!defined(BC250_DMA_ADAPTER_MOCK)', '!defined(BC250_DMA_GATE_MOCK)',
            '!defined(BC250_DMA_CAPTURE_MOCK)', '#ifdef _KERNEL_MODE',
            '#error Snapshot CPU association is NEVER kernel integrated'):
        if source.count(token) != 1: raise ValueError('Mock-only boundary missing')
        sections.append((source, token))
    def ordered(suffix, *items):
        positions = [body(suffix).index(item) for item in items]
        if positions != sorted(positions) or len(set(positions)) != len(positions):
            raise ValueError('Wrong ordering: ' + suffix)
    ordered('Policy', '!Bc250MockExecutionAllowed', '!Bc250MockCaptureExecutionAllowed', 'KeGetCurrentIrql(')
    ordered('Init', 'Bc250ScPolicy(', 'memcmp(m,', 'Bc250VmSessionCheck(', 'm->Self = m',
        'Bc250DmaCaptureSnapshot(', 'Bc250ScSnapshotValid(', 'Bc250VmDomainRegister(')
    ordered('Retire', 'Bc250ScCheck(', 'Bc250ScStop(', 'm->Session->Mappings[i].Live',
        'Bc250VmSessionRelease(', 'm->BackingReleased = 1U', 'Bc250DmaCaptureSnapshotRelease(',
        'm->SnapshotReleased = 1U', 'Bc250DmaCaptureRetire(', 'm->State = BC250_SC_DEAD')
    for suffix in ('Stop', 'Map', 'Unmap'):
        if re.search(r'Bc250(?:DmaCaptureSnapshotRelease|VmSessionRelease|DmaCaptureRetire)\(', body(suffix)):
            raise ValueError('Premature implicit cleanup')
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
        data = path.read_bytes(); text = data.decode('utf-8'); canonical = data.replace(b'\r\n', b'\n')
        if b'\r' in canonical or not canonical.endswith(b'\n') or canonical.endswith(b'\n\n'):
            raise ValueError('Single-final-newline UTF8 required: ' + relative)
        if path.suffix in ('.c', '.h') and data != canonical: raise ValueError('Pinned C/H requires LF')
        if b'\0' in data or re.search(r'-----BEGIN [A-Z ]*PRIVATE KEY-----|gh[pousr]_[A-Za-z0-9]{36,}|[A-Za-z]:[\\/]Users[\\/]', text):
            raise ValueError('Private/binary export')
        hashes[relative] = hashlib.sha256(data).hexdigest()
    if hashes[PREFIX + 'bc250_snapshot_cpu.h'] != HEADER_SHA256:
        raise ValueError('Reviewed header changed')
    source = normalize((HERE / 'bc250_snapshot_cpu.h').read_text(encoding='utf-8'))
    sections, calls = check_source(source)
    for section, token in sections:
        mutant = source.replace(section, section.replace(token, 'REMOVED_GUARD', 1), 1)
        try: check_source(mutant)
        except ValueError: continue
        raise ValueError('Negative control accepted: ' + token)
    readme = (HERE / 'README.txt').read_text(encoding='utf-8')
    for token in ('Build11', 'NO SYS/INF/CAT', 'CREDITS.txt', 'lifetime externo',
            'NO prueba de concurrencia real', 'NO consumidores reales', 'AGREGADA',
            'SnapshotRelease original NO sabe', 'SOLO Stop', 'DESPUES de publicar Snapshot LIVE'):
        if token not in readme: raise ValueError('Scope missing: ' + token)
    provenance = json.loads((HERE / 'primary-source.json').read_text(encoding='utf-8'))
    if provenance['source_baseline'] != BASE or len(provenance['local_sources']) != 6:
        raise ValueError('Provenance mismatch')
    for relative in provenance['local_sources']:
        if not (ROOT / relative).is_file(): raise ValueError('Protected source missing')
    for key in ('implementation_copied_from_external', 'production_execution_allowed',
            'real_dma_page_binding_verified', 'hardware_tested'):
        if provenance[key] is not False: raise ValueError('Unexpected production/import claim')
    runner = (HERE / 'verify-offline.py').read_text(encoding='utf-8')
    for token in ("for name in ('CL', '_CL_')", "'/UBC250_DMA_CAPTURE_MOCK'",
            "'/UBC250_DMA_GATE_MOCK'", "'/UBC250_DMA_ADAPTER_MOCK'",
            'len(history) != 55', 'len(tests) != 24', 'len(artifacts) != 29',
            "ROOT / 'research/windows/dma-capture-20261004/bc250_dma_capture.c'"):
        if token not in runner: raise ValueError('Build isolation missing')
    if "HERE / 'bc250_snapshot_cpu.h'" in runner or 'driver_loaded=True' in runner:
        raise ValueError('CPU facade kernel integrated')
    result = dict(scope='RAM_PUBLIC_SNAPSHOT_CPU_ASSOCIATION', baseline=BASE,
        new_source_sha256=hashes, source_guard_negative_controls=len(sections),
        operation_inventory=calls, predecessors_unchanged=True,
        production_execution_allowed=False, hardware_tested=False, installable_package=False,
        real_map_consumers_supported=False, w2p_memory_ownership=False)
    (out / 'AUDIT.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('PASS: six new source files; %d guard negative controls; PUBLIC snapshot, RAM ONLY' % len(sections))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True)
    audit(parser.parse_args().output)
