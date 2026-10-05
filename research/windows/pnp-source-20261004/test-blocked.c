/* SPDX-License-Identifier: Apache-2.0 */
#define BC250_PNP_SOURCE_TYPES_MOCK 1
#define BC250_PNP_CAPTURE_TYPES_MOCK 1
#define BC250_PCI_TYPES_MOCK 1
#include <string.h>
#include <stdio.h>
#include "bc250_pnp_source.c"
static unsigned calls;
unsigned char KeGetCurrentIrql(VOID) { ++calls;return 0; }
PVOID KeGetCurrentThread(VOID) { ++calls;return NULL; }
SIZE_T RtlCompareMemory(const VOID *a,const VOID *b,SIZE_T n)
{ (void)a;(void)b;(void)n;++calls;return 0; }
VOID KeInitializeSpinLock(KSPIN_LOCK *p) { (void)p;++calls; }
VOID KeAcquireSpinLock(KSPIN_LOCK *p,KIRQL *v) { (void)p;(void)v;++calls; }
VOID KeReleaseSpinLock(KSPIN_LOCK *p,KIRQL v) { (void)p;(void)v;++calls; }
VOID ExInitializeRundownProtection(PEX_RUNDOWN_REF p) { (void)p;++calls; }
BOOLEAN ExAcquireRundownProtection(PEX_RUNDOWN_REF p) { (void)p;++calls;return FALSE; }
VOID ExReleaseRundownProtection(PEX_RUNDOWN_REF p) { (void)p;++calls; }
VOID ExWaitForRundownProtectionRelease(PEX_RUNDOWN_REF p) { (void)p;++calls; }
VOID ObReferenceObject(PVOID p) { (void)p;++calls; }
VOID ObDereferenceObject(PVOID p) { (void)p;++calls; }
int main(VOID)
{
    BC250_CAPTURE_INPUT out;SIZE_T n;memset(&out,0xa5,sizeof(out));
    if(Bc250SourceInit(NULL,1,1,NULL,NULL)!=STATUS_NOT_SUPPORTED ||
        Bc250SourceBegin(NULL,1,0,NULL)!=STATUS_NOT_SUPPORTED ||
        Bc250SourceComplete(NULL,NULL,STATUS_SUCCESS,NULL)!=STATUS_NOT_SUPPORTED ||
        Bc250SourceTransition(NULL,9)!=STATUS_NOT_SUPPORTED ||
        Bc250SourceInterlocks(NULL,1)!=STATUS_NOT_SUPPORTED ||
        Bc250SourceAcquire(NULL,NULL)!=STATUS_NOT_SUPPORTED ||
        Bc250SourceValidate(NULL,NULL,&out)!=STATUS_NOT_SUPPORTED ||
        Bc250SourceRelease(NULL,NULL)!=STATUS_NOT_SUPPORTED ||
        Bc250SourceClose(NULL)!=STATUS_NOT_SUPPORTED || Bc250SourceDrain(NULL)!=STATUS_NOT_SUPPORTED || calls)return 1;
    for(n=0;n<sizeof(out);++n)if(((const UCHAR *)&out)[n])return 1;
    puts("PASS: ten native-closed producer APIs refused; no platform/reference calls, snapshot output zero.");return 0;
}
