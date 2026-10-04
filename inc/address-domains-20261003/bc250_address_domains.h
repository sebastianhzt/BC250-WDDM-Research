/* Original arithmetic-only research model. No allocation or GPU consumer. */
#ifndef BC250_ADDRESS_DOMAINS_20261003_H
#define BC250_ADDRESS_DOMAINS_20261003_H

typedef unsigned long long BC250_AD_U64;
typedef char BC250_AD_U64_must_be_64_bits[(sizeof(BC250_AD_U64) == 8) ? 1 : -1];

#define BC250_AD_MAX48 0x0000FFFFFFFFFFFFULL
#define BC250_AD_PAGE_BYTES 4096ULL
#define BC250_AD_VRAM_OFFSET 1U
#define BC250_AD_VRAM_MC 2U
/* Linux GPU-view physical namespace, NOT a Windows CPU physical mapping. */
#define BC250_AD_FB_PHYSICAL 3U
#define BC250_AD_CPU_PHYSICAL 4U
#define BC250_AD_DMA_LOGICAL 5U
#define BC250_AD_GPU_VIRTUAL 6U
#define BC250_AD_CPU_VIRTUAL 7U

typedef struct BC250_AD_LAYOUT {
    BC250_AD_U64 McBase;
    BC250_AD_U64 FbPhysicalBase;
    BC250_AD_U64 VramBytes;
} BC250_AD_LAYOUT;

typedef struct BC250_AD_SPAN {
    unsigned int Domain;
    BC250_AD_U64 Start;
    BC250_AD_U64 Bytes;
    BC250_AD_U64 Last;
} BC250_AD_SPAN;

static __inline int Bc250AdLayoutValid(const BC250_AD_LAYOUT *layout)
{
    if (!layout || !layout->VramBytes ||
        (layout->VramBytes & (BC250_AD_PAGE_BYTES - 1ULL)) ||
        (layout->McBase & (BC250_AD_PAGE_BYTES - 1ULL)) ||
        (layout->FbPhysicalBase & (BC250_AD_PAGE_BYTES - 1ULL))) return 0;
    if (layout->McBase > BC250_AD_MAX48 ||
        layout->FbPhysicalBase > BC250_AD_MAX48) return 0;
    if (layout->VramBytes - 1ULL > BC250_AD_MAX48 - layout->McBase ||
        layout->VramBytes - 1ULL > BC250_AD_MAX48 - layout->FbPhysicalBase) return 0;
    return 1;
}

static __inline int Bc250AdLayoutInit(BC250_AD_U64 mc_base,
    BC250_AD_U64 fb_physical_base, BC250_AD_U64 bytes, BC250_AD_LAYOUT *output)
{
    BC250_AD_LAYOUT candidate;
    if (!output) return 0;
    candidate.McBase = mc_base;
    candidate.FbPhysicalBase = fb_physical_base;
    candidate.VramBytes = bytes;
    if (!Bc250AdLayoutValid(&candidate)) return 0;
    *output = candidate;
    return 1;
}

static __inline int Bc250AdDomainBase(const BC250_AD_LAYOUT *layout,
    unsigned int domain, BC250_AD_U64 *base)
{
    if (!layout || !base) return 0;
    switch (domain) {
    case BC250_AD_VRAM_OFFSET: *base = 0ULL; return 1;
    case BC250_AD_VRAM_MC: *base = layout->McBase; return 1;
    case BC250_AD_FB_PHYSICAL: *base = layout->FbPhysicalBase; return 1;
    default: return 0;
    }
}

/* Explicit input/target tags; never guess a domain from the address value.
 * Alignment is a caller constraint (1..4096 power of two), not GPU proof.
 * Failure leaves output untouched. The layout is rechecked on every call.
 */
static __inline int Bc250AdConvert(const BC250_AD_LAYOUT *layout,
    unsigned int input_domain, BC250_AD_U64 address, BC250_AD_U64 bytes,
    BC250_AD_U64 alignment, unsigned int target_domain, BC250_AD_SPAN *output)
{
    BC250_AD_U64 input_base, target_base, offset, start;
    BC250_AD_SPAN candidate;
    if (!output || !Bc250AdLayoutValid(layout) || !bytes || !alignment ||
        alignment > BC250_AD_PAGE_BYTES || (alignment & (alignment - 1ULL))) return 0;
    if (!Bc250AdDomainBase(layout, input_domain, &input_base) ||
        !Bc250AdDomainBase(layout, target_domain, &target_base)) return 0;
    if (address < input_base || (address & (alignment - 1ULL))) return 0;
    offset = address - input_base;
    if (offset >= layout->VramBytes || bytes > layout->VramBytes - offset) return 0;
    /* Layout validation guarantees target_base + offset + bytes - 1 fits 48 bits. */
    start = target_base + offset;
    if (start & (alignment - 1ULL)) return 0;
    candidate.Domain = target_domain;
    candidate.Start = start;
    candidate.Bytes = bytes;
    candidate.Last = start + bytes - 1ULL;
    *output = candidate;
    return 1;
}

#endif
