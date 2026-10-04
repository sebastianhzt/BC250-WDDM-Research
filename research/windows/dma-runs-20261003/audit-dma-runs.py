"""Bounded CPU DMA-run source audit, not OS ownership or hardware validation."""
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
BASE = '4546263f73c38006a5af338d71de3fcd8510a44f'
HEADER = 'inc/dma-runs-20261003/bc250_dma_runs.h'
PREFIX = 'research/windows/dma-runs-20261003/'
FILES = {HEADER} | {PREFIX + name for name in ('test-dma-runs.c',
    'verify-offline.py', 'audit-dma-runs.py', 'README.txt')}
HEADER_SHA256 = 'c0d072b1da77aa1a0d08b5b9f577ca1f7494c8cd80dc8dc28a65018621588b57'
spec = importlib.util.spec_from_file_location('domain_auditor',
    ROOT / 'research/windows/domain-backend-20261003/audit-domain-backend.py')
previous = importlib.util.module_from_spec(spec)
spec.loader.exec_module(previous)


def check_source(source):
    calls = set(re.findall(r'\b([A-Za-z_]\w*)\s*\(', previous.stripped(source)))
    allowed = {'if', 'for', 'sizeof', 'memcpy', 'memset', 'Bc250DmaRunsExpand',
        'Bc250VmDomainRegisterRuns', 'Bc250VmDomainSpanValid',
        'Bc250DmaPageListDisjoint', 'Bc250VmSessionCheck',
        'Bc250VmSessionBufferAway', 'Bc250VmDomainRegister'}
    if calls - allowed: raise ValueError('Unexpected operation: ' + str(calls - allowed))
    expand = previous.function(source, 'Bc250DmaRunsExpand')
    register = previous.function(source, 'Bc250VmDomainRegisterRuns')
    sections = (
        (expand, 'count > capacity || !expected_bytes || (expected_bytes & 4095ULL)'),
        (expand, 'maximum_last > BC250_AD_MAX48'),
        (expand, 'Bc250VmDomainSpanValid(&runs[i], BC250_AD_DMA_LOGICAL, maximum_last)'),
        (expand, '(runs[i].Start & 4095ULL) || (runs[i].Bytes & 4095ULL)'),
        (expand, 'if (run_pages > BC250_VM_CPU_MAX_BATCH - total) return 0;'),
        (expand, '(BC250_AD_U64)total * 4096ULL != expected_bytes || total > page_capacity'),
        (expand, 'pages, (BC250_AD_U64)total * sizeof(*pages))'),
        (expand, 'Bc250DmaPageListDisjoint(runs, (BC250_AD_U64)count * sizeof(*runs), out_count, sizeof(*out_count))'),
        (expand, 'Bc250DmaPageListDisjoint(pages, (BC250_AD_U64)total * sizeof(*pages), out_count, sizeof(*out_count))'),
        (register, 'if (status != BC250_VM_SESSION_OK) return status;'),
        (register, 'Bc250VmSessionBufferAway(session, runs, (BC250_AD_U64)count * sizeof(*runs))'),
        (register, 'Bc250VmSessionBufferAway(session, out, sizeof(*out))'),
        (register, 'Bc250DmaPageListDisjoint(runs, (BC250_AD_U64)count * sizeof(*runs), out, sizeof(*out))'),
        (register, 'session->Backend.MaximumDmaLast, pages, BC250_VM_CPU_MAX_BATCH, &page_count)'),
        (register, 'return Bc250VmDomainRegister(session, pages, page_count, page_count, out);'),
    )
    for section, token in sections:
        if token not in section: raise ValueError('Guard missing: ' + token)
    if expand.index('total > page_capacity') > expand.index('memcpy(pages, result,'):
        raise ValueError('Premature output publication')
    if register.index('if (!Bc250DmaRunsExpand(') > register.index('return Bc250VmDomainRegister('):
        raise ValueError('Premature backing registration')
    if 'session->State =' in source or '*out =' in register:
        raise ValueError('Predecessor bypass')
    return sections, sorted(calls)


def audit(output):
    out = Path(output).resolve()
    out.relative_to((ROOT / 'output').resolve())
    if not out.is_dir(): raise ValueError('Existing scoped output folder required')
    previous.git('merge-base', '--is-ancestor', BASE, 'HEAD')
    changed = set(previous.git('diff', '--name-only', '-z', BASE).split('\0')) - {''}
    untracked = set(previous.git('ls-files', '--others', '--exclude-standard', '-z').split('\0')) - {''}
    if changed | untracked != FILES:
        raise ValueError('Exact five-file scope mismatch: ' + str(sorted((changed | untracked) ^ FILES)))
    hashes = {}
    for relative in sorted(FILES):
        path = ROOT / relative
        if not path.is_file() or path.is_symlink(): raise ValueError('Not regular source: ' + relative)
        data = path.read_bytes()
        text = data.decode('utf-8')
        if b'\0' in data or re.search(r'-----BEGIN [A-Z ]*PRIVATE KEY-----|gh[pousr]_[A-Za-z0-9]{36,}|[A-Za-z]:[\\/]Users[\\/]', text):
            raise ValueError('Private or binary export')
        hashes[relative] = hashlib.sha256(data).hexdigest()
    if hashes[HEADER] != HEADER_SHA256: raise ValueError('Reviewed header changed')
    source = (ROOT / HEADER).read_text(encoding='utf-8')
    sections, calls = check_source(source)
    normalized = source.replace('static __inline BC250_VM_SESSION_STATUS ', 'static __inline int ')
    for section, token in sections:
        if section.count(token) != 1 or section not in normalized:
            raise ValueError('Negative control target invalid')
        mutant = normalized.replace(section, section.replace(token, 'REMOVED_CHECK', 1), 1)
        try: check_source(mutant)
        except ValueError: continue
        raise ValueError('Source guard negative control accepted')
    readme = (HERE / 'README.txt').read_text(encoding='utf-8')
    for token in ('SOLO RAM', 'CREDITS.txt', 'NO implementa', 'Build11'):
        if token not in readme: raise ValueError('Scope/attribution missing')
    result = dict(scope='DMA_RUNS_CPU_ONLY', baseline=BASE,
        protected_implementation_unchanged=True, new_source_sha256=hashes,
        operation_inventory=calls, source_guard_negative_controls=len(sections),
        hardware_tested=False, installable_package=False,
        windows_dma_owner=False, dma_translation_validated=False,
        w2p_memory_ownership=False, copied_external_implementation=False)
    (out / 'AUDIT.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('PASS: five new files only; 15 source guard negative controls; no OS/GPU consumer')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True)
    audit(parser.parse_args().output)
