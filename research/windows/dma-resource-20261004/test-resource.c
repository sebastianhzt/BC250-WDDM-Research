/* SPDX-License-Identifier: Apache-2.0 */
#define BC250_DMA_ADAPTER_MOCK 1
#define BC250_DMA_GATE_MOCK 1
#define BC250_DMA_RESOURCE_MOCK 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../dma-windows-20261004/bc250_dma_adapter.c"
#include "../dma-gate-20261004/bc250_dma_gate.c"
#include "bc250_dma_resource.c" /* ACTUAL composition and frozen dependencies. */

BOOLEAN Bc250MockExecutionAllowed = TRUE;
static unsigned checks, irql, calls, gets, frees, adapterPuts, inits, live, mapRegisters;
static unsigned noAdapter, nullList, reenter, failureWithList;
static NTSTATUS initStatus, getStatus;
static DMA_OPERATIONS operations;
static DMA_ADAPTER adapter;
static DEVICE_OBJECT pdo;
static DEVICE_DESCRIPTION description;
static SCATTER_GATHER_LIST list;
static MDL mdl;
static BC250_DMA_RESOURCE resourceFixture;
#define owner (resourceFixture.Native)
static void ResourceEvent(unsigned int phase);
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
    ++calls; ++adapterPuts; CHECK(a == &adapter); CHECK(!live); ResourceEvent(5); reentry();
}
static NTSTATUS initialize(PDMA_ADAPTER a, void *context)
{
    ++calls; ++inits; CHECK(a == &adapter); CHECK(context == owner.TransferContext);
    CHECK(!live); ResourceEvent(2); reentry(); return initStatus;
}
static NTSTATUS get(PDMA_ADAPTER a, PDEVICE_OBJECT device, void *context,
    PMDL m, ULONGLONG offset, ULONG bytes, ULONG flags, void *callback,
    void *callbackContext, BOOLEAN direction, void *completion, void *completionContext,
    PSCATTER_GATHER_LIST *out)
{
    ++calls; ++gets;
    CHECK(a == &adapter && device == &pdo && context == owner.TransferContext);
    CHECK(m == &mdl && offset == 0 && bytes >= 4096 && direction <= TRUE);
    CHECK(flags == DMA_SYNCHRONOUS_CALLBACK);
    CHECK(!callback && !callbackContext && !completion && !completionContext);
    CHECK(out == &owner.List && !*out && !live); ResourceEvent(3); reentry();
    if (getStatus == STATUS_SUCCESS) { live = 1; *out = nullList ? NULL : &list; }
    if (failureWithList) { live = 1; *out = &list; } /* Broken fake provider only. */
    return getStatus;
}
static void freeObject(PDMA_ADAPTER a, IO_ALLOCATION_ACTION action)
{
    ++calls; ++frees; CHECK(a == &adapter && action == DeallocateObject);
    CHECK(live == 1); ResourceEvent(4); reentry(); live = 0; memset(&list, 0xDD, sizeof(list));
}
PDMA_ADAPTER IoGetDmaAdapter(PDEVICE_OBJECT device, DEVICE_DESCRIPTION *d, ULONG *registers)
{
    ++calls; CHECK(device == &pdo); CHECK(d->Version == DEVICE_DESCRIPTION_VERSION3);
    CHECK(irql == PASSIVE_LEVEL); ResourceEvent(1); reentry(); *registers = mapRegisters;
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
static unsigned int completeAsFault;
BOOLEAN Bc250MockResourceExecutionAllowed = TRUE;

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
    CHECK(Bc250DmaResourceRetire(&resourceFixture) == STATUS_DEVICE_BUSY);
    CHECK(Bc250DmaResourceAcquire(&resourceFixture, &mdl, 0, 4096, TRUE) == STATUS_CANCELLED);
    CHECK(Bc250DmaGateStop(eventGate) == STATUS_SUCCESS);
    CHECK(Bc250DmaGateEnter(eventGate, &rejected) == STATUS_CANCELLED);
    if (rundown->Count) {
        CHECK(completeTicket && completeThread != coordinator);
        threadIndex = completeThread;
        if (completeAsFault) {
            owner.State = BC250_WIN_DMA_QUARANTINED;
            CHECK(Bc250DmaResourceFinish(&resourceFixture, completeTicket, STATUS_INVALID_DEVICE_STATE) == STATUS_INVALID_DEVICE_STATE);
        } else {
            CHECK(Bc250DmaResourceFinish(&resourceFixture, completeTicket, STATUS_SUCCESS) == STATUS_SUCCESS);
        }
        threadIndex = coordinator; completeTicket = NULL;
    }
    CHECK(!rundown->Count && !eventGate->Mutex.Held);
    /* Deterministic scheduler event, NOT a real concurrent thread or wait. */
}

