/* SPDX-License-Identifier: Apache-2.0
 * Standalone /c VARIANT, fake-tested only. No real map/address consumers.
 */
#include "bc250_dma_capture.h"
#ifdef BC250_DMA_CAPTURE_MOCK
extern BOOLEAN Bc250MockCaptureExecutionAllowed;
#endif
static BOOLEAN Bc250DmaCapturePolicy(void)
{
#ifdef BC250_DMA_CAPTURE_MOCK
    return Bc250MockCaptureExecutionAllowed;
#else
    return FALSE;
#endif
}
static NTSTATUS Bc250DmaCaptureGuard(BC250_DMA_CAPTURE *owner)
{
    if (!Bc250DmaCapturePolicy()) return STATUS_NOT_SUPPORTED;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) return STATUS_INVALID_DEVICE_STATE;
    if (!owner) return STATUS_INVALID_PARAMETER;
    return STATUS_SUCCESS;
}
static BOOLEAN Bc250DmaCaptureDisjoint(const void *a, SIZE_T an, const void *b, SIZE_T bn)
{
    ULONG_PTR x = (ULONG_PTR)a, y = (ULONG_PTR)b, maximum = ~(ULONG_PTR)0;
    if (!a || !b || !an || !bn || x > maximum - (an - 1U) || y > maximum - (bn - 1U)) return FALSE;
    return x + an - 1U < y || y + bn - 1U < x;
}
static BOOLEAN Bc250DmaCaptureAway(BC250_DMA_CAPTURE *owner, const void *buffer, SIZE_T bytes)
{
    return Bc250DmaCaptureDisjoint(owner, sizeof(*owner), buffer, bytes);
}
static void Bc250DmaCaptureFault(BC250_DMA_CAPTURE *owner)
{
    InterlockedExchange(&owner->State, BC250_DMA_CAPTURE_FAULT);
    Bc250DmaGateStop(&owner->Gate); /* Stop only; no implicit recovery. */
}
static NTSTATUS Bc250DmaCaptureLock(BC250_DMA_CAPTURE *owner)
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
        Bc250DmaCaptureFault(owner); /* Unknown mutex ownership, retain storage. */
        KeLeaveCriticalRegion(); return STATUS_INVALID_DEVICE_STATE;
    }
    InterlockedExchangePointer(&owner->MetadataThread, thread);
    return STATUS_SUCCESS;
}
static void Bc250DmaCaptureUnlock(BC250_DMA_CAPTURE *owner)
{
    InterlockedExchangePointer(&owner->MetadataThread, NULL);
    KeReleaseMutex(&owner->Metadata, FALSE);
    KeLeaveCriticalRegion();
}
static NTSTATUS Bc250DmaCaptureCheck(BC250_DMA_CAPTURE *owner)
{
    ULONG i, j, count = 0;
    for (i = 0; i < BC250_DMA_CAPTURE_SLOTS; ++i) {
        const BC250_DMA_CAPTURE_RECORD *record = &owner->Records[i];
        if (!record->Live) {
            if (record->Snapshot) return STATUS_INVALID_DEVICE_STATE;
            continue;
        }
        if (record->Live != 1U || !record->Id || record->Id > owner->LastId)
            return STATUS_INVALID_DEVICE_STATE;
        for (j = 0; j < i; ++j) if (owner->Records[j].Live && owner->Records[j].Id == record->Id)
            return STATUS_INVALID_DEVICE_STATE;
        if (record->Snapshot) for (j = 0; j < i; ++j)
            if (owner->Records[j].Live && owner->Records[j].Snapshot == record->Snapshot)
                return STATUS_INVALID_DEVICE_STATE;
        ++count;
    }
    if (count != owner->References) return STATUS_INVALID_DEVICE_STATE;
    if (count && (owner->RequestValid != 1U || !owner->Request.Bytes ||
        (owner->Request.Bytes & 4095U) || owner->Request.Bytes > BC250_WIN_DMA_MAX_BYTES ||
        (owner->Request.Offset & 4095ULL) || owner->Request.Direction > TRUE ||
        !owner->RequestWidth || owner->RequestWidth > 48U)) return STATUS_INVALID_DEVICE_STATE;
    if (count && (!owner->PinnedMdl || !owner->PinnedList || !owner->PinnedListBytes))
        return STATUS_INVALID_DEVICE_STATE;
    return STATUS_SUCCESS;
}
static BOOLEAN Bc250DmaCaptureStorage(BC250_DMA_CAPTURE *owner, const void *buffer, SIZE_T bytes)
{
    if (!Bc250DmaCaptureAway(owner, buffer, bytes)) return FALSE;
    if (owner->PinnedMdl && !Bc250DmaCaptureDisjoint(owner->PinnedMdl, sizeof(*owner->PinnedMdl), buffer, bytes))
        return FALSE;
    if (owner->PinnedList && !Bc250DmaCaptureDisjoint(owner->PinnedList, owner->PinnedListBytes, buffer, bytes))
        return FALSE;
    return TRUE;
}
static BOOLEAN Bc250DmaCaptureBuffer(BC250_DMA_CAPTURE *owner, const void *buffer, SIZE_T bytes)
{
    ULONG i;
    if (!Bc250DmaCaptureStorage(owner, buffer, bytes)) return FALSE;
    for (i = 0; i < BC250_DMA_CAPTURE_SLOTS; ++i)
        if (owner->Records[i].Live && owner->Records[i].Snapshot &&
            !Bc250DmaCaptureDisjoint(owner->Records[i].Snapshot, sizeof(BC250_DMA_CAPTURE_SNAPSHOT),
                buffer, bytes)) return FALSE;
    return TRUE;
}
static BOOLEAN Bc250DmaCaptureEmptyOut(BC250_DMA_CAPTURE_HANDLE *out)
{
    BC250_DMA_CAPTURE_HANDLE zero;
    RtlZeroMemory(&zero, sizeof(zero));
    return RtlCompareMemory(out, &zero, sizeof(zero)) == sizeof(zero);
}
static NTSTATUS Bc250DmaCaptureMatch(BC250_DMA_CAPTURE *owner, const BC250_DMA_CAPTURE_HANDLE *handle)
{
    if (!Bc250DmaCaptureBuffer(owner, handle, sizeof(*handle))) return STATUS_INVALID_PARAMETER;
    if (handle->Owner != owner || handle->Slot >= BC250_DMA_CAPTURE_SLOTS || !handle->Id ||
        !owner->Records[handle->Slot].Live || owner->Records[handle->Slot].Id != handle->Id)
        return STATUS_INVALID_DEVICE_STATE;
    return STATUS_SUCCESS;
}
static NTSTATUS Bc250DmaCaptureNew(BC250_DMA_CAPTURE *owner, BC250_DMA_CAPTURE_HANDLE *out)
{
    BC250_DMA_CAPTURE_HANDLE handle = {0};
    ULONG i;
    if (!Bc250DmaCaptureBuffer(owner, out, sizeof(*out)) || !Bc250DmaCaptureEmptyOut(out))
        return STATUS_INVALID_PARAMETER;
    if (owner->LastId == ~(ULONGLONG)0) return STATUS_INSUFFICIENT_RESOURCES;
    for (i = 0; i < BC250_DMA_CAPTURE_SLOTS; ++i) if (!owner->Records[i].Live) break;
    if (i == BC250_DMA_CAPTURE_SLOTS) return STATUS_INSUFFICIENT_RESOURCES;
    handle.Owner = owner; handle.Id = ++owner->LastId; handle.Slot = i;
    owner->Records[i].Snapshot = NULL;
    owner->Records[i].Id = handle.Id; owner->Records[i].Live = 1U; ++owner->References;
    *out = handle; return STATUS_SUCCESS;
}
static NTSTATUS Bc250DmaCaptureAdmit(BC250_DMA_CAPTURE *owner, BC250_DMA_GATE_TICKET *ticket)
{
    NTSTATUS status;
    if (owner->Signature != BC250_DMA_CAPTURE_SIGNATURE) return STATUS_INVALID_PARAMETER;
    if (InterlockedCompareExchange(&owner->State, 0, 0) != BC250_DMA_CAPTURE_ACTIVE) return STATUS_CANCELLED;
    status = Bc250DmaGateEnter(&owner->Gate, ticket);
    if (status != STATUS_SUCCESS && InterlockedCompareExchange(&owner->Gate.State, 0, 0) == BC250_DMA_GATE_FAULT)
        Bc250DmaCaptureFault(owner);
    return status;
}
static NTSTATUS Bc250DmaCaptureFinish(BC250_DMA_CAPTURE *owner, BC250_DMA_GATE_TICKET *ticket, NTSTATUS status)
{
    NTSTATUS leave;
    if (owner->Native.State == BC250_WIN_DMA_QUARANTINED) {
        Bc250DmaCaptureFault(owner); status = STATUS_INVALID_DEVICE_STATE;
    }
    leave = Bc250DmaGateLeave(&owner->Gate, ticket);
    if (leave != STATUS_SUCCESS) { Bc250DmaCaptureFault(owner); return leave; }
    return status; /* No owner/handle access after successful last GateLeave. */
}
NTSTATUS Bc250DmaCaptureInit(BC250_DMA_CAPTURE *owner, PDEVICE_OBJECT pdo, const DEVICE_DESCRIPTION *description)
{
    BC250_DMA_CAPTURE zero;
    BC250_DMA_GATE_TICKET ticket = {0};
    NTSTATUS status = Bc250DmaCaptureGuard(owner);
    if (status != STATUS_SUCCESS) return status;
    RtlZeroMemory(&zero, sizeof(zero));
    if (RtlCompareMemory(owner, &zero, sizeof(zero)) != sizeof(zero)) return STATUS_INVALID_DEVICE_STATE;
    if (!Bc250DmaCaptureAway(owner, pdo, sizeof(*pdo)) ||
        !Bc250DmaCaptureAway(owner, description, sizeof(*description))) return STATUS_INVALID_PARAMETER;
    status = Bc250DmaGateInit(&owner->Gate);
    if (status != STATUS_SUCCESS) return status;
    KeInitializeMutex(&owner->Metadata, 0);
    owner->Signature = BC250_DMA_CAPTURE_SIGNATURE;
    InterlockedExchange(&owner->State, BC250_DMA_CAPTURE_ACTIVE);
    status = Bc250DmaCaptureAdmit(owner, &ticket);
    if (status != STATUS_SUCCESS) return status;
    status = Bc250WinDmaOpen(&owner->Native, pdo, description);
    return Bc250DmaCaptureFinish(owner, &ticket, status);
}
NTSTATUS Bc250DmaCaptureAcquire(BC250_DMA_CAPTURE *owner, PMDL mdl, ULONGLONG offset,
    ULONG bytes, BOOLEAN direction, BC250_DMA_CAPTURE_HANDLE *out)
{
    BC250_DMA_GATE_TICKET ticket = {0};
    NTSTATUS status = Bc250DmaCaptureGuard(owner);
    if (status != STATUS_SUCCESS) return status;
    if (!Bc250DmaCaptureAway(owner, mdl, sizeof(*mdl)) || !Bc250DmaCaptureAway(owner, out, sizeof(*out)) ||
        !Bc250DmaCaptureDisjoint(mdl, sizeof(*mdl), out, sizeof(*out))) return STATUS_INVALID_PARAMETER;
    status = Bc250DmaCaptureAdmit(owner, &ticket);
    if (status != STATUS_SUCCESS) return status;
    status = Bc250DmaCaptureLock(owner);
    if (status == STATUS_SUCCESS) {
        status = Bc250DmaCaptureCheck(owner);
        if (status != STATUS_SUCCESS) Bc250DmaCaptureFault(owner);
        else if (InterlockedCompareExchange(&owner->State, 0, 0) == BC250_DMA_CAPTURE_FAULT)
            status = STATUS_INVALID_DEVICE_STATE;
        else if (!Bc250DmaCaptureEmptyOut(out)) status = STATUS_INVALID_PARAMETER;
        else if (owner->LastId == ~(ULONGLONG)0 || owner->References == BC250_DMA_CAPTURE_SLOTS)
            status = STATUS_INSUFFICIENT_RESOURCES;
        else {
            status = Bc250WinDmaAcquire(&owner->Native, mdl, offset, bytes, direction);
            if (status == STATUS_SUCCESS) {
                SIZE_T listBytes = FIELD_OFFSET(SCATTER_GATHER_LIST, Elements) +
                    (SIZE_T)owner->Native.List->NumberOfElements * sizeof(owner->Native.List->Elements[0]);
                if (!Bc250DmaCaptureAway(owner, owner->Native.List, listBytes)) {
                    Bc250DmaCaptureFault(owner); status = STATUS_INVALID_DEVICE_STATE;
                } else {
                    owner->PinnedMdl = mdl; owner->PinnedList = owner->Native.List; owner->PinnedListBytes = listBytes;
                    owner->Request.Offset = offset; owner->Request.Bytes = bytes;
                    owner->Request.Direction = direction; owner->RequestWidth = owner->Native.AddressWidth;
                    owner->RequestValid = 1U;
                    status = Bc250DmaCaptureNew(owner, out);
                }
            }
        }
        Bc250DmaCaptureUnlock(owner);
    }
    return Bc250DmaCaptureFinish(owner, &ticket, status);
}
NTSTATUS Bc250DmaCaptureRetain(BC250_DMA_CAPTURE *owner, const BC250_DMA_CAPTURE_HANDLE *input,
    BC250_DMA_CAPTURE_HANDLE *out)
{
    BC250_DMA_GATE_TICKET ticket = {0};
    NTSTATUS status = Bc250DmaCaptureGuard(owner);
    if (status != STATUS_SUCCESS) return status;
    if (!Bc250DmaCaptureAway(owner, input, sizeof(*input)) || !Bc250DmaCaptureAway(owner, out, sizeof(*out)) ||
        !Bc250DmaCaptureDisjoint(input, sizeof(*input), out, sizeof(*out))) return STATUS_INVALID_PARAMETER;
    status = Bc250DmaCaptureAdmit(owner, &ticket);
    if (status != STATUS_SUCCESS) return status;
    status = Bc250DmaCaptureLock(owner);
    if (status == STATUS_SUCCESS) {
        status = Bc250DmaCaptureCheck(owner);
        if (status != STATUS_SUCCESS) Bc250DmaCaptureFault(owner);
        else if (InterlockedCompareExchange(&owner->State, 0, 0) == BC250_DMA_CAPTURE_FAULT)
            status = STATUS_INVALID_DEVICE_STATE;
        else if (owner->Native.State != BC250_WIN_DMA_HELD) status = STATUS_INVALID_DEVICE_STATE;
        else {
            status = Bc250DmaCaptureMatch(owner, input);
            if (status == STATUS_SUCCESS && owner->Records[input->Slot].Snapshot)
                status = STATUS_INVALID_PARAMETER;
            if (status == STATUS_SUCCESS) status = Bc250DmaCaptureNew(owner, out);
        }
        Bc250DmaCaptureUnlock(owner);
    }
    return Bc250DmaCaptureFinish(owner, &ticket, status);
}
NTSTATUS Bc250DmaCaptureDrop(BC250_DMA_CAPTURE *owner, const BC250_DMA_CAPTURE_HANDLE *input)
{
    NTSTATUS status = Bc250DmaCaptureGuard(owner);
    if (status != STATUS_SUCCESS) return status;
    if (owner->Signature != BC250_DMA_CAPTURE_SIGNATURE || !Bc250DmaCaptureAway(owner, input, sizeof(*input)))
        return STATUS_INVALID_PARAMETER;
    status = Bc250DmaCaptureLock(owner);
    if (status != STATUS_SUCCESS) return status;
    status = Bc250DmaCaptureCheck(owner);
    if (status != STATUS_SUCCESS) Bc250DmaCaptureFault(owner);
    else {
        status = Bc250DmaCaptureMatch(owner, input);
        if (status == STATUS_SUCCESS && owner->Records[input->Slot].Snapshot)
            status = STATUS_INVALID_PARAMETER;
        if (status == STATUS_SUCCESS) {
            RtlZeroMemory(&owner->Records[input->Slot], sizeof(owner->Records[input->Slot]));
            --owner->References; /* No Native, GateEnter, Cancel or cleanup. */
        }
    }
    Bc250DmaCaptureUnlock(owner);
    return status;
}
NTSTATUS Bc250DmaCaptureStop(BC250_DMA_CAPTURE *owner)
{
    LONG state;
    NTSTATUS status = Bc250DmaCaptureGuard(owner);
    if (status != STATUS_SUCCESS) return status;
    if (owner->Signature != BC250_DMA_CAPTURE_SIGNATURE) return STATUS_INVALID_PARAMETER;
    state = InterlockedCompareExchange(&owner->State, BC250_DMA_CAPTURE_STOPPING, BC250_DMA_CAPTURE_ACTIVE);
    if (state == BC250_DMA_CAPTURE_DRAINED) return STATUS_SUCCESS;
    if (state != BC250_DMA_CAPTURE_ACTIVE && state != BC250_DMA_CAPTURE_STOPPING) return STATUS_INVALID_DEVICE_STATE;
    status = Bc250DmaGateStop(&owner->Gate);
    if (status != STATUS_SUCCESS) {
        /* An already-linearized Stop may resume after full retirement. */
        LONG current = InterlockedCompareExchange(&owner->State, 0, 0);
        if (InterlockedCompareExchange(&owner->Gate.State, 0, 0) == BC250_DMA_GATE_CLOSED &&
            (current == BC250_DMA_CAPTURE_STOPPING || current == BC250_DMA_CAPTURE_DRAINED ||
             current == BC250_DMA_CAPTURE_RETIRED))
            return STATUS_SUCCESS;
        Bc250DmaCaptureFault(owner);
    }
    return status;
}
NTSTATUS Bc250DmaCaptureRetire(BC250_DMA_CAPTURE *owner)
{
    BC250_WIN_DMA zero;
    LONG state;
    NTSTATUS status = Bc250DmaCaptureGuard(owner);
    if (status != STATUS_SUCCESS) return status;
    if (owner->Signature != BC250_DMA_CAPTURE_SIGNATURE) return STATUS_INVALID_PARAMETER;
    if (InterlockedCompareExchangePointer(&owner->Gate.OwnerThread, NULL, NULL) == KeGetCurrentThread() ||
        InterlockedCompareExchangePointer(&owner->MetadataThread, NULL, NULL) == KeGetCurrentThread())
        return STATUS_DEVICE_BUSY;
    if (InterlockedCompareExchange(&owner->Coordinator, 1, 0)) return STATUS_DEVICE_BUSY;
    state = InterlockedCompareExchange(&owner->State, 0, 0);
    if (state != BC250_DMA_CAPTURE_ACTIVE && state != BC250_DMA_CAPTURE_STOPPING && state != BC250_DMA_CAPTURE_DRAINED) {
        InterlockedExchange(&owner->Coordinator, 0); return STATUS_INVALID_DEVICE_STATE;
    }
    status = Bc250DmaCaptureStop(owner);
    if (status == STATUS_SUCCESS && InterlockedCompareExchange(&owner->Gate.State, 0, 0) != BC250_DMA_GATE_CLOSED)
        status = Bc250DmaGateQuiesce(&owner->Gate);
    if (status != STATUS_SUCCESS || InterlockedCompareExchange(&owner->State, 0, 0) == BC250_DMA_CAPTURE_FAULT) {
        Bc250DmaCaptureFault(owner); InterlockedExchange(&owner->Coordinator, 0);
        return STATUS_INVALID_DEVICE_STATE;
    }
    InterlockedCompareExchange(&owner->State, BC250_DMA_CAPTURE_DRAINED, BC250_DMA_CAPTURE_STOPPING);
    status = Bc250DmaCaptureLock(owner);
    if (status != STATUS_SUCCESS) { InterlockedExchange(&owner->Coordinator, 0); return status; }
    /* A Drop/metadata failure could publish FAULT after the earlier check. */
    if (InterlockedCompareExchange(&owner->State, 0, 0) != BC250_DMA_CAPTURE_DRAINED)
        status = STATUS_INVALID_DEVICE_STATE;
    else status = Bc250DmaCaptureCheck(owner);
    if (status != STATUS_SUCCESS) Bc250DmaCaptureFault(owner);
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
        if (status != STATUS_SUCCESS) Bc250DmaCaptureFault(owner);
        else {
            owner->PinnedMdl = NULL; owner->PinnedList = NULL; owner->PinnedListBytes = 0;
            RtlZeroMemory(&owner->Request, sizeof(owner->Request));
            owner->RequestValid = owner->RequestWidth = 0U;
            InterlockedExchange(&owner->State, BC250_DMA_CAPTURE_RETIRED);
        }
    }
    Bc250DmaCaptureUnlock(owner);
    InterlockedExchange(&owner->Coordinator, 0);
    return status;
}

