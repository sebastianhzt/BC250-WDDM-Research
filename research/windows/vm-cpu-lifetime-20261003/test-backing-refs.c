/* SPDX-License-Identifier: Apache-2.0
 * Tests actual numeric registry in caller-owned CPU RAM. No mapping, OS DMA,
 * GPU or automatic Map/Unmap integration. Forged counters are negative fixtures.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vm-cpu-lifetime-20261003/bc250_vm_cpu_backing_refs.h"
static unsigned int assertions;
#define CHECK(c) do { ++assertions; if (!(c)) { fprintf(stderr, "FAIL %u: %s\n", \
    (unsigned int)__LINE__, #c); exit(1); } } while (0)

static void Pages(BC250_ADDRESS_SPAN *pages, unsigned int count)
{
    unsigned int i;
    for (i = 0; i < count; ++i)
        CHECK(Bc250DescribeAddressNumbers(BC250_ADDRESS_DMA_LOGICAL,
            ((BC250_GART_U64)((i * 7U) % 13U) + 1ULL) * 4096ULL,
            4096ULL, BC250_VM_MAX_ADDRESS, &pages[i]));
}

static void AliasesAndReuse(void)
{
    BC250_BACKING_POOL pool, other, before;
    BC250_ADDRESS_SPAN pages[3];
    BC250_BACKING_HANDLE first, second, newer;
    BC250_BACKING_TOKEN a, b, c, stale;
    memset(&pool, 0, sizeof(pool)); memset(&other, 0, sizeof(other));
    Pages(pages, 3); pages[2] = pages[0];
    CHECK(Bc250BackingRegister(&pool, pages, 3, 3, BC250_VM_MAX_ADDRESS, &first) == BC250_BACKING_OK);
    CHECK(first.Pool == &pool && first.Slot == 0 && first.Generation == 1);
    CHECK(pool.Records[0].PageCount == 3 && pool.Records[0].References == 0);
    CHECK(memcmp(pool.Records[0].Pages, pages, sizeof(pages)) == 0);
    pages[0].Start = 0; /* Registry owns its descriptor COPY, not the input buffer. */
    CHECK(pool.Records[0].Pages[0].Start == 4096 && pool.Records[0].Pages[2].Start == 4096);
    Pages(pages, 3);
    CHECK(Bc250BackingRegister(&pool, pages, 3, 3, BC250_VM_MAX_ADDRESS, &second) == BC250_BACKING_OK);
    CHECK(Bc250BackingAcquire(&pool, &first, &a) == BC250_BACKING_OK);
    CHECK(Bc250BackingAcquire(&pool, &first, &b) == BC250_BACKING_OK);
    CHECK(Bc250BackingAcquire(&pool, &second, &c) == BC250_BACKING_OK);
    CHECK(a.LeaseId == 1 && b.LeaseId == 2 && c.LeaseId == 3);
    CHECK(pool.Records[0].References == 2 && pool.Records[1].References == 1);
    memcpy(&before, &pool, sizeof(pool));
    CHECK(Bc250BackingRelease(&pool, &first) == BC250_BACKING_BUSY);
    CHECK(memcmp(&pool, &before, sizeof(pool)) == 0);
    CHECK(Bc250BackingAcquire(&other, &first, &stale) == BC250_BACKING_STALE);
    CHECK(Bc250BackingDrop(&other, &a) == BC250_BACKING_STALE);
    CHECK(Bc250BackingRelease(&other, &first) == BC250_BACKING_STALE);
    stale = a; stale.Slot = second.Slot;
    CHECK(Bc250BackingDrop(&pool, &stale) == BC250_BACKING_STALE);
    stale = a; ++stale.Generation;
    CHECK(Bc250BackingDrop(&pool, &stale) == BC250_BACKING_STALE);
    CHECK(memcmp(&pool, &before, sizeof(pool)) == 0);
    CHECK(Bc250BackingDrop(&pool, &a) == BC250_BACKING_OK);
    memcpy(&before, &pool, sizeof(pool));
    CHECK(Bc250BackingDrop(&pool, &a) == BC250_BACKING_STALE);
    CHECK(memcmp(&pool, &before, sizeof(pool)) == 0);
    CHECK(pool.Records[0].References == 1 && pool.Records[1].References == 1);
    CHECK(Bc250BackingDrop(&pool, &b) == BC250_BACKING_OK);
    CHECK(Bc250BackingRelease(&pool, &first) == BC250_BACKING_OK);
    CHECK(!pool.Records[0].Registered && pool.Records[0].Generation == 1);
    CHECK(Bc250BackingRegister(&pool, pages, 3, 3, BC250_VM_MAX_ADDRESS, &newer) == BC250_BACKING_OK);
    CHECK(newer.Slot == first.Slot && newer.Generation == 2);
    CHECK(Bc250BackingAcquire(&pool, &newer, &stale) == BC250_BACKING_OK);
    CHECK(stale.LeaseId == 4);
    memcpy(&before, &pool, sizeof(pool));
    CHECK(Bc250BackingAcquire(&pool, &first, &b) == BC250_BACKING_STALE);
    CHECK(Bc250BackingRelease(&pool, &first) == BC250_BACKING_STALE);
    CHECK(Bc250BackingDrop(&pool, &a) == BC250_BACKING_STALE);
    CHECK(memcmp(&pool, &before, sizeof(pool)) == 0);
    CHECK(Bc250BackingDrop(&pool, &stale) == BC250_BACKING_OK);
    CHECK(Bc250BackingDrop(&pool, &c) == BC250_BACKING_OK);
    CHECK(Bc250BackingRelease(&pool, &newer) == BC250_BACKING_OK);
    CHECK(Bc250BackingRelease(&pool, &second) == BC250_BACKING_OK);
    CHECK(Bc250BackingConsistent(&pool));
}

