"""Simulated DMA-owner successor campaign, never OS DMA/WDK/driver install."""
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
spec = importlib.util.spec_from_file_location('original_runner', ROOT / 'research/offline-20261003/run-cpu-tests.py')
original = importlib.util.module_from_spec(spec)
spec.loader.exec_module(original)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    if os.name != 'nt' or os.environ.get('VSCMD_ARG_TGT_ARCH', '').lower() != 'x64':
        raise RuntimeError('Use Windows x64 Native Tools Command Prompt for VS 2022')
    compiler = shutil.which('cl.exe')
    if not compiler: raise RuntimeError('cl.exe unavailable')
    original.negative_control()
    inputs = {'README.md', 'build.bat', 'research/offline-20261003/run-cpu-tests.py'}
    for directory in ('inc', 'src/kmd', 'research/windows'):
        for path in (ROOT / directory).rglob('*'):
            if path.is_file() and path.suffix in ('.c', '.h', '.py', '.json', '.txt'):
                inputs.add(path.relative_to(ROOT).as_posix())
    before = {relative: sha(ROOT / relative) for relative in sorted(inputs)}
    (ROOT / 'output').mkdir(exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='dma-owner-', dir=ROOT / 'output'))
    print('Local output:', out, flush=True)
    history = []
    execute = original.execute
    execute('audit-dma-owner', [sys.executable, '-B', str(HERE / 'audit-dma-owner.py'), '--output', str(out)], out, history)
    execute('catalog-generation', [sys.executable, '-B', str(ROOT / 'research/windows/gc-source-map-20260930/catalog.py')], out, history)
    tests = [('dma-owner', HERE / 'test-dma-owner.c'),
        ('dma-runs', ROOT / 'research/windows/dma-runs-20261003/test-dma-runs.c'),
        ('domain-backend', ROOT / 'research/windows/domain-backend-20261003/test-domain-backend.c')]
    tests += [(name, ROOT / 'research/windows' / relative) for name, relative in original.TESTS]
    tests += [('address-domains', ROOT / 'research/windows/address-domains-20261003/test-address-domains.c'),
        ('control-mock', ROOT / 'research/windows/passive-control-20261003/test-control.c')]
    artifacts = []
    for name, source in tests:
        executable = out / (name + '.exe'); artifacts.append(executable)
        execute('compile-' + name, [compiler, '/nologo', '/W4', '/WX', '/O2',
            '/I' + str(ROOT / 'inc'), '/Fe:' + str(executable), str(source)], out, history)
        execute('run-' + name, [str(executable)], out, history)
    if len(tests) != 14 or len(history) != 30 or any(item['returncode'] != 0 for item in history):
        raise RuntimeError('Incomplete successful 30-stage history')
    if len(artifacts) != 14 or any(not path.is_file() or not path.stat().st_size for path in artifacts):
        raise RuntimeError('Missing/empty expected 14 RAM-test EXE artifacts')
    after = {relative: sha(ROOT / relative) for relative in sorted(inputs)}
    if before != after: raise RuntimeError('Source changed during acceptance; re-review and repeat')
    result = dict(scope='SIMULATED_DMA_RESOURCE_LIFETIME_RAM_ONLY', all_steps_passed=True,
        stages=history, source_sha256=after,
        artifact_sha256={path.name: sha(path) for path in artifacts}, ram_tests=14,
        runtime_gpu_permission=False, driver_linked=False, driver_loaded=False,
        kernel_compiled=False, installable_package=False, hardware_tested=False,
        w2p_memory_ownership=False, dma_translation_validated=False,
        windows_dma_owner=False, windows_dma_api_tested=False,
        irql_or_concurrency_validated=False, cpu_models_kernel_integrated=False,
        real_provider_rundown_validated=False, uma_4_or_6_gib_verified=False)
    (out / 'RESULT.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('PASS: 14 RAM EXE / 30 stages; lifetime simulated, no Windows DMA or GPU proof')


if __name__ == '__main__':
    main()
