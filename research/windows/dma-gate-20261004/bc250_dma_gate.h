/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BC250_DMA_GATE_CANDIDATE_H
#define BC250_DMA_GATE_CANDIDATE_H
#ifdef BC250_DMA_GATE_MOCK
#include "mock-gate-wdm.h"
#else
#include <wdm.h>
#endif
#define BC250_DMA_GATE_SIGNATURE 0x47415431U
#define BC250_DMA_GATE_RUNNING 1L
#define BC250_DMA_GATE_STOPPING 2L
#define BC250_DMA_GATE_QUIESCING 3L
#define BC250_DMA_GATE_CLOSED 4L
#define BC250_DMA_GATE_FAULT 5L
typedef struct BC250_DMA_GATE BC250_DMA_GATE;
typedef struct BC250_DMA_GATE_TICKET {
    BC250_DMA_GATE *Gate;
    PVOID Thread;
    ULONGLONG Id;
    ULONG Active;
} BC250_DMA_GATE_TICKET;

/* Zero once, aligned resident storage, externally anchored BEFORE any API
 * entry and through all API returns (including failed entrants/coordinators).
 * Rundown does NOT solve publishing/removing this pointer or PDO lifetime.
 * Init is exclusive; no restart/reset/reinitialization. Internal fields are
 * not user-controlled. One trusted stable external ticket per admitted call,
 * on the SAME nonarbitrary PASSIVE thread until Leave. No thread exit with
 * a live ticket. No lock-order inversions or blocking on a remover in a call.
 * Only Stop is allowed from synchronous hooks. Quiesce is called by an
 * external coordinator after its own Enter/Leave attempts fully return;
 * it must hold NO reference on this gate, including an in-progress attempt.
 * All payload access must be exclusive via this gate; cross-gate nesting is
 * caller-prohibited. Stop only closes admission. Quiesce protects API access,
 * NOT map references, MDLs, DMA ownership, PnP completion or GPU quiescence.
 */
struct BC250_DMA_GATE {
    KMUTEX Mutex;
    EX_RUNDOWN_REF Rundown;
    PVOID volatile OwnerThread;
    BC250_DMA_GATE_TICKET *Ticket; /* Under Mutex, accessed only by owning thread. */
    ULONGLONG LastId;
    volatile LONG State;
    ULONG Signature;
};
NTSTATUS Bc250DmaGateInit(BC250_DMA_GATE *);
NTSTATUS Bc250DmaGateEnter(BC250_DMA_GATE *, BC250_DMA_GATE_TICKET *);
NTSTATUS Bc250DmaGateLeave(BC250_DMA_GATE *, BC250_DMA_GATE_TICKET *);
NTSTATUS Bc250DmaGateStop(BC250_DMA_GATE *);
NTSTATUS Bc250DmaGateQuiesce(BC250_DMA_GATE *);
#endif
