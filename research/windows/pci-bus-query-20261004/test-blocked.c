/* SPDX-License-Identifier: Apache-2.0 */
#define BC250_BUS_QUERY_TYPES_MOCK 1
#include <stdio.h>
#include <string.h>
#include "bc250_pci_bus_query.c"
static unsigned platform;
UCHAR KeGetCurrentIrql(VOID){++platform;return 0;}
VOID KeInitializeEvent(PKEVENT e,EVENT_TYPE t,BOOLEAN b){(void)e;(void)t;(void)b;++platform;}
PIRP IoBuildSynchronousFsdRequest(ULONG a,PDEVICE_OBJECT b,PVOID c,ULONG d,PLARGE_INTEGER e,PKEVENT f,PIO_STATUS_BLOCK g)
{(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;++platform;return NULL;}
PIO_STACK_LOCATION IoGetNextIrpStackLocation(PIRP p){(void)p;++platform;return NULL;}
NTSTATUS IoCallDriver(PDEVICE_OBJECT p,PIRP i){(void)p;(void)i;++platform;return STATUS_NOT_SUPPORTED;}
NTSTATUS KeWaitForSingleObject(PVOID a,KWAIT_REASON b,KPROCESSOR_MODE c,BOOLEAN d,PLARGE_INTEGER e)
{(void)a;(void)b;(void)c;(void)d;(void)e;++platform;return STATUS_NOT_SUPPORTED;}
NTSTATUS Bc250BoundReadIdentity(const BUS_INTERFACE_STANDARD *a,BC250_BOUND_CHECK b,BC250_BOUND_CHECK c,PVOID d,BC250_BOUND_RESULT *e)
{(void)a;(void)b;(void)c;(void)d;(void)e;++platform;return STATUS_NOT_SUPPORTED;}
int main(VOID)
{
    BC250_BOUND_RESULT result;memset(&result,0xcc,sizeof(result));
    if(Bc250BusQueryObserve(NULL,NULL,NULL,NULL,NULL,&result)!=STATUS_NOT_SUPPORTED || platform || ((UCHAR *)&result)[0]!=0xcc)return 1;
    if(Bc250BusQueryObserve(NULL,NULL,NULL,NULL,NULL,NULL)!=STATUS_NOT_SUPPORTED || platform)return 2;
    puts("PASS: bus query native FALSE before pointers and all OS/provider calls.");return 0;
}
