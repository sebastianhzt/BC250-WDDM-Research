/* SPDX-License-Identifier: Apache-2.0 */
/* Reduced serial RAM types, NOT Windows ABI/completion/scheduler semantics. */
#ifndef BC250_MOCK_QUERY_WDM_H
#define BC250_MOCK_QUERY_WDM_H
#include "../pci-pnp-admission-20261004/mock-admission-wdm.h"
#define IRP_MJ_PNP 0x1b
#define IRP_MN_QUERY_INTERFACE 0x08
typedef struct {ULONG Data1;USHORT Data2,Data3;UCHAR Data4[8];} GUID;
static const GUID GUID_BUS_INTERFACE_STANDARD={0x496b8280U,0x6f25,0x11d0,{0xbe,0xaf,8,0,0x2b,0xe2,9,0x2f}};
typedef struct {ULONG Signal;} KEVENT,*PKEVENT;
typedef enum {NotificationEvent} EVENT_TYPE;
typedef struct {NTSTATUS Status;ULONG_PTR Information;} IO_STATUS_BLOCK,*PIO_STATUS_BLOCK;
typedef BUS_INTERFACE_STANDARD *PINTERFACE;
typedef struct {
    UCHAR MajorFunction,MinorFunction;
    union {struct {const GUID *InterfaceType;USHORT Size,Version;PINTERFACE Interface;PVOID InterfaceSpecificData;} QueryInterface;} Parameters;
} IO_STACK_LOCATION,*PIO_STACK_LOCATION;
typedef struct {IO_STATUS_BLOCK IoStatus;IO_STACK_LOCATION Stack;PKEVENT Event;PIO_STATUS_BLOCK Final;} IRP,*PIRP;
VOID KeInitializeEvent(PKEVENT,EVENT_TYPE,BOOLEAN);
PIRP IoBuildSynchronousFsdRequest(ULONG,PDEVICE_OBJECT,PVOID,ULONG,PLARGE_INTEGER,PKEVENT,PIO_STATUS_BLOCK);
PIO_STACK_LOCATION IoGetNextIrpStackLocation(PIRP);
NTSTATUS IoCallDriver(PDEVICE_OBJECT,PIRP);
extern BOOLEAN Bc250BusQueryMockAllowed;
#endif
