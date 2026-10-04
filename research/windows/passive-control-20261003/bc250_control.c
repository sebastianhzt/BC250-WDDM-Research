/* SPDX-License-Identifier: Apache-2.0
 * Independent control-only candidate, never linked to the display miniport.
 * Offline tests and /c only at this stage. DO NOT load/install this source.
 * No PCI enumeration, MMIO, firmware, physical allocations, DMA or WDDM DDIs.
 */
#include "bc250_control_platform.h"
#include "bc250_control_abi.h"
#include "passive-port-20261003/bc250_passive_policy.h"

#define BC250_CONTROL_SIGNATURE 0x31434243U
static const WCHAR Bc250ControlDeviceName[] = L"\\Device\\BC250ResearchControlV1";
static const WCHAR Bc250ControlLinkName[] = L"\\DosDevices\\BC250ResearchControlV1";
/* Default only: administrators may override class settings in Windows. */
static const WCHAR Bc250ControlSddl[] = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)";
static const GUID Bc250ControlClass = {
    0xe27f9606, 0x8bd3, 0x48a0, {0x8f, 0x53, 0x0e, 0x66, 0xdf, 0xfb, 0xfd, 0xef}
};
typedef struct BC250_CONTROL_EXTENSION {
    ULONG Signature;
    BOOLEAN LinkCreated;
    BOOLEAN Ready;
} BC250_CONTROL_EXTENSION;

