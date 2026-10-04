"""Portable MSVC x64 RAM test runner; no WDK or inherited hardware scripts."""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import contextlib
import io
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
TESTS = (
    ('catalog', 'gc-source-map-20260930/test-catalog.c'),
    ('selectors', 'gc-selectors-20260930/test-selectors.c'),
    ('geometry', 'gart-geometry-20261001/test-geometry.c'),
    ('vm-model', 'vm-source-model-20261001/test-vm-model.c'),
    ('dma-page-list', 'upstream-integration-20261003/test-dma-page-list.c'),
    ('vm-backend', 'vm-cpu-backend-20261003/test-vm-cpu-backend.c'),
    ('vm-unmap', 'vm-cpu-lifetime-20261003/test-vm-unmap.c'),
    ('backing-refs', 'vm-cpu-lifetime-20261003/test-backing-refs.c'),
)


def execute(name, argv, out, history):
    (out / (name + '.command.json')).write_text(json.dumps(argv) + '\n', encoding='utf-8')
    result = subprocess.run(argv, cwd=out, capture_output=True, shell=False)
    (out / (name + '.stdout')).write_bytes(result.stdout)
    (out / (name + '.stderr')).write_bytes(result.stderr)
    history.append(dict(stage=name, returncode=result.returncode))
    (out / 'stages.json').write_text(json.dumps(history, indent=2) + '\n', encoding='utf-8')
    for stream in (result.stdout, result.stderr):
        if stream:
            print(stream.decode('utf-8', errors='replace'), end='', flush=True)
    if result.returncode:
        raise RuntimeError(name + ': child exit ' + str(result.returncode))


def negative_control():
    # No compiler, hardware or inherited code: ensure a failed child aborts.
    with tempfile.TemporaryDirectory(prefix='cpu-runner-control-') as folder:
        out = Path(folder)
        history = []
        try:
            with contextlib.redirect_stdout(io.StringIO()):
                execute('fail7', [sys.executable, '-c', 'import sys; sys.exit(7)'], out, history)
                execute('forbidden-next', [sys.executable, '-c', 'print("UNREACHABLE")'], out, history)
        except RuntimeError:
            if (history != [dict(stage='fail7', returncode=7)] or
                (out / 'forbidden-next.command.json').exists() or
                (out / 'forbidden-next.stdout').exists() or
                (out / 'RESULT.json').exists()):
                raise RuntimeError('Fail-closed control failed')
            if json.loads((out / 'stages.json').read_text()) != history:
                raise RuntimeError('Failed-stage recorded history mismatch')
        else:
            raise RuntimeError('Failed child was accepted')


def main():
    if os.name != 'nt' or os.environ.get('VSCMD_ARG_TGT_ARCH', '').lower() != 'x64':
        raise RuntimeError('Use Windows x64 Native Tools Command Prompt for VS 2022')
    compiler = shutil.which('cl.exe')
    if not compiler:
        raise RuntimeError('cl.exe unavailable')
    negative_control()
    (ROOT / 'output').mkdir(exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='offline-cpu-', dir=ROOT / 'output'))
    print('Local output:', out, flush=True)
    history = []
    execute('audit-export', [sys.executable, '-B', str(HERE / 'audit-export.py'), '--output', str(out)], out, history)
    execute('catalog-generation', [sys.executable, '-B', str(ROOT / 'research/windows/gc-source-map-20260930/catalog.py')], out, history)
    executables = []
    for name, relative in TESTS:
        source = ROOT / 'research/windows' / relative
        executable = out / (name + '.exe')
        executables.append(executable)
        execute('compile-' + name, [compiler, '/nologo', '/W4', '/WX', '/O2',
            '/I' + str(ROOT / 'inc'), '/Fe:' + str(executable), str(source)], out, history)
        execute('run-' + name, [str(executable)], out, history)
    if len(history) != 2 + 2 * len(TESTS) or any(stage['returncode'] != 0 for stage in history):
        raise RuntimeError('Incomplete successful stage history')
    if any(not executable.is_file() or executable.stat().st_size == 0 for executable in executables):
        raise RuntimeError('Missing/empty compiled test artifact')
    (out / 'RESULT.json').write_text(json.dumps(dict(scope='USER_MODE_RAM_ONLY',
        all_steps_passed=True, stages=history, hardware_tested=False,
        inherited_driver_passive=False, installable_package=False), indent=2) + '\n', encoding='utf-8')
    print('PASS: standalone RAM models only; inherited driver unchanged, no installation')


if __name__ == '__main__':
    main()
