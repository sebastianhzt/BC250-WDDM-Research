/* SPDX-License-Identifier: Apache-2.0
 * RAM event fake: no WDK ABI or scheduler/atomic correctness emulation.
 */
#ifndef BC250_MOCK_GATE_WDM_H
#define BC250_MOCK_GATE_WDM_H
#include "../dma-windows-20261004/mock-wdm.h"
#define STATUS_TIMEOUT ((NTSTATUS)0x102)
typedef int32_t LONG;
typedef void *PVOID;
typedef struct { int64_t QuadPart; } LARGE_INTEGER, *PLARGE_INTEGER;
typedef struct { PVOID Thread; ULONG Held, Initialized; } KMUTEX, *PRKMUTEX;
typedef struct { ULONG Count, Closing, Initialized; } EX_RUNDOWN_REF, *PEX_RUNDOWN_REF;
typedef enum { Executive } KWAIT_REASON;
typedef enum { KernelMode } KPROCESSOR_MODE;
extern BOOLEAN Bc250MockGateExecutionAllowed;
#define RtlZeroMemory(pointer, bytes) memset((pointer), 0, (bytes))
SIZE_T RtlCompareMemory(const void *, const void *, SIZE_T);
PVOID KeGetCurrentThread(void);
LONG InterlockedCompareExchange(volatile LONG *, LONG, LONG);
LONG InterlockedExchange(volatile LONG *, LONG);
PVOID InterlockedCompareExchangePointer(PVOID volatile *, PVOID, PVOID);
PVOID InterlockedExchangePointer(PVOID volatile *, PVOID);
void KeEnterCriticalRegion(void);
void KeLeaveCriticalRegion(void);
void KeInitializeMutex(PRKMUTEX, ULONG);
NTSTATUS KeWaitForSingleObject(PVOID, KWAIT_REASON, KPROCESSOR_MODE, BOOLEAN, PLARGE_INTEGER);
LONG KeReleaseMutex(PRKMUTEX, BOOLEAN);
void ExInitializeRundownProtection(PEX_RUNDOWN_REF);
BOOLEAN ExAcquireRundownProtection(PEX_RUNDOWN_REF);
void ExReleaseRundownProtection(PEX_RUNDOWN_REF);
void ExWaitForRundownProtectionRelease(PEX_RUNDOWN_REF);
#endif
