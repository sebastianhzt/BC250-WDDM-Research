/* SPDX-License-Identifier: Apache-2.0 */
/* RAM fixture derived verbatim from dma-gate-20261004/test-gate.c through
 * ExWaitForRundownProtectionRelease; only the relative gate include changes.
 * The frozen full gate regression runs independently in verify-offline.py.
 * No real gate implementation or predecessor is copied/modified here. */
#define main Bc250GateAdapterRegression
#include "../dma-windows-20261004/test-adapter.c"
#undef main
#define BC250_DMA_GATE_MOCK 1
#include "../dma-gate-20261004/bc250_dma_gate.c" /* ACTUAL standalone candidate, only fake DDIs. */

BOOLEAN Bc250MockGateExecutionAllowed = TRUE;
static unsigned int threadIndex, critical[3], gateCalls, rundownGets, rundownPuts, waits, mutexPuts;
static unsigned int denyRundown, stopAtAcquire, stopAtMutex;
static int fakeThreads[3];
static BC250_DMA_GATE *eventGate;
static BC250_DMA_GATE_TICKET *completeTicket;
static unsigned int completeThread;
static NTSTATUS fakeWaitStatus = STATUS_SUCCESS;

PVOID KeGetCurrentThread(void) { ++gateCalls; return &fakeThreads[threadIndex]; }
SIZE_T RtlCompareMemory(const void *a, const void *b, SIZE_T bytes)
{
    const unsigned char *x = a, *y = b; SIZE_T i;
    for (i = 0; i < bytes && x[i] == y[i]; ++i) {}
    return i;
}
LONG InterlockedCompareExchange(volatile LONG *target, LONG value, LONG compare)
{
    LONG old = *target; ++gateCalls; if (old == compare) *target = value; return old;
}
LONG InterlockedExchange(volatile LONG *target, LONG value)
{
    LONG old = *target; ++gateCalls; *target = value; return old;
}
PVOID InterlockedCompareExchangePointer(PVOID volatile *target, PVOID value, PVOID compare)
{
    PVOID old = *target; ++gateCalls; if (old == compare) *target = value; return old;
}
PVOID InterlockedExchangePointer(PVOID volatile *target, PVOID value)
{
    PVOID old = *target; ++gateCalls; *target = value; return old;
}
void KeEnterCriticalRegion(void) { ++gateCalls; ++critical[threadIndex]; CHECK(irql == 0); }
void KeLeaveCriticalRegion(void) { ++gateCalls; CHECK(critical[threadIndex]); --critical[threadIndex]; }
void KeInitializeMutex(PRKMUTEX mutex, ULONG level)
{
    ++gateCalls; CHECK(level == 0 && !mutex->Initialized);
    mutex->Initialized = 1;
}
NTSTATUS KeWaitForSingleObject(PVOID object, KWAIT_REASON reason, KPROCESSOR_MODE mode,
    BOOLEAN alertable, PLARGE_INTEGER timeout)
{
    PRKMUTEX mutex = object;
    ++gateCalls; ++waits;
    CHECK(critical[threadIndex] && irql == 0 && reason == Executive && mode == KernelMode);
    CHECK(!alertable && timeout && timeout->QuadPart == 0 && mutex->Initialized);
    if (mutex->Held) return STATUS_TIMEOUT;
    if (fakeWaitStatus != STATUS_SUCCESS) return fakeWaitStatus;
    mutex->Held = 1; mutex->Thread = &fakeThreads[threadIndex];
    if (stopAtMutex) CHECK(Bc250DmaGateStop(eventGate) == STATUS_SUCCESS);
    return STATUS_SUCCESS;
}
LONG KeReleaseMutex(PRKMUTEX mutex, BOOLEAN wait)
{
    ++gateCalls; ++mutexPuts;
    CHECK(!wait && critical[threadIndex] && irql == 0 && mutex->Held == 1);
    CHECK(mutex->Thread == &fakeThreads[threadIndex]);
    mutex->Held = 0; mutex->Thread = NULL; return 0;
}
void ExInitializeRundownProtection(PEX_RUNDOWN_REF rundown)
{
    ++gateCalls; CHECK(!rundown->Initialized); rundown->Initialized = 1;
}
BOOLEAN ExAcquireRundownProtection(PEX_RUNDOWN_REF rundown)
{
    ++gateCalls; CHECK(critical[threadIndex] && rundown->Initialized);
    if (denyRundown || rundown->Closing) return FALSE;
    ++rundownGets; ++rundown->Count;
    if (stopAtAcquire) CHECK(Bc250DmaGateStop(eventGate) == STATUS_SUCCESS);
    return TRUE;
}
void ExReleaseRundownProtection(PEX_RUNDOWN_REF rundown)
{
    ++gateCalls; ++rundownPuts;
    CHECK(critical[threadIndex] && rundown->Initialized && rundown->Count);
    --rundown->Count;
}
void ExWaitForRundownProtectionRelease(PEX_RUNDOWN_REF rundown)
{
    BC250_DMA_GATE_TICKET rejected = {0};
    unsigned int coordinator = threadIndex;
    ++gateCalls; CHECK(irql == 0 && rundown->Initialized && !rundown->Closing);
    rundown->Closing = 1;
    CHECK(Bc250DmaGateQuiesce(eventGate) == STATUS_DEVICE_BUSY);
    CHECK(Bc250DmaGateStop(eventGate) == STATUS_SUCCESS);
    CHECK(Bc250DmaGateEnter(eventGate, &rejected) == STATUS_CANCELLED);
    if (rundown->Count) {
        CHECK(completeTicket && completeThread != coordinator);
        threadIndex = completeThread;
        CHECK(Bc250DmaGateLeave(eventGate, completeTicket) == STATUS_SUCCESS);
        threadIndex = coordinator; completeTicket = NULL;
    }
    CHECK(!rundown->Count && !eventGate->Mutex.Held);
    /* Deterministic scheduler event, NOT a real concurrent thread or wait. */
}
