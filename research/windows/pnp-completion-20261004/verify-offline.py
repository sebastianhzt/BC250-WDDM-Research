#!/usr/bin/env python3
"""Deferred completion prototype; fake IRP/remove-lock/workqueue + WDK /c. NO SYS/INSTALL."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
BASELINE='678ddff0ed93e0e4cd29e18e364d029c6ccf3b60'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    if any(os.environ.get(k,'').strip() for k in ('CL','_CL_','LINK')):
        raise RuntimeError('Hidden compiler/link flags rejected')
    if os.environ.get('VSCMD_ARG_TGT_ARCH','').lower()!='x64':
        raise RuntimeError('VS2022 x64 Native Tools required')
    cl=shutil.which('cl.exe');dump=shutil.which('dumpbin.exe')
    if not cl or not dump:raise RuntimeError('MSVC missing')
    sources={p.relative_to(HERE).as_posix():sha(p) for p in HERE.rglob('*') if p.is_file()}
    def preserve():
        git=['git','-c','safe.directory='+str(ROOT)]
        delta=subprocess.check_output(git+['diff','--name-only',BASELINE,'--','.',
            ':(exclude)research/windows/pnp-completion-20261004'],cwd=ROOT)
        if delta.strip():raise RuntimeError('Tracked predecessor changed outside candidate')
        bases=['diagnostic-package-20261004','diagnostic-package-build22-20261004',
            'pci-pnp-contract-20261004','dma-gate-20261004','dma-windows-20261004',
            'pci-pnp-admission-20261004','pci-pnp-publisher-20261004','pci-pnp-capture-20261004',
            'pnp-source-20261004']
        return {p.relative_to(ROOT).as_posix():sha(p) for b in bases
            for p in (ROOT/'research/windows'/b).rglob('*') if p.is_file()}
    before=preserve()
    wdk=Path(os.environ.get('ProgramFiles(x86)','C:/Program Files (x86)'))/'Windows Kits/10/Include/10.0.26100.0'
    dirs=[wdk/p for p in ('km','km/crt','shared')]
    if not all(p.is_dir() for p in dirs):raise RuntimeError('WDK missing')
    (ROOT/'output').mkdir(exist_ok=True)
    out=Path(tempfile.mkdtemp(prefix='pnp-completion-',dir=ROOT/'output'))
    stages=[];artifacts=[]
    def step(name,args):
        (out/(name+'.command.json')).write_text(json.dumps(args,indent=2))
        result=subprocess.run(args,cwd=out,capture_output=True)
        (out/(name+'.stdout')).write_bytes(result.stdout)
        (out/(name+'.stderr')).write_bytes(result.stderr)
        stages.append(dict(name=name,command=args,returncode=result.returncode))
        (out/'stages.json').write_text(json.dumps(stages,indent=2))
        print(name,result.returncode,flush=True)
        if result.returncode or re.search(rb'\b(?:warning|error) [A-Z]+[0-9]+\b',result.stdout+result.stderr,re.I):
            raise RuntimeError('Failed '+name+'; '+str(out))
        return result.stdout
    tests=[('completion',HERE/'test-completion.c'),('completion-blocked',HERE/'test-blocked.c'),
        ('source',ROOT/'research/windows/pnp-source-20261004/test-source.c'),
        ('source-blocked',ROOT/'research/windows/pnp-source-20261004/test-blocked.c'),
        ('capture',ROOT/'research/windows/pci-pnp-capture-20261004/test-capture.c'),
        ('capture-blocked',ROOT/'research/windows/pci-pnp-capture-20261004/test-blocked.c'),
        ('abi-layout',ROOT/'research/windows/pci-pnp-capture-20261004/test-abi-layout.c'),
        ('publisher',ROOT/'research/windows/pci-pnp-publisher-20261004/test-publisher.c'),
        ('publisher-blocked',ROOT/'research/windows/pci-pnp-publisher-20261004/test-blocked.c'),
        ('admission',ROOT/'research/windows/pci-pnp-admission-20261004/test-admission.c'),
        ('pci',ROOT/'research/windows/pci-pnp-contract-20261004/test-pci.c'),
        ('pci-blocked',ROOT/'research/windows/pci-pnp-contract-20261004/test-blocked.c'),
        ('gate',ROOT/'research/windows/dma-gate-20261004/test-gate.c')]
    units=[('completion',HERE/'bc250_pnp_completion.c'),
        ('source',ROOT/'research/windows/pnp-source-20261004/bc250_pnp_source.c'),
        ('capture',ROOT/'research/windows/pci-pnp-capture-20261004/bc250_pnp_capture.c'),
        ('publisher',ROOT/'research/windows/pci-pnp-publisher-20261004/bc250_pci_publisher.c'),
        ('admission',ROOT/'research/windows/pci-pnp-admission-20261004/bc250_pci_admission.c'),
        ('pci',ROOT/'research/windows/pci-pnp-contract-20261004/bc250_pci_contract.c'),
        ('gate',ROOT/'research/windows/dma-gate-20261004/bc250_dma_gate.c')]
    macros=['BC250_PNP_COMPLETION_MOCK','BC250_PNP_COMPLETION_TYPES_MOCK',
        'BC250_PNP_SOURCE_MOCK','BC250_PNP_SOURCE_TYPES_MOCK',
        'BC250_PNP_CAPTURE_MOCK','BC250_PNP_CAPTURE_TYPES_MOCK',
        'BC250_PCI_PUBLISHER_MOCK','BC250_PCI_PUBLISHER_TYPES_MOCK','BC250_PCI_ADMISSION_MOCK',
        'BC250_PCI_CONTRACT_MOCK','BC250_PCI_TYPES_MOCK','BC250_DMA_GATE_MOCK']
    for profile in ('O2','Od'):
        for name,path in tests:
            exe=out/(name+'-'+profile+'.exe')
            step('compile-'+name+'-'+profile,[cl,'/nologo','/W4','/WX','/GS','/'+profile,'/Fe'+str(exe),str(path)])
            stdout=step('run-'+name+'-'+profile,[str(exe)])
            if b'PASS:' not in stdout:raise RuntimeError('Missing semantic test success '+name)
            artifacts.append(exe)
        for name,path in units:
            obj=out/(name+'-'+profile+'.obj')
            step('wdk-'+name+'-'+profile,[cl,'/nologo','/c','/kernel','/W4','/WX','/GS','/'+profile,
                '/DAMD64','/D_AMD64_',*['/U'+m for m in macros],
                *['/I'+str(p) for p in dirs],'/Fo'+str(obj),str(path)])
            symbols=step('symbols-'+name+'-'+profile,[dump,'/symbols',str(obj)])
            forbidden=rb'UNDEF[^\r\n]*(?:IoGetDma|MmMap|READ_REGISTER|WRITE_REGISTER|READ_PORT|WRITE_PORT)'
            if re.search(forbidden,symbols) or (name!='completion' and re.search(rb'UNDEF[^\r\n]*Iof?CallDriver',symbols)):
                raise RuntimeError('Unexpected hardware import')
            # Completion /Od retains WDK IofCallDriver prototype import. This is
            # explicitly allowed ONLY in its native-FALSE unit, never executed.
            artifacts.append(obj)
    if len(stages)!=80 or len(artifacts)!=40:raise RuntimeError('Incomplete campaign')
    if before!=preserve() or sources!={p.relative_to(HERE).as_posix():sha(p) for p in HERE.rglob('*') if p.is_file()}:
        raise RuntimeError('Inputs changed during acceptance')
    result=dict(scope='PNP_DEFERRED_COMPLETION_FAKE_IRP_REMOVELOCK_WORKQUEUE',stages=stages,
        source_sha256=sources,protected_source_sha256=before,
        artifact_sha256={p.name:sha(p) for p in artifacts},production_execution_allowed=False,
        driver_linked=False,installable_package=False,driver_installed=False,
        hardware_tested=False,physical_pci_read=False,real_pnp_or_power_integration=False,
        real_rundown_or_concurrency_validated=False,real_sampler_validated=False,
        vram_ownership=False,dma_authorized=False,physical_availability_proved=False,
        real_publisher_or_restart_implemented=False,synthetic_publisher_model=True,
        actual_backing_storage_or_pdo_lifetime_validated=False,quarantine_recovery_implemented=False,
        actual_build22_wire_layout_verified=True,real_coherent_source_provider=False,
        coherent_metadata_model=True,synthetic_revision_is_pnp_authority=False,
        actual_pnp_dispatch_modified=False,real_power_state_observed=False,
        source_producer_candidate=True,request_completions_synthetic=True,
        actual_object_reference_lifetime_validated=False,actual_remove_lock_integration=False,
        dispatch_level_completion_deferral_implemented=False,
        isolated_deferred_completion_prototype=True,actual_os_irp_forwarded=False,
        actual_work_item_queued=False,actual_remove_lock_executed=False,
        immediate_worker_simulated=True,native_completion_forward_import_permitted=True)
    (out/'RESULT.json').write_text(json.dumps(result,indent=2)+'\n')
    print('PASS: 80 stages, 26 RAM EXE + 14 WDK OBJ; no driver link/install. '+str(out))
if __name__=='__main__':main()
