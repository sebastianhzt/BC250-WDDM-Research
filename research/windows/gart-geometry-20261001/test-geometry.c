/* SPDX-License-Identifier: Apache-2.0 - Pure arithmetic; no device APIs. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gart-geometry-20261001/bc250_gart_geometry.h"

static unsigned int checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static void test_geometry(void)
{
    BC250_GART_GEOMETRY plan, bad, sentinel;
    BC250_GART_SLOTS slots, untouched;
    unsigned int n;
    memset(&sentinel, 0xA5, sizeof(sentinel));
    plan = sentinel;
    CHECK(!Bc250GartPlan(0U, 16384U, 131072U, 0ULL, &plan));
    CHECK(memcmp(&plan, &sentinel, sizeof(plan)) == 0);
    CHECK(!Bc250GartPlan(16384U, 16383U, 131072U, 67108864ULL, &plan));
    CHECK(!Bc250GartPlan(16384U, 16384U, 131071U, 67108864ULL, &plan));
    CHECK(!Bc250GartPlan(16384U, 16384U, 131072U, 134217728ULL, &plan));
    CHECK(!Bc250GartPlan(16384U, 16384U, 131072U, BC250_GART_U64_MAX, &plan));
    CHECK(memcmp(&plan, &sentinel, sizeof(plan)) == 0);
    CHECK(!Bc250GartPlan(1U, 1U, 4096U, 4096ULL, NULL));
    CHECK(!Bc250GartGeometryConsistent(NULL));
    CHECK(Bc250GartPlan(16384U, 16384U, 131072U, 67108864ULL, &plan));
    CHECK(plan.TableBytes == 131072U && plan.AllocationBytes == 131072U);
    CHECK(plan.Entries == 16384U && plan.BitmapWords == 512U);
    CHECK(plan.CoverageBytes == 67108864ULL);
    CHECK(Bc250GartDescribeSlots(&plan, 0ULL, plan.CoverageBytes, &slots));
    CHECK(slots.First == 0U && slots.Count == 16384U && slots.TableByteCount == 131072U);
    CHECK(Bc250GartDescribeSlots(&plan, 67104768ULL, 4096ULL, &slots));
    CHECK(slots.First == 16383U && slots.Count == 1U && slots.TableByteOffset == 131064U);
    memset(&untouched, 0xA5, sizeof(untouched)); slots = untouched;
    CHECK(!Bc250GartDescribeSlots(&plan, 67108864ULL, 4096ULL, &slots));
    CHECK(!Bc250GartDescribeSlots(&plan, 67104768ULL, 8192ULL, &slots));
    CHECK(!Bc250GartDescribeSlots(&plan, 0ULL, 0ULL, &slots));
    CHECK(!Bc250GartDescribeSlots(&plan, 1ULL, 4096ULL, &slots));
    CHECK(!Bc250GartDescribeSlots(&plan, 0ULL, 4095ULL, &slots));
    CHECK(!Bc250GartDescribeSlots(&plan, BC250_GART_U64_MAX, 4096ULL, &slots));
    CHECK(!Bc250GartDescribeSlots(&plan, 0ULL, BC250_GART_U64_MAX, &slots));
    CHECK(!Bc250GartDescribeSlots(NULL, 0ULL, 4096ULL, &slots));
    CHECK(!Bc250GartDescribeSlots(&plan, 0ULL, 4096ULL, NULL));
    CHECK(memcmp(&slots, &untouched, sizeof(slots)) == 0);
    for (n = 0U; n < 5U; ++n) {
        bad = plan;
        if (n == 0U) ++bad.Entries;
        if (n == 1U) ++bad.TableBytes;
        if (n == 2U) ++bad.AllocationBytes;
        if (n == 3U) ++bad.BitmapWords;
        if (n == 4U) ++bad.CoverageBytes;
        CHECK(!Bc250GartGeometryConsistent(&bad));
        CHECK(!Bc250GartDescribeSlots(&bad, 0ULL, 4096ULL, &slots));
        CHECK(memcmp(&slots, &untouched, sizeof(slots)) == 0);
    }
    /* Independent division-based oracle for every table rounding/bitmap edge. */
    for (n = 1U; n <= 8192U; ++n) {
        unsigned int table = n * 8U;
        unsigned int rounded = ((table - 1U) / 4096U + 1U) * 4096U;
        CHECK(Bc250GartPlan(n, 8192U, 65536U, (BC250_GART_U64)n * 4096ULL, &plan));
        CHECK(plan.TableBytes == table && plan.AllocationBytes == rounded);
        CHECK(plan.BitmapWords == (n - 1U) / 32U + 1U);
        CHECK(Bc250GartGeometryConsistent(&plan));
        CHECK(Bc250GartDescribeSlots(&plan, (BC250_GART_U64)(n - 1U) * 4096ULL, 4096ULL, &slots));
        CHECK(slots.First == n - 1U && slots.TableByteOffset == table - 8U);
    }
    CHECK(Bc250GartPlan(536870400U, 536870400U, 0xFFFFFFFFU, 2199021158400ULL, &plan));
    CHECK(plan.AllocationBytes == 0xFFFFF000U);
    CHECK(!Bc250GartPlan(536870401U, 536870401U, 0xFFFFFFFFU, 2199021162496ULL, &plan));
    CHECK(!Bc250GartPlan(0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU, 17592186040320ULL, &plan));
    /* Linux-like 512 MiB coverage needs a 1 MiB table and a larger bitmap.
     * An arithmetic example, NOT permission to reuse the Linux BO or activate it.
     */
    CHECK(!Bc250GartPlan(131072U, 16384U, 1048576U, 536870912ULL, &plan));
    CHECK(Bc250GartPlan(131072U, 131072U, 1048576U, 536870912ULL, &plan));
    CHECK(plan.TableBytes == 1048576U && plan.BitmapWords == 4096U);
}

