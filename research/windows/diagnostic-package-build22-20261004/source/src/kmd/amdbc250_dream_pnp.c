/* Minimal WDM PnP binding for the Windows 11 fallback path.
 *
 * This module creates an unnamed FDO, attaches it to the PCI stack and
 * forwards all requests it does not own. It deliberately performs no MMIO,
 * DMA allocation, resource mapping, interrupt setup or hardware start.
 */

#include "amdbc250_dream_pnp.h"

#define DREAM_V3_PNP_EXTENSION_SIGNATURE 0x38504E50UL /* "PNP8" */
#define DREAM_V3_PNP_TAG                 '8PnD'
#define DREAM_V3_MAX_RESOURCE_DESCRIPTORS 32UL

typedef struct _DREAM_V3_WDM_PNP_EXTENSION {
    ULONG Signature;
    PDEVICE_OBJECT Self;
    PDEVICE_OBJECT LowerDeviceObject;
    PDEVICE_OBJECT PhysicalDeviceObject;
    IO_REMOVE_LOCK RemoveLock;
    volatile LONG State;
    volatile LONG StateEpoch;
    ULONG PreviousState;
    BOOLEAN HardwareIdValidated;
    AMDBC250_IOCTL_RESOURCE_PREFLIGHT ResourceSnapshot;
} DREAM_V3_WDM_PNP_EXTENSION, *PDREAM_V3_WDM_PNP_EXTENSION;

static EX_PUSH_LOCK g_DreamV3PnpBindingLock;
static PDREAM_V3_WDM_PNP_EXTENSION g_DreamV3PnpBinding;
static PVOID volatile g_DreamV3FdoIdentity;
static volatile LONG g_DreamV3BindingGeneration;
static volatile LONG g_DreamV3AddDeviceCalls;
static volatile LONG g_DreamV3StartDeviceCalls;
static volatile LONG g_DreamV3QueryStopCalls;
static volatile LONG g_DreamV3StopDeviceCalls;
static volatile LONG g_DreamV3QueryRemoveCalls;
static volatile LONG g_DreamV3SurpriseRemoveCalls;
static volatile LONG g_DreamV3RemoveDeviceCalls;
static volatile LONG g_DreamV3LastAddStatus = STATUS_NOT_SUPPORTED;
static volatile LONG g_DreamV3LastStartStatus = STATUS_NOT_SUPPORTED;

static VOID DreamV3SetPnpState(
    _Inout_ PDREAM_V3_WDM_PNP_EXTENSION Extension,
    _In_ ULONG NewState
    );

/* Called only for START_DEVICE while its PnP-supplied lists are valid. The
 * ordinal correspondence is between raw and translated descriptors, never
 * between a memory descriptor and a PCI BAR number. */
static VOID
DreamV3CaptureResourceSnapshot(
    _In_opt_ PCM_RESOURCE_LIST Raw,
    _In_opt_ PCM_RESOURCE_LIST Translated,
    _Out_ PAMDBC250_IOCTL_RESOURCE_PREFLIGHT Snapshot
    )
{
    PCM_PARTIAL_RESOURCE_LIST rawPart;
    PCM_PARTIAL_RESOURCE_LIST translatedPart;
    ULONG index;

    RtlZeroMemory(Snapshot, sizeof(*Snapshot));
    if (Raw == NULL || Translated == NULL)
        return;
    Snapshot->SnapshotState = AMDBC250_RESOURCE_SNAPSHOT_REJECTED;
    if (Raw->Count != 1 || Translated->Count != 1)
        return;
    rawPart = &Raw->List[0].PartialResourceList;
    translatedPart = &Translated->List[0].PartialResourceList;
    if (rawPart->Count != translatedPart->Count ||
        rawPart->Count > DREAM_V3_MAX_RESOURCE_DESCRIPTORS)
        return;
    Snapshot->DescriptorCount = rawPart->Count;
    for (index = 0; index < rawPart->Count; ++index) {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR raw =
            &rawPart->PartialDescriptors[index];
        PCM_PARTIAL_RESOURCE_DESCRIPTOR translated =
            &translatedPart->PartialDescriptors[index];
        PAMDBC250_RESOURCE_MEMORY_ENTRY memory;
        ULONGLONG rawStart, translatedStart, length;

        if (raw->Type != translated->Type)
            goto Reject;
        if (raw->Type == CmResourceTypeMemoryLarge)
            goto Reject; /* Its Length uses a different encoding. */
        if (raw->Type != CmResourceTypeMemory)
            continue;
        if (Snapshot->MemoryCount >= AMDBC250_RESOURCE_MAX_MEMORY_ENTRIES ||
            raw->u.Memory.Length == 0 ||
            raw->u.Memory.Length != translated->u.Memory.Length)
            goto Reject;
        rawStart = (ULONGLONG)raw->u.Memory.Start.QuadPart;
        translatedStart =
            (ULONGLONG)translated->u.Memory.Start.QuadPart;
        length = raw->u.Memory.Length;
        if (raw->u.Memory.Start.QuadPart < 0 ||
            translated->u.Memory.Start.QuadPart < 0 ||
            rawStart > ~(ULONGLONG)0 - length ||
            translatedStart > ~(ULONGLONG)0 - length)
            goto Reject;
        memory = &Snapshot->Memory[Snapshot->MemoryCount++];
        memory->RawStart = rawStart;
        memory->TranslatedStart = translatedStart;
        memory->Length = length;
        memory->RawFlags = raw->Flags;
        memory->TranslatedFlags = translated->Flags;
        memory->DescriptorOrdinal = index;
    }
    Snapshot->SnapshotState = AMDBC250_RESOURCE_SNAPSHOT_VALID;
    return;

Reject:
    RtlZeroMemory(Snapshot->Memory, sizeof(Snapshot->Memory));
    Snapshot->MemoryCount = 0;
    Snapshot->SnapshotState = AMDBC250_RESOURCE_SNAPSHOT_REJECTED;
}

