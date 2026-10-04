/* SPDX-License-Identifier: Apache-2.0
 * Logical RAM-only router/request/worker/callback debt model. NO OS objects.
 * Router and EVERY buffer externally valid/stable/anchored before EVERY call,
 * through ALL returns and debts, including rejected entrants and coordinators.
 * Trusted DISJOINT buffers; whole-call exclusive serial events, NOT SMP/IRQL/
 * PnP/IRP or ownership proof. Field checks cannot probe arbitrary pointers.
 * DEAD is logical only: router storage NEVER freed here. No reset/reuse/copies.
 * Owned/borrowed are fixture declarations, not Windows ownership capabilities.
 */
#ifndef BC250_OWNER_ANCHOR_RAM_H
#define BC250_OWNER_ANCHOR_RAM_H
#ifndef BC250_OWNER_ANCHOR_RAM_ONLY
#error Owner anchor model requires explicit RAM-only compilation
#endif
#ifdef _KERNEL_MODE
#error Owner anchor model is NOT kernel integrated
#endif
#include <stddef.h>
#include <stdint.h>
#include <string.h>
extern unsigned int Bc250OwnerAnchorRamAllowed;
#define BC250_OA_CALLS 16U
#define BC250_OA_REQUESTS 8U
#define BC250_OA_CALLBACKS 8U
#define BC250_OA_ACTIVE 1U
#define BC250_OA_STOPPED 2U
#define BC250_OA_DEAD 3U
#define BC250_OA_OWNED 1U
#define BC250_OA_BORROWED 2U
#define BC250_OA_CALL 1U
#define BC250_OA_REQUEST 2U
#define BC250_OA_CALLBACK 3U
typedef enum BC250_OA_STATUS {
    BC250_OA_OK, BC250_OA_INVALID, BC250_OA_IN_USE, BC250_OA_CLOSED,
    BC250_OA_STALE, BC250_OA_FULL, BC250_OA_FAULT, BC250_OA_DISABLED,
    BC250_OA_EXHAUSTED, BC250_OA_DEAD_RESULT, BC250_OA_CORRUPT
} BC250_OA_STATUS;
typedef struct BC250_OWNER_ANCHOR BC250_OWNER_ANCHOR;
typedef struct BC250_OA_TOKEN {
    const struct BC250_OA_TOKEN *Self;
    const BC250_OWNER_ANCHOR *Router;
    uint64_t Id;
    unsigned int Slot, Class;
} BC250_OA_TOKEN;
typedef struct BC250_OA_CALL_RECORD {
    const BC250_OA_TOKEN *Canonical;
    uint64_t Id;
    unsigned int Decided, Admitted;
} BC250_OA_CALL_RECORD;
typedef struct BC250_OA_REQUEST_RECORD {
    const BC250_OA_TOKEN *Canonical;
    uint64_t Id;
    unsigned int Ownership, Cancel, Worker, DeviceDebt;
} BC250_OA_REQUEST_RECORD;
typedef struct BC250_OA_CALLBACK_RECORD {
    const BC250_OA_TOKEN *Canonical;
    uint64_t Id, RequestId;
} BC250_OA_CALLBACK_RECORD;
struct BC250_OWNER_ANCHOR {
    const BC250_OWNER_ANCHOR *Self;
    unsigned int State, Fault;
    uint64_t LastCall, LastRequest, LastCallback;
    uint64_t Completed, OwnedFreed, BorrowedReturned;
    BC250_OA_CALL_RECORD Calls[BC250_OA_CALLS];
    BC250_OA_REQUEST_RECORD Requests[BC250_OA_REQUESTS];
    BC250_OA_CALLBACK_RECORD Callbacks[BC250_OA_CALLBACKS];
};
static __inline int Bc250OaDisjoint(const void *p, size_t pn, const void *q, size_t qn)
{
    uintptr_t a = (uintptr_t)p, b = (uintptr_t)q, top = ~(uintptr_t)0;
    if (!p || !q || !pn || !qn || a > top - (pn - 1U) || b > top - (qn - 1U)) return 0;
    return a + pn - 1U < b || b + qn - 1U < a;
}
static __inline int Bc250OaAway(const BC250_OWNER_ANCHOR *r, const void *p, size_t bytes)
{
    return Bc250OaDisjoint(r, sizeof(*r), p, bytes);
}
static __inline BC250_OA_STATUS Bc250OaCheck(BC250_OWNER_ANCHOR *r)
{
    unsigned int i, j, pending = 0;
    if (Bc250OwnerAnchorRamAllowed != 1U) return BC250_OA_DISABLED;
    if (!r || r->Self != r) return BC250_OA_INVALID;
    if (r->Fault) return BC250_OA_FAULT;
    if (r->State != BC250_OA_ACTIVE && r->State != BC250_OA_STOPPED && r->State != BC250_OA_DEAD)
        return BC250_OA_CORRUPT;
    for (i = 0; i < BC250_OA_CALLS; ++i) {
        const BC250_OA_CALL_RECORD *c = &r->Calls[i];
        if (!c->Id) {
            if (c->Canonical || c->Decided || c->Admitted) return BC250_OA_CORRUPT;
        } else {
            if (c->Id > r->LastCall || !Bc250OaAway(r, c->Canonical, sizeof(BC250_OA_TOKEN)) ||
                c->Decided > 1U || c->Admitted > 1U || (c->Admitted && !c->Decided)) return BC250_OA_CORRUPT;
            for (j = 0; j < i; ++j)
                if (r->Calls[j].Id == c->Id || r->Calls[j].Canonical == c->Canonical) return BC250_OA_CORRUPT;
            if (r->State == BC250_OA_DEAD) return BC250_OA_CORRUPT;
        }
    }
    for (i = 0; i < BC250_OA_REQUESTS; ++i) {
        const BC250_OA_REQUEST_RECORD *q = &r->Requests[i];
        if (!q->Id) {
            if (q->Canonical || q->Ownership || q->Cancel || q->Worker || q->DeviceDebt) return BC250_OA_CORRUPT;
        } else {
            ++pending;
            if (q->Id > r->LastRequest || !Bc250OaAway(r, q->Canonical, sizeof(BC250_OA_TOKEN)) ||
                (q->Ownership != BC250_OA_OWNED && q->Ownership != BC250_OA_BORROWED) ||
                q->Cancel > 1U || q->Worker > 1U || q->DeviceDebt > 1U) return BC250_OA_CORRUPT;
            for (j = 0; j < i; ++j)
                if (r->Requests[j].Id == q->Id || r->Requests[j].Canonical == q->Canonical) return BC250_OA_CORRUPT;
            if (r->State == BC250_OA_DEAD) return BC250_OA_CORRUPT;
        }
    }
    if (r->Completed > r->LastRequest || r->LastRequest - r->Completed != pending ||
        r->OwnedFreed > r->Completed || r->BorrowedReturned != r->Completed - r->OwnedFreed)
        return BC250_OA_CORRUPT;
    for (i = 0; i < BC250_OA_CALLBACKS; ++i) {
        const BC250_OA_CALLBACK_RECORD *c = &r->Callbacks[i];
        unsigned int parent = 0;
        if (!c->Id) {
            if (c->Canonical || c->RequestId) return BC250_OA_CORRUPT;
        } else {
            if (c->Id > r->LastCallback || !Bc250OaAway(r, c->Canonical, sizeof(BC250_OA_TOKEN))) return BC250_OA_CORRUPT;
            for (j = 0; j < i; ++j)
                if (r->Callbacks[j].Id == c->Id || r->Callbacks[j].Canonical == c->Canonical) return BC250_OA_CORRUPT;
            for (j = 0; j < BC250_OA_REQUESTS; ++j)
                if (r->Requests[j].Id && r->Requests[j].Id == c->RequestId) ++parent;
            if (parent != 1U || r->State == BC250_OA_DEAD) return BC250_OA_CORRUPT;
        }
    }
    return BC250_OA_OK;
}
static __inline BC250_OA_STATUS Bc250OaGuard(BC250_OWNER_ANCHOR *r)
{
    BC250_OA_STATUS status = Bc250OaCheck(r);
    if (status != BC250_OA_OK) return status;
    return r->State == BC250_OA_DEAD ? BC250_OA_DEAD_RESULT : BC250_OA_OK;
}
static __inline int Bc250OaOutput(BC250_OWNER_ANCHOR *r, BC250_OA_TOKEN *out)
{
    static const BC250_OA_TOKEN zero = {0};
    unsigned int i;
    if (!Bc250OaAway(r, out, sizeof(*out)) || memcmp(out, &zero, sizeof(zero))) return 0;
    for (i = 0; i < BC250_OA_CALLS; ++i) if (r->Calls[i].Id &&
        !Bc250OaDisjoint(r->Calls[i].Canonical, sizeof(*out), out, sizeof(*out))) return 0;
    for (i = 0; i < BC250_OA_REQUESTS; ++i) if (r->Requests[i].Id &&
        !Bc250OaDisjoint(r->Requests[i].Canonical, sizeof(*out), out, sizeof(*out))) return 0;
    for (i = 0; i < BC250_OA_CALLBACKS; ++i) if (r->Callbacks[i].Id &&
        !Bc250OaDisjoint(r->Callbacks[i].Canonical, sizeof(*out), out, sizeof(*out))) return 0;
    return 1;
}
static __inline BC250_OA_STATUS Bc250OaToken(BC250_OWNER_ANCHOR *r, const BC250_OA_TOKEN *t, unsigned int kind)
{
    BC250_OA_STATUS status = Bc250OaGuard(r);
    if (status != BC250_OA_OK) return status;
    if (!Bc250OaAway(r, t, sizeof(*t)) || t->Self != t || t->Router != r || t->Class != kind || !t->Id)
        return BC250_OA_INVALID;
    if (kind == BC250_OA_CALL)
        return t->Slot < BC250_OA_CALLS && r->Calls[t->Slot].Id == t->Id &&
            r->Calls[t->Slot].Canonical == t ? BC250_OA_OK : BC250_OA_STALE;
    if (kind == BC250_OA_REQUEST)
        return t->Slot < BC250_OA_REQUESTS && r->Requests[t->Slot].Id == t->Id &&
            r->Requests[t->Slot].Canonical == t ? BC250_OA_OK : BC250_OA_STALE;
    return t->Slot < BC250_OA_CALLBACKS && r->Callbacks[t->Slot].Id == t->Id &&
        r->Callbacks[t->Slot].Canonical == t ? BC250_OA_OK : BC250_OA_STALE;
}
static __inline void Bc250OaPublish(BC250_OWNER_ANCHOR *r, BC250_OA_TOKEN *out,
    unsigned int slot, unsigned int kind, uint64_t id)
{
    out->Self = out; out->Router = r; out->Slot = slot; out->Class = kind; out->Id = id;
}
static __inline BC250_OA_STATUS Bc250OaInit(BC250_OWNER_ANCHOR *r)
{
    static const BC250_OWNER_ANCHOR zero = {0};
    if (Bc250OwnerAnchorRamAllowed != 1U) return BC250_OA_DISABLED;
    if (!r || memcmp(r, &zero, sizeof(zero))) return BC250_OA_INVALID;
    r->Self = r; r->State = BC250_OA_ACTIVE; return BC250_OA_OK;
}
static __inline BC250_OA_STATUS Bc250OaBegin(BC250_OWNER_ANCHOR *r, BC250_OA_TOKEN *out)
{
    unsigned int i;
    BC250_OA_STATUS status = Bc250OaGuard(r);
    if (status != BC250_OA_OK) return status;
    if (!Bc250OaOutput(r, out)) return BC250_OA_INVALID;
    if (r->LastCall == UINT64_MAX) return BC250_OA_EXHAUSTED;
    for (i = 0; i < BC250_OA_CALLS && r->Calls[i].Id; ++i) {}
    if (i == BC250_OA_CALLS) return BC250_OA_FULL;
    r->Calls[i].Id = ++r->LastCall; r->Calls[i].Canonical = out;
    Bc250OaPublish(r, out, i, BC250_OA_CALL, r->LastCall);
    return BC250_OA_OK; /* Count BEFORE admission, also for late rejected calls. */
}
static __inline BC250_OA_STATUS Bc250OaAdmit(BC250_OWNER_ANCHOR *r, const BC250_OA_TOKEN *call)
{
    BC250_OA_STATUS status = Bc250OaToken(r, call, BC250_OA_CALL);
    if (status != BC250_OA_OK) return status;
    if (r->Calls[call->Slot].Decided) return BC250_OA_INVALID;
    r->Calls[call->Slot].Decided = 1U;
    if (r->State != BC250_OA_ACTIVE) return BC250_OA_CLOSED;
    r->Calls[call->Slot].Admitted = 1U; return BC250_OA_OK;
}
static __inline BC250_OA_STATUS Bc250OaLeave(BC250_OWNER_ANCHOR *r, const BC250_OA_TOKEN *call)
{
    unsigned int slot;
    BC250_OA_STATUS status = Bc250OaToken(r, call, BC250_OA_CALL);
    if (status != BC250_OA_OK) return status;
    slot = call->Slot; memset(&r->Calls[slot], 0, sizeof(r->Calls[slot]));
    return BC250_OA_OK; /* NO owner/token access after dropping the obligation. */
}
static __inline BC250_OA_STATUS Bc250OaStop(BC250_OWNER_ANCHOR *r)
{
    BC250_OA_STATUS status = Bc250OaGuard(r);
    if (status != BC250_OA_OK) return status;
    r->State = BC250_OA_STOPPED; return BC250_OA_OK;
}
static __inline BC250_OA_STATUS Bc250OaRequest(BC250_OWNER_ANCHOR *r, const BC250_OA_TOKEN *call,
    unsigned int ownership, BC250_OA_TOKEN *out)
{
    unsigned int i;
    BC250_OA_STATUS status = Bc250OaToken(r, call, BC250_OA_CALL);
    if (status != BC250_OA_OK) return status;
    if (r->State != BC250_OA_ACTIVE || !r->Calls[call->Slot].Admitted) return BC250_OA_CLOSED;
    if ((ownership != BC250_OA_OWNED && ownership != BC250_OA_BORROWED) || !Bc250OaOutput(r, out))
        return BC250_OA_INVALID;
    if (r->LastRequest == UINT64_MAX) return BC250_OA_EXHAUSTED;
    for (i = 0; i < BC250_OA_REQUESTS && r->Requests[i].Id; ++i) {}
    if (i == BC250_OA_REQUESTS) return BC250_OA_FULL;
    r->Requests[i].Id = ++r->LastRequest; r->Requests[i].Canonical = out; r->Requests[i].Ownership = ownership;
    Bc250OaPublish(r, out, i, BC250_OA_REQUEST, r->LastRequest); return BC250_OA_OK;
}
static __inline BC250_OA_STATUS Bc250OaCancel(BC250_OWNER_ANCHOR *r, const BC250_OA_TOKEN *request)
{
    BC250_OA_STATUS status = Bc250OaToken(r, request, BC250_OA_REQUEST);
    if (status != BC250_OA_OK) return status;
    r->Requests[request->Slot].Cancel = 1U; return BC250_OA_OK; /* Intent ONLY. */
}
static __inline BC250_OA_STATUS Bc250OaWorker(BC250_OWNER_ANCHOR *r, const BC250_OA_TOKEN *request, unsigned int enter)
{
    BC250_OA_REQUEST_RECORD *q;
    BC250_OA_STATUS status = Bc250OaToken(r, request, BC250_OA_REQUEST);
    if (status != BC250_OA_OK) return status;
    q = &r->Requests[request->Slot];
    if (enter > 1U || q->Worker == enter) return BC250_OA_INVALID;
    if (enter && (q->Cancel || r->State != BC250_OA_ACTIVE)) return BC250_OA_CLOSED;
    q->Worker = enter; return BC250_OA_OK;
}
static __inline BC250_OA_STATUS Bc250OaDeviceDebt(BC250_OWNER_ANCHOR *r, const BC250_OA_TOKEN *request, unsigned int acquire)
{
    BC250_OA_REQUEST_RECORD *q;
    BC250_OA_STATUS status = Bc250OaToken(r, request, BC250_OA_REQUEST);
    if (status != BC250_OA_OK) return status;
    q = &r->Requests[request->Slot];
    if (acquire > 1U || q->DeviceDebt == acquire) return BC250_OA_INVALID;
    if (acquire && (!q->Worker || q->Cancel || r->State != BC250_OA_ACTIVE)) return BC250_OA_CLOSED;
    q->DeviceDebt = acquire; return BC250_OA_OK; /* Synthetic completion event, NOT a fence. */
}
static __inline BC250_OA_STATUS Bc250OaCallback(BC250_OWNER_ANCHOR *r, const BC250_OA_TOKEN *request, BC250_OA_TOKEN *out)
{
    unsigned int i;
    BC250_OA_STATUS status = Bc250OaToken(r, request, BC250_OA_REQUEST);
    if (status != BC250_OA_OK) return status;
    if (!Bc250OaOutput(r, out)) return BC250_OA_INVALID;
    if (r->LastCallback == UINT64_MAX) return BC250_OA_EXHAUSTED;
    for (i = 0; i < BC250_OA_CALLBACKS && r->Callbacks[i].Id; ++i) {}
    if (i == BC250_OA_CALLBACKS) return BC250_OA_FULL;
    r->Callbacks[i].Id = ++r->LastCallback; r->Callbacks[i].Canonical = out; r->Callbacks[i].RequestId = request->Id;
    Bc250OaPublish(r, out, i, BC250_OA_CALLBACK, r->LastCallback); return BC250_OA_OK;
}
static __inline BC250_OA_STATUS Bc250OaCallbackLeave(BC250_OWNER_ANCHOR *r, const BC250_OA_TOKEN *callback)
{
    unsigned int slot;
    BC250_OA_STATUS status = Bc250OaToken(r, callback, BC250_OA_CALLBACK);
    if (status != BC250_OA_OK) return status;
    slot = callback->Slot; memset(&r->Callbacks[slot], 0, sizeof(r->Callbacks[slot]));
    return BC250_OA_OK; /* NO access after the callback obligation is dropped. */
}
static __inline BC250_OA_STATUS Bc250OaFinish(BC250_OWNER_ANCHOR *r, const BC250_OA_TOKEN *request)
{
    unsigned int i, slot;
    BC250_OA_STATUS status = Bc250OaToken(r, request, BC250_OA_REQUEST);
    if (status != BC250_OA_OK) return status;
    slot = request->Slot;
    if (r->Requests[slot].Worker || r->Requests[slot].DeviceDebt) return BC250_OA_IN_USE;
    for (i = 0; i < BC250_OA_CALLBACKS; ++i)
        if (r->Callbacks[i].Id && r->Callbacks[i].RequestId == request->Id) return BC250_OA_IN_USE;
    if (r->Requests[slot].Ownership == BC250_OA_OWNED) ++r->OwnedFreed;
    else ++r->BorrowedReturned;
    ++r->Completed; memset(&r->Requests[slot], 0, sizeof(r->Requests[slot])); return BC250_OA_OK;
}
static __inline BC250_OA_STATUS Bc250OaRetire(BC250_OWNER_ANCHOR *r)
{
    unsigned int i;
    BC250_OA_STATUS status = Bc250OaGuard(r);
    if (status != BC250_OA_OK) return status;
    if (r->State != BC250_OA_STOPPED) return BC250_OA_CLOSED;
    for (i = 0; i < BC250_OA_CALLS; ++i) if (r->Calls[i].Id) return BC250_OA_IN_USE;
    for (i = 0; i < BC250_OA_REQUESTS; ++i)
        if (r->Requests[i].Id || r->Requests[i].Worker || r->Requests[i].DeviceDebt) return BC250_OA_IN_USE;
    for (i = 0; i < BC250_OA_CALLBACKS; ++i) if (r->Callbacks[i].Id) return BC250_OA_IN_USE;
    r->State = BC250_OA_DEAD; return BC250_OA_OK; /* Logical only, NEVER free router. */
}
#endif
