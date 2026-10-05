/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BC250_MOCK_COMPLETION_WDM_H
#define BC250_MOCK_COMPLETION_WDM_H
#include "../pnp-source-20261004/mock-source-wdm.h"
#ifndef _Use_decl_annotations_
#define _Use_decl_annotations_
#endif
#define STATUS_MORE_PROCESSING_REQUIRED ((NTSTATUS)0xC0000016U)
#define IO_NO_INCREMENT 0
typedef struct { NTSTATUS Status;ULONG_PTR Information; } IO_STATUS_BLOCK;
typedef struct _IRP IRP,*PIRP;
typedef NTSTATUS IO_COMPLETION_ROUTINE(PDEVICE_OBJECT,PIRP,PVOID);
typedef IO_COMPLETION_ROUTINE *PIO_COMPLETION_ROUTINE;
typedef VOID IO_WORKITEM_ROUTINE(PDEVICE_OBJECT,PVOID);
typedef IO_WORKITEM_ROUTINE *PIO_WORKITEM_ROUTINE;
typedef struct _IO_REMOVE_LOCK { ULONG Count,RejectAt,Attempts;PVOID Tags[8]; } IO_REMOVE_LOCK,*PIO_REMOVE_LOCK;
typedef struct _IO_WORKITEM { ULONG Allocated,Queued;PDEVICE_OBJECT Device;PIO_WORKITEM_ROUTINE Routine;PVOID Context; } IO_WORKITEM,*PIO_WORKITEM;
struct _IRP {
    IO_STATUS_BLOCK IoStatus;
    BOOLEAN PendingReturned;
    ULONG Marked,Completed,StackCopied;
    PIO_COMPLETION_ROUTINE Routine;
    PVOID Context;
};
typedef enum { DelayedWorkQueue } WORK_QUEUE_TYPE;
NTSTATUS IoAcquireRemoveLock(PIO_REMOVE_LOCK,PVOID);
VOID IoReleaseRemoveLock(PIO_REMOVE_LOCK,PVOID);
PIO_WORKITEM IoAllocateWorkItem(PDEVICE_OBJECT);
VOID IoFreeWorkItem(PIO_WORKITEM);
VOID IoQueueWorkItem(PIO_WORKITEM,PIO_WORKITEM_ROUTINE,WORK_QUEUE_TYPE,PVOID);
VOID IoCopyCurrentIrpStackLocationToNext(PIRP);
NTSTATUS IoSetCompletionRoutineEx(PDEVICE_OBJECT,PIRP,PIO_COMPLETION_ROUTINE,PVOID,BOOLEAN,BOOLEAN,BOOLEAN);
VOID IoMarkIrpPending(PIRP);
NTSTATUS IoCallDriver(PDEVICE_OBJECT,PIRP);
VOID IoCompleteRequest(PIRP,char);
#endif
