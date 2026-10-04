/* SPDX-License-Identifier: Apache-2.0
 * Actual control source with RAM-only platform doubles. No WDK/OS I/O calls.
 * Successful mocks do not validate Windows security, lifetime or installation.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <wchar.h>
#define BC250_CONTROL_PLATFORM_H

typedef int NTSTATUS;
typedef unsigned long ULONG;
typedef uintptr_t ULONG_PTR;
typedef unsigned char BOOLEAN;
typedef wchar_t WCHAR;
typedef void VOID;
#define TRUE 1
#define FALSE 0
#define STATUS_SUCCESS 0
#define STATUS_INVALID_PARAMETER (-1)
#define STATUS_INVALID_DEVICE_REQUEST (-2)
#define STATUS_DEVICE_NOT_READY (-3)
#define STATUS_NOT_A_DIRECTORY (-4)
#define STATUS_BUFFER_TOO_SMALL (-5)
#define STATUS_INVALID_DEVICE_STATE (-6)
#define STATUS_INSUFFICIENT_RESOURCES (-7)
#define STATUS_OBJECT_NAME_COLLISION (-8)
#define STATUS_UNSUCCESSFUL (-9)
#define NT_SUCCESS(status) ((status) >= 0)
#define PASSIVE_LEVEL 0U
#define IO_NO_INCREMENT 0
#define IRP_MJ_CREATE 0U
#define IRP_MJ_CLOSE 2U
#define IRP_MJ_DEVICE_CONTROL 14U
#define IRP_MJ_CLEANUP 18U
#define IRP_MJ_MAXIMUM_FUNCTION 27U
#define FILE_DIRECTORY_FILE 1U
#define FILE_DEVICE_SECURE_OPEN 0x100U
#define DO_BUFFERED_IO 4U
#define DO_DEVICE_INITIALIZING 0x80U
#define UNREFERENCED_PARAMETER(value) ((void)(value))
#define RtlZeroMemory(buffer, size) memset(buffer, 0, size)
typedef struct { unsigned short Length, MaximumLength; WCHAR *Buffer; } UNICODE_STRING, *PUNICODE_STRING;
typedef struct { unsigned long Data1; unsigned short Data2, Data3; unsigned char Data4[8]; } GUID;
typedef struct FILE_OBJECT { UNICODE_STRING FileName; } FILE_OBJECT;
typedef struct IO_STACK_LOCATION {
    unsigned char MajorFunction;
    FILE_OBJECT *FileObject;
    union {
        struct { ULONG Options; } Create;
        struct { ULONG OutputBufferLength, InputBufferLength, IoControlCode; } DeviceIoControl;
    } Parameters;
} IO_STACK_LOCATION, *PIO_STACK_LOCATION;
typedef struct IRP {
    struct { NTSTATUS Status; ULONG_PTR Information; } IoStatus;
    struct { void *SystemBuffer; } AssociatedIrp;
    IO_STACK_LOCATION Stack;
    unsigned int Completed;
} IRP, *PIRP;
typedef struct DEVICE_OBJECT DEVICE_OBJECT, *PDEVICE_OBJECT;
typedef struct DRIVER_OBJECT DRIVER_OBJECT, *PDRIVER_OBJECT;
typedef NTSTATUS (*DISPATCH)(PDEVICE_OBJECT, PIRP);
typedef VOID (*UNLOAD)(PDRIVER_OBJECT);
struct DRIVER_OBJECT {
    PDEVICE_OBJECT DeviceObject;
    DISPATCH MajorFunction[IRP_MJ_MAXIMUM_FUNCTION + 1];
    UNLOAD DriverUnload;
};
struct DEVICE_OBJECT {
    PDEVICE_OBJECT NextDevice;
    void *DeviceExtension;
    ULONG Flags;
    PDRIVER_OBJECT Owner;
};

static unsigned int checks;
#define CHECK(test) do { ++checks; if (!(test)) { fprintf(stderr, "FAIL %u: %s\n", (unsigned int)__LINE__, #test); exit(1); } } while (0)
static unsigned int irql, create_calls, link_calls, unlink_calls, delete_calls, completions;
static NTSTATUS create_status, link_status, unlink_status;
static BOOLEAN link_exists;
static PDEVICE_OBJECT live;
static PDRIVER_OBJECT creating_driver;
static char events[64];
static unsigned int event_count;
static NTSTATUS Bc250ControlReject(PDEVICE_OBJECT, PIRP);
static NTSTATUS Bc250ControlCreateClose(PDEVICE_OBJECT, PIRP);
static NTSTATUS Bc250ControlDeviceControl(PDEVICE_OBJECT, PIRP);
static VOID Bc250ControlUnload(PDRIVER_OBJECT);
static void CheckPublishingState(void);
static void CheckDestroyingState(PDEVICE_OBJECT);
static void Event(char event)
{
    CHECK(event_count < sizeof(events) - 1);
    events[event_count++] = event;
    events[event_count] = 0;
}
static unsigned int KeGetCurrentIrql(void) { return irql; }
static void RtlInitUnicodeString(PUNICODE_STRING target, const WCHAR *source)
{
    target->Buffer = (WCHAR *)source;
    target->Length = (unsigned short)(wcslen(source) * sizeof(WCHAR));
    target->MaximumLength = (unsigned short)(target->Length + sizeof(WCHAR));
}
static PIO_STACK_LOCATION IoGetCurrentIrpStackLocation(PIRP irp)
{
    CHECK(irp != NULL);
    return &irp->Stack;
}
static void IoCompleteRequest(PIRP irp, int increment)
{
    CHECK(irp != NULL && increment == IO_NO_INCREMENT && irp->Completed == 0);
    ++irp->Completed;
    ++completions;
}
static NTSTATUS IoCreateDeviceSecure(PDRIVER_OBJECT driver, ULONG extension_size,
    PUNICODE_STRING name, ULONG device_type, ULONG characteristics, BOOLEAN exclusive,
    PUNICODE_STRING sddl, const GUID *class_guid, PDEVICE_OBJECT *device)
{
    ULONG index;
    ++create_calls;
    Event('C');
    CHECK(driver != NULL && device != NULL && *device == NULL && live == NULL);
    CHECK(driver->DriverUnload == Bc250ControlUnload);
    CHECK(wcscmp(name->Buffer, L"\\Device\\BC250ResearchControlV1") == 0);
    CHECK(wcscmp(sddl->Buffer, L"D:P(A;;GA;;;SY)(A;;GA;;;BA)") == 0);
    CHECK(device_type == 0x8000U && characteristics == FILE_DEVICE_SECURE_OPEN && !exclusive);
    CHECK(class_guid != NULL && class_guid->Data1 == 0xe27f9606UL);
    for (index = 0; index <= IRP_MJ_MAXIMUM_FUNCTION; ++index) {
        DISPATCH expected = Bc250ControlReject;
        if (index == IRP_MJ_CREATE || index == IRP_MJ_CLOSE || index == IRP_MJ_CLEANUP) expected = Bc250ControlCreateClose;
        if (index == IRP_MJ_DEVICE_CONTROL) expected = Bc250ControlDeviceControl;
        CHECK(driver->MajorFunction[index] == expected);
    }
    if (!NT_SUCCESS(create_status)) return create_status;
    live = (PDEVICE_OBJECT)calloc(1, sizeof(*live));
    CHECK(live != NULL);
    live->DeviceExtension = malloc(extension_size);
    CHECK(live->DeviceExtension != NULL);
    memset(live->DeviceExtension, 0xA5, extension_size);
    live->Flags = DO_DEVICE_INITIALIZING;
    live->Owner = driver;
    live->NextDevice = driver->DeviceObject;
    driver->DeviceObject = live;
    creating_driver = driver;
    *device = live;
    return STATUS_SUCCESS;
}
static NTSTATUS IoCreateSymbolicLink(PUNICODE_STRING link, PUNICODE_STRING target)
{
    ++link_calls;
    Event('L');
    CHECK(wcscmp(link->Buffer, L"\\DosDevices\\BC250ResearchControlV1") == 0);
    CHECK(wcscmp(target->Buffer, L"\\Device\\BC250ResearchControlV1") == 0);
    CheckPublishingState();
    if (NT_SUCCESS(link_status)) link_exists = TRUE;
    return link_status;
}
static NTSTATUS IoDeleteSymbolicLink(PUNICODE_STRING link)
{
    ++unlink_calls;
    Event('U');
    CHECK(wcscmp(link->Buffer, L"\\DosDevices\\BC250ResearchControlV1") == 0 && link_exists);
    if (NT_SUCCESS(unlink_status)) link_exists = FALSE;
    return unlink_status;
}
static void IoDeleteDevice(PDEVICE_OBJECT device)
{
    PDEVICE_OBJECT *slot;
    CHECK(device != NULL && device == live);
    CheckDestroyingState(device);
    ++delete_calls;
    Event('D');
    slot = &device->Owner->DeviceObject;
    while (*slot && *slot != device) slot = &(*slot)->NextDevice;
    CHECK(*slot == device);
    *slot = device->NextDevice;
    free(device->DeviceExtension);
    free(device);
    live = NULL;
}

/* Platform guard above removes only the WDK include, not the real GPU policy. */
#include "bc250_control.c"