/* The same binding lock protects snapshot copy, replacement and invalidation. */
static VOID
DreamV3ReplaceResourceSnapshot(
    _Inout_ PDREAM_V3_WDM_PNP_EXTENSION Extension,
    _In_opt_ PAMDBC250_IOCTL_RESOURCE_PREFLIGHT Snapshot,
    _In_ ULONG NewState
    )
{
    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&g_DreamV3PnpBindingLock);
    RtlZeroMemory(&Extension->ResourceSnapshot,
        sizeof(Extension->ResourceSnapshot));
    if (Snapshot != NULL && g_DreamV3PnpBinding == Extension)
        RtlCopyMemory(&Extension->ResourceSnapshot, Snapshot,
            sizeof(Extension->ResourceSnapshot));
    DreamV3SetPnpState(Extension, NewState);
    ExReleasePushLockExclusive(&g_DreamV3PnpBindingLock);
    KeLeaveCriticalRegion();
}

BOOLEAN
DreamV3IsBc250Pdo(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject
    )
{
    static const UNICODE_STRING bc250Prefix =
        RTL_CONSTANT_STRING(L"PCI\\VEN_1002&DEV_13FE");
    PWSTR hardwareIds = NULL;
    PWSTR currentId;
    ULONG propertyLength = 0;
    ULONG remainingChars;
    NTSTATUS status;
    BOOLEAN match = FALSE;

    if (PhysicalDeviceObject == NULL)
        return FALSE;
    status = IoGetDeviceProperty(PhysicalDeviceObject,
        DevicePropertyHardwareID, 0, NULL, &propertyLength);
    if (status != STATUS_BUFFER_TOO_SMALL ||
        propertyLength < 2 * sizeof(WCHAR))
        return FALSE;

    hardwareIds = (PWSTR)ExAllocatePool2(POOL_FLAG_PAGED,
        propertyLength, 'iD3A');
    if (hardwareIds == NULL)
        return FALSE;

    status = IoGetDeviceProperty(PhysicalDeviceObject,
        DevicePropertyHardwareID, propertyLength, hardwareIds,
        &propertyLength);
    if (!NT_SUCCESS(status))
        goto Cleanup;

    currentId = hardwareIds;
    remainingChars = propertyLength / sizeof(WCHAR);
    while (remainingChars > 1 && *currentId != L'\0') {
        ULONG idChars = 0;
        UNICODE_STRING candidate;

        while (idChars < remainingChars && currentId[idChars] != L'\0')
            ++idChars;
        if (idChars == remainingChars ||
            idChars > MAXUSHORT / sizeof(WCHAR))
            break;

        candidate.Buffer = currentId;
        candidate.Length = (USHORT)(idChars * sizeof(WCHAR));
        candidate.MaximumLength = candidate.Length;
        if (RtlPrefixUnicodeString(&bc250Prefix, &candidate, TRUE) &&
            (candidate.Length == bc250Prefix.Length ||
             candidate.Buffer[bc250Prefix.Length / sizeof(WCHAR)] == L'&')) {
            match = TRUE;
            break;
        }

        currentId += idChars + 1;
        remainingChars -= idChars + 1;
    }

Cleanup:
    ExFreePoolWithTag(hardwareIds, 'iD3A');
    return match;
}

static PDREAM_V3_WDM_PNP_EXTENSION
DreamV3GetPnpExtension(
    _In_ PDEVICE_OBJECT DeviceObject
    )
{
    PDREAM_V3_WDM_PNP_EXTENSION extension;

    if (DeviceObject == NULL || DeviceObject->DeviceExtension == NULL)
        return NULL;
    extension = (PDREAM_V3_WDM_PNP_EXTENSION)DeviceObject->DeviceExtension;
    if (extension->Signature != DREAM_V3_PNP_EXTENSION_SIGNATURE ||
        extension->Self != DeviceObject)
        return NULL;
    return extension;
}

