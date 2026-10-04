/* SPDX-License-Identifier: Apache-2.0 */
#define main Bc250GateAdapterRegression
#include "../dma-windows-20261004/test-adapter.c"
#undef main
#define BC250_DMA_GATE_MOCK 1
#include "bc250_dma_gate.c" /* ACTUAL standalone candidate, only fake DDIs. */

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
static void ResetGate(BC250_DMA_GATE *gate)
{
    memset(gate, 0, sizeof(*gate)); memset(critical, 0, sizeof(critical));
    threadIndex = gateCalls = rundownGets = rundownPuts = waits = mutexPuts = 0;
    denyRundown = stopAtAcquire = stopAtMutex = 0;
    eventGate = gate; completeTicket = NULL; fakeWaitStatus = STATUS_SUCCESS;
    Bc250MockGateExecutionAllowed = TRUE; irql = 0;
    CHECK(Bc250DmaGateInit(gate) == STATUS_SUCCESS);
}
static void QuiesceGate(BC250_DMA_GATE *gate)
{
    CHECK(Bc250DmaGateStop(gate) == STATUS_SUCCESS);
    CHECK(Bc250DmaGateQuiesce(gate) == STATUS_SUCCESS);
    CHECK(gate->State == BC250_DMA_GATE_CLOSED && !gate->Rundown.Count);
    CHECK(rundownGets == rundownPuts && !critical[0] && !critical[1] && !critical[2]);
    CHECK(Bc250DmaGateStop(gate) == STATUS_INVALID_DEVICE_STATE);
    CHECK(Bc250DmaGateQuiesce(gate) == STATUS_INVALID_DEVICE_STATE);
}
static void TestLifecycle(void)
{
    BC250_DMA_GATE gate; BC250_DMA_GATE_TICKET ticket = {0}, copy, other = {0};
    ResetGate(&gate);
    CHECK(Bc250DmaGateQuiesce(&gate) == STATUS_INVALID_DEVICE_STATE);
    CHECK(Bc250DmaGateInit(&gate) == STATUS_INVALID_DEVICE_STATE);
    CHECK(Bc250DmaGateEnter(&gate, &ticket) == STATUS_SUCCESS);
    CHECK(irql == 0 && critical[0] == 1 && gate.Rundown.Count == 1 && gate.Mutex.Held);
    copy = ticket;
    CHECK(Bc250DmaGateLeave(&gate, &copy) == STATUS_INVALID_DEVICE_STATE);
    CHECK(Bc250DmaGateEnter(&gate, &other) == STATUS_DEVICE_BUSY);
    CHECK(Bc250DmaGateQuiesce(&gate) == STATUS_DEVICE_BUSY);
    threadIndex = 1;
    CHECK(Bc250DmaGateEnter(&gate, &other) == STATUS_DEVICE_BUSY && !critical[1]);
    CHECK(gate.Rundown.Count == 1 && critical[0] == 1);
    CHECK(Bc250DmaGateLeave(&gate, &ticket) == STATUS_INVALID_DEVICE_STATE);
    CHECK(Bc250DmaGateStop(&gate) == STATUS_SUCCESS);
    CHECK(Bc250DmaGateEnter(&gate, &other) == STATUS_CANCELLED);
    completeTicket = &ticket; completeThread = 0;
    CHECK(Bc250DmaGateQuiesce(&gate) == STATUS_SUCCESS);
    CHECK(gate.State == BC250_DMA_GATE_CLOSED && !gate.OwnerThread && !ticket.Active);
    CHECK(rundownGets == rundownPuts && !critical[0] && !critical[1]);
    threadIndex = 0;
    CHECK(Bc250DmaGateLeave(&gate, &ticket) == STATUS_INVALID_DEVICE_STATE);
    CHECK(Bc250DmaGateEnter(&gate, &other) == STATUS_CANCELLED);
}
static void TestStopRaces(void)
{
    BC250_DMA_GATE gate; BC250_DMA_GATE_TICKET ticket;
    unsigned int stage;
    for (stage = 0; stage < 3; ++stage) {
        ResetGate(&gate); memset(&ticket, 0, sizeof(ticket));
        if (stage == 0) denyRundown = 1;
        if (stage == 1) stopAtAcquire = 1;
        if (stage == 2) stopAtMutex = 1;
        CHECK(Bc250DmaGateEnter(&gate, &ticket) == STATUS_CANCELLED);
        CHECK(!ticket.Active && !ticket.Gate && !gate.OwnerThread && !gate.Mutex.Held);
        CHECK(rundownGets == rundownPuts && !critical[0]);
        denyRundown = stopAtAcquire = stopAtMutex = 0; QuiesceGate(&gate);
    }
    ResetGate(&gate); memset(&ticket, 0, sizeof(ticket)); gate.LastId = ~(ULONGLONG)0;
    CHECK(Bc250DmaGateEnter(&gate, &ticket) == STATUS_CANCELLED);
    CHECK(!gate.Rundown.Count && !gate.Mutex.Held && !ticket.Active);
    QuiesceGate(&gate);
}
static void TestValidation(void)
{
    BC250_DMA_GATE gate, snapshot; BC250_DMA_GATE_TICKET ticket = {0};
    unsigned int previousCalls, previousIrqlCalls;
    ResetGate(&gate); snapshot = gate; previousCalls = gateCalls; previousIrqlCalls = calls;
    Bc250MockGateExecutionAllowed = FALSE;
    CHECK(Bc250DmaGateInit(&gate) == STATUS_NOT_SUPPORTED);
    CHECK(Bc250DmaGateEnter(&gate, &ticket) == STATUS_NOT_SUPPORTED);
    CHECK(Bc250DmaGateLeave(&gate, &ticket) == STATUS_NOT_SUPPORTED);
    CHECK(Bc250DmaGateStop(&gate) == STATUS_NOT_SUPPORTED);
    CHECK(Bc250DmaGateQuiesce(&gate) == STATUS_NOT_SUPPORTED);
    CHECK(gateCalls == previousCalls && calls == previousIrqlCalls && !memcmp(&snapshot, &gate, sizeof(gate)));
    Bc250MockGateExecutionAllowed = TRUE; irql = 1; previousCalls = gateCalls;
    CHECK(Bc250DmaGateEnter(&gate, &ticket) == STATUS_INVALID_DEVICE_STATE);
    CHECK(Bc250DmaGateStop(&gate) == STATUS_INVALID_DEVICE_STATE);
    CHECK(Bc250DmaGateLeave(&gate, &ticket) == STATUS_INVALID_DEVICE_STATE);
    CHECK(gateCalls == previousCalls && !memcmp(&snapshot, &gate, sizeof(gate))); irql = 0;
    CHECK(Bc250DmaGateEnter(&gate, (BC250_DMA_GATE_TICKET *)&gate) == STATUS_INVALID_PARAMETER);
    CHECK(!memcmp(&snapshot, &gate, sizeof(gate)));
    CHECK(Bc250DmaGateEnter(NULL, &ticket) == STATUS_INVALID_PARAMETER);
    CHECK(Bc250DmaGateEnter(&gate, &ticket) == STATUS_SUCCESS);
    ++ticket.Id;
    CHECK(Bc250DmaGateLeave(&gate, &ticket) == STATUS_INVALID_DEVICE_STATE && gate.Rundown.Count == 1);
    --ticket.Id;
    CHECK(Bc250DmaGateLeave(&gate, &ticket) == STATUS_SUCCESS);
    QuiesceGate(&gate);
}
static void TestQuarantine(void)
{
    BC250_DMA_GATE gate; BC250_DMA_GATE_TICKET ticket = {0};
    ResetGate(&gate); fakeWaitStatus = STATUS_PENDING;
    CHECK(Bc250DmaGateEnter(&gate, &ticket) == STATUS_INVALID_DEVICE_STATE);
    CHECK(gate.State == BC250_DMA_GATE_FAULT && gate.Rundown.Count == 1);
    CHECK(rundownGets == 1 && rundownPuts == 0 && !ticket.Active && !critical[0]);
    CHECK(Bc250DmaGateStop(&gate) == STATUS_INVALID_DEVICE_STATE);
    CHECK(Bc250DmaGateQuiesce(&gate) == STATUS_INVALID_DEVICE_STATE);
    CHECK(!mutexPuts); /* Deliberate quarantine leak in fake world, not cleanup. */
    eventGate = NULL; /* Fixture destruction, not recovery of kernel storage. */
}
static void TestCompositionAndGenerations(void)
{
    BC250_DMA_GATE gate; BC250_DMA_GATE_TICKET ticket = {0};
    ULONGLONG last = 0;
    unsigned int i;
    reset(); ResetGate(&gate);
    CHECK(Bc250DmaGateEnter(&gate, &ticket) == STATUS_SUCCESS);
    openOwner();
    CHECK(Bc250WinDmaAcquire(&owner, &mdl, 0, 4096, TRUE) == STATUS_SUCCESS);
    CHECK(irql == 0 && live && owner.State == BC250_WIN_DMA_HELD);
    CHECK(Bc250DmaGateLeave(&gate, &ticket) == STATUS_SUCCESS && live && !frees);
    QuiesceGate(&gate);
    CHECK(live && !frees && !adapterPuts); /* Gate closure is NOT DMA release permission. */
    CHECK(Bc250WinDmaRelease(&owner) == STATUS_SUCCESS); closeOwner();
    /* Standalone fake-only resource teardown; no joined Windows owner exists. */
    ResetGate(&gate);
    for (i = 0; i < 256; ++i) {
        memset(&ticket, 0, sizeof(ticket)); threadIndex = i % 3;
        CHECK(Bc250DmaGateEnter(&gate, &ticket) == STATUS_SUCCESS);
        CHECK(ticket.Id > last && critical[threadIndex] == 1); last = ticket.Id;
        CHECK(Bc250DmaGateLeave(&gate, &ticket) == STATUS_SUCCESS && !critical[threadIndex]);
    }
    QuiesceGate(&gate);
}
int main(void)
{
    unsigned int baseline;
    CHECK(Bc250GateAdapterRegression() == 0); baseline = checks;
    TestLifecycle(); TestStopRaces(); TestValidation(); TestQuarantine(); TestCompositionAndGenerations();
    printf("PASS: %u additional gate checks (%u total), actual source with fake DDIs; NOT real Windows rundown/concurrency\n",
        checks - baseline, checks);
    return 0;
}