/* The request is owner-recorded by Acquire, never supplied by SG or inferred
 * from MaximumLength. Variable SG storage is trusted/pinned from successful
 * Native acquisition; Elements[] is NOT a fixed-capacity array in real WDK.
 * First pass validates everything, second pass infallibly copies numeric pages.
 * No big stack workspace, allocating DDI, SG/MDL pointer output, or GPU work.
 */
NTSTATUS Bc250DmaCaptureSnapshot(BC250_DMA_CAPTURE *owner,
    const BC250_DMA_CAPTURE_HANDLE *seed, const BC250_DMA_CAPTURE_REQUEST *expected,
    BC250_DMA_CAPTURE_SNAPSHOT *out)
{
    static const BC250_DMA_CAPTURE_SNAPSHOT zero = {0};
    BC250_DMA_GATE_TICKET ticket = {0};
    PSCATTER_GATHER_LIST sg = NULL;
    ULONGLONG total = 0, limit = 0;
    ULONG i, count = 0, pages = 0, emitted = 0;
    NTSTATUS status = Bc250DmaCaptureGuard(owner);
    if (status != STATUS_SUCCESS) return status;
    if (!Bc250DmaCaptureAway(owner, seed, sizeof(*seed)) ||
        !Bc250DmaCaptureAway(owner, expected, sizeof(*expected)) ||
        !Bc250DmaCaptureAway(owner, out, sizeof(*out)) ||
        !Bc250DmaCaptureDisjoint(seed, sizeof(*seed), out, sizeof(*out)) ||
        !Bc250DmaCaptureDisjoint(expected, sizeof(*expected), out, sizeof(*out)))
        return STATUS_INVALID_PARAMETER;
    status = Bc250DmaCaptureAdmit(owner, &ticket);
    if (status != STATUS_SUCCESS) return status;
    status = Bc250DmaCaptureLock(owner);
    if (status == STATUS_SUCCESS) {
        status = Bc250DmaCaptureCheck(owner);
        if (status != STATUS_SUCCESS) Bc250DmaCaptureFault(owner);
        else if (InterlockedCompareExchange(&owner->State, 0, 0) == BC250_DMA_CAPTURE_FAULT)
            status = STATUS_INVALID_DEVICE_STATE;
        else status = Bc250DmaCaptureMatch(owner, seed);
        if (status == STATUS_SUCCESS &&
            (!Bc250DmaCaptureBuffer(owner, expected, sizeof(*expected)) ||
             !Bc250DmaCaptureBuffer(owner, out, sizeof(*out)) ||
             owner->Records[seed->Slot].Snapshot ||
             RtlCompareMemory(out, &zero, sizeof(zero)) != sizeof(zero)))
            status = STATUS_INVALID_PARAMETER;
        if (status == STATUS_SUCCESS && (expected->Offset != owner->Request.Offset ||
            expected->Bytes != owner->Request.Bytes || expected->Direction != owner->Request.Direction))
            status = STATUS_INVALID_PARAMETER; /* Wrong consumer request; owner unchanged. */
        if (status == STATUS_SUCCESS && (owner->RequestValid != 1U ||
            owner->Native.State != BC250_WIN_DMA_HELD || owner->Native.Busy ||
            !owner->Native.RequestIssued || owner->Native.Cancelled ||
            owner->PinnedList != owner->Native.List || owner->PinnedMdl != owner->Native.HeldMdl ||
            owner->RequestWidth != owner->Native.AddressWidth || !owner->RequestWidth ||
            owner->RequestWidth > 48U || owner->Request.Bytes > owner->Native.MaximumLength ||
            owner->PinnedMdl->Next || !(owner->PinnedMdl->MdlFlags & MDL_PAGES_LOCKED) ||
            MmGetMdlByteOffset(owner->PinnedMdl) != 0 ||
            owner->Request.Offset >= MmGetMdlByteCount(owner->PinnedMdl) ||
            owner->Request.Bytes > MmGetMdlByteCount(owner->PinnedMdl) - owner->Request.Offset)) {
            Bc250DmaCaptureFault(owner); status = STATUS_INVALID_DEVICE_STATE;
        }
        if (status == STATUS_SUCCESS) {
            sg = owner->PinnedList; count = sg->NumberOfElements;
            if (!count || count > BC250_DMA_CAPTURE_MAX_PAGES ||
                owner->PinnedListBytes != FIELD_OFFSET(SCATTER_GATHER_LIST, Elements) +
                    (SIZE_T)count * sizeof(sg->Elements[0])) status = STATUS_INVALID_DEVICE_STATE;
            else {
                limit = (1ULL << owner->RequestWidth) - 1ULL;
                for (i = 0; i < count; ++i) {
                    ULONGLONG start = (ULONGLONG)sg->Elements[i].Address.QuadPart;
                    ULONG bytes = sg->Elements[i].Length;
                    if (!bytes || (bytes & 4095U) || (start & 4095ULL) || start > limit ||
                        (ULONGLONG)bytes - 1ULL > limit - start || total > owner->Request.Bytes ||
                        bytes > owner->Request.Bytes - total ||
                        (bytes / 4096U) > BC250_DMA_CAPTURE_MAX_PAGES - pages) {
                        status = STATUS_INVALID_DEVICE_STATE; break;
                    }
                    total += bytes; pages += bytes / 4096U;
                }
                if (status == STATUS_SUCCESS && total != owner->Request.Bytes)
                    status = STATUS_INVALID_DEVICE_STATE;
            }
            if (status != STATUS_SUCCESS) Bc250DmaCaptureFault(owner);
            else {
                /* New can fail only BEFORE mutation; output was fully zero. */
                status = Bc250DmaCaptureNew(owner, &out->Reference);
                if (status == STATUS_SUCCESS) {
                    owner->Records[out->Reference.Slot].Snapshot = out;
                    out->Self = out; out->Request = owner->Request; out->MaximumLast = limit;
                    out->PageCount = pages; out->ElementCount = count;
                    for (i = 0; i < count; ++i) {
                        ULONG page;
                        ULONGLONG start = (ULONGLONG)sg->Elements[i].Address.QuadPart;
                        for (page = 0; page < sg->Elements[i].Length / 4096U; ++page) {
                            BC250_AD_SPAN *destination = &out->Pages[emitted++];
                            destination->Domain = BC250_AD_DMA_LOGICAL;
                            destination->Start = start + (ULONGLONG)page * 4096ULL;
                            destination->Bytes = 4096ULL; destination->Last = destination->Start + 4095ULL;
                        }
                    }
                    out->State = BC250_DMA_CAPTURE_SNAPSHOT_LIVE;
                }
            }
        }
        Bc250DmaCaptureUnlock(owner);
    }
    /* Once published, any Finish error retains the FULL obligation/output. */
    return Bc250DmaCaptureFinish(owner, &ticket, status);
}
NTSTATUS Bc250DmaCaptureSnapshotRelease(BC250_DMA_CAPTURE *owner,
    BC250_DMA_CAPTURE_SNAPSHOT *snapshot)
{
    ULONG slot;
    NTSTATUS status = Bc250DmaCaptureGuard(owner);
    if (status != STATUS_SUCCESS) return status;
    if (owner->Signature != BC250_DMA_CAPTURE_SIGNATURE ||
        !Bc250DmaCaptureAway(owner, snapshot, sizeof(*snapshot))) return STATUS_INVALID_PARAMETER;
    status = Bc250DmaCaptureLock(owner);
    if (status != STATUS_SUCCESS) return status;
    status = Bc250DmaCaptureCheck(owner);
    if (status != STATUS_SUCCESS) Bc250DmaCaptureFault(owner);
    else if (!Bc250DmaCaptureStorage(owner, snapshot, sizeof(*snapshot)) ||
        snapshot->Self != snapshot || snapshot->State != BC250_DMA_CAPTURE_SNAPSHOT_LIVE ||
        snapshot->Reference.Owner != owner || snapshot->Reference.Slot >= BC250_DMA_CAPTURE_SLOTS ||
        !snapshot->Reference.Id) status = STATUS_INVALID_PARAMETER;
    else {
        slot = snapshot->Reference.Slot;
        if (!owner->Records[slot].Live || owner->Records[slot].Id != snapshot->Reference.Id ||
            owner->Records[slot].Snapshot != snapshot) status = STATUS_INVALID_DEVICE_STATE;
        else {
            RtlZeroMemory(&owner->Records[slot], sizeof(owner->Records[slot]));
            --owner->References;
            RtlZeroMemory(&snapshot->Reference, sizeof(snapshot->Reference));
            snapshot->State = BC250_DMA_CAPTURE_SNAPSHOT_RELEASED;
        }
    }
    Bc250DmaCaptureUnlock(owner);
    return status; /* No GateEnter, Native access, cancellation or cleanup. */
}
