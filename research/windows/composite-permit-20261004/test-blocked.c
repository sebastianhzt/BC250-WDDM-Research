/* SPDX-License-Identifier: Apache-2.0 */
#define BC250_PERMIT_TYPES_MOCK 1
#include <string.h>
#include <stdio.h>
#include "bc250_composite_permit.c"
static unsigned calls;
unsigned char KeGetCurrentIrql(VOID){++calls;return 0;}
PVOID KeGetCurrentThread(VOID){++calls;return NULL;}
VOID KeInitializeSpinLock(KSPIN_LOCK *l){(VOID)l;++calls;}
VOID KeAcquireSpinLock(KSPIN_LOCK *l,KIRQL *old){(VOID)l;(VOID)old;++calls;}
VOID KeReleaseSpinLock(KSPIN_LOCK *l,KIRQL old){(VOID)l;(VOID)old;++calls;}
int main(VOID)
{
    BC250_PERMIT r={0};BC250_PERMIT_READER a={0};BC250_PERMIT_WRITER w={0};BC250_PERMIT_SNAPSHOT s;ULONGLONG v=99;
    memset(&s,0xff,sizeof(s));
    if(Bc250PermitInit(&r,1,1)!=STATUS_NOT_SUPPORTED ||
       Bc250PermitWriteAdmit(&r,1,&w)!=STATUS_NOT_SUPPORTED ||
       Bc250PermitWriteClaim(&r,&w)!=STATUS_NOT_SUPPORTED ||
       Bc250PermitWriteFinish(&r,&w,STATUS_SUCCESS,1)!=STATUS_NOT_SUPPORTED ||
       Bc250PermitWriteAbandon(&r,&w)!=STATUS_NOT_SUPPORTED ||
       Bc250PermitAcquire(&r,&a)!=STATUS_NOT_SUPPORTED ||
       Bc250PermitStepEnter(&r,&a,&v)!=STATUS_NOT_SUPPORTED ||
       Bc250PermitStepLeave(&r,&a)!=STATUS_NOT_SUPPORTED ||
       Bc250PermitRelease(&r,&a)!=STATUS_NOT_SUPPORTED ||
       Bc250PermitClose(&r)!=STATUS_NOT_SUPPORTED ||
       Bc250PermitInspect(&r,&s)!=STATUS_NOT_SUPPORTED || v ||
       !PermitZero(&r,sizeof(r)) || !PermitZero(&a,sizeof(a)) || !PermitZero(&w,sizeof(w)) ||
       !PermitZero(&s,sizeof(s)) || calls)return 1;
    puts("PASS: eleven permit APIs native FALSE; zero platform calls; no hardware authorization.");return 0;
}