static void test_domains(void)
{
    BC250_ADDRESS_SPAN span, sentinel, bad;
    unsigned int domain, other;
    memset(&sentinel, 0xA5, sizeof(sentinel)); span = sentinel;
    CHECK(!Bc250DescribeAddressNumbers(0U, 0ULL, 4096ULL, BC250_GART_U64_MAX, &span));
    CHECK(!Bc250DescribeAddressNumbers(6U, 0ULL, 4096ULL, BC250_GART_U64_MAX, &span));
    CHECK(!Bc250DescribeAddressNumbers(1U, 0ULL, 0ULL, BC250_GART_U64_MAX, &span));
    CHECK(!Bc250DescribeAddressNumbers(1U, 0ULL, 1ULL, BC250_GART_U64_MAX, NULL));
    CHECK(!Bc250DescribeAddressNumbers(1U, BC250_GART_U64_MAX, 2ULL, BC250_GART_U64_MAX, &span));
    CHECK(!Bc250DescribeAddressNumbers(1U, 0x100000000ULL, 1ULL, 0xFFFFFFFFULL, &span));
    CHECK(!Bc250DescribeAddressNumbers(1U, 0xFFFFF000ULL, 4097ULL, 0xFFFFFFFFULL, &span));
    CHECK(memcmp(&span, &sentinel, sizeof(span)) == 0);
    for (domain = 1U; domain <= 5U; ++domain) {
        CHECK(Bc250DescribeAddressNumbers(domain, 0x100000ULL, 4096ULL, BC250_GART_U64_MAX, &span));
        CHECK(span.Start == 0x100000ULL && span.Last == 0x100FFFULL);
        for (other = 1U; other <= 5U; ++other)
            CHECK(Bc250AddressNumbersMatchDomain(&span, other, BC250_GART_U64_MAX) == (domain == other));
        bad = span; ++bad.Last;
        CHECK(!Bc250AddressNumbersMatchDomain(&bad, domain, BC250_GART_U64_MAX));
        bad = span; bad.Bytes = BC250_GART_U64_MAX;
        CHECK(!Bc250AddressNumbersMatchDomain(&bad, domain, BC250_GART_U64_MAX));
        CHECK(!Bc250AddressNumbersMatchDomain(&span, domain, 0x100FFEULL));
    }
    CHECK(Bc250DescribeAddressNumbers(3U, 0xFFFFF000ULL, 4096ULL, 0xFFFFFFFFULL, &span));
    CHECK(span.Last == 0xFFFFFFFFULL);
    CHECK(Bc250DescribeAddressNumbers(1U, BC250_GART_U64_MAX, 1ULL, BC250_GART_U64_MAX, &span));
    CHECK(span.Last == BC250_GART_U64_MAX);
    CHECK(Bc250AddressNumbersMatchDomain(&span, 1U, BC250_GART_U64_MAX));
    CHECK(!Bc250AddressNumbersMatchDomain(NULL, 1U, BC250_GART_U64_MAX));
}

int main(void)
{
    test_geometry(); test_domains();
    printf("PASS: %u arithmetic/domain assertions; NO allocation, DMA, GPU access or PTE encoding.\n", checks);
    return 0;
}
