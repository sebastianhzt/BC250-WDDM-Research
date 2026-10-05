/* SPDX-License-Identifier: Apache-2.0 */
#include "bc250_pci_bus_query.h"
static BOOLEAN QueryEnabled(VOID)
{
#ifdef BC250_BUS_QUERY_MOCK
    return Bc250BusQueryMockAllowed;
#else
    return FALSE;
#endif
}
static BOOLEAN QueryZero(const VOID *ptr,SIZE_T length)
{const UCHAR *p=ptr;SIZE_T i;for(i=0;i<length;++i)if(p[i])return FALSE;return TRUE;}
static NTSTATUS QueryChecked(NTSTATUS st)
{return st==STATUS_SUCCESS || st<0?st:STATUS_DATA_ERROR;}
NTSTATUS Bc250BusQueryObserve(BC250_BUS_QUERY *q,PDEVICE_OBJECT lower,
    BC250_BOUND_CHECK before,BC250_BOUND_CHECK after,PVOID context,BC250_BOUND_RESULT *output)
{
    PIRP irp;PIO_STACK_LOCATION stack;NTSTATUS st;BC250_BOUND_RESULT local={0};
    if(!QueryEnabled())return STATUS_NOT_SUPPORTED;
    if(!output)return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(output,sizeof(*output));
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL)return STATUS_INVALID_DEVICE_STATE;
    if(!q || !lower || !before || !after)return STATUS_INVALID_PARAMETER;
    if(!QueryZero(q,sizeof(*q)))return q->State==BC250_BUS_QUERY_ACTIVE?STATUS_DEVICE_BUSY:STATUS_INVALID_DEVICE_STATE;
    q->State=BC250_BUS_QUERY_ACTIVE;q->Lower=lower;
    st=QueryChecked(before(context));if(st!=STATUS_SUCCESS)goto Done;
    KeInitializeEvent(&q->Event,NotificationEvent,FALSE);q->Final.Status=STATUS_NOT_SUPPORTED;
    irp=IoBuildSynchronousFsdRequest(IRP_MJ_PNP,lower,NULL,0,NULL,&q->Event,&q->Final);
    if(!irp){st=STATUS_INSUFFICIENT_RESOURCES;goto Done;}
    stack=IoGetNextIrpStackLocation(irp);
    stack->MinorFunction=IRP_MN_QUERY_INTERFACE;
    stack->Parameters.QueryInterface.InterfaceType=(GUID *)&GUID_BUS_INTERFACE_STANDARD;
    stack->Parameters.QueryInterface.Size=sizeof(q->Bus);
    stack->Parameters.QueryInterface.Version=1;
    stack->Parameters.QueryInterface.Interface=(PINTERFACE)&q->Bus;
    stack->Parameters.QueryInterface.InterfaceSpecificData=NULL;
    irp->IoStatus.Status=STATUS_NOT_SUPPORTED;q->Sent=1;
    /* Mandatory send: no fallible operation after builder success. */
    (VOID)IoCallDriver(lower,irp); /* NEVER touch irp/stack below. */
    st=KeWaitForSingleObject(&q->Event,Executive,KernelMode,FALSE,NULL);
    if(st!=STATUS_SUCCESS)goto Unknown;
    q->Sent=0;st=q->Final.Status;
    if(st!=STATUS_SUCCESS){
        if(st>=0 || !QueryZero(&q->Bus,sizeof(q->Bus)))goto Unknown;
        goto Done;
    }
    q->InterfaceHeld=1; /* query provider, not this function, took the ref */
    if(!q->Bus.InterfaceDereference)goto Unknown;
    st=QueryChecked(after(context));
    if(st==STATUS_SUCCESS)st=Bc250BoundReadIdentity(&q->Bus,before,after,context,&local);
    q->Bus.InterfaceDereference(q->Bus.Context);q->InterfaceHeld=0;
    RtlZeroMemory(&q->Bus,sizeof(q->Bus));
    /* Catch STOP/cancel during provider cleanup too; still historical only. */
    if(st==STATUS_SUCCESS)st=QueryChecked(after(context));
Done:
    q->Lower=NULL;q->State=BC250_BUS_QUERY_DONE;
    if(st==STATUS_SUCCESS)*output=local;
    RtlZeroMemory(&local,sizeof(local));return st;
Unknown:
    q->State=BC250_BUS_QUERY_UNKNOWN;return STATUS_DATA_ERROR;
}
