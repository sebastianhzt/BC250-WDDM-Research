/* SPDX-License-Identifier: Apache-2.0
 * Standalone /c VARIANT, fake-tested only. No real map/address consumers.
 */
#include "bc250_dma_leases.h"
#ifdef BC250_DMA_LEASES_MOCK
extern BOOLEAN Bc250MockLeasesExecutionAllowed;
#endif
static BOOLEAN Bc250DmaLeasesPolicy(void)
{
#ifdef BC250_DMA_LEASES_MOCK
    return Bc250MockLeasesExecutionAllowed;
#else
    return FALSE;
#endif
}
static NTSTATUS Bc250DmaLeasesGuard(BC250_DMA_LEASES *owner)
{
    if (!Bc250DmaLeasesPolicy()) return STATUS_NOT_SUPPORTED;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) return STATUS_INVALID_DEVICE_STATE;
    if (!owner) return STATUS_INVALID_PARAMETER;
    return STATUS_SUCCESS;
}
static BOOLEAN Bc250DmaLeasesDisjoint(const void *a, SIZE_T an, const void *b, SIZE_T bn)
{
    ULONG_PTR x = (ULONG_PTR)a, y = (ULONG_PTR)b, maximum = ~(ULONG_PTR)0;
    if (!a || !b || !an || !bn || x > maximum - (an - 1U) || y > maximum - (bn - 1U)) return FALSE;
    return x + an - 1U < y || y + bn - 1U < x;
}
static BOOLEAN Bc250DmaLeasesAway(BC250_DMA_LEASES *owner, const void *buffer, SIZE_T bytes)
{
    return Bc250DmaLeasesDisjoint(owner, sizeof(*owner), buffer, bytes);
}
static void Bc250DmaLeasesFault(BC250_DMA_LEASES *owner)
{
    InterlockedExchange(&owner->State, BC250_DMA_LEASES_FAULT);
    Bc250DmaGateStop(&owner->Gate); /* Stop only; no implicit recovery. */
}
static NTSTATUS Bc250DmaLeasesLock(BC250_DMA_LEASES *owner)
{
    LARGE_INTEGER timeout;
    NTSTATUS status;
    PVOID thread = KeGetCurrentThread();
    if (InterlockedCompareExchangePointer(&owner->MetadataThread, NULL, NULL) == thread)
        return STATUS_DEVICE_BUSY;
    KeEnterCriticalRegion(); timeout.QuadPart = 0;
    status = KeWaitForSingleObject(&owner->Metadata, Executive, KernelMode, FALSE, &timeout);
    if (status == STATUS_TIMEOUT) { KeLeaveCriticalRegion(); return STATUS_DEVICE_BUSY; }
    if (status != STATUS_SUCCESS) {
        Bc250DmaLeasesFault(owner); /* Unknown mutex ownership, retain storage. */
        KeLeaveCriticalRegion(); return STATUS_INVALID_DEVICE_STATE;
    }
    InterlockedExchangePointer(&owner->MetadataThread, thread);
    return STATUS_SUCCESS;
}
static void Bc250DmaLeasesUnlock(BC250_DMA_LEASES *owner)
{
    InterlockedExchangePointer(&owner->MetadataThread, NULL);
    KeReleaseMutex(&owner->Metadata, FALSE);
    KeLeaveCriticalRegion();
}
static NTSTATUS Bc250DmaLeasesCheck(BC250_DMA_LEASES *owner)
{
    ULONG i, j, count = 0;
    for (i = 0; i < BC250_DMA_LEASES_SLOTS; ++i) {
        const BC250_DMA_LEASE_RECORD *record = &owner->Records[i];
        if (!record->Live) continue;
        if (record->Live != 1U || !record->Id || record->Id > owner->LastId)
            return STATUS_INVALID_DEVICE_STATE;
        for (j = 0; j < i; ++j) if (owner->Records[j].Live && owner->Records[j].Id == record->Id)
            return STATUS_INVALID_DEVICE_STATE;
        ++count;
    }
    if (count != owner->References) return STATUS_INVALID_DEVICE_STATE;
    if (count && (!owner->PinnedMdl || !owner->PinnedList || !owner->PinnedListBytes))
        return STATUS_INVALID_DEVICE_STATE;
    return STATUS_SUCCESS;
}
static BOOLEAN Bc250DmaLeasesBuffer(BC250_DMA_LEASES *owner, const void *buffer, SIZE_T bytes)
{
    if (!Bc250DmaLeasesAway(owner, buffer, bytes)) return FALSE;
    if (owner->PinnedMdl && !Bc250DmaLeasesDisjoint(owner->PinnedMdl, sizeof(*owner->PinnedMdl), buffer, bytes))
        return FALSE;
    if (owner->PinnedList && !Bc250DmaLeasesDisjoint(owner->PinnedList, owner->PinnedListBytes, buffer, bytes))
        return FALSE;
    return TRUE;
}
static BOOLEAN Bc250DmaLeasesEmptyOut(BC250_DMA_LEASE_HANDLE *out)
{
    BC250_DMA_LEASE_HANDLE zero;
    RtlZeroMemory(&zero, sizeof(zero));
    return RtlCompareMemory(out, &zero, sizeof(zero)) == sizeof(zero);
}
static NTSTATUS Bc250DmaLeasesMatch(BC250_DMA_LEASES *owner, const BC250_DMA_LEASE_HANDLE *handle)
{
    if (!Bc250DmaLeasesBuffer(owner, handle, sizeof(*handle))) return STATUS_INVALID_PARAMETER;
    if (handle->Owner != owner || handle->Slot >= BC250_DMA_LEASES_SLOTS || !handle->Id ||
        !owner->Records[handle->Slot].Live || owner->Records[handle->Slot].Id != handle->Id)
        return STATUS_INVALID_DEVICE_STATE;
    return STATUS_SUCCESS;
}
static NTSTATUS Bc250DmaLeasesNew(BC250_DMA_LEASES *owner, BC250_DMA_LEASE_HANDLE *out)
{
    BC250_DMA_LEASE_HANDLE handle = {0};
    ULONG i;
    if (!Bc250DmaLeasesBuffer(owner, out, sizeof(*out)) || !Bc250DmaLeasesEmptyOut(out))
        return STATUS_INVALID_PARAMETER;
    if (owner->LastId == ~(ULONGLONG)0) return STATUS_INSUFFICIENT_RESOURCES;
    for (i = 0; i < BC250_DMA_LEASES_SLOTS; ++i) if (!owner->Records[i].Live) break;
    if (i == BC250_DMA_LEASES_SLOTS) return STATUS_INSUFFICIENT_RESOURCES;
    handle.Owner = owner; handle.Id = ++owner->LastId; handle.Slot = i;
    owner->Records[i].Id = handle.Id; owner->Records[i].Live = 1U; ++owner->References;
    *out = handle; return STATUS_SUCCESS;
}
static NTSTATUS Bc250DmaLeasesAdmit(BC250_DMA_LEASES *owner, BC250_DMA_GATE_TICKET *ticket)
{
    NTSTATUS status;
    if (owner->Signature != BC250_DMA_LEASES_SIGNATURE) return STATUS_INVALID_PARAMETER;
    if (InterlockedCompareExchange(&owner->State, 0, 0) != BC250_DMA_LEASES_ACTIVE) return STATUS_CANCELLED;
    status = Bc250DmaGateEnter(&owner->Gate, ticket);
    if (status != STATUS_SUCCESS && InterlockedCompareExchange(&owner->Gate.State, 0, 0) == BC250_DMA_GATE_FAULT)
        Bc250DmaLeasesFault(owner);
    return status;
}
static NTSTATUS Bc250DmaLeasesFinish(BC250_DMA_LEASES *owner, BC250_DMA_GATE_TICKET *ticket, NTSTATUS status)
{
    NTSTATUS leave;
    if (owner->Native.State == BC250_WIN_DMA_QUARANTINED) {
        Bc250DmaLeasesFault(owner); status = STATUS_INVALID_DEVICE_STATE;
    }
    leave = Bc250DmaGateLeave(&owner->Gate, ticket);
    if (leave != STATUS_SUCCESS) { Bc250DmaLeasesFault(owner); return leave; }
    return status; /* No owner/handle access after successful last GateLeave. */
}
NTSTATUS Bc250DmaLeasesInit(BC250_DMA_LEASES *owner, PDEVICE_OBJECT pdo, const DEVICE_DESCRIPTION *description)
{
    BC250_DMA_LEASES zero;
    BC250_DMA_GATE_TICKET ticket = {0};
    NTSTATUS status = Bc250DmaLeasesGuard(owner);
    if (status != STATUS_SUCCESS) return status;
    RtlZeroMemory(&zero, sizeof(zero));
    if (RtlCompareMemory(owner, &zero, sizeof(zero)) != sizeof(zero)) return STATUS_INVALID_DEVICE_STATE;
    if (!Bc250DmaLeasesAway(owner, pdo, sizeof(*pdo)) ||
        !Bc250DmaLeasesAway(owner, description, sizeof(*description))) return STATUS_INVALID_PARAMETER;
    status = Bc250DmaGateInit(&owner->Gate);
    if (status != STATUS_SUCCESS) return status;
    KeInitializeMutex(&owner->Metadata, 0);
    owner->Signature = BC250_DMA_LEASES_SIGNATURE;
    InterlockedExchange(&owner->State, BC250_DMA_LEASES_ACTIVE);
    status = Bc250DmaLeasesAdmit(owner, &ticket);
    if (status != STATUS_SUCCESS) return status;
    status = Bc250WinDmaOpen(&owner->Native, pdo, description);
    return Bc250DmaLeasesFinish(owner, &ticket, status);
}
NTSTATUS Bc250DmaLeasesAcquire(BC250_DMA_LEASES *owner, PMDL mdl, ULONGLONG offset,
    ULONG bytes, BOOLEAN direction, BC250_DMA_LEASE_HANDLE *out)
{
    BC250_DMA_GATE_TICKET ticket = {0};
    NTSTATUS status = Bc250DmaLeasesGuard(owner);
    if (status != STATUS_SUCCESS) return status;
    if (!Bc250DmaLeasesAway(owner, mdl, sizeof(*mdl)) || !Bc250DmaLeasesAway(owner, out, sizeof(*out)) ||
        !Bc250DmaLeasesDisjoint(mdl, sizeof(*mdl), out, sizeof(*out))) return STATUS_INVALID_PARAMETER;
    status = Bc250DmaLeasesAdmit(owner, &ticket);
    if (status != STATUS_SUCCESS) return status;
    status = Bc250DmaLeasesLock(owner);
    if (status == STATUS_SUCCESS) {
        status = Bc250DmaLeasesCheck(owner);
        if (status != STATUS_SUCCESS) Bc250DmaLeasesFault(owner);
        else if (InterlockedCompareExchange(&owner->State, 0, 0) == BC250_DMA_LEASES_FAULT)
            status = STATUS_INVALID_DEVICE_STATE;
        else if (!Bc250DmaLeasesEmptyOut(out)) status = STATUS_INVALID_PARAMETER;
        else if (owner->LastId == ~(ULONGLONG)0 || owner->References == BC250_DMA_LEASES_SLOTS)
            status = STATUS_INSUFFICIENT_RESOURCES;
        else {
            status = Bc250WinDmaAcquire(&owner->Native, mdl, offset, bytes, direction);
            if (status == STATUS_SUCCESS) {
                SIZE_T listBytes = FIELD_OFFSET(SCATTER_GATHER_LIST, Elements) +
                    (SIZE_T)owner->Native.List->NumberOfElements * sizeof(owner->Native.List->Elements[0]);
                if (!Bc250DmaLeasesAway(owner, owner->Native.List, listBytes)) {
                    Bc250DmaLeasesFault(owner); status = STATUS_INVALID_DEVICE_STATE;
                } else {
                    owner->PinnedMdl = mdl; owner->PinnedList = owner->Native.List; owner->PinnedListBytes = listBytes;
                    status = Bc250DmaLeasesNew(owner, out);
                }
            }
        }
        Bc250DmaLeasesUnlock(owner);
    }
    return Bc250DmaLeasesFinish(owner, &ticket, status);
}
NTSTATUS Bc250DmaLeasesRetain(BC250_DMA_LEASES *owner, const BC250_DMA_LEASE_HANDLE *input,
    BC250_DMA_LEASE_HANDLE *out)
{
    BC250_DMA_GATE_TICKET ticket = {0};
    NTSTATUS status = Bc250DmaLeasesGuard(owner);
    if (status != STATUS_SUCCESS) return status;
    if (!Bc250DmaLeasesAway(owner, input, sizeof(*input)) || !Bc250DmaLeasesAway(owner, out, sizeof(*out)) ||
        !Bc250DmaLeasesDisjoint(input, sizeof(*input), out, sizeof(*out))) return STATUS_INVALID_PARAMETER;
    status = Bc250DmaLeasesAdmit(owner, &ticket);
    if (status != STATUS_SUCCESS) return status;
    status = Bc250DmaLeasesLock(owner);
    if (status == STATUS_SUCCESS) {
        status = Bc250DmaLeasesCheck(owner);
        if (status != STATUS_SUCCESS) Bc250DmaLeasesFault(owner);
        else if (InterlockedCompareExchange(&owner->State, 0, 0) == BC250_DMA_LEASES_FAULT)
            status = STATUS_INVALID_DEVICE_STATE;
        else if (owner->Native.State != BC250_WIN_DMA_HELD) status = STATUS_INVALID_DEVICE_STATE;
        else {
            status = Bc250DmaLeasesMatch(owner, input);
            if (status == STATUS_SUCCESS) status = Bc250DmaLeasesNew(owner, out);
        }
        Bc250DmaLeasesUnlock(owner);
    }
    return Bc250DmaLeasesFinish(owner, &ticket, status);
}
NTSTATUS Bc250DmaLeasesDrop(BC250_DMA_LEASES *owner, const BC250_DMA_LEASE_HANDLE *input)
{
    NTSTATUS status = Bc250DmaLeasesGuard(owner);
    if (status != STATUS_SUCCESS) return status;
    if (owner->Signature != BC250_DMA_LEASES_SIGNATURE || !Bc250DmaLeasesAway(owner, input, sizeof(*input)))
        return STATUS_INVALID_PARAMETER;
    status = Bc250DmaLeasesLock(owner);
    if (status != STATUS_SUCCESS) return status;
    status = Bc250DmaLeasesCheck(owner);
    if (status != STATUS_SUCCESS) Bc250DmaLeasesFault(owner);
    else {
        status = Bc250DmaLeasesMatch(owner, input);
        if (status == STATUS_SUCCESS) {
            RtlZeroMemory(&owner->Records[input->Slot], sizeof(owner->Records[input->Slot]));
            --owner->References; /* No Native, GateEnter, Cancel or cleanup. */
        }
    }
    Bc250DmaLeasesUnlock(owner);
    return status;
}
NTSTATUS Bc250DmaLeasesStop(BC250_DMA_LEASES *owner)
{
    LONG state;
    NTSTATUS status = Bc250DmaLeasesGuard(owner);
    if (status != STATUS_SUCCESS) return status;
    if (owner->Signature != BC250_DMA_LEASES_SIGNATURE) return STATUS_INVALID_PARAMETER;
    state = InterlockedCompareExchange(&owner->State, BC250_DMA_LEASES_STOPPING, BC250_DMA_LEASES_ACTIVE);
    if (state == BC250_DMA_LEASES_DRAINED) return STATUS_SUCCESS;
    if (state != BC250_DMA_LEASES_ACTIVE && state != BC250_DMA_LEASES_STOPPING) return STATUS_INVALID_DEVICE_STATE;
    status = Bc250DmaGateStop(&owner->Gate);
    if (status != STATUS_SUCCESS) {
        /* An already-linearized Stop may resume after full retirement. */
        LONG current = InterlockedCompareExchange(&owner->State, 0, 0);
        if (InterlockedCompareExchange(&owner->Gate.State, 0, 0) == BC250_DMA_GATE_CLOSED &&
            (current == BC250_DMA_LEASES_STOPPING || current == BC250_DMA_LEASES_DRAINED ||
             current == BC250_DMA_LEASES_RETIRED))
            return STATUS_SUCCESS;
        Bc250DmaLeasesFault(owner);
    }
    return status;
}
NTSTATUS Bc250DmaLeasesRetire(BC250_DMA_LEASES *owner)
{
    BC250_WIN_DMA zero;
    LONG state;
    NTSTATUS status = Bc250DmaLeasesGuard(owner);
    if (status != STATUS_SUCCESS) return status;
    if (owner->Signature != BC250_DMA_LEASES_SIGNATURE) return STATUS_INVALID_PARAMETER;
    if (InterlockedCompareExchangePointer(&owner->Gate.OwnerThread, NULL, NULL) == KeGetCurrentThread() ||
        InterlockedCompareExchangePointer(&owner->MetadataThread, NULL, NULL) == KeGetCurrentThread())
        return STATUS_DEVICE_BUSY;
    if (InterlockedCompareExchange(&owner->Coordinator, 1, 0)) return STATUS_DEVICE_BUSY;
    state = InterlockedCompareExchange(&owner->State, 0, 0);
    if (state != BC250_DMA_LEASES_ACTIVE && state != BC250_DMA_LEASES_STOPPING && state != BC250_DMA_LEASES_DRAINED) {
        InterlockedExchange(&owner->Coordinator, 0); return STATUS_INVALID_DEVICE_STATE;
    }
    status = Bc250DmaLeasesStop(owner);
    if (status == STATUS_SUCCESS && InterlockedCompareExchange(&owner->Gate.State, 0, 0) != BC250_DMA_GATE_CLOSED)
        status = Bc250DmaGateQuiesce(&owner->Gate);
    if (status != STATUS_SUCCESS || InterlockedCompareExchange(&owner->State, 0, 0) == BC250_DMA_LEASES_FAULT) {
        Bc250DmaLeasesFault(owner); InterlockedExchange(&owner->Coordinator, 0);
        return STATUS_INVALID_DEVICE_STATE;
    }
    InterlockedCompareExchange(&owner->State, BC250_DMA_LEASES_DRAINED, BC250_DMA_LEASES_STOPPING);
    status = Bc250DmaLeasesLock(owner);
    if (status != STATUS_SUCCESS) { InterlockedExchange(&owner->Coordinator, 0); return status; }
    /* A Drop/metadata failure could publish FAULT after the earlier check. */
    if (InterlockedCompareExchange(&owner->State, 0, 0) != BC250_DMA_LEASES_DRAINED)
        status = STATUS_INVALID_DEVICE_STATE;
    else status = Bc250DmaLeasesCheck(owner);
    if (status != STATUS_SUCCESS) Bc250DmaLeasesFault(owner);
    else if (owner->References) status = STATUS_DEVICE_BUSY;
    else {
        RtlZeroMemory(&zero, sizeof(zero));
        if (RtlCompareMemory(&owner->Native, &zero, sizeof(zero)) != sizeof(zero)) {
            if (owner->Native.State != BC250_WIN_DMA_OPEN && owner->Native.State != BC250_WIN_DMA_HELD)
                status = STATUS_INVALID_DEVICE_STATE;
            else {
                status = Bc250WinDmaCancel(&owner->Native);
                if (status == STATUS_SUCCESS) status = Bc250WinDmaClose(&owner->Native);
            }
        }
        if (status != STATUS_SUCCESS) Bc250DmaLeasesFault(owner);
        else {
            owner->PinnedMdl = NULL; owner->PinnedList = NULL; owner->PinnedListBytes = 0;
            InterlockedExchange(&owner->State, BC250_DMA_LEASES_RETIRED);
        }
    }
    Bc250DmaLeasesUnlock(owner);
    InterlockedExchange(&owner->Coordinator, 0);
    return status;
}
