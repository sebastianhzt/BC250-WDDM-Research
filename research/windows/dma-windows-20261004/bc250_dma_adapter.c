/* SPDX-License-Identifier: Apache-2.0
 * Standalone /c candidate. NOT linked, loaded, installed or hardware tested.
 * Synchronous bus-master allocation only, no callback and NO DMA submission.
 */
#include "bc250_dma_adapter.h"

static BOOLEAN Bc250WinDmaPolicy(void)
{
#ifdef BC250_DMA_ADAPTER_MOCK
    return Bc250MockExecutionAllowed; /* fake platform ONLY */
#else
    return FALSE; /* Production execution gate: permanently closed. */
#endif
}

static NTSTATUS Bc250WinDmaGuard(BC250_WIN_DMA *owner)
{
    if (!Bc250WinDmaPolicy()) return STATUS_NOT_SUPPORTED;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) return STATUS_INVALID_DEVICE_STATE;
    if (!owner) return STATUS_INVALID_PARAMETER;
    if (owner->Busy) return STATUS_DEVICE_BUSY;
    return STATUS_SUCCESS;
}

static BOOLEAN Bc250WinDmaAway(BC250_WIN_DMA *owner, const void *pointer, SIZE_T bytes)
{
    ULONG_PTR a = (ULONG_PTR)owner, b = (ULONG_PTR)pointer;
    ULONG_PTR maximum = ~(ULONG_PTR)0;
    if (!pointer || a > maximum - (sizeof(*owner) - 1U) ||
        !bytes || b > maximum - (bytes - 1U)) return FALSE;
    return a + sizeof(*owner) - 1U < b || b + bytes - 1U < a;
}

NTSTATUS Bc250WinDmaOpen(BC250_WIN_DMA *owner, PDEVICE_OBJECT pdo,
    const DEVICE_DESCRIPTION *description)
{
    DEVICE_DESCRIPTION copy;
    PDMA_ADAPTER adapter;
    PDMA_OPERATIONS operations;
    ULONG registers = 0;
    NTSTATUS status = Bc250WinDmaGuard(owner);
    if (!NT_SUCCESS(status)) return status;
    if (owner->State || owner->Adapter || owner->Put || owner->List || owner->Cancelled ||
        owner->RequestIssued)
        return STATUS_INVALID_DEVICE_STATE;
    if (!pdo || !Bc250WinDmaAway(owner, description, sizeof(*description)))
        return STATUS_INVALID_PARAMETER;
    copy = *description;
    /* Capabilities are caller-supplied, NOT inferred from Linux or UMA size.
     * Caller must zero-init description and establish actual PnP capabilities.
     * This candidate deliberately accepts only a restricted PCI bus-master.
     */
    if (copy.Version != DEVICE_DESCRIPTION_VERSION3 || copy.Master != TRUE ||
        copy.ScatterGather != TRUE || copy.InterfaceType != PCIBus ||
        copy.Reserved1 || copy.DemandMode || copy.AutoInitialize || copy.IgnoreCount ||
        !copy.DmaAddressWidth || copy.DmaAddressWidth > 48U ||
        !copy.MaximumLength || copy.MaximumLength > BC250_WIN_DMA_MAX_BYTES ||
        (copy.MaximumLength & 4095U)) return STATUS_INVALID_PARAMETER;
    owner->Busy = 1U; owner->State = BC250_WIN_DMA_OPENING;
    adapter = IoGetDmaAdapter(pdo, &copy, &registers);
    if (!adapter) {
        owner->State = 0; owner->Busy = 0; return STATUS_INSUFFICIENT_RESOURCES;
    }
    operations = adapter->DmaOperations;
    /* Adapter->Version is always 1, including a version-3 operations table.
     * An invalid provider without the basic Put contract cannot be repaired
     * safely here: retain/quarantine the handle rather than guess cleanup.
     */
    if (!operations || operations->Size < FIELD_OFFSET(DMA_OPERATIONS, PutDmaAdapter) +
        sizeof(operations->PutDmaAdapter) || !operations->PutDmaAdapter) {
        owner->Adapter = adapter; owner->State = BC250_WIN_DMA_QUARANTINED;
        owner->Busy = 0; return STATUS_NOT_SUPPORTED;
    }
    owner->Put = operations->PutDmaAdapter;
    if (operations->Size < FIELD_OFFSET(DMA_OPERATIONS, FreeAdapterObject) +
        sizeof(operations->FreeAdapterObject) || !operations->InitializeDmaTransferContext ||
        !operations->GetScatterGatherListEx || !operations->FreeAdapterObject ||
        registers < copy.MaximumLength / 4096U) {
        owner->Put(adapter); owner->Put = NULL;
        owner->State = 0; owner->Busy = 0; return STATUS_NOT_SUPPORTED;
    }
    owner->Adapter = adapter; owner->Pdo = pdo;
    owner->Initialize = operations->InitializeDmaTransferContext;
    owner->Get = operations->GetScatterGatherListEx; owner->Free = operations->FreeAdapterObject;
    owner->MaximumLength = copy.MaximumLength; owner->AddressWidth = copy.DmaAddressWidth;
    owner->State = BC250_WIN_DMA_OPEN; owner->Busy = 0;
    return STATUS_SUCCESS;
}

