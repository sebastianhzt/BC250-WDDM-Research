#pragma once

#ifndef _AMDBC250_DREAM_PNP_H_
#define _AMDBC250_DREAM_PNP_H_

#include <ntddk.h>
#include "amdbc250_ioctl.h"

/* Internal lifetime token. BindingContext is opaque outside the PnP module
 * and is never copied to user mode. */
typedef struct _DREAM_V3_PNP_PDO_REFERENCE {
    PDEVICE_OBJECT PhysicalDeviceObject;
    PDEVICE_OBJECT LowerDeviceObject;
    PVOID BindingContext;
    ULONG BindingGeneration;
    ULONG StateEpoch;
} DREAM_V3_PNP_PDO_REFERENCE, *PDREAM_V3_PNP_PDO_REFERENCE;

BOOLEAN
DreamV3IsBc250Pdo(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject
    );

VOID
DreamV3WdmPnpInitialize(
    _Inout_ PDRIVER_OBJECT DriverObject
    );

VOID
DreamV3WdmPnpUninitialize(VOID);

DRIVER_ADD_DEVICE DreamV3WdmAddDevice;
BOOLEAN DreamV3WdmIsBoundFdo(_In_ PDEVICE_OBJECT DeviceObject);
DRIVER_DISPATCH DreamV3WdmDispatchPassThrough;
DRIVER_DISPATCH DreamV3WdmDispatchPnp;
DRIVER_DISPATCH DreamV3WdmDispatchPower;

NTSTATUS
DreamV3AcquireStartedPnpPdo(
    _Out_ PDREAM_V3_PNP_PDO_REFERENCE Reference
    );

VOID
DreamV3ReleasePnpPdo(
    _Inout_ PDREAM_V3_PNP_PDO_REFERENCE Reference
    );

VOID
DreamV3FillPnpPreflight(
    _Out_ PAMDBC250_IOCTL_PNP_PREFLIGHT Preflight,
    _In_ ULONG DriverBuildId
    );

VOID
DreamV3FillResourcePreflight(
    _Out_ PAMDBC250_IOCTL_RESOURCE_PREFLIGHT Preflight,
    _In_ ULONG DriverBuildId
    );

VOID
DreamV3FillPciConfigPreflight(
    _Out_ PAMDBC250_IOCTL_PCI_CONFIG_PREFLIGHT Preflight,
    _In_ ULONG DriverBuildId
    );

#endif /* _AMDBC250_DREAM_PNP_H_ */
