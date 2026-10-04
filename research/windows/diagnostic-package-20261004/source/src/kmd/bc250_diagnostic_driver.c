/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Sebastian
 * Isolated build 21: WDM metadata diagnostics, NOT a WDDM miniport.
 * No display, GPU MMIO, DMA, firmware, scheduler, UMD or asynchronous jobs.
 * Reuses the separately reviewed passive PnP module, not legacy DriverEntry.
 */
#include "amdbc250_dream_pnp.h"
#include "bc250_diag_policy.h"
#include <wdmsec.h>

static PDEVICE_OBJECT g_Normal;
static PDEVICE_OBJECT g_Research;
static const GUID g_NormalClass =
    {0x7d992ead, 0xd2c5, 0x47be, {0xb0,0xdb,0x2b,0x2d,0x1c,0x71,0xae,0xb8}};
static const GUID g_ResearchClass =
    {0x1b117e14, 0xaac7, 0x49cf, {0x83,0x0f,0x13,0x0d,0xc7,0xe1,0x68,0x40}};

static NTSTATUS Bc250Complete(PIRP irp, NTSTATUS status, ULONG bytes)
{
    irp->IoStatus.Status = status;
    irp->IoStatus.Information = bytes;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

static NTSTATUS Bc250Pass(PDEVICE_OBJECT device, PIRP irp)
{
    if (device == g_Normal || device == g_Research ||
        !DreamV3WdmIsBoundFdo(device))
        return Bc250Complete(irp, STATUS_NOT_SUPPORTED, 0);
    return DreamV3WdmDispatchPassThrough(device, irp);
}

static NTSTATUS Bc250PnpDispatch(PDEVICE_OBJECT device, PIRP irp)
{
    if (device == g_Normal || device == g_Research ||
        !DreamV3WdmIsBoundFdo(device))
        return Bc250Complete(irp, STATUS_NOT_SUPPORTED, 0);
    return DreamV3WdmDispatchPnp(device, irp);
}

static NTSTATUS Bc250Power(PDEVICE_OBJECT device, PIRP irp)
{
    if (device == g_Normal || device == g_Research ||
        !DreamV3WdmIsBoundFdo(device))
        return Bc250Complete(irp, STATUS_NOT_SUPPORTED, 0);
    return DreamV3WdmDispatchPower(device, irp);
}

static BOOLEAN Bc250ServiceDword(PCWSTR name, UINT32 *value)
{
    UNICODE_STRING path, keyName;
    OBJECT_ATTRIBUTES attrs;
    HANDLE key = NULL;
    ULONG returned = 0;
    NTSTATUS status;
    DECLSPEC_ALIGN(8) UCHAR data[sizeof(KEY_VALUE_PARTIAL_INFORMATION)+sizeof(ULONG)] = {0};
    PKEY_VALUE_PARTIAL_INFORMATION info = (PKEY_VALUE_PARTIAL_INFORMATION)data;
    *value = MAXULONG;
    RtlInitUnicodeString(&path, L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\atikmdag");
    InitializeObjectAttributes(&attrs, &path, OBJ_CASE_INSENSITIVE|OBJ_KERNEL_HANDLE, NULL, NULL);
    status = ZwOpenKey(&key, KEY_QUERY_VALUE, &attrs);
    if (!NT_SUCCESS(status)) return FALSE;
    RtlInitUnicodeString(&keyName, name);
    status = ZwQueryValueKey(key, &keyName, KeyValuePartialInformation, data, sizeof(data), &returned);
    ZwClose(key);
    if (!NT_SUCCESS(status) || info->Type != REG_DWORD || info->DataLength != sizeof(ULONG))
        return FALSE;
    *value = *(UNALIGNED PULONG)info->Data;
    return TRUE;
}

static VOID Bc250Pnp( PAMDBC250_IOCTL_PNP_PREFLIGHT result)
{
    BOOLEAN gart, vm, sdma;
    DreamV3FillPnpPreflight(result, BC250_DIAGNOSTIC_BUILD_ID);
    gart = Bc250ServiceDword(L"HwInitGart", &result->HwInitGart);
    vm = Bc250ServiceDword(L"HwInitVm", &result->HwInitVm);
    sdma = Bc250ServiceDword(L"HwInitSdmaRing", &result->HwInitSdmaRing);
    if (gart && vm && sdma && !result->HwInitGart && !result->HwInitVm && !result->HwInitSdmaRing)
        result->SafetyFlags |= AMDBC250_PNP_SAFE_INTERLOCKS_OFF;
    else result->BlockerFlags |= AMDBC250_PNP_BLOCK_INTERLOCK_ON;
}

static VOID Bc250W2p(PAMDBC250_IOCTL_W2P_PREFLIGHT result)
{
    AMDBC250_IOCTL_PNP_PREFLIGHT pnp;
    Bc250Pnp(&pnp);
    RtlZeroMemory(result, sizeof(*result));
    result->Version = AMDBC250_W2P_PREFLIGHT_VERSION;
    result->StructSize = sizeof(*result);
    result->DriverBuildId = BC250_DIAGNOSTIC_BUILD_ID;
    result->Status = STATUS_DEVICE_NOT_READY;
    result->PnpSafetyFlags = pnp.SafetyFlags;
    result->PnpBlockerFlags = pnp.BlockerFlags;
    result->PnpState = pnp.PnpState;
    result->PnpStarted = pnp.Started;
    result->HwInitGart = pnp.HwInitGart;
    result->HwInitVm = pnp.HwInitVm;
    result->HwInitSdmaRing = pnp.HwInitSdmaRing;
    result->BlockerFlags = AMDBC250_W2P_BLOCK_NO_BAR0_RESOURCE |
        AMDBC250_W2P_BLOCK_NO_WINDOWS_VRAM_OWNER |
        AMDBC250_W2P_BLOCK_GPU_MC_MAP_UNVALIDATED |
        AMDBC250_W2P_BLOCK_NO_WINDOWS_GART_OWNER |
        AMDBC250_W2P_BLOCK_DMA_NOT_AUTHORIZED;
    if (!pnp.Started || !pnp.PdoReferenceAcquired)
        result->BlockerFlags |= AMDBC250_W2P_BLOCK_PNP_NOT_STARTED;
    if (!(pnp.SafetyFlags & AMDBC250_PNP_SAFE_INTERLOCKS_OFF))
        result->BlockerFlags |= AMDBC250_W2P_BLOCK_INTERLOCK_UNKNOWN_OR_ON;
}

static NTSTATUS Bc250CreateClose(PDEVICE_OBJECT device, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    if (device != g_Normal && device != g_Research)
        return Bc250Pass(device, irp);
    stack = IoGetCurrentIrpStackLocation(irp);
    if (stack->MajorFunction == IRP_MJ_CREATE &&
        (stack->FileObject == NULL || stack->FileObject->FileName.Length != 0))
        return Bc250Complete(irp, STATUS_OBJECT_NAME_NOT_FOUND, 0);
    return Bc250Complete(irp, STATUS_SUCCESS, 0);
}

static NTSTATUS Bc250Control(PDEVICE_OBJECT device, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    ULONG endpoint, route, size;
    NTSTATUS status;
    PVOID buffer;
    if (device != g_Normal && device != g_Research)
        return Bc250Pass(device, irp);
    endpoint = device == g_Normal ? BC250_DIAG_NORMAL : BC250_DIAG_RESEARCH;
    stack = IoGetCurrentIrpStackLocation(irp);
    route = Bc250DiagRoute(endpoint, stack->Parameters.DeviceIoControl.IoControlCode);
    size = Bc250DiagSize(route);
    if (!size) return Bc250Complete(irp, STATUS_NOT_SUPPORTED, 0);
    status = IoValidateDeviceIoControlAccess(irp, FILE_READ_ACCESS);
    if (!NT_SUCCESS(status)) return Bc250Complete(irp, status, 0);
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return Bc250Complete(irp, STATUS_INVALID_DEVICE_STATE, 0);
    if (stack->Parameters.DeviceIoControl.InputBufferLength != 0)
        return Bc250Complete(irp, STATUS_INVALID_PARAMETER, 0);
    if (stack->Parameters.DeviceIoControl.OutputBufferLength < size)
        return Bc250Complete(irp, STATUS_BUFFER_TOO_SMALL, 0);
    buffer = irp->AssociatedIrp.SystemBuffer;
    if (buffer == NULL) return Bc250Complete(irp, STATUS_INVALID_USER_BUFFER, 0);
    switch (route) {
    case BC250_DIAG_PNP: Bc250Pnp((PAMDBC250_IOCTL_PNP_PREFLIGHT)buffer); break;
    case BC250_DIAG_W2P: Bc250W2p((PAMDBC250_IOCTL_W2P_PREFLIGHT)buffer); break;
    case BC250_DIAG_RESOURCE:
        DreamV3FillResourcePreflight((PAMDBC250_IOCTL_RESOURCE_PREFLIGHT)buffer, BC250_DIAGNOSTIC_BUILD_ID);
        break;
    case BC250_DIAG_PCI_DISABLED:
        DreamV3FillPciConfigPreflight((PAMDBC250_IOCTL_PCI_CONFIG_PREFLIGHT)buffer, BC250_DIAGNOSTIC_BUILD_ID);
        break;
    default: return Bc250Complete(irp, STATUS_NOT_SUPPORTED, 0);
    }
    return Bc250Complete(irp, STATUS_SUCCESS, size);
}

static VOID Bc250Delete(PDEVICE_OBJECT *device, PCWSTR link)
{
    UNICODE_STRING name;
    if (*device == NULL) return;
    RtlInitUnicodeString(&name, link);
    IoDeleteSymbolicLink(&name);
    IoDeleteDevice(*device);
    *device = NULL;
}

static VOID Bc250Unload(PDRIVER_OBJECT driver)
{
    UNREFERENCED_PARAMETER(driver);
    DreamV3WdmPnpUninitialize();
    Bc250Delete(&g_Research, L"\\DosDevices\\AMDBC250DreamResearchV1");
    Bc250Delete(&g_Normal, L"\\DosDevices\\AMDBC250DreamV43");
}

static NTSTATUS Bc250Create(PDRIVER_OBJECT driver, PCWSTR path, PCWSTR link,
    const GUID *guid, PDEVICE_OBJECT *device)
{
    UNICODE_STRING name, symbolic;
    NTSTATUS status;
    RtlInitUnicodeString(&name, path);
    RtlInitUnicodeString(&symbolic, link);
    status = IoCreateDeviceSecure(driver, 0, &name, FILE_DEVICE_AMDBC250,
        FILE_DEVICE_SECURE_OPEN, FALSE, &SDDL_DEVOBJ_SYS_ALL_ADM_ALL, guid, device);
    if (!NT_SUCCESS(status)) { *device = NULL; return status; }
    (*device)->Flags |= DO_BUFFERED_IO;
    status = IoCreateSymbolicLink(&symbolic, &name);
    if (!NT_SUCCESS(status)) { IoDeleteDevice(*device); *device = NULL; return status; }
    return STATUS_SUCCESS;
}

DRIVER_INITIALIZE DriverEntry;
NTSTATUS DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING registryPath)
{
    ULONG index;
    NTSTATUS status;
    HANDLE key = NULL;
    OBJECT_ATTRIBUTES attrs;
    UNICODE_STRING valueName;
    ULONG build = BC250_DIAGNOSTIC_BUILD_ID;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL || registryPath == NULL || registryPath->Buffer == NULL)
        return STATUS_INVALID_PARAMETER;
    DreamV3WdmPnpInitialize(driver);
    /* Classify named endpoints/unknown objects BEFORE any extension access. */
    for (index = 0; index <= IRP_MJ_MAXIMUM_FUNCTION; ++index)
        driver->MajorFunction[index] = Bc250Pass;
    driver->MajorFunction[IRP_MJ_PNP] = Bc250PnpDispatch;
    driver->MajorFunction[IRP_MJ_POWER] = Bc250Power;
    driver->MajorFunction[IRP_MJ_CREATE] = Bc250CreateClose;
    driver->MajorFunction[IRP_MJ_CLEANUP] = Bc250CreateClose;
    driver->MajorFunction[IRP_MJ_CLOSE] = Bc250CreateClose;
    driver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = Bc250Control;
    driver->DriverUnload = Bc250Unload;
    status = Bc250Create(driver, L"\\Device\\AMDBC250DreamV43", L"\\DosDevices\\AMDBC250DreamV43", &g_NormalClass, &g_Normal);
    if (!NT_SUCCESS(status)) return status;
    status = Bc250Create(driver, L"\\Device\\AMDBC250DreamResearchV1", L"\\DosDevices\\AMDBC250DreamResearchV1", &g_ResearchClass, &g_Research);
    if (!NT_SUCCESS(status)) { Bc250Unload(driver); return status; }
    /* Marker is written last; the IOCTL build ID and installed SYS hash must
     * still be verified after reboot. Registry state alone can be stale. */
    InitializeObjectAttributes(&attrs, registryPath, OBJ_CASE_INSENSITIVE|OBJ_KERNEL_HANDLE, NULL, NULL);
    status = ZwOpenKey(&key, KEY_SET_VALUE, &attrs);
    if (NT_SUCCESS(status)) {
        RtlInitUnicodeString(&valueName, L"DriverBuildId");
        status = ZwSetValueKey(key, &valueName, 0, REG_DWORD, &build, sizeof(build));
        ZwClose(key);
    }
    if (!NT_SUCCESS(status)) Bc250Unload(driver);
    else {
        /* Publish only after both endpoints and the marker are complete. */
        g_Research->Flags &= ~DO_DEVICE_INITIALIZING;
        g_Normal->Flags &= ~DO_DEVICE_INITIALIZING;
    }
    return status;
}
