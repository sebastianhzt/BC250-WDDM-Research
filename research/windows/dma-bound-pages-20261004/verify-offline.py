"""RAM fake SG provenance + 21 regressions; four standalone WDK /c units, NO LOAD."""
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
    if any(os.environ.get(name, '').strip() for name in ('CL', '_CL_')):
        raise RuntimeError('Hidden compiler flags CL/_CL_ must be empty; refusing build')
    original.negative_control()
    wdk = Path(os.environ.get('BC250_WDK_ROOT', str(Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)')) / 'Windows Kits/10')))
    version = os.environ.get('BC250_WDK_VERSION', '10.0.26100.0')
    include_dirs = [wdk / 'Include' / version / part for part in ('km', 'km/crt', 'shared')]
    if not all(path.is_dir() for path in include_dirs): raise RuntimeError('Required WDK headers unavailable')
    inputs = {'README.md', 'build.bat', 'research/offline-20261003/run-cpu-tests.py'}
    for directory in ('inc', 'src/kmd', 'research/windows'):
        for path in (ROOT / directory).rglob('*'):
            if path.is_file() and path.suffix in ('.c', '.h', '.py', '.json', '.txt'):
                inputs.add(path.relative_to(ROOT).as_posix())
    before = {relative: sha(ROOT / relative) for relative in sorted(inputs)}
    (ROOT / 'output').mkdir(exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='dma-bound-pages-', dir=ROOT / 'output'))
    print('Local output:', out, flush=True)
    history = []; execute = original.execute
    execute('audit-bound-pages', [sys.executable, '-B', str(HERE / 'audit-bound-pages.py'), '--output', str(out)], out, history)
    execute('catalog-generation', [sys.executable, '-B', str(ROOT / 'research/windows/gc-source-map-20260930/catalog.py')], out, history)
    tests = [('dma-bound-pages', HERE / 'test-bound-pages.c'),
        ('dma-map-leases', ROOT / 'research/windows/dma-map-leases-20261004/test-map-leases.c'),
        ('dma-leases', ROOT / 'research/windows/dma-leases-20261004/test-leases.c'),
        ('dma-resource', ROOT / 'research/windows/dma-resource-20261004/test-resource.c'),
        ('dma-gate', ROOT / 'research/windows/dma-gate-20261004/test-gate.c'),
        ('dma-lifecycle', ROOT / 'research/windows/dma-lifecycle-20261004/test-lifecycle.c'),
        ('dma-bridge', ROOT / 'research/windows/dma-bridge-20261004/test-bridge.c'),
        ('dma-windows-mock', ROOT / 'research/windows/dma-windows-20261004/test-adapter.c'),
        ('dma-owner', ROOT / 'research/windows/dma-owner-20261003/test-dma-owner.c'),
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
    kernel_obj = out / 'bc250-dma-adapter-kernel-only.obj'; artifacts.append(kernel_obj)
    execute('compile-dma-kernel-only', [compiler, '/nologo', '/c', '/kernel', '/W4', '/WX',
        '/Od', '/DAMD64', '/D_AMD64_', '/UBC250_DMA_ADAPTER_MOCK',
        *['/I' + str(path) for path in include_dirs],
        '/Fo:' + str(kernel_obj), str(ROOT / 'research/windows/dma-windows-20261004/bc250_dma_adapter.c')], out, history)
    gate_obj = out / 'bc250-dma-gate-kernel-only.obj'; artifacts.append(gate_obj)
    execute('compile-gate-kernel-only', [compiler, '/nologo', '/c', '/kernel', '/W4', '/WX',
        '/Od', '/DAMD64', '/D_AMD64_', '/UBC250_DMA_GATE_MOCK', '/UBC250_DMA_ADAPTER_MOCK',
        *['/I' + str(path) for path in include_dirs],
        '/Fo:' + str(gate_obj), str(ROOT / 'research/windows/dma-gate-20261004/bc250_dma_gate.c')], out, history)
    resource_obj = out / 'bc250-dma-resource-kernel-only.obj'; artifacts.append(resource_obj)
    execute('compile-resource-kernel-only', [compiler, '/nologo', '/c', '/kernel', '/W4', '/WX',
        '/Od', '/DAMD64', '/D_AMD64_', '/UBC250_DMA_RESOURCE_MOCK',
        '/UBC250_DMA_GATE_MOCK', '/UBC250_DMA_ADAPTER_MOCK',
        *['/I' + str(path) for path in include_dirs],
        '/Fo:' + str(resource_obj), str(ROOT / 'research/windows/dma-resource-20261004/bc250_dma_resource.c')], out, history)
    leases_obj = out / 'bc250-dma-leases-kernel-only.obj'; artifacts.append(leases_obj)
    execute('compile-leases-kernel-only', [compiler, '/nologo', '/c', '/kernel', '/W4', '/WX',
        '/Od', '/DAMD64', '/D_AMD64_', '/UBC250_DMA_LEASES_MOCK',
        '/UBC250_DMA_GATE_MOCK', '/UBC250_DMA_ADAPTER_MOCK',
        *['/I' + str(path) for path in include_dirs],
        '/Fo:' + str(leases_obj), str(ROOT / 'research/windows/dma-leases-20261004/bc250_dma_leases.c')], out, history)
    if len(tests) != 22 or len(history) != 50 or any(item['returncode'] != 0 for item in history):
        raise RuntimeError('Incomplete successful 50-stage history')
    if len(artifacts) != 26 or any(not path.is_file() or not path.stat().st_size for path in artifacts):
        raise RuntimeError('Missing/empty expected 22 EXE + 4 OBJ')
    after = {relative: sha(ROOT / relative) for relative in sorted(inputs)}
    if before != after: raise RuntimeError('Sources changed during acceptance; re-review and repeat')
    result = dict(scope='RAM_FAKE_SG_BOUND_PAGES_OFFLINE', all_steps_passed=True,
        stages=history, source_sha256=after,
        artifact_sha256={path.name: sha(path) for path in artifacts}, ram_tests=22,
        fake_sg_to_cpu_provenance_tested=True, real_dma_page_binding_verified=False, actual_candidate_source_mock_tested=True, bridge_ram_only=True, lifecycle_ram_only=True, real_pnp_rundown_validated=False, gate_source_mock_tested=True, gate_kernel_compile_only=True, resource_source_mock_tested=True, leases_source_mock_tested=True, real_map_consumers_supported=False, kernel_compile_only_units=4,
        production_execution_allowed=False, driver_linked=False, driver_loaded=False,
        installable_package=False, hardware_tested=False, real_windows_dma_tested=False,
        w2p_memory_ownership=False, dma_translation_validated=False,
        irql_or_concurrency_validated=False, cpu_models_kernel_integrated=False,
        real_provider_rundown_validated=False, async_cancellation_implemented=False,
        uma_4_or_6_gib_verified=False)
    (out / 'RESULT.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('PASS: 22 RAM suites / 50 stages / 4 standalone WDK OBJ; fake SG provenance only, NO DMA/INSTALL')


if __name__ == '__main__':
    main()
