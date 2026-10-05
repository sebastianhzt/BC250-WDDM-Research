"""SPDX-License-Identifier: Apache-2.0 -- exhaustive finite DESIGN graph test."""
import json
from collections import deque
from dataclasses import asdict, replace
from pathlib import Path
import sys
from model import State, step, check, EVENTS, TERMINAL, LOWER, UNKNOWN

def require(value, message):
    if not value: raise AssertionError(message)

def trace(events):
    s = State(); check(s)
    for event in events:
        t = step(s,event); require(t is not None, 'Invalid trace '+event)
        check(t); s = t
    return s

def main():
    policy=json.loads(Path(__file__).with_name('contracts.json').read_text())
    require(not policy['production_execution_allowed'] and not policy['native_adapter_implemented'], 'Scope')
    require(policy['queue_scope']==['private_cancelable_request_design_only'], 'No generic PnP queue')
    require(all(not r['queue_authorized'] for r in policy['current_driver_routes']), 'Current routes closed')
    q=deque([State()]); seen={State()}; edges=rejects=0
    while q:
        s=q.popleft(); check(s)
        for event in EVENTS:
            t=step(s,event)
            if t is None:
                rejects+=1; continue
            edges+=1;check(t)
            if t not in seen:seen.add(t);q.append(t)
    require(len(seen)>100 and edges>100, 'Incomplete state exploration')
    terminals=sum(not s.module for s in seen)
    quarantines=sum(s.phase==UNKNOWN for s in seen)
    require(terminals and quarantines, 'Missing safe terminal/quarantine states')
    # Inline pre-cancel: dispatch never reuses IRP after insert, scheduled worker
    # still runs empty; parent/module kernel obligations distinct from child.
    prefix=('prepare','cancel_bit','insert','csq_cancel','schedule','worker_enter',
            'worker_body_end','dispatch_body_end','close','child_retire',
            'remove_forward','remove_wait_done','detach_lower','delete_request')
    s=trace(prefix)
    require(s.delete_pending and s.parent and s.module and not s.child, 'Kernel work ref must survive body')
    s=trace(prefix+('dispatch_os_return','worker_os_return'))
    require(not s.parent and not s.module, 'Known final kernel return')
    # Worker wins dequeue; CSQ loser cannot complete; after registration forward
    # is mandatory even with cancel/close; cancel bit is NOT finality/ownership.
    prepared=trace(('prepare','insert','schedule','worker_enter','dequeue','register','cancel_bit','close'))
    require(step(prepared,'csq_cancel') is None, 'CSQ cannot own dequeued request')
    require(step(prepared,'owned_cancel_complete') is None, 'Registered must forward')
    s=step(prepared,'forward');require(s is not None and s.phase==LOWER,'Mandatory forward');check(s)
    for event in ('csq_cancel','owned_cancel_complete','register_fail','cancel_return_means_done','local_complete_lower'):
        require(step(s,event) is None,'Forwarded cancellation cannot grant '+event)
    cb=trace(('prepare','insert','schedule','worker_enter','dequeue','register','forward',
              'worker_body_end','worker_os_return','dispatch_body_end','dispatch_os_return',
              'lower_callback','known_completion','callback_body_end','close','child_retire',
              'remove_forward','remove_wait_done','detach_lower','delete_request'))
    require(not cb.parent and cb.module and cb.callback_os, 'Callback module return distinct from storage')
    cb=step(cb,'callback_os_return');require(cb is not None and not cb.module,'Callback kernel return');check(cb)
    u=trace(('prepare','insert','schedule','worker_enter','dequeue','register','forward',
             'lower_callback','unknown_completion','callback_body_end','worker_body_end',
             'worker_os_return','dispatch_body_end','dispatch_os_return','callback_os_return','close','remove_forward'))
    require(u.child and u.parent and u.module,'Quarantine stays alive')
    for event in ('child_retire','remove_wait_done','detach_lower','delete_request','force_free','reset_remove_lock'):
        require(step(u,event) is None,'No forced unknown drain '+event)
    # Mutation tests are counterexamples only; no unsafe production actions.
    mutations=[replace(State(),child=False),replace(State(),parent=False),replace(State(),module=False),
        replace(State(),completed=2),replace(State(),wait_done=True),replace(State(),detached=True),
        replace(u,callback_hold=False),replace(u,callback_ref=False),replace(u,request_tag=False),
        replace(trace(prefix),parent=False),replace(prepared,callback_os=False)]
    for bad in mutations:
        try:check(bad)
        except AssertionError:pass
        else:raise AssertionError('Negative mutation escaped invariant')
    # Preserve exact targeted paths and every intermediate abstract state.
    # These are model evidence, NOT logged kernel/IRP/ref activity.
    trace_specs={
        'inline_precancel_and_worker_kernel_return':prefix+('dispatch_os_return','worker_os_return'),
        'dequeue_wins_then_cancel_mandatory_forward':('prepare','insert','schedule','worker_enter','dequeue','register','cancel_bit','close','forward'),
        'callback_module_outlives_parent_storage':('prepare','insert','schedule','worker_enter','dequeue','register','forward',
            'worker_body_end','worker_os_return','dispatch_body_end','dispatch_os_return',
            'lower_callback','known_completion','callback_body_end','close','child_retire',
            'remove_forward','remove_wait_done','detach_lower','delete_request','callback_os_return'),
        'unknown_quarantine':('prepare','insert','schedule','worker_enter','dequeue','register','forward',
            'lower_callback','unknown_completion','callback_body_end','worker_body_end',
            'worker_os_return','dispatch_body_end','dispatch_os_return','callback_os_return','close','remove_forward'),
    }
    trace_evidence={}
    for name,events in trace_specs.items():
        current=State();states=[asdict(current)]
        for event in events:
            current=step(current,event);require(current is not None,'Evidence path '+event)
            check(current);states.append(asdict(current))
        trace_evidence[name]=dict(events=list(events),states=states)
    result=dict(scope='BOUNDED_PRIVATE_QUEUE_AND_KERNEL_ANCHOR_DESIGN_NOT_OS_IMPLEMENTATION',
        states=len(seen),edges=edges,rejected_transitions=rejects,terminal_states=terminals,
        quarantine_states=quarantines,negative_mutations=len(mutations),targeted_traces=len(trace_evidence),trace_evidence=trace_evidence,
        production_execution_allowed=False,native_adapter_implemented=False,
        actual_os_lifetime_validated=False,real_csq_implemented=False,smp_validated=False,
        pnp_power_queue_authorized=False,hardware_tested=False)
    if len(sys.argv)!=2:raise RuntimeError('Output path argument required')
    Path(sys.argv[1]).write_text(json.dumps(result,indent=2)+'\n')
    print('PASS: parent/queue design',len(seen),'states',edges,'edges;',len(mutations),'negative mutations; no native/OS/SMP/hardware.')

if __name__=='__main__':main()
