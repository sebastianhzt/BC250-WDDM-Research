/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BC250_PCI_ADMISSION_H
#define BC250_PCI_ADMISSION_H
#ifdef BC250_PCI_ADMISSION_MOCK
#include "mock-admission-wdm.h"
#endif
#include "../dma-gate-20261004/bc250_dma_gate.h"
#include "../pci-pnp-contract-20261004/bc250_pci_contract.h"
#define BC250_PCI_OWNER_SIGNATURE 0x504f5731U
#define BC250_PCI_OWNER_ACTIVE 0L
#define BC250_PCI_OWNER_RETIRING 1L
#define BC250_PCI_OWNER_RETIRED 2L
#define BC250_PCI_OWNER_FAULT 3L
#define BC250_PCI_REASON_QUERY_STOP 1U
#define BC250_PCI_REASON_QUERY_REMOVE 2U
#define BC250_PCI_REASON_STOP 4U
#define BC250_PCI_REASON_SURPRISE 8U
#define BC250_PCI_REASON_POWER_DOWN 16U
#define BC250_PCI_REASON_REMOVE 32U
typedef struct BC250_PCI_OWNER {
    BC250_DMA_GATE Gate; /* Existing gate, no Windows DMA resource involved. */
    BC250_PCI_SESSION Session;
    BC250_PCI_OPS Backend;
    ULONGLONG Generation,Epoch; /* Immutable incarnation, trusted external IDs. */
    volatile LONG Reasons,Faulted,Retirement;
    ULONG Signature;
} BC250_PCI_OWNER;
/* Zero/init ONCE, aligned resident storage. Publisher/external coordinator
 * anchors Owner+Backend context before every API entry through ALL returns,
 * including failed entrants. Gate rundown does NOT protect pointer publication
 * or PDO/backend refs. One read attempt per Owner; never reset/reopen/reuse.
 * New START/D0/cancel needs a new separately anchored Owner, strictly new
 * nonwrapping Generation/Epoch, after old retirement and external unpublication.
 * No shared Session or arbitrary callback contexts outside the gate.
 * Owner, Backend and output buffers must not alias; none is user controlled.
 * APIs nonarbitrary PASSIVE; no nested gates, wait on remover, own-thread APC
 * completion dependence, or thread termination while ticket held.
 * Only Stop allowed in backend hooks. Retire EXTERNAL coordinator only, after
 * all its own Observe attempts returned, no own ticket/in-progress attempt.
 * Fault retains backend anchors forever: no close/recovery or free permission.
 * Stop only closes admission; already-admitted calls may be inside backend.
 * Neither Gate nor epoch checks prove physical availability/quiescence.
 * No IRP/PnP/power dispatch, publication implementation, adapter or GPU here. */
NTSTATUS Bc250PciOwnerInit(BC250_PCI_OWNER *,const BC250_PCI_OPS *,ULONGLONG,ULONGLONG);
NTSTATUS Bc250PciOwnerObserve(BC250_PCI_OWNER *,BC250_PCI_OBSERVATION *);
NTSTATUS Bc250PciOwnerStop(BC250_PCI_OWNER *,ULONG);
NTSTATUS Bc250PciOwnerRetire(BC250_PCI_OWNER *);
#endif
