/* SPDX-License-Identifier: Apache-2.0 */
#define BC250_PCI_TYPES_MOCK /* fake types only, execution gate remains FALSE */
#include "bc250_pci_contract.c"
#include <stdio.h>
int main(VOID)
{
    BC250_PCI_SESSION session={0};
    BC250_PCI_OPS ops={0};
    BC250_PCI_OBSERVATION observation;
    memset(&observation,0xa5,sizeof(observation));
    if(Bc250PciSessionInit(&session,&ops,0)!=STATUS_NOT_SUPPORTED ||
       Bc250PciObserve(&session,&observation,0)!=STATUS_NOT_SUPPORTED ||
       !ZeroBytes(&session,sizeof(session)) ||
       !ZeroBytes(&observation,sizeof(observation))) return 2;
    puts("PASS: execution gate FALSE without simulation macro; no platform calls.");
    return 0;
}