static unsigned int stopAtProvider, providerPhases;
static void ResourceEvent(unsigned int phase)
{
    unsigned int oldGets = gets, oldFrees = frees, oldPuts = adapterPuts;
    LONG retirement = resourceFixture.Retirement;
    NTSTATUS admission = retirement == BC250_DMA_RESOURCE_RETIRING ? STATUS_CANCELLED : STATUS_DEVICE_BUSY;
    providerPhases |= 1U << phase;
    if (retirement == BC250_DMA_RESOURCE_RETIRING) {
        CHECK(resourceFixture.Gate.State == BC250_DMA_GATE_CLOSED);
        CHECK(!resourceFixture.Gate.Rundown.Count && !resourceFixture.Gate.Mutex.Held);
    } else {
        CHECK(resourceFixture.Gate.Rundown.Count == 1 && resourceFixture.Gate.Mutex.Held && critical[threadIndex]);
    }
    CHECK(Bc250DmaResourceAcquire(&resourceFixture, &mdl, 0, 4096, TRUE) == admission);
    CHECK(Bc250DmaResourceRelease(&resourceFixture) == admission);
    CHECK(Bc250DmaResourceRetire(&resourceFixture) == STATUS_DEVICE_BUSY);
    CHECK(Bc250DmaResourceInit(&resourceFixture, &pdo, &description) == STATUS_INVALID_DEVICE_STATE);
    if (stopAtProvider == phase) {
        NTSTATUS expected = retirement == BC250_DMA_RESOURCE_RETIRING ? STATUS_INVALID_DEVICE_STATE : STATUS_SUCCESS;
        CHECK(Bc250DmaResourceStop(&resourceFixture) == expected);
    }
    CHECK(gets == oldGets && frees == oldFrees && adapterPuts == oldPuts);
}
static void ResetResource(void)
{
    /* New isolated fake world; never reuse/reset a real live or faulted bundle. */
    memset(&resourceFixture, 0, sizeof(resourceFixture)); reset();
    memset(critical, 0, sizeof(critical));
    threadIndex = gateCalls = rundownGets = rundownPuts = waits = mutexPuts = 0;
    denyRundown = stopAtAcquire = stopAtMutex = completeAsFault = 0;
    stopAtProvider = providerPhases = 0;
    eventGate = &resourceFixture.Gate; completeTicket = NULL; fakeWaitStatus = STATUS_SUCCESS;
    Bc250MockGateExecutionAllowed = Bc250MockResourceExecutionAllowed = TRUE; reenter = 1;
}
static void OpenResource(void)
{
    CHECK(Bc250DmaResourceInit(&resourceFixture, &pdo, &description) == STATUS_SUCCESS);
    CHECK(owner.State == BC250_WIN_DMA_OPEN && resourceFixture.Signature == BC250_DMA_RESOURCE_SIGNATURE);
    CHECK(!resourceFixture.Gate.Rundown.Count && !critical[threadIndex]);
}
static void CheckRetired(void)
{
    CHECK(resourceFixture.Retirement == BC250_DMA_RESOURCE_RETIRED);
    CHECK(resourceFixture.Gate.State == BC250_DMA_GATE_CLOSED && !resourceFixture.Gate.Rundown.Count);
    CHECK(!resourceFixture.Gate.Mutex.Held && !live && rundownGets == rundownPuts);
    CHECK(!critical[0] && !critical[1] && !critical[2]);
    CHECK(Bc250DmaResourceRetire(&resourceFixture) == STATUS_INVALID_DEVICE_STATE);
    CHECK(Bc250DmaResourceAcquire(&resourceFixture, &mdl, 0, 4096, TRUE) == STATUS_CANCELLED);
}
static void TestNormalAndStop(void)
{
    unsigned int phase;
    ResetResource(); OpenResource();
    CHECK(Bc250DmaResourceAcquire(&resourceFixture, &mdl, 0, 4096, TRUE) == STATUS_SUCCESS);
    CHECK(live && !frees && !adapterPuts && !resourceFixture.Gate.Rundown.Count);
    CHECK(Bc250DmaResourceRelease(&resourceFixture) == STATUS_SUCCESS && frees == 1 && !live);
    CHECK(Bc250DmaResourceRelease(&resourceFixture) == STATUS_INVALID_DEVICE_STATE && frees == 1);
    CHECK(Bc250DmaResourceAcquire(&resourceFixture, &mdl, 0, 4096, TRUE) == STATUS_INVALID_DEVICE_STATE && gets == 1);
    CHECK(Bc250DmaResourceRetire(&resourceFixture) == STATUS_SUCCESS);
    CHECK(frees == 1 && adapterPuts == 1); CheckRetired();
    for (phase = 1; phase <= 3; ++phase) {
        ResetResource();
        if (phase == 1) stopAtProvider = phase;
        OpenResource();
        if (phase > 1) {
            stopAtProvider = phase;
            CHECK(Bc250DmaResourceAcquire(&resourceFixture, &mdl, 0, 4096, TRUE) == STATUS_SUCCESS);
            CHECK(live && !frees); /* Already admitted synchronous call may finish. */
        }
        CHECK(Bc250DmaResourceAcquire(&resourceFixture, &mdl, 0, 4096, TRUE) == STATUS_CANCELLED);
        CHECK(Bc250DmaResourceRelease(&resourceFixture) == STATUS_CANCELLED);
        CHECK(resourceFixture.Gate.State == BC250_DMA_GATE_STOPPING && !resourceFixture.Gate.Rundown.Count);
        CHECK(Bc250DmaResourceRetire(&resourceFixture) == STATUS_SUCCESS);
        CHECK(frees == (phase == 1 ? 0U : 1U) && adapterPuts == 1);
        CHECK(providerPhases & (1U << phase)); CheckRetired();
    }
    for (phase = 4; phase <= 5; ++phase) {
        ResetResource(); OpenResource();
        CHECK(Bc250DmaResourceAcquire(&resourceFixture, &mdl, 0, 4096, TRUE) == STATUS_SUCCESS);
        stopAtProvider = phase;
        CHECK(Bc250DmaResourceRetire(&resourceFixture) == STATUS_SUCCESS);
        CHECK(frees == 1 && adapterPuts == 1 && (providerPhases & (1U << phase))); CheckRetired();
    }
}
static void TestRetireWaitAndLateFault(void)
{
    BC250_DMA_GATE_TICKET ticket;
    unsigned int fault;
    for (fault = 0; fault <= 1; ++fault) {
        ResetResource(); OpenResource();
        CHECK(Bc250DmaResourceAcquire(&resourceFixture, &mdl, 0, 4096, TRUE) == STATUS_SUCCESS);
        memset(&ticket, 0, sizeof(ticket));
        /* Instrumented split of a protected operation: no real OS call pending. */
        CHECK(Bc250DmaResourceAdmit(&resourceFixture, &ticket) == STATUS_SUCCESS);
        completeTicket = &ticket; completeThread = 0; completeAsFault = fault; threadIndex = 1;
        CHECK(live && !frees && resourceFixture.Gate.Rundown.Count == 1);
        if (!fault) {
            CHECK(Bc250DmaResourceRetire(&resourceFixture) == STATUS_SUCCESS);
            CHECK(frees == 1 && adapterPuts == 1); CheckRetired();
        } else {
            CHECK(Bc250DmaResourceRetire(&resourceFixture) == STATUS_INVALID_DEVICE_STATE);
            CHECK(resourceFixture.Retirement == BC250_DMA_RESOURCE_FAULT && live && !frees && !adapterPuts);
            CHECK(resourceFixture.Gate.State == BC250_DMA_GATE_CLOSED && !resourceFixture.Gate.Rundown.Count);
            CHECK(Bc250DmaResourceRetire(&resourceFixture) == STATUS_INVALID_DEVICE_STATE);
            eventGate = NULL; /* Discard fake quarantined world; NOT Windows cleanup. */
        }
    }
}
static void TestKnownFailures(void)
{
    unsigned int scenario;
    for (scenario = 0; scenario < 5; ++scenario) {
        ResetResource();
        if (scenario == 0) noAdapter = 1;
        if (scenario == 1) operations.GetScatterGatherListEx = NULL;
        if (scenario == 2) description.Reserved1 = 1;
        if (scenario <= 2) {
            CHECK(Bc250DmaResourceInit(&resourceFixture, &pdo, &description) != STATUS_SUCCESS);
            CHECK(!owner.State && !owner.Adapter && !live);
        } else {
            OpenResource();
            if (scenario == 3) getStatus = STATUS_INSUFFICIENT_RESOURCES;
            if (scenario == 4) list.Elements[0].Length = 4097;
            CHECK(Bc250DmaResourceAcquire(&resourceFixture, &mdl, 0, 4096, TRUE) != STATUS_SUCCESS);
            CHECK(owner.State == BC250_WIN_DMA_OPEN && !live && !owner.List);
        }
        CHECK(resourceFixture.Retirement == BC250_DMA_RESOURCE_ACTIVE);
        CHECK(Bc250DmaResourceRetire(&resourceFixture) == STATUS_SUCCESS); CheckRetired();
        CHECK(adapterPuts == (scenario == 0 || scenario == 2 ? 0U : 1U));
    }
}
static void TestAmbiguity(void)
{
    unsigned int scenario;
    for (scenario = 0; scenario < 4; ++scenario) {
        ResetResource();
        if (scenario == 0) operations.PutDmaAdapter = NULL;
        if (scenario == 3) fakeWaitStatus = STATUS_PENDING;
        if (scenario == 0 || scenario == 3) {
            CHECK(Bc250DmaResourceInit(&resourceFixture, &pdo, &description) != STATUS_SUCCESS);
        } else {
            OpenResource();
            if (scenario == 1) getStatus = STATUS_PENDING;
            if (scenario == 2) { getStatus = STATUS_INSUFFICIENT_RESOURCES; failureWithList = 1; }
            CHECK(Bc250DmaResourceAcquire(&resourceFixture, &mdl, 0, 4096, TRUE) == STATUS_INVALID_DEVICE_STATE);
            CHECK(owner.HeldMdl == &mdl);
        }
        CHECK(resourceFixture.Retirement == BC250_DMA_RESOURCE_FAULT && !frees && !adapterPuts);
        CHECK(Bc250DmaResourceRetire(&resourceFixture) == STATUS_INVALID_DEVICE_STATE);
        CHECK(Bc250DmaResourceAcquire(&resourceFixture, &mdl, 0, 4096, TRUE) == STATUS_CANCELLED);
        if (scenario == 3) CHECK(resourceFixture.Gate.Rundown.Count == 1 && !critical[0]);
        eventGate = NULL; /* Fake teardown only; no recovery is implemented. */
    }
}
static void TestGuardsAndBoundaries(void)
{
    BC250_DMA_RESOURCE snapshot;
    unsigned int oldCalls, oldGateCalls, scenario;
    ResetResource(); snapshot = resourceFixture; oldCalls = calls; oldGateCalls = gateCalls;
    Bc250MockResourceExecutionAllowed = FALSE;
    CHECK(Bc250DmaResourceInit(NULL, NULL, NULL) == STATUS_NOT_SUPPORTED);
    CHECK(Bc250DmaResourceAcquire(NULL, NULL, 0, 0, FALSE) == STATUS_NOT_SUPPORTED);
    CHECK(Bc250DmaResourceRelease(NULL) == STATUS_NOT_SUPPORTED);
    CHECK(Bc250DmaResourceStop(NULL) == STATUS_NOT_SUPPORTED);
    CHECK(Bc250DmaResourceRetire(NULL) == STATUS_NOT_SUPPORTED);
    CHECK(oldCalls == calls && oldGateCalls == gateCalls && !memcmp(&snapshot, &resourceFixture, sizeof(snapshot)));
    Bc250MockResourceExecutionAllowed = TRUE;
    CHECK(Bc250DmaResourceInit(&resourceFixture, (PDEVICE_OBJECT)&resourceFixture.Native, &description) == STATUS_INVALID_PARAMETER);
    CHECK(Bc250DmaResourceInit(&resourceFixture, &pdo, (DEVICE_DESCRIPTION *)&resourceFixture.Gate) == STATUS_INVALID_PARAMETER);
    CHECK(!memcmp(&snapshot, &resourceFixture, sizeof(snapshot)));
    OpenResource(); snapshot = resourceFixture; irql = 1; oldGateCalls = gateCalls;
    CHECK(Bc250DmaResourceAcquire(&resourceFixture, &mdl, 0, 4096, TRUE) == STATUS_INVALID_DEVICE_STATE);
    CHECK(Bc250DmaResourceRetire(&resourceFixture) == STATUS_INVALID_DEVICE_STATE);
    CHECK(gateCalls == oldGateCalls && !memcmp(&snapshot, &resourceFixture, sizeof(snapshot))); irql = 0;
    CHECK(Bc250DmaResourceAcquire(&resourceFixture, (PMDL)&resourceFixture.Retirement, 0, 4096, TRUE) == STATUS_INVALID_PARAMETER);
    CHECK(!memcmp(&snapshot, &resourceFixture, sizeof(snapshot)));
    CHECK(Bc250DmaResourceRetire(&resourceFixture) == STATUS_SUCCESS); CheckRetired();
    for (scenario = 1; scenario <= 64; ++scenario) {
        unsigned int i;
        ResetResource(); OpenResource(); list.NumberOfElements = scenario;
        for (i = 0; i < scenario; ++i) { list.Elements[i].Address.QuadPart = 0; list.Elements[i].Length = 4096; }
        CHECK(Bc250DmaResourceAcquire(&resourceFixture, &mdl, 0, scenario * 4096, FALSE) == STATUS_SUCCESS);
        CHECK(!resourceFixture.Gate.Rundown.Count && live && gets == 1);
        CHECK(Bc250DmaResourceRetire(&resourceFixture) == STATUS_SUCCESS);
        CHECK(frees == 1 && adapterPuts == 1); CheckRetired();
    }
}
static void TestCleanupFailure(void)
{
    ResetResource(); OpenResource();
    CHECK(Bc250DmaResourceAcquire(&resourceFixture, &mdl, 0, 4096, TRUE) == STATUS_SUCCESS);
    Bc250MockExecutionAllowed = FALSE; /* Inject native policy failure ONLY in RAM. */
    CHECK(Bc250DmaResourceRetire(&resourceFixture) == STATUS_NOT_SUPPORTED);
    CHECK(resourceFixture.Gate.State == BC250_DMA_GATE_CLOSED && !resourceFixture.Gate.Rundown.Count);
    CHECK(resourceFixture.Retirement == BC250_DMA_RESOURCE_FAULT && live && !frees && !adapterPuts);
    CHECK(owner.List == &list && owner.HeldMdl == &mdl);
    CHECK(Bc250DmaResourceRetire(&resourceFixture) == STATUS_INVALID_DEVICE_STATE);
    eventGate = NULL; /* Discard fake world, never infer cleanup from closed gate. */
}
int main(void)
{
    TestNormalAndStop(); TestRetireWaitAndLateFault(); TestKnownFailures();
    TestAmbiguity(); TestGuardsAndBoundaries(); TestCleanupFailure();
    printf("PASS: %u composition checks; actual frozen gate/native plus resource source, fake DDIs only; NO Windows DMA/PnP/concurrency\n", checks);
    return 0;
}