static BOOLEAN Bc250WinDmaListValid(BC250_WIN_DMA *owner, ULONG bytes)
{
    ULONG i;
    ULONGLONG total = 0, limit = (1ULL << owner->AddressWidth) - 1ULL;
    PSCATTER_GATHER_LIST list = owner->List;
    if (!list || !list->NumberOfElements || list->NumberOfElements > 64U) return FALSE;
    /* Trusted OS-owned variable-size list, stable until FreeAdapterObject.
     * Ordered DMA-logical spans; never CPU PA, MC, FB or GPU-VA addresses.
     */
    for (i = 0; i < list->NumberOfElements; ++i) {
        ULONGLONG address = (ULONGLONG)list->Elements[i].Address.QuadPart;
        ULONG length = list->Elements[i].Length;
        if (!length || (length & 4095U) || (address & 4095ULL) ||
            address > limit || (ULONGLONG)length - 1ULL > limit - address ||
            total > bytes || length > bytes - total) return FALSE;
        total += length;
    }
    return total == bytes;
}

static void Bc250WinDmaDropHeld(BC250_WIN_DMA *owner)
{
    /* Busy must remain set through the provider's void cleanup operation.
     * Safe only because this candidate never exposes/submits a GPU address.
     */
    owner->Free(owner->Adapter, DeallocateObject);
    owner->List = NULL; owner->HeldMdl = NULL; owner->State = BC250_WIN_DMA_OPEN;
}

NTSTATUS Bc250WinDmaAcquire(BC250_WIN_DMA *owner, PMDL mdl,
    ULONGLONG offset, ULONG bytes, BOOLEAN writeToDevice)
{
    NTSTATUS status = Bc250WinDmaGuard(owner);
    if (!NT_SUCCESS(status)) return status;
    if (owner->State != BC250_WIN_DMA_OPEN) return STATUS_INVALID_DEVICE_STATE;
    if (owner->Cancelled) return STATUS_CANCELLED;
    if (owner->RequestIssued) return STATUS_INVALID_DEVICE_STATE;
    if (!Bc250WinDmaAway(owner, mdl, sizeof(*mdl)) || mdl->Next ||
        !(mdl->MdlFlags & MDL_PAGES_LOCKED) || MmGetMdlByteOffset(mdl) != 0 ||
        (offset & 4095ULL) || !bytes || (bytes & 4095U) ||
        bytes > owner->MaximumLength || writeToDevice > TRUE ||
        offset >= MmGetMdlByteCount(mdl) || bytes > MmGetMdlByteCount(mdl) - offset)
        return STATUS_INVALID_PARAMETER;
    owner->Busy = 1U;
    status = owner->Initialize(owner->Adapter, owner->TransferContext);
    if (status != STATUS_SUCCESS) {
        owner->Busy = 0;
        return NT_SUCCESS(status) ? STATUS_INVALID_DEVICE_STATE : status;
    }
    owner->List = NULL;
    owner->RequestIssued = 1U; /* This context is never offered for a second request. */
    status = owner->Get(owner->Adapter, owner->Pdo, owner->TransferContext, mdl,
        offset, bytes, DMA_SYNCHRONOUS_CALLBACK, NULL, NULL, writeToDevice,
        NULL, NULL, &owner->List);
    if (status != STATUS_SUCCESS) {
        /* A synchronous provider must never leave a pending allocation.
         * Unexpected positive status / failure with a list: quarantine, do
         * NOT infer no callback/no resource or attempt unsafe cleanup.
         */
        if (NT_SUCCESS(status) || owner->List) {
            owner->HeldMdl = mdl; owner->State = BC250_WIN_DMA_QUARANTINED;
            status = STATUS_INVALID_DEVICE_STATE;
        }
        owner->Busy = 0; return status;
    }
    owner->HeldMdl = mdl; owner->State = BC250_WIN_DMA_HELD;
    if (!Bc250WinDmaListValid(owner, bytes)) {
        Bc250WinDmaDropHeld(owner); owner->Busy = 0; return STATUS_INVALID_PARAMETER;
    }
    owner->Busy = 0; return STATUS_SUCCESS;
}

NTSTATUS Bc250WinDmaRelease(BC250_WIN_DMA *owner)
{
    NTSTATUS status = Bc250WinDmaGuard(owner);
    if (!NT_SUCCESS(status)) return status;
    if (owner->State != BC250_WIN_DMA_HELD) return STATUS_INVALID_DEVICE_STATE;
    owner->Busy = 1U; Bc250WinDmaDropHeld(owner); owner->Busy = 0;
    return STATUS_SUCCESS;
}

NTSTATUS Bc250WinDmaCancel(BC250_WIN_DMA *owner)
{
    NTSTATUS status = Bc250WinDmaGuard(owner);
    if (!NT_SUCCESS(status)) return status;
    if (owner->State != BC250_WIN_DMA_OPEN && owner->State != BC250_WIN_DMA_HELD)
        return STATUS_INVALID_DEVICE_STATE;
    /* Serialized cancellation before acquisition or after its synchronous
     * return. NOT asynchronous CancelAdapterChannel or GPU job cancellation.
     */
    owner->Busy = 1U; owner->Cancelled = 1U;
    if (owner->State == BC250_WIN_DMA_HELD) Bc250WinDmaDropHeld(owner);
    owner->Busy = 0; return STATUS_SUCCESS;
}

NTSTATUS Bc250WinDmaClose(BC250_WIN_DMA *owner)
{
    NTSTATUS status = Bc250WinDmaGuard(owner);
    if (!NT_SUCCESS(status)) return status;
    if (owner->State != BC250_WIN_DMA_OPEN) return STATUS_INVALID_DEVICE_STATE;
    owner->Busy = 1U; owner->Put(owner->Adapter);
    owner->Adapter = NULL; owner->Put = NULL; owner->Pdo = NULL;
    owner->Initialize = NULL; owner->Get = NULL; owner->Free = NULL;
    owner->State = BC250_WIN_DMA_CLOSED; owner->Busy = 0;
    return STATUS_SUCCESS;
}
