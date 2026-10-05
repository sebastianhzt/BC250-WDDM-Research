#!/usr/bin/env python3
"""Parent/queue DESIGN model + frozen regressions. NO SYS/INSTALL."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
BASELINE='cd1c1083f60cf46d99a637750e602fee27581860'
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
            ':(exclude)research/windows/parent-queue-contract-20261004'],cwd=ROOT)
        if delta.strip():raise RuntimeError('Tracked predecessor changed outside candidate')
        bases=['diagnostic-package-20261004','diagnostic-package-build22-20261004',
            'pci-pnp-contract-20261004','dma-gate-20261004','dma-windows-20261004',
            'pci-pnp-admission-20261004','pci-pnp-publisher-20261004','pci-pnp-capture-20261004',
            'pnp-source-20261004','pnp-completion-20261004','dispatch-intake-20261004',
            'intake-composition-20261004','composite-permit-20261004','permit-source-binding-20261004',
            'permit-completion-20261004','context-lifetime-20261004','lifetime-completion-20261004']
        return {p.relative_to(ROOT).as_posix():sha(p) for b in bases
            for p in (ROOT/'research/windows'/b).rglob('*') if p.is_file()}
    before=preserve()
    wdk=Path(os.environ.get('ProgramFiles(x86)','C:/Program Files (x86)'))/'Windows Kits/10/Include/10.0.26100.0'
    dirs=[wdk/p for p in ('km','km/crt','shared')]
    if not all(p.is_dir() for p in dirs):raise RuntimeError('WDK missing')
    (ROOT/'output').mkdir(exist_ok=True)
    out=Path(tempfile.mkdtemp(prefix='parent-queue-contract-',dir=ROOT/'output'))
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
    for name,flags in [('normal',[]),('optimized',['-O'])]:
        report=out/('model-'+name+'.json')
        stdout=step('design-model-'+name,[sys.executable,'-B',*flags,str(HERE/'test-model.py'),str(report)])
        if b'PASS: parent/queue design' not in stdout:raise RuntimeError('Missing model acceptance')
        artifacts.append(report)
    if json.loads(artifacts[0].read_text())!=json.loads(artifacts[1].read_text()):
        raise RuntimeError('Model differs with Python optimization')
    tests=[('life-completion',ROOT/'research/windows/lifetime-completion-20261004/test-composition.c'),
        ('lifetime',ROOT/'research/windows/context-lifetime-20261004/test-lifetime.c'),
        ('lifetime-blocked',ROOT/'research/windows/context-lifetime-20261004/test-blocked.c'),
        ('permit-completion',ROOT/'research/windows/permit-completion-20261004/test-composition.c'),
        ('binding',ROOT/'research/windows/permit-source-binding-20261004/test-binding.c'),
        ('permit',ROOT/'research/windows/composite-permit-20261004/test-permit.c'),
        ('permit-blocked',ROOT/'research/windows/composite-permit-20261004/test-blocked.c'),
        ('composition',ROOT/'research/windows/intake-composition-20261004/test-composition.c'),
        ('intake',ROOT/'research/windows/dispatch-intake-20261004/test-intake.c'),
        ('intake-blocked',ROOT/'research/windows/dispatch-intake-20261004/test-blocked.c'),
        ('completion',ROOT/'research/windows/pnp-completion-20261004/test-completion.c'),
        ('completion-blocked',ROOT/'research/windows/pnp-completion-20261004/test-blocked.c'),
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
    units=[('lifetime',ROOT/'research/windows/context-lifetime-20261004/bc250_context_lifetime.c'),
        ('permit',ROOT/'research/windows/composite-permit-20261004/bc250_composite_permit.c'),
        ('intake',ROOT/'research/windows/dispatch-intake-20261004/bc250_dispatch_intake.c'),
        ('completion',ROOT/'research/windows/pnp-completion-20261004/bc250_pnp_completion.c'),
        ('source',ROOT/'research/windows/pnp-source-20261004/bc250_pnp_source.c'),
        ('capture',ROOT/'research/windows/pci-pnp-capture-20261004/bc250_pnp_capture.c'),
        ('publisher',ROOT/'research/windows/pci-pnp-publisher-20261004/bc250_pci_publisher.c'),
        ('admission',ROOT/'research/windows/pci-pnp-admission-20261004/bc250_pci_admission.c'),
        ('pci',ROOT/'research/windows/pci-pnp-contract-20261004/bc250_pci_contract.c'),
        ('gate',ROOT/'research/windows/dma-gate-20261004/bc250_dma_gate.c')]
    macros=['BC250_LIFE_MOCK','BC250_LIFE_TYPES_MOCK','BC250_PERMIT_MOCK','BC250_PERMIT_TYPES_MOCK','BC250_INTAKE_MOCK','BC250_INTAKE_TYPES_MOCK',
        'BC250_PNP_COMPLETION_MOCK','BC250_PNP_COMPLETION_TYPES_MOCK',
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
            if name=='composition' and b'PASS: composition ' not in stdout:
                raise RuntimeError('Missing final composition result')
            if name=='binding' and b'PASS: permit Source binding ' not in stdout:
                raise RuntimeError('Missing final binding result')
            if name=='permit-completion' and b'PASS: permit completion composition ' not in stdout:
                raise RuntimeError('Missing final permit completion result')
            if name=='lifetime' and b'PASS: context lifetime ' not in stdout:
                raise RuntimeError('Missing final context lifetime result')
            if name=='life-completion' and b'PASS: lifetime completion composition ' not in stdout:
                raise RuntimeError('Missing final lifetime completion composition result')
            artifacts.append(exe)
        for name,path in units:
            obj=out/(name+'-'+profile+'.obj')
            step('wdk-'+name+'-'+profile,[cl,'/nologo','/c','/kernel','/W4','/WX','/GS','/'+profile,
                '/DAMD64','/D_AMD64_',*['/U'+m for m in macros],
                *['/I'+str(p) for p in dirs],'/Fo'+str(obj),str(path)])
            symbols=step('symbols-'+name+'-'+profile,[dump,'/symbols',str(obj)])
            forbidden=rb'UNDEF[^\r\n]*(?:IoGetDma|MmMap|READ_REGISTER|WRITE_REGISTER|READ_PORT|WRITE_PORT)'
            if re.search(forbidden,symbols) or (not (name=='completion' and profile=='Od') and re.search(rb'UNDEF[^\r\n]*Iof?CallDriver',symbols)):
                raise RuntimeError('Unexpected hardware import')
            # Completion /Od retains WDK IofCallDriver prototype import. This is
            # explicitly allowed ONLY in its native-FALSE unit, never executed.
            artifacts.append(obj)
    if len(stages)!=134 or len(artifacts)!=68:raise RuntimeError('Incomplete campaign')
    if before!=preserve() or sources!={p.relative_to(HERE).as_posix():sha(p) for p in HERE.rglob('*') if p.is_file()}:
        raise RuntimeError('Inputs changed during acceptance')
    result=dict(scope='PARENT_MODULE_PRIVATE_QUEUE_DESIGN_ENVIRONMENT_ASSUMPTIONS_NOT_OS_IMPLEMENTATION',stages=stages,
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
        isolated_deferred_completion_prototype=False,actual_os_irp_forwarded=False,
        actual_work_item_queued=False,actual_remove_lock_executed=False,
        immediate_worker_simulated=True,native_completion_forward_import_permitted=True,
        dispatch_intake_metadata_model=True,actual_initial_dispatch_deferral=False,
        real_irp_queue_implemented=False,real_irp_ownership_transferred=False,
        frozen_source_modified=False,guard_composed_with_real_backend=False,
        metadata_idle_is_hardware_authority=False,stack_specific_dependency_policy=False,
        ram_composition_protocol_tested=True,new_native_component=False,
        historical_sampling_is_reservation=False,post_validation_window_tested=True,
        all_returns_witness_is_ram_fixture_only=True,actual_composite_held_permit=False,
        participant_metadata_pin_step_model=True,all_driver_writers_participate=False,
        actual_source_bound_to_permit=False,asynchronous_backend_lifetime_proved=False,
        permit_authorizes_hardware=False,long_lived_spinlock=False,
        test_world_source_bound_to_permit=True,participating_fixture_tuple_stable_in_step=True,
        all_source_tuple_writer_api_routes_exercised_in_fixture=True,
        initial_dispatch_deferral_still_absent=True,post_release_output_is_historical=True,
        physical_surprise_removal_independent_of_metadata_pin=True,
        permit_completion_composed_in_ram=True,retire_requires_external_fixture_all_returns=True,
        completion_done_or_zero_tags_is_lifetime_authority=False,
        actual_callback_dispatch_worker_lifetimes_implemented=False,
        actual_irp_ownership_queue_cancel_policy_implemented=False,
        stable_parent_child_lookup_model=True,child_never_dereferenced_by_lifetime_api=True,
        child_body_wrapper_retention_tested=True,late_helper_return_parent_only_tested=True,
        native_parent_module_lifetime_implemented=False,child_envelopes_separate_residency_required=True,
        old_completion_fixture_witness_replaced=False,caller_premature_drop_negative_fixture=True,
        new_ram_child_composition_uses_tokens=True,new_composition_all_returns_boolean=False,
        parent_envelopes_external_anchor=True,unknown_future_holds_retained=True,
        native_lifetime_completion_wrapper=False,child_poison_inside_last_hold_release_tested=True,
        abstract_parent_queue_model=True,kernel_returns_environment_assumptions=True,
        private_queue_model_only=True,real_csq_implemented=False,pnp_power_queue_authorized=False,
        actual_module_anchor_implemented=False,unknown_remove_recovery_implemented=False,
        model_result=json.loads(artifacts[0].read_text()))
    (out/'RESULT.json').write_text(json.dumps(result,indent=2)+'\n')
    print('PASS: 134 stages, 2 abstract model reports +46 RAM EXE +20 WDK OBJ; no driver link/install. '+str(out))
if __name__=='__main__':main()
