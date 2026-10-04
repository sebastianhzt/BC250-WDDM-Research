"""Domain adapter source/attribution audit; not a driver or licensing certificate."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import sys

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
BASE = '030f67f174c0d391508db9b0c44f070ec57754df'
HEADER = 'inc/domain-backend-20261003/bc250_vm_domain_backend.h'
PREFIX = 'research/windows/domain-backend-20261003/'
FILES = {HEADER, 'README.md'} | {PREFIX + name for name in ('test-domain-backend.c',
    'verify-offline.py', 'audit-domain-backend.py', 'README.txt', 'CREDITS.txt')}
HEADER_SHA256 = '2528060be5b6c3de8199433e76f46c308ebc4210c3b33a13108e925ebac52e7e'
spec = importlib.util.spec_from_file_location('address_auditor',
    ROOT / 'research/windows/address-domains-20261003/audit-address-domains.py')
previous = importlib.util.module_from_spec(spec)
spec.loader.exec_module(previous)
body, stripped = previous.body, previous.parser.stripped


def git(*args):
    process = subprocess.run(['git', '-c', 'safe.directory=' + ROOT.as_posix(),
        '-C', str(ROOT), *args], capture_output=True, check=True, shell=False)
    return process.stdout.decode('utf-8')


def function(source, name):
    # Reuse the reviewed brace-aware extraction for the int helpers and normalize
    # only the return-type spelling for the two status-returning wrappers.
    return body(source.replace('static __inline BC250_VM_SESSION_STATUS ',
        'static __inline int '), name)


def check_source(source):
    allowed = {'if', 'for', 'sizeof', 'memset', 'Bc250VmDomainSpanValid',
        'Bc250VmDomainPreviewVram', 'Bc250VmDomainRegister', 'Bc250VmDomainMap',
        'Bc250DmaPageListDisjoint', 'Bc250AdConvert', 'Bc250VmSessionCheck',
        'Bc250VmSessionBufferAway', 'Bc250VmSessionRegister',
        'Bc250BackingHandleMatches', 'Bc250VmSessionMap'}
    calls = set(re.findall(r'\b([A-Za-z_]\w*)\s*\(', stripped(source)))
    if calls - allowed: raise ValueError('Unexpected adapter operation: ' + str(calls - allowed))
    valid = function(source, 'Bc250VmDomainSpanValid')
    preview = function(source, 'Bc250VmDomainPreviewVram')
    register = function(source, 'Bc250VmDomainRegister')
    mapping = function(source, 'Bc250VmDomainMap')
    sections = (
        (valid, 'span->Domain == expected_domain'),
        (valid, 'span->Bytes - 1ULL <= maximum_last - span->Start'),
        (valid, 'span->Last == span->Start + (span->Bytes - 1ULL)'),
        (preview, 'Bc250DmaPageListDisjoint(input, sizeof(*input), output, sizeof(*output))'),
        (preview, '4096ULL, BC250_AD_FB_PHYSICAL, output'),
        (register, 'count > BC250_VM_CPU_MAX_BATCH || count > capacity'),
        (register, 'Bc250VmSessionBufferAway(session, pages, (BC250_GART_U64)count * sizeof(*pages))'),
        (register, 'Bc250VmDomainSpanValid(&pages[i], BC250_AD_DMA_LOGICAL, session->Backend.MaximumDmaLast)'),
        (register, 'pages[i].Bytes != 4096ULL || (pages[i].Start & 4095ULL)'),
        (register, 'translated[i].Domain = BC250_ADDRESS_DMA_LOGICAL;'),
        (register, 'translated[i].Start = pages[i].Start;'),
        (mapping, 'Bc250DmaPageListDisjoint(va, sizeof(*va), journal, sizeof(*journal))'),
        (mapping, 'if (!Bc250BackingHandleMatches(&session->Backing, handle)) return BC250_VM_SESSION_STALE;'),
        (mapping, 'Bc250VmDomainSpanValid(va, BC250_AD_GPU_VIRTUAL, maximum_va_last)'),
        (mapping, 'va->Bytes != (BC250_GART_U64)count * 4096ULL'),
    )
    for section, token in sections:
        if token not in section: raise ValueError('Required domain guard missing: ' + token)
    for section in (register, mapping):
        if 'if (status != BC250_VM_SESSION_OK) return status;' not in section:
            raise ValueError('Session guard missing')
    if register.index('BC250_AD_DMA_LOGICAL') > register.index('return Bc250VmSessionRegister('):
        raise ValueError('Registered before checking entire list')
    if mapping.index('if (!Bc250BackingHandleMatches(') > mapping.index('count = session->Backing.Records['):
        raise ValueError('Read backing before handle validation')
    if 'Bc250AdConvert(' in register or 'Bc250AdConvert(' in mapping:
        raise ValueError('Preview conversion leaked into mapping permission')
    if '*out =' in register or '*out =' in mapping or 'session->State =' in source:
        raise ValueError('Wrapper bypasses predecessor transaction')
    return sections, sorted(calls)


def check_readme_and_credits():
    text = (ROOT / 'README.md').read_text(encoding='utf-8')
    marker = '## Preserved upstream README (claims and commands are historical upstream content)\n\n'
    if text.count(marker) != 1: raise ValueError('Upstream boundary missing')
    prefix, inherited = text.split(marker)
    upstream = git('show', BASE + ':README.md')
    # apply_patch supplies a final newline. Permit exactly that one EOF
    # normalization only when upstream lacked it, never arbitrary whitespace.
    expected = upstream if upstream.endswith('\n') else upstream + '\n'
    if inherited != expected:
        raise ValueError('Inherited README changed beyond research prefix')
    for token in ('**not an installable driver release**', 'Publication of this branch is source-only.',
        'CREDITS.txt', 'No driver was installed, signed or loaded'):
        if token not in prefix: raise ValueError('Research scope not clear')
    credits = (HERE / 'CREDITS.txt').read_text(encoding='utf-8')
    for token in ('Keshas-dev', 'D-Ogi', 'amethyst8118', 'Apache-2.0',
        'PolyForm Noncommercial 1.0.0', 'TSNPL 1.5', 'No main update or release.'):
        if token not in credits: raise ValueError('Attribution boundary missing: ' + token)


def audit(output):
    out = Path(output).resolve()
    out.relative_to((ROOT / 'output').resolve())
    if not out.is_dir(): raise ValueError('Existing scoped output folder required')
    git('merge-base', '--is-ancestor', BASE, 'HEAD')
    changed = set(git('diff', '--name-only', '-z', BASE).split('\0')) - {''}
    untracked = set(git('ls-files', '--others', '--exclude-standard', '-z').split('\0')) - {''}
    if changed | untracked != FILES:
        raise ValueError('Exact adapter/documentation mismatch: ' + str(sorted((changed | untracked) ^ FILES)))
    hashes = {}
    for relative in sorted(FILES):
        path = ROOT / relative
        if not path.is_file() or path.is_symlink(): raise ValueError('Not regular source: ' + relative)
        data = path.read_bytes()
        text = data.decode('utf-8')
        if b'\0' in data or re.search(r'-----BEGIN [A-Z ]*PRIVATE KEY-----|gh[pousr]_[A-Za-z0-9]{36,}|[A-Za-z]:[\\/]Users[\\/]', text):
            raise ValueError('Private/binary export detected')
        hashes[relative] = hashlib.sha256(data).hexdigest()
    if hashes[HEADER] != HEADER_SHA256: raise ValueError('Reviewed header snapshot changed')
    source = (ROOT / HEADER).read_text(encoding='utf-8')
    sections, calls = check_source(source)
    for section, token in sections:
        if section.count(token) != 1: raise ValueError('Nonunique negative control')
        # function() normalized a return type: replace the matching section in
        # the same normalized view so each negative control actually changes it.
        normalized = source.replace('static __inline BC250_VM_SESSION_STATUS ', 'static __inline int ')
        if section not in normalized: raise ValueError('Negative control target absent')
        mutant = normalized.replace(section, section.replace(token, 'REMOVED_CHECK', 1), 1)
        try: check_source(mutant)
        except ValueError: continue
        raise ValueError('Negative control accepted: ' + token)
    check_readme_and_credits()
    result = dict(scope='DOMAIN_ADAPTER_CPU_ONLY', baseline=BASE,
        protected_implementation_unchanged=True, preserved_upstream_readme_text=True,
        readme_eof_normalization_only=True,
        new_source_sha256=hashes, operation_inventory=calls,
        semantic_negative_controls=len(sections), copied_external_implementation=False,
        hardware_tested=False, installable_package=False, w2p_memory_ownership=False,
        preview_authorizes_dma=False, whole_repository_license_certified=False)
    (out / 'AUDIT.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('PASS: domain adapter + attributed documentation only; 15 source negative controls')
    return result


if __name__ == '__main__':
    arguments = argparse.ArgumentParser(description=__doc__)
    arguments.add_argument('--output', required=True)
    audit(arguments.parse_args().output)
