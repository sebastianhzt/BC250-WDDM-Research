/* SPDX-License-Identifier: Apache-2.0 */
#define BC250_DMA_ADAPTER_MOCK 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bc250_dma_adapter.c" /* Test the ACTUAL candidate against fake APIs. */

BOOLEAN Bc250MockExecutionAllowed = TRUE;
static unsigned checks, irql, calls, gets, frees, adapterPuts, inits, live, mapRegisters;
static unsigned noAdapter, nullList, reenter, failureWithList;
static NTSTATUS initStatus, getStatus;
static DMA_OPERATIONS operations;
static DMA_ADAPTER adapter;
static DEVICE_OBJECT pdo;
static DEVICE_DESCRIPTION description;
static SCATTER_GATHER_LIST list;
static MDL mdl;
static BC250_WIN_DMA owner;
#define CHECK(expression) do { ++checks; if (!(expression)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expression); exit(1); } } while (0)

static void reentry(void)
{
    if (reenter) {
        CHECK(Bc250WinDmaClose(&owner) == STATUS_DEVICE_BUSY);
        CHECK(Bc250WinDmaRelease(&owner) == STATUS_DEVICE_BUSY);
        CHECK(Bc250WinDmaCancel(&owner) == STATUS_DEVICE_BUSY);
        CHECK(Bc250WinDmaAcquire(&owner, &mdl, 0, 4096, TRUE) == STATUS_DEVICE_BUSY);
        CHECK(Bc250WinDmaOpen(&owner, &pdo, &description) == STATUS_DEVICE_BUSY);
    }
}
unsigned char KeGetCurrentIrql(void) { ++calls; return (unsigned char)irql; }
static void put(PDMA_ADAPTER a)
{
    ++calls; ++adapterPuts; CHECK(a == &adapter); CHECK(!live); reentry();
}
static NTSTATUS initialize(PDMA_ADAPTER a, void *context)
{
    ++calls; ++inits; CHECK(a == &adapter); CHECK(context == owner.TransferContext);
    CHECK(!live); reentry(); return initStatus;
}
static NTSTATUS get(PDMA_ADAPTER a, PDEVICE_OBJECT device, void *context,
    PMDL m, ULONGLONG offset, ULONG bytes, ULONG flags, void *callback,
    void *callbackContext, BOOLEAN direction, void *completion, void *completionContext,
    PSCATTER_GATHER_LIST *out)
{
    ++calls; ++gets;
    CHECK(a == &adapter && device == &pdo && context == owner.TransferContext);
    CHECK(m == &mdl && offset == 0 && bytes >= 4096 && direction <= TRUE);
    CHECK(flags == DMA_SYNCHRONOUS_CALLBACK);
    CHECK(!callback && !callbackContext && !completion && !completionContext);
    CHECK(out == &owner.List && !*out && !live); reentry();
    if (getStatus == STATUS_SUCCESS) { live = 1; *out = nullList ? NULL : &list; }
    if (failureWithList) { live = 1; *out = &list; } /* Broken fake provider only. */
    return getStatus;
}
static void freeObject(PDMA_ADAPTER a, IO_ALLOCATION_ACTION action)
{
    ++calls; ++frees; CHECK(a == &adapter && action == DeallocateObject);
    CHECK(live == 1); reentry(); live = 0;
}
PDMA_ADAPTER IoGetDmaAdapter(PDEVICE_OBJECT device, DEVICE_DESCRIPTION *d, ULONG *registers)
{
    ++calls; CHECK(device == &pdo); CHECK(d->Version == DEVICE_DESCRIPTION_VERSION3);
    CHECK(irql == PASSIVE_LEVEL); reentry(); *registers = mapRegisters;
    return noAdapter ? NULL : &adapter;
}
static void reset(void)
{
    /* New fake fixture only; never resets a real live owner. */
    memset(&owner, 0, sizeof(owner)); memset(&description, 0, sizeof(description));
    memset(&mdl, 0, sizeof(mdl)); memset(&list, 0, sizeof(list));
    memset(&operations, 0, sizeof(operations)); memset(&adapter, 0, sizeof(adapter));
    irql = calls = gets = frees = adapterPuts = inits = live = noAdapter = nullList = reenter = 0;
    failureWithList = 0;
    Bc250MockExecutionAllowed = TRUE; mapRegisters = 64;
    initStatus = getStatus = STATUS_SUCCESS;
    operations.Size = sizeof(operations); operations.PutDmaAdapter = put;
    operations.InitializeDmaTransferContext = initialize;
    operations.GetScatterGatherListEx = get; operations.FreeAdapterObject = freeObject;
    adapter.Version = 1; adapter.DmaOperations = &operations;
    description.Version = DEVICE_DESCRIPTION_VERSION3; description.Master = TRUE;
    description.ScatterGather = TRUE; description.InterfaceType = PCIBus;
    description.DmaAddressWidth = 48; description.MaximumLength = BC250_WIN_DMA_MAX_BYTES;
    mdl.MdlFlags = MDL_PAGES_LOCKED; mdl.ByteCount = BC250_WIN_DMA_MAX_BYTES;
    list.NumberOfElements = 1; list.Elements[0].Address.QuadPart = 0x100000;
    list.Elements[0].Length = 4096;
}
static void openOwner(void) { CHECK(Bc250WinDmaOpen(&owner, &pdo, &description) == STATUS_SUCCESS); }
static void closeOwner(void) { CHECK(Bc250WinDmaClose(&owner) == STATUS_SUCCESS); CHECK(adapterPuts == 1 && !live); }

