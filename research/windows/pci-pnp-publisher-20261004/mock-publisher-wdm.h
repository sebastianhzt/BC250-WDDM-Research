/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BC250_MOCK_PUBLISHER_WDM_H
#define BC250_MOCK_PUBLISHER_WDM_H
#include "../pci-pnp-admission-20261004/mock-admission-wdm.h"
typedef UCHAR KIRQL;
typedef struct { ULONG Initialized,Held; } KSPIN_LOCK;
VOID KeInitializeSpinLock(KSPIN_LOCK *);
VOID KeAcquireSpinLock(KSPIN_LOCK *,KIRQL *);
VOID KeReleaseSpinLock(KSPIN_LOCK *,KIRQL);
extern BOOLEAN Bc250PublisherMockAllowed;
#endif
