/* SPDX-License-Identifier: Apache-2.0 */
#define BC250_DMA_ADAPTER_MOCK 1
#define BC250_DMA_GATE_MOCK 1
#define BC250_DMA_CAPTURE_MOCK 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../dma-windows-20261004/bc250_dma_adapter.c"
#include "../dma-gate-20261004/bc250_dma_gate.c"
#include "bc250_dma_capture.c" /* ACTUAL composition and frozen dependencies. */

BOOLEAN Bc250MockExecutionAllowed = TRUE;
static unsigned checks, irql, calls, gets, frees, adapterPuts, inits, live, mapRegisters;
static unsigned noAdapter, nullList, reenter, failureWithList;
static ULONGLONG expectedGetOffset, observedGetOffset;
static ULONG observedGetBytes;
static BOOLEAN observedGetDirection;
static unsigned stopSnapshotPublication, faultSnapshotPublication;
static NTSTATUS initStatus, getStatus;
static DMA_OPERATIONS operations;
static DMA_ADAPTER adapter;
static DEVICE_OBJECT pdo;
static DEVICE_DESCRIPTION description;
static SCATTER_GATHER_LIST list;
static MDL mdl;
static BC250_DMA_CAPTURE leaseFixture;
#define owner (leaseFixture.Native)
static void LeaseEvent(unsigned int phase);
#define CHECK(expression) do { ++checks; if (!(expression)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expression); exit(1); } } while (0)

static void reentry(void)
{
    if (reenter) {
        CHECK(Bc250WinDmaClose(&owner) == STATUS_DEVICE_BUSY);
        CHECK(Bc250WinDmaRelease(&owner) == STATUS_DEVICE_BUSY);
        CHECK(Bc250WinDmaCancel(&owner) == STATUS_DEVICE_BUSY);
        CHECK(Bc250WinDmaAcquire(&owner, &mdl, 0, 4096, TRUE) == STATUS_DEVICE_BUSY);
        CHECK(Bc250WinDmaOpen(&owner, &pdo, &description) == STATUS_DEVICE_BUSY);
    }
}
unsigned char KeGetCurrentIrql(void) { ++calls; return (unsigned char)irql; }
static void put(PDMA_ADAPTER a)
{
    ++calls; ++adapterPuts; CHECK(a == &adapter); CHECK(!live); LeaseEvent(5); reentry();
}
static NTSTATUS initialize(PDMA_ADAPTER a, void *context)
{
    ++calls; ++inits; CHECK(a == &adapter); CHECK(context == owner.TransferContext);
    CHECK(!live); LeaseEvent(2); reentry(); return initStatus;
}
static NTSTATUS get(PDMA_ADAPTER a, PDEVICE_OBJECT device, void *context,
    PMDL m, ULONGLONG offset, ULONG bytes, ULONG flags, void *callback,
    void *callbackContext, BOOLEAN direction, void *completion, void *completionContext,
    PSCATTER_GATHER_LIST *out)
{
    ++calls; ++gets;
    CHECK(a == &adapter && device == &pdo && context == owner.TransferContext);
    CHECK(m == &mdl && offset == expectedGetOffset && bytes >= 4096 && direction <= TRUE);
    observedGetOffset = offset; observedGetBytes = bytes; observedGetDirection = direction;
    CHECK(flags == DMA_SYNCHRONOUS_CALLBACK);
    CHECK(!callback && !callbackContext && !completion && !completionContext);
    CHECK(out == &owner.List && !*out && !live); LeaseEvent(3); reentry();
    if (getStatus == STATUS_SUCCESS) { live = 1; *out = nullList ? NULL : &list; }
    if (failureWithList) { live = 1; *out = &list; } /* Broken fake provider only. */
    return getStatus;
}
static void freeObject(PDMA_ADAPTER a, IO_ALLOCATION_ACTION action)
{
    ++calls; ++frees; CHECK(a == &adapter && action == DeallocateObject);
    CHECK(live == 1); LeaseEvent(4); reentry(); live = 0; memset(&list, 0xDD, sizeof(list));
}
PDMA_ADAPTER IoGetDmaAdapter(PDEVICE_OBJECT device, DEVICE_DESCRIPTION *d, ULONG *registers)
{
    ++calls; CHECK(device == &pdo); CHECK(d->Version == DEVICE_DESCRIPTION_VERSION3);
    CHECK(irql == PASSIVE_LEVEL); LeaseEvent(1); reentry(); *registers = mapRegisters;
    return noAdapter ? NULL : &adapter;
}
static void reset(void)
{
    /* New fake fixture only; never resets a real live owner. */
    memset(&owner, 0, sizeof(owner)); memset(&description, 0, sizeof(description));
    memset(&mdl, 0, sizeof(mdl)); memset(&list, 0, sizeof(list));
    memset(&operations, 0, sizeof(operations)); memset(&adapter, 0, sizeof(adapter));
    irql = calls = gets = frees = adapterPuts = inits = live = noAdapter = nullList = reenter = 0;
    failureWithList = 0;
    expectedGetOffset = observedGetOffset = observedGetBytes = observedGetDirection = 0;
    stopSnapshotPublication = faultSnapshotPublication = 0;
    Bc250MockExecutionAllowed = TRUE; mapRegisters = 64;
    initStatus = getStatus = STATUS_SUCCESS;
    operations.Size = sizeof(operations); operations.PutDmaAdapter = put;
    operations.InitializeDmaTransferContext = initialize;
    operations.GetScatterGatherListEx = get; operations.FreeAdapterObject = freeObject;
    adapter.Version = 1; adapter.DmaOperations = &operations;
    description.Version = DEVICE_DESCRIPTION_VERSION3; description.Master = TRUE;
    description.ScatterGather = TRUE; description.InterfaceType = PCIBus;
    description.DmaAddressWidth = 48; description.MaximumLength = BC250_WIN_DMA_MAX_BYTES;
    mdl.MdlFlags = MDL_PAGES_LOCKED; mdl.ByteCount = BC250_WIN_DMA_MAX_BYTES;
    list.NumberOfElements = 1; list.Elements[0].Address.QuadPart = 0x100000;
    list.Elements[0].Length = 4096;
}

