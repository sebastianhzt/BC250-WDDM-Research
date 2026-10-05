/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BC250_ENVELOPE_REFERENCE_H
#define BC250_ENVELOPE_REFERENCE_H
#if defined(BC250_ENV_MOCK) || defined(BC250_ENV_TYPES_MOCK)
#ifndef BC250_LIFE_TYPES_MOCK
#define BC250_LIFE_TYPES_MOCK 1
#endif
#include "../context-lifetime-20261004/bc250_context_lifetime.h"
#include "../pnp-completion-20261004/mock-completion-wdm.h"
NTSTATUS IoAcquireRemoveLock(PIO_REMOVE_LOCK,PVOID);
VOID IoReleaseRemoveLock(PIO_REMOVE_LOCK,PVOID);
VOID ObReferenceObject(PVOID);
VOID ObDereferenceObject(PVOID);
#else
#include <wdm.h>
#include "../context-lifetime-20261004/bc250_context_lifetime.h"
#endif
#define BC250_ENV_SIGNATURE 0x45565231U
#define BC250_ENV_SLOTS 8U
#define BC250_ENV_PREPARING 1U
#define BC250_ENV_ACTIVE 2U
#define BC250_ENV_RELEASING 3U
typedef struct BC250_ENV_DOMAIN BC250_ENV_DOMAIN;
typedef struct BC250_ENV_RECEIPT {
    BC250_ENV_DOMAIN *Root;
    PDEVICE_OBJECT Self;
    ULONGLONG Scope,Id;
    BC250_LIFE_TOKEN Hold;
} BC250_ENV_RECEIPT;
typedef struct BC250_ENV_SLOT {
    BC250_ENV_RECEIPT *Address;
    BC250_ENV_RECEIPT Seal;
    ULONG Phase;
} BC250_ENV_SLOT;
struct BC250_ENV_DOMAIN {
    KSPIN_LOCK Lock;
    ULONG Signature,Closing,Fault;
    ULONGLONG Scope,LastId,Key;
    PDEVICE_OBJECT Self;
    PIO_REMOVE_LOCK RemoveLock;
    BC250_LIFE_DOMAIN *Life;
    BC250_ENV_SLOT Slots[BC250_ENV_SLOTS];
};
/* Native FALSE. Isolated reference/remove-tag/child-Hold acquisition wrapper.
 * Init once, exclusive PASSIVE zero resident parent; Self is caller-owned FDO
 * NOT arbitrary lower/PDO/DriverObject. RemoveLock already initialized. Life
 * is separate frozen parent, child published by independent producer. All
 * roots/receipts/locks resident stable nonaliasing OUTSIDE child. Caller supplies
 * independently valid OS entry/publication lookup anchor BEFORE every attempt
 * through ALL returns, including failures and final Self dereference. This
 * adapter does not solve bootstrap, storage allocation or module-code return.
 * Acquire/Release/Close <=DISPATCH. No platform calls while registry lock held.
 * Reserve exact-address slot -> Self ref -> remove tag(receipt address) -> Hold;
 * failure unwinds only successes, clears metadata BEFORE final Self dereference.
 * Close denies new reservations; pre-reserved acquisition may finish. No reset.
 * Receipt immutable/exclusive until Release, except derived Life API use via
 * exact &receipt.Hold (read only). KEEP receipt storage through ALL helper returns.
 * Caller child body uses LifeEnterHeld/Exit, never releases Hold independently.
 * Release refuses live derived calls (BUSY retains ref/tag/Hold) and unknown/
 * copied/tampered identity (no guessed cleanup). Known last Hold drops FIRST;
 * child may retire there. Then Parent-only remove tag cleanup, erase receipt/
 * registry BEFORE final Self dereference; stack-only return after last drop.
 * Non-BUSY Hold failure quarantines known resources, no force free/recovery.
 * No actual async envelope publishing/CSQ/work/IRP/forward/CDO lookup/unload.
 * Object refs preserve storage, not hardware availability; not a module pin.
 * This does NOT authorize PCI/MMIO/VRAM/GART/firmware/ring/DMA or driver linking.
 */
NTSTATUS Bc250EnvInit(BC250_ENV_DOMAIN *,ULONGLONG,PDEVICE_OBJECT,PIO_REMOVE_LOCK,BC250_LIFE_DOMAIN *,ULONGLONG);
NTSTATUS Bc250EnvAcquire(BC250_ENV_DOMAIN *,BC250_ENV_RECEIPT *);
NTSTATUS Bc250EnvRelease(BC250_ENV_DOMAIN *,BC250_ENV_RECEIPT *);
NTSTATUS Bc250EnvClose(BC250_ENV_DOMAIN *);
#endif
