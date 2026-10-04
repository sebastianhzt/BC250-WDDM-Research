/* SPDX-License-Identifier: Apache-2.0 */
#define BC250_PNP_CAPTURE_TYPES_MOCK 1
#define BC250_PCI_TYPES_MOCK 1
#include <string.h>
#include <stdio.h>
#include "bc250_pnp_capture.c"
static unsigned calls;
unsigned char KeGetCurrentIrql(VOID) { ++calls;return 0; }
SIZE_T RtlCompareMemory(const VOID *a,const VOID *b,SIZE_T n)
{ (void)a;(void)b;(void)n;++calls;return 0; }
VOID KeInitializeSpinLock(KSPIN_LOCK *p) { (void)p;++calls; }
VOID KeAcquireSpinLock(KSPIN_LOCK *p,KIRQL *v) { (void)p;(void)v;++calls; }
VOID KeReleaseSpinLock(KSPIN_LOCK *p,KIRQL v) { (void)p;(void)v;++calls; }
int main(VOID)
{
    BC250_CAPTURE_FRAME f;BC250_PCI_RESOURCES r;SIZE_T k;
    memset(&f,0xa5,sizeof(f));memset(&r,0xa5,sizeof(r));
    if(Bc250CaptureInit(NULL,1)!=STATUS_NOT_SUPPORTED ||
       Bc250CaptureCommit(NULL,NULL)!=STATUS_NOT_SUPPORTED ||
       Bc250CaptureRead(NULL,&f)!=STATUS_NOT_SUPPORTED ||
       Bc250CaptureValidate(NULL,NULL)!=STATUS_NOT_SUPPORTED ||
       Bc250CaptureResources(NULL,NULL,&r)!=STATUS_NOT_SUPPORTED || calls) return 1;
    for(k=0;k<sizeof(f);++k)if(((const UCHAR *)&f)[k])return 1;
    for(k=0;k<sizeof(r);++k)if(((const UCHAR *)&r)[k])return 1;
    puts("PASS: five native-closed capture APIs refused; outputs zero, platform calls zero.");return 0;
}