/* Original repo-owned provider and gate fixtures reused, extended with
 * composition observations. Fake thread switches are NOT real concurrency. */
BOOLEAN Bc250MockGateExecutionAllowed = TRUE;
static unsigned int threadIndex, critical[3], gateCalls, rundownGets, rundownPuts, waits, mutexPuts;
static unsigned int denyRundown, stopAtAcquire, stopAtMutex;
static int fakeThreads[3];
static BC250_DMA_GATE *eventGate;
static BC250_DMA_GATE_TICKET *completeTicket;
static unsigned int completeThread;
static NTSTATUS fakeWaitStatus = STATUS_SUCCESS;
static unsigned int completeAsFault, faultAtMetadata;
static unsigned int stopAtGateClose;
static unsigned int delayedStopUntilRetired;
BOOLEAN Bc250MockCaptureExecutionAllowed = TRUE;

PVOID KeGetCurrentThread(void) { ++gateCalls; return &fakeThreads[threadIndex]; }
SIZE_T RtlCompareMemory(const void *a, const void *b, SIZE_T bytes)
{
    const unsigned char *x = a, *y = b; SIZE_T i;
    for (i = 0; i < bytes && x[i] == y[i]; ++i) {}
    return i;
}
LONG InterlockedCompareExchange(volatile LONG *target, LONG value, LONG compare)
{
    LONG old;
    if (delayedStopUntilRetired && target == &leaseFixture.Gate.State &&
        value == BC250_DMA_GATE_STOPPING && compare == BC250_DMA_GATE_RUNNING) {
        unsigned int pausedThread = threadIndex;
        delayedStopUntilRetired = 0; threadIndex = 1;
        CHECK(Bc250DmaCaptureRetire(&leaseFixture) == STATUS_SUCCESS);
        CHECK(leaseFixture.State == BC250_DMA_CAPTURE_RETIRED);
        threadIndex = pausedThread;
    }
    old = *target; ++gateCalls; if (old == compare) *target = value; return old;
}
LONG InterlockedExchange(volatile LONG *target, LONG value)
{
    LONG old = *target; ++gateCalls; *target = value;
    if (stopAtGateClose && target == &leaseFixture.Gate.State && value == BC250_DMA_GATE_CLOSED)
        CHECK(Bc250DmaCaptureStop(&leaseFixture) == STATUS_SUCCESS);
    return old;
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
    if (stopAtMutex && mutex == &leaseFixture.Gate.Mutex) CHECK(Bc250DmaCaptureStop(&leaseFixture) == STATUS_SUCCESS);
    if (faultAtMetadata && mutex == &leaseFixture.Metadata) Bc250DmaCaptureFault(&leaseFixture);
    return STATUS_SUCCESS;
}
LONG KeReleaseMutex(PRKMUTEX mutex, BOOLEAN wait)
{
    ++gateCalls; ++mutexPuts;
    CHECK(!wait && critical[threadIndex] && irql == 0 && mutex->Held == 1);
    CHECK(mutex->Thread == &fakeThreads[threadIndex]);
    mutex->Held = 0; mutex->Thread = NULL;
    if (mutex == &leaseFixture.Metadata && stopSnapshotPublication) {
        stopSnapshotPublication = 0; CHECK(Bc250DmaCaptureStop(&leaseFixture) == STATUS_SUCCESS);
    }
    if (mutex == &leaseFixture.Metadata && faultSnapshotPublication) {
        faultSnapshotPublication = 0; owner.State = BC250_WIN_DMA_QUARANTINED;
    }
    return 0;
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
    if (stopAtAcquire) CHECK(Bc250DmaCaptureStop(&leaseFixture) == STATUS_SUCCESS);
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
    BC250_DMA_CAPTURE_HANDLE rejectedLease = {0};
    unsigned int coordinator = threadIndex;
    ++gateCalls; CHECK(irql == 0 && rundown->Initialized && !rundown->Closing);
    rundown->Closing = 1;
    CHECK(Bc250DmaGateQuiesce(eventGate) == STATUS_DEVICE_BUSY);
    CHECK(Bc250DmaCaptureRetire(&leaseFixture) == STATUS_DEVICE_BUSY);
    CHECK(Bc250DmaCaptureAcquire(&leaseFixture, &mdl, 0, 4096, TRUE, &rejectedLease) == STATUS_CANCELLED);
    CHECK(Bc250DmaGateStop(eventGate) == STATUS_SUCCESS);
    CHECK(Bc250DmaGateEnter(eventGate, &rejected) == STATUS_CANCELLED);
    if (rundown->Count) {
        CHECK(completeTicket && completeThread != coordinator);
        threadIndex = completeThread;
        if (completeAsFault) {
            owner.State = BC250_WIN_DMA_QUARANTINED;
            CHECK(Bc250DmaCaptureFinish(&leaseFixture, completeTicket, STATUS_INVALID_DEVICE_STATE) == STATUS_INVALID_DEVICE_STATE);
        } else {
            CHECK(Bc250DmaCaptureFinish(&leaseFixture, completeTicket, STATUS_SUCCESS) == STATUS_SUCCESS);
        }
        threadIndex = coordinator; completeTicket = NULL;
    }
    CHECK(!rundown->Count && !eventGate->Mutex.Held);
    /* Deterministic scheduler event, NOT a real concurrent thread or wait. */
}