static NTSTATUS Bc250ControlComplete(PIRP Irp, NTSTATUS Status, ULONG_PTR Information)
{
    if (!Irp) return STATUS_INVALID_PARAMETER;
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static BOOLEAN Bc250ControlIsReady(PDEVICE_OBJECT Device)
{
    BC250_CONTROL_EXTENSION *extension;
    if (!Device || !Device->DeviceExtension || (Device->Flags & DO_DEVICE_INITIALIZING)) return FALSE;
    extension = (BC250_CONTROL_EXTENSION *)Device->DeviceExtension;
    return extension->Signature == BC250_CONTROL_SIGNATURE && extension->Ready;
}

static NTSTATUS Bc250ControlReject(PDEVICE_OBJECT Device, PIRP Irp)
{
    UNREFERENCED_PARAMETER(Device);
    return Bc250ControlComplete(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
}

static NTSTATUS Bc250ControlCreateClose(PDEVICE_OBJECT Device, PIRP Irp)
{
    PIO_STACK_LOCATION stack;
    if (!Irp) return STATUS_INVALID_PARAMETER;
    stack = IoGetCurrentIrpStackLocation(Irp);
    if (stack->MajorFunction == IRP_MJ_CLOSE || stack->MajorFunction == IRP_MJ_CLEANUP)
        return Bc250ControlComplete(Irp, STATUS_SUCCESS, 0);
    if (stack->MajorFunction != IRP_MJ_CREATE)
        return Bc250ControlReject(Device, Irp);
    if (!Bc250ControlIsReady(Device))
        return Bc250ControlComplete(Irp, STATUS_DEVICE_NOT_READY, 0);
    if (!stack->FileObject || stack->FileObject->FileName.Length != 0)
        return Bc250ControlComplete(Irp, STATUS_INVALID_PARAMETER, 0);
    if (stack->Parameters.Create.Options & FILE_DIRECTORY_FILE)
        return Bc250ControlComplete(Irp, STATUS_NOT_A_DIRECTORY, 0);
    return Bc250ControlComplete(Irp, STATUS_SUCCESS, 0);
}

static NTSTATUS Bc250ControlDeviceControl(PDEVICE_OBJECT Device, PIRP Irp)
{
    PIO_STACK_LOCATION stack;
    BC250_CONTROL_METADATA *metadata;
    if (!Irp) return STATUS_INVALID_PARAMETER;
    if (!Bc250ControlIsReady(Device))
        return Bc250ControlComplete(Irp, STATUS_DEVICE_NOT_READY, 0);
    stack = IoGetCurrentIrpStackLocation(Irp);
    if (stack->Parameters.DeviceIoControl.IoControlCode != BC250_CONTROL_QUERY_METADATA)
        return Bc250ControlReject(Device, Irp);
    if (stack->Parameters.DeviceIoControl.InputBufferLength != 0)
        return Bc250ControlComplete(Irp, STATUS_INVALID_PARAMETER, 0);
    if (stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(BC250_CONTROL_METADATA))
        return Bc250ControlComplete(Irp, STATUS_BUFFER_TOO_SMALL, 0);
    if (!Irp->AssociatedIrp.SystemBuffer)
        return Bc250ControlComplete(Irp, STATUS_INVALID_PARAMETER, 0);
    metadata = (BC250_CONTROL_METADATA *)Irp->AssociatedIrp.SystemBuffer;
    RtlZeroMemory(metadata, sizeof(*metadata));
    metadata->SizeBytes = sizeof(*metadata);
    metadata->AbiVersion = BC250_CONTROL_ABI_VERSION;
    metadata->PolicyVersion = BC250_PASSIVE_PORT_POLICY_VERSION;
    metadata->GpuRuntimeEnabled = BC250_PASSIVE_GPU_RUNTIME_ENABLED;
    /* HardwareCapabilities and all reserved words remain zero. */
    return Bc250ControlComplete(Irp, STATUS_SUCCESS, sizeof(*metadata));
}

/* Only objects returned by this driver's successful create are eligible. */
static VOID Bc250ControlDestroyOwnDevice(PDEVICE_OBJECT Device)
{
    BC250_CONTROL_EXTENSION *extension;
    UNICODE_STRING link;
    if (!Device || !Device->DeviceExtension) return;
    extension = (BC250_CONTROL_EXTENSION *)Device->DeviceExtension;
    if (extension->Signature != BC250_CONTROL_SIGNATURE) return;
    extension->Ready = FALSE;
    if (extension->LinkCreated) {
        RtlInitUnicodeString(&link, Bc250ControlLinkName);
        /* Unload cannot propagate an OS unlink failure; no delete-by-name.
         * A residual link on such an OS failure is a runtime validation gap. */
        (void)IoDeleteSymbolicLink(&link);
        extension->LinkCreated = FALSE;
    }
    extension->Signature = 0;
    IoDeleteDevice(Device);
}

static VOID Bc250ControlUnload(PDRIVER_OBJECT DriverObject)
{
    PDEVICE_OBJECT device, next;
    if (!DriverObject) return;
    device = DriverObject->DeviceObject;
    while (device) {
        next = device->NextDevice;
        Bc250ControlDestroyOwnDevice(device);
        device = next;
    }
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    UNICODE_STRING name, link, sddl;
    PDEVICE_OBJECT device = NULL;
    BC250_CONTROL_EXTENSION *extension;
    NTSTATUS status;
    ULONG index;
    UNREFERENCED_PARAMETER(RegistryPath);
    if (!DriverObject) return STATUS_INVALID_PARAMETER;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL || DriverObject->DeviceObject)
        return STATUS_INVALID_DEVICE_STATE;
    /* Safe handlers and unload exist before any object can be published. */
    for (index = 0; index <= IRP_MJ_MAXIMUM_FUNCTION; ++index)
        DriverObject->MajorFunction[index] = Bc250ControlReject;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = Bc250ControlCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = Bc250ControlCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = Bc250ControlCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = Bc250ControlDeviceControl;
    DriverObject->DriverUnload = Bc250ControlUnload;
    RtlInitUnicodeString(&name, Bc250ControlDeviceName);
    RtlInitUnicodeString(&link, Bc250ControlLinkName);
    RtlInitUnicodeString(&sddl, Bc250ControlSddl);
    status = IoCreateDeviceSecure(DriverObject, sizeof(*extension), &name,
        BC250_CONTROL_DEVICE_TYPE, FILE_DEVICE_SECURE_OPEN, FALSE,
        &sddl, &Bc250ControlClass, &device);
    /* In particular: a name collision is a failure, never a lookup/delete. */
    if (!NT_SUCCESS(status)) return status;
    extension = (BC250_CONTROL_EXTENSION *)device->DeviceExtension;
    RtlZeroMemory(extension, sizeof(*extension));
    extension->Signature = BC250_CONTROL_SIGNATURE;
    device->Flags |= DO_BUFFERED_IO;
    status = IoCreateSymbolicLink(&link, &name);
    if (!NT_SUCCESS(status)) {
        Bc250ControlDestroyOwnDevice(device);
        return status;
    }
    extension->LinkCreated = TRUE;
    extension->Ready = TRUE;
    device->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}