static VOID
DreamV3SetPnpState(
    _Inout_ PDREAM_V3_WDM_PNP_EXTENSION Extension,
    _In_ ULONG NewState
    )
{
    Extension->PreviousState = (ULONG)InterlockedExchange(
        &Extension->State, (LONG)NewState);
    InterlockedIncrement(&Extension->StateEpoch);
}

static VOID
DreamV3RestorePnpState(
    _Inout_ PDREAM_V3_WDM_PNP_EXTENSION Extension
    )
{
    InterlockedExchange(&Extension->State, (LONG)Extension->PreviousState);
    InterlockedIncrement(&Extension->StateEpoch);
}

static NTSTATUS
DreamV3CompleteIrp(
    _Inout_ PIRP Irp,
    _In_ NTSTATUS Status
    )
{
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static NTSTATUS
DreamV3ReleaseRemoveLockCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp,
    _In_ PVOID Context
    )
{
    PDREAM_V3_WDM_PNP_EXTENSION extension =
        (PDREAM_V3_WDM_PNP_EXTENSION)Context;

    UNREFERENCED_PARAMETER(DeviceObject);
    if (Irp->PendingReturned)
        IoMarkIrpPending(Irp);
    IoReleaseRemoveLock(&extension->RemoveLock, Irp);
    return STATUS_CONTINUE_COMPLETION;
}

static NTSTATUS
DreamV3ForwardIrpWithRemoveLock(
    _In_ PDREAM_V3_WDM_PNP_EXTENSION Extension,
    _Inout_ PIRP Irp
    )
{
    NTSTATUS status;
    IoCopyCurrentIrpStackLocationToNext(Irp);
    status = IoSetCompletionRoutineEx(Extension->Self, Irp, DreamV3ReleaseRemoveLockCompletion,
        Extension, TRUE, TRUE, TRUE);
    if (!NT_SUCCESS(status)) {
        IoReleaseRemoveLock(&Extension->RemoveLock, Irp);
        return DreamV3CompleteIrp(Irp, status);
    }
    /* A successful registration MUST be followed by IoCallDriver. */
    return IoCallDriver(Extension->LowerDeviceObject, Irp);
}

/* Classify by the published binding before interpreting DeviceExtension.
 * Caller has the OS-supplied device/IRP lifetime anchor. Both named endpoints
 * have zero extension bytes and must never enter DreamV3GetPnpExtension.
 */
BOOLEAN DreamV3WdmIsBoundFdo(_In_ PDEVICE_OBJECT DeviceObject)
{
    /* Power IRPs may arrive at DISPATCH_LEVEL: classification does not take
     * the PASSIVE/APC binding push lock and never dereferences this pointer.
     * Identity alone is NOT a lifetime reference; the OS anchors the supplied
     * DeviceObject/IRP, and the forwarding routine takes its remove lock. */
    return DeviceObject != NULL && InterlockedCompareExchangePointer(
        &g_DreamV3FdoIdentity, NULL, NULL) == DeviceObject;
}

VOID
DreamV3WdmPnpInitialize(
    _Inout_ PDRIVER_OBJECT DriverObject
    )
{
    ULONG index;

    ExInitializePushLock(&g_DreamV3PnpBindingLock);
    g_DreamV3PnpBinding = NULL;
    InterlockedExchangePointer(&g_DreamV3FdoIdentity, NULL);
    for (index = 0; index <= IRP_MJ_MAXIMUM_FUNCTION; ++index)
        DriverObject->MajorFunction[index] =
            DreamV3WdmDispatchPassThrough;
    DriverObject->MajorFunction[IRP_MJ_PNP] = DreamV3WdmDispatchPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = DreamV3WdmDispatchPower;
    DriverObject->DriverExtension->AddDevice = DreamV3WdmAddDevice;
}

VOID
DreamV3WdmPnpUninitialize(VOID)
{
    PDREAM_V3_WDM_PNP_EXTENSION binding;

    KeEnterCriticalRegion();
    ExAcquirePushLockShared(&g_DreamV3PnpBindingLock);
    binding = g_DreamV3PnpBinding;
    ExReleasePushLockShared(&g_DreamV3PnpBindingLock);
    KeLeaveCriticalRegion();

    if (binding != NULL) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
            "AMDBC250-DREAM-PNP: unload requested with a live FDO\n"));
    }
}

