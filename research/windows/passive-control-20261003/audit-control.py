"""Scoped standalone control source audit; never an installation certificate."""
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
BASE = 'db19baca9b2cc32f5402170d69a50a156674113d'
PREFIX = 'research/windows/passive-control-20261003/'
FILES = {PREFIX + name for name in ('bc250_control.c', 'bc250_control_abi.h',
    'bc250_control_platform.h', 'test-control.c', 'verify-offline.py',
    'audit-control.py', 'README.txt', 'METALCYAN-REFERENCE.txt')}
SNAPSHOT = {
    'bc250_control.c': '44d032ed446afc802d1d183e7909a02f415a58eba50894d785906d3acbe7f626',
    'bc250_control_abi.h': '0ff802334a5eae0c9168ca7a007a475e63aa9cce541896f2634373142865caa7',
    'bc250_control_platform.h': '80a33cfa28567e3eebe76f4c2450c20aa37b430805f06aa845e280557f410675',
}
spec = importlib.util.spec_from_file_location('source_parser',
    ROOT / 'research/windows/passive-port-20261003/audit-passive-port.py')
parser_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(parser_module)
function = parser_module.function
stripped = parser_module.stripped


def git(*args):
    result = subprocess.run(['git', '-c', 'safe.directory=' + ROOT.as_posix(),
        '-C', str(ROOT), *args], capture_output=True, check=True, shell=False)
    return result.stdout.decode('utf-8')


def check_source(source):
    allowed = {'if', 'for', 'while', 'sizeof', 'UNREFERENCED_PARAMETER', 'NT_SUCCESS',
        'Bc250ControlComplete', 'Bc250ControlIsReady', 'Bc250ControlReject',
        'Bc250ControlCreateClose', 'Bc250ControlDeviceControl',
        'Bc250ControlDestroyOwnDevice', 'Bc250ControlUnload', 'DriverEntry',
        'IoCompleteRequest', 'IoGetCurrentIrpStackLocation', 'RtlZeroMemory',
        'RtlInitUnicodeString', 'IoDeleteSymbolicLink', 'IoDeleteDevice',
        'KeGetCurrentIrql', 'IoCreateDeviceSecure', 'IoCreateSymbolicLink'}
    calls = set(re.findall(r'\b([A-Za-z_]\w*)\s*\(', stripped(source)))
    if calls - allowed: raise ValueError('Unexpected control operation: ' + str(calls - allowed))
    entry = function(source, 'DriverEntry')
    create_at = entry.index('status = IoCreateDeviceSecure(')
    if entry.index('DriverObject->DriverUnload = Bc250ControlUnload;') > create_at:
        raise ValueError('Unload installed too late')
    if entry.index('DriverObject->MajorFunction[index] = Bc250ControlReject;') > create_at:
        raise ValueError('Default dispatch installed too late')
    if entry.index('extension->Ready = TRUE;') > entry.index('device->Flags &= ~DO_DEVICE_INITIALIZING;'):
        raise ValueError('Premature device publication')
    if entry.index('status = IoCreateSymbolicLink(') > entry.index('extension->Ready = TRUE;'):
        raise ValueError('Ready before link success')
    if 'if (!NT_SUCCESS(status)) return status;' not in entry:
        raise ValueError('Secure create failure not returned')
    if 'Bc250ControlDestroyOwnDevice(device);\n        return status;' not in entry:
        raise ValueError('Link failure rollback missing')
    query = function(source, 'Bc250ControlDeviceControl')
    for token in ('InputBufferLength != 0', 'OutputBufferLength < sizeof(BC250_CONTROL_METADATA)',
        'IoControlCode != BC250_CONTROL_QUERY_METADATA', '!Irp->AssociatedIrp.SystemBuffer',
        'RtlZeroMemory(metadata, sizeof(*metadata));',
        'metadata->GpuRuntimeEnabled = BC250_PASSIVE_GPU_RUNTIME_ENABLED;'):
        if token not in query: raise ValueError('Metadata validation missing: ' + token)
    if query.index('InputBufferLength != 0') > query.index('RtlZeroMemory(metadata,'):
        raise ValueError('Output modified before input validation')
    destroy = function(source, 'Bc250ControlDestroyOwnDevice')
    if 'if (extension->Signature != BC250_CONTROL_SIGNATURE) return;' not in destroy:
        raise ValueError('Own-object guard missing')
    create = function(source, 'Bc250ControlCreateClose')
    if 'stack->FileObject->FileName.Length != 0' not in create:
        raise ValueError('Namespace rejection missing')
    if 'D:P(A;;GA;;;SY)(A;;GA;;;BA)' not in source or 'FILE_DEVICE_SECURE_OPEN' not in entry:
        raise ValueError('Default device security missing')
    return sorted(calls)


