/* Original RAM-only arithmetic tests; no mapping, ownership or GPU consumer. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "address-domains-20261003/bc250_address_domains.h"

static unsigned long long checks, conversions, successes;
#define CHECK(test) do { ++checks; if (!(test)) { fprintf(stderr, "FAIL %u: %s\n", (unsigned int)__LINE__, #test); exit(1); } } while (0)
#define U64_MAX_VALUE (~0ULL)
#define LIMIT48 (1ULL << 48)

/* Independent exclusive-bound oracle, not a call to any production helper. */
static int LayoutOracle(const BC250_AD_LAYOUT *layout)
{
    if (!layout || layout->VramBytes == 0 || layout->VramBytes > LIMIT48) return 0;
    if (layout->McBase % 4096ULL || layout->FbPhysicalBase % 4096ULL || layout->VramBytes % 4096ULL) return 0;
    return layout->McBase <= LIMIT48 - layout->VramBytes &&
           layout->FbPhysicalBase <= LIMIT48 - layout->VramBytes;
}
static int Oracle(const BC250_AD_LAYOUT *layout, unsigned int input_domain,
    BC250_AD_U64 address, BC250_AD_U64 bytes, BC250_AD_U64 alignment,
    unsigned int target_domain, BC250_AD_SPAN *span)
{
    BC250_AD_U64 bases[3], offset;
    if (!LayoutOracle(layout) || input_domain < 1 || input_domain > 3 || target_domain < 1 || target_domain > 3) return 0;
    if (bytes == 0 || alignment == 0 || alignment > 4096 || (alignment & (alignment - 1ULL))) return 0;
    bases[0] = 0; bases[1] = layout->McBase; bases[2] = layout->FbPhysicalBase;
    if (address < bases[input_domain - 1] || address % alignment != 0) return 0;
    offset = address - bases[input_domain - 1];
    if (offset >= layout->VramBytes || bytes - 1ULL > (layout->VramBytes - 1ULL) - offset) return 0;
    span->Domain = target_domain;
    span->Start = bases[target_domain - 1] + offset;
    if (span->Start % alignment != 0) return 0;
    span->Bytes = bytes;
    span->Last = span->Start + (bytes - 1ULL);
    return 1;
}
static void Convert(const BC250_AD_LAYOUT *layout, unsigned int input_domain,
    BC250_AD_U64 address, BC250_AD_U64 bytes, BC250_AD_U64 alignment,
    unsigned int target_domain, int required)
{
    BC250_AD_SPAN result, expected, back;
    unsigned char saved[sizeof(result)];
    int valid, oracle;
    memset(&result, 0xA5, sizeof(result)); memcpy(saved, &result, sizeof(result));
    memset(&expected, 0, sizeof(expected));
    oracle = Oracle(layout, input_domain, address, bytes, alignment, target_domain, &expected);
    if (required >= 0) CHECK(oracle == required);
    valid = Bc250AdConvert(layout, input_domain, address, bytes, alignment, target_domain, &result);
    ++conversions;
    CHECK(valid == oracle);
    if (!valid) {
        CHECK(memcmp(&result, saved, sizeof(result)) == 0);
        return;
    }
    ++successes;
    CHECK(result.Domain == expected.Domain && result.Start == expected.Start && result.Bytes == expected.Bytes && result.Last == expected.Last);
    CHECK(result.Last <= BC250_AD_MAX48 && result.Last >= result.Start);
    memset(&back, 0xA5, sizeof(back));
    CHECK(Bc250AdConvert(layout, target_domain, result.Start, bytes, alignment, input_domain, &back));
    CHECK(back.Domain == input_domain && back.Start == address && back.Bytes == bytes && back.Last == address + bytes - 1ULL);
}
static void Init(BC250_AD_U64 mc, BC250_AD_U64 physical, BC250_AD_U64 bytes, int required)
{
    BC250_AD_LAYOUT result, candidate;
    unsigned char saved[sizeof(result)];
    int valid;
    memset(&result, 0xA5, sizeof(result)); memcpy(saved, &result, sizeof(result));
    candidate.McBase = mc; candidate.FbPhysicalBase = physical; candidate.VramBytes = bytes;
    CHECK(LayoutOracle(&candidate) == required);
    valid = Bc250AdLayoutInit(mc, physical, bytes, &result);
    CHECK(valid == required);
    if (!valid) CHECK(memcmp(&result, saved, sizeof(result)) == 0);
    else CHECK(result.McBase == mc && result.FbPhysicalBase == physical && result.VramBytes == bytes);
}
static void TestLayouts(void)
{
    BC250_AD_LAYOUT layout, invalid;
    BC250_AD_U64 base, saved;
    unsigned int domain;
    Init(0, 0, 4096, 1);
    Init(0, 0, LIMIT48, 1);
    Init(BC250_AD_MAX48 - 4095ULL, BC250_AD_MAX48 - 4095ULL, 4096, 1);
    Init(BC250_AD_MAX48 - 4095ULL, 0, 8192, 0);
    Init(0, BC250_AD_MAX48 - 4095ULL, 8192, 0);
    Init(0, 0, LIMIT48 + 4096, 0);
    Init(0, 0, 0, 0); Init(0, 0, 1, 0); Init(0, 0, 4095, 0); Init(0, 0, 4097, 0);
    Init(1, 0, 4096, 0); Init(0, 1, 4096, 0);
    Init(U64_MAX_VALUE, 0, 4096, 0); Init(0, U64_MAX_VALUE, 4096, 0);
    Init(U64_MAX_VALUE - 4095ULL, 0, 4096, 0);
    Init(0, 0, U64_MAX_VALUE, 0); Init(0, 0, U64_MAX_VALUE - 4095ULL, 0);
    CHECK(!Bc250AdLayoutInit(0, 0, 4096, NULL)); CHECK(!Bc250AdLayoutValid(NULL));
    CHECK(Bc250AdLayoutInit(0xF400000000ULL, 0x10000000ULL, 0x40000000ULL, &layout));
    CHECK(Bc250AdLayoutValid(&layout));
    for (domain = 0; domain <= 8; ++domain) {
        base = 0xDEAD1234BEEF5678ULL; saved = base;
        if (domain >= 1 && domain <= 3) {
            CHECK(Bc250AdDomainBase(&layout, domain, &base));
            CHECK(base == (domain == 1 ? 0 : (domain == 2 ? layout.McBase : layout.FbPhysicalBase)));
        } else {
            CHECK(!Bc250AdDomainBase(&layout, domain, &base)); CHECK(base == saved);
        }
    }
    base = saved;
    CHECK(!Bc250AdDomainBase(NULL, 1, &base) && base == saved);
    CHECK(!Bc250AdDomainBase(&layout, 1, NULL));
    Convert(NULL, 1, 0, 1, 1, 3, 0);
    CHECK(!Bc250AdConvert(&layout, 1, 0, 1, 1, 3, NULL));
    invalid = layout; invalid.McBase |= 1;
    Convert(&invalid, 1, 0, 1, 1, 3, 0);
    invalid = layout; invalid.FbPhysicalBase = BC250_AD_MAX48;
    Convert(&invalid, 1, 0, 1, 1, 3, 0);
    invalid = layout; invalid.VramBytes = 0;
    Convert(&invalid, 1, 0, 1, 1, 3, 0);
    invalid = layout; invalid.VramBytes = U64_MAX_VALUE;
    Convert(&invalid, 1, 0, 1, 1, 3, 0);
    invalid = layout; invalid.McBase = LIMIT48;
    Convert(&invalid, 1, 0, 1, 1, 3, 0);
    /* Valid mutation changes arithmetic only; it is not fresh ownership evidence. */
    layout.FbPhysicalBase += 4096;
    Convert(&layout, 1, 0, 4096, 4096, 3, 1);
}
static void TestEdges(void)
{
    BC250_AD_LAYOUT layout;
    BC250_AD_SPAN result;
    BC250_AD_U64 bases[3], alignment;
    const BC250_AD_U64 bad_alignments[] = { 0, 3, 4097, 8192, 1ULL << 63, U64_MAX_VALUE };
    unsigned int input, target, index;
    CHECK(Bc250AdLayoutInit(0xF400000000ULL, 0xFFFFF000ULL, 8192, &layout));
    Convert(&layout, 1, 4096, 4096, 4096, 3, 1);
    CHECK(Bc250AdConvert(&layout, 1, 4096, 4096, 4096, 3, &result));
    CHECK(result.Start == 0x100000000ULL && result.Last == 0x100000FFFULL);
    CHECK(Bc250AdLayoutInit(0xF400000000ULL, 0x10000000ULL, 0x40000000ULL, &layout));
    Convert(&layout, 1, 0x18000000ULL, 4096, 4096, 3, 1);
    CHECK(Bc250AdConvert(&layout, 1, 0x18000000ULL, 4096, 4096, 3, &result));
    CHECK(result.Start == 0x28000000ULL && result.Start != (layout.FbPhysicalBase | 0x18000000ULL));
    CHECK(Bc250AdLayoutInit(0x2000, 0x5000, 0x10000, &layout));
    CHECK(Bc250AdConvert(&layout, 1, 0x6000, 1, 1, 3, &result)); CHECK(result.Start == 0xB000);
    CHECK(Bc250AdConvert(&layout, 2, 0x6000, 1, 1, 3, &result)); CHECK(result.Start == 0x9000);
    CHECK(Bc250AdConvert(&layout, 3, 0x6000, 1, 1, 3, &result)); CHECK(result.Start == 0x6000);
    bases[0] = 0; bases[1] = layout.McBase; bases[2] = layout.FbPhysicalBase;
    for (input = 1; input <= 3; ++input) {
        for (target = 1; target <= 3; ++target) {
            BC250_AD_U64 base = bases[input - 1];
            Convert(&layout, input, base, layout.VramBytes, 4096, target, 1);
            Convert(&layout, input, base + layout.VramBytes - 1, 1, 1, target, 1);
            Convert(&layout, input, base + layout.VramBytes - 1, 2, 1, target, 0);
            Convert(&layout, input, base + layout.VramBytes, 1, 1, target, 0);
            Convert(&layout, input, base, 0, 1, target, 0);
            Convert(&layout, input, base, U64_MAX_VALUE, 1, target, 0);
            Convert(&layout, input, U64_MAX_VALUE, 1, 1, target, 0);
            if (base) Convert(&layout, input, base - 1, 1, 1, target, 0);
            for (alignment = 1; alignment <= 4096; alignment *= 2)
                Convert(&layout, input, base, 17, alignment, target, 1);
            for (index = 0; index < sizeof(bad_alignments) / sizeof(bad_alignments[0]); ++index)
                Convert(&layout, input, base, 1, bad_alignments[index], target, 0);
            Convert(&layout, input, base + 1, 1, 2, target, 0);
        }
    }
    for (input = 0; input <= 8; ++input) {
        if (input >= 1 && input <= 3) continue;
        /* Identical small numeric addresses do not authorize another namespace. */
        Convert(&layout, input, 0x6000, 1, 1, 3, 0);
        Convert(&layout, 1, 0x6000, 1, 1, input, 0);
    }
    Convert(&layout, 0xFFFFFFFFU, 0x6000, 1, 1, 3, 0);
    Convert(&layout, 1, 0x6000, 1, 1, 0xFFFFFFFFU, 0);
    CHECK(Bc250AdLayoutInit(0, 0, LIMIT48, &layout));
    for (input = 1; input <= 3; ++input) for (target = 1; target <= 3; ++target) {
        Convert(&layout, input, 0, LIMIT48, 4096, target, 1);
        Convert(&layout, input, BC250_AD_MAX48, 1, 1, target, 1);
        Convert(&layout, input, BC250_AD_MAX48, 2, 1, target, 0);
        Convert(&layout, input, LIMIT48, 1, 1, target, 0);
        Convert(&layout, input, BC250_AD_MAX48 - 4095, 4096, 4096, target, 1);
        Convert(&layout, input, BC250_AD_MAX48, 1, 4096, target, 0);
    }
    CHECK(Bc250AdLayoutInit(BC250_AD_MAX48 - 4095, 0, 4096, &layout));
    Convert(&layout, 2, BC250_AD_MAX48, 1, 1, 3, 1);
    Convert(&layout, 2, BC250_AD_MAX48, 2, 1, 3, 0);
    CHECK(Bc250AdLayoutInit(0, BC250_AD_MAX48 - 4095, 4096, &layout));
    Convert(&layout, 1, 4095, 1, 1, 3, 1);
    Convert(&layout, 1, 4095, 2, 1, 3, 0);
}
static void TestExhaustiveSmallSpans(void)
{
    BC250_AD_LAYOUT layout;
    BC250_AD_U64 bases[3], alignment;
    unsigned int mc_page, fb_page, pages, input, target, offset, bytes;
    /* Exhaustive specified subspace: bases0..2pages, lengths1..2pages,
     * offsets0..32, spanbytes0..16, align1/2/4/8/16/32, all3x3domainpairs. */
    for (mc_page = 0; mc_page <= 2; ++mc_page) for (fb_page = 0; fb_page <= 2; ++fb_page)
    for (pages = 1; pages <= 2; ++pages) {
        CHECK(Bc250AdLayoutInit((BC250_AD_U64)mc_page * 4096, (BC250_AD_U64)fb_page * 4096, (BC250_AD_U64)pages * 4096, &layout));
        bases[0] = 0; bases[1] = layout.McBase; bases[2] = layout.FbPhysicalBase;
        for (input = 1; input <= 3; ++input) for (target = 1; target <= 3; ++target)
        for (offset = 0; offset <= 32; ++offset) for (bytes = 0; bytes <= 16; ++bytes)
        for (alignment = 1; alignment <= 32; alignment *= 2)
            Convert(&layout, input, bases[input - 1] + offset, bytes, alignment, target, -1);
    }
}
static BC250_AD_U64 Random(BC250_AD_U64 *state)
{
    *state ^= *state << 13; *state ^= *state >> 7; *state ^= *state << 17;
    return *state;
}
static void TestDeterministic(void)
{
    BC250_AD_LAYOUT layout;
    BC250_AD_U64 state = 0x7DBBC25031415926ULL, bases[3], address, bytes, alignment, mc, physical, size;
    unsigned int index, input, target;
    unsigned long long before = successes;
    for (index = 0; index < 20000; ++index) {
        mc = (Random(&state) % 1024) * 4096;
        physical = (Random(&state) % 1024) * 4096;
        size = (Random(&state) % 64 + 1) * 4096;
        CHECK(Bc250AdLayoutInit(mc, physical, size, &layout));
        bases[0] = 0; bases[1] = layout.McBase; bases[2] = layout.FbPhysicalBase;
        input = (unsigned int)(Random(&state) % 7 + 1);
        target = (unsigned int)(Random(&state) % 7 + 1);
        alignment = 1ULL << (Random(&state) % 14);
        address = (input <= 3 ? bases[input - 1] : 0) + (Random(&state) % (layout.VramBytes + 4096));
        address &= ~(alignment - 1ULL);
        bytes = Random(&state) % (layout.VramBytes + 1);
        Convert(&layout, input, address, bytes, alignment, target, -1);
    }
    CHECK(successes > before + 100);
}
int main(void)
{
    TestLayouts(); TestEdges(); TestExhaustiveSmallSpans(); TestDeterministic();
    printf("PASS: %llu address-domain RAM assertions, %llu conversions/%llu successes; no mapping/ownership/GPU proof\n", checks, conversions, successes);
    return 0;
}