NTSTATUS
DreamV3WdmAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject
    )
{
    PDEVICE_OBJECT fdo = NULL;
    PDREAM_V3_WDM_PNP_EXTENSION extension;
    NTSTATUS status;

    InterlockedIncrement(&g_DreamV3AddDeviceCalls);
    if (KeGetCurrentIrql() != PASSIVE_LEVEL ||
        PhysicalDeviceObject == NULL) {
        status = STATUS_INVALID_DEVICE_STATE;
        InterlockedExchange(&g_DreamV3LastAddStatus, status);
        return status;
    }
    if (!DreamV3IsBc250Pdo(PhysicalDeviceObject)) {
        status = STATUS_OBJECT_TYPE_MISMATCH;
        InterlockedExchange(&g_DreamV3LastAddStatus, status);
        return status;
    }

    status = IoCreateDevice(DriverObject,
        sizeof(DREAM_V3_WDM_PNP_EXTENSION), NULL,
        PhysicalDeviceObject->DeviceType,
        PhysicalDeviceObject->Characteristics, FALSE, &fdo);
    if (!NT_SUCCESS(status)) {
        InterlockedExchange(&g_DreamV3LastAddStatus, status);
        return status;
    }

    extension = (PDREAM_V3_WDM_PNP_EXTENSION)fdo->DeviceExtension;
    RtlZeroMemory(extension, sizeof(*extension));
    extension->Signature = DREAM_V3_PNP_EXTENSION_SIGNATURE;
    extension->Self = fdo;
    extension->PhysicalDeviceObject = PhysicalDeviceObject;
    extension->State = AMDBC250_PNP_STATE_NOT_STARTED;
    extension->PreviousState = AMDBC250_PNP_STATE_NOT_STARTED;
    extension->HardwareIdValidated = TRUE;
    IoInitializeRemoveLock(&extension->RemoveLock, DREAM_V3_PNP_TAG, 0, 0);

    status = IoAttachDeviceToDeviceStackSafe(fdo, PhysicalDeviceObject,
        &extension->LowerDeviceObject);
    if (!NT_SUCCESS(status) || extension->LowerDeviceObject == NULL) {
        extension->Signature = 0;
        IoDeleteDevice(fdo);
        if (NT_SUCCESS(status))
            status = STATUS_NO_SUCH_DEVICE;
        InterlockedExchange(&g_DreamV3LastAddStatus, status);
        return status;
    }

    fdo->Flags |= extension->LowerDeviceObject->Flags &
        (DO_BUFFERED_IO | DO_DIRECT_IO | DO_POWER_PAGABLE |
         DO_POWER_INRUSH);

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&g_DreamV3PnpBindingLock);
    if (g_DreamV3PnpBinding != NULL) {
        ExReleasePushLockExclusive(&g_DreamV3PnpBindingLock);
        KeLeaveCriticalRegion();
        IoDetachDevice(extension->LowerDeviceObject);
        extension->LowerDeviceObject = NULL;
        extension->Signature = 0;
        IoDeleteDevice(fdo);
        status = STATUS_DEVICE_BUSY;
        InterlockedExchange(&g_DreamV3LastAddStatus, status);
        return status;
    }
    g_DreamV3PnpBinding = extension;
    InterlockedExchangePointer(&g_DreamV3FdoIdentity, fdo);
    InterlockedIncrement(&g_DreamV3BindingGeneration);
    ExReleasePushLockExclusive(&g_DreamV3PnpBindingLock);
    KeLeaveCriticalRegion();

    fdo->Flags &= ~DO_DEVICE_INITIALIZING;
    status = STATUS_SUCCESS;
    InterlockedExchange(&g_DreamV3LastAddStatus, status);
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250-DREAM-PNP: FDO attached, awaiting START_DEVICE\n"));
    return status;
}

NTSTATUS
DreamV3WdmDispatchPassThrough(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp
    )
{
    PDREAM_V3_WDM_PNP_EXTENSION extension =
        DreamV3GetPnpExtension(DeviceObject);
    NTSTATUS status;

    if (extension == NULL || extension->LowerDeviceObject == NULL)
        return DreamV3CompleteIrp(Irp, STATUS_NOT_SUPPORTED);
    status = IoAcquireRemoveLock(&extension->RemoveLock, Irp);
    if (!NT_SUCCESS(status))
        return DreamV3CompleteIrp(Irp, status);
    return DreamV3ForwardIrpWithRemoveLock(extension, Irp);
}

NTSTATUS
DreamV3WdmDispatchPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp
    )
{
    return DreamV3WdmDispatchPassThrough(DeviceObject, Irp);
}

