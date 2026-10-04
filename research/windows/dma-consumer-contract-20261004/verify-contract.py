"""Original source/design audit ONLY. Never authorizes or executes DMA."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
BASE = 'ddb76daf248b55303505905f0ac7589a8512f91f'
PREFIX = 'research/windows/dma-consumer-contract-20261004/'
FILES = {PREFIX + name for name in ('README.txt', 'requirements.json',
    'verify-contract.py', 'test-contract.py', 'primary-source.json')}
MANIFEST_SHA256_LF = 'cfa7007162c36c73b226e74a52d831c810361381d5688dcff6d4c06362456094'
FLAGS = ('production_execution_allowed', 'real_dma_permitted', 'hardware_tested',
    'installable_package', 'w2p_memory_ownership')
SPEC = {
  "A01": [
    "acquire_only",
    "not_implemented",
    [],
    "research/windows/dma-gate-20261004/bc250_dma_gate.h",
    "Rundown does NOT solve publishing/removing this pointer or PDO lifetime.",
    "remove"
  ],
  "A02": [
    "acquire_only",
    "not_implemented",
    [
      "A01"
    ],
    "research/windows/dma-windows-20261004/bc250_dma_adapter.h",
    "Trusted PDO/description/locked single MDL remain alive until release/close.",
    "mdl"
  ],
  "A03": [
    "acquire_only",
    "mock_only",
    [
      "A01",
      "A02"
    ],
    "research/windows/dma-windows-20261004/bc250_dma_adapter.c",
    "owner->RequestIssued = 1U;",
    "get"
  ],
  "A04": [
    "acquire_only",
    "not_implemented",
    [
      "A01"
    ],
    "research/windows/dma-windows-20261004/bc250_dma_adapter.c",
    "Capabilities are caller-supplied, NOT inferred from Linux or UMA size.",
    "get"
  ],
  "A05": [
    "acquire_only",
    "mock_only",
    [
      "A02",
      "A03",
      "A04"
    ],
    "research/windows/dma-capture-20261004/bc250_dma_capture.c",
    "total != owner->Request.Bytes",
    "get"
  ],
  "A06": [
    "acquire_only",
    "mock_only",
    [
      "A01"
    ],
    "research/windows/dma-gate-20261004/bc250_dma_gate.h",
    "Quiesce protects API access,",
    "remove"
  ],
  "A07": [
    "acquire_only",
    "mock_only",
    [
      "A05",
      "A06"
    ],
    "research/windows/snapshot-cpu-20261004/bc250_snapshot_cpu.h",
    "if (m->Snapshot.Reference.Owner) m->HasSnapshot = 1U;",
    "local"
  ],
  "A08": [
    "acquire_only",
    "mock_only",
    [
      "A07"
    ],
    "research/windows/snapshot-cpu-20261004/bc250_snapshot_cpu.h",
    "if (m->Session->Mappings[i].Live) return BC250_VM_SESSION_IN_USE;",
    "local"
  ],
  "A09": [
    "acquire_only",
    "not_implemented",
    [
      "A01",
      "A02",
      "A06"
    ],
    "research/windows/dma-windows-20261004/bc250_dma_adapter.c",
    "NOT asynchronous CancelAdapterChannel or GPU job cancellation.",
    "get"
  ],
  "A10": [
    "acquire_only",
    "not_implemented",
    [
      "A01",
      "A02",
      "A09"
    ],
    "research/windows/dma-gate-20261004/bc250_dma_gate.h",
    "NOT map references, MDLs, DMA ownership, PnP completion or GPU quiescence.",
    "remove"
  ],
  "A11": [
    "acquire_only",
    "not_implemented",
    [
      "A01",
      "A02",
      "A03",
      "A04",
      "A05",
      "A06",
      "A07",
      "A08",
      "A09",
      "A10"
    ],
    "research/windows/dma-windows-20261004/bc250_dma_adapter.c",
    "return FALSE; /* Production execution gate: permanently closed. */",
    "local"
  ],
  "T01": [
    "transfer",
    "not_implemented",
    [
      "A11"
    ],
    "research/windows/snapshot-cpu-20261004/bc250_snapshot_cpu.h",
    "#error Snapshot CPU association is NEVER kernel integrated",
    "local"
  ],
  "T02": [
    "transfer",
    "not_implemented",
    [
      "T01",
      "A02",
      "A03"
    ],
    "research/windows/dma-windows-20261004/bc250_dma_adapter.c",
    "owner->Get = operations->GetScatterGatherListEx; owner->Free = operations->FreeAdapterObject;",
    "flush"
  ],
  "T03": [
    "transfer",
    "not_implemented",
    [
      "T01",
      "T02"
    ],
    "research/windows/dma-gate-20261004/bc250_dma_gate.h",
    "NOT map references, MDLs, DMA ownership, PnP completion or GPU quiescence.",
    "flush"
  ],
  "G01": [
    "gpu_publish",
    "not_implemented",
    [
      "T01",
      "A05",
      "A08"
    ],
    "inc/address-domains-20261003/bc250_address_domains.h",
    "#define BC250_AD_DMA_LOGICAL 5U",
    "local"
  ],
  "G02": [
    "gpu_publish",
    "not_implemented",
    [
      "G01",
      "T03"
    ],
    "research/windows/snapshot-cpu-20261004/bc250_snapshot_cpu.h",
    "NOT a per-map OS DMA ref, actual consumer, GPU mapping or W2P proof.",
    "local"
  ],
  "R01": [
    "release",
    "mock_only",
    [
      "A07",
      "A08"
    ],
    "research/windows/snapshot-cpu-20261004/bc250_snapshot_cpu.h",
    "if (native == STATUS_SUCCESS) m->SnapshotReleased = 1U;",
    "local"
  ],
  "R02": [
    "release",
    "not_implemented",
    [
      "T02",
      "T03",
      "G02",
      "R01"
    ],
    "research/windows/dma-windows-20261004/bc250_dma_adapter.c",
    "owner->Free(owner->Adapter, DeallocateObject);",
    "flush"
  ],
  "R03": [
    "release",
    "not_implemented",
    [
      "R02",
      "A10"
    ],
    "research/windows/dma-windows-20261004/bc250_dma_adapter.c",
    "owner->Busy = 1U; owner->Put(owner->Adapter);",
    "free"
  ],
  "R04": [
    "release",
    "not_implemented",
    [
      "A11",
      "T01",
      "T02",
      "T03",
      "G01",
      "G02",
      "R01",
      "R02",
      "R03"
    ],
    "research/windows/dma-windows-20261004/bc250_dma_adapter.c",
    "return FALSE; /* Production execution gate: permanently closed. */",
    "local"
  ]
}
PROTECTED = {
  "research/windows/dma-windows-20261004/bc250_dma_adapter.h": "f4aacae0529f419ae48c7340e7fac26b1ee19d4d3337d24990139edf5fb41fb2",
  "research/windows/dma-windows-20261004/bc250_dma_adapter.c": "e8905ef2ac418b6f7d9c09203df782fcaf54dfcadf771d3fc5a03103442d16ed",
  "research/windows/dma-gate-20261004/bc250_dma_gate.h": "8866f42b5e059d1e42f6f7f6c1e718594a0414dadc1389c6c084e8f6705ce3b4",
  "research/windows/dma-gate-20261004/bc250_dma_gate.c": "0d0552845e2254581cecc15dbac96454c2fd96fb5c40da45c796341926dc570b",
  "research/windows/dma-capture-20261004/bc250_dma_capture.h": "f727aacc13c4f446c67320884ae2f4a2242bb5d25a03155eeaa624f0bd145fa9",
  "research/windows/dma-capture-20261004/bc250_dma_capture.c": "22f9cd6126eecb543fd5ac15ad7d4c3e0f083486ee3135dec124b778937fd0bd",
  "research/windows/snapshot-cpu-20261004/bc250_snapshot_cpu.h": "895f127a971ebc7f1c50f3ce9c19d1ff4806c2521adac779f0ec2b00bbd0d12d",
  "research/windows/snapshot-cpu-20261004/test-snapshot-cpu.c": "83d9869a6d983131fcff243a08169d6c1698b6f9aa57df67db4c8f1c2de4fad1",
  "inc/address-domains-20261003/bc250_address_domains.h": "21b3406ad79cd1a9ae396a220c97f64b4da07c2682030e05018d467eb3dae4d3",
  "build.bat": "871a9589a7cf561f1ae5262fe54d4d558e9b6c0f22ed88237c24717c2cede7d4",
  "src/kmd/amdbc250_dream_kmd.c": "6a559f990d48f5864f2675440a2709f323e02b085cf563ae4c78025e1aa0f085",
  "research/windows/domain-backend-20261003/CREDITS.txt": "2074a4cbceb76262ad0c2c188eeecca916d5badc781aa1f7d24861006941920f"
}
ORDER = ["close_admission","drain_calls","prove_device_quiescent","retire_gpu_translations","retire_cpu_maps_and_backing","final_dma_sync","release_snapshot","free_dma_resources","release_owned_mdl_and_complete_irp","retire_external_anchor"]
REFERENCE_URLS = {
  "get": "https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nc-wdm-pget_scatter_gather_list_ex",
  "free": "https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nc-wdm-pfree_adapter_object",
  "flush": "https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nc-wdm-pflush_adapter_buffers_ex",
  "remove": "https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/using-remove-locks",
  "mdl": "https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-mmprobeandlockpages"
}


def require(condition, message):
    if not condition: raise ValueError(message)


def unique_pairs(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, 'Duplicate JSON key: ' + key)
        result[key] = value
    return result


def parse(text):
    return json.loads(text, object_pairs_hook=unique_pairs)


def validate_manifest(m):
    require(type(m) is dict and set(m) == {
        'schema_version', 'source_baseline', 'scope', 'protected_source_sha256_lf',
        'release_order', 'requirements', *FLAGS}, 'Manifest shape')
    require(type(m['schema_version']) is int and m['schema_version'] == 1, 'Schema version')
    require(m['source_baseline'] == BASE and m['scope'] == 'DESIGN_AND_SOURCE_AUDIT_ONLY', 'Scope/baseline')
    for flag in FLAGS: require(m[flag] is False, 'Execution/proof claim: ' + flag)
    require(m['protected_source_sha256_lf'] == PROTECTED, 'Protected source inventory')
    require(m['release_order'] == ORDER, 'Release ordering')
    items = m['requirements']
    require(type(items) is list and len(items) == len(SPEC), 'Requirement count')
    seen = set()
    for item in items:
        require(type(item) is dict and set(item) == {'id', 'stage', 'status', 'depends',
            'title', 'acceptance', 'evidence', 'reference', 'real_guarantee_verified'}, 'Entry shape')
        ident = item['id']
        require(type(ident) is str and ident in SPEC and ident not in seen, 'Unknown/duplicate ID')
        seen.add(ident)
        stage, status, depends, path, token, reference = SPEC[ident]
        require(item['stage'] == stage and item['status'] == status, 'Evidence promotion: ' + ident)
        require(item['depends'] == depends, 'Dependency change: ' + ident)
        require(item['evidence'] == {'path': path, 'token': token}, 'Source evidence mismatch')
        require(item['reference'] == reference, 'Primary reference mismatch')
        require(item['real_guarantee_verified'] is False, 'Real guarantee claim: ' + ident)
        for key in ('title', 'acceptance'):
            require(type(item[key]) is str and 0 < len(item[key]) <= 1000, 'Missing criterion')
    require(seen == set(SPEC), 'Missing requirement')
    require([item['id'] for item in items] == list(SPEC), 'Deterministic requirement order')
    visited, active = set(), set()
    def visit(ident):
        require(ident not in active, 'Dependency cycle')
        if ident in visited: return
        active.add(ident)
        for dep in SPEC[ident][2]: visit(dep)
        active.remove(ident); visited.add(ident)
    for ident in SPEC: visit(ident)
    # Success proves only this bounded DOCUMENT/schema. Not real driver eligibility.
    return dict(contract_valid=True, verdict='BLOCKED_FOR_REAL_DMA',
        production_execution_allowed=False, real_dma_permitted=False,
        installable_package=False, hardware_tested=False, w2p_memory_ownership=False,
        requirement_count=len(items), real_verified_requirements=0)


def canonical(path):
    data = path.read_bytes().replace(b'\r\n', b'\n')
    data.decode('utf-8')
    require(b'\r' not in data and b'\0' not in data, 'Invalid text')
    return data


def sha(data):
    return hashlib.sha256(data).hexdigest()


def source_path(relative):
    require(type(relative) is str and relative in PROTECTED, 'Unknown protected path')
    path = ROOT / relative
    require(path.is_file() and not path.is_symlink(), 'Protected regular source required')
    path.resolve().relative_to(ROOT.resolve())
    return path


def validate_sources(m):
    for relative, digest in PROTECTED.items():
        require(sha(canonical(source_path(relative))) == digest, 'Protected source changed: ' + relative)
    for item in m['requirements']:
        text = canonical(source_path(item['evidence']['path'])).decode('utf-8')
        require(item['evidence']['token'] in text, 'Evidence anchor missing')
    # Full source pins AND exact policy bodies. Documentation can't open a policy.
    for relative, symbol, macro in (
        ('research/windows/dma-windows-20261004/bc250_dma_adapter.c', 'Bc250WinDmaPolicy', 'BC250_DMA_ADAPTER_MOCK'),
        ('research/windows/dma-gate-20261004/bc250_dma_gate.c', 'Bc250DmaGatePolicy', 'BC250_DMA_GATE_MOCK'),
        ('research/windows/dma-capture-20261004/bc250_dma_capture.c', 'Bc250DmaCapturePolicy', 'BC250_DMA_CAPTURE_MOCK')):
        text = canonical(source_path(relative)).decode('utf-8')
        match = re.search(r'static BOOLEAN ' + symbol + r'\(void\)\s*\{(.*?)\n\}', text, re.S)
        require(match is not None, 'Policy absent')
        plain = re.sub(r'/\*.*?\*/', '', match.group(1), flags=re.S)
        require('#ifdef ' + macro in plain and
            re.search(r'#else\s*return FALSE;\s*#endif', plain), 'Production policy opened')


def git(*args):
    return subprocess.check_output(['git', '-c', 'safe.directory=' + ROOT.as_posix(),
        '-C', str(ROOT), *args]).decode('utf-8')


def audit():
    git('merge-base', '--is-ancestor', BASE, 'HEAD')
    changed = set(git('diff', '--name-only', '-z', BASE).split('\0')) - {''}
    untracked = set(git('ls-files', '--others', '--exclude-standard', '-z').split('\0')) - {''}
    require(changed | untracked == FILES, 'Exact five-file source-only scope')
    raw_hashes = {}; normalized_hashes = {}
    for relative in sorted(FILES):
        path = ROOT / relative
        require(path.is_file() and not path.is_symlink(), 'Regular contract source required')
        path.resolve().relative_to(ROOT.resolve())
        data = canonical(path); text = data.decode('utf-8')
        require(data.endswith(b'\n') and not data.endswith(b'\n\n'), 'Final newline')
        require(not re.search(r'-----BEGIN [A-Z ]*PRIVATE KEY-----|gh[pousr]_[A-Za-z0-9]{36,}|[A-Za-z]:[\\/]Users[\\/]', text), 'Private export')
        raw_hashes[relative] = sha(path.read_bytes()); normalized_hashes[relative] = sha(data)
    require(normalized_hashes[PREFIX + 'requirements.json'] == MANIFEST_SHA256_LF, 'Reviewed manifest changed')
    m = parse(canonical(HERE / 'requirements.json').decode('utf-8'))
    result = validate_manifest(m); validate_sources(m)
    provenance = parse(canonical(HERE / 'primary-source.json').decode('utf-8'))
    require(provenance['source_baseline'] == BASE and provenance['primary_references'] == REFERENCE_URLS,
        'Primary source inventory')
    require(provenance['implementation_copied_from_external'] is False, 'External implementation import')
    readme = canonical(HERE / 'README.txt').decode('utf-8')
    for token in ('Build11', 'NO SYS/INF/CAT', 'NO permisos de DMA', 'NO recuperacion real',
        'MapRegisterBase', 'CREDITS.txt', 'adquisicion SIN transferencia', 'NO replica'):
        require(token in readme, 'Contract boundary: ' + token)
    spec = importlib.util.spec_from_file_location('contract_tests', HERE / 'test-contract.py')
    tests = importlib.util.module_from_spec(spec); spec.loader.exec_module(tests)
    result['schema_checks'] = tests.run(sys.modules[__name__])
    result.update(baseline=BASE, new_source_sha256=raw_hashes,
        new_source_sha256_lf=normalized_hashes, protected_source_sha256_lf=PROTECTED,
        compiler_invoked=False, c_tests_rerun=False, os_dma_calls_made=False,
        predecessors_unchanged=True, scope='DESIGN_AND_SOURCE_AUDIT_ONLY')
    # Only normal generated report output; no writes to source/driver/registry.
    directory = ROOT / 'output'; directory.mkdir(exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='dma-consumer-contract-', dir=directory))
    (out / 'RESULT.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('PASS contract/schema audit:', result['schema_checks'], 'checks;',
        '20 real obligations BLOCKED; NO C BUILD / DMA / INSTALL')
    print('Local report:', out / 'RESULT.json')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__); parser.parse_args()
    audit()