int main(void)
{
    unsigned i, saved;
    BC250_WIN_DMA snapshot;
    reset(); Bc250MockExecutionAllowed = FALSE;
    CHECK(Bc250WinDmaOpen(NULL, NULL, NULL) == STATUS_NOT_SUPPORTED);
    CHECK(Bc250WinDmaAcquire(NULL, NULL, 0, 0, FALSE) == STATUS_NOT_SUPPORTED);
    CHECK(Bc250WinDmaCancel(NULL) == STATUS_NOT_SUPPORTED);
    CHECK(Bc250WinDmaRelease(NULL) == STATUS_NOT_SUPPORTED);
    CHECK(Bc250WinDmaClose(NULL) == STATUS_NOT_SUPPORTED); CHECK(calls == 0);
    reset(); irql = 2;
    CHECK(Bc250WinDmaOpen(&owner, &pdo, &description) == STATUS_INVALID_DEVICE_STATE);
    CHECK(gets == 0 && adapterPuts == 0 && owner.State == 0);
    reset(); noAdapter = 1;
    CHECK(Bc250WinDmaOpen(&owner, &pdo, &description) == STATUS_INSUFFICIENT_RESOURCES);
    CHECK(owner.State == 0 && adapterPuts == 0);

    for (i = 0; i < 6; ++i) {
        reset();
        if (i == 0) operations.Size = (ULONG)FIELD_OFFSET(DMA_OPERATIONS, FreeAdapterObject);
        if (i == 1) operations.InitializeDmaTransferContext = NULL;
        if (i == 2) operations.GetScatterGatherListEx = NULL;
        if (i == 3) operations.FreeAdapterObject = NULL;
        if (i == 4) mapRegisters = 63;
        if (i == 5) mapRegisters = 0;
        CHECK(Bc250WinDmaOpen(&owner, &pdo, &description) == STATUS_NOT_SUPPORTED);
        CHECK(adapterPuts == 1 && !owner.Adapter && owner.State == 0 && !frees);
    }
    reset(); operations.PutDmaAdapter = NULL;
    CHECK(Bc250WinDmaOpen(&owner, &pdo, &description) == STATUS_NOT_SUPPORTED);
    CHECK(owner.State == BC250_WIN_DMA_QUARANTINED && owner.Adapter == &adapter && adapterPuts == 0);
    CHECK(Bc250WinDmaClose(&owner) == STATUS_INVALID_DEVICE_STATE);
    reset(); adapter.DmaOperations = NULL;
    CHECK(Bc250WinDmaOpen(&owner, &pdo, &description) == STATUS_NOT_SUPPORTED);
    CHECK(owner.State == BC250_WIN_DMA_QUARANTINED && adapterPuts == 0);
    reset(); operations.Size = (ULONG)FIELD_OFFSET(DMA_OPERATIONS, PutDmaAdapter);
    CHECK(Bc250WinDmaOpen(&owner, &pdo, &description) == STATUS_NOT_SUPPORTED);
    CHECK(owner.State == BC250_WIN_DMA_QUARANTINED && adapterPuts == 0);

    for (i = 0; i < 8; ++i) {
        reset();
        if (i == 0) description.Version = 2;
        if (i == 1) description.Master = FALSE;
        if (i == 2) description.ScatterGather = FALSE;
        if (i == 3) description.DmaAddressWidth = 0;
        if (i == 4) description.DmaAddressWidth = 49;
        if (i == 5) description.Reserved1 = TRUE;
        if (i == 6) description.MaximumLength = 4097;
        if (i == 7) description.MaximumLength = BC250_WIN_DMA_MAX_BYTES + 4096;
        CHECK(Bc250WinDmaOpen(&owner, &pdo, &description) == STATUS_INVALID_PARAMETER);
        CHECK(calls == 1 && owner.State == 0);
    }
    reset(); CHECK(Bc250WinDmaOpen(&owner, &pdo, (DEVICE_DESCRIPTION *)&owner) == STATUS_INVALID_PARAMETER);
    reset(); openOwner(); snapshot = owner;
    CHECK(Bc250WinDmaOpen(&owner, &pdo, &description) == STATUS_INVALID_DEVICE_STATE);
    CHECK(!memcmp(&snapshot, &owner, sizeof(owner)));
    for (i = 0; i < 7; ++i) {
        saved = gets;
        if (i == 0) mdl.Next = &mdl;
        if (i == 1) mdl.MdlFlags = 0;
        if (i == 2) mdl.ByteOffset = 1;
        if (i == 3) mdl.ByteCount = 4095;
        CHECK(Bc250WinDmaAcquire(&owner, &mdl, i == 4 ? 1 : 0,
            i == 5 ? 0 : 4096, (BOOLEAN)(i == 6 ? 2 : TRUE)) == STATUS_INVALID_PARAMETER);
        CHECK(gets == saved && owner.State == BC250_WIN_DMA_OPEN);
        mdl.Next = NULL; mdl.MdlFlags = MDL_PAGES_LOCKED; mdl.ByteOffset = 0;
        mdl.ByteCount = BC250_WIN_DMA_MAX_BYTES;
    }
    CHECK(Bc250WinDmaAcquire(&owner, (PMDL)&owner, 0, 4096, TRUE) == STATUS_INVALID_PARAMETER);
    CHECK(Bc250WinDmaAcquire(&owner, &mdl, 0xFFFFFFFFFFFFF000ULL, 4096, TRUE) == STATUS_INVALID_PARAMETER);
    initStatus = STATUS_INSUFFICIENT_RESOURCES;
    CHECK(Bc250WinDmaAcquire(&owner, &mdl, 0, 4096, TRUE) == initStatus); CHECK(gets == 0 && frees == 0);
    initStatus = STATUS_PENDING;
    CHECK(Bc250WinDmaAcquire(&owner, &mdl, 0, 4096, TRUE) == STATUS_INVALID_DEVICE_STATE); CHECK(gets == 0);
    initStatus = STATUS_SUCCESS; getStatus = STATUS_INSUFFICIENT_RESOURCES;
    CHECK(Bc250WinDmaAcquire(&owner, &mdl, 0, 4096, TRUE) == getStatus); CHECK(frees == 0);
    CHECK(Bc250WinDmaAcquire(&owner, &mdl, 0, 4096, TRUE) == STATUS_INVALID_DEVICE_STATE);
    CHECK(gets == 1); closeOwner(); reset(); openOwner();
    CHECK(Bc250WinDmaAcquire(&owner, &mdl, 0, 4096, TRUE) == STATUS_SUCCESS);
    CHECK(Bc250WinDmaClose(&owner) == STATUS_INVALID_DEVICE_STATE && adapterPuts == 0);
    CHECK(Bc250WinDmaAcquire(&owner, &mdl, 0, 4096, TRUE) == STATUS_INVALID_DEVICE_STATE);
    CHECK(Bc250WinDmaRelease(&owner) == STATUS_SUCCESS && frees == 1);
    CHECK(Bc250WinDmaRelease(&owner) == STATUS_INVALID_DEVICE_STATE && frees == 1);
    CHECK(Bc250WinDmaAcquire(&owner, &mdl, 0, 4096, TRUE) == STATUS_INVALID_DEVICE_STATE && gets == 1);
    closeOwner(); CHECK(Bc250WinDmaClose(&owner) == STATUS_INVALID_DEVICE_STATE && adapterPuts == 1);

    for (i = 0; i < 8; ++i) {
        reset(); openOwner();
        if (i == 0) nullList = 1;
        if (i == 1) list.NumberOfElements = 0;
        if (i == 2) list.NumberOfElements = 65;
        if (i == 3) list.Elements[0].Length = 0;
        if (i == 4) list.Elements[0].Length = 4097;
        if (i == 5) list.Elements[0].Address.QuadPart = -4096;
        if (i == 6) list.Elements[0].Address.QuadPart = (1LL << 48);
        if (i == 7) list.Elements[0].Address.QuadPart = 1;
        CHECK(Bc250WinDmaAcquire(&owner, &mdl, 0, 4096, TRUE) == STATUS_INVALID_PARAMETER);
        CHECK(frees == 1 && !live && owner.State == BC250_WIN_DMA_OPEN); closeOwner();
    }
    reset(); openOwner(); list.Elements[0].Length = 8192;
    list.Elements[0].Address.QuadPart = (1LL << 48) - 4096;
    CHECK(Bc250WinDmaAcquire(&owner, &mdl, 0, 8192, TRUE) == STATUS_INVALID_PARAMETER); closeOwner();
    reset(); openOwner(); getStatus = STATUS_PENDING;
    CHECK(Bc250WinDmaAcquire(&owner, &mdl, 0, 4096, TRUE) == STATUS_INVALID_DEVICE_STATE);
    CHECK(owner.State == BC250_WIN_DMA_QUARANTINED && !frees && !adapterPuts);
    CHECK(Bc250WinDmaClose(&owner) == STATUS_INVALID_DEVICE_STATE);
    reset(); openOwner(); failureWithList = 1; getStatus = STATUS_INSUFFICIENT_RESOURCES;
    CHECK(Bc250WinDmaAcquire(&owner, &mdl, 0, 4096, TRUE) == STATUS_INVALID_DEVICE_STATE);
    CHECK(owner.State == BC250_WIN_DMA_QUARANTINED && !frees && !adapterPuts && live);
    CHECK(Bc250WinDmaCancel(&owner) == STATUS_INVALID_DEVICE_STATE);
    /* Quarantined fake fixture discarded here; NOT a real cleanup mechanism. */
    reset(); description.DmaAddressWidth = 32; openOwner();
    list.Elements[0].Address.QuadPart = (1LL << 32) - 4096;
    CHECK(Bc250WinDmaAcquire(&owner, &mdl, 0, 4096, TRUE) == STATUS_SUCCESS);
    CHECK(Bc250WinDmaRelease(&owner) == STATUS_SUCCESS); closeOwner();
    reset(); description.DmaAddressWidth = 32; openOwner();
    list.Elements[0].Address.QuadPart = (1LL << 32) - 4096;
    list.Elements[0].Length = 8192;
    CHECK(Bc250WinDmaAcquire(&owner, &mdl, 0, 8192, TRUE) == STATUS_INVALID_PARAMETER); closeOwner();
    reset(); openOwner(); snapshot = owner; irql = 1;
    CHECK(Bc250WinDmaAcquire(&owner, &mdl, 0, 4096, TRUE) == STATUS_INVALID_DEVICE_STATE);
    CHECK(Bc250WinDmaRelease(&owner) == STATUS_INVALID_DEVICE_STATE);
    CHECK(Bc250WinDmaCancel(&owner) == STATUS_INVALID_DEVICE_STATE);
    CHECK(Bc250WinDmaClose(&owner) == STATUS_INVALID_DEVICE_STATE);
    CHECK(!memcmp(&snapshot, &owner, sizeof(owner)) && !gets && !adapterPuts);
    irql = 0; closeOwner();

    for (i = 1; i <= 64; ++i) {
        unsigned n;
        reset(); reenter = 1; openOwner(); list.NumberOfElements = i;
        /* Repeated DMA pages are valid ordered spans, not deduplicated. */
        for (n = 0; n < i; ++n) { list.Elements[n].Address.QuadPart = 0; list.Elements[n].Length = 4096; }
        CHECK(Bc250WinDmaAcquire(&owner, &mdl, 0, i * 4096, FALSE) == STATUS_SUCCESS);
        CHECK(live && owner.HeldMdl == &mdl);
        if (i & 1U) {
            CHECK(Bc250WinDmaCancel(&owner) == STATUS_SUCCESS);
            CHECK(Bc250WinDmaCancel(&owner) == STATUS_SUCCESS && frees == 1);
            CHECK(Bc250WinDmaAcquire(&owner, &mdl, 0, 4096, FALSE) == STATUS_CANCELLED);
        } else CHECK(Bc250WinDmaRelease(&owner) == STATUS_SUCCESS);
        CHECK(frees == 1 && !live); closeOwner();
    }
    reset(); openOwner(); CHECK(Bc250WinDmaCancel(&owner) == STATUS_SUCCESS);
    CHECK(Bc250WinDmaAcquire(&owner, &mdl, 0, 4096, TRUE) == STATUS_CANCELLED);
    CHECK(gets == 0 && inits == 0 && frees == 0); closeOwner();
    printf("PASS: %u checks, actual candidate with RAM fakes; NO Windows DMA/GPU execution\n", checks);
    return 0;
}
