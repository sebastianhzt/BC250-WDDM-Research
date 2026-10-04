"""Six new source files only, protected predecessor unchanged. RAM bridge audit."""
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
BASE = 'bd27d05e27df0471e74675df3b6aa209ad65b87a'
PREFIX = 'research/windows/dma-bridge-20261004/'
FILES = {PREFIX + name for name in ('bc250_dma_bridge.h', 'test-bridge.c',
    'audit-bridge.py', 'verify-offline.py', 'README.txt', 'primary-source.json')}
HEADER_SHA256 = '869168be451714af50efe77b46322a3e96cdf95004805cbda8a3d8c523aa5b79'
spec = importlib.util.spec_from_file_location('domain_auditor',
    ROOT / 'research/windows/domain-backend-20261003/audit-domain-backend.py')
previous = importlib.util.module_from_spec(spec); spec.loader.exec_module(previous)


def check_source(source):
    source = source.replace('static __inline void ', 'static __inline int ').replace(
        'static __inline BC250_VM_SESSION_STATUS ', 'static __inline int ')
    allowed = {'if', 'for', 'sizeof', 'memcmp', 'memset', 'KeGetCurrentIrql',
        'Bc250DmaBridgeGuard', 'Bc250DmaBridgeAway', 'Bc250DmaBridgeCheck',
        'Bc250DmaBridgePut', 'Bc250DmaBridgeInit', 'Bc250DmaBridgeBegin',
        'Bc250DmaBridgeAcquire', 'Bc250DmaBridgePublish', 'Bc250DmaBridgeCancel',
        'Bc250DmaBridgeMap', 'Bc250DmaBridgeUnmap', 'Bc250DmaBridgeRelease',
        'Bc250DmaBridgeClose', 'Bc250DmaPageListDisjoint', 'Bc250VmSessionBufferAway',
        'Bc250VmSessionCheck', 'Bc250DmaOwnerCheck', 'Bc250DmaOwnerInit',
        'Bc250DmaOwnerBegin', 'Bc250DmaOwnerResolveNoResource', 'Bc250DmaOwnerDeliver',
        'Bc250DmaOwnerCancel', 'Bc250DmaOwnerMap', 'Bc250DmaOwnerUnmap',
        'Bc250DmaOwnerRelease', 'Bc250DmaOwnerShutdown', 'Bc250WinDmaOpen',
        'Bc250WinDmaAcquire', 'Bc250WinDmaRelease', 'Bc250WinDmaCancel', 'Bc250WinDmaClose'}
    calls = set(re.findall(r'\b([A-Za-z_]\w*)\s*\(', previous.stripped(source)))
    if calls - allowed: raise ValueError('Unexpected bridge operation: ' + str(calls - allowed))
    def body(suffix):
        return previous.function(source.replace('static __inline void ', 'static __inline int '),
            'Bc250DmaBridge' + suffix)
    sections = (
        (source, '#ifndef BC250_DMA_ADAPTER_MOCK\n#error DMA bridge requires the RAM fake platform'),
        (source, '#ifdef _KERNEL_MODE\n#error CPU DMA bridge is not kernel integrated'),
        (body('Guard'), 'if (!Bc250MockExecutionAllowed) return BC250_VM_SESSION_INVALID;'),
        (body('Guard'), 'if (KeGetCurrentIrql() != PASSIVE_LEVEL) return BC250_VM_SESSION_INVALID;'),
        (body('Guard'), 'if (bridge->Busy) return BC250_VM_SESSION_BUSY_RESULT;'),
        (body('Check'), 'Bc250VmSessionBufferAway(bridge->Owner.Session, bridge, sizeof(*bridge))'),
        (body('Check'), 'bridge->Owner.Cookie != &bridge->Transport'),
        (body('Away'), 'Bc250DmaPageListDisjoint(bridge, sizeof(*bridge), buffer, bytes)'),
        (body('Put'), '!bridge->Busy || !bridge->Owner.Busy || cookie != &bridge->Transport'),
        (body('Put'), 'bridge->Transport.State != BC250_WIN_DMA_HELD'),
        (body('Put'), 'bridge->NativeStatus = Bc250WinDmaRelease(&bridge->Transport);'),
        (body('Put'), 'if (bridge->NativeStatus != STATUS_SUCCESS) bridge->State = BC250_DMA_BRIDGE_FAULT;'),
        (body('Begin'), 'if (bridge->Begun) return BC250_VM_SESSION_IN_USE;'),
        (body('Acquire'), '!Bc250DmaBridgeAway(bridge, mdl, sizeof(*mdl))'),
        (body('Acquire'), 'bridge->Transport.State == BC250_WIN_DMA_OPEN && !bridge->Transport.List'),
        (body('Publish'), 'Bc250DmaBridgeAway(bridge, list, sizeof(*list))'),
        (body('Publish'), 'limit = (1ULL << bridge->Transport.AddressWidth) - 1ULL;'),
        (body('Publish'), 'run->Domain = BC250_AD_DMA_LOGICAL;'),
        (body('Publish'), 'run->Bytes - 1ULL > limit - run->Start'),
        (body('Publish'), 'if (!result.Consumed) bridge->State = BC250_DMA_BRIDGE_FAULT;'),
        (body('Cancel'), 'bridge->Owner.State == BC250_DMA_OWNER_CANCEL_WAIT'),
        (body('Cancel'), 'result.Consumed ? result.Status : BC250_VM_SESSION_FAULT'),
        (body('Map'), '!Bc250DmaBridgeAway(bridge, out, sizeof(*out))'),
        (body('Unmap'), '!Bc250DmaBridgeAway(bridge, mapping, sizeof(*mapping))'),
        (body('Release'), 'status = Bc250DmaOwnerRelease(&bridge->Owner, &bridge->Ticket);'),
        (body('Release'), 'if (bridge->State == BC250_DMA_BRIDGE_FAULT) status = BC250_VM_SESSION_FAULT;'),
        (body('Close'), 'status == BC250_VM_SESSION_FAULT && !bridge->Begun && !bridge->Transport.Adapter'),
        (body('Close'), 'bridge->Owner.State != BC250_DMA_OWNER_IDLE'),
        (body('Close'), 'bridge->NativeStatus = Bc250WinDmaClose(&bridge->Transport);'),
    )
    normalized = source
    for section, token in sections:
        if section.count(token) != 1 or section not in normalized: raise ValueError('Guard missing: ' + token)
    # Native cancellation is allowed only before handing a cookie to the owner.
    cancel = body('Cancel')
    if cancel.count('Bc250WinDmaCancel(') != 2 or cancel.index('BC250_DMA_OWNER_CANCEL_WAIT') > cancel.rindex('Bc250WinDmaCancel('):
        raise ValueError('Cancellation branches changed')
    return sections, sorted(calls)