static void CheckPublishingState(void)
{
    BC250_CONTROL_EXTENSION *extension = (BC250_CONTROL_EXTENSION *)live->DeviceExtension;
    CHECK(creating_driver->DriverUnload == Bc250ControlUnload);
    CHECK((live->Flags & (DO_DEVICE_INITIALIZING | DO_BUFFERED_IO)) == (DO_DEVICE_INITIALIZING | DO_BUFFERED_IO));
    CHECK(extension->Signature == BC250_CONTROL_SIGNATURE && !extension->Ready && !extension->LinkCreated);
    CHECK(!Bc250ControlIsReady(live));
}
static void CheckDestroyingState(PDEVICE_OBJECT device)
{
    BC250_CONTROL_EXTENSION *extension = (BC250_CONTROL_EXTENSION *)device->DeviceExtension;
    CHECK(!extension->Ready && !extension->LinkCreated && extension->Signature == 0);
}
static void Reset(void)
{
    CHECK(live == NULL);
    irql = create_calls = link_calls = unlink_calls = delete_calls = completions = event_count = 0;
    create_status = link_status = unlink_status = STATUS_SUCCESS;
    creating_driver = NULL;
    link_exists = FALSE;
    memset(events, 0, sizeof(events));
}
static IRP NewIrp(ULONG major)
{
    IRP irp;
    memset(&irp, 0, sizeof(irp));
    irp.Stack.MajorFunction = (unsigned char)major;
    irp.IoStatus.Status = 123;
    irp.IoStatus.Information = ~(ULONG_PTR)0;
    return irp;
}
static void Completed(const IRP *irp, NTSTATUS status, ULONG_PTR information)
{
    CHECK(irp->Completed == 1 && irp->IoStatus.Status == status && irp->IoStatus.Information == information);
}
static void TestEntry(void)
{
    DRIVER_OBJECT driver, saved;
    DEVICE_OBJECT foreign;
    Reset(); memset(&driver, 0, sizeof(driver));
    CHECK(DriverEntry(NULL, NULL) == STATUS_INVALID_PARAMETER && create_calls == 0);
    saved = driver;
    irql = 1;
    CHECK(DriverEntry(&driver, NULL) == STATUS_INVALID_DEVICE_STATE);
    CHECK(memcmp(&driver, &saved, sizeof(driver)) == 0 && create_calls == 0);
    irql = 0; memset(&foreign, 0xA5, sizeof(foreign));
    driver.DeviceObject = &foreign; saved = driver;
    CHECK(DriverEntry(&driver, NULL) == STATUS_INVALID_DEVICE_STATE);
    CHECK(memcmp(&driver, &saved, sizeof(driver)) == 0 && create_calls == 0 && delete_calls == 0);
    memset(&driver, 0, sizeof(driver)); create_status = STATUS_INSUFFICIENT_RESOURCES;
    CHECK(DriverEntry(&driver, NULL) == create_status);
    CHECK(create_calls == 1 && link_calls == 0 && delete_calls == 0 && driver.DeviceObject == NULL);
    driver.DriverUnload(&driver); CHECK(delete_calls == 0);
    Reset(); create_status = STATUS_OBJECT_NAME_COLLISION;
    CHECK(DriverEntry(&driver, NULL) == STATUS_OBJECT_NAME_COLLISION);
    CHECK(create_calls == 1 && link_calls == 0 && delete_calls == 0 && driver.DeviceObject == NULL);
    Reset(); link_status = STATUS_OBJECT_NAME_COLLISION;
    CHECK(DriverEntry(&driver, NULL) == STATUS_OBJECT_NAME_COLLISION);
    CHECK(strcmp(events, "CLD") == 0 && !link_exists && unlink_calls == 0 && live == NULL && driver.DeviceObject == NULL);
    driver.DriverUnload(&driver); CHECK(delete_calls == 1);
    Reset(); CHECK(DriverEntry(&driver, NULL) == STATUS_SUCCESS);
    CHECK(strcmp(events, "CL") == 0 && Bc250ControlIsReady(live) && link_exists);
    CHECK(!(live->Flags & DO_DEVICE_INITIALIZING));
    driver.DriverUnload(&driver);
    CHECK(strcmp(events, "CLUD") == 0 && !link_exists && driver.DeviceObject == NULL);
    driver.DriverUnload(&driver); Bc250ControlUnload(NULL);
    CHECK(unlink_calls == 1 && delete_calls == 1);
}
static void TestCreateAndDefaults(void)
{
    DRIVER_OBJECT driver;
    FILE_OBJECT file;
    IRP irp;
    ULONG index;
    BC250_CONTROL_EXTENSION *extension;
    Reset(); memset(&driver, 0, sizeof(driver)); memset(&file, 0, sizeof(file));
    CHECK(DriverEntry(&driver, NULL) == STATUS_SUCCESS);
    for (index = 0; index <= IRP_MJ_MAXIMUM_FUNCTION; ++index) {
        if (index == IRP_MJ_CREATE || index == IRP_MJ_CLOSE || index == IRP_MJ_CLEANUP || index == IRP_MJ_DEVICE_CONTROL) continue;
        irp = NewIrp(index);
        CHECK(driver.MajorFunction[index](live, &irp) == STATUS_INVALID_DEVICE_REQUEST);
        Completed(&irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    }
    CHECK(Bc250ControlReject(NULL, NULL) == STATUS_INVALID_PARAMETER);
    CHECK(Bc250ControlCreateClose(live, NULL) == STATUS_INVALID_PARAMETER);
    irp = NewIrp(IRP_MJ_CREATE);
    CHECK(Bc250ControlCreateClose(live, &irp) == STATUS_INVALID_PARAMETER); Completed(&irp, STATUS_INVALID_PARAMETER, 0);
    irp = NewIrp(IRP_MJ_CREATE); irp.Stack.FileObject = &file;
    CHECK(Bc250ControlCreateClose(live, &irp) == STATUS_SUCCESS); Completed(&irp, STATUS_SUCCESS, 0);
    file.FileName.Length = 2;
    irp = NewIrp(IRP_MJ_CREATE); irp.Stack.FileObject = &file;
    CHECK(Bc250ControlCreateClose(live, &irp) == STATUS_INVALID_PARAMETER); Completed(&irp, STATUS_INVALID_PARAMETER, 0);
    file.FileName.Length = 0;
    irp = NewIrp(IRP_MJ_CREATE); irp.Stack.FileObject = &file; irp.Stack.Parameters.Create.Options = FILE_DIRECTORY_FILE;
    CHECK(Bc250ControlCreateClose(live, &irp) == STATUS_NOT_A_DIRECTORY); Completed(&irp, STATUS_NOT_A_DIRECTORY, 0);
    irp = NewIrp(IRP_MJ_CREATE); irp.Stack.FileObject = &file;
    CHECK(Bc250ControlCreateClose(NULL, &irp) == STATUS_DEVICE_NOT_READY); Completed(&irp, STATUS_DEVICE_NOT_READY, 0);
    extension = (BC250_CONTROL_EXTENSION *)live->DeviceExtension; extension->Ready = FALSE;
    irp = NewIrp(IRP_MJ_CREATE); irp.Stack.FileObject = &file;
    CHECK(Bc250ControlCreateClose(live, &irp) == STATUS_DEVICE_NOT_READY); Completed(&irp, STATUS_DEVICE_NOT_READY, 0);
    for (index = 0; index < 2; ++index) {
        irp = NewIrp(index == 0 ? IRP_MJ_CLOSE : IRP_MJ_CLEANUP);
        CHECK(Bc250ControlCreateClose(NULL, &irp) == STATUS_SUCCESS); Completed(&irp, STATUS_SUCCESS, 0);
    }
    irp = NewIrp(3);
    CHECK(Bc250ControlCreateClose(live, &irp) == STATUS_INVALID_DEVICE_REQUEST); Completed(&irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    extension->Ready = TRUE;
    driver.DriverUnload(&driver);
}
static void Query(PDEVICE_OBJECT device, ULONG code, ULONG input, ULONG output, void *buffer,
    NTSTATUS expected, ULONG_PTR information)
{
    IRP irp = NewIrp(IRP_MJ_DEVICE_CONTROL);
    unsigned int before = completions;
    irp.Stack.Parameters.DeviceIoControl.IoControlCode = code;
    irp.Stack.Parameters.DeviceIoControl.InputBufferLength = input;
    irp.Stack.Parameters.DeviceIoControl.OutputBufferLength = output;
    irp.AssociatedIrp.SystemBuffer = buffer;
    CHECK(Bc250ControlDeviceControl(device, &irp) == expected);
    Completed(&irp, expected, information);
    CHECK(completions == before + 1);
}
static void TestQuery(void)
{
    DRIVER_OBJECT driver;
    DEVICE_OBJECT invalid;
    BC250_CONTROL_EXTENSION bad;
    BC250_CONTROL_METADATA expected;
    union { uint64_t Align; unsigned char Bytes[80]; } buffer, saved;
    unsigned int output, method;
    volatile unsigned int runtime_policy = BC250_PASSIVE_GPU_RUNTIME_ENABLED;
    Reset(); memset(&driver, 0, sizeof(driver));
    CHECK(DriverEntry(&driver, NULL) == STATUS_SUCCESS);
    CHECK(sizeof(expected) == 32 && runtime_policy == 0);
    memset(&expected, 0, sizeof(expected));
    expected.SizeBytes = (unsigned int)sizeof(expected); expected.AbiVersion = BC250_CONTROL_ABI_VERSION;
    expected.PolicyVersion = BC250_PASSIVE_PORT_POLICY_VERSION;
    CHECK(Bc250ControlDeviceControl(live, NULL) == STATUS_INVALID_PARAMETER);
    memset(&buffer, 0xA5, sizeof(buffer)); saved = buffer;
    Query(NULL, BC250_CONTROL_QUERY_METADATA, 0, 32, buffer.Bytes + 8, STATUS_DEVICE_NOT_READY, 0);
    memset(&invalid, 0, sizeof(invalid));
    Query(&invalid, BC250_CONTROL_QUERY_METADATA, 0, 32, buffer.Bytes + 8, STATUS_DEVICE_NOT_READY, 0);
    memset(&bad, 0, sizeof(bad)); invalid.DeviceExtension = &bad;
    Query(&invalid, BC250_CONTROL_QUERY_METADATA, 0, 32, buffer.Bytes + 8, STATUS_DEVICE_NOT_READY, 0);
    bad.Signature = BC250_CONTROL_SIGNATURE;
    Query(&invalid, BC250_CONTROL_QUERY_METADATA, 0, 32, buffer.Bytes + 8, STATUS_DEVICE_NOT_READY, 0);
    bad.Signature = BC250_CONTROL_SIGNATURE; bad.Ready = TRUE; invalid.Flags = DO_DEVICE_INITIALIZING;
    Query(&invalid, BC250_CONTROL_QUERY_METADATA, 0, 32, buffer.Bytes + 8, STATUS_DEVICE_NOT_READY, 0);
    for (method = 1; method <= 3; ++method)
        Query(live, BC250_CONTROL_QUERY_METADATA | method, 0, 32, buffer.Bytes + 8, STATUS_INVALID_DEVICE_REQUEST, 0);
    Query(live, BC250_CONTROL_QUERY_METADATA ^ (1U << 14), 0, 32, buffer.Bytes + 8, STATUS_INVALID_DEVICE_REQUEST, 0);
    Query(live, BC250_CONTROL_QUERY_METADATA ^ (1U << 15), 0, 32, buffer.Bytes + 8, STATUS_INVALID_DEVICE_REQUEST, 0);
    Query(live, BC250_CONTROL_QUERY_METADATA ^ (1U << 16), 0, 32, buffer.Bytes + 8, STATUS_INVALID_DEVICE_REQUEST, 0);
    Query(live, BC250_CONTROL_QUERY_METADATA ^ 4U, 0, 32, buffer.Bytes + 8, STATUS_INVALID_DEVICE_REQUEST, 0);
    Query(live, BC250_CONTROL_QUERY_METADATA, 1, 32, buffer.Bytes + 8, STATUS_INVALID_PARAMETER, 0);
    Query(live, BC250_CONTROL_QUERY_METADATA, UINT32_MAX, 32, buffer.Bytes + 8, STATUS_INVALID_PARAMETER, 0);
    for (output = 0; output < 32; ++output)
        Query(live, BC250_CONTROL_QUERY_METADATA, 0, output, buffer.Bytes + 8, STATUS_BUFFER_TOO_SMALL, 0);
    Query(live, BC250_CONTROL_QUERY_METADATA, 0, 32, NULL, STATUS_INVALID_PARAMETER, 0);
    CHECK(memcmp(&buffer, &saved, sizeof(buffer)) == 0);
    for (output = 32; output <= 64; ++output) {
        memset(&buffer, 0xA5, sizeof(buffer)); saved = buffer;
        Query(live, BC250_CONTROL_QUERY_METADATA, 0, output, buffer.Bytes + 8, STATUS_SUCCESS, 32);
        CHECK(memcmp(buffer.Bytes + 8, &expected, 32) == 0);
        CHECK(memcmp(buffer.Bytes, saved.Bytes, 8) == 0 && memcmp(buffer.Bytes + 40, saved.Bytes + 40, 40) == 0);
    }
    memset(&buffer, 0xA5, sizeof(buffer)); saved = buffer;
    Query(live, BC250_CONTROL_QUERY_METADATA, 0, UINT32_MAX, buffer.Bytes + 8, STATUS_SUCCESS, 32);
    CHECK(memcmp(buffer.Bytes + 8, &expected, 32) == 0);
    CHECK(memcmp(buffer.Bytes, saved.Bytes, 8) == 0 && memcmp(buffer.Bytes + 40, saved.Bytes + 40, 40) == 0);
    driver.DriverUnload(&driver);
}
static void TestOwnCleanupAndGap(void)
{
    DRIVER_OBJECT driver;
    DEVICE_OBJECT foreign, empty;
    BC250_CONTROL_EXTENSION other;
    Reset(); memset(&driver, 0, sizeof(driver)); memset(&foreign, 0, sizeof(foreign)); memset(&other, 0, sizeof(other));
    CHECK(DriverEntry(&driver, NULL) == STATUS_SUCCESS);
    /* Unknown objects are not eligible; this fixture injects one into a mock chain. */
    foreign.DeviceExtension = &other; foreign.NextDevice = live; foreign.Owner = &driver;
    driver.DeviceObject = &foreign;
    Bc250ControlDestroyOwnDevice(NULL);
    memset(&empty, 0, sizeof(empty));
    Bc250ControlDestroyOwnDevice(&empty);
    Bc250ControlDestroyOwnDevice(&foreign);
    CHECK(delete_calls == 0 && unlink_calls == 0);
    driver.DriverUnload(&driver);
    CHECK(driver.DeviceObject == &foreign && foreign.NextDevice == NULL && delete_calls == 1 && unlink_calls == 1);
    driver.DriverUnload(&driver); CHECK(delete_calls == 1 && unlink_calls == 1);
    driver.DeviceObject = NULL;
    Reset(); CHECK(DriverEntry(&driver, NULL) == STATUS_SUCCESS);
    unlink_status = STATUS_UNSUCCESSFUL;
    driver.DriverUnload(&driver);
    CHECK(link_exists && live == NULL && driver.DeviceObject == NULL && unlink_calls == 1 && delete_calls == 1);
    driver.DriverUnload(&driver); CHECK(link_exists && unlink_calls == 1 && delete_calls == 1);
    /* Residual mock link is deliberately observed, not reported as cleaned up. */
    printf("GAP: simulated OS unlink failure leaves a residual link; no runtime cleanup claim\n");
    Reset();
}
int main(void)
{
    TestEntry(); TestCreateAndDefaults(); TestQuery(); TestOwnCleanupAndGap();
    printf("PASS: %u actual-control-source RAM assertions; no hardware or OS driver calls\n", checks);
    return 0;
}