NTSTATUS
DreamV3WdmDispatchPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp
    )
{
    PDREAM_V3_WDM_PNP_EXTENSION extension =
        DreamV3GetPnpExtension(DeviceObject);
    PIO_STACK_LOCATION stack;
    PDEVICE_OBJECT lower;
    NTSTATUS status;
    BOOLEAN forwarded;
    AMDBC250_IOCTL_RESOURCE_PREFLIGHT startResources;

    if (extension == NULL || extension->LowerDeviceObject == NULL)
        return DreamV3CompleteIrp(Irp, STATUS_NOT_SUPPORTED);
    lower = extension->LowerDeviceObject;
    stack = IoGetCurrentIrpStackLocation(Irp);

    status = IoAcquireRemoveLock(&extension->RemoveLock, Irp);
    if (!NT_SUCCESS(status))
        return DreamV3CompleteIrp(Irp, status);

    switch (stack->MinorFunction) {
    case IRP_MN_START_DEVICE:
        InterlockedIncrement(&g_DreamV3StartDeviceCalls);
        DreamV3CaptureResourceSnapshot(
            stack->Parameters.StartDevice.AllocatedResources,
            stack->Parameters.StartDevice.AllocatedResourcesTranslated,
            &startResources);
        forwarded = IoForwardIrpSynchronously(lower, Irp);
        status = forwarded ? Irp->IoStatus.Status :
            STATUS_INVALID_DEVICE_STATE;
        InterlockedExchange(&g_DreamV3LastStartStatus, status);
        if (NT_SUCCESS(status))
            DreamV3ReplaceResourceSnapshot(extension, &startResources,
                AMDBC250_PNP_STATE_STARTED);
        else
            DreamV3ReplaceResourceSnapshot(extension, NULL,
                AMDBC250_PNP_STATE_NOT_STARTED);
        Irp->IoStatus.Status = status;
        IoReleaseRemoveLock(&extension->RemoveLock, Irp);
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return status;

    case IRP_MN_QUERY_STOP_DEVICE:
        InterlockedIncrement(&g_DreamV3QueryStopCalls);
        DreamV3SetPnpState(extension,
            AMDBC250_PNP_STATE_STOP_PENDING);
        Irp->IoStatus.Status = STATUS_SUCCESS;
        return DreamV3ForwardIrpWithRemoveLock(extension, Irp);

    case IRP_MN_CANCEL_STOP_DEVICE:
        forwarded = IoForwardIrpSynchronously(lower, Irp);
        if (!forwarded) {
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                "AMDBC250-DREAM-PNP: could not forward CANCEL_STOP\n"));
        }
        if (InterlockedCompareExchange(&extension->State, 0, 0) ==
            AMDBC250_PNP_STATE_STOP_PENDING)
            DreamV3RestorePnpState(extension);
        /* PnP requires CANCEL_STOP to succeed even if a lower driver fails. */
        Irp->IoStatus.Status = STATUS_SUCCESS;
        IoReleaseRemoveLock(&extension->RemoveLock, Irp);
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_SUCCESS;

    case IRP_MN_STOP_DEVICE:
        InterlockedIncrement(&g_DreamV3StopDeviceCalls);
        DreamV3ReplaceResourceSnapshot(extension, NULL,
            AMDBC250_PNP_STATE_STOPPED);
        Irp->IoStatus.Status = STATUS_SUCCESS;
        return DreamV3ForwardIrpWithRemoveLock(extension, Irp);

    case IRP_MN_QUERY_REMOVE_DEVICE:
        InterlockedIncrement(&g_DreamV3QueryRemoveCalls);
        DreamV3SetPnpState(extension,
            AMDBC250_PNP_STATE_REMOVE_PENDING);
        Irp->IoStatus.Status = STATUS_SUCCESS;
        return DreamV3ForwardIrpWithRemoveLock(extension, Irp);

    case IRP_MN_CANCEL_REMOVE_DEVICE:
        forwarded = IoForwardIrpSynchronously(lower, Irp);
        if (!forwarded) {
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                "AMDBC250-DREAM-PNP: could not forward CANCEL_REMOVE\n"));
        }
        if (InterlockedCompareExchange(&extension->State, 0, 0) ==
            AMDBC250_PNP_STATE_REMOVE_PENDING)
            DreamV3RestorePnpState(extension);
        /* PnP requires CANCEL_REMOVE to succeed even if a lower driver fails. */
        Irp->IoStatus.Status = STATUS_SUCCESS;
        IoReleaseRemoveLock(&extension->RemoveLock, Irp);
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_SUCCESS;

    case IRP_MN_SURPRISE_REMOVAL:
        InterlockedIncrement(&g_DreamV3SurpriseRemoveCalls);
        DreamV3ReplaceResourceSnapshot(extension, NULL,
            AMDBC250_PNP_STATE_SURPRISE_REMOVED);
        Irp->IoStatus.Status = STATUS_SUCCESS;
        return DreamV3ForwardIrpWithRemoveLock(extension, Irp);

    case IRP_MN_REMOVE_DEVICE:
        InterlockedIncrement(&g_DreamV3RemoveDeviceCalls);
        InterlockedExchange(&extension->State,
            AMDBC250_PNP_STATE_DELETED);
        InterlockedIncrement(&extension->StateEpoch);
        KeEnterCriticalRegion();
        ExAcquirePushLockExclusive(&g_DreamV3PnpBindingLock);
        RtlZeroMemory(&extension->ResourceSnapshot,
            sizeof(extension->ResourceSnapshot));
        if (g_DreamV3PnpBinding == extension) {
            g_DreamV3PnpBinding = NULL;
            InterlockedExchangePointer(&g_DreamV3FdoIdentity, NULL);
        }
        ExReleasePushLockExclusive(&g_DreamV3PnpBindingLock);
        KeLeaveCriticalRegion();

        Irp->IoStatus.Status = STATUS_SUCCESS;
        IoSkipCurrentIrpStackLocation(Irp);
        status = IoCallDriver(lower, Irp);
        IoReleaseRemoveLockAndWait(&extension->RemoveLock, Irp);
        IoDetachDevice(lower);
        extension->LowerDeviceObject = NULL;
        extension->PhysicalDeviceObject = NULL;
        extension->Signature = 0;
        IoDeleteDevice(DeviceObject);
        return status;

    default:
        return DreamV3ForwardIrpWithRemoveLock(extension, Irp);
    }
}

