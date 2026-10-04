/* Test-only RAM backend. Never linked into the driver. No Windows/device API. */
#include <stdio.h>
#include <string.h>
#include "gc-selectors-20260930/bc250_gc_selectors.h"

typedef struct FakeScope FakeScope;
typedef struct FakeBackend {
    unsigned int Index, Cntl, Reads, Writes;
    unsigned int FailRead, FailWriteBefore, FailWriteAfter;
    FakeScope *Owner;
} FakeBackend;
struct FakeScope {
    FakeBackend *Backend;
    unsigned int Offset, Saved, Active;
};
static unsigned int checks;
static int checked(int ok, const char *label, int line)
{
    ++checks;
    if (!ok) fprintf(stderr, "FAIL line %d: %s\n", line, label);
    return ok;
}
#define REQUIRE(expr) do { if (!checked(!!(expr), #expr, __LINE__)) return 1; } while (0)

static unsigned int *fake_cell(FakeBackend *backend, unsigned int offset)
{
    if (offset == 0x30800U) return &backend->Index;
    if (offset == 0x8088U) return &backend->Cntl;
    return NULL;
}
static int fake_read(FakeBackend *backend, unsigned int offset, unsigned int *out)
{
    unsigned int *cell = fake_cell(backend, offset);
    ++backend->Reads;
    if (!cell || backend->FailRead) { backend->FailRead = 0; return 0; }
    *out = *cell;
    return 1;
}
static int fake_write(FakeBackend *backend, unsigned int offset, unsigned int value)
{
    unsigned int *cell = fake_cell(backend, offset);
    ++backend->Writes;
    if (!cell || backend->FailWriteBefore) { backend->FailWriteBefore = 0; return 0; }
    *cell = value;
    if (backend->FailWriteAfter) { backend->FailWriteAfter = 0; return 0; }
    return 1;
}
static int fake_begin(FakeBackend *backend, const BC250_GC_SELECTOR *plan, FakeScope *scope)
{
    unsigned int saved;
    if (!backend || !scope || scope->Active || backend->Owner || !Bc250GcSelectorPlanValid(plan))
        return 0;
    if (!fake_read(backend, plan->ByteOffset, &saved)) return 0;
    scope->Backend = backend;
    scope->Offset = plan->ByteOffset;
    scope->Saved = saved;
    /* Once a write is attempted, an error can mean committed or not committed.
     * Keep the saved state and ownership until an explicit restore succeeds. */
    scope->Active = 1;
    backend->Owner = scope;
    return fake_write(backend, scope->Offset, plan->Value);
}
static int fake_end(FakeBackend *backend, FakeScope *scope)
{
    if (!backend || !scope || !scope->Active || scope->Backend != backend || backend->Owner != scope)
        return 0;
    if (!fake_write(backend, scope->Offset, scope->Saved)) return 0;
    scope->Active = 0;
    scope->Backend = NULL;
    backend->Owner = NULL;
    return 1;
}

