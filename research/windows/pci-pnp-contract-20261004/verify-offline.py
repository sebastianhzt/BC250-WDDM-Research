#!/usr/bin/env python3
"""Isolated RAM contracts + WDK /c only. Requires prior Code Reviewer PREBUILD."""
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
BASELINE='779541245ea376911404a9084e75d306a0aad613'
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    if any(os.environ.get(k,'').strip() for k in ('CL','_CL_','LINK')):
        raise RuntimeError('Hidden compiler/link flags rejected')
    if os.environ.get('VSCMD_ARG_TGT_ARCH','').lower()!='x64':
        raise RuntimeError('Use VS2022 x64 Native Tools')
    cl=shutil.which('cl.exe'); dump=shutil.which('dumpbin.exe')
    if not cl or not dump: raise RuntimeError('MSVC required')
    files={p.relative_to(HERE).as_posix():sha(p) for p in HERE.rglob('*') if p.is_file()}
    def preserve():
        git=['git','-c','safe.directory='+str(ROOT)]
        delta=subprocess.check_output(git+['diff','--name-only',BASELINE,'--','.',
            ':(exclude)research/windows/pci-pnp-contract-20261004'],cwd=ROOT)
        if delta.strip(): raise RuntimeError('Tracked predecessor changed outside candidate')
        return {p.relative_to(ROOT).as_posix():sha(p)
            for base in ('diagnostic-package-20261004','diagnostic-package-build22-20261004')
            for p in (ROOT/'research/windows'/base).rglob('*') if p.is_file()}
    before=preserve()
    wdk=Path(os.environ.get('ProgramFiles(x86)','C:/Program Files (x86)'))/'Windows Kits/10/Include/10.0.26100.0'
    include=[wdk/x for x in ('km','km/crt','shared')]
    if not all(p.is_dir() for p in include):raise RuntimeError('WDK missing')
    (ROOT/'output').mkdir(exist_ok=True)
    out=Path(tempfile.mkdtemp(prefix='pci-pnp-contract-',dir=ROOT/'output'))
    stages=[]; artifacts=[]
    def step(name,args):
        (out/(name+'.command.json')).write_text(json.dumps(args,indent=2))
        r=subprocess.run(args,cwd=out,capture_output=True)
        (out/(name+'.stdout')).write_bytes(r.stdout)
        (out/(name+'.stderr')).write_bytes(r.stderr)
        stages.append(dict(name=name,command=args,returncode=r.returncode))
        (out/'stages.json').write_text(json.dumps(stages,indent=2))
        print(name,r.returncode,flush=True)
        if r.returncode or re.search(rb'\b(?:warning|error) [A-Z]+[0-9]+\b',r.stdout+r.stderr,re.I):
            raise RuntimeError('Failed '+name+'; '+str(out))
        return r.stdout
    for profile in ('O2','Od'):
        for name in ('pci','blocked'):
            exe=out/(name+'-'+profile+'.exe')
            step('compile-'+name+'-'+profile,[cl,'/nologo','/W4','/WX','/GS','/'+profile,
                '/Fe'+str(exe),str(HERE/('test-'+name+'.c'))])
            stdout=step('run-'+name+'-'+profile,[str(exe)])
            if name=='pci' and not re.search(rb'^PASS: [1-9][0-9]* checks;',stdout):
                raise RuntimeError('Missing semantic test pass')
            if name=='blocked' and b'PASS: execution gate FALSE' not in stdout:
                raise RuntimeError('Missing closed gate pass')
            artifacts.append(exe)
        obj=out/('pci-production-'+profile+'.obj')
        step('wdk-'+profile,[cl,'/nologo','/c','/kernel','/W4','/WX','/GS','/'+profile,
            '/DAMD64','/D_AMD64_','/UBC250_PCI_CONTRACT_MOCK','/UBC250_PCI_TYPES_MOCK',
            *['/I'+str(p) for p in include],'/Fo'+str(obj),str(HERE/'bc250_pci_contract.c')])
        symbols=step('symbols-'+profile,[dump,'/symbols',str(obj)])
        if re.search(rb'UNDEF[^\r\n]*(?:Io|Mm|Hal|GetBusData|SetBusData|GetDmaAdapter|READ_|WRITE_)',symbols):
            raise RuntimeError('Unexpected platform/GPU import')
        artifacts.append(obj)
    if len(stages)!=12 or len(artifacts)!=6: raise RuntimeError('Incomplete campaign')
    if before!=preserve() or files!={p.relative_to(HERE).as_posix():sha(p) for p in HERE.rglob('*') if p.is_file()}:
        raise RuntimeError('Inputs changed; re-review and repeat')
    result=dict(scope='PCI_PNP_CONTRACT_RAM_ONLY',stages=stages,source_sha256=files,
        protected_diagnostic_sha256=before,artifact_sha256={p.name:sha(p) for p in artifacts},
        production_execution_allowed=False,driver_linked=False,installable_package=False,
        driver_installed=False,hardware_tested=False,physical_pci_read=False,
        real_pnp_rundown_validated=False,real_epoch_or_irql_validated=False,
        vram_ownership=False,dma_authorized=False,bar_size_measured=False)
    (out/'RESULT.json').write_text(json.dumps(result,indent=2)+'\n')
    print('PASS: 12 offline stages, 4 RAM EXE + 2 standalone WDK OBJ; NO SYS/INSTALL. '+str(out))
if __name__=='__main__': main()
