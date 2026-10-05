/* SPDX-License-Identifier: Apache-2.0 */
#define BC250_BOUND_READ_TYPES_MOCK 1
#include <stdio.h>
#include <string.h>
#include "bc250_pci_bounded_reader.c"
static unsigned platform;
UCHAR KeGetCurrentIrql(VOID){++platform;return 0;}
int main(VOID)
{
    BC250_BOUND_RESULT result;memset(&result,0xcc,sizeof(result));
    if(Bc250BoundReadIdentity(NULL,NULL,NULL,NULL,&result)!=STATUS_NOT_SUPPORTED || platform || ((UCHAR *)&result)[0]!=0xcc)return 1;
    if(Bc250BoundReadIdentity(NULL,NULL,NULL,NULL,NULL)!=STATUS_NOT_SUPPORTED || platform)return 2;
    puts("PASS: bounded PCI reader native FALSE before pointers/DDIs; no activation knob.");return 0;
}