NTSTATUS
DreamV3AcquireStartedPnpPdo(
    _Out_ PDREAM_V3_PNP_PDO_REFERENCE Reference
    )
{
    PDREAM_V3_WDM_PNP_EXTENSION extension;
    NTSTATUS status = STATUS_DEVICE_NOT_READY;

    RtlZeroMemory(Reference, sizeof(*Reference));
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;

    KeEnterCriticalRegion();
    ExAcquirePushLockShared(&g_DreamV3PnpBindingLock);
    extension = g_DreamV3PnpBinding;
    if (extension != NULL &&
        extension->Signature == DREAM_V3_PNP_EXTENSION_SIGNATURE &&
        extension->LowerDeviceObject != NULL &&
        extension->PhysicalDeviceObject != NULL &&
        extension->HardwareIdValidated &&
        InterlockedCompareExchange(&extension->State, 0, 0) ==
            AMDBC250_PNP_STATE_STARTED) {
        status = IoAcquireRemoveLock(&extension->RemoveLock, Reference);
        if (NT_SUCCESS(status)) {
            if (g_DreamV3PnpBinding == extension &&
                InterlockedCompareExchange(&extension->State, 0, 0) ==
                    AMDBC250_PNP_STATE_STARTED) {
                ObReferenceObject(extension->PhysicalDeviceObject);
                ObReferenceObject(extension->LowerDeviceObject);
                Reference->PhysicalDeviceObject =
                    extension->PhysicalDeviceObject;
                Reference->LowerDeviceObject =
                    extension->LowerDeviceObject;
                Reference->BindingContext = extension;
                Reference->BindingGeneration = (ULONG)
                    InterlockedCompareExchange(
                        &g_DreamV3BindingGeneration, 0, 0);
                Reference->StateEpoch = (ULONG)InterlockedCompareExchange(
                    &extension->StateEpoch, 0, 0);
            } else {
                IoReleaseRemoveLock(&extension->RemoveLock, Reference);
                status = STATUS_DELETE_PENDING;
            }
        }
    }
    ExReleasePushLockShared(&g_DreamV3PnpBindingLock);
    KeLeaveCriticalRegion();
    return status;
}

VOID
DreamV3ReleasePnpPdo(
    _Inout_ PDREAM_V3_PNP_PDO_REFERENCE Reference
    )
{
    PDREAM_V3_WDM_PNP_EXTENSION extension;

    if (Reference == NULL)
        return;
    extension = (PDREAM_V3_WDM_PNP_EXTENSION)Reference->BindingContext;
    if (Reference->LowerDeviceObject != NULL) {
        ObDereferenceObject(Reference->LowerDeviceObject);
        Reference->LowerDeviceObject = NULL;
    }
    if (Reference->PhysicalDeviceObject != NULL) {
        ObDereferenceObject(Reference->PhysicalDeviceObject);
        Reference->PhysicalDeviceObject = NULL;
    }
    if (extension != NULL) {
        IoReleaseRemoveLock(&extension->RemoveLock, Reference);
        Reference->BindingContext = NULL;
    }
}

