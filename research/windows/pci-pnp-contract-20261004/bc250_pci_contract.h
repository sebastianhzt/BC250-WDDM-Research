/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BC250_PCI_CONTRACT_H
#define BC250_PCI_CONTRACT_H
#if defined(BC250_PCI_CONTRACT_MOCK) || defined(BC250_PCI_TYPES_MOCK)
#include "mock-wdm.h"
#else
#include <ntddk.h>
#endif
#define BC250_PCI_SIGNATURE 0x50434931U
#define BC250_PCI_READY 1U
#define BC250_PCI_CLOSED 2U
#define BC250_PCI_QUARANTINED 3U
#define BC250_PCI_HEADER_BYTES 64U
#define BC250_PCI_MAX_MEMORY 8U
/* Opaque internal anchor, NOT an ABI/user-mode pointer. Gen/Epoch must never
 * wrap/reuse during a lease; adapter must reject exhaustion, not hide ABA. */
typedef struct BC250_PCI_LEASE {
    PVOID Anchor;
    ULONGLONG Generation, Epoch;
} BC250_PCI_LEASE;
typedef struct BC250_PCI_OPS {
    PVOID Context;
    NTSTATUS (*Acquire)(PVOID,BC250_PCI_LEASE *);
    BOOLEAN (*StillStarted)(PVOID,const BC250_PCI_LEASE *);
    VOID (*Release)(PVOID,BC250_PCI_LEASE *);
    NTSTATUS (*Query)(PVOID,const BC250_PCI_LEASE *,BUS_INTERFACE_STANDARD *);
} BC250_PCI_OPS;
typedef struct BC250_PCI_SESSION {
    ULONG Signature, State, Busy, LeaseHeld, InterfaceHeld;
    BC250_PCI_OPS Ops;
    BC250_PCI_LEASE Lease;
    BUS_INTERFACE_STANDARD Bus;
} BC250_PCI_SESSION;
typedef struct BC250_PCI_OBSERVATION {
    ULONG Valid;
    ULONGLONG Generation, Epoch;
    UCHAR Header[BC250_PCI_HEADER_BYTES];
} BC250_PCI_OBSERVATION;
typedef struct BC250_PCI_MEMORY {
    ULONGLONG RawBase, TranslatedBase, Length;
    USHORT RawFlags, TranslatedFlags;
    ULONG Ordinal;
} BC250_PCI_MEMORY;
typedef struct BC250_PCI_RESOURCES {
    ULONGLONG Generation, Epoch;
    ULONG Started, Valid, DescriptorCount, MemoryCount;
    BC250_PCI_MEMORY Memory[BC250_PCI_MAX_MEMORY];
} BC250_PCI_RESOURCES;
typedef struct BC250_PCI_MATCH {
    ULONG Bar, DescriptorOrdinal;
    ULONGLONG RawBase, TranslatedBase, PnpLength;
} BC250_PCI_MATCH;
typedef struct BC250_PCI_CORRELATION {
    ULONG Valid, Count, VramOwnership, DmaAuthorized;
    BC250_PCI_MATCH Matches[6];
} BC250_PCI_CORRELATION;
/* Init once in exclusive zeroed stable resident storage. Trusted operations
 * and context/token anchored across ALL calls. Same-thread external serialization;
 * Busy guards synchronous reentry ONLY, not concurrency. All inputs resident,
 * nonaliasing, lifetime-stable, kernel-owned. Query is synchronous: SUCCESS owns
 * one interface ref; failure leaves Bus all-zero, no late completion/reference.
 * QUARANTINED retains uncertain references/lease permanently: no retry/close.
 * No real adapter, PnP integration, IRPs, waiting, cancellation or rundown here.
 * Before/after checks do NOT prevent a physical read racing STOP/power/remove. */
NTSTATUS Bc250PciSessionInit(BC250_PCI_SESSION *,const BC250_PCI_OPS *,ULONG);
NTSTATUS Bc250PciObserve(BC250_PCI_SESSION *,BC250_PCI_OBSERVATION *,ULONG);
NTSTATUS Bc250PciCorrelate(const BC250_PCI_OBSERVATION *,const BC250_PCI_RESOURCES *,BC250_PCI_CORRELATION *);
#endif
