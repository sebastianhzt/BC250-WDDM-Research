/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BC250_PCI_MOCK_WDM_H
#define BC250_PCI_MOCK_WDM_H
#include <stdint.h>
#include <string.h>
typedef uint8_t UCHAR, BOOLEAN;
typedef uint16_t USHORT;
typedef uint32_t ULONG;
typedef uint64_t ULONGLONG;
typedef int32_t NTSTATUS;
typedef void *PVOID;
typedef void VOID;
#define TRUE 1
#define FALSE 0
#define STATUS_SUCCESS ((NTSTATUS)0)
#define STATUS_NOT_SUPPORTED ((NTSTATUS)0xC00000BBU)
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000DU)
#define STATUS_DEVICE_NOT_READY ((NTSTATUS)0xC00000A3U)
#define STATUS_DEVICE_BUSY ((NTSTATUS)0x80000011U)
#define STATUS_DATA_ERROR ((NTSTATUS)0xC000003EU)
#define PCI_WHICHSPACE_CONFIG 0
#define RtlZeroMemory(p,n) memset(p,0,n)
#define RtlCopyMemory(p,s,n) memcpy(p,s,n)
/* Reduced RAM ABI only. Independent WDK /c checks real declarations. */
typedef struct _BUS_INTERFACE_STANDARD {
    USHORT Size, Version;
    PVOID Context;
    VOID (*InterfaceReference)(PVOID);
    VOID (*InterfaceDereference)(PVOID);
    BOOLEAN (*TranslateBusAddress)(PVOID,ULONGLONG,ULONG *,ULONGLONG *);
    PVOID (*GetDmaAdapter)(PVOID,PVOID,ULONG *);
    ULONG (*SetBusData)(PVOID,ULONG,PVOID,ULONG,ULONG);
    ULONG (*GetBusData)(PVOID,ULONG,PVOID,ULONG,ULONG);
} BUS_INTERFACE_STANDARD;
#endif
