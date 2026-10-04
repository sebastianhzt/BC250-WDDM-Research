"""Arithmetic-only successor audit; does not certify Windows memory ownership."""
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
BASE = '62b7edeab0f7e3aeec92f7743189062199101dc7'
HEADER = 'inc/address-domains-20261003/bc250_address_domains.h'
PREFIX = 'research/windows/address-domains-20261003/'
FILES = {HEADER} | {PREFIX + name for name in ('test-address-domains.c',
    'verify-offline.py', 'audit-address-domains.py', 'README.txt', 'primary-source.json')}
HEADER_SHA256 = '21b3406ad79cd1a9ae396a220c97f64b4da07c2682030e05018d467eb3dae4d3'
spec = importlib.util.spec_from_file_location('passive_parser',
    ROOT / 'research/windows/passive-port-20261003/audit-passive-port.py')
parser = importlib.util.module_from_spec(spec)
spec.loader.exec_module(parser)


def git(*args):
    process = subprocess.run(['git', '-c', 'safe.directory=' + ROOT.as_posix(),
        '-C', str(ROOT), *args], capture_output=True, check=True, shell=False)
    return process.stdout.decode('utf-8')


def body(source, name):
    clean = parser.stripped(source)
    matches = list(re.finditer(r'(?m)^static __inline int ' + re.escape(name) +
        r'\s*\([^;{}]*\)\s*\{', clean))
    if len(matches) != 1: raise ValueError('Expected one model function: ' + name)
    start, opening = matches[0].start(), matches[0].end() - 1
    depth = 1
    for end in range(opening + 1, len(clean)):
        depth += (clean[end] == '{') - (clean[end] == '}')
        if not depth: return source[start:end + 1]
    raise ValueError('Unbalanced model function')


def check_model(source):
    allowed = {'if', 'switch', 'sizeof', 'Bc250AdLayoutValid', 'Bc250AdLayoutInit',
        'Bc250AdDomainBase', 'Bc250AdConvert'}
    calls = set(re.findall(r'\b([A-Za-z_]\w*)\s*\(', parser.stripped(source)))
    if calls - allowed: raise ValueError('Unexpected operation: ' + str(calls - allowed))
    valid = body(source, 'Bc250AdLayoutValid')
    convert = body(source, 'Bc250AdConvert')
    init = body(source, 'Bc250AdLayoutInit')
    domain = body(source, 'Bc250AdDomainBase')
    sections = (
        (valid, '!layout->VramBytes'),
        (valid, 'layout->VramBytes & (BC250_AD_PAGE_BYTES - 1ULL)'),
        (valid, 'layout->McBase > BC250_AD_MAX48'),
        (valid, 'layout->FbPhysicalBase > BC250_AD_MAX48'),
        (valid, 'layout->VramBytes - 1ULL > BC250_AD_MAX48 - layout->McBase'),
        (valid, 'layout->VramBytes - 1ULL > BC250_AD_MAX48 - layout->FbPhysicalBase'),
        (convert, '!output || !Bc250AdLayoutValid(layout) || !bytes || !alignment'),
        (convert, 'alignment > BC250_AD_PAGE_BYTES || (alignment & (alignment - 1ULL))'),
        (convert, '!Bc250AdDomainBase(layout, input_domain, &input_base)'),
        (convert, '!Bc250AdDomainBase(layout, target_domain, &target_base)'),
        (convert, 'address < input_base || (address & (alignment - 1ULL))'),
        (convert, 'offset >= layout->VramBytes || bytes > layout->VramBytes - offset'),
        (convert, 'start = target_base + offset;'),
        (convert, 'candidate.Last = start + bytes - 1ULL;'),
        (init, 'if (!Bc250AdLayoutValid(&candidate)) return 0;'),
        (domain, 'default: return 0;'),
    )
    for section, token in sections:
        if token not in section: raise ValueError('Required arithmetic check missing: ' + token)
    if convert.count('*output =') != 1 or init.count('*output =') != 1:
        raise ValueError('Unexpected output mutation')
    if convert.index('bytes > layout->VramBytes - offset') > convert.index('start = target_base + offset;'):
        raise ValueError('Add before full-span check')
    if convert.index('address < input_base') > convert.index('offset = address - input_base;'):
        raise ValueError('Subtract before lower bound check')
    if convert.index('*output = candidate;') < convert.index('candidate.Last ='):
        raise ValueError('Premature output mutation')
    domains = re.findall(r'case (BC250_AD_\w+):', domain)
    if domains != ['BC250_AD_VRAM_OFFSET', 'BC250_AD_VRAM_MC', 'BC250_AD_FB_PHYSICAL']:
        raise ValueError('Unexpected convertible domain')
    return sections, sorted(calls)


def audit(output):
    out = Path(output).resolve()
    out.relative_to((ROOT / 'output').resolve())
    if not out.is_dir(): raise ValueError('Existing output subfolder required')
    git('merge-base', '--is-ancestor', BASE, 'HEAD')
    changed = set(git('diff', '--name-only', '-z', BASE).split('\0')) - {''}
    untracked = set(git('ls-files', '--others', '--exclude-standard', '-z').split('\0')) - {''}
    if changed | untracked != FILES:
        raise ValueError('Exact arithmetic-only export mismatch: ' + str(sorted((changed | untracked) ^ FILES)))
    hashes = {}
    for relative in sorted(FILES):
        path = ROOT / relative
        if not path.is_file() or path.is_symlink(): raise ValueError('Not regular source: ' + relative)
        data = path.read_bytes()
        text = data.decode('utf-8')
        if b'\0' in data or re.search(r'-----BEGIN [A-Z ]*PRIVATE KEY-----|gh[pousr]_[A-Za-z0-9]{36,}|[A-Za-z]:[\\/]Users[\\/]', text):
            raise ValueError('Private/binary material in export')
        hashes[relative] = hashlib.sha256(data).hexdigest()
    if hashes[HEADER] != HEADER_SHA256: raise ValueError('Reviewed header snapshot changed')
    source = (ROOT / HEADER).read_text(encoding='utf-8')
    sections, calls = check_model(source)
    for section, token in sections:
        if section.count(token) != 1: raise ValueError('Nonunique negative-control token')
        mutant = source.replace(section, section.replace(token, 'REMOVED_CHECK', 1), 1)
        try: check_model(mutant)
        except ValueError: continue
        raise ValueError('Source negative control accepted: ' + token)
    result = dict(scope='ARITHMETIC_ADDRESS_DOMAINS_ONLY', baseline=BASE,
        protected_base_unchanged=True, new_source_sha256=hashes,
        operation_inventory=calls, semantic_negative_controls=len(sections),
        linux_code_copied=False, metalcyan_code_copied=False,
        hardware_tested=False, installable_package=False, w2p_memory_ownership=False,
        cpu_physical_mapping_validated=False, dma_translation_validated=False,
        pte_encoding_validated=False, tlb_invalidation_validated=False)
    (out / 'AUDIT.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('PASS: original address arithmetic only; protected base; 16 source negative controls')
    return result


if __name__ == '__main__':
    arguments = argparse.ArgumentParser(description=__doc__)
    arguments.add_argument('--output', required=True)
    audit(arguments.parse_args().output)