static unsigned int stopAtProvider, providerPhases;
static void LeaseEvent(unsigned int phase)
{
    BC250_DMA_CAPTURE_HANDLE out = {0}, input = {0};
    unsigned int oldGets = gets, oldFrees = frees, oldPuts = adapterPuts;
    NTSTATUS admission = leaseFixture.State == BC250_DMA_CAPTURE_ACTIVE ? STATUS_DEVICE_BUSY : STATUS_CANCELLED;
    providerPhases |= 1U << phase;
    if (leaseFixture.Coordinator) {
        CHECK(leaseFixture.Gate.State == BC250_DMA_GATE_CLOSED && !leaseFixture.Gate.Rundown.Count);
        CHECK(leaseFixture.Metadata.Held && !leaseFixture.References);
    } else CHECK(leaseFixture.Gate.Rundown.Count == 1 && leaseFixture.Gate.Mutex.Held);
    CHECK(Bc250DmaCaptureAcquire(&leaseFixture, &mdl, 0, 4096, TRUE, &out) == admission);
    CHECK(Bc250DmaCaptureRetain(&leaseFixture, &input, &out) == admission);
    CHECK(Bc250DmaCaptureRetire(&leaseFixture) == STATUS_DEVICE_BUSY);
    CHECK(Bc250DmaCaptureInit(&leaseFixture, &pdo, &description) == STATUS_INVALID_DEVICE_STATE);
    CHECK(Bc250DmaCaptureDrop(&leaseFixture, &input) ==
        (leaseFixture.Metadata.Held ? STATUS_DEVICE_BUSY : STATUS_INVALID_DEVICE_STATE));
    if (stopAtProvider == phase) CHECK(Bc250DmaCaptureStop(&leaseFixture) == STATUS_SUCCESS);
    CHECK(gets == oldGets && frees == oldFrees && adapterPuts == oldPuts);
}
static void ResetLeases(void)
{
    /* NEW isolated fake world only, never reset a real retired/faulted object. */
    memset(&leaseFixture, 0, sizeof(leaseFixture)); reset(); memset(critical, 0, sizeof(critical));
    threadIndex = gateCalls = rundownGets = rundownPuts = waits = mutexPuts = 0;
    denyRundown = stopAtAcquire = stopAtMutex = completeAsFault = faultAtMetadata = 0;
    stopAtGateClose = 0;
    delayedStopUntilRetired = 0;
    stopAtProvider = providerPhases = 0; eventGate = &leaseFixture.Gate;
    completeTicket = NULL; fakeWaitStatus = STATUS_SUCCESS;
    Bc250MockGateExecutionAllowed = Bc250MockCaptureExecutionAllowed = TRUE; reenter = 1;
}
static void OpenLeases(void)
{
    CHECK(Bc250DmaCaptureInit(&leaseFixture, &pdo, &description) == STATUS_SUCCESS);
    CHECK(owner.State == BC250_WIN_DMA_OPEN && !leaseFixture.References);
    CHECK(!critical[threadIndex] && !leaseFixture.Gate.Rundown.Count);
}
static BC250_DMA_CAPTURE_HANDLE AcquireLease(void)
{
    BC250_DMA_CAPTURE_HANDLE handle = {0};
    CHECK(Bc250DmaCaptureAcquire(&leaseFixture, &mdl, 0, 4096, TRUE, &handle) == STATUS_SUCCESS);
    CHECK(handle.Owner == &leaseFixture && handle.Id && live && leaseFixture.References == 1);
    CHECK(!leaseFixture.Gate.Rundown.Count && !leaseFixture.Gate.Mutex.Held && !leaseFixture.Metadata.Held);
    return handle;
}
static void CheckRetired(void)
{
    CHECK(leaseFixture.State == BC250_DMA_CAPTURE_RETIRED && !leaseFixture.Coordinator);
    CHECK(leaseFixture.Gate.State == BC250_DMA_GATE_CLOSED && !leaseFixture.Gate.Rundown.Count);
    CHECK(!leaseFixture.References && !live && !leaseFixture.Metadata.Held);
    CHECK(rundownGets == rundownPuts && !critical[0] && !critical[1] && !critical[2]);
    CHECK(Bc250DmaCaptureRetire(&leaseFixture) == STATUS_INVALID_DEVICE_STATE);
}
static void TestPersistentDrain(void)
{
    BC250_DMA_CAPTURE_HANDLE first, second = {0}, out = {0};
    ResetLeases(); OpenLeases(); first = AcquireLease();
    CHECK(Bc250DmaCaptureRetain(&leaseFixture, &first, &second) == STATUS_SUCCESS);
    CHECK(leaseFixture.References == 2 && second.Id > first.Id);
    CHECK(Bc250DmaCaptureStop(&leaseFixture) == STATUS_SUCCESS);
    stopAtGateClose = 1; /* STOP may race Gate CLOSED before DRAINED publication. */
    CHECK(Bc250DmaCaptureRetain(&leaseFixture, &first, &out) == STATUS_CANCELLED);
    CHECK(Bc250DmaCaptureRetire(&leaseFixture) == STATUS_DEVICE_BUSY);
    CHECK(leaseFixture.State == BC250_DMA_CAPTURE_DRAINED && live && !frees && !adapterPuts && !leaseFixture.Coordinator);
    CHECK(Bc250DmaCaptureDrop(&leaseFixture, &first) == STATUS_SUCCESS);
    CHECK(Bc250DmaCaptureDrop(&leaseFixture, &first) == STATUS_INVALID_DEVICE_STATE);
    CHECK(leaseFixture.References == 1 && live && !frees);
    CHECK(Bc250DmaCaptureRetire(&leaseFixture) == STATUS_DEVICE_BUSY);
    CHECK(Bc250DmaCaptureDrop(&leaseFixture, &second) == STATUS_SUCCESS);
    CHECK(!leaseFixture.References && live && !frees && !adapterPuts); /* Last Drop NEVER cleanup. */
    CHECK(Bc250DmaCaptureRetire(&leaseFixture) == STATUS_SUCCESS);
    CHECK(frees == 1 && adapterPuts == 1); CheckRetired();
    CHECK(Bc250DmaCaptureDrop(&leaseFixture, &second) == STATUS_INVALID_DEVICE_STATE);
}
static void TestStopPublication(void)
{
    BC250_DMA_CAPTURE_HANDLE handle, out;
    unsigned int phase;
    for (phase = 1; phase <= 3; ++phase) {
        ResetLeases(); memset(&handle, 0, sizeof(handle)); memset(&out, 0, sizeof(out));
        if (phase == 1) stopAtProvider = phase;
        OpenLeases();
        if (phase > 1) {
            stopAtProvider = phase; handle = AcquireLease();
            CHECK(leaseFixture.State == BC250_DMA_CAPTURE_STOPPING && leaseFixture.References == 1);
        }
        CHECK(Bc250DmaCaptureAcquire(&leaseFixture, &mdl, 0, 4096, TRUE, &out) == STATUS_CANCELLED);
        if (phase > 1) {
            CHECK(Bc250DmaCaptureRetire(&leaseFixture) == STATUS_DEVICE_BUSY && live && !frees);
            CHECK(Bc250DmaCaptureDrop(&leaseFixture, &handle) == STATUS_SUCCESS);
        }
        CHECK(Bc250DmaCaptureRetire(&leaseFixture) == STATUS_SUCCESS);
        CHECK(frees == (phase == 1 ? 0U : 1U) && adapterPuts == 1);
        CHECK(providerPhases & (1U << phase)); CheckRetired();
    }
}
static void TestDelayedStopAfterRetirement(void)
{
    BC250_DMA_CAPTURE_HANDLE handle;
    ResetLeases(); OpenLeases(); handle = AcquireLease();
    CHECK(Bc250DmaCaptureDrop(&leaseFixture, &handle) == STATUS_SUCCESS);
    delayedStopUntilRetired = 1;
    CHECK(Bc250DmaCaptureStop(&leaseFixture) == STATUS_SUCCESS);
    CHECK(frees == 1 && adapterPuts == 1); CheckRetired();
    /* A NEW Stop after retirement still rejects, without changing terminal state. */
    CHECK(Bc250DmaCaptureStop(&leaseFixture) == STATUS_INVALID_DEVICE_STATE);
    CHECK(leaseFixture.State == BC250_DMA_CAPTURE_RETIRED && frees == 1 && adapterPuts == 1);
}
static void TestSlotsAndAliases(void)
{
    BC250_DMA_CAPTURE_HANDLE handles[16], stale, wrong, out = {0}, snapshot;
    unsigned int i;
    ResetLeases(); OpenLeases(); memset(handles, 0, sizeof(handles)); handles[0] = AcquireLease();
    for (i = 1; i < 16; ++i) CHECK(Bc250DmaCaptureRetain(&leaseFixture, &handles[0], &handles[i]) == STATUS_SUCCESS);
    CHECK(Bc250DmaCaptureRetain(&leaseFixture, &handles[0], &out) == STATUS_INSUFFICIENT_RESOURCES && !out.Owner);
    snapshot = handles[1];
    CHECK(Bc250DmaCaptureRetain(&leaseFixture, &handles[0], &handles[1]) == STATUS_INVALID_PARAMETER);
    CHECK(!memcmp(&snapshot, &handles[1], sizeof(snapshot)) && leaseFixture.References == 16);
    CHECK(Bc250DmaCaptureRetain(&leaseFixture, &handles[0], &handles[0]) == STATUS_INVALID_PARAMETER);
    CHECK(Bc250DmaCaptureRetain(&leaseFixture, &handles[0], (BC250_DMA_CAPTURE_HANDLE *)&mdl) == STATUS_INVALID_PARAMETER);
    CHECK(Bc250DmaCaptureRetain(&leaseFixture, &handles[0], (BC250_DMA_CAPTURE_HANDLE *)&list) == STATUS_INVALID_PARAMETER);
    CHECK(Bc250DmaCaptureDrop(&leaseFixture, (BC250_DMA_CAPTURE_HANDLE *)&list) == STATUS_INVALID_PARAMETER);
    CHECK(Bc250DmaCaptureRetain(&leaseFixture, &handles[0], (BC250_DMA_CAPTURE_HANDLE *)&leaseFixture.Metadata) == STATUS_INVALID_PARAMETER);
    wrong = handles[0]; wrong.Owner = NULL;
    CHECK(Bc250DmaCaptureDrop(&leaseFixture, &wrong) == STATUS_INVALID_DEVICE_STATE && leaseFixture.References == 16);
    stale = handles[1]; CHECK(Bc250DmaCaptureDrop(&leaseFixture, &handles[1]) == STATUS_SUCCESS);
    memset(&handles[1], 0, sizeof(handles[1]));
    CHECK(Bc250DmaCaptureRetain(&leaseFixture, &handles[0], &handles[1]) == STATUS_SUCCESS);
    CHECK(handles[1].Slot == stale.Slot && handles[1].Id > stale.Id);
    CHECK(Bc250DmaCaptureDrop(&leaseFixture, &stale) == STATUS_INVALID_DEVICE_STATE && leaseFixture.References == 16);
    for (i = 0; i < 16; ++i) CHECK(Bc250DmaCaptureDrop(&leaseFixture, &handles[i]) == STATUS_SUCCESS);
    CHECK(!frees && live);
    CHECK(Bc250DmaCaptureRetire(&leaseFixture) == STATUS_SUCCESS); CheckRetired();
    ResetLeases(); OpenLeases(); handles[0] = AcquireLease();
    CHECK(Bc250DmaCaptureLock(&leaseFixture) == STATUS_SUCCESS);
    leaseFixture.LastId = ~(ULONGLONG)0; /* Test-only exhaustion; no production reset. */
    Bc250DmaCaptureUnlock(&leaseFixture);
    memset(&out, 0, sizeof(out));
    CHECK(Bc250DmaCaptureRetain(&leaseFixture, &handles[0], &out) == STATUS_INSUFFICIENT_RESOURCES && !out.Owner);
    CHECK(Bc250DmaCaptureDrop(&leaseFixture, &handles[0]) == STATUS_SUCCESS);
    CHECK(Bc250DmaCaptureRetire(&leaseFixture) == STATUS_SUCCESS); CheckRetired();
}
static void TestMetadataAndWait(void)
{
    BC250_DMA_CAPTURE_HANDLE handle;
    BC250_DMA_GATE_TICKET ticket = {0};
    ResetLeases(); OpenLeases(); handle = AcquireLease();
    CHECK(Bc250DmaCaptureLock(&leaseFixture) == STATUS_SUCCESS); threadIndex = 1;
    CHECK(Bc250DmaCaptureDrop(&leaseFixture, &handle) == STATUS_DEVICE_BUSY);
    CHECK(Bc250DmaCaptureRetire(&leaseFixture) == STATUS_DEVICE_BUSY);
    CHECK(leaseFixture.State == BC250_DMA_CAPTURE_DRAINED && live && !frees);
    threadIndex = 0; Bc250DmaCaptureUnlock(&leaseFixture);
    CHECK(Bc250DmaCaptureDrop(&leaseFixture, &handle) == STATUS_SUCCESS);
    CHECK(Bc250DmaCaptureRetire(&leaseFixture) == STATUS_SUCCESS); CheckRetired();
    ResetLeases(); OpenLeases(); handle = AcquireLease();
    CHECK(Bc250DmaCaptureAdmit(&leaseFixture, &ticket) == STATUS_SUCCESS);
    /* Split protected operation in fake world, no real Get is pending. */
    completeTicket = &ticket; completeThread = 0; threadIndex = 1;
    CHECK(Bc250DmaCaptureRetire(&leaseFixture) == STATUS_DEVICE_BUSY);
    CHECK(!leaseFixture.Gate.Rundown.Count && live && !frees && leaseFixture.References == 1);
    CHECK(Bc250DmaCaptureDrop(&leaseFixture, &handle) == STATUS_SUCCESS);
    CHECK(Bc250DmaCaptureRetire(&leaseFixture) == STATUS_SUCCESS); CheckRetired();
}
static void TestFaultRetention(void)
{
    BC250_DMA_CAPTURE_HANDLE handle;
    unsigned int stage;
    for (stage = 0; stage < 4; ++stage) {
        BC250_DMA_GATE_TICKET ticket = {0};
        ResetLeases(); OpenLeases(); handle = AcquireLease();
        if (stage == 0) {
            CHECK(Bc250DmaCaptureAdmit(&leaseFixture, &ticket) == STATUS_SUCCESS);
            completeTicket = &ticket; completeThread = 0; completeAsFault = 1; threadIndex = 1;
        }
        if (stage == 1) {
            CHECK(Bc250DmaCaptureDrop(&leaseFixture, &handle) == STATUS_SUCCESS);
            Bc250MockExecutionAllowed = FALSE;
        }
        if (stage == 2) faultAtMetadata = 1; /* Fault arrives after earlier coordinator check. */
        if (stage == 3) {
            CHECK(Bc250DmaCaptureLock(&leaseFixture) == STATUS_SUCCESS);
            ++leaseFixture.References; /* Deliberate corruption in isolated fake memory. */
            Bc250DmaCaptureUnlock(&leaseFixture);
        }
        CHECK(Bc250DmaCaptureRetire(&leaseFixture) != STATUS_SUCCESS);
        CHECK(leaseFixture.State == BC250_DMA_CAPTURE_FAULT && live && !frees && !adapterPuts);
        CHECK(owner.List == &list && owner.HeldMdl == &mdl);
        CHECK(Bc250DmaCaptureRetire(&leaseFixture) == STATUS_INVALID_DEVICE_STATE);
        if (stage == 0) CHECK(Bc250DmaCaptureDrop(&leaseFixture, &handle) == STATUS_SUCCESS && !frees);
        eventGate = NULL; /* Discard quarantined RAM world, never real cleanup. */
    }
}
static void TestGuardsAndKnownFailures(void)
{
    BC250_DMA_CAPTURE snapshot;
    BC250_DMA_CAPTURE_HANDLE handle = {0}, out = {0};
    unsigned int oldCalls, oldGateCalls, scenario;
    ResetLeases(); snapshot = leaseFixture; oldCalls = calls; oldGateCalls = gateCalls;
    Bc250MockCaptureExecutionAllowed = FALSE;
    CHECK(Bc250DmaCaptureInit(NULL, NULL, NULL) == STATUS_NOT_SUPPORTED);
    CHECK(Bc250DmaCaptureAcquire(NULL, NULL, 0, 0, FALSE, NULL) == STATUS_NOT_SUPPORTED);
    CHECK(Bc250DmaCaptureRetain(NULL, NULL, NULL) == STATUS_NOT_SUPPORTED);
    CHECK(Bc250DmaCaptureDrop(NULL, NULL) == STATUS_NOT_SUPPORTED);
    CHECK(Bc250DmaCaptureStop(NULL) == STATUS_NOT_SUPPORTED);
    CHECK(Bc250DmaCaptureRetire(NULL) == STATUS_NOT_SUPPORTED);
    CHECK(oldCalls == calls && oldGateCalls == gateCalls && !memcmp(&snapshot, &leaseFixture, sizeof(snapshot)));
    Bc250MockCaptureExecutionAllowed = TRUE; OpenLeases(); handle = AcquireLease();
    snapshot = leaseFixture; oldGateCalls = gateCalls; irql = 1;
    CHECK(Bc250DmaCaptureDrop(&leaseFixture, &handle) == STATUS_INVALID_DEVICE_STATE);
    CHECK(Bc250DmaCaptureRetire(&leaseFixture) == STATUS_INVALID_DEVICE_STATE);
    CHECK(gateCalls == oldGateCalls && !memcmp(&snapshot, &leaseFixture, sizeof(snapshot))); irql = 0;
    CHECK(Bc250DmaCaptureDrop(&leaseFixture, &handle) == STATUS_SUCCESS);
    CHECK(Bc250DmaCaptureRetire(&leaseFixture) == STATUS_SUCCESS); CheckRetired();
    for (scenario = 0; scenario < 4; ++scenario) {
        ResetLeases(); memset(&out, 0, sizeof(out));
        if (scenario == 0) noAdapter = 1;
        if (scenario == 1) operations.GetScatterGatherListEx = NULL;
        if (scenario <= 1) CHECK(Bc250DmaCaptureInit(&leaseFixture, &pdo, &description) != STATUS_SUCCESS);
        else {
            OpenLeases();
            if (scenario == 2) getStatus = STATUS_INSUFFICIENT_RESOURCES;
            if (scenario == 3) list.Elements[0].Length = 4097;
            CHECK(Bc250DmaCaptureAcquire(&leaseFixture, &mdl, 0, 4096, TRUE, &out) != STATUS_SUCCESS);
        }
        CHECK(!out.Owner && !leaseFixture.References);
        CHECK(Bc250DmaCaptureRetire(&leaseFixture) == STATUS_SUCCESS); CheckRetired();
    }
}
static int RunLeaseRegressions(void)
{
    TestPersistentDrain(); TestStopPublication(); TestDelayedStopAfterRetirement(); TestSlotsAndAliases();
    TestMetadataAndWait(); TestFaultRetention(); TestGuardsAndKnownFailures();
    printf("PASS: %u capture-variant regression checks; frozen gate/native with fake DDIs, fake DDIs only; NO real maps/Windows DMA/PnP/concurrency\n", checks);
    return 0;
}

