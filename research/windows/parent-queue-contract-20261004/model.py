"""SPDX-License-Identifier: Apache-2.0
Bounded DESIGN model. Not a driver, CSQ implementation or Windows lifetime proof.
Kernel-return/reference events below are explicit ENVIRONMENT assumptions, not
driver counters/booleans that can certify OS returns. One private request only.
Completion postprocessing is abstract, not the frozen bridge's second worker.
"""
from dataclasses import dataclass, replace

NEW, PREPARED, QUEUED, OWNED, REGISTERED, LOWER, CALLBACK, TERMINAL, UNKNOWN = range(9)

@dataclass(frozen=True)
class State:
    phase: int = NEW
    closed: bool = False
    cancel: bool = False
    dispatch_body: bool = True
    dispatch_os: bool = True
    work: int = 0  # 0 absent,1 prepared,2 kernel queued,3 body,4 body done,5 OS returned
    work_hold: bool = False
    work_ref: bool = False  # explicit driver envelope FDO ref, separate from kernel ref
    callback_body: bool = False
    callback_os: bool = False  # modeled IoSetCompletionRoutineEx module-return guarantee
    callback_hold: bool = False
    callback_ref: bool = False
    request_tag: bool = False
    marked: bool = False
    completed: int = 0
    child: bool = True
    remove_forwarded: bool = False
    wait_done: bool = False
    detached: bool = False
    delete_pending: bool = False
    parent: bool = True
    module: bool = True

def object_refs(s):
    # Bootstrap dispatch-FDO validity is a proposed OS-entry contract assumption.
    # Work2..4 kernel FDO reference persists even if driver freed its work item.
    return int(s.dispatch_os) + int(s.work in (2,3,4)) + int(s.work_ref) + int(s.callback_ref)

def normalize(s):
    if s.delete_pending and not object_refs(s):
        s = replace(s, parent=False)
    if not s.parent and not s.dispatch_os and s.work not in (2,3,4) and not s.callback_os:
        s = replace(s, module=False)
    return s

def tags(s):
    return int(s.dispatch_body) + int(s.work_hold) + int(s.callback_hold) + int(s.request_tag)

def child_users(s):
    return s.dispatch_body or s.work == 3 or s.callback_body or s.work_hold or s.callback_hold or s.request_tag