static void Capacities(void)
{
    BC250_BACKING_POOL pool, before;
    BC250_BACKING_HANDLE handles[8], output, saved;
    BC250_BACKING_TOKEN leases[64], token, saved_token;
    BC250_ADDRESS_SPAN pages[64];
    unsigned int i;
    memset(&pool, 0, sizeof(pool)); Pages(pages, 64);
    for (i = 0; i < 8; ++i) {
        CHECK(Bc250BackingRegister(&pool, pages, 64, 64, BC250_VM_MAX_ADDRESS, &handles[i]) == BC250_BACKING_OK);
        CHECK(handles[i].Slot == i && handles[i].Generation == 1);
    }
    memcpy(&before, &pool, sizeof(pool)); memset(&output, 0xA5, sizeof(output));
    memcpy(&saved, &output, sizeof(output));
    CHECK(Bc250BackingRegister(&pool, pages, 64, 64, BC250_VM_MAX_ADDRESS, &output) == BC250_BACKING_FULL);
    CHECK(memcmp(&pool, &before, sizeof(pool)) == 0 && memcmp(&output, &saved, sizeof(output)) == 0);
    for (i = 0; i < 64; ++i) {
        CHECK(Bc250BackingAcquire(&pool, &handles[i % 8], &leases[i]) == BC250_BACKING_OK);
        CHECK(leases[i].LeaseId == (BC250_GART_U64)i + 1);
    }
    for (i = 0; i < 8; ++i) CHECK(pool.Records[i].References == 8);
    memcpy(&before, &pool, sizeof(pool)); memset(&token, 0xA5, sizeof(token));
    memcpy(&saved_token, &token, sizeof(token));
    CHECK(Bc250BackingAcquire(&pool, &handles[0], &token) == BC250_BACKING_FULL);
    CHECK(memcmp(&pool, &before, sizeof(pool)) == 0 && memcmp(&token, &saved_token, sizeof(token)) == 0);
    for (i = 0; i < 64; ++i) {
        unsigned int index = (i * 17U) % 64U; /* permutation independent of slot order */
        CHECK(Bc250BackingDrop(&pool, &leases[index]) == BC250_BACKING_OK);
        CHECK(Bc250BackingDrop(&pool, &leases[index]) == BC250_BACKING_STALE);
        CHECK(Bc250BackingConsistent(&pool));
    }
    for (i = 0; i < 8; ++i) {
        CHECK(pool.Records[i].References == 0);
        CHECK(Bc250BackingRelease(&pool, &handles[i]) == BC250_BACKING_OK);
    }
}

