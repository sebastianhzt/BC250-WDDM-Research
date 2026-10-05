/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BC250_PCI_BOUNDED_READER_H
#define BC250_PCI_BOUNDED_READER_H
#if defined(BC250_BOUND_READ_MOCK) || defined(BC250_BOUND_READ_TYPES_MOCK)
#include "../pci-pnp-admission-20261004/mock-admission-wdm.h"
#include "../pci-pnp-publisher-20261004/mock-publisher-wdm.h"
extern BOOLEAN Bc250BoundReadMockAllowed;
#else
#include <wdm.h>
#endif
#define BC250_BOUND_HEADER_BYTES 64U
typedef NTSTATUS (*BC250_BOUND_CHECK)(PVOID);
typedef struct BC250_BOUND_RESULT {
    ULONG Valid,BytesReturned,HardwareAuthorized,DmaAuthorized;
    UCHAR Header[BC250_BOUND_HEADER_BYTES];
} BC250_BOUND_RESULT;
/* Native FALSE, no enable knob or IOCTL. PASSIVE only. One synchronous call,
 * fixed CONFIG offset=0 length=64. Callback never writes beyond the supplied
 * buffer; returned count is validated, NOT a defense against a bad provider
 * writing OOB. Trusted already-referenced V1 BUS_INTERFACE_STANDARD copied
 * from the OS, NOT user data. Caller owns acquisition and releases reference
 * only AFTER ALL returns; routine neither refs nor derefs the interface.
 * Bus/output/check context nonaliasing stable resident anchored before entry
 * across ALL returns, even denied ones. Serial/exclusive outputs, no reentry.
 * Caller retains child/FDO/module and supplies actual operation admission.
 * Before/After are trusted checks, not an atomic reserve, OS cancellation or
 * proof against physical surprise removal. They return exact SUCCESS only.
 * No timeout/wait/IRP allocation here: GetBusData is synchronous and cannot
 * be interrupted by this wrapper; bounded BYTES does not mean bounded time.
 * STOP/cancel checks can reject before/after call, not recall an entered read.
 * Native FALSE returns before pointers/DDIs. Enabled MOCK failures zero the
 * entire result; successful Header is a HISTORICAL identity snapshot, never
 * MMIO/VRAM ownership, BAR sizing, DMA or physical availability authority.
 */
NTSTATUS Bc250BoundReadIdentity(const BUS_INTERFACE_STANDARD *,
    BC250_BOUND_CHECK,BC250_BOUND_CHECK,PVOID,BC250_BOUND_RESULT *);
#endif
