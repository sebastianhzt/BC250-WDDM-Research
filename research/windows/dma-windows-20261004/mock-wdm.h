/* SPDX-License-Identifier: Apache-2.0
 * RAM fake types, NOT a WDK ABI implementation or Windows DMA emulator.
 */
#ifndef BC250_MOCK_DMA_WDM_H
#define BC250_MOCK_DMA_WDM_H
#include <stddef.h>
#include <stdint.h>
#define TRUE 1U
#define FALSE 0U
#define PASSIVE_LEVEL 0U
#define DEVICE_DESCRIPTION_VERSION3 3U
#define DMA_SYNCHRONOUS_CALLBACK 1U
#define DMA_TRANSFER_CONTEXT_SIZE_V1 128U
#define MDL_PAGES_LOCKED 2U
#define PCIBus 5U
#define STATUS_SUCCESS ((NTSTATUS)0)
#define STATUS_PENDING ((NTSTATUS)0x103)
#define STATUS_NOT_SUPPORTED ((NTSTATUS)0xC00000BB)
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000D)
#define STATUS_INVALID_DEVICE_STATE ((NTSTATUS)0xC0000184)
#define STATUS_DEVICE_BUSY ((NTSTATUS)0x80000011)
#define STATUS_INSUFFICIENT_RESOURCES ((NTSTATUS)0xC000009A)
#define STATUS_CANCELLED ((NTSTATUS)0xC0000120)
#define NT_SUCCESS(s) ((NTSTATUS)(s) >= 0)
#define FIELD_OFFSET(type, member) offsetof(type, member)
#define MmGetMdlByteCount(mdl) ((mdl)->ByteCount)
#define MmGetMdlByteOffset(mdl) ((mdl)->ByteOffset)
typedef uint32_t ULONG;
typedef uint64_t ULONGLONG;
typedef unsigned char BOOLEAN;
typedef int32_t NTSTATUS;
typedef uintptr_t ULONG_PTR;
typedef size_t SIZE_T;
typedef struct { int Fake; } DEVICE_OBJECT, *PDEVICE_OBJECT;
typedef struct { int64_t QuadPart; } PHYSICAL_ADDRESS;
typedef struct MDL { struct MDL *Next; unsigned short MdlFlags; ULONG ByteCount, ByteOffset; } MDL, *PMDL;
typedef struct {
    ULONG Version; BOOLEAN Master, ScatterGather, DemandMode, AutoInitialize;
    BOOLEAN Dma32BitAddresses, IgnoreCount, Reserved1, Dma64BitAddresses;
    ULONG InterfaceType, MaximumLength, DmaAddressWidth;
} DEVICE_DESCRIPTION;
typedef struct { PHYSICAL_ADDRESS Address; ULONG Length; ULONG_PTR Reserved; } SCATTER_GATHER_ELEMENT;
/* Fixed fake capacity permits invalid NumberOfElements tests without reads. */
typedef struct { ULONG NumberOfElements; ULONG_PTR Reserved; SCATTER_GATHER_ELEMENT Elements[64]; } SCATTER_GATHER_LIST, *PSCATTER_GATHER_LIST;
typedef enum { KeepObject, DeallocateObject, DeallocateObjectKeepRegisters } IO_ALLOCATION_ACTION;
typedef struct DMA_ADAPTER DMA_ADAPTER, *PDMA_ADAPTER;
typedef void (*PPUT_DMA_ADAPTER)(PDMA_ADAPTER);
typedef NTSTATUS (*PINITIALIZE_DMA_TRANSFER_CONTEXT)(PDMA_ADAPTER, void *);
typedef NTSTATUS (*PGET_SCATTER_GATHER_LIST_EX)(PDMA_ADAPTER, PDEVICE_OBJECT,
    void *, PMDL, ULONGLONG, ULONG, ULONG, void *, void *, BOOLEAN, void *, void *, PSCATTER_GATHER_LIST *);
typedef void (*PFREE_ADAPTER_OBJECT)(PDMA_ADAPTER, IO_ALLOCATION_ACTION);
typedef struct {
    ULONG Size;
    PPUT_DMA_ADAPTER PutDmaAdapter;
    PINITIALIZE_DMA_TRANSFER_CONTEXT InitializeDmaTransferContext;
    PGET_SCATTER_GATHER_LIST_EX GetScatterGatherListEx;
    PFREE_ADAPTER_OBJECT FreeAdapterObject;
} DMA_OPERATIONS, *PDMA_OPERATIONS;
struct DMA_ADAPTER { unsigned short Version, Size; PDMA_OPERATIONS DmaOperations; };
extern BOOLEAN Bc250MockExecutionAllowed;
unsigned char KeGetCurrentIrql(void);
PDMA_ADAPTER IoGetDmaAdapter(PDEVICE_OBJECT, DEVICE_DESCRIPTION *, ULONG *);
#endif
