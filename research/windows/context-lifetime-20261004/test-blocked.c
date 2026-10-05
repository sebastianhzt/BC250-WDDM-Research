/* SPDX-License-Identifier: Apache-2.0 */
#define BC250_LIFE_TYPES_MOCK 1
#include <string.h>
#include <stdio.h>
#include "bc250_context_lifetime.c"
static unsigned calls;
unsigned char KeGetCurrentIrql(VOID){++calls;return 0;}
VOID KeInitializeSpinLock(KSPIN_LOCK *p){(VOID)p;++calls;}
VOID KeAcquireSpinLock(KSPIN_LOCK *p,KIRQL *old){(VOID)p;(VOID)old;++calls;}
VOID KeReleaseSpinLock(KSPIN_LOCK *p,KIRQL old){(VOID)p;(VOID)old;++calls;}
int main(VOID)
{
    BC250_LIFE_DOMAIN d={0};BC250_LIFE_TOKEN t={0},h={0};BC250_LIFE_STATUS s;PVOID out=(PVOID)(ULONG_PTR)1;
    memset(&s,0xff,sizeof(s));
    if(Bc250LifeInit(&d,1)!=STATUS_NOT_SUPPORTED ||
        Bc250LifePublish(&d,1,1,&d)!=STATUS_NOT_SUPPORTED ||
        Bc250LifeHold(&d,1,&h)!=STATUS_NOT_SUPPORTED ||
        Bc250LifeEnter(&d,1,&t,&out)!=STATUS_NOT_SUPPORTED || out)return 1;
    out=(PVOID)(ULONG_PTR)1;
    if(Bc250LifeEnterHeld(&d,&h,&t,&out)!=STATUS_NOT_SUPPORTED || out ||
        Bc250LifeExit(&d,&t)!=STATUS_NOT_SUPPORTED || Bc250LifeReleaseHold(&d,&h)!=STATUS_NOT_SUPPORTED ||
        Bc250LifeClose(&d)!=STATUS_NOT_SUPPORTED)return 1;
    out=(PVOID)(ULONG_PTR)1;
    if(Bc250LifeDetach(&d,&out)!=STATUS_NOT_SUPPORTED || out ||
        Bc250LifeInspect(&d,&s)!=STATUS_NOT_SUPPORTED || !LifeZero(&s,sizeof(s)) ||
        !LifeZero(&d,sizeof(d)) || !LifeZero(&t,sizeof(t)) || !LifeZero(&h,sizeof(h)) || calls)return 1;
    puts("PASS: ten context lifetime APIs native FALSE; zero platform calls/zero outputs; no hardware.");return 0;
}