VOID
DreamV3FillPnpPreflight(
    _Out_ PAMDBC250_IOCTL_PNP_PREFLIGHT Preflight,
    _In_ ULONG DriverBuildId
    )
{
    PDREAM_V3_WDM_PNP_EXTENSION extension;
    DREAM_V3_PNP_PDO_REFERENCE reference;
    NTSTATUS referenceStatus;

    RtlZeroMemory(Preflight, sizeof(*Preflight));
    Preflight->Version = AMDBC250_PNP_PREFLIGHT_VERSION;
    Preflight->StructSize = sizeof(*Preflight);
    Preflight->DriverBuildId = DriverBuildId;
    Preflight->Status = STATUS_DEVICE_NOT_READY;
    Preflight->QueryIrql = KeGetCurrentIrql();
    Preflight->BlockerFlags =
        AMDBC250_PNP_BLOCK_ACTIVE_DMA_NOT_AUTHORIZED;
    Preflight->BindingGeneration = (UINT32)InterlockedCompareExchange(
        &g_DreamV3BindingGeneration, 0, 0);
    Preflight->AddDeviceCalls = (UINT32)InterlockedCompareExchange(
        &g_DreamV3AddDeviceCalls, 0, 0);
    Preflight->StartDeviceCalls = (UINT32)InterlockedCompareExchange(
        &g_DreamV3StartDeviceCalls, 0, 0);
    Preflight->QueryStopCalls = (UINT32)InterlockedCompareExchange(
        &g_DreamV3QueryStopCalls, 0, 0);
    Preflight->StopDeviceCalls = (UINT32)InterlockedCompareExchange(
        &g_DreamV3StopDeviceCalls, 0, 0);
    Preflight->QueryRemoveCalls = (UINT32)InterlockedCompareExchange(
        &g_DreamV3QueryRemoveCalls, 0, 0);
    Preflight->SurpriseRemoveCalls = (UINT32)InterlockedCompareExchange(
        &g_DreamV3SurpriseRemoveCalls, 0, 0);
    Preflight->RemoveDeviceCalls = (UINT32)InterlockedCompareExchange(
        &g_DreamV3RemoveDeviceCalls, 0, 0);
    Preflight->LastAddStatus = InterlockedCompareExchange(
        &g_DreamV3LastAddStatus, 0, 0);
    Preflight->LastStartStatus = InterlockedCompareExchange(
        &g_DreamV3LastStartStatus, 0, 0);

    if (Preflight->QueryIrql != PASSIVE_LEVEL) {
        Preflight->BlockerFlags |= AMDBC250_PNP_BLOCK_WRONG_IRQL;
        return;
    }
    Preflight->SafetyFlags |= AMDBC250_PNP_SAFE_PASSIVE_LEVEL;
    if (Preflight->AddDeviceCalls != 0)
        Preflight->SafetyFlags |= AMDBC250_PNP_SAFE_ADD_DEVICE_CALLED;
    else
        Preflight->BlockerFlags |= AMDBC250_PNP_BLOCK_NO_ADD_DEVICE;

    KeEnterCriticalRegion();
    ExAcquirePushLockShared(&g_DreamV3PnpBindingLock);
    extension = g_DreamV3PnpBinding;
    if (extension != NULL &&
        extension->Signature == DREAM_V3_PNP_EXTENSION_SIGNATURE) {
        Preflight->BindingPresent = 1;
        Preflight->FdoCreated = 1;
        Preflight->PnpState = (UINT32)InterlockedCompareExchange(
            &extension->State, 0, 0);
        Preflight->HardwareIdMatched =
            extension->HardwareIdValidated ? 1 : 0;
        Preflight->LowerAttached =
            extension->LowerDeviceObject != NULL ? 1 : 0;
        Preflight->Started =
            Preflight->PnpState == AMDBC250_PNP_STATE_STARTED ? 1 : 0;
        Preflight->FdoStackSize = extension->Self->StackSize;
        if (extension->LowerDeviceObject != NULL)
            Preflight->LowerStackSize =
                extension->LowerDeviceObject->StackSize;
    } else {
        Preflight->PnpState = AMDBC250_PNP_STATE_NOT_STARTED;
    }
    ExReleasePushLockShared(&g_DreamV3PnpBindingLock);
    KeLeaveCriticalRegion();

    if (Preflight->FdoCreated)
        Preflight->SafetyFlags |= AMDBC250_PNP_SAFE_FDO_CREATED;
    else
        Preflight->BlockerFlags |= AMDBC250_PNP_BLOCK_NO_FDO;
    if (Preflight->HardwareIdMatched)
        Preflight->SafetyFlags |= AMDBC250_PNP_SAFE_HARDWARE_ID_MATCH;
    else
        Preflight->BlockerFlags |=
            AMDBC250_PNP_BLOCK_HARDWARE_ID_MISMATCH;
    if (Preflight->LowerAttached)
        Preflight->SafetyFlags |= AMDBC250_PNP_SAFE_LOWER_ATTACHED;
    else
        Preflight->BlockerFlags |= AMDBC250_PNP_BLOCK_NO_LOWER_DEVICE;
    if (Preflight->Started)
        Preflight->SafetyFlags |= AMDBC250_PNP_SAFE_START_SUCCEEDED;
    else
        Preflight->BlockerFlags |= AMDBC250_PNP_BLOCK_NOT_STARTED;
    if (Preflight->PnpState == AMDBC250_PNP_STATE_REMOVE_PENDING ||
        Preflight->PnpState == AMDBC250_PNP_STATE_SURPRISE_REMOVED ||
        Preflight->PnpState == AMDBC250_PNP_STATE_DELETED)
        Preflight->BlockerFlags |= AMDBC250_PNP_BLOCK_REMOVING;

    referenceStatus = DreamV3AcquireStartedPnpPdo(&reference);
    if (NT_SUCCESS(referenceStatus) &&
        reference.PhysicalDeviceObject != NULL) {
        Preflight->PdoReferenceAcquired = 1;
        Preflight->SafetyFlags |= AMDBC250_PNP_SAFE_PDO_REFERENCED;
        DreamV3ReleasePnpPdo(&reference);
    } else {
        Preflight->BlockerFlags |=
            AMDBC250_PNP_BLOCK_PDO_REFERENCE_FAILED;
    }
}