def step(s, event):
    """Return immutable next state, or None for rejected transition; no I/O."""
    t = None
    if event == 'close' and not s.closed:
        t = replace(s, closed=True)
    elif event == 'cancel_bit' and not s.cancel and s.phase != TERMINAL:
        # Environment indicates cancellation; NOT a call to IoCancelIrp by driver.
        t = replace(s, cancel=True)
    elif event == 'prepare' and s.phase == NEW and not s.closed:
        t = replace(s, phase=PREPARED, work=1, work_hold=True, work_ref=True, request_tag=True)
    elif event == 'reject_before_admission' and s.phase == NEW and s.closed:
        t = replace(s, phase=TERMINAL, completed=1)
    elif event == 'insert' and s.phase == PREPARED:
        # Abstract atomic CSQ arbitration, including already-cancelled insertion.
        # Mark BEFORE transfer; after transfer dispatch never touches IRP again.
        t = replace(s, phase=QUEUED, marked=True)
    elif event == 'csq_cancel' and s.phase == QUEUED and s.cancel:
        t = replace(s, phase=TERMINAL, completed=1, request_tag=False)
    elif event == 'schedule' and s.work == 1 and s.marked:
        # Schedule even when inline CSQ cancellation already completed request:
        # worker runs empty, avoiding an IRP dereference after insert/wakeup race.
        t = replace(s, work=2)
    elif event == 'worker_enter' and s.work == 2:
        t = replace(s, work=3)
    elif event == 'dequeue' and s.work == 3 and s.phase == QUEUED and not s.cancel:
        # Atomic with csq_cancel; ownership goes to exactly ONE winner.
        t = replace(s, phase=OWNED)
    elif event == 'owned_cancel_complete' and s.work == 3 and s.phase == OWNED and s.cancel:
        # Valid only for proposed PRIVATE request policy, never PnP/power.
        t = replace(s, phase=TERMINAL, completed=1, request_tag=False)
    elif event == 'register_fail' and s.work == 3 and s.phase == OWNED:
        t = replace(s, phase=TERMINAL, completed=1, request_tag=False)
    elif event == 'register' and s.work == 3 and s.phase == OWNED:
        t = replace(s, phase=REGISTERED, callback_os=True, callback_hold=True, callback_ref=True)
    elif event == 'forward' and s.phase == REGISTERED and s.work == 3:
        # Mandatory even when close/cancel interleaves AFTER registration success.
        t = replace(s, phase=LOWER)
    elif event == 'lower_callback' and s.phase == LOWER:
        t = replace(s, phase=CALLBACK, callback_body=True)
    elif event == 'known_completion' and s.phase == CALLBACK:
        # Abstract terminal lower+postprocess ownership proof, no actual bridge.
        # Success is possible despite cancel bit. Incoming forwarded IRP is NOT
        # locally completed just because cancel returned/bit became set.
        t = replace(s, phase=TERMINAL, completed=1, request_tag=False)
    elif event == 'unknown_completion' and s.phase == CALLBACK:
        t = replace(s, phase=UNKNOWN)
    elif event == 'callback_body_end' and s.callback_body and s.phase in (TERMINAL, UNKNOWN):
        known = s.phase == TERMINAL
        t = replace(s, callback_body=False, callback_hold=not known, callback_ref=not known)
    elif event == 'callback_os_return' and s.callback_os and not s.callback_body and s.phase in (TERMINAL, UNKNOWN):
        # Only ENVIRONMENT can produce this boundary, never a driver-set flag.
        t = replace(s, callback_os=False)
    elif event == 'worker_body_end' and s.work == 3 and s.phase in (LOWER, CALLBACK, TERMINAL, UNKNOWN):
        t = replace(s, work=4, work_hold=False, work_ref=False)
    elif event == 'worker_os_return' and s.work == 4:
        t = replace(s, work=5)
    elif event == 'dispatch_body_end' and s.dispatch_body and (s.work >= 2 or (s.phase == TERMINAL and not s.marked)):
        t = replace(s, dispatch_body=False)
    elif event == 'dispatch_os_return' and s.dispatch_os and not s.dispatch_body:
        t = replace(s, dispatch_os=False)
    elif event == 'child_retire' and s.closed and s.phase == TERMINAL and s.child and not child_users(s):
        t = replace(s, child=False)
    elif event == 'remove_forward' and s.closed and not s.remove_forwarded:
        t = replace(s, remove_forwarded=True)
    elif event == 'remove_wait_done' and s.remove_forwarded and not s.wait_done and not tags(s):
        # Abstract passive wait return; not a wait in a worker or under any lock.
        t = replace(s, wait_done=True)
    elif event == 'detach_lower' and s.wait_done and not s.child and not s.detached:
        t = replace(s, detached=True)
    elif event == 'delete_request' and s.detached and not s.delete_pending:
        t = replace(s, delete_pending=True)
    # Deliberately absent: local_complete_lower, cancel_return_means_done,
    # enqueue_pnp/power, reset_remove_lock, remove_wait_before_forward, force_free.
    return normalize(t) if t is not None else None

EVENTS = (
    'close','cancel_bit','prepare','reject_before_admission','insert','csq_cancel',
    'schedule','worker_enter','dequeue','owned_cancel_complete','register_fail',
    'register','forward','lower_callback','known_completion','unknown_completion',
    'callback_body_end','callback_os_return','worker_body_end','worker_os_return',
    'dispatch_body_end','dispatch_os_return','child_retire','remove_forward',
    'remove_wait_done','detach_lower','delete_request',
)

def check(s):
    """Invariant checker, explicit raises (survives python -O)."""
    def require(value, message):
        if not value: raise AssertionError(message)
    require(s.completed in (0,1), 'double completion')
    require(s.completed == int(s.phase == TERMINAL), 'terminal/completion mismatch')
    require(s.child or not child_users(s), 'child reclaimed with live use/obligation')
    require(s.parent or not (s.dispatch_body or s.work in (1,2,3,4) or s.callback_body or s.work_ref or s.callback_ref), 'parent reclaimed too early')
    require(s.module or not (s.dispatch_os or s.work in (2,3,4) or s.callback_os), 'module unloaded before kernel return')
    require(not s.wait_done or (s.remove_forwarded and not tags(s)), 'remove wait order/drain')
    require(not s.detached or (s.wait_done and not s.child), 'lower detached too early')
    require(not s.delete_pending or s.detached, 'device deletion order')
    require(s.phase != REGISTERED or (s.work == 3 and s.callback_os and s.callback_hold), 'registration obligation lost')
    require(s.phase != UNKNOWN or (s.child and s.callback_hold and s.callback_ref and s.request_tag), 'unknown obligation discarded')
    require(s.phase not in (QUEUED,OWNED,REGISTERED,LOWER,CALLBACK,UNKNOWN) or s.request_tag, 'request anchor lost')
    require(s.work != 1 or s.work_ref, 'prequeue envelope anchor lost')
    require(not s.callback_body or s.callback_os, 'callback code anchor lost')
