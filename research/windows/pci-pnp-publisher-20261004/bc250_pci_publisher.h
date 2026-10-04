/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BC250_PCI_PUBLISHER_H
#define BC250_PCI_PUBLISHER_H
#if defined(BC250_PCI_PUBLISHER_MOCK) || defined(BC250_PCI_PUBLISHER_TYPES_MOCK)
#include "mock-publisher-wdm.h"
#endif
#include "../pci-pnp-admission-20261004/bc250_pci_admission.h"
#define BC250_PUBLISHER_SIGNATURE 0x50554231U
#define BC250_BINDING_SIGNATURE 0x42494e31U
#define BC250_PUBLISHER_PINS 16U
#define BC250_BINDING_PREPARED 1U
#define BC250_BINDING_LIVE 2U
#define BC250_BINDING_CLOSING 3U
#define BC250_BINDING_DRAINING 4U
#define BC250_BINDING_RETIRING 5U
#define BC250_BINDING_RETIRED 6U
#define BC250_BINDING_FAULT 7U
typedef struct BC250_PCI_PUBLISHER BC250_PCI_PUBLISHER;
typedef struct BC250_PCI_PIN BC250_PCI_PIN;
typedef struct BC250_PCI_BINDING {
    BC250_PCI_OWNER Owner;
    BC250_PCI_RESOURCES Resources; /* Immutable after Prepare; synthetic tuple. */
    BC250_PCI_PUBLISHER *Home;
    ULONG Signature,Phase,Pins; /* After Publish: under Home.Lock only. */
} BC250_PCI_BINDING;
struct BC250_PCI_PIN {
    BC250_PCI_PUBLISHER *Root;
    BC250_PCI_BINDING *Binding;
    PVOID Thread;
    ULONGLONG Id,Generation,Epoch;
    ULONG Busy; /* Single owner thread, registered exact address; not copyable. */
};
typedef struct BC250_PCI_PIN_RECORD {
    BC250_PCI_PIN *Ticket;
    BC250_PCI_BINDING *Binding;
    PVOID Thread;
    ULONGLONG Id;
} BC250_PCI_PIN_RECORD;
struct BC250_PCI_PUBLISHER {
    KSPIN_LOCK Lock;
    BC250_PCI_BINDING *Current,*Draining;
    BC250_PCI_PIN_RECORD Records[BC250_PUBLISHER_PINS];
    ULONGLONG NextId,LastGeneration,LastEpoch;
    ULONG Signature;
};
typedef struct BC250_PCI_SAMPLE {
    ULONG Valid;
    BC250_PCI_OBSERVATION Pci;
    BC250_PCI_RESOURCES Resources;
    BC250_PCI_CORRELATION Relation;
} BC250_PCI_SAMPLE;
/* Candidate only: native Policy FALSE, no switch or driver/PnP integration.
 * Root lifetime must be externally anchored BEFORE all APIs through ALL returns
 * (also rejected calls/coordinator). Root never reset/copied/reinitialized.
 * All pointers/storage/code resident, aligned, trusted kernel-owned, nonaliasing.
 * Prepare AND Publish are exclusive producer-only for each Binding, with one
 * chosen Root: no concurrent Publish to different Roots (their locks differ),
 * no other Owner API, Prepare or producer field access during transfer.
 * Prepare uses a fresh zeroed Binding. Producer keeps
 * Binding+backend/PDO/context alive through Prepare/Publish, then transfers
 * exclusive management to this Root, retaining backing allocation until Detach
 * returns. No other Owner/Session/Gate APIs or field writes after publication.
 * Failed prepare/retirement or quarantine is NOT deletion permission.
 * Pins protect entire Owner API returns INCLUDING rejected entrants; exact
 * stable zeroed tickets, same PASSIVE nonarbitrary thread, no sharing/copy/APC
 * reentry/thread-exit while pinned. Busy covers callbacks AND final output copy.
 * No caller may free a published/closing/draining/faulted Binding. Retire is
 * external, nonblocking BUSY with any pins, never called with own pending Pin.
 * Only Close is allowed inside synchronous backend hooks; it calls OwnerStop
 * OUTSIDE spinlock. Never callbacks/Owner APIs/waits under Root.Lock.
 * Detach success means model accesses drained, NOT PDO ref accounting, maps,
 * jobs, MDLs, GPU/DMA idle or VRAM ownership. Reclamation only AFTER API return.
 * New START/D0/cancel model needs a fresh Owner/Binding, strictly increasing
 * Generation AND Epoch; IDs never wrap/reuse. 16 pins !=16 successful reads:
 * frozen Owner stays one-shot. No cancellation/restart PnP handlers.
 * Sample is internally coherent historical immutable tuple, not the current
 * Build22 State/StateEpoch/ResourceSnapshot or atomic hardware snapshot. */
NTSTATUS Bc250PciPublisherInit(BC250_PCI_PUBLISHER *);
NTSTATUS Bc250PciBindingPrepare(BC250_PCI_BINDING *,const BC250_PCI_OPS *,const BC250_PCI_RESOURCES *);
NTSTATUS Bc250PciPublish(BC250_PCI_PUBLISHER *,BC250_PCI_BINDING *);
NTSTATUS Bc250PciPin(BC250_PCI_PUBLISHER *,BC250_PCI_PIN *);
NTSTATUS Bc250PciPinRead(BC250_PCI_PUBLISHER *,BC250_PCI_PIN *,BC250_PCI_SAMPLE *);
NTSTATUS Bc250PciUnpin(BC250_PCI_PUBLISHER *,BC250_PCI_PIN *);
NTSTATUS Bc250PciPublisherClose(BC250_PCI_PUBLISHER *,ULONG);
NTSTATUS Bc250PciPublisherRetire(BC250_PCI_PUBLISHER *,BC250_PCI_BINDING **);
#endif