def audit(output):
    out = Path(output).resolve()
    out.relative_to((ROOT / 'output').resolve())
    if not out.is_dir(): raise ValueError('Existing scoped output folder required')
    git('merge-base', '--is-ancestor', BASE, 'HEAD')
    changed = set(git('diff', '--name-only', '-z', BASE).split('\0')) - {''}
    untracked = set(git('ls-files', '--others', '--exclude-standard', '-z').split('\0')) - {''}
    if changed | untracked != FILES:
        raise ValueError('Exact standalone export mismatch: ' + str(sorted((changed | untracked) ^ FILES)))
    hashes = {}
    for relative in FILES:
        path = ROOT / relative
        if not path.is_file() or path.is_symlink(): raise ValueError('Not a regular source: ' + relative)
        data = path.read_bytes()
        text = data.decode('utf-8')
        if b'\0' in data or re.search(r'-----BEGIN [A-Z ]*PRIVATE KEY-----|gh[pousr]_[A-Za-z0-9]{36,}|[A-Za-z]:[\\/]Users[\\/]', text):
            raise ValueError('Private/binary material in standalone export')
        hashes[relative] = hashlib.sha256(data).hexdigest()
    for name, expected in SNAPSHOT.items():
        if hashes[PREFIX + name] != expected: raise ValueError('Control snapshot changed: ' + name)
    source = (HERE / 'bc250_control.c').read_text(encoding='utf-8')
    calls = check_source(source)
    replacements = (
        ('DriverObject->DriverUnload = Bc250ControlUnload;', ''),
        ('if (!NT_SUCCESS(status)) return status;', 'if (!NT_SUCCESS(status)) return STATUS_SUCCESS;'),
        ('InputBufferLength != 0', 'InputBufferLength != 1'),
        ('RtlZeroMemory(metadata, sizeof(*metadata));', ''),
        ('stack->FileObject->FileName.Length != 0', 'stack->FileObject->FileName.Length != 1'),
        ('if (extension->Signature != BC250_CONTROL_SIGNATURE) return;', ''),
        ('Bc250ControlDestroyOwnDevice(device);\n        return status;', 'return status;'),
    )
    for old, new in replacements:
        if old not in source: raise ValueError('Negative control target absent')
        try: check_source(source.replace(old, new, 1))
        except (ValueError, KeyError): continue
        raise ValueError('Source negative control accepted: ' + old)
    result = dict(scope='STANDALONE_CONTROL_SOURCE_ONLY', baseline=BASE,
        protected_base_unchanged=True, new_source_sha256=hashes,
        control_call_inventory=calls, semantic_negative_controls=len(replacements),
        hardware_tested=False, installable_package=False, effective_acl_tested=False,
        w2p_memory_ownership=False, metalcyan_code_copied=False)
    (out / 'AUDIT.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('PASS: standalone control only, protected driver baseline, 7 source negative controls')
    return result


if __name__ == '__main__':
    arguments = argparse.ArgumentParser(description=__doc__)
    arguments.add_argument('--output', required=True)
    audit(arguments.parse_args().output)