static void InvalidDescriptorsAndAliasing(void)
{
    BC250_BACKING_POOL pool, before;
    BC250_BACKING_HANDLE out, saved_out, handle;
    BC250_BACKING_TOKEN token;
    BC250_ADDRESS_SPAN pages[64], original;
    unsigned int kind, point;
    memset(&pool, 0, sizeof(pool)); Pages(pages, 64);
    memcpy(&before, &pool, sizeof(pool)); memset(&out, 0xA5, sizeof(out));
    memcpy(&saved_out, &out, sizeof(out));
    for (kind = 0; kind < 6; ++kind) for (point = 0; point < 3; ++point) {
        unsigned int index = point == 0 ? 0 : point == 1 ? 31 : 63;
        original = pages[index];
        switch (kind) {
        case 0: pages[index].Domain = BC250_ADDRESS_CPU_PHYSICAL; break;
        case 1: ++pages[index].Start; ++pages[index].Last; break;
        case 2: pages[index].Bytes = 8192; break;
        case 3: --pages[index].Last; break;
        case 4: pages[index].Start = 1ULL << 48; pages[index].Last = pages[index].Start + 4095; break;
        default: pages[index].Domain = BC250_ADDRESS_GPU_VIRTUAL; break;
        }
        CHECK(Bc250BackingRegister(&pool, pages, 64, 64, BC250_VM_MAX_ADDRESS, &out) == BC250_BACKING_INVALID);
        CHECK(memcmp(&pool, &before, sizeof(pool)) == 0 && memcmp(&out, &saved_out, sizeof(out)) == 0);
        pages[index] = original;
    }
#define BAD_REGISTER(array, count, capacity, maximum, result) do { \
    CHECK(Bc250BackingRegister(&pool, array, count, capacity, maximum, result) == BC250_BACKING_INVALID); \
    CHECK(memcmp(&pool, &before, sizeof(pool)) == 0); } while (0)
    BAD_REGISTER(pages, 0, 64, BC250_VM_MAX_ADDRESS, &out);
    BAD_REGISTER(pages, 65, 64, BC250_VM_MAX_ADDRESS, &out);
    BAD_REGISTER(pages, 64, 63, BC250_VM_MAX_ADDRESS, &out);
    BAD_REGISTER(NULL, 1, 1, BC250_VM_MAX_ADDRESS, &out);
    BAD_REGISTER(pages, 1, 1, BC250_VM_MAX_ADDRESS, NULL);
    BAD_REGISTER(pages, 1, 1, 0xFFFULL, &out);
    BAD_REGISTER(pages, 1, 1, 1ULL << 48, &out);
    BAD_REGISTER(pool.Records[0].Pages, 1, 1, BC250_VM_MAX_ADDRESS, &out);
    BAD_REGISTER(pages, 1, 1, BC250_VM_MAX_ADDRESS, (BC250_BACKING_HANDLE *)&pool);
    BAD_REGISTER(pages, 1, 1, BC250_VM_MAX_ADDRESS, (BC250_BACKING_HANDLE *)pages);
#undef BAD_REGISTER
    CHECK(Bc250BackingRegister(&pool, pages, 1, 1, BC250_VM_MAX_ADDRESS, &handle) == BC250_BACKING_OK);
    memcpy(&before, &pool, sizeof(pool));
    CHECK(Bc250BackingAcquire(&pool, &handle, (BC250_BACKING_TOKEN *)&pool) == BC250_BACKING_INVALID);
    CHECK(Bc250BackingAcquire(&pool, (BC250_BACKING_HANDLE *)&pool, &token) == BC250_BACKING_INVALID);
    CHECK(Bc250BackingRelease(&pool, (BC250_BACKING_HANDLE *)&pool) == BC250_BACKING_INVALID);
    CHECK(Bc250BackingDrop(&pool, (BC250_BACKING_TOKEN *)&pool) == BC250_BACKING_INVALID);
    CHECK(memcmp(&pool, &before, sizeof(pool)) == 0);
    CHECK(Bc250BackingRelease(&pool, &handle) == BC250_BACKING_OK);
    CHECK(Bc250BackingRegister(NULL, pages, 1, 1, BC250_VM_MAX_ADDRESS, &out) == BC250_BACKING_INVALID);
    CHECK(Bc250BackingAcquire(NULL, &handle, &token) == BC250_BACKING_INVALID);
    CHECK(Bc250BackingAcquire(&pool, NULL, &token) == BC250_BACKING_INVALID);
    CHECK(Bc250BackingAcquire(&pool, &handle, NULL) == BC250_BACKING_INVALID);
    CHECK(Bc250BackingDrop(NULL, &token) == BC250_BACKING_INVALID);
    CHECK(Bc250BackingDrop(&pool, NULL) == BC250_BACKING_INVALID);
    CHECK(Bc250BackingRelease(NULL, &handle) == BC250_BACKING_INVALID);
    CHECK(Bc250BackingRelease(&pool, NULL) == BC250_BACKING_INVALID);
}

