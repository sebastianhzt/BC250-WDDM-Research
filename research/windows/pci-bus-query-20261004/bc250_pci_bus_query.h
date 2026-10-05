/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BC250_PCI_BUS_QUERY_H
#define BC250_PCI_BUS_QUERY_H
#if defined(BC250_BUS_QUERY_MOCK) || defined(BC250_BUS_QUERY_TYPES_MOCK)
#include "mock-query-wdm.h"
#ifndef BC250_BOUND_READ_TYPES_MOCK
#define BC250_BOUND_READ_TYPES_MOCK 1
#endif
#else
#include <wdm.h>
#include <wdmguid.h>
#endif
#include "../pci-bounded-reader-20261004/bc250_pci_bounded_reader.h"
#define BC250_BUS_QUERY_ACTIVE 1U
#define BC250_BUS_QUERY_DONE 2U
#define BC250_BUS_QUERY_UNKNOWN 3U
typedef struct BC250_BUS_QUERY {
    ULONG State,Sent,InterfaceHeld;
    PDEVICE_OBJECT Lower;
    KEVENT Event;
    IO_STATUS_BLOCK Final;
    BUS_INTERFACE_STANDARD Bus;
} BC250_BUS_QUERY;
/* Native FALSE, NO activation switch. One-shot synchronous QUERY_INTERFACE
 * followed by frozen 64-byte identity reader. PASSIVE, kernel APCs enabled,
 * not a PnP/power forwarding path; no spin/publication lock held over send/wait.
 * Caller passes ALREADY referenced lower device from protected own-FDO lookup,
 * NEVER own FDO/top-of-stack that would recursively reenter this handler.
 * Lower/FDO/code/module/operation admission independently anchored BEFORE
 * entry through ALL returns, failures, provider dereference and output copy.
 * Session exclusively zero-init once, resident/nonaliasing, stable exact
 * address; Event/Final/Bus inside it, NEVER stack locals that outlive a return.
 * Before/After trusted checks NOT atomic admission/physical availability.
 * Successful OS query owns ONE bus interface ref; no extra InterfaceReference.
 * InterfaceDereference once after all read calls, while caller retains roots.
 * Windows owns builder IRP: NEVER IoFreeIrp/IoCompleteRequest/read it post-send.
 * Always wait for completion event nonalertable KernelMode with NULL timeout;
 * final IO_STATUS_BLOCK, NOT IoCallDriver return, drives result. No custom
 * completion callback/CSQ and NO bounded-time, thread-exit or cancel guarantee.
 * Checks may deny before query and after completion but don't abort an IRP.
 * Query failure must leave Bus zero. Unexpected wait/finality/ownership ->
 * UNKNOWN preserves Session and ALL caller anchors, including issuing thread
 * while IRP may still be outstanding. No reset/retry/free/recovery supplied.
 * Known malformed success with a valid dereference callback is released;
 * missing callback means unknown ref retained. No guessed cleanup on failure.
 * No driver linking, real dispatch/admission integration or hardware tested.
 */
NTSTATUS Bc250BusQueryObserve(BC250_BUS_QUERY *,PDEVICE_OBJECT,
    BC250_BOUND_CHECK,BC250_BOUND_CHECK,PVOID,BC250_BOUND_RESULT *);
#endif
