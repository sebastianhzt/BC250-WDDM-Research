"""Audit only the scoped new export; inherited upstream is not certified safe."""
import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BASE = '9c657a11de52597b1aca66b8ea7f131c5ad41f00'
FILES = (
    'inc/gc-source-map-20260930/bc250_gc_source_map.h',
    'inc/gc-selectors-20260930/bc250_gc_selectors.h',
    'inc/gart-lifecycle-20260930/bc250_gart_backing.h',
    'inc/gart-geometry-20261001/bc250_gart_geometry.h',
    'inc/vm-source-model-20261001/bc250_vm_source_model.h',
    'inc/upstream-integration-20261003/bc250_pm4_bounds.h',
    'inc/upstream-integration-20261003/bc250_dma_page_list.h',
    'inc/vm-cpu-backend-20261003/bc250_vm_cpu_backend.h',
    'inc/vm-cpu-lifetime-20261003/bc250_vm_cpu_unmap.h',
    'inc/vm-cpu-lifetime-20261003/bc250_vm_cpu_backing_refs.h',
    'inc/vm-cpu-session-20261003/bc250_vm_cpu_session.h',
    'research/windows/gc-source-map-20260930/test-catalog.c',
    'research/windows/gc-source-map-20260930/catalog.py',
    'research/windows/gc-source-map-20260930/catalog.json',
    'research/windows/gc-selectors-20260930/test-selectors.c',
    'research/windows/gart-geometry-20261001/test-geometry.c',
    'research/windows/gart-geometry-20261001/linux-numeric-provenance.json',
    'research/windows/vm-source-model-20261001/test-vm-model.c',
    'research/windows/vm-source-model-20261001/primary-source.json',
    'research/windows/upstream-integration-20261003/test-dma-page-list.c',
    'research/windows/upstream-integration-20261003/provenance.json',
    'research/windows/vm-cpu-backend-20261003/test-vm-cpu-backend.c',
    'research/windows/vm-cpu-backend-20261003/provenance.json',
    'research/windows/vm-cpu-lifetime-20261003/test-vm-unmap.c',
    'research/windows/vm-cpu-lifetime-20261003/test-backing-refs.c',
    'research/windows/vm-cpu-session-20261003/test-vm-session.c',
    'research/windows/vm-cpu-session-20261003/00-LEER-PRIMERO.txt',
    'research/windows/vm-cpu-session-20261003/provenance.json',
    'research/offline-20261003/README.txt',
    'research/offline-20261003/provenance.json',
    'research/offline-20261003/audit-export.py',
    'research/offline-20261003/run-cpu-tests.py',
)


def git(*args):
    result = subprocess.run(['git', '-c', 'safe.directory=' + ROOT.as_posix(),
        '-C', str(ROOT), *args], check=True, capture_output=True)
    return result.stdout.decode('utf-8')


def audit():
    git('merge-base', '--is-ancestor', BASE, 'HEAD')
    # Also works after an approved commit: compare the entire export against
    # the pinned inherited base, not against a transient uncommitted HEAD.
    new = (set(git('ls-files', '--others', '--exclude-standard', '-z').split('\0')) |
           set(git('diff', '--name-only', '-z', BASE).split('\0'))) - {''}
    if new != set(FILES):
        raise ValueError('Exact new export mismatch: ' + str(sorted(new ^ set(FILES))))
    hashes = {}
    private_path = re.compile(r'(?i)(?:[A-Z]:[\\/](?:Users|BC250)[\\/]|/h[o]me/|'
                              + 'sh' + '662|' + 'se' + 'bas' + r'tian(?=[\\/]))')
    secret = re.compile(r'-----BEGIN\s+(?:RSA\s+|EC\s+|OPENSSH\s+)?PRIVATE\s+KEY-----')
    token_signature = re.compile(r'gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{20,}|AKIA[0-9A-Z]{16}')
    for name in FILES:
        if git('ls-tree', BASE, '--', name).strip():
            raise ValueError('Export would replace an inherited file: ' + name)
        path = ROOT / name
        if path.is_symlink() or not path.is_file():
            raise ValueError('Nonregular export: ' + name)
        data = path.read_bytes()
        text = data.decode('utf-8')
        if '\0' in text or private_path.search(text) or secret.search(text) or token_signature.search(text):
            raise ValueError('Binary/private material in export: ' + name)
        if path.suffix in ('.c', '.h') and any(token in text for token in
            ('DeviceIoControl(', 'WRITE_REGISTER_', 'READ_REGISTER_', 'MmMapIoSpace(',
             'MmAllocateContiguousMemory(', 'ZwCreateFile(', 'WRITE_PORT_', 'READ_PORT_')):
            raise ValueError('OS/device API entered standalone model: ' + name)
        hashes[name] = hashlib.sha256(data).hexdigest()
    for source in (ROOT / 'src').rglob('*'):
        if source.is_file() and source.suffix in ('.c', '.cpp', '.h'):
            text = source.read_text(encoding='utf-8', errors='replace')
            if any(prefix in text for prefix in ('vm-cpu-backend-20261003/',
                'vm-cpu-lifetime-20261003/', 'vm-cpu-session-20261003/')):
                raise ValueError('CPU model integrated into inherited production')
    return dict(scope='SCOPED_PUBLIC_CPU_MODEL_EXPORT', base_head=BASE,
        inherited_tracked_unchanged=True, inherited_driver_passive=False,
        hardware_tested=False, installable_package=False, export_sha256=hashes)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True)
    out = Path(parser.parse_args().output).resolve()
    out.relative_to((ROOT / 'output').resolve())
    if not out.is_dir():
        raise ValueError('Existing local output directory required')
    result = audit()
    (out / 'export-audit.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('PASS: exact export, text/privacy scan, unchanged inherited files; no hardware certification')
