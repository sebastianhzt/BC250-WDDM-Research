"""One NEW RAM suite O2/Od; no kernel/driver builds, no prior-suite rerun."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
spec = importlib.util.spec_from_file_location('original_runner', ROOT / 'research/offline-20261003/run-cpu-tests.py')
original = importlib.util.module_from_spec(spec); spec.loader.exec_module(original)
spec = importlib.util.spec_from_file_location('owner_auditor', HERE / 'audit-owner-anchor.py')
auditor = importlib.util.module_from_spec(spec); spec.loader.exec_module(auditor)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    if os.name != 'nt' or os.environ.get('VSCMD_ARG_TGT_ARCH', '').lower() != 'x64':
        raise RuntimeError('Use Windows x64 Native Tools VS2022')
    if any(os.environ.get(k, '').strip() for k in ('CL', '_CL_')):
        raise RuntimeError('Hidden compiler flags forbidden')
    compiler = shutil.which('cl.exe')
    if not compiler: raise RuntimeError('Compiler unavailable')
    original.negative_control()
    files = auditor.FILES | set(auditor.PROTECTED) | {'research/offline-20261003/run-cpu-tests.py'}
    before = {p: sha(ROOT / p) for p in sorted(files)}
    (ROOT / 'output').mkdir(exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='owner-anchor-', dir=ROOT / 'output'))
    print('Local output:', out, flush=True)
    history = []; artifacts = []
    original.execute('audit-owner-anchor', [sys.executable, '-B', str(HERE / 'audit-owner-anchor.py'), '--output', str(out)], out, history)
    for profile in ('O2', 'Od'):
        exe = out / ('owner-anchor-' + profile + '.exe'); artifacts.append(exe)
        original.execute('compile-' + profile, [compiler, '/nologo', '/W4', '/WX', '/' + profile,
            '/Fe:' + str(exe), str(HERE / 'test-owner-anchor.c')], out, history)
        original.execute('run-' + profile, [str(exe)], out, history)
    if len(history) != 5 or any(x['returncode'] != 0 for x in history):
        raise RuntimeError('Incomplete five-stage acceptance')
    if any(not p.is_file() or not p.stat().st_size for p in artifacts):
        raise RuntimeError('Missing RAM EXE')
    after = {p: sha(ROOT / p) for p in sorted(files)}
    if before != after: raise RuntimeError('Source changed; re-review before repeat')
    result = dict(scope='NEW_RAM_OWNER_REQUEST_WORKER_CALLBACK_MODEL', all_steps_passed=True,
        stages=history, source_sha256=after, artifact_sha256={p.name: sha(p) for p in artifacts},
        ram_suites=1, compiler_profiles=['O2', 'Od'], predecessor_c_suites_rerun=False,
        kernel_units_compiled=0, driver_linked=False, driver_loaded=False, installable_package=False,
        real_irp_mdl_pdo_ownership=False, pnp_rundown_validated=False, real_windows_dma_tested=False,
        production_execution_allowed=False, hardware_tested=False, w2p_memory_ownership=False,
        dma_translation_validated=False, irql_or_concurrency_validated=False, uma_4_or_6_gib_verified=False)
    (out / 'RESULT.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('PASS: ONE new RAM suite / O2+Od / five stages; NO kernel/driver/OS DMA, prior suites NOT rerun')


if __name__ == '__main__':
    main()
