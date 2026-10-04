"""Original deterministic schema tests. NO compiler, OS DMA or GPU calls."""
import copy
import importlib.util
from pathlib import Path
import sys

sys.dont_write_bytecode = True


def run(verifier):
    manifest = verifier.parse((Path(__file__).parent / 'requirements.json').read_text(encoding='utf-8'))
    checks = 0
    def check(condition):
        nonlocal checks
        checks += 1
        if not condition: raise AssertionError('Contract control failed')
    result = verifier.validate_manifest(manifest)
    check(result['contract_valid'] is True)
    check(result['verdict'] == 'BLOCKED_FOR_REAL_DMA')
    check(result['real_verified_requirements'] == 0 and result['requirement_count'] == 20)
    for flag in verifier.FLAGS: check(result[flag] is False)
    verifier.validate_sources(manifest); check(True)
    def rejects(change):
        candidate = copy.deepcopy(manifest); change(candidate)
        try: verifier.validate_manifest(candidate)
        except ValueError: check(True); return
        raise AssertionError('Unsafe/malformed document accepted')
    for flag in verifier.FLAGS:
        for value in (True, 1, 'false', None):
            rejects(lambda m, f=flag, v=value: m.__setitem__(f, v))
        rejects(lambda m, f=flag: m.pop(f))
    for index in range(20):
        rejects(lambda m, n=index: m['requirements'][n].__setitem__('status', 'verified'))
        rejects(lambda m, n=index: m['requirements'][n].__setitem__('real_guarantee_verified', True))
        rejects(lambda m, n=index: m['requirements'][n].__setitem__('real_guarantee_verified', 0))
        rejects(lambda m, n=index: m['requirements'][n].__setitem__('depends', [m['requirements'][n]['id']]))
        rejects(lambda m, n=index: m['requirements'][n].__setitem__('acceptance', ''))
        rejects(lambda m, n=index: m['requirements'][n].__setitem__('evidence',
            {'path': '../../Windows/System32/drivers/atikmdag.sys', 'token': 'owned'}))
    for index in range(len(verifier.ORDER)):
        rejects(lambda m, n=index: m['release_order'].pop(n))
    for path in verifier.PROTECTED:
        rejects(lambda m, p=path: m['protected_source_sha256_lf'].pop(p))
        rejects(lambda m, p=path: m['protected_source_sha256_lf'].__setitem__(p, '0' * 64))
    rejects(lambda m: m.__setitem__('scope', 'PRODUCTION'))
    rejects(lambda m: m.__setitem__('schema_version', True))
    rejects(lambda m: m.__setitem__('source_baseline', '0' * 40))
    rejects(lambda m: m['requirements'].pop())
    rejects(lambda m: m['requirements'].append(copy.deepcopy(m['requirements'][0])))
    rejects(lambda m: m['requirements'][1].__setitem__('id', m['requirements'][0]['id']))
    rejects(lambda m: m['requirements'].reverse())
    rejects(lambda m: m.__setitem__('driver_loaded', True))
    try: verifier.parse('{"real_dma_permitted": false, "real_dma_permitted": true}')
    except ValueError: check(True)
    else: raise AssertionError('Duplicate JSON key accepted')
    # Reports must not turn schema PASS into runtime PASS, even after all controls.
    result = verifier.validate_manifest(manifest)
    check(result['real_dma_permitted'] is False and result['production_execution_allowed'] is False)
    return checks


if __name__ == '__main__':
    path = Path(__file__).parent / 'verify-contract.py'
    spec = importlib.util.spec_from_file_location('contract_verifier', path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module; spec.loader.exec_module(module)
    print('PASS:', run(module), 'schema checks; real DMA remains BLOCKED')
