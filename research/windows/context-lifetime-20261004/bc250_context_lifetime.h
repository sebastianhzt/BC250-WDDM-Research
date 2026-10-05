/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BC250_CONTEXT_LIFETIME_H
#define BC250_CONTEXT_LIFETIME_H
#if defined(BC250_LIFE_MOCK) || defined(BC250_LIFE_TYPES_MOCK)
#include "../pci-pnp-publisher-20261004/mock-publisher-wdm.h"
#else
#include <wdm.h>
#endif
#define BC250_LIFE_SIGNATURE 0x4c494631U
#define BC250_LIFE_SLOTS 16U
#define BC250_LIFE_CALL 1U
#define BC250_LIFE_HOLD 2U
#define BC250_LIFE_LIMIT 0x7fffffffU
typedef struct BC250_LIFE_DOMAIN BC250_LIFE_DOMAIN;
typedef struct BC250_LIFE_TOKEN {
    BC250_LIFE_DOMAIN *Domain;
    ULONGLONG Scope,Key,Id,HoldId;
    ULONG Generation,Kind;
} BC250_LIFE_TOKEN;
typedef struct BC250_LIFE_SLOT {
    BC250_LIFE_TOKEN *Address;
    BC250_LIFE_TOKEN Seal;
    ULONG Users;
} BC250_LIFE_SLOT;
struct BC250_LIFE_DOMAIN {
    KSPIN_LOCK Lock;
    ULONG Signature,Generation,LastGeneration,Published,Closing,Fault,Calls,Holds;
    ULONGLONG Scope,Key,LastKey,LastId;
    PVOID Payload; /* opaque, NEVER dereferenced by this component */
    BC250_LIFE_SLOT Slots[BC250_LIFE_SLOTS];
};
typedef struct BC250_LIFE_STATUS {
    ULONGLONG Scope,Key;
    ULONG Generation,Published,Closing,Fault,Calls,Holds;
} BC250_LIFE_STATUS;
/* Native FALSE, isolated child-context METADATA lifetime model, NOT hardware.
 * Domain is a separate stable PARENT registry, resident/aligned/externally
 * anchored before EVERY attempt through ALL returns (including failures and
 * Exit/Detach). Parent/module/code lifetime and OS removal/unload are NOT
 * implemented. Never place Domain/tokens/output storage inside child Payload.
 * Init exclusive unpublished once; no reset/copy/edit. Publish exclusive
 * producer transfers child backing retention to Domain; opaque pointer copied,
 * no object reference/free/allocator/callback. Key AND generation increase on
 * successive publication; one child, never replace a live/draining child.
 * ALL child accesses ONLY after Enter/EnterHeld returns SUCCESS, including
 * rejected backend calls and final output copies. Exit only after body/child
 * accesses have returned/finished, with no child dereference afterward. API
 * cannot prove a malicious caller followed that boundary. Calls are exact
 * stable tokens, immutable/exclusive entire lifetime across domains. Cross
 * thread handoff permitted ONLY under caller's independent token/OS ownership.
 * Hold reserves future callback/work obligations BEFORE registration/forward.
 * EnterHeld can enter known continuation cleanup after Close, never new work
 * authorization. Holds cannot release while derived calls exist. No queue,
 * cancellation, registration, RemoveLock or callback classification supplied.
 * Close unpublishes; no new key lookup/Hold. Existing calls/holds retained.
 * Known Exit/ReleaseHold cleanup allowed after closing/fault; unknown identity
 * never guessed/cleared. Detach PASSIVE nonblocking BUSY with any calls/holds,
 * returns opaque payload only after Close and known slots drained. Counts/
 * Inspect alone not reclaim permission. Exit final drop accesses only Parent;
 * child may detach before Exit returns, Parent/token/code must remain anchored.
 * Init/Publish/Detach PASSIVE; other APIs <=DISPATCH, no spinlock retained
 * across child use, no waits/nested locks/callbacks under lock. Pointers trusted
 * resident stable nonaliasing; no user ABI/IOCTL. Capacity16, no counter wrap.
 * Life tokens do not reserve Source tuple or prove VRAM/GART/GPU availability.
 */
NTSTATUS Bc250LifeInit(BC250_LIFE_DOMAIN *,ULONGLONG);
NTSTATUS Bc250LifePublish(BC250_LIFE_DOMAIN *,ULONGLONG,ULONG,PVOID);
NTSTATUS Bc250LifeHold(BC250_LIFE_DOMAIN *,ULONGLONG,BC250_LIFE_TOKEN *);
NTSTATUS Bc250LifeEnter(BC250_LIFE_DOMAIN *,ULONGLONG,BC250_LIFE_TOKEN *,PVOID *);
NTSTATUS Bc250LifeEnterHeld(BC250_LIFE_DOMAIN *,const BC250_LIFE_TOKEN *,BC250_LIFE_TOKEN *,PVOID *);
NTSTATUS Bc250LifeExit(BC250_LIFE_DOMAIN *,BC250_LIFE_TOKEN *);
NTSTATUS Bc250LifeReleaseHold(BC250_LIFE_DOMAIN *,BC250_LIFE_TOKEN *);
NTSTATUS Bc250LifeClose(BC250_LIFE_DOMAIN *);
NTSTATUS Bc250LifeDetach(BC250_LIFE_DOMAIN *,PVOID *);
NTSTATUS Bc250LifeInspect(BC250_LIFE_DOMAIN *,BC250_LIFE_STATUS *);
#endif