VOID
DreamV3FillResourcePreflight(
    _Out_ PAMDBC250_IOCTL_RESOURCE_PREFLIGHT Preflight,
    _In_ ULONG DriverBuildId
    )
{
    PDREAM_V3_WDM_PNP_EXTENSION extension;

    RtlZeroMemory(Preflight, sizeof(*Preflight));
    Preflight->Version = AMDBC250_RESOURCE_PREFLIGHT_VERSION;
    Preflight->StructSize = sizeof(*Preflight);
    Preflight->DriverBuildId = DriverBuildId;
    Preflight->Status = STATUS_DEVICE_NOT_READY;
    Preflight->BlockerFlags =
        AMDBC250_RESOURCE_BLOCK_BAR_UNIDENTIFIED |
        AMDBC250_RESOURCE_BLOCK_NO_VRAM_OWNER |
        AMDBC250_RESOURCE_BLOCK_DMA_NOT_AUTHORIZED;
    Preflight->PnpState = MAXULONG; /* UNKNOWN: not sampled yet. */
    Preflight->BindingGeneration = MAXULONG;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
        Preflight->BlockerFlags |= AMDBC250_RESOURCE_BLOCK_NO_SNAPSHOT;
        return;
    }

    KeEnterCriticalRegion();
    ExAcquirePushLockShared(&g_DreamV3PnpBindingLock);
    extension = g_DreamV3PnpBinding;
    Preflight->BindingGeneration = (UINT32)InterlockedCompareExchange(
        &g_DreamV3BindingGeneration, 0, 0);
    if (extension == NULL)
        Preflight->PnpState = AMDBC250_PNP_STATE_NOT_STARTED;
    if (extension != NULL &&
        extension->Signature == DREAM_V3_PNP_EXTENSION_SIGNATURE) {
        Preflight->PnpState = (UINT32)InterlockedCompareExchange(
            &extension->State, 0, 0);
        if (Preflight->PnpState == AMDBC250_PNP_STATE_STARTED) {
            Preflight->SnapshotState =
                extension->ResourceSnapshot.SnapshotState;
            Preflight->DescriptorCount =
                extension->ResourceSnapshot.DescriptorCount;
            Preflight->MemoryCount =
                extension->ResourceSnapshot.MemoryCount;
            RtlCopyMemory(Preflight->Memory,
                extension->ResourceSnapshot.Memory,
                sizeof(Preflight->Memory));
        }
    }
    ExReleasePushLockShared(&g_DreamV3PnpBindingLock);
    KeLeaveCriticalRegion();

    if (Preflight->SnapshotState == AMDBC250_RESOURCE_SNAPSHOT_REJECTED)
        Preflight->BlockerFlags |= AMDBC250_RESOURCE_BLOCK_REJECTED;
    else if (Preflight->SnapshotState !=
        AMDBC250_RESOURCE_SNAPSHOT_VALID)
        Preflight->BlockerFlags |= AMDBC250_RESOURCE_BLOCK_NO_SNAPSHOT;
}

/* No experimental PCI request prototype is linked into this candidate. */
VOID
DreamV3FillPciConfigPreflight(
    _Out_ PAMDBC250_IOCTL_PCI_CONFIG_PREFLIGHT Preflight,
    _In_ ULONG DriverBuildId
    )
{
    PDREAM_V3_WDM_PNP_EXTENSION extension;

    RtlZeroMemory(Preflight, sizeof(*Preflight));
    Preflight->Version = AMDBC250_PCI_CONFIG_PREFLIGHT_VERSION;
    Preflight->StructSize = sizeof(*Preflight);
    Preflight->DriverBuildId = DriverBuildId;
    Preflight->ReadStatus = STATUS_NOT_SUPPORTED;
    Preflight->BlockerFlags = AMDBC250_PCI_CONFIG_BLOCK_DMA_NOT_AUTHORIZED |
        AMDBC250_PCI_CONFIG_BLOCK_READ_DISABLED;
    Preflight->PnpState = MAXULONG; /* UNKNOWN, not NOT_STARTED. */
    Preflight->BindingGeneration = MAXULONG;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return;
    KeEnterCriticalRegion();
    ExAcquirePushLockShared(&g_DreamV3PnpBindingLock);
    Preflight->BindingGeneration = (UINT32)InterlockedCompareExchange(
        &g_DreamV3BindingGeneration, 0, 0);
    extension = g_DreamV3PnpBinding;
    if (extension == NULL)
        Preflight->PnpState = AMDBC250_PNP_STATE_NOT_STARTED;
    else if (extension->Signature == DREAM_V3_PNP_EXTENSION_SIGNATURE)
        Preflight->PnpState = (UINT32)InterlockedCompareExchange(
            &extension->State, 0, 0);
    ExReleasePushLockShared(&g_DreamV3PnpBindingLock);
    KeLeaveCriticalRegion();
}
