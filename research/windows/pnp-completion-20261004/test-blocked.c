/* SPDX-License-Identifier: Apache-2.0 */
#define BC250_PNP_COMPLETION_TYPES_MOCK 1
#define BC250_PNP_SOURCE_TYPES_MOCK 1
#define BC250_PNP_CAPTURE_TYPES_MOCK 1
#define BC250_PCI_TYPES_MOCK 1
#include <string.h>
#include <stdio.h>
#include "bc250_pnp_completion.c"
static unsigned calls;
unsigned char KeGetCurrentIrql(VOID) { ++calls;return 0; }
VOID KeInitializeSpinLock(KSPIN_LOCK *p) { (void)p;++calls; }
VOID KeAcquireSpinLock(KSPIN_LOCK *p,KIRQL *v) { (void)p;(void)v;++calls; }
VOID KeReleaseSpinLock(KSPIN_LOCK *p,KIRQL v) { (void)p;(void)v;++calls; }
NTSTATUS IoAcquireRemoveLock(PIO_REMOVE_LOCK p,PVOID v) { (void)p;(void)v;++calls;return STATUS_SUCCESS; }
VOID IoReleaseRemoveLock(PIO_REMOVE_LOCK p,PVOID v) { (void)p;(void)v;++calls; }
PIO_WORKITEM IoAllocateWorkItem(PDEVICE_OBJECT p) { (void)p;++calls;return NULL; }
VOID IoFreeWorkItem(PIO_WORKITEM p) { (void)p;++calls; }
VOID IoQueueWorkItem(PIO_WORKITEM p,PIO_WORKITEM_ROUTINE r,WORK_QUEUE_TYPE q,PVOID v) { (void)p;(void)r;(void)q;(void)v;++calls; }
VOID IoCopyCurrentIrpStackLocationToNext(PIRP p) { (void)p;++calls; }
NTSTATUS IoSetCompletionRoutineEx(PDEVICE_OBJECT d,PIRP p,PIO_COMPLETION_ROUTINE r,PVOID v,BOOLEAN a,BOOLEAN b,BOOLEAN c)
{ (void)d;(void)p;(void)r;(void)v;(void)a;(void)b;(void)c;++calls;return STATUS_SUCCESS; }
VOID IoMarkIrpPending(PIRP p) { (void)p;++calls; }
NTSTATUS IoCallDriver(PDEVICE_OBJECT d,PIRP p) { (void)d;(void)p;++calls;return STATUS_SUCCESS; }
VOID IoCompleteRequest(PIRP p,char b) { (void)p;(void)b;++calls; }
NTSTATUS Bc250SourceBegin(BC250_SOURCE *p,ULONG k,ULONG t,BC250_SOURCE_REQUEST *v)
{ (void)p;(void)k;(void)t;(void)v;++calls;return STATUS_SUCCESS; }
NTSTATUS Bc250SourceComplete(BC250_SOURCE *p,BC250_SOURCE_REQUEST *v,NTSTATUS s,const BC250_SOURCE_RESOURCES *r)
{ (void)p;(void)v;(void)s;(void)r;++calls;return STATUS_SUCCESS; }
NTSTATUS Bc250SourceClose(BC250_SOURCE *p) { (void)p;++calls;return STATUS_SUCCESS; }
int main(VOID)
{
    BC250_COMPLETION_STATUS out;BOOLEAN taken=TRUE;SIZE_T n;memset(&out,0xa5,sizeof(out));
    if(Bc250CompletionDispatch(NULL,NULL,NULL,NULL,NULL,NULL,1,0,NULL,&taken)!=STATUS_NOT_SUPPORTED || taken ||
        Bc250CompletionCallback(NULL,NULL,NULL)!=STATUS_NOT_SUPPORTED ||
        Bc250CompletionInspect(NULL,&out)!=STATUS_NOT_SUPPORTED)return 1;
    Bc250CompletionWorker(NULL,NULL);if(calls)return 1;
    for(n=0;n<sizeof(out);++n)if(((const UCHAR *)&out)[n])return 1;
    puts("PASS: native-closed completion dispatcher/callback/worker/inspect refused; zero platform/IRP calls.");return 0;
}