static void ExhaustionAndCorruption(void)
{
    BC250_BACKING_POOL pool, before;
    BC250_BACKING_HANDLE handle, next;
    BC250_BACKING_TOKEN token, rejected;
    BC250_ADDRESS_SPAN page;
    unsigned int i;
    memset(&pool, 0, sizeof(pool)); Pages(&page, 1);
    pool.Records[0].Generation = BC250_GART_U64_MAX - 1;
    CHECK(Bc250BackingRegister(&pool, &page, 1, 1, BC250_VM_MAX_ADDRESS, &handle) == BC250_BACKING_OK);
    CHECK(handle.Generation == BC250_GART_U64_MAX);
    pool.LastLeaseId = BC250_GART_U64_MAX - 1;
    CHECK(Bc250BackingAcquire(&pool, &handle, &token) == BC250_BACKING_OK);
    CHECK(token.LeaseId == BC250_GART_U64_MAX);
    memcpy(&before, &pool, sizeof(pool));
    CHECK(Bc250BackingAcquire(&pool, &handle, &rejected) == BC250_BACKING_EXHAUSTED);
    CHECK(memcmp(&pool, &before, sizeof(pool)) == 0);
    CHECK(Bc250BackingDrop(&pool, &token) == BC250_BACKING_OK);
    CHECK(Bc250BackingRelease(&pool, &handle) == BC250_BACKING_OK);
    CHECK(Bc250BackingRegister(&pool, &page, 1, 1, BC250_VM_MAX_ADDRESS, &next) == BC250_BACKING_OK);
    CHECK(next.Slot == 1 && next.Generation == 1); /* exhausted slot never wraps */
    CHECK(Bc250BackingAcquire(&pool, &next, &rejected) == BC250_BACKING_EXHAUSTED);
    CHECK(Bc250BackingRelease(&pool, &next) == BC250_BACKING_OK);
    for (i = 0; i < 8; ++i) pool.Records[i].Generation = BC250_GART_U64_MAX;
    memcpy(&before, &pool, sizeof(pool));
    CHECK(Bc250BackingRegister(&pool, &page, 1, 1, BC250_VM_MAX_ADDRESS, &next) == BC250_BACKING_EXHAUSTED);
    CHECK(memcmp(&pool, &before, sizeof(pool)) == 0);
    memset(&pool, 0, sizeof(pool));
    CHECK(Bc250BackingRegister(&pool, &page, 1, 1, BC250_VM_MAX_ADDRESS, &handle) == BC250_BACKING_OK);
    CHECK(Bc250BackingAcquire(&pool, &handle, &token) == BC250_BACKING_OK);
    ++pool.Records[0].References; memcpy(&before, &pool, sizeof(pool));
    CHECK(Bc250BackingDrop(&pool, &token) == BC250_BACKING_CORRUPT);
    CHECK(Bc250BackingRelease(&pool, &handle) == BC250_BACKING_CORRUPT);
    CHECK(memcmp(&pool, &before, sizeof(pool)) == 0);
    --pool.Records[0].References;
    CHECK(Bc250BackingDrop(&pool, &token) == BC250_BACKING_OK);
    CHECK(Bc250BackingRelease(&pool, &handle) == BC250_BACKING_OK);
}

int main(void)
{
    AliasesAndReuse(); Capacities(); InvalidDescriptorsAndAliasing(); ExhaustionAndCorruption();
    printf("PASS: %u CPU numeric backing/lease assertions; NOT OS DMA ownership or Map/Unmap integration.\n", assertions);
    return 0;
}
