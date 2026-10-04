"""Six NEW RAM-only sources. No driver integration or OS lifetime certification."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import sys

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
BASE = '6e714d55de52cd8513790b7a02164060be0ebdf7'
PREFIX = 'research/windows/owner-anchor-20261004/'
FILES = {PREFIX + name for name in ('bc250_owner_anchor.h', 'test-owner-anchor.c',
    'README.txt', 'primary-source.json', 'audit-owner-anchor.py', 'verify-offline.py')}
HEADER_SHA256 = 'cf9644fbc84d4f4baa6f8884905a4d4e135f08119c61d9f1e126375c44eed552'
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
  "research/windows/domain-backend-20261003/CREDITS.txt": "2074a4cbceb76262ad0c2c188eeecca916d5badc781aa1f7d24861006941920f",
  "research/windows/dma-consumer-contract-20261004/requirements.json": "cfa7007162c36c73b226e74a52d831c810361381d5688dcff6d4c06362456094",
  "research/windows/dma-consumer-contract-20261004/verify-contract.py": "37e652cbffe30358d7860b94dc77005aaa47f9517545e9b37c9bfd4a78504eae",
  "research/windows/snapshot-cpu-20261004/README.txt": "21f4ec4cdca37c8e1be89c599e9290193ca2ee28e56d8aac4f5e299c5e097403"
}
spec = importlib.util.spec_from_file_location('domain_auditor',
    ROOT / 'research/windows/domain-backend-20261003/audit-domain-backend.py')
previous = importlib.util.module_from_spec(spec); spec.loader.exec_module(previous)


def normalize(source):
    return source.replace('static __inline BC250_OA_STATUS ', 'static __inline int ').replace('static __inline void ', 'static __inline int ')


def check_source(source):
    source = normalize(source)
    def body(suffix): return previous.function(source, 'Bc250Oa' + suffix)
    suffixes = ('Disjoint', 'Away', 'Check', 'Guard', 'Output', 'Token', 'Publish',
        'Init', 'Begin', 'Admit', 'Leave', 'Stop', 'Request', 'Cancel', 'Worker',
        'DeviceDebt', 'Callback', 'CallbackLeave', 'Finish', 'Retire')
    calls = set(re.findall(r'\b([A-Za-z_]\w*)\s*\(', previous.stripped(source)))
    allowed = {'if', 'for', 'sizeof', 'memset', 'memcmp'} | {'Bc250Oa' + s for s in suffixes}
    if calls - allowed: raise ValueError('Unexpected OS/external operation: ' + str(calls - allowed))
    tokens = {
        'Disjoint': ('a > top - (pn - 1U)', 'b > top - (qn - 1U)'),
        'Check': ('if (Bc250OwnerAnchorRamAllowed != 1U)', 'r->Self != r',
            'if (r->Fault)', 'c->Id > r->LastCall ||', 'q->Id > r->LastRequest',
            'r->LastRequest - r->Completed != pending', 'r->BorrowedReturned != r->Completed - r->OwnedFreed',
            'c->Id > r->LastCallback', 'parent != 1U'),
        'Guard': ('Bc250OaCheck(r)', 'r->State == BC250_OA_DEAD'),
        'Output': ('Bc250OaAway(r, out, sizeof(*out))', 'memcmp(out, &zero, sizeof(zero))',
            'Bc250OaDisjoint(r->Calls[i].Canonical, sizeof(*out), out, sizeof(*out))',
            'Bc250OaDisjoint(r->Requests[i].Canonical, sizeof(*out), out, sizeof(*out))',
            'Bc250OaDisjoint(r->Callbacks[i].Canonical, sizeof(*out), out, sizeof(*out))'),
        'Token': ('Bc250OaGuard(r)', 't->Self != t', 't->Router != r', 't->Class != kind',
            'r->Calls[t->Slot].Canonical == t', 'r->Requests[t->Slot].Canonical == t',
            'r->Callbacks[t->Slot].Canonical == t'),
        'Init': ('Bc250OwnerAnchorRamAllowed != 1U', 'memcmp(r, &zero, sizeof(zero))', 'r->Self = r'),
        'Begin': ('Bc250OaGuard(r)', 'Bc250OaOutput(r, out)', 'r->LastCall == UINT64_MAX',
            'i == BC250_OA_CALLS', 'r->Calls[i].Id = ++r->LastCall'),
        'Admit': ('Bc250OaToken(r, call, BC250_OA_CALL)', 'if (r->Calls[call->Slot].Decided)',
            'r->State != BC250_OA_ACTIVE', 'r->Calls[call->Slot].Admitted = 1U'),
        'Leave': ('Bc250OaToken(r, call, BC250_OA_CALL)', 'memset(&r->Calls[slot], 0, sizeof(r->Calls[slot]))'),
        'Stop': ('Bc250OaGuard(r)', 'r->State = BC250_OA_STOPPED'),
        'Request': ('Bc250OaToken(r, call, BC250_OA_CALL)', 'r->State != BC250_OA_ACTIVE',
            '!r->Calls[call->Slot].Admitted', 'ownership != BC250_OA_OWNED && ownership != BC250_OA_BORROWED',
            '!Bc250OaOutput(r, out)', 'r->LastRequest == UINT64_MAX', 'i == BC250_OA_REQUESTS',
            'r->Requests[i].Id = ++r->LastRequest'),
        'Cancel': ('Bc250OaToken(r, request, BC250_OA_REQUEST)', 'r->Requests[request->Slot].Cancel = 1U'),
        'Worker': ('Bc250OaToken(r, request, BC250_OA_REQUEST)', 'q->Worker == enter',
            'q->Cancel || r->State != BC250_OA_ACTIVE', 'q->Worker = enter'),
        'DeviceDebt': ('Bc250OaToken(r, request, BC250_OA_REQUEST)', 'q->DeviceDebt == acquire',
            '!q->Worker || q->Cancel || r->State != BC250_OA_ACTIVE', 'q->DeviceDebt = acquire'),
        'Callback': ('Bc250OaToken(r, request, BC250_OA_REQUEST)', '!Bc250OaOutput(r, out)',
            'r->LastCallback == UINT64_MAX', 'i == BC250_OA_CALLBACKS',
            'r->Callbacks[i].RequestId = request->Id'),
        'CallbackLeave': ('Bc250OaToken(r, callback, BC250_OA_CALLBACK)',
            'memset(&r->Callbacks[slot], 0, sizeof(r->Callbacks[slot]))'),
        'Finish': ('Bc250OaToken(r, request, BC250_OA_REQUEST)',
            'r->Requests[slot].Worker || r->Requests[slot].DeviceDebt',
            'r->Callbacks[i].Id && r->Callbacks[i].RequestId == request->Id',
            'Ownership == BC250_OA_OWNED', '++r->OwnedFreed', 'else ++r->BorrowedReturned',
            '++r->Completed', 'memset(&r->Requests[slot], 0, sizeof(r->Requests[slot]))'),
        'Retire': ('Bc250OaGuard(r)', 'r->State != BC250_OA_STOPPED',
            'r->Calls[i].Id', 'r->Requests[i].Id || r->Requests[i].Worker || r->Requests[i].DeviceDebt',
            'r->Callbacks[i].Id', 'r->State = BC250_OA_DEAD')}
    sections = [(body(s), token) for s, entries in tokens.items() for token in entries]
    for section, token in sections:
        if section.count(token) != 1: raise ValueError('Guard missing/nonunique: ' + token)
    for token in ('#ifndef BC250_OWNER_ANCHOR_RAM_ONLY', '#ifdef _KERNEL_MODE',
            '#error Owner anchor model is NOT kernel integrated'):
        if source.count(token) != 1: raise ValueError('RAM-only boundary')
        sections.append((source, token))
    def ordered(s, *entries):
        positions = [body(s).index(x) for x in entries]
        if positions != sorted(positions) or len(set(positions)) != len(positions):
            raise ValueError('Wrong ordering: ' + s)
    ordered('Check', 'Bc250OwnerAnchorRamAllowed != 1U', 'r->Self != r')
    ordered('Init', 'Bc250OwnerAnchorRamAllowed != 1U', 'memcmp(r,', 'r->Self = r')
    ordered('Begin', 'Bc250OaGuard(', 'Bc250OaOutput(', 'r->LastCall == UINT64_MAX',
        'r->Calls[i].Id = ++r->LastCall', 'Bc250OaPublish(')
    ordered('Finish', 'r->Requests[slot].Worker || r->Requests[slot].DeviceDebt',
        'r->Callbacks[i].Id', '++r->OwnedFreed', '++r->Completed', 'memset(')
    ordered('Retire', 'Bc250OaGuard(', 'BC250_OA_STOPPED', 'r->Calls[i].Id',
        'r->Requests[i].Id', 'r->Callbacks[i].Id', 'r->State = BC250_OA_DEAD')
    for s in ('Cancel', 'Stop', 'Worker', 'DeviceDebt', 'Admit'):
        if re.search(r'memset\(|OwnedFreed|BorrowedReturned|Completed|BC250_OA_DEAD;', body(s)):
            raise ValueError('Premature implicit release')
    for s in ('Leave', 'CallbackLeave'):
        tail = previous.stripped(body(s)).split('memset(', 1)[1]
        if not re.search(r'\);\s*return BC250_OA_OK;\s*\}$', tail):
            raise ValueError('Post-drop router/token access')
    return sections, sorted(calls)


def sha(path, lf=False):
    data = path.read_bytes()
    if lf: data = data.replace(b'\r\n', b'\n')
    return hashlib.sha256(data).hexdigest()


def audit(out):
    out = Path(out).resolve(); out.relative_to((ROOT / 'output').resolve())
    if not out.is_dir(): raise ValueError('Scoped output required')
    previous.git('merge-base', '--is-ancestor', BASE, 'HEAD')
    changed = set(previous.git('diff', '--name-only', '-z', BASE).split('\0')) - {''}
    untracked = set(previous.git('ls-files', '--others', '--exclude-standard', '-z').split('\0')) - {''}
    if changed | untracked != FILES: raise ValueError('Exact six NEW source files required')
    hashes = {}
    for relative in sorted(FILES):
        path = ROOT / relative
        if not path.is_file() or path.is_symlink(): raise ValueError('Regular source required')
        path.resolve().relative_to(ROOT.resolve())
        data = path.read_bytes(); canonical = data.replace(b'\r\n', b'\n'); text = canonical.decode('utf-8')
        if b'\0' in data or b'\r' in canonical or not canonical.endswith(b'\n') or canonical.endswith(b'\n\n'):
            raise ValueError('Text/newline invalid')
        if path.suffix in ('.c', '.h') and data != canonical: raise ValueError('C/H requires LF')
        if re.search(r'-----BEGIN [A-Z ]*PRIVATE KEY-----|gh[pousr]_[A-Za-z0-9]{36,}|[A-Za-z]:[\\/]Users[\\/]', text):
            raise ValueError('Private export')
        hashes[relative] = sha(path)
    if hashes[PREFIX + 'bc250_owner_anchor.h'] != HEADER_SHA256: raise ValueError('Header snapshot changed')
    for relative, digest in PROTECTED.items():
        path = ROOT / relative
        if not path.is_file() or path.is_symlink() or sha(path, True) != digest: raise ValueError('Frozen source changed')
    source = normalize((HERE / 'bc250_owner_anchor.h').read_text(encoding='utf-8'))
    sections, calls = check_source(source)
    for section, token in sections:
        mutant = source.replace(section, section.replace(token, 'REMOVED_GUARD', 1), 1)
        try: check_source(mutant)
        except ValueError: continue
        raise ValueError('Negative control accepted: ' + token)
    provenance = json.loads((HERE / 'primary-source.json').read_text(encoding='utf-8'))
    if provenance['source_baseline'] != BASE or provenance['protected_source_sha256_lf'] != PROTECTED:
        raise ValueError('Provenance mismatch')
    for flag in ('implementation_copied_from_external', 'production_execution_allowed',
            'real_dma_permitted', 'hardware_tested', 'installable_package', 'predecessor_c_suites_rerun'):
        if provenance[flag] is not False: raise ValueError('Unsafe scope claim')
    readme = (HERE / 'README.txt').read_text(encoding='utf-8')
    for token in ('Build11', 'NO SYS/INF/CAT', 'NO paquete instalable', 'NO prueba de carreras',
            'TODOS los retornos', 'BORROWED', 'ancla NO lo toma', 'NO se repiten', 'CREDITS.txt'):
        if token not in readme: raise ValueError('Missing scope: ' + token)
    result = dict(scope='NEW_RAM_OWNER_REQUEST_WORKER_CALLBACK_MODEL', baseline=BASE,
        new_source_sha256=hashes, protected_source_sha256_lf=PROTECTED,
        source_guard_negative_controls=len(sections), operation_inventory=calls,
        production_execution_allowed=False, hardware_tested=False, installable_package=False,
        real_irp_mdl_pdo_ownership=False, pnp_rundown_validated=False, predecessor_c_suites_rerun=False)
    (out / 'AUDIT.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('PASS six-source owner anchor audit:', len(sections), 'guard controls; NO OS lifetime proof')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument('--output', required=True)
    audit(parser.parse_args().output)
