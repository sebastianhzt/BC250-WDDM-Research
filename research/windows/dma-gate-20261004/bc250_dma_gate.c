/* SPDX-License-Identifier: Apache-2.0
 * Standalone /c candidate, no driver integration or resource cleanup.
 */
#include "bc250_dma_gate.h"

static BOOLEAN Bc250DmaGatePolicy(void)
{
#ifdef BC250_DMA_GATE_MOCK
    return Bc250MockGateExecutionAllowed;
#else
    return FALSE; /* Production execution gate is permanently closed. */
#endif
}
static NTSTATUS Bc250DmaGateGuard(BC250_DMA_GATE *gate)
{
    if (!Bc250DmaGatePolicy()) return STATUS_NOT_SUPPORTED;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) return STATUS_INVALID_DEVICE_STATE;
    if (!gate) return STATUS_INVALID_PARAMETER;
    return STATUS_SUCCESS;
}
static LONG Bc250DmaGateState(BC250_DMA_GATE *gate)
{
    return InterlockedCompareExchange(&gate->State, 0, 0);
}
static BOOLEAN Bc250DmaGateAway(BC250_DMA_GATE *gate, const void *buffer, SIZE_T bytes)
{
    ULONG_PTR a = (ULONG_PTR)gate, b = (ULONG_PTR)buffer, maximum = ~(ULONG_PTR)0;
    if (!buffer || !bytes || a > maximum - (sizeof(*gate) - 1U) ||
        b > maximum - (bytes - 1U)) return FALSE;
    return a + sizeof(*gate) - 1U < b || b + bytes - 1U < a;
}
NTSTATUS Bc250DmaGateInit(BC250_DMA_GATE *gate)
{
    BC250_DMA_GATE zero;
    NTSTATUS status = Bc250DmaGateGuard(gate);
    if (status != STATUS_SUCCESS) return status;
    RtlZeroMemory(&zero, sizeof(zero));
    if (RtlCompareMemory(gate, &zero, sizeof(zero)) != sizeof(zero))
        return STATUS_INVALID_DEVICE_STATE;
    KeInitializeMutex(&gate->Mutex, 0);
    ExInitializeRundownProtection(&gate->Rundown);
    gate->Signature = BC250_DMA_GATE_SIGNATURE;
    InterlockedExchange(&gate->State, BC250_DMA_GATE_RUNNING);
    return STATUS_SUCCESS;
}
NTSTATUS Bc250DmaGateEnter(BC250_DMA_GATE *gate, BC250_DMA_GATE_TICKET *ticket)
{
    LARGE_INTEGER timeout;
    PVOID thread;
    NTSTATUS status = Bc250DmaGateGuard(gate);
    if (status != STATUS_SUCCESS) return status;
    if (gate->Signature != BC250_DMA_GATE_SIGNATURE ||
        !Bc250DmaGateAway(gate, ticket, sizeof(*ticket)))
        return STATUS_INVALID_PARAMETER;
    if (ticket->Active || ticket->Gate) return STATUS_INVALID_DEVICE_STATE;
    thread = KeGetCurrentThread();
    if (InterlockedCompareExchangePointer(&gate->OwnerThread, NULL, NULL) == thread)
        return STATUS_DEVICE_BUSY; /* Reject recursive acquisition, not a nested lease. */
    KeEnterCriticalRegion();
    if (!ExAcquireRundownProtection(&gate->Rundown)) {
        KeLeaveCriticalRegion(); return STATUS_CANCELLED;
    }
    if (Bc250DmaGateState(gate) != BC250_DMA_GATE_RUNNING) {
        ExReleaseRundownProtection(&gate->Rundown);
        KeLeaveCriticalRegion(); return STATUS_CANCELLED;
    }
    timeout.QuadPart = 0; /* Nonblocking try; caller retry policy is out of scope. */
    status = KeWaitForSingleObject(&gate->Mutex, Executive, KernelMode, FALSE, &timeout);
    if (status == STATUS_TIMEOUT) {
        ExReleaseRundownProtection(&gate->Rundown);
        KeLeaveCriticalRegion(); return STATUS_DEVICE_BUSY;
    }
    if (status != STATUS_SUCCESS) {
        /* Broken wait contract: unknown ownership, never guess release.
         * Retain rundown/storage, poison gate. No public recovery.
         */
        InterlockedExchange(&gate->State, BC250_DMA_GATE_FAULT);
        KeLeaveCriticalRegion(); return STATUS_INVALID_DEVICE_STATE;
    }
    if (Bc250DmaGateState(gate) != BC250_DMA_GATE_RUNNING ||
        gate->LastId == ~(ULONGLONG)0) {
        KeReleaseMutex(&gate->Mutex, FALSE);
        ExReleaseRundownProtection(&gate->Rundown);
        KeLeaveCriticalRegion(); return STATUS_CANCELLED;
    }
    ++gate->LastId; gate->Ticket = ticket;
    ticket->Gate = gate; ticket->Thread = thread; ticket->Id = gate->LastId; ticket->Active = 1U;
    InterlockedExchangePointer(&gate->OwnerThread, thread);
    return STATUS_SUCCESS; /* Critical region + Mutex + Rundown retained until Leave. */
}
NTSTATUS Bc250DmaGateLeave(BC250_DMA_GATE *gate, BC250_DMA_GATE_TICKET *ticket)
{
    PVOID thread;
    NTSTATUS status = Bc250DmaGateGuard(gate);
    if (status != STATUS_SUCCESS) return status;
    if (gate->Signature != BC250_DMA_GATE_SIGNATURE ||
        !Bc250DmaGateAway(gate, ticket, sizeof(*ticket))) return STATUS_INVALID_PARAMETER;
    thread = KeGetCurrentThread();
    if (ticket->Gate != gate || ticket->Active != 1U || ticket->Thread != thread ||
        InterlockedCompareExchangePointer(&gate->OwnerThread, NULL, NULL) != thread)
        return STATUS_INVALID_DEVICE_STATE;
    /* Only the mutex-owning thread may inspect these non-atomic fields. */
    if (gate->Ticket != ticket || ticket->Id != gate->LastId)
        return STATUS_INVALID_DEVICE_STATE;
    gate->Ticket = NULL;
    InterlockedExchangePointer(&gate->OwnerThread, NULL);
    RtlZeroMemory(ticket, sizeof(*ticket));
    KeReleaseMutex(&gate->Mutex, FALSE);
    ExReleaseRundownProtection(&gate->Rundown);
    /* NO gate/ticket access after dropping the final rundown reference. */
    KeLeaveCriticalRegion();
    return STATUS_SUCCESS;
}
NTSTATUS Bc250DmaGateStop(BC250_DMA_GATE *gate)
{
    LONG state;
    NTSTATUS status = Bc250DmaGateGuard(gate);
    if (status != STATUS_SUCCESS) return status;
    if (gate->Signature != BC250_DMA_GATE_SIGNATURE) return STATUS_INVALID_PARAMETER;
    state = InterlockedCompareExchange(&gate->State, BC250_DMA_GATE_STOPPING, BC250_DMA_GATE_RUNNING);
    return state == BC250_DMA_GATE_RUNNING || state == BC250_DMA_GATE_STOPPING ||
        state == BC250_DMA_GATE_QUIESCING ? STATUS_SUCCESS : STATUS_INVALID_DEVICE_STATE;
}
NTSTATUS Bc250DmaGateQuiesce(BC250_DMA_GATE *gate)
{
    LONG state;
    NTSTATUS status = Bc250DmaGateGuard(gate);
    if (status != STATUS_SUCCESS) return status;
    if (gate->Signature != BC250_DMA_GATE_SIGNATURE) return STATUS_INVALID_PARAMETER;
    if (InterlockedCompareExchangePointer(&gate->OwnerThread, NULL, NULL) == KeGetCurrentThread())
        return STATUS_DEVICE_BUSY; /* Never wait for your own retained reference. */
    state = InterlockedCompareExchange(&gate->State, BC250_DMA_GATE_QUIESCING, BC250_DMA_GATE_STOPPING);
    if (state == BC250_DMA_GATE_QUIESCING) return STATUS_DEVICE_BUSY;
    if (state != BC250_DMA_GATE_STOPPING) return STATUS_INVALID_DEVICE_STATE;
    ExWaitForRundownProtectionRelease(&gate->Rundown);
    InterlockedExchange(&gate->State, BC250_DMA_GATE_CLOSED);
    return STATUS_SUCCESS; /* Payload maps/resources are NOT released here. */
}
