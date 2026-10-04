/* SPDX-License-Identifier: Apache-2.0
 * CPU-owned fake arrays only. No OS/device APIs, DMA mapping or GPU accesses.
 */
#include <stdio.h>
#include <string.h>
#include "upstream-integration-20261003/bc250_dma_page_list.h"

static unsigned int checks, failures;
#define CHECK(c) do { ++checks; if (!(c)) { ++failures; \
    printf("FAIL line %u: %s\n", (unsigned int)__LINE__, #c); } } while (0)
#define POISON 0xDEADBEEF12345678ULL

static void Poison(BC250_GART_U64 *table, unsigned int count)
{
    unsigned int i;
    for (i = 0; i < count; ++i) table[i] = POISON;
}

static void Unchanged(const BC250_GART_U64 *table, unsigned int count)
{
    unsigned int i;
    for (i = 0; i < count; ++i) CHECK(table[i] == POISON);
}

static void BasicAndNegative(void)
{
    BC250_GART_GEOMETRY plan;
    BC250_ADDRESS_SPAN pages[3], saved;
    BC250_GART_U64 table[8];
    const BC250_GART_U64 dma_numbers[3] = {0x91000ULL, 0x12000ULL, 0x91000ULL};
    const BC250_GART_U64 cpu_numbers[3] = {0x22000ULL, 0x7B000ULL, 0x3C000ULL};
    unsigned int i, malformed;
    CHECK(Bc250GartPlan(8, 8, 4096, 8ULL * 4096, &plan));
    for (i = 0; i < 3; ++i) {
        CHECK(Bc250DescribeAddressNumbers(BC250_ADDRESS_DMA_LOGICAL,
            dma_numbers[i], 4096, BC250_VM_MAX_ADDRESS, &pages[i]));
        CHECK(dma_numbers[i] != cpu_numbers[i]);
    }
    Poison(table, 8);
    CHECK(Bc250DmaPageListEncode(&plan, 4096, 3ULL * 4096, pages, 3, 3,
        BC250_VM_MAX_ADDRESS, BC250_VM_ACCESS_READ | BC250_VM_ACCESS_WRITE, table, 8));
    for (i = 0; i < 8; ++i) {
        if (i >= 1 && i <= 3) {
            /* Independent bit arithmetic: no production codec used as oracle. */
            CHECK(table[i] == (dma_numbers[i - 1] | 0x63ULL));
            CHECK((table[i] & BC250_VM_PTE_ADDR_MASK) != cpu_numbers[i - 1]);
        } else CHECK(table[i] == POISON);
    }
    CHECK(table[1] == table[3]); /* legitimate repeated DMA-page numbers */

    for (malformed = 0; malformed < 8; ++malformed) {
        for (i = 0; i < 3; ++i) {
            saved = pages[i];
            switch (malformed) {
            case 0: pages[i].Domain = BC250_ADDRESS_CPU_PHYSICAL; break;
            case 1: pages[i].Domain = BC250_ADDRESS_GPU_VIRTUAL; break;
            case 2: pages[i].Start += 1; pages[i].Last += 1; break;
            case 3: pages[i].Bytes = 8192; pages[i].Last += 4096; break;
            case 4: pages[i].Last -= 1; break;
            case 5: pages[i].Start = 1ULL << 48; pages[i].Last = pages[i].Start + 4095; break;
            case 6: pages[i].Bytes = 0; break;
            default: pages[i].Domain = 0; break;
            }
            Poison(table, 8);
            CHECK(!Bc250DmaPageListEncode(&plan, 4096, 3ULL * 4096, pages, 3, 3,
                BC250_VM_MAX_ADDRESS, 3, table, 8));
            Unchanged(table, 8);
            pages[i] = saved;
        }
    }
    Poison(table, 8);
#define REFUSED(offset, length, count, capacity, maximum, flags, output_count) \
    do { CHECK(!Bc250DmaPageListEncode(&plan, offset, length, pages, count, capacity, \
        maximum, flags, table, output_count)); Unchanged(table, 8); } while (0)
    REFUSED(4096, 12288, 3, 2, BC250_VM_MAX_ADDRESS, 3, 8);
    REFUSED(4096, 12288, 2, 3, BC250_VM_MAX_ADDRESS, 3, 8);
    REFUSED(4096, 12288, 3, 3, BC250_VM_MAX_ADDRESS, 3, 3);
    REFUSED(1, 12288, 3, 3, BC250_VM_MAX_ADDRESS, 3, 8);
    REFUSED(4096, 12289, 3, 3, BC250_VM_MAX_ADDRESS, 3, 8);
    REFUSED(6ULL * 4096, 12288, 3, 3, BC250_VM_MAX_ADDRESS, 3, 8);
    REFUSED(~0ULL - 4095ULL, 12288, 3, 3, BC250_VM_MAX_ADDRESS, 3, 8);
    REFUSED(4096, 12288, 3, 3, 0x11FFFULL, 3, 8);
    REFUSED(4096, 12288, 3, 3, ~0ULL, 3, 8);
    REFUSED(4096, 12288, 3, 3, BC250_VM_MAX_ADDRESS, 16, 8);
    REFUSED(4096, 0, 0, 3, BC250_VM_MAX_ADDRESS, 3, 8);
#undef REFUSED
    plan.TableBytes -= 8;
    CHECK(!Bc250DmaPageListEncode(&plan, 4096, 12288, pages, 3, 3,
        BC250_VM_MAX_ADDRESS, 3, table, 8));
    Unchanged(table, 8);
}

static void OverlapAndBoundaries(void)
{
    BC250_GART_GEOMETRY plan;
    union { BC250_ADDRESS_SPAN Pages[3]; BC250_GART_U64 Words[16]; } storage;
    unsigned char before[sizeof(storage)];
    BC250_ADDRESS_SPAN high_page;
    BC250_GART_U64 table[8];
    CHECK(Bc250GartPlan(8, 8, 4096, 32768, &plan));
    memset(&storage, 0, sizeof(storage));
    CHECK(Bc250DescribeAddressNumbers(BC250_ADDRESS_DMA_LOGICAL, 0x1000,
        4096, BC250_VM_MAX_ADDRESS, &storage.Pages[0]));
    memcpy(before, &storage, sizeof(storage));
    CHECK(!Bc250DmaPageListEncode(&plan, 0, 4096, storage.Pages, 1, 3,
        BC250_VM_MAX_ADDRESS, 3, storage.Words, 16));
    CHECK(memcmp(before, &storage, sizeof(storage)) == 0);
    CHECK(!Bc250DmaPageListEncode(&plan, 4096, 4096, storage.Pages, 1, 3,
        BC250_VM_MAX_ADDRESS, 3, storage.Words, 16));
    CHECK(memcmp(before, &storage, sizeof(storage)) == 0);
    CHECK(!Bc250DmaPageListDisjoint((const void *)(uintptr_t)(UINTPTR_MAX - 1U),
        4, table, sizeof(table))); /* checked pointer extent, no dereference */
    CHECK(!Bc250DmaPageListDisjoint(table, sizeof(table),
        (const void *)(uintptr_t)(UINTPTR_MAX - 1U), 4));
    CHECK(!Bc250DmaPageListDisjoint(table, 0, storage.Words, 8));
    CHECK(!Bc250DmaPageListDisjoint(NULL, 8, storage.Words, 8));
    CHECK(!Bc250DmaPageListDisjoint(table, 8, NULL, 8));
    CHECK(Bc250DmaPageListDisjoint(storage.Words, 8, storage.Words + 1, 8));
    CHECK(!Bc250DmaPageListDisjoint(storage.Words, 16, storage.Words + 1, 8));
    CHECK(Bc250DescribeAddressNumbers(BC250_ADDRESS_DMA_LOGICAL,
        BC250_VM_MAX_ADDRESS - 4095, 4096, BC250_VM_MAX_ADDRESS, &high_page));
    Poison(table, 8);
    CHECK(Bc250DmaPageListEncode(&plan, 7ULL * 4096, 4096, &high_page, 1, 1,
        BC250_VM_MAX_ADDRESS, 15, table, 8));
    CHECK(table[7] == (0x0000FFFFFFFFF000ULL | 0x77ULL));
    CHECK(!Bc250DmaPageListEncode(NULL, 0, 4096, &high_page, 1, 1,
        BC250_VM_MAX_ADDRESS, 3, table, 8));
    CHECK(!Bc250DmaPageListEncode(&plan, 0, 4096, NULL, 1, 1,
        BC250_VM_MAX_ADDRESS, 3, table, 8));
    CHECK(!Bc250DmaPageListEncode(&plan, 0, 4096, &high_page, 1, 1,
        BC250_VM_MAX_ADDRESS, 3, NULL, 8));
}

static void ShuffledListSweep(void)
{
    BC250_GART_GEOMETRY plan;
    BC250_ADDRESS_SPAN pages[16];
    BC250_GART_U64 table[32];
    unsigned int round, i, count;
    CHECK(Bc250GartPlan(32, 32, 4096, 32ULL * 4096, &plan));
    for (round = 0; round < 256; ++round) {
        count = round % 16 + 1;
        for (i = 0; i < count; ++i) {
            BC250_GART_U64 page = ((BC250_GART_U64)((i * 7 + round) % 13) + 1) * 4096;
            CHECK(Bc250DescribeAddressNumbers(BC250_ADDRESS_DMA_LOGICAL,
                page, 4096, BC250_VM_MAX_ADDRESS, &pages[i]));
        }
        Poison(table, 32);
        CHECK(Bc250DmaPageListEncode(&plan, 3ULL * 4096, (BC250_GART_U64)count * 4096,
            pages, count, 16, BC250_VM_MAX_ADDRESS, round % 16, table, 32));
        for (i = 0; i < 32; ++i) {
            if (i >= 3 && i - 3 < count) {
                BC250_GART_U64 flags = 3;
                unsigned int access = round % 16;
                if (access & 1) flags |= 0x20;
                if (access & 2) flags |= 0x40;
                if (access & 4) flags |= 0x10;
                if (access & 8) flags |= 4;
                CHECK(table[i] == (pages[i - 3].Start | flags));
            } else CHECK(table[i] == POISON);
        }
    }
}

int main(void)
{
    BasicAndNegative(); OverlapAndBoundaries(); ShuffledListSweep();
    printf("DMA page-list offline checks: %u, failures: %u\n", checks, failures);
    return failures ? 1 : 0;
}