int main(void)
{
    BC250_GC_SELECTOR plan, sePlan, queuePlan, bad;
    FakeBackend backend = {0}, other = {0};
    FakeScope scope = {0}, nested = {0};
    unsigned int me, pipe, queue, vmid, se, sa, broadcast, failures;
    unsigned int savedReads, savedWrites;

    REQUIRE(BC250_GC_SOURCE_MAP_RUNTIME_VALIDATED == 0U);
    REQUIRE(Bc250GcBuildQueueSelector(1, 0, 0, 0, &queuePlan));
    REQUIRE(queuePlan.Kind == BC250_GC_SELECTOR_QUEUE && queuePlan.ByteOffset == 0x8088U && queuePlan.Value == 4U);
    REQUIRE(Bc250GcBuildSeShSelector(1, 1, 0, 0, &sePlan));
    REQUIRE(sePlan.Kind == BC250_GC_SELECTOR_SE_SH && sePlan.ByteOffset == 0x30800U && sePlan.Value == 0x10100U);

    /* Exhaust all encodable queue field combinations. These are FIELD ranges,
     * not proof that 2048 queues/contexts exist on this board. */
    for (me = 0; me < 4; ++me) for (pipe = 0; pipe < 4; ++pipe)
    for (queue = 0; queue < 8; ++queue) for (vmid = 0; vmid < 16; ++vmid) {
        REQUIRE(Bc250GcBuildQueueSelector(me, pipe, queue, vmid, &plan));
        REQUIRE(plan.Value == (me * 4U | pipe | queue * 256U | vmid * 16U));
        REQUIRE(Bc250GcSelectorPlanValid(&plan));
    }
    for (se = 0; se < 2; ++se) for (sa = 0; sa < 2; ++sa) {
        REQUIRE(Bc250GcBuildSeShSelector(se, sa, 0, 0, &plan));
        REQUIRE(plan.Value == (se * 65536U | sa * 256U));
        REQUIRE(Bc250GcSelectorPlanValid(&plan));
    }
    for (broadcast = 0; broadcast < 8; ++broadcast) {
        REQUIRE(Bc250GcBuildSeShSelector(0, 0, 0, broadcast, &plan));
        REQUIRE(plan.Value == ((broadcast & 1U ? 0x40000000U : 0U) |
                              (broadcast & 2U ? 0x20000000U : 0U) |
                              (broadcast & 4U ? 0x80000000U : 0U)));
        REQUIRE(Bc250GcSelectorPlanValid(&plan));
    }
    REQUIRE(Bc250GcBuildSeShSelector(255, 255, 255, 0, &plan));
    REQUIRE(plan.Value == 0xFFFFFFU);
    bad = queuePlan;
    REQUIRE(!Bc250GcBuildQueueSelector(4, 0, 0, 0, &bad));
    REQUIRE(!Bc250GcBuildQueueSelector(0, 4, 0, 0, &bad));
    REQUIRE(!Bc250GcBuildQueueSelector(0, 0, 8, 0, &bad));
    REQUIRE(!Bc250GcBuildQueueSelector(0, 0, 0, 16, &bad));
    REQUIRE(!Bc250GcBuildQueueSelector((unsigned int)-1, 0, 0, 0, &bad));
    REQUIRE(bad.Kind == queuePlan.Kind && bad.ByteOffset == queuePlan.ByteOffset && bad.Value == queuePlan.Value);
    REQUIRE(!Bc250GcBuildQueueSelector(0, 0, 0, 0, NULL));
    REQUIRE(!Bc250GcBuildSeShSelector(256, 0, 0, 0, &bad));
    REQUIRE(!Bc250GcBuildSeShSelector(0, 256, 0, 0, &bad));
    REQUIRE(!Bc250GcBuildSeShSelector(0, 0, 256, 0, &bad));
    REQUIRE(!Bc250GcBuildSeShSelector(0, 0, 0, 8, &bad));
    REQUIRE(!Bc250GcBuildSeShSelector(1, 0, 0, BC250_GC_BROADCAST_SE, &bad));
    REQUIRE(!Bc250GcBuildSeShSelector(0, 1, 0, BC250_GC_BROADCAST_SA, &bad));
    REQUIRE(!Bc250GcBuildSeShSelector(0, 0, 1, BC250_GC_BROADCAST_INSTANCE, &bad));
    REQUIRE(!Bc250GcBuildSeShSelector(0, 0, 0, 0, NULL));

    backend.Index = 0xABC01234U; backend.Cntl = 0x13572468U;
    REQUIRE(fake_begin(&backend, &queuePlan, &scope));
    REQUIRE(backend.Cntl == 4U && backend.Index == 0xABC01234U);
    savedReads = backend.Reads; savedWrites = backend.Writes;
    REQUIRE(!fake_begin(&backend, &sePlan, &nested));
    REQUIRE(backend.Reads == savedReads && backend.Writes == savedWrites);
    REQUIRE(!fake_end(&other, &scope));
    REQUIRE(fake_end(&backend, &scope));
    REQUIRE(backend.Cntl == 0x13572468U && backend.Index == 0xABC01234U);
    savedWrites = backend.Writes;
    REQUIRE(!fake_end(&backend, &scope) && backend.Writes == savedWrites);
    REQUIRE(fake_begin(&backend, &sePlan, &scope));
    REQUIRE(backend.Index == 0x10100U && backend.Cntl == 0x13572468U);
    REQUIRE(fake_end(&backend, &scope) && backend.Index == 0xABC01234U);

    /* Wrong kind/offset, reserved bits, and broadcast/index contradictions
     * must be rejected BEFORE touching the fake backend. */
    savedReads = backend.Reads; savedWrites = backend.Writes;
    bad = queuePlan; bad.ByteOffset = 0x30800U;
    REQUIRE(!fake_begin(&backend, &bad, &scope));
    bad = queuePlan; bad.Value |= 0xE0000000U;
    REQUIRE(!fake_begin(&backend, &bad, &scope));
    bad = sePlan; bad.Value |= 0x4000000U;
    REQUIRE(!fake_begin(&backend, &bad, &scope));
    bad = sePlan; bad.Value |= 0x80000000U;
    REQUIRE(!fake_begin(&backend, &bad, &scope));
    bad = sePlan; bad.Kind = (BC250_GC_SELECTOR_KIND)99;
    REQUIRE(!fake_begin(&backend, &bad, &scope));
    REQUIRE(!fake_begin(&backend, NULL, &scope));
    REQUIRE(backend.Reads == savedReads && backend.Writes == savedWrites);

    backend.FailRead = 1;
    REQUIRE(!fake_begin(&backend, &queuePlan, &scope));
    REQUIRE(!scope.Active && !backend.Owner && backend.Cntl == 0x13572468U);
    for (failures = 0; failures < 2; ++failures) {
        backend.FailWriteBefore = failures == 0;
        backend.FailWriteAfter = failures == 1;
        REQUIRE(!fake_begin(&backend, &queuePlan, &scope));
        REQUIRE(scope.Active && backend.Owner == &scope);
        REQUIRE(fake_end(&backend, &scope) && backend.Cntl == 0x13572468U);
    }
    REQUIRE(fake_begin(&backend, &queuePlan, &scope));
    backend.FailWriteBefore = 1;
    REQUIRE(!fake_end(&backend, &scope) && scope.Active && backend.Owner == &scope);
    REQUIRE(!fake_begin(&backend, &sePlan, &nested));
    REQUIRE(fake_end(&backend, &scope) && backend.Cntl == 0x13572468U);
    REQUIRE(fake_begin(&backend, &queuePlan, &scope));
    backend.FailWriteAfter = 1;
    REQUIRE(!fake_end(&backend, &scope) && backend.Cntl == 0x13572468U);
    REQUIRE(scope.Active && backend.Owner == &scope);
    REQUIRE(!fake_begin(&backend, &sePlan, &nested));
    REQUIRE(fake_end(&backend, &scope));
    REQUIRE(!backend.Owner && !scope.Active);
    printf("PASS: %u assertions; 2048 queue encodings; selection/restore/error model; NO GPU access.\n", checks);
    return 0;
}
