/* SPDX-License-Identifier: Apache-2.0
 * CPU-only numeric backing registry. No allocation, mapping, OS DMA ownership,
 * GPU access or automatic connection to the CPU tree backend.
 * Caller supplies a zeroed pool at a stable address for its entire lifetime,
 * exclusively accessed, plus valid/disjoint/stable buffers. Never reset/reuse
 * the pool while handles/tokens exist: pointer identity is NOT an OS identity.
 * Acquire one lease per mapping occurrence, including aliases. Retain leases
 * when unmap fails; Drop only after successful unmap (or failed map rollback).
 * Release removes metadata, NOT physical memory. No callback can free backing.
 * Tokens are trusted caller metadata, not cryptographic/unforgeable capabilities.
 */
#ifndef BC250_VM_CPU_BACKING_REFS_H
#define BC250_VM_CPU_BACKING_REFS_H
#include <string.h>
#include "upstream-integration-20261003/bc250_dma_page_list.h"

#define BC250_BACKING_SLOTS 8U
#define BC250_BACKING_PAGES 64U
#define BC250_BACKING_LEASES 64U
typedef enum BC250_BACKING_STATUS {
    BC250_BACKING_OK, BC250_BACKING_INVALID, BC250_BACKING_FULL,
    BC250_BACKING_BUSY, BC250_BACKING_STALE, BC250_BACKING_EXHAUSTED,
    BC250_BACKING_CORRUPT
} BC250_BACKING_STATUS;
typedef struct BC250_BACKING_POOL BC250_BACKING_POOL;
typedef struct BC250_BACKING_HANDLE {
    const BC250_BACKING_POOL *Pool;
    BC250_GART_U64 Generation;
    unsigned int Slot;
} BC250_BACKING_HANDLE;
typedef struct BC250_BACKING_TOKEN {
    const BC250_BACKING_POOL *Pool;
    BC250_GART_U64 Generation, LeaseId;
    unsigned int Slot;
} BC250_BACKING_TOKEN;
typedef struct BC250_BACKING_RECORD {
    BC250_ADDRESS_SPAN Pages[BC250_BACKING_PAGES];
    BC250_GART_U64 Generation, MaximumDmaLast;
    unsigned int Registered, PageCount, References;
} BC250_BACKING_RECORD;
typedef struct BC250_BACKING_LEASE {
    BC250_GART_U64 Generation, Id;
    unsigned int Live, Slot;
} BC250_BACKING_LEASE;
struct BC250_BACKING_POOL {
    BC250_BACKING_RECORD Records[BC250_BACKING_SLOTS];
    BC250_BACKING_LEASE Leases[BC250_BACKING_LEASES];
    BC250_GART_U64 LastLeaseId;
};

static __inline int Bc250BackingAway(const BC250_BACKING_POOL *pool,
    const void *buffer, BC250_GART_U64 bytes)
{
    return pool && Bc250DmaPageListDisjoint(pool, sizeof(*pool), buffer, bytes);
}

/* Internal consistency, not validation of the pool pointer or DMA provenance. */
static __inline int Bc250BackingConsistent(const BC250_BACKING_POOL *pool)
{
    unsigned int refs[BC250_BACKING_SLOTS] = {0};
    unsigned int i, j;
    BC250_GART_U64 encoded;
    if (!pool) return 0;
    for (i = 0; i < BC250_BACKING_LEASES; ++i) {
        const BC250_BACKING_LEASE *lease = &pool->Leases[i];
        if (!lease->Live) continue;
        if (lease->Live != 1U || lease->Slot >= BC250_BACKING_SLOTS ||
            !lease->Id || lease->Id > pool->LastLeaseId ||
            !pool->Records[lease->Slot].Registered ||
            lease->Generation != pool->Records[lease->Slot].Generation) return 0;
        for (j = 0; j < i; ++j)
            if (pool->Leases[j].Live && pool->Leases[j].Id == lease->Id) return 0;
        ++refs[lease->Slot];
    }
    for (i = 0; i < BC250_BACKING_SLOTS; ++i) {
        const BC250_BACKING_RECORD *record = &pool->Records[i];
        if (record->References != refs[i] || record->Registered > 1U) return 0;
        if (!record->Registered) {
            if (record->PageCount || record->References) return 0;
            continue;
        }
        if (!record->Generation || !record->PageCount ||
            record->PageCount > BC250_BACKING_PAGES) return 0;
        for (j = 0; j < record->PageCount; ++j)
            if (!Bc250VmEncodeSystemLeaf(&record->Pages[j], record->MaximumDmaLast, 0U, &encoded)) return 0;
    }
    return 1;
}

