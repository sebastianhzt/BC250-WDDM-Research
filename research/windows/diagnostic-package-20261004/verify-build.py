#!/usr/bin/env python3
"""Build an isolated UNSIGNED WDM diagnostic candidate. Never load/install.
Run only after independent Code Reviewer PREBUILD approval.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

BASE = Path(__file__).resolve().parent
ROOT = BASE.parents[2]
SOURCE = BASE / 'source'

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    if any(os.environ.get(name, '').strip() for name in ('CL','_CL_','LINK')):
        raise RuntimeError('CL/_CL_/LINK injection is forbidden; no output/process created')
    snapshot = {p.relative_to(BASE).as_posix():sha(p) for p in BASE.rglob('*') if p.is_file()}
    provenance = json.loads((BASE / 'provenance.json').read_text())
    expected = provenance['candidate_source_sha256']
    actual = {p.relative_to(SOURCE).as_posix(): sha(p) for p in SOURCE.rglob('*') if p.is_file()}
    if actual != expected:
        raise RuntimeError('Source snapshot differs from reviewed provenance')
    def check_predecessor():
        for relative, digest in provenance['protected_public_source_sha256_lf'].items():
            if hashlib.sha256((ROOT / relative).read_bytes().replace(b'\r\n', b'\n')).hexdigest() != digest:
                raise RuntimeError('Protected predecessor changed: ' + relative)
        git = ['git','-c','safe.directory='+str(ROOT)]
        delta = subprocess.check_output(git+['diff','--name-only',provenance['public_baseline'],'--','.',
            ':(exclude)research/windows/diagnostic-package-20261004'],cwd=ROOT)
        if delta.strip(): raise RuntimeError('Tracked predecessor delta outside isolated candidate')
    check_predecessor()
    wdk = Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)')) / 'Windows Kits/10'
    version = '10.0.26100.0'
    vcvars = Path(os.environ.get('ProgramFiles', 'C:/Program Files')) / 'Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat'
    if not vcvars.is_file() or not (wdk / 'Include' / version / 'km/ntddk.h').is_file():
        raise RuntimeError('Required VS2022/WDK tools missing')
    (ROOT / 'output').mkdir(exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix='diagnostic-build21-', dir=ROOT / 'output'))
    stages = []
    def stage(name, args, env=None):
        (output / (name+'.command.json')).write_text(json.dumps(args, indent=2))
        result = subprocess.run(args, cwd=output, env=env, capture_output=True)
        (output / (name+'.stdout')).write_bytes(result.stdout)
        (output / (name+'.stderr')).write_bytes(result.stderr)
        stages.append({'name': name, 'command': args, 'returncode': result.returncode})
        (output / 'stages.json').write_text(json.dumps(stages, indent=2))
        print(name, result.returncode, flush=True)
        if result.returncode:
            raise RuntimeError('Stage failed: '+name+'; output: '+str(output))
        if re.search(rb'\b(?:warning|error) [A-Z]+\d+\b', result.stdout+result.stderr, re.I):
            raise RuntimeError('Compiler/linker diagnostic: '+name)
        return result.stdout
    env = os.environ.copy()
    cl = shutil.which('cl.exe')
    link = shutil.which('link.exe')
    dumpbin = shutil.which('dumpbin.exe')
    if not all((cl, link, dumpbin)) or env.get('VSCMD_ARG_TGT_ARCH') != 'x64':
        raise RuntimeError('Expected x64 MSVC tools')
    includes = ['/I'+str(wdk/'Include'/version/part) for part in ('km','km/crt','shared')]
    includes += ['/I'+str(SOURCE/'inc')]
    objects = []
    for unit in ('bc250_diagnostic_driver', 'amdbc250_dream_pnp'):
        obj = output / (unit+'.obj')
        stage('compile-'+unit, [cl,'/nologo','/c','/kernel','/W4','/WX','/GS','/Od','/DAMD64','/D_AMD64_',
              '/Fo'+str(obj), *includes, str(SOURCE/'src/kmd'/(unit+'.c'))], env)
        if not obj.is_file() or not obj.stat().st_size:
            raise RuntimeError('Required OBJ absent')
        objects.append(str(obj))
    sys = output / 'atikmdag-UNSIGNED.sys'
    linkmap = output/'driver.map'
    stage('link-driver', [link,'/nologo','/DRIVER','/SUBSYSTEM:NATIVE','/ENTRY:GsDriverEntry','/WX',
          '/MAP:'+str(linkmap), '/OUT:'+str(sys), *objects, 'ntoskrnl.lib','hal.lib','wdmsec.lib','BufferOverflowFastFailK.lib',
          '/LIBPATH:'+str(wdk/'Lib'/version/'km/x64')], env)
    imports = stage('driver-imports', [dumpbin,'/imports',str(sys)], env)
    stage('driver-headers', [dumpbin,'/headers',str(sys)], env)
    stage('driver-loadconfig', [dumpbin,'/loadconfig',str(sys)], env)
    forbidden = rb'\b(?:MmMapIoSpace(?:Ex)?|IoGetDmaAdapter|MmAllocateContiguousMemory(?:SpecifyCache)?|MmAllocatePagesForMdl(?:Ex)?|MmMapLockedPagesSpecifyCache|DxgkInitialize)\b'
    if re.search(forbidden, imports):
        raise RuntimeError('Forbidden GPU/DMA/display import')
    artifacts = {sys.name: sha(sys), linkmap.name:sha(linkmap)}
    # Extract one production function verbatim, not a rewritten dispatcher.
    driver_text = (SOURCE/'src/kmd/bc250_diagnostic_driver.c').read_text()
    start = driver_text.index('static NTSTATUS Bc250Control(')
    brace = driver_text.index('{', start)
    depth = 1
    end = brace+1
    while depth:
        if driver_text[end] == '{': depth += 1
        elif driver_text[end] == '}': depth -= 1
        end += 1
    extracted = output/'dispatch-body.inc'
    extracted.write_text(driver_text[start:end]+'\n')
    artifacts[extracted.name] = sha(extracted)
    for profile in ('O2','Od'):
        exe = output / ('test-diagnostic-policy-'+profile+'.exe')
        stage('compile-policy-'+profile, [cl,'/nologo','/W4','/WX','/'+profile,'/Fe'+str(exe),
              str(SOURCE/'test-tools/test-diagnostic-policy.c')], env)
        stdout = stage('run-policy-'+profile, [str(exe)], env)
        if b'PASS: 524301 routing/ABI checks; CPU-only, no device opened.' not in stdout:
            raise RuntimeError('Unexpected RAM test result')
        artifacts[exe.name] = sha(exe)
        fixture = output/('test-dispatch-'+profile+'.exe')
        stage('compile-dispatch-'+profile,[cl,'/nologo','/W4','/WX','/'+profile,'/I'+str(output),
              '/Fe'+str(fixture),str(SOURCE/'test-tools/test-dispatch-fixture.c')],env)
        stdout = stage('run-dispatch-'+profile,[str(fixture)],env)
        if b'PASS: 921987 extracted-dispatch checks; all Windows services are RAM fakes.' not in stdout:
            raise RuntimeError('Unexpected dispatch fixture result')
        artifacts[fixture.name] = sha(fixture)
    for name in ('pnp-binding-preflight','w2p-preflight','resource-preflight',
                 'pci-config-preflight','diagnostic-access-preflight'):
        exe = output / (name+'.exe')
        args = [cl,'/nologo','/W4','/WX','/O2','/Fe'+str(exe),str(SOURCE/'test-tools'/(name+'.c'))]
        if name == 'diagnostic-access-preflight': args.append('advapi32.lib')
        stage('compile-'+name, args, env)
        if not exe.is_file() or not exe.stat().st_size: raise RuntimeError('Required tool missing')
        artifacts[exe.name] = sha(exe)
    if actual != {p.relative_to(SOURCE).as_posix(): sha(p) for p in SOURCE.rglob('*') if p.is_file()}:
        raise RuntimeError('Inputs changed during build')
    if snapshot != {p.relative_to(BASE).as_posix():sha(p) for p in BASE.rglob('*') if p.is_file()}:
        raise RuntimeError('Auxiliary sources changed during build')
    check_predecessor()
    report = {'build_id':21,'stages':stages,'source_sha256':actual,'artifact_sha256':artifacts,
              'snapshot_sha256':snapshot,
              'compiler_sha256':{str(Path(p).name):sha(Path(p)) for p in (cl,link,dumpbin)},
              'diagnostic_tools_executed':False,'driver_loaded':False,'driver_installed':False,
              'driver_signed':False,'installable_package':False,'hardware_tested':False,
              'wddm_miniport':False,'windows_vram_ownership_validated':False,
              'gpu_dma_enabled':False,'screen_output_validated':False,'pnp_runtime_validated':False}
    (output/'RESULT.json').write_text(json.dumps(report, indent=2))
    print('UNSIGNED DIAGNOSTIC CANDIDATE:', output)

if __name__ == '__main__':
    main()
