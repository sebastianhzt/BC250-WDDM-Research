"""CPU DMA-run successor campaign; never WDK, DMA APIs or driver installation."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile

sys.dont_write_bytecode = True
for stream in (sys.stdout, sys.stderr):
    if hasattr(stream, 'reconfigure'):
        stream.reconfigure(encoding='utf-8', errors='replace')
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
spec = importlib.util.spec_from_file_location('domain_runner',
    ROOT / 'research/windows/domain-backend-20261003/verify-offline.py')
previous = importlib.util.module_from_spec(spec)
spec.loader.exec_module(previous)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    if os.name != 'nt' or os.environ.get('VSCMD_ARG_TGT_ARCH', '').lower() != 'x64':
        raise RuntimeError('Use Windows x64 Native Tools Command Prompt for VS 2022')
    compiler = shutil.which('cl.exe')
    if not compiler: raise RuntimeError('cl.exe unavailable')
    previous.original.negative_control()
    inputs = {'README.md', 'build.bat', 'research/offline-20261003/run-cpu-tests.py'}
    for directory in ('inc', 'src/kmd', 'research/windows'):
        for path in (ROOT / directory).rglob('*'):
            if path.is_file() and path.suffix in ('.c', '.h', '.py', '.json', '.txt'):
                inputs.add(path.relative_to(ROOT).as_posix())
    before = {relative: sha(ROOT / relative) for relative in sorted(inputs)}
    (ROOT / 'output').mkdir(exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='dma-runs-', dir=ROOT / 'output'))
    print('Local output:', out, flush=True)
    history = []
    execute = previous.original.execute
    execute('audit-dma-runs', [sys.executable, '-B', str(HERE / 'audit-dma-runs.py'), '--output', str(out)], out, history)
    execute('catalog-generation', [sys.executable, '-B', str(ROOT / 'research/windows/gc-source-map-20260930/catalog.py')], out, history)
    tests = [('dma-runs', HERE / 'test-dma-runs.c'),
        ('domain-backend', ROOT / 'research/windows/domain-backend-20261003/test-domain-backend.c')]
    tests += [(name, ROOT / 'research/windows' / relative) for name, relative in previous.original.TESTS]
    tests += [('address-domains', ROOT / 'research/windows/address-domains-20261003/test-address-domains.c'),
        ('control-mock', ROOT / 'research/windows/passive-control-20261003/test-control.c')]
    artifacts = []
    for name, source in tests:
        executable = out / (name + '.exe')
        artifacts.append(executable)
        execute('compile-' + name, [compiler, '/nologo', '/W4', '/WX', '/O2',
            '/I' + str(ROOT / 'inc'), '/Fe:' + str(executable), str(source)], out, history)
        execute('run-' + name, [str(executable)], out, history)
    if len(tests) != 13 or len(history) != 28 or any(item['returncode'] != 0 for item in history):
        raise RuntimeError('Incomplete successful 28-stage history')
    if len(artifacts) != 13 or any(not path.is_file() or not path.stat().st_size for path in artifacts):
        raise RuntimeError('Missing/empty expected 13 RAM-test EXE artifacts')
    after = {relative: sha(ROOT / relative) for relative in sorted(inputs)}
    if before != after: raise RuntimeError('Source changed during acceptance; re-review and repeat')
    result = dict(scope='DMA_RUNS_CPU_BACKEND_RAM_ONLY', all_steps_passed=True,
        stages=history, source_sha256=after,
        artifact_sha256={path.name: sha(path) for path in artifacts}, ram_tests=13,
        runtime_gpu_permission=False, driver_linked=False, driver_loaded=False,
        kernel_compiled=False, installable_package=False, hardware_tested=False,
        w2p_memory_ownership=False, dma_translation_validated=False,
        windows_dma_owner=False, scatter_gather_os_api_tested=False,
        cpu_models_kernel_integrated=False)
    (out / 'RESULT.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('PASS: 13 RAM EXE / 28 stages; DMA runs normalized, no Windows ownership or GPU proof')


if __name__ == '__main__':
    main()
