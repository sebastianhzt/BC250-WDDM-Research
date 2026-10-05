/* SPDX-License-Identifier: Apache-2.0 */
#include "bc250_pci_bounded_reader.h"
static BOOLEAN BoundEnabled(VOID)
{
#ifdef BC250_BOUND_READ_MOCK
    return Bc250BoundReadMockAllowed;
#else
    return FALSE;
#endif
}
static NTSTATUS BoundChecked(NTSTATUS st)
{return st==STATUS_SUCCESS || st<0?st:STATUS_DATA_ERROR;}
NTSTATUS Bc250BoundReadIdentity(const BUS_INTERFACE_STANDARD *bus,
    BC250_BOUND_CHECK before,BC250_BOUND_CHECK after,PVOID context,BC250_BOUND_RESULT *output)
{
    BC250_BOUND_RESULT local={0};NTSTATUS st;ULONG bytes;
    if(!BoundEnabled())return STATUS_NOT_SUPPORTED;
    if(output==NULL)return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(output,sizeof(*output));
    if(KeGetCurrentIrql()!=0)return STATUS_INVALID_DEVICE_STATE;
    if(bus==NULL || before==NULL || after==NULL ||
       bus->Size!=sizeof(*bus) || bus->Version!=1 ||
       bus->InterfaceReference==NULL || bus->InterfaceDereference==NULL || bus->GetBusData==NULL)
        return STATUS_INVALID_PARAMETER;
    st=BoundChecked(before(context));if(st!=STATUS_SUCCESS)return st;
    bytes=bus->GetBusData(bus->Context,PCI_WHICHSPACE_CONFIG,local.Header,0,BC250_BOUND_HEADER_BYTES);
    /* Run post-check even after short/zero/oversized reported byte count. */
    st=BoundChecked(after(context));
    if(st==STATUS_SUCCESS){
        if(bytes!=BC250_BOUND_HEADER_BYTES || local.Header[0]!=0x02 || local.Header[1]!=0x10 ||
           local.Header[2]!=0xfe || local.Header[3]!=0x13 || local.Header[11]!=3 ||
           (local.Header[14]&0x7f)!=0)st=STATUS_DATA_ERROR;
        else{local.Valid=1;local.BytesReturned=bytes;*output=local;}
    }
    RtlZeroMemory(&local,sizeof(local));return st;
}
