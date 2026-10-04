/* SPDX-License-Identifier: Apache-2.0
 * Standalone composition /c only, permanently disabled in production.
 */
#include "bc250_dma_resource.h"
#ifdef BC250_DMA_RESOURCE_MOCK
extern BOOLEAN Bc250MockResourceExecutionAllowed;
#endif
static BOOLEAN Bc250DmaResourcePolicy(void)
{
#ifdef BC250_DMA_RESOURCE_MOCK
    return Bc250MockResourceExecutionAllowed;
#else
    return FALSE;
#endif
}
static NTSTATUS Bc250DmaResourceGuard(BC250_DMA_RESOURCE *resource)
{
    if (!Bc250DmaResourcePolicy()) return STATUS_NOT_SUPPORTED;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) return STATUS_INVALID_DEVICE_STATE;
    if (!resource) return STATUS_INVALID_PARAMETER;
    return STATUS_SUCCESS;
}
static BOOLEAN Bc250DmaResourceAway(BC250_DMA_RESOURCE *resource, const void *buffer, SIZE_T bytes)
{
    ULONG_PTR a = (ULONG_PTR)resource, b = (ULONG_PTR)buffer, maximum = ~(ULONG_PTR)0;
    if (!buffer || !bytes || a > maximum - (sizeof(*resource) - 1U) ||
        b > maximum - (bytes - 1U)) return FALSE;
    return a + sizeof(*resource) - 1U < b || b + bytes - 1U < a;
}
static NTSTATUS Bc250DmaResourceAdmit(BC250_DMA_RESOURCE *resource, BC250_DMA_GATE_TICKET *ticket)
{
    NTSTATUS status;
    if (resource->Signature != BC250_DMA_RESOURCE_SIGNATURE) return STATUS_INVALID_PARAMETER;
    if (InterlockedCompareExchange(&resource->Retirement, 0, 0) != BC250_DMA_RESOURCE_ACTIVE)
        return STATUS_CANCELLED;
    status = Bc250DmaGateEnter(&resource->Gate, ticket);
    if (status != STATUS_SUCCESS &&
        InterlockedCompareExchange(&resource->Gate.State, 0, 0) == BC250_DMA_GATE_FAULT)
        InterlockedExchange(&resource->Retirement, BC250_DMA_RESOURCE_FAULT);
    return status;
}
static NTSTATUS Bc250DmaResourceFinish(BC250_DMA_RESOURCE *resource,
    BC250_DMA_GATE_TICKET *ticket, NTSTATUS status)
{
    NTSTATUS leave;
    if (resource->Native.State == BC250_WIN_DMA_QUARANTINED) {
        InterlockedExchange(&resource->Retirement, BC250_DMA_RESOURCE_FAULT);
        Bc250DmaGateStop(&resource->Gate);
        status = STATUS_INVALID_DEVICE_STATE;
    }
    /* Publish native failure while still protected, before a waiting retire
     * coordinator can inspect or clean Native. No bundle access after Leave
     * succeeds; external lifetime still protects the full outer API return.
     */
    leave = Bc250DmaGateLeave(&resource->Gate, ticket);
    if (leave != STATUS_SUCCESS) {
        /* Failed Leave keeps the reference/lock: never infer safe cleanup. */
        InterlockedExchange(&resource->Retirement, BC250_DMA_RESOURCE_FAULT);
        return leave;
    }
    return status;
}
NTSTATUS Bc250DmaResourceInit(BC250_DMA_RESOURCE *resource, PDEVICE_OBJECT pdo,
    const DEVICE_DESCRIPTION *description)
{
    BC250_DMA_RESOURCE zero;
    BC250_DMA_GATE_TICKET ticket = {0};
    NTSTATUS status = Bc250DmaResourceGuard(resource);
    if (status != STATUS_SUCCESS) return status;
    RtlZeroMemory(&zero, sizeof(zero));
    if (RtlCompareMemory(resource, &zero, sizeof(zero)) != sizeof(zero))
        return STATUS_INVALID_DEVICE_STATE;
    if (!Bc250DmaResourceAway(resource, pdo, sizeof(*pdo)) ||
        !Bc250DmaResourceAway(resource, description, sizeof(*description)))
        return STATUS_INVALID_PARAMETER;
    status = Bc250DmaGateInit(&resource->Gate);
    if (status != STATUS_SUCCESS) return status;
    resource->Signature = BC250_DMA_RESOURCE_SIGNATURE;
    status = Bc250DmaResourceAdmit(resource, &ticket);
    if (status != STATUS_SUCCESS) return status;
    status = Bc250WinDmaOpen(&resource->Native, pdo, description);
    return Bc250DmaResourceFinish(resource, &ticket, status);
}
NTSTATUS Bc250DmaResourceAcquire(BC250_DMA_RESOURCE *resource, PMDL mdl,
    ULONGLONG offset, ULONG bytes, BOOLEAN direction)
{
    BC250_DMA_GATE_TICKET ticket = {0};
    NTSTATUS status = Bc250DmaResourceGuard(resource);
    if (status != STATUS_SUCCESS) return status;
    if (!Bc250DmaResourceAway(resource, mdl, sizeof(*mdl))) return STATUS_INVALID_PARAMETER;
    status = Bc250DmaResourceAdmit(resource, &ticket);
    if (status != STATUS_SUCCESS) return status;
    status = Bc250WinDmaAcquire(&resource->Native, mdl, offset, bytes, direction);
    return Bc250DmaResourceFinish(resource, &ticket, status);
}
NTSTATUS Bc250DmaResourceRelease(BC250_DMA_RESOURCE *resource)
{
    BC250_DMA_GATE_TICKET ticket = {0};
    NTSTATUS status = Bc250DmaResourceGuard(resource);
    if (status != STATUS_SUCCESS) return status;
    status = Bc250DmaResourceAdmit(resource, &ticket);
    if (status != STATUS_SUCCESS) return status;
    status = Bc250WinDmaRelease(&resource->Native);
    return Bc250DmaResourceFinish(resource, &ticket, status);
}
NTSTATUS Bc250DmaResourceStop(BC250_DMA_RESOURCE *resource)
{
    NTSTATUS status = Bc250DmaResourceGuard(resource);
    if (status != STATUS_SUCCESS) return status;
    if (resource->Signature != BC250_DMA_RESOURCE_SIGNATURE) return STATUS_INVALID_PARAMETER;
    return Bc250DmaGateStop(&resource->Gate); /* No Native access or cleanup. */
}
NTSTATUS Bc250DmaResourceRetire(BC250_DMA_RESOURCE *resource)
{
    BC250_WIN_DMA zero;
    LONG retirement;
    NTSTATUS status = Bc250DmaResourceGuard(resource);
    if (status != STATUS_SUCCESS) return status;
    if (resource->Signature != BC250_DMA_RESOURCE_SIGNATURE) return STATUS_INVALID_PARAMETER;
    if (InterlockedCompareExchangePointer(&resource->Gate.OwnerThread, NULL, NULL) == KeGetCurrentThread())
        return STATUS_DEVICE_BUSY;
    retirement = InterlockedCompareExchange(&resource->Retirement, BC250_DMA_RESOURCE_RETIRING,
        BC250_DMA_RESOURCE_ACTIVE);
    if (retirement == BC250_DMA_RESOURCE_RETIRING) return STATUS_DEVICE_BUSY;
    if (retirement != BC250_DMA_RESOURCE_ACTIVE) return STATUS_INVALID_DEVICE_STATE;
    status = Bc250DmaGateStop(&resource->Gate);
    if (status == STATUS_SUCCESS) status = Bc250DmaGateQuiesce(&resource->Gate);
    if (status != STATUS_SUCCESS) {
        /* May retain a reference/unknown mutex. Poison, do not reset/retry. */
        InterlockedExchange(&resource->Retirement, BC250_DMA_RESOURCE_FAULT);
        return status;
    }
    /* Native FAULT can be published by the call we just waited for. */
    if (InterlockedCompareExchange(&resource->Retirement, 0, 0) == BC250_DMA_RESOURCE_FAULT ||
        resource->Native.State == BC250_WIN_DMA_QUARANTINED) {
        InterlockedExchange(&resource->Retirement, BC250_DMA_RESOURCE_FAULT);
        return STATUS_INVALID_DEVICE_STATE;
    }
    RtlZeroMemory(&zero, sizeof(zero));
    if (RtlCompareMemory(&resource->Native, &zero, sizeof(zero)) != sizeof(zero)) {
        if (resource->Native.State != BC250_WIN_DMA_OPEN && resource->Native.State != BC250_WIN_DMA_HELD) {
            InterlockedExchange(&resource->Retirement, BC250_DMA_RESOURCE_FAULT);
            return STATUS_INVALID_DEVICE_STATE;
        }
        /* No exported addresses/consumers exist in this standalone bundle. */
        status = Bc250WinDmaCancel(&resource->Native);
        if (status == STATUS_SUCCESS) status = Bc250WinDmaClose(&resource->Native);
        if (status != STATUS_SUCCESS) {
            InterlockedExchange(&resource->Retirement, BC250_DMA_RESOURCE_FAULT);
            return status;
        }
    } /* Exact all-zero Native: failed Open with proven no handle. */
    InterlockedExchange(&resource->Retirement, BC250_DMA_RESOURCE_RETIRED);
    return STATUS_SUCCESS;
}
