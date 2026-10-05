/* SPDX-License-Identifier: Apache-2.0 */
#define BC250_ENV_TYPES_MOCK 1
#include <stdio.h>
#include <string.h>
#include "bc250_envelope_reference.c"
static unsigned calls;
unsigned char KeGetCurrentIrql(VOID){++calls;return 0;}
SIZE_T RtlCompareMemory(const VOID *a,const VOID *b,SIZE_T n){(VOID)a;(VOID)b;(VOID)n;++calls;return 0;}
VOID KeInitializeSpinLock(KSPIN_LOCK *p){(VOID)p;++calls;}
VOID KeAcquireSpinLock(KSPIN_LOCK *p,KIRQL *old){(VOID)p;(VOID)old;++calls;}
VOID KeReleaseSpinLock(KSPIN_LOCK *p,KIRQL old){(VOID)p;(VOID)old;++calls;}
VOID ObReferenceObject(PVOID p){(VOID)p;++calls;}
VOID ObDereferenceObject(PVOID p){(VOID)p;++calls;}
NTSTATUS IoAcquireRemoveLock(PIO_REMOVE_LOCK p,PVOID t){(VOID)p;(VOID)t;++calls;return STATUS_CANCELLED;}
VOID IoReleaseRemoveLock(PIO_REMOVE_LOCK p,PVOID t){(VOID)p;(VOID)t;++calls;}
NTSTATUS Bc250LifeHold(BC250_LIFE_DOMAIN *d,ULONGLONG k,BC250_LIFE_TOKEN *t){(VOID)d;(VOID)k;(VOID)t;++calls;return STATUS_CANCELLED;}
NTSTATUS Bc250LifeReleaseHold(BC250_LIFE_DOMAIN *d,BC250_LIFE_TOKEN *t){(VOID)d;(VOID)t;++calls;return STATUS_CANCELLED;}
int main(VOID)
{
    BC250_ENV_DOMAIN d={0};BC250_ENV_RECEIPT e={0};BC250_LIFE_DOMAIN life={0};DEVICE_OBJECT self={0};IO_REMOVE_LOCK lock={0};
    if(Bc250EnvInit(&d,1,&self,&lock,&life,1)!=STATUS_NOT_SUPPORTED ||
       Bc250EnvAcquire(&d,&e)!=STATUS_NOT_SUPPORTED || Bc250EnvRelease(&d,&e)!=STATUS_NOT_SUPPORTED ||
       Bc250EnvClose(&d)!=STATUS_NOT_SUPPORTED || !EnvZero(&d,sizeof(d)) || !EnvZero(&e,sizeof(e)) || calls)return 1;
    puts("PASS: four envelope reference APIs native FALSE; zero platform calls and untouched roots/receipts.");return 0;
}
