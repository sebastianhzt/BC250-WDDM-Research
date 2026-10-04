/* SPDX-License-Identifier: Apache-2.0 */
/* Combined reduced RAM types; not the WDK ABI or Windows scheduler. */
#ifndef BC250_MOCK_ADMISSION_WDM_H
#define BC250_MOCK_ADMISSION_WDM_H
#include "../dma-gate-20261004/mock-gate-wdm.h"
/* Suppress the incompatible duplicate reduced PCI mock type declarations.
 * This adapter supplies the missing PCI-only members without editing either
 * predecessor. Real WDK builds never include any of these fake types. */
#define BC250_PCI_MOCK_WDM_H
typedef uint8_t UCHAR;
typedef uint16_t USHORT;
typedef void VOID;
#define STATUS_DEVICE_NOT_READY ((NTSTATUS)0xC00000A3U)
#define STATUS_DATA_ERROR ((NTSTATUS)0xC000003EU)
#define PCI_WHICHSPACE_CONFIG 0
#define RtlCopyMemory(p,s,n) memcpy(p,s,n)
typedef struct _BUS_INTERFACE_STANDARD {
    USHORT Size,Version;
    PVOID Context;
    VOID (*InterfaceReference)(PVOID);
    VOID (*InterfaceDereference)(PVOID);
    BOOLEAN (*TranslateBusAddress)(PVOID,ULONGLONG,ULONG *,ULONGLONG *);
    PVOID (*GetDmaAdapter)(PVOID,PVOID,ULONG *);
    ULONG (*SetBusData)(PVOID,ULONG,PVOID,ULONG,ULONG);
    ULONG (*GetBusData)(PVOID,ULONG,PVOID,ULONG,ULONG);
} BUS_INTERFACE_STANDARD;
extern BOOLEAN Bc250AdmissionMockAllowed;
LONG InterlockedOr(volatile LONG *,LONG);
#endif
