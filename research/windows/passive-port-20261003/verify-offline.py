"""Compile-only KMD and RAM-only regression acceptance; never an install build."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


original = module('original_cpu_runner', ROOT / 'research/offline-20261003/run-cpu-tests.py')
audit = module('passive_port_audit', HERE / 'audit-passive-port.py')


def reject_override(compiler, out, history):
    name = 'policy-override-rejected'
    obj = out / 'forbidden-override.obj'
    argv = [compiler, '/nologo', '/c', '/W4', '/WX',
        '/DBC250_PASSIVE_GPU_RUNTIME_ENABLED=1', '/I' + str(ROOT / 'inc'),
        '/Fo:' + str(obj), str(HERE / 'test-policy.c')]
    (out / (name + '.command.json')).write_text(json.dumps(argv) + '\n', encoding='utf-8')
    result = subprocess.run(argv, cwd=out, capture_output=True, shell=False)
    (out / (name + '.stdout')).write_bytes(result.stdout)
    (out / (name + '.stderr')).write_bytes(result.stderr)
    history.append(dict(stage=name, returncode=result.returncode, expected_failure=True))
    (out / 'stages.json').write_text(json.dumps(history, indent=2) + '\n', encoding='utf-8')
    text = (result.stdout + result.stderr).decode('utf-8', errors='replace')
    if result.returncode == 0 or obj.exists() or 'Do not override the passive research policy with compiler flags' not in text:
        raise RuntimeError('Policy compiler override was not rejected by the expected guard')
    print('PASS: explicit runtime compiler override rejected', flush=True)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    if os.name != 'nt' or os.environ.get('VSCMD_ARG_TGT_ARCH', '').lower() != 'x64':
        raise RuntimeError('Use Windows x64 Native Tools Command Prompt for VS 2022')
    compiler = shutil.which('cl.exe')
    if not compiler: raise RuntimeError('cl.exe unavailable')
    # Existing checked subprocess helper, not original.main() or its stale audit.
    original.negative_control()
    wdk = Path(os.environ.get('BC250_WDK_ROOT', str(Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)')) / 'Windows Kits/10')))
    version = os.environ.get('BC250_WDK_VERSION', '10.0.26100.0')
    includes = [wdk / 'Include' / version / folder for folder in ('km', 'km/crt', 'shared')]
    if not all(path.is_dir() for path in includes): raise RuntimeError('Required WDK include directories unavailable')
    input_paths = set(audit.PRODUCTION) | audit.NEW_FILES | {
        'inc/upstream-integration-20261003/bc250_pm4_bounds.h', 'build.bat',
        'research/offline-20261003/run-cpu-tests.py'}
    # Include all model inputs and unchanged KMD/header inputs in the no-drift check.
    for folder in ('inc', 'src/kmd', 'research/windows'):
        for path in (ROOT / folder).rglob('*'):
            if path.is_file() and path.suffix in ('.c', '.h', '.py', '.json', '.txt'):
                input_paths.add(path.relative_to(ROOT).as_posix())
    before = {path: sha(ROOT / path) for path in sorted(input_paths)}
    (ROOT / 'output').mkdir(exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='passive-port-', dir=ROOT / 'output'))
    print('Local output:', out, flush=True)
    history = []
    execute = original.execute
    execute('audit-passive-port', [sys.executable, '-B', str(HERE / 'audit-passive-port.py'), '--output', str(out)], out, history)
    execute('catalog-generation', [sys.executable, '-B', str(ROOT / 'research/windows/gc-source-map-20260930/catalog.py')], out, history)
    artifacts = []
    for name, relative in original.TESTS:
        exe = out / (name + '.exe')
        artifacts.append(exe)
        execute('compile-' + name, [compiler, '/nologo', '/W4', '/WX', '/O2',
            '/I' + str(ROOT / 'inc'), '/Fe:' + str(exe), str(ROOT / 'research/windows' / relative)], out, history)
        execute('run-' + name, [str(exe)], out, history)
    for name, source in (('hardening', HERE / 'test-hardening.c'), ('policy', HERE / 'test-policy.c')):
        exe = out / (name + '.exe')
        artifacts.append(exe)
        execute('compile-' + name, [compiler, '/nologo', '/W4', '/WX', '/O2',
            '/I' + str(ROOT / 'inc'), '/I' + str(out),
            '/FI' + str(ROOT / audit.POLICY), '/Fe:' + str(exe), str(source)], out, history)
        execute('run-' + name, [str(exe)], out, history)
    reject_override(compiler, out, history)
    for unit in audit.UNITS:
        obj = out / ('kmd-' + Path(unit).stem + '.obj')
        artifacts.append(obj)
        execute('compile-only-' + Path(unit).stem, [compiler, '/nologo', '/c', '/kernel',
            '/W3', '/Od', '/DAMD64', '/D_AMD64_', '/DAMDBC250_DREAM_V3',
            *['/I' + str(path) for path in includes], '/I' + str(ROOT / 'inc'),
            '/I' + str(ROOT / 'src/kmd'), '/Fo:' + str(obj), str(ROOT / 'src/kmd' / unit)], out, history)
    expected = 2 + 2 * len(original.TESTS) + 4 + 1 + len(audit.UNITS)
    if len(history) != expected or expected != 38:
        raise RuntimeError('Incomplete stage history')
    failures = [item for item in history if item['returncode'] != 0]
    if len(failures) != 1 or failures[0]['stage'] != 'policy-override-rejected' or not failures[0].get('expected_failure'):
        raise RuntimeError('Unexpected failed stage')
    if len(artifacts) != 24 or any(not item.is_file() or item.stat().st_size == 0 for item in artifacts):
        raise RuntimeError('Missing/empty expected EXE/OBJ artifact')
    after = {path: sha(ROOT / path) for path in sorted(input_paths)}
    if before != after: raise RuntimeError('Source changed during acceptance; repeat review/test')
    result = dict(scope='OFFLINE_PASSIVE_PORT_ACCEPTANCE', all_expected_outcomes_passed=True,
        stages=history, source_sha256=after,
        artifact_sha256={item.name: sha(item) for item in artifacts},
        runtime_gpu_permission=False, hardware_tested=False, driver_linked=False,
        driver_loaded=False, installable_package=False, whole_driver_safety_certified=False,
        cpu_models_kernel_integrated=False, kmd_compile_only_units=len(audit.UNITS), ram_tests=11)
    (out / 'RESULT.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('PASS: 11 RAM test programs, override rejection, 13 KMD /c units; NO INSTALLATION')


if __name__ == '__main__':
    main()