/* Original snapshot tests. Input corruption and scheduling hooks are isolated
 * RAM instrumentation only, never production reset/recovery or concurrency.
 */
static int SnapshotZero(const BC250_DMA_CAPTURE_SNAPSHOT *s)
{
    static const BC250_DMA_CAPTURE_SNAPSHOT zero = {0};
    return memcmp(s, &zero, sizeof(zero)) == 0;
}
static BC250_DMA_CAPTURE_HANDLE AcquireRequest(ULONG pages, ULONGLONG offset, BOOLEAN direction)
{
    BC250_DMA_CAPTURE_HANDLE seed = {0};
    expectedGetOffset = offset;
    list.NumberOfElements = pages == 4 ? 3 : 1;
    list.Elements[0].Address.QuadPart = 0xB00000;
    list.Elements[0].Length = pages == 4 ? 8192 : pages * 4096;
    if (pages == 4) {
        list.Elements[1].Address.QuadPart = 0x50000; list.Elements[1].Length = 4096;
        list.Elements[2].Address.QuadPart = 0xB00000; list.Elements[2].Length = 4096;
    }
    CHECK(Bc250DmaCaptureAcquire(&leaseFixture, &mdl, offset, pages * 4096, direction, &seed) == STATUS_SUCCESS);
    CHECK(leaseFixture.RequestValid && leaseFixture.Request.Offset == observedGetOffset);
    CHECK(leaseFixture.Request.Bytes == observedGetBytes && leaseFixture.Request.Direction == observedGetDirection);
    CHECK(leaseFixture.RequestWidth == 48 && leaseFixture.References == 1);
    return seed;
}
static void SnapshotFinish(const BC250_DMA_CAPTURE_HANDLE *seed, BC250_DMA_CAPTURE_SNAPSHOT *s)
{
    CHECK(Bc250DmaCaptureDrop(&leaseFixture, seed) == STATUS_SUCCESS);
    CHECK(leaseFixture.References == 1 && live && !frees);
    CHECK(Bc250DmaCaptureRetire(&leaseFixture) == STATUS_DEVICE_BUSY);
    CHECK(Bc250DmaCaptureSnapshotRelease(&leaseFixture, s) == STATUS_SUCCESS);
    CHECK(!leaseFixture.References && live && !frees && !adapterPuts);
    CHECK(s->Self == s && s->State == BC250_DMA_CAPTURE_SNAPSHOT_RELEASED && !s->Reference.Owner);
    CHECK(Bc250DmaCaptureSnapshotRelease(&leaseFixture, s) == STATUS_INVALID_PARAMETER);
    CHECK(Bc250DmaCaptureRetire(&leaseFixture) == STATUS_SUCCESS);
    CHECK(!leaseFixture.RequestValid && !leaseFixture.RequestWidth && !leaseFixture.Request.Bytes);
    CHECK(frees == 1 && adapterPuts == 1); CheckRetired();
}
static void TestSnapshotsRequestAndIdentity(void)
{
    BC250_DMA_CAPTURE_SNAPSHOT *s = calloc(1, sizeof(*s)), *copy = calloc(1, sizeof(*copy));
    BC250_DMA_CAPTURE_REQUEST request, wrong;
    BC250_DMA_CAPTURE_HANDLE seed, duplicate, out = {0};
    const ULONGLONG expected[4] = {0xB00000, 0xB01000, 0x50000, 0xB00000};
    unsigned i;
    CHECK(s && copy); ResetLeases(); OpenLeases(); seed = AcquireRequest(4, 8192, FALSE);
    request = leaseFixture.Request;
    for (i = 0; i < 3; ++i) {
        wrong = request;
        if (i == 0) wrong.Offset += 4096;
        if (i == 1) wrong.Bytes -= 4096;
        if (i == 2) wrong.Direction = TRUE;
        CHECK(Bc250DmaCaptureSnapshot(&leaseFixture, &seed, &wrong, s) == STATUS_INVALID_PARAMETER);
        CHECK(SnapshotZero(s) && leaseFixture.References == 1 && leaseFixture.State == BC250_DMA_CAPTURE_ACTIVE);
    }
    CHECK(Bc250DmaCaptureSnapshot(&leaseFixture, &seed, &request, s) == STATUS_SUCCESS);
    CHECK(s->Self == s && s->State == BC250_DMA_CAPTURE_SNAPSHOT_LIVE && s->PageCount == 4 && s->ElementCount == 3);
    CHECK(s->Request.Offset == observedGetOffset && s->Request.Bytes == observedGetBytes &&
        s->Request.Direction == observedGetDirection && leaseFixture.References == 2);
    for (i = 0; i < 4; ++i) CHECK(s->Pages[i].Domain == BC250_AD_DMA_LOGICAL &&
        s->Pages[i].Start == expected[i] && s->Pages[i].Last == expected[i] + 4095 && s->Pages[i].Bytes == 4096);
    *copy = *s;
    CHECK(Bc250DmaCaptureSnapshotRelease(&leaseFixture, copy) == STATUS_INVALID_PARAMETER);
    copy->Self = copy; /* Even forged Self cannot match canonical record pointer. */
    CHECK(Bc250DmaCaptureSnapshotRelease(&leaseFixture, copy) == STATUS_INVALID_DEVICE_STATE);
    duplicate = s->Reference;
    CHECK(Bc250DmaCaptureDrop(&leaseFixture, &duplicate) == STATUS_INVALID_PARAMETER);
    CHECK(Bc250DmaCaptureRetain(&leaseFixture, &duplicate, &out) == STATUS_INVALID_PARAMETER && !out.Owner);
    memset(copy, 0, sizeof(*copy));
    CHECK(Bc250DmaCaptureSnapshot(&leaseFixture, &duplicate, &request, copy) == STATUS_INVALID_PARAMETER);
    CHECK(SnapshotZero(copy) && leaseFixture.References == 2);
    CHECK(Bc250DmaCaptureSnapshot(&leaseFixture, &seed, &request, s) == STATUS_INVALID_PARAMETER);
    CHECK(Bc250DmaCaptureSnapshot(&leaseFixture, &seed, &request,
        (BC250_DMA_CAPTURE_SNAPSHOT *)&list) == STATUS_INVALID_PARAMETER);
    CHECK(Bc250DmaCaptureSnapshot(&leaseFixture, &seed, &request,
        (BC250_DMA_CAPTURE_SNAPSHOT *)&mdl) == STATUS_INVALID_PARAMETER);
    CHECK(Bc250DmaCaptureSnapshot(&leaseFixture, &seed, (BC250_DMA_CAPTURE_REQUEST *)&list, copy) == STATUS_INVALID_PARAMETER);
    CHECK(Bc250DmaCaptureSnapshotRelease(&leaseFixture, (BC250_DMA_CAPTURE_SNAPSHOT *)&list) == STATUS_INVALID_PARAMETER);
    CHECK(leaseFixture.References == 2 && live && !frees);
    SnapshotFinish(&seed, s); free(copy); free(s);
}
static void TestSnapshotsCapacityAndRetry(void)
{
    BC250_DMA_CAPTURE_SNAPSHOT *s = calloc(17, sizeof(*s));
    BC250_DMA_CAPTURE_REQUEST request;
    BC250_DMA_CAPTURE_HANDLE seed;
    unsigned i;
    CHECK(s); ResetLeases(); OpenLeases(); seed = AcquireRequest(64, 0, TRUE); request = leaseFixture.Request;
    for (i = 0; i < 15; ++i) CHECK(Bc250DmaCaptureSnapshot(&leaseFixture, &seed, &request, &s[i]) == STATUS_SUCCESS);
    CHECK(leaseFixture.References == 16);
    for (i = 0; i < 64; ++i) CHECK(s[0].Pages[i].Start == 0xB00000 + (ULONGLONG)i * 4096);
    CHECK(Bc250DmaCaptureSnapshot(&leaseFixture, &seed, &request, &s[15]) == STATUS_INSUFFICIENT_RESOURCES);
    CHECK(SnapshotZero(&s[15]) && leaseFixture.References == 16);
    CHECK(Bc250DmaCaptureSnapshotRelease(&leaseFixture, &s[0]) == STATUS_SUCCESS);
    CHECK(Bc250DmaCaptureSnapshot(&leaseFixture, &seed, &request, &s[15]) == STATUS_SUCCESS);
    CHECK(s[15].Reference.Slot == 1 && s[15].Reference.Id > 2);
    CHECK(Bc250DmaCaptureSnapshotRelease(&leaseFixture, &s[0]) == STATUS_INVALID_PARAMETER);
    CHECK(Bc250DmaCaptureSnapshot(&leaseFixture, &seed, &request, &s[0]) == STATUS_INVALID_PARAMETER);
    CHECK(Bc250DmaCaptureDrop(&leaseFixture, &seed) == STATUS_SUCCESS);
    CHECK(Bc250DmaCaptureRetire(&leaseFixture) == STATUS_DEVICE_BUSY);
    threadIndex = 1; CHECK(Bc250DmaCaptureLock(&leaseFixture) == STATUS_SUCCESS); threadIndex = 0;
    CHECK(Bc250DmaCaptureSnapshotRelease(&leaseFixture, &s[1]) == STATUS_DEVICE_BUSY);
    CHECK(s[1].State == BC250_DMA_CAPTURE_SNAPSHOT_LIVE && s[1].Reference.Id && leaseFixture.References == 15);
    threadIndex = 1; Bc250DmaCaptureUnlock(&leaseFixture); threadIndex = 0;
    for (i = 1; i <= 15; ++i) CHECK(Bc250DmaCaptureSnapshotRelease(&leaseFixture, &s[i]) == STATUS_SUCCESS);
    CHECK(!leaseFixture.References && live && !frees);
    CHECK(Bc250DmaCaptureRetire(&leaseFixture) == STATUS_SUCCESS); CheckRetired();
    memset(s, 0, 17 * sizeof(*s)); /* New RAM world only. */
    ResetLeases(); OpenLeases(); seed = AcquireRequest(1, 0, TRUE); request = leaseFixture.Request;
    CHECK(Bc250DmaCaptureLock(&leaseFixture) == STATUS_SUCCESS);
    leaseFixture.LastId = ~(ULONGLONG)0; Bc250DmaCaptureUnlock(&leaseFixture);
    CHECK(Bc250DmaCaptureSnapshot(&leaseFixture, &seed, &request, s) == STATUS_INSUFFICIENT_RESOURCES);
    CHECK(SnapshotZero(s) && leaseFixture.References == 1);
    CHECK(Bc250DmaCaptureDrop(&leaseFixture, &seed) == STATUS_SUCCESS);
    CHECK(Bc250DmaCaptureRetire(&leaseFixture) == STATUS_SUCCESS); CheckRetired(); free(s);
}
static void TestSnapshotsPublicationAndFaults(void)
{
    BC250_DMA_CAPTURE_SNAPSHOT *s = calloc(1, sizeof(*s));
    BC250_DMA_CAPTURE_HANDLE seed;
    BC250_DMA_CAPTURE_REQUEST request;
    unsigned scenario, before, oldCalls, oldGateCalls;
    CHECK(s);
    for (scenario = 0; scenario < 10; ++scenario) {
        memset(s, 0, sizeof(*s)); ResetLeases(); OpenLeases(); seed = AcquireRequest(4, 0, TRUE);
        request = leaseFixture.Request; before = leaseFixture.References;
        switch (scenario) {
        case 0: list.Elements[0].Length -= 4096; break; /* Exact request sum mismatch. */
        case 1: list.Elements[0].Length += 4096; break;
        case 2: list.NumberOfElements = 65; break;
        case 3: list.Elements[0].Address.QuadPart = 0x0001000000000000ULL; break;
        case 4: list.Elements[0].Address.QuadPart += 1; break;
        case 5: list.Elements[0].Length = 4095; break;
        case 6: leaseFixture.PinnedListBytes -= sizeof(list.Elements[0]); break;
        case 7: owner.AddressWidth = 0; break;
        case 8: mdl.MdlFlags = 0; break;
        default: owner.Cancelled = 1; break;
        }
        CHECK(Bc250DmaCaptureSnapshot(&leaseFixture, &seed, &request, s) == STATUS_INVALID_DEVICE_STATE);
        CHECK(SnapshotZero(s) && leaseFixture.References == before && live && !frees && !adapterPuts);
        CHECK(leaseFixture.State == BC250_DMA_CAPTURE_FAULT);
        CHECK(Bc250DmaCaptureDrop(&leaseFixture, &seed) == STATUS_SUCCESS);
        CHECK(Bc250DmaCaptureRetire(&leaseFixture) == STATUS_INVALID_DEVICE_STATE && live && !frees);
        /* Discard isolated quarantined RAM world only, never recovery. */
    }
    memset(s, 0, sizeof(*s)); ResetLeases(); OpenLeases(); seed = AcquireRequest(1, 0, TRUE);
    request = leaseFixture.Request; oldCalls = calls; oldGateCalls = gateCalls;
    Bc250MockCaptureExecutionAllowed = FALSE;
    CHECK(Bc250DmaCaptureSnapshot(NULL, NULL, NULL, NULL) == STATUS_NOT_SUPPORTED);
    CHECK(Bc250DmaCaptureSnapshotRelease(NULL, NULL) == STATUS_NOT_SUPPORTED);
    CHECK(calls == oldCalls && gateCalls == oldGateCalls && SnapshotZero(s));
    Bc250MockCaptureExecutionAllowed = TRUE; stopAtAcquire = 1;
    CHECK(Bc250DmaCaptureSnapshot(&leaseFixture, &seed, &request, s) == STATUS_CANCELLED);
    CHECK(SnapshotZero(s) && leaseFixture.References == 1);
    stopAtAcquire = 0; CHECK(Bc250DmaCaptureDrop(&leaseFixture, &seed) == STATUS_SUCCESS);
    CHECK(Bc250DmaCaptureRetire(&leaseFixture) == STATUS_SUCCESS); CheckRetired();
    memset(s, 0, sizeof(*s)); ResetLeases(); OpenLeases(); seed = AcquireRequest(1, 0, FALSE);
    request = leaseFixture.Request; stopSnapshotPublication = 1;
    CHECK(Bc250DmaCaptureSnapshot(&leaseFixture, &seed, &request, s) == STATUS_SUCCESS);
    CHECK(leaseFixture.State == BC250_DMA_CAPTURE_STOPPING && s->Reference.Id && s->State == BC250_DMA_CAPTURE_SNAPSHOT_LIVE);
    SnapshotFinish(&seed, s);
    memset(s, 0, sizeof(*s)); ResetLeases(); OpenLeases(); seed = AcquireRequest(1, 0, TRUE);
    request = leaseFixture.Request; faultSnapshotPublication = 1;
    CHECK(Bc250DmaCaptureSnapshot(&leaseFixture, &seed, &request, s) == STATUS_INVALID_DEVICE_STATE);
    CHECK(s->Self == s && s->State == BC250_DMA_CAPTURE_SNAPSHOT_LIVE && leaseFixture.References == 2);
    CHECK(leaseFixture.State == BC250_DMA_CAPTURE_FAULT && live && !frees);
    CHECK(Bc250DmaCaptureSnapshotRelease(&leaseFixture, s) == STATUS_SUCCESS);
    CHECK(Bc250DmaCaptureDrop(&leaseFixture, &seed) == STATUS_SUCCESS);
    CHECK(!leaseFixture.References && live && !frees && !adapterPuts);
    CHECK(Bc250DmaCaptureRetire(&leaseFixture) == STATUS_INVALID_DEVICE_STATE); free(s);
}
int main(void)
{
    unsigned before;
    CHECK(RunLeaseRegressions() == 0); before = checks;
    TestSnapshotsRequestAndIdentity(); TestSnapshotsCapacityAndRetry(); TestSnapshotsPublicationAndFaults();
    printf("PASS: %u additional capture checks (%u total); original request + retained copied SG; NO real DMA/GPU/W2P\n", checks - before, checks);
    return 0;
}
