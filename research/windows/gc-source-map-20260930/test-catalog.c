/* Portable userspace constant test. No Windows headers or device APIs. */
#include <stdio.h>
#include "gc-source-map-20260930/bc250_gc_source_map.h"

static int check_value(const char *name, unsigned int actual, unsigned int expected)
{
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s\n", name);
        return 0;
    }
    return 1;
}

#define CHECK(name, expected) do { \
    if (!check_value(#name, (name), (expected))) return 1; \
} while (0)

int main(void)
{
    CHECK(BC250_GC_SOURCE_MAP_RUNTIME_VALIDATED, 0U);
    CHECK(BC250_GC_GRBM_STATUS_BYTE_OFFSET, 0x8010U);
    CHECK(BC250_GC_GRBM_GFX_INDEX_BYTE_OFFSET, 0x30800U);
    CHECK(BC250_GC_GRBM_GFX_CNTL_BYTE_OFFSET, 0x8088U);
    CHECK(BC250_GC_SCRATCH_REG0_BYTE_OFFSET, 0x30100U);
    CHECK(BC250_GC_SCRATCH_REG7_BYTE_OFFSET, 0x3011CU);
    CHECK(BC250_GC_CP_ME_CNTL_BYTE_OFFSET, 0x86D8U);
    CHECK(BC250_GC_CP_RB0_BASE_BYTE_OFFSET, 0xC100U);
    CHECK(BC250_GC_SPI_PG_ENABLE_STATIC_WGP_MASK_BYTE_OFFSET, 0x935CU);
    CHECK(BC250_GC_GCVM_CONTEXT0_CNTL_BYTE_OFFSET, 0xA200U);
    CHECK(BC250_GC_SDMA0_GFX_RB_BASE_BYTE_OFFSET, 0x4B84U);
    CHECK(BC250_GC_GFX_INDEX_BROADCAST, 0xE0000000U);
    CHECK(1U << BC250_GC_GRBM_GFX_CNTL__MEID__SHIFT, 4U);
    puts("PASS: 13 compiled constant checks; no GPU access.");
    return 0;
}