def audit(output):
    out = Path(output).resolve(); out.relative_to((ROOT / 'output').resolve())
    if not out.is_dir(): raise ValueError('Scoped output directory required')
    previous.git('merge-base', '--is-ancestor', BASE, 'HEAD')
    changed = set(previous.git('diff', '--name-only', '-z', BASE).split('\0')) - {''}
    untracked = set(previous.git('ls-files', '--others', '--exclude-standard', '-z').split('\0')) - {''}
    if changed | untracked != FILES: raise ValueError('Exact six-file scope mismatch')
    hashes = {}
    for relative in sorted(FILES):
        path = ROOT / relative
        if not path.is_file() or path.is_symlink(): raise ValueError('Regular source required')
        data = path.read_bytes(); text = data.decode('utf-8')
        if b'\0' in data or re.search(r'-----BEGIN [A-Z ]*PRIVATE KEY-----|gh[pousr]_[A-Za-z0-9]{36,}|[A-Za-z]:[\\/]Users[\\/]', text):
            raise ValueError('Private/binary export')
        hashes[relative] = hashlib.sha256(data).hexdigest()
    if hashes[PREFIX + 'bc250_dma_bridge.h'] != HEADER_SHA256: raise ValueError('Reviewed bridge changed')
    source = (HERE / 'bc250_dma_bridge.h').read_text(encoding='utf-8')
    sections, calls = check_source(source)
    normalized = source.replace('static __inline void ', 'static __inline int ').replace(
        'static __inline BC250_VM_SESSION_STATUS ', 'static __inline int ')
    for section, token in sections:
        mutant = normalized.replace(section, section.replace(token, 'REMOVED_GUARD', 1), 1)
        try: check_source(mutant)
        except ValueError: continue
        raise ValueError('Negative control accepted')
    readme = (HERE / 'README.txt').read_text(encoding='utf-8')
    for token in ('SOLO RAM', 'Build11', 'NO lock', 'QUARANTINED', 'CREDITS.txt', 'NO SYS/INF/CAT', 'NO un fallo tras Begin'):
        if token not in readme: raise ValueError('Scope/attribution missing')
    provenance = json.loads((HERE / 'primary-source.json').read_text(encoding='utf-8'))
    if provenance['source_baseline'] != BASE or len(provenance['local_sources']) != 6:
        raise ValueError('Protected local sources missing')
    for relative in provenance['local_sources']:
        if not (ROOT / relative).is_file(): raise ValueError('Missing protected source')
    runner = (HERE / 'verify-offline.py').read_text(encoding='utf-8')
    for token in ("for name in ('CL', '_CL_')", "'/UBC250_DMA_ADAPTER_MOCK'", "'/c', '/kernel'", 'len(history) != 35'):
        if token not in runner: raise ValueError('Build isolation missing')
    result = dict(scope='RAM_ONLY_DMA_BRIDGE', baseline=BASE, new_source_sha256=hashes,
        source_guard_negative_controls=len(sections), operation_inventory=calls,
        predecessors_unchanged=True, bridge_kernel_integrated=False,
        real_windows_dma_tested=False, hardware_tested=False, installable_package=False)
    (out / 'AUDIT.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('PASS: six new sources, %d guard negative controls; RAM bridge only' % len(sections))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True)
    audit(parser.parse_args().output)
