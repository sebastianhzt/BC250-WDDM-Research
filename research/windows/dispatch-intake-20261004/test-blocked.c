/* SPDX-License-Identifier: Apache-2.0 */
#define BC250_INTAKE_TYPES_MOCK 1
#include <string.h>
#include <stdio.h>
#include "bc250_dispatch_intake.c"
static unsigned calls;
unsigned char KeGetCurrentIrql(VOID){++calls;return 0;}
VOID KeInitializeSpinLock(KSPIN_LOCK *l){(VOID)l;++calls;}
VOID KeAcquireSpinLock(KSPIN_LOCK *l,KIRQL *old){(VOID)l;(VOID)old;++calls;}
VOID KeReleaseSpinLock(KSPIN_LOCK *l,KIRQL old){(VOID)l;(VOID)old;++calls;}
int main(VOID)
{
    BC250_INTAKE r={0};BC250_INTAKE_TICKET t={0};BC250_INTAKE_SNAPSHOT s;
    memset(&s,0xff,sizeof(s));
    if(Bc250IntakeInit(&r,1,1)!=STATUS_NOT_SUPPORTED ||
       Bc250IntakeAdmit(&r,1,0,&t)!=STATUS_NOT_SUPPORTED ||
       Bc250IntakeClaim(&r,&t)!=STATUS_NOT_SUPPORTED ||
       Bc250IntakeAbandon(&r,&t)!=STATUS_NOT_SUPPORTED ||
       Bc250IntakeFinish(&r,&t,STATUS_SUCCESS)!=STATUS_NOT_SUPPORTED ||
       Bc250IntakeClose(&r)!=STATUS_NOT_SUPPORTED ||
       Bc250IntakeInspect(&r,&s)!=STATUS_NOT_SUPPORTED ||
       Bc250IntakeCheckIdleAtEpoch(&r,1)!=STATUS_NOT_SUPPORTED ||
       !IntakeZero(&r,sizeof(r)) || !IntakeZero(&t,sizeof(t)) ||
       !IntakeZero(&s,sizeof(s)) || calls)return 1;
    puts("PASS: eight intake APIs natively closed; zero platform calls, no IRP ownership.");return 0;
}
