/* SPDX-License-Identifier: Apache-2.0 */
#define BC250_QUERY_ADMIT_TYPES_MOCK 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bc250_query_admission.c"
/* Link-only platform observers; test rejects any call. Not noreturn stubs,
 * so /O2 does not infer unrelated unreachable-code warnings. */
static volatile ULONG platformCalls;
UCHAR KeGetCurrentIrql(VOID){++platformCalls;return 3;}
BOOLEAN KeAreApcsDisabled(VOID){++platformCalls;return TRUE;}
VOID KeInitializeSpinLock(KSPIN_LOCK *p){(void)p;++platformCalls;}
VOID KeAcquireSpinLock(KSPIN_LOCK *p,KIRQL *old){(void)p;(void)old;++platformCalls;}
VOID KeReleaseSpinLock(KSPIN_LOCK *p,KIRQL old){(void)p;(void)old;++platformCalls;}
int main(VOID)
{
    BC250_QUERY_ADMISSION *d=(BC250_QUERY_ADMISSION *)1;
    BC250_QUERY_TICKET *t=(BC250_QUERY_TICKET *)1;
    if(Bc250QueryAdmitInit(d,1,1,1)!=STATUS_NOT_SUPPORTED ||
       Bc250QueryAdmitEnter(d,t)!=STATUS_NOT_SUPPORTED ||
       Bc250QueryAdmitCheck(d,t)!=STATUS_NOT_SUPPORTED ||
       Bc250QueryAdmitClose(d,BC250_QA_REMOVE)!=STATUS_NOT_SUPPORTED ||
       Bc250QueryAdmitLeave(d,t)!=STATUS_NOT_SUPPORTED ||
       Bc250QueryAdmitQuarantine(d,t)!=STATUS_NOT_SUPPORTED || platformCalls)return 1;
    puts("PASS: six query admission APIs native FALSE before pointers/DDIs.");return 0;
}
