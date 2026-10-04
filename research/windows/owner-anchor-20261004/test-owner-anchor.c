/* SPDX-License-Identifier: Apache-2.0
 * Deterministic RAM worlds. Raw mutations are negative fixture instrumentation,
 * NOT a public recovery API or proof of Windows ownership/concurrency.
 */
#define BC250_OWNER_ANCHOR_RAM_ONLY
#include "bc250_owner_anchor.h"
#include <stdio.h>
#include <stdlib.h>
unsigned int Bc250OwnerAnchorRamAllowed = 1U;
static unsigned int checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "FAIL %u: %s\n", (unsigned int)__LINE__, #x); exit(1); } } while (0)
static void Start(BC250_OWNER_ANCHOR *r)
{
    memset(r, 0, sizeof(*r)); CHECK(Bc250OaInit(r) == BC250_OA_OK);
    CHECK(Bc250OaCheck(r) == BC250_OA_OK);
}
static void End(BC250_OWNER_ANCHOR *r)
{
    BC250_OA_TOKEN rejected = {0};
    CHECK(Bc250OaStop(r) == BC250_OA_OK);
    CHECK(Bc250OaRetire(r) == BC250_OA_OK);
    CHECK(r->State == BC250_OA_DEAD && Bc250OaCheck(r) == BC250_OA_OK);
    CHECK(Bc250OaBegin(r, &rejected) == BC250_OA_DEAD_RESULT && !rejected.Id);
    CHECK(Bc250OaRetire(r) == BC250_OA_DEAD_RESULT);
    CHECK(Bc250OaInit(r) == BC250_OA_INVALID);
}
static void TestAdmission(void)
{
    unsigned int phase, round;
    for (round = 0; round < 128; ++round) for (phase = 0; phase < 3; ++phase) {
        BC250_OWNER_ANCHOR r;
        BC250_OA_TOKEN first = {0}, late = {0}, request = {0};
        Start(&r);
        if (!phase) CHECK(Bc250OaStop(&r) == BC250_OA_OK);
        CHECK(Bc250OaBegin(&r, &first) == BC250_OA_OK);
        if (phase == 1) CHECK(Bc250OaStop(&r) == BC250_OA_OK);
        CHECK(Bc250OaAdmit(&r, &first) == (phase == 2 ? BC250_OA_OK : BC250_OA_CLOSED));
        if (phase == 2) CHECK(Bc250OaStop(&r) == BC250_OA_OK);
        CHECK(Bc250OaAdmit(&r, &first) == BC250_OA_INVALID);
        CHECK(Bc250OaRequest(&r, &first, BC250_OA_OWNED, &request) == BC250_OA_CLOSED && !request.Id);
        CHECK(!r.Completed && !r.OwnedFreed && !r.BorrowedReturned);
        CHECK(Bc250OaRetire(&r) == BC250_OA_IN_USE);
        CHECK(Bc250OaBegin(&r, &late) == BC250_OA_OK); /* Already stopped, still an access debt. */
        CHECK(Bc250OaAdmit(&r, &late) == BC250_OA_CLOSED);
        CHECK(Bc250OaLeave(&r, &first) == BC250_OA_OK);
        CHECK(Bc250OaLeave(&r, &first) == BC250_OA_STALE);
        CHECK(Bc250OaRetire(&r) == BC250_OA_IN_USE);
        CHECK(Bc250OaLeave(&r, &late) == BC250_OA_OK);
        End(&r);
    }
}
static void TestRequests(void)
{
    unsigned int ownership, cancelPhase, round;
    for (round = 0; round < 128; ++round) for (ownership = 1; ownership <= 2; ++ownership)
    for (cancelPhase = 0; cancelPhase < 3; ++cancelPhase) {
        BC250_OWNER_ANCHOR r;
        BC250_OA_TOKEN call = {0}, request = {0}, callback = {0};
        Start(&r); CHECK(Bc250OaBegin(&r, &call) == BC250_OA_OK);
        CHECK(Bc250OaAdmit(&r, &call) == BC250_OA_OK);
        CHECK(Bc250OaRequest(&r, &call, ownership, &request) == BC250_OA_OK);
        CHECK(Bc250OaLeave(&r, &call) == BC250_OA_OK);
        if (!cancelPhase) {
            CHECK(Bc250OaCancel(&r, &request) == BC250_OA_OK);
            CHECK(Bc250OaWorker(&r, &request, 1) == BC250_OA_CLOSED);
        } else {
            CHECK(Bc250OaWorker(&r, &request, 1) == BC250_OA_OK);
            CHECK(Bc250OaFinish(&r, &request) == BC250_OA_IN_USE);
            if (cancelPhase == 2) CHECK(Bc250OaDeviceDebt(&r, &request, 1) == BC250_OA_OK);
            CHECK(Bc250OaCancel(&r, &request) == BC250_OA_OK);
            CHECK(Bc250OaDeviceDebt(&r, &request, 1) ==
                (cancelPhase == 2 ? BC250_OA_INVALID : BC250_OA_CLOSED));
            CHECK(Bc250OaStop(&r) == BC250_OA_OK);
            CHECK(Bc250OaWorker(&r, &request, 0) == BC250_OA_OK);
            if (cancelPhase == 2) {
                CHECK(Bc250OaFinish(&r, &request) == BC250_OA_IN_USE);
                CHECK(Bc250OaDeviceDebt(&r, &request, 0) == BC250_OA_OK);
            }
        }
        CHECK(Bc250OaCancel(&r, &request) == BC250_OA_OK); /* Intent is idempotent. */
        CHECK(Bc250OaCallback(&r, &request, &callback) == BC250_OA_OK);
        CHECK(Bc250OaStop(&r) == BC250_OA_OK);
        CHECK(Bc250OaRetire(&r) == BC250_OA_IN_USE);
        CHECK(Bc250OaFinish(&r, &request) == BC250_OA_IN_USE); /* Worker0/debt0 but callback still queued. */
        CHECK(!r.Completed && !r.OwnedFreed && !r.BorrowedReturned);
        CHECK(Bc250OaCallbackLeave(&r, &callback) == BC250_OA_OK);
        CHECK(Bc250OaCallbackLeave(&r, &callback) == BC250_OA_STALE);
        CHECK(Bc250OaFinish(&r, &request) == BC250_OA_OK);
        CHECK(r.Completed == 1 && r.OwnedFreed == (ownership == 1 ? 1ULL : 0ULL));
        CHECK(r.BorrowedReturned == (ownership == 2 ? 1ULL : 0ULL));
        CHECK(Bc250OaFinish(&r, &request) == BC250_OA_STALE);
        CHECK(Bc250OaCancel(&r, &request) == BC250_OA_STALE);
        End(&r);
    }
}
static void TestBoundsAndIdentity(void)
{
    BC250_OWNER_ANCHOR r, copied;
    BC250_OA_TOKEN calls[BC250_OA_CALLS] = {0}, requests[BC250_OA_REQUESTS] = {0};
    BC250_OA_TOKEN callbacks[BC250_OA_CALLBACKS] = {0}, out = {0}, copy;
    unsigned int i;
    Start(&r); copied = r; CHECK(Bc250OaStop(&copied) == BC250_OA_INVALID);
    CHECK(Bc250OaBegin(&r, (BC250_OA_TOKEN *)&r) == BC250_OA_INVALID);
    for (i = 0; i < BC250_OA_CALLS; ++i) CHECK(Bc250OaBegin(&r, &calls[i]) == BC250_OA_OK);
    CHECK(Bc250OaBegin(&r, &out) == BC250_OA_FULL && !out.Id);
    copy = calls[0]; CHECK(Bc250OaLeave(&r, &copy) == BC250_OA_INVALID);
    CHECK(Bc250OaRequest(&r, &calls[0], 1, &out) == BC250_OA_CLOSED);
    CHECK(Bc250OaAdmit(&r, &calls[0]) == BC250_OA_OK);
    CHECK(Bc250OaRequest(&r, &calls[0], 0, &out) == BC250_OA_INVALID);
    for (i = 0; i < BC250_OA_REQUESTS; ++i)
        CHECK(Bc250OaRequest(&r, &calls[0], i & 1U ? BC250_OA_OWNED : BC250_OA_BORROWED, &requests[i]) == BC250_OA_OK);
    CHECK(Bc250OaRequest(&r, &calls[0], 1, &out) == BC250_OA_FULL && !out.Id);
    CHECK(Bc250OaCallback(&r, &requests[0], &requests[1]) == BC250_OA_INVALID);
    for (i = 0; i < BC250_OA_CALLBACKS; ++i)
        CHECK(Bc250OaCallback(&r, &requests[0], &callbacks[i]) == BC250_OA_OK);
    CHECK(Bc250OaCallback(&r, &requests[0], &out) == BC250_OA_FULL && !out.Id);
    CHECK(Bc250OaCancel(&r, &calls[0]) == BC250_OA_INVALID);
    copy = requests[0]; copy.Self = &copy;
    CHECK(Bc250OaFinish(&r, &copy) == BC250_OA_STALE); /* Forged Self still not canonical. */
    CHECK(Bc250OaStop(&r) == BC250_OA_OK);
    for (i = 0; i < BC250_OA_CALLS; ++i) CHECK(Bc250OaLeave(&r, &calls[i]) == BC250_OA_OK);
    for (i = 0; i < BC250_OA_REQUESTS; ++i)
        CHECK(Bc250OaFinish(&r, &requests[i]) == (i ? BC250_OA_OK : BC250_OA_IN_USE));
    CHECK(Bc250OaRetire(&r) == BC250_OA_IN_USE);
    for (i = 0; i < BC250_OA_CALLBACKS; ++i) CHECK(Bc250OaCallbackLeave(&r, &callbacks[i]) == BC250_OA_OK);
    CHECK(Bc250OaFinish(&r, &requests[0]) == BC250_OA_OK);
    CHECK(r.OwnedFreed == 4 && r.BorrowedReturned == 4 && r.Completed == 8);
    End(&r);
}
static void TestExhaustionFaultAndPolicy(void)
{
    BC250_OWNER_ANCHOR r;
    BC250_OA_TOKEN call = {0}, request = {0}, out = {0};
    unsigned int i;
    for (i = 0; i < 3; ++i) {
        Start(&r);
        if (!i) {
            r.LastCall = UINT64_MAX; /* isolated exhaustion world */
            CHECK(Bc250OaBegin(&r, &out) == BC250_OA_EXHAUSTED);
        } else {
            CHECK(Bc250OaBegin(&r, &call) == BC250_OA_OK);
            CHECK(Bc250OaAdmit(&r, &call) == BC250_OA_OK);
            if (i == 1) {
                r.LastRequest = r.Completed = r.OwnedFreed = UINT64_MAX;
                CHECK(Bc250OaRequest(&r, &call, 1, &out) == BC250_OA_EXHAUSTED);
            } else {
                CHECK(Bc250OaRequest(&r, &call, 2, &request) == BC250_OA_OK);
                r.LastCallback = UINT64_MAX;
                CHECK(Bc250OaCallback(&r, &request, &out) == BC250_OA_EXHAUSTED);
                CHECK(Bc250OaFinish(&r, &request) == BC250_OA_OK);
            }
            CHECK(Bc250OaLeave(&r, &call) == BC250_OA_OK);
        }
        CHECK(!out.Id); End(&r);
        memset(&call, 0, sizeof(call)); memset(&request, 0, sizeof(request));
    }
    Start(&r); CHECK(Bc250OaBegin(&r, &call) == BC250_OA_OK);
    CHECK(Bc250OaAdmit(&r, &call) == BC250_OA_OK);
    CHECK(Bc250OaRequest(&r, &call, 2, &request) == BC250_OA_OK);
    r.Fault = 1; /* Unresolved world; no cleanup/recovery promised. */
    CHECK(Bc250OaCancel(&r, &request) == BC250_OA_FAULT);
    CHECK(Bc250OaFinish(&r, &request) == BC250_OA_FAULT);
    CHECK(Bc250OaLeave(&r, &call) == BC250_OA_FAULT);
    CHECK(Bc250OaRetire(&r) == BC250_OA_FAULT && !r.Completed);
    Bc250OwnerAnchorRamAllowed = 0;
    CHECK(Bc250OaInit(NULL) == BC250_OA_DISABLED);
    CHECK(Bc250OaBegin(NULL, NULL) == BC250_OA_DISABLED);
    CHECK(Bc250OaCancel(NULL, NULL) == BC250_OA_DISABLED);
    CHECK(Bc250OaRetire(NULL) == BC250_OA_DISABLED);
    Bc250OwnerAnchorRamAllowed = 1;
    /* Discard fake universe, NOT recovery/cleanup of real driver resources. */
}
int main(void)
{
    TestAdmission(); TestRequests(); TestBoundsAndIdentity(); TestExhaustionFaultAndPolicy();
    printf("PASS: %u owner-anchor RAM checks; calls/requests/workers/callbacks; NO IRP/PDO/MDL/PnP/DMA proof\n", checks);
    return 0;
}
