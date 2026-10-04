"""Actual control source RAM mocks and independent kernel /c; do not load/install."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile

sys.dont_write_bytecode = True
# The original helper preserves raw compiler bytes in logs, but decodes console
# text with replacement. A redirected Windows ANSI stdout cannot encode U+FFFD.
for stream in (sys.stdout, sys.stderr):
    if hasattr(stream, 'reconfigure'):
        stream.reconfigure(encoding='utf-8', errors='replace')
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


original = load('original_ram_runner', ROOT / 'research/offline-20261003/run-cpu-tests.py')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    if os.name != 'nt' or os.environ.get('VSCMD_ARG_TGT_ARCH', '').lower() != 'x64':
        raise RuntimeError('Use Windows x64 Native Tools Command Prompt for VS 2022')
    compiler = shutil.which('cl.exe')
    if not compiler: raise RuntimeError('cl.exe unavailable')
    # Checks exit-7 abort, next child not executed, recorded history, no RESULT.
    # This imports only the old checked subprocess helper/TESTS, never old.main().
    original.negative_control()
    wdk = Path(os.environ.get('BC250_WDK_ROOT', str(Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)')) / 'Windows Kits/10')))
    version = os.environ.get('BC250_WDK_VERSION', '10.0.26100.0')
    include_dirs = [wdk / 'Include' / version / part for part in ('km', 'km/crt', 'shared')]
    if not all(path.is_dir() for path in include_dirs):
        raise RuntimeError('Required WDK include directories unavailable')
    inputs = {'build.bat', 'research/offline-20261003/run-cpu-tests.py'}
    for directory in ('inc', 'src/kmd', 'research/windows'):
        for path in (ROOT / directory).rglob('*'):
            if path.is_file() and path.suffix in ('.c', '.h', '.py', '.json', '.txt'):
                inputs.add(path.relative_to(ROOT).as_posix())
    before = {relative: sha(ROOT / relative) for relative in sorted(inputs)}
    (ROOT / 'output').mkdir(exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='passive-control-', dir=ROOT / 'output'))
    print('Local output:', out, flush=True)
    history = []
    execute = original.execute
    # The previous stage's strict auditor would correctly reject these new files.
    # Use this successor audit; do not weaken or invoke the historical auditor.
    execute('audit-control', [sys.executable, '-B', str(HERE / 'audit-control.py'), '--output', str(out)], out, history)
    execute('catalog-generation', [sys.executable, '-B', str(ROOT / 'research/windows/gc-source-map-20260930/catalog.py')], out, history)
    artifacts = []
    control_exe = out / 'control-mock.exe'
    artifacts.append(control_exe)
    execute('compile-control-mock', [compiler, '/nologo', '/W4', '/WX', '/O2',
        '/I' + str(ROOT / 'inc'), '/Fe:' + str(control_exe), str(HERE / 'test-control.c')], out, history)
    execute('run-control-mock', [str(control_exe)], out, history)
    for name, relative in original.TESTS:
        executable = out / (name + '.exe')
        artifacts.append(executable)
        execute('compile-' + name, [compiler, '/nologo', '/W4', '/WX', '/O2',
            '/I' + str(ROOT / 'inc'), '/Fe:' + str(executable), str(ROOT / 'research/windows' / relative)], out, history)
        execute('run-' + name, [str(executable)], out, history)
    control_obj = out / 'bc250-control-kernel-only.obj'
    artifacts.append(control_obj)
    # Never compile/link the display miniport into this candidate; object only.
    execute('compile-control-kernel-only', [compiler, '/nologo', '/c', '/kernel', '/W4', '/WX',
        '/Od', '/DAMD64', '/D_AMD64_', *['/I' + str(path) for path in include_dirs],
        '/I' + str(ROOT / 'inc'), '/Fo:' + str(control_obj), str(HERE / 'bc250_control.c')], out, history)
    expected = 2 + 2 + 2 * len(original.TESTS) + 1
    if expected != 23 or len(history) != expected or any(item['returncode'] != 0 for item in history):
        raise RuntimeError('Incomplete successful stage history')
    if len(artifacts) != 11 or any(not path.is_file() or path.stat().st_size == 0 for path in artifacts):
        raise RuntimeError('Missing/empty expected 10 EXE + 1 OBJ artifacts')
    after = {relative: sha(ROOT / relative) for relative in sorted(inputs)}
    if before != after: raise RuntimeError('Input source changed during acceptance; re-review and repeat')
    result = dict(scope='STANDALONE_PASSIVE_CONTROL_OFFLINE', all_steps_passed=True,
        stages=history, source_sha256=after,
        artifact_sha256={path.name: sha(path) for path in artifacts},
        actual_control_source_mock_tested=True, kernel_compile_only_units=1, ram_tests=10,
        driver_linked=False, driver_loaded=False, installable_package=False,
        hardware_tested=False, runtime_gpu_permission=False, w2p_memory_ownership=False,
        effective_windows_acl_tested=False, windows_io_lifetime_tested=False,
        real_unlink_failure_cleanup_validated=False, cpu_models_kernel_integrated=False)
    (out / 'RESULT.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('PASS: control RAM mocks, 9 CPU model regressions, independent control /c; NO LOAD/INSTALL')


if __name__ == '__main__':
    main()
