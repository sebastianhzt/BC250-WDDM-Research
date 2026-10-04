/* SPDX-License-Identifier: Apache-2.0
 * The runner extracts the actual Bc250Control body into dispatch-body.inc.
 * All surrounding WDM/access/IRQL/metadata functions here are RAM FAKE APIs.
 * This does not call Windows driver APIs, open hardware or test real PnP.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../inc/amdbc250_ioctl.h"
#include "../inc/bc250_diag_policy.h"
typedef LONG BC250_FAKE_NTSTATUS;
#define NTSTATUS BC250_FAKE_NTSTATUS
/* Replace the two user-header definitions with the fixture's typed constants. */
#undef STATUS_INVALID_PARAMETER
#undef PASSIVE_LEVEL
#define STATUS_SUCCESS ((NTSTATUS)0)
#define STATUS_NOT_SUPPORTED ((NTSTATUS)0xc00000bbL)
#define STATUS_ACCESS_DENIED ((NTSTATUS)0xc0000022L)
#define STATUS_INVALID_DEVICE_STATE ((NTSTATUS)0xc0000184L)
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xc000000dL)
#define STATUS_BUFFER_TOO_SMALL ((NTSTATUS)0xc0000023L)
#define STATUS_INVALID_USER_BUFFER ((NTSTATUS)0xc00000e8L)
#define NT_SUCCESS(x) ((NTSTATUS)(x)>=0)
#define PASSIVE_LEVEL 0U
typedef void *PDEVICE_OBJECT;
typedef struct {
    struct { struct { ULONG IoControlCode, InputBufferLength, OutputBufferLength; } DeviceIoControl; } Parameters;
} IO_STACK_LOCATION, *PIO_STACK_LOCATION;
typedef struct {
    struct { PVOID SystemBuffer; } AssociatedIrp;
    IO_STACK_LOCATION Stack;
    NTSTATUS Status;
    ULONG Bytes;
} IRP, *PIRP;
static int normal, research, unknown;
static PDEVICE_OBJECT g_Normal = &normal, g_Research = &research;
static NTSTATUS accessStatus;
static ULONG irql, fills, forwards, checks;
static PIO_STACK_LOCATION IoGetCurrentIrpStackLocation(PIRP irp) { return &irp->Stack; }
static NTSTATUS IoValidateDeviceIoControlAccess(PIRP irp, ULONG requested) {
    (void)irp; return requested == FILE_READ_ACCESS ? accessStatus : STATUS_ACCESS_DENIED;
}
static ULONG KeGetCurrentIrql(void) { return irql; }
static NTSTATUS Bc250Complete(PIRP irp, NTSTATUS status, ULONG bytes) {
    irp->Status=status; irp->Bytes=bytes; return status;
}
static NTSTATUS Bc250Pass(PDEVICE_OBJECT device, PIRP irp) {
    (void)device; ++forwards; return Bc250Complete(irp,STATUS_NOT_SUPPORTED,0);
}
static void Fill(void *p, unsigned size) { ++fills; memset(p,0x42,size); }
static void Bc250Pnp(PAMDBC250_IOCTL_PNP_PREFLIGHT p) { Fill(p,sizeof(*p)); }
static void Bc250W2p(PAMDBC250_IOCTL_W2P_PREFLIGHT p) { Fill(p,sizeof(*p)); }
static void DreamV3FillResourcePreflight(PAMDBC250_IOCTL_RESOURCE_PREFLIGHT p, ULONG build) {
    (void)build; Fill(p,sizeof(*p));
}
static void DreamV3FillPciConfigPreflight(PAMDBC250_IOCTL_PCI_CONFIG_PREFLIGHT p, ULONG build) {
    (void)build; Fill(p,sizeof(*p));
}
#include "dispatch-body.inc"
#define CHECK(x) do { ++checks; if (!(x)) { printf("FAIL dispatch line %d\n",__LINE__); return 1; } } while (0)
int main(void)
{
    const ULONG codes[]={IOCTL_AMDBC250_PNP_PREFLIGHT,IOCTL_AMDBC250_W2P_PREFLIGHT,
        IOCTL_AMDBC250_RESOURCE_PREFLIGHT,IOCTL_AMDBC250_PCI_CONFIG_PREFLIGHT,0,0xffffffffU,0x80000b80U};
    ULONG endpoint, ci, access, level, input, pointer, lenCase, size, index;
    unsigned char data[1024];
    IRP irp;
    NTSTATUS expected, actual;
    for(endpoint=1;endpoint<=2;++endpoint) for(ci=0;ci<sizeof(codes)/sizeof(codes[0]);++ci)
    for(access=0;access<2;++access) for(level=0;level<2;++level) for(input=0;input<2;++input)
    for(pointer=0;pointer<2;++pointer) for(lenCase=0;lenCase<4;++lenCase) {
        size=Bc250DiagSize(Bc250DiagRoute(endpoint,codes[ci]));
        memset(&irp,0,sizeof(irp)); memset(data,0xcc,sizeof(data));
        fills=forwards=0; accessStatus=access?STATUS_ACCESS_DENIED:STATUS_SUCCESS;
        irql=level?2U:0U;
        irp.Stack.Parameters.DeviceIoControl.IoControlCode=codes[ci];
        irp.Stack.Parameters.DeviceIoControl.InputBufferLength=input;
        irp.Stack.Parameters.DeviceIoControl.OutputBufferLength=lenCase==0?0:lenCase==1?(size?size-1:1):lenCase==2?size:size+1;
        irp.AssociatedIrp.SystemBuffer=pointer?data:NULL;
        expected=!size?STATUS_NOT_SUPPORTED:access?STATUS_ACCESS_DENIED:level?STATUS_INVALID_DEVICE_STATE:
            input?STATUS_INVALID_PARAMETER:irp.Stack.Parameters.DeviceIoControl.OutputBufferLength<size?STATUS_BUFFER_TOO_SMALL:
            !pointer?STATUS_INVALID_USER_BUFFER:STATUS_SUCCESS;
        actual=Bc250Control(endpoint==1?g_Normal:g_Research,&irp);
        CHECK(actual==expected); CHECK(irp.Status==expected); CHECK(forwards==0);
        CHECK(irp.Bytes==(expected==STATUS_SUCCESS?size:0)); CHECK(fills==(expected==STATUS_SUCCESS?1U:0U));
        for(index=0;index<sizeof(data);++index)
            CHECK(data[index]==(expected==STATUS_SUCCESS && index<size?0x42:0xcc));
    }
    fills=forwards=0; memset(&irp,0,sizeof(irp));
    CHECK(Bc250Control(&unknown,&irp)==STATUS_NOT_SUPPORTED); CHECK(forwards==1); CHECK(fills==0);
    printf("PASS: %lu extracted-dispatch checks; all Windows services are RAM fakes.\n",checks);
    return 0;
}