static __inline BC250_BACKING_STATUS Bc250BackingRegister(BC250_BACKING_POOL *pool,
    const BC250_ADDRESS_SPAN *pages, unsigned int count, unsigned int capacity,
    BC250_GART_U64 maximum_dma_last, BC250_BACKING_HANDLE *out)
{
    unsigned int i, slot = BC250_BACKING_SLOTS, exhausted = 0U;
    BC250_GART_U64 encoded;
    BC250_BACKING_RECORD *record;
    BC250_BACKING_HANDLE handle = {0};
    if (!pool || !pages || !out || !count || count > BC250_BACKING_PAGES ||
        count > capacity || !Bc250BackingAway(pool, out, sizeof(*out)) ||
        !Bc250BackingAway(pool, pages, (BC250_GART_U64)count * sizeof(*pages)) ||
        !Bc250DmaPageListDisjoint(pages, (BC250_GART_U64)count * sizeof(*pages), out, sizeof(*out)))
        return BC250_BACKING_INVALID;
    if (!Bc250BackingConsistent(pool)) return BC250_BACKING_CORRUPT;
    for (i = 0; i < count; ++i)
        if (!Bc250VmEncodeSystemLeaf(&pages[i], maximum_dma_last, 0U, &encoded))
            return BC250_BACKING_INVALID;
    for (i = 0; i < BC250_BACKING_SLOTS; ++i) {
        if (!pool->Records[i].Registered) {
            if (pool->Records[i].Generation == BC250_GART_U64_MAX) exhausted = 1U;
            else { slot = i; break; }
        }
    }
    if (slot == BC250_BACKING_SLOTS)
        return exhausted ? BC250_BACKING_EXHAUSTED : BC250_BACKING_FULL;
    record = &pool->Records[slot];
    handle.Pool = pool; handle.Slot = slot; handle.Generation = record->Generation + 1ULL;
    /* No fallible operation follows the first persistent mutation. */
    memset(record, 0, sizeof(*record));
    memcpy(record->Pages, pages, (size_t)count * sizeof(*pages));
    record->Generation = handle.Generation; record->MaximumDmaLast = maximum_dma_last;
    record->PageCount = count; record->Registered = 1U;
    *out = handle;
    return BC250_BACKING_OK;
}

static __inline int Bc250BackingHandleMatches(const BC250_BACKING_POOL *pool,
    const BC250_BACKING_HANDLE *handle)
{
    return handle->Pool == pool && handle->Slot < BC250_BACKING_SLOTS &&
        pool->Records[handle->Slot].Registered && handle->Generation &&
        pool->Records[handle->Slot].Generation == handle->Generation;
}

static __inline BC250_BACKING_STATUS Bc250BackingAcquire(BC250_BACKING_POOL *pool,
    const BC250_BACKING_HANDLE *handle, BC250_BACKING_TOKEN *out)
{
    unsigned int i;
    BC250_BACKING_TOKEN token = {0};
    if (!pool || !handle || !out || !Bc250BackingAway(pool, handle, sizeof(*handle)) ||
        !Bc250BackingAway(pool, out, sizeof(*out)) ||
        !Bc250DmaPageListDisjoint(handle, sizeof(*handle), out, sizeof(*out))) return BC250_BACKING_INVALID;
    if (!Bc250BackingConsistent(pool)) return BC250_BACKING_CORRUPT;
    if (!Bc250BackingHandleMatches(pool, handle)) return BC250_BACKING_STALE;
    if (pool->LastLeaseId == BC250_GART_U64_MAX) return BC250_BACKING_EXHAUSTED;
    for (i = 0; i < BC250_BACKING_LEASES; ++i) if (!pool->Leases[i].Live) break;
    if (i == BC250_BACKING_LEASES) return BC250_BACKING_FULL;
    token.Pool = pool; token.Slot = handle->Slot; token.Generation = handle->Generation;
    token.LeaseId = pool->LastLeaseId + 1ULL;
    pool->Leases[i].Generation = token.Generation;
    pool->Leases[i].Id = token.LeaseId; pool->Leases[i].Slot = token.Slot;
    pool->Leases[i].Live = 1U; pool->LastLeaseId = token.LeaseId;
    ++pool->Records[token.Slot].References;
    *out = token;
    return BC250_BACKING_OK;
}

static __inline BC250_BACKING_STATUS Bc250BackingDrop(BC250_BACKING_POOL *pool,
    const BC250_BACKING_TOKEN *token)
{
    unsigned int i;
    if (!pool || !token || !Bc250BackingAway(pool, token, sizeof(*token))) return BC250_BACKING_INVALID;
    if (!Bc250BackingConsistent(pool)) return BC250_BACKING_CORRUPT;
    if (token->Pool != pool || token->Slot >= BC250_BACKING_SLOTS ||
        !token->Generation || !token->LeaseId) return BC250_BACKING_STALE;
    for (i = 0; i < BC250_BACKING_LEASES; ++i) {
        BC250_BACKING_LEASE *lease = &pool->Leases[i];
        if (lease->Live && lease->Id == token->LeaseId && lease->Slot == token->Slot &&
            lease->Generation == token->Generation) {
            --pool->Records[token->Slot].References;
            memset(lease, 0, sizeof(*lease));
            return BC250_BACKING_OK;
        }
    }
    return BC250_BACKING_STALE;
}

static __inline BC250_BACKING_STATUS Bc250BackingRelease(BC250_BACKING_POOL *pool,
    const BC250_BACKING_HANDLE *handle)
{
    BC250_BACKING_RECORD *record;
    BC250_GART_U64 generation;
    if (!pool || !handle || !Bc250BackingAway(pool, handle, sizeof(*handle))) return BC250_BACKING_INVALID;
    if (!Bc250BackingConsistent(pool)) return BC250_BACKING_CORRUPT;
    if (!Bc250BackingHandleMatches(pool, handle)) return BC250_BACKING_STALE;
    record = &pool->Records[handle->Slot];
    if (record->References) return BC250_BACKING_BUSY;
    generation = record->Generation;
    memset(record, 0, sizeof(*record));
    record->Generation = generation;
    return BC250_BACKING_OK;
}
#endif
