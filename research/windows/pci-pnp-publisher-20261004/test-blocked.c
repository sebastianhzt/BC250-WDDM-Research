/* SPDX-License-Identifier: Apache-2.0 */
#define BC250_PCI_PUBLISHER_TYPES_MOCK 1
/* Reduced predecessor HEADER types only. Their implementations are not linked;
 * Bc250PciOwner* stubs below would count any accidental call. */
#define BC250_DMA_GATE_MOCK 1
#define BC250_PCI_TYPES_MOCK 1
#include <string.h>
#include "bc250_pci_publisher.c" /* Native FALSE policy, reduced types only. */
#include <stdio.h>
static unsigned platformCalls;
unsigned char KeGetCurrentIrql(VOID) { ++platformCalls;return 0; }
PVOID KeGetCurrentThread(VOID) { ++platformCalls;return NULL; }
SIZE_T RtlCompareMemory(const VOID *a,const VOID *c,SIZE_T n)
{ (void)a;(void)c;(void)n;++platformCalls;return 0; }
LONG InterlockedCompareExchange(volatile LONG *p,LONG v,LONG c)
{ (void)p;(void)v;(void)c;++platformCalls;return 0; }
VOID KeInitializeSpinLock(KSPIN_LOCK *p) { (void)p;++platformCalls; }
VOID KeAcquireSpinLock(KSPIN_LOCK *p,KIRQL *v) { (void)p;(void)v;++platformCalls; }
VOID KeReleaseSpinLock(KSPIN_LOCK *p,KIRQL v) { (void)p;(void)v;++platformCalls; }
NTSTATUS Bc250PciOwnerInit(BC250_PCI_OWNER *p,const BC250_PCI_OPS *v,ULONGLONG g,ULONGLONG e)
{ (void)p;(void)v;(void)g;(void)e;++platformCalls;return STATUS_NOT_SUPPORTED; }
NTSTATUS Bc250PciOwnerObserve(BC250_PCI_OWNER *p,BC250_PCI_OBSERVATION *v)
{ (void)p;(void)v;++platformCalls;return STATUS_NOT_SUPPORTED; }
NTSTATUS Bc250PciOwnerStop(BC250_PCI_OWNER *p,ULONG v)
{ (void)p;(void)v;++platformCalls;return STATUS_NOT_SUPPORTED; }
NTSTATUS Bc250PciOwnerRetire(BC250_PCI_OWNER *p)
{ (void)p;++platformCalls;return STATUS_NOT_SUPPORTED; }
NTSTATUS Bc250PciCorrelate(const BC250_PCI_OBSERVATION *p,const BC250_PCI_RESOURCES *v,BC250_PCI_CORRELATION *c)
{ (void)p;(void)v;(void)c;++platformCalls;return STATUS_NOT_SUPPORTED; }
int main(VOID)
{
    BC250_PCI_SAMPLE output;
    BC250_PCI_BINDING *detached=(BC250_PCI_BINDING *)1;
    unsigned n;
    const UCHAR *bytes=(const UCHAR *)&output;
    memset(&output,0xa5,sizeof(output));
    if(Bc250PciPublisherInit(NULL)!=STATUS_NOT_SUPPORTED ||
        Bc250PciBindingPrepare(NULL,NULL,NULL)!=STATUS_NOT_SUPPORTED ||
        Bc250PciPublish(NULL,NULL)!=STATUS_NOT_SUPPORTED ||
        Bc250PciPin(NULL,NULL)!=STATUS_NOT_SUPPORTED ||
        Bc250PciPinRead(NULL,NULL,&output)!=STATUS_NOT_SUPPORTED ||
        Bc250PciUnpin(NULL,NULL)!=STATUS_NOT_SUPPORTED ||
        Bc250PciPublisherClose(NULL,1)!=STATUS_NOT_SUPPORTED ||
        Bc250PciPublisherRetire(NULL,&detached)!=STATUS_NOT_SUPPORTED ||
        platformCalls || detached) return 1;
    for(n=0;n<sizeof(output);++n)if(bytes[n])return 1;
    puts("PASS: eight native-closed publisher APIs refused without platform calls.");
    return 0;
}
