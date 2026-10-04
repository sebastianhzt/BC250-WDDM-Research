/* SPDX-License-Identifier: Apache-2.0
 * RAM-only arithmetic/codec and fake sparse tree. No Windows APIs/GPU access.
 * Tree links are CPU pointers, NOT encoded hardware PDEs. Rollback test scaffold
 * is not compiled into the driver and does not prove hardware transaction safety.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vm-source-model-20261001/bc250_vm_source_model.h"
static unsigned int checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static void test_indices(void)
{
    BC250_VM_LAYOUT layout, sentinel, bad;
    BC250_VM_INDICES indices, next, untouched;
    BC250_GART_U64 va, rebuilt;
    unsigned int level, root, n;
    memset(&sentinel, 0xA5, sizeof(sentinel)); layout = sentinel;
    CHECK(!Bc250VmPlanLayout(0U, 9U, 1ULL << 36, &layout));
    CHECK(!Bc250VmPlanLayout(4U, 9U, 512ULL, &layout));
    CHECK(!Bc250VmPlanLayout(1U, 10U, 1ULL << 36, &layout));
    CHECK(!Bc250VmPlanLayout(1U, 9U, 0ULL, &layout));
    CHECK(!Bc250VmPlanLayout(1U, 9U, (1ULL << 36) + 1ULL, &layout));
    CHECK(!Bc250VmPlanLayout(3U, 9U, (1ULL << 18) + 1ULL, &layout));
    CHECK(!Bc250VmPlanLayout(1U, 9U, 1ULL << 36, NULL));
    CHECK(memcmp(&layout, &sentinel, sizeof(layout)) == 0);
    CHECK(Bc250VmPfnShift(5U) == 0xFFFFFFFFU);
    memset(&untouched, 0xA5, sizeof(untouched)); indices = untouched;
    CHECK(!Bc250VmDecode(NULL, 0ULL, &indices));
    CHECK(Bc250VmPlanLayout(1U, 9U, 1ULL << 36, &layout));
    CHECK(!Bc250VmDecode(&layout, 1ULL << 48, &indices));
    CHECK(!Bc250VmDecode(&layout, ~0ULL, &indices));
    CHECK(!Bc250VmDecode(&layout, 0ULL, NULL));
    bad = layout; ++bad.RootEntries;
    CHECK(!Bc250VmDecode(&bad, 0ULL, &indices));
    CHECK(memcmp(&indices, &untouched, sizeof(indices)) == 0);
    for (root = 1U; root <= 3U; ++root) {
        CHECK(Bc250VmPlanLayout(root, 9U, 1ULL << (9U * (5U - root)), &layout));
        for (n = 0; n < 8192U; ++n) {
            va = (BC250_GART_U64)n * 4096ULL;
            CHECK(Bc250VmDecode(&layout, va, &indices));
            /* Independent division oracle: byte address, not PFN shift formula. */
            for (level = root; level <= 4U; ++level) {
                unsigned int byte_shift = 12U + 9U * (4U - level);
                CHECK(indices.Index[level] == (unsigned int)((va / (1ULL << byte_shift)) % 512ULL));
            }
            CHECK(Bc250VmDecode(&layout, va + 4096ULL, &next));
            CHECK(memcmp(indices.Index, next.Index, sizeof(indices.Index)) != 0);
        }
        va = (layout.MaxPfn << 12) - 1ULL;
        CHECK(Bc250VmDecode(&layout, va, &indices)); rebuilt = indices.Offset;
        for (level = root; level <= 4U; ++level)
            rebuilt |= (BC250_GART_U64)indices.Index[level] << (12U + 9U * (4U - level));
        CHECK(rebuilt == va && indices.Offset == 4095U);
    }
    CHECK(Bc250VmPlanLayout(1U, 9U, (1ULL << 27) + 1ULL, &layout));
    CHECK(layout.RootEntries == 2U);
    CHECK(Bc250VmDecode(&layout, 1ULL << 39, &indices));
    CHECK(indices.Index[1] == 1U && indices.Index[4] == 0U);
    /* Full 48-bit layout: rollovers at every directory boundary. */
    CHECK(Bc250VmPlanLayout(1U, 9U, 1ULL << 36, &layout));
    for (level = 1U; level <= 3U; ++level) {
        va = 1ULL << (12U + 9U * (4U - level));
        CHECK(Bc250VmDecode(&layout, va - 4096ULL, &indices));
        CHECK(Bc250VmDecode(&layout, va, &next));
        CHECK(indices.Index[level] == 0U && next.Index[level] == 1U);
        CHECK(indices.Index[4] == 511U && next.Index[4] == 0U);
    }
}

static void test_codec(void)
{
    BC250_ADDRESS_SPAN dma, bad;
    BC250_GART_U64 value, expected, sentinel = 0xA5A5A5A5A5A5A5A5ULL;
    unsigned int access, domain, level;
    CHECK(Bc250DescribeAddressNumbers(3U, 0x100000ULL, 4096ULL, (1ULL << 44) - 1ULL, &dma));
    for (access = 0U; access < 16U; ++access) {
        CHECK(Bc250VmEncodeSystemLeaf(&dma, (1ULL << 44) - 1ULL, access, &value));
        expected = 0x100003ULL;
        if (access & 1U) expected |= 0x20ULL;
        if (access & 2U) expected |= 0x40ULL;
        if (access & 4U) expected |= 0x10ULL;
        if (access & 8U) expected |= 0x4ULL;
        CHECK(value == expected);
        CHECK(!(value & ((1ULL << 3) | (31ULL << 7) | (7ULL << 48))));
    }
    value = sentinel;
    CHECK(!Bc250VmEncodeSystemLeaf(&dma, (1ULL << 44) - 1ULL, 16U, &value));
    CHECK(!Bc250VmEncodeSystemLeaf(&dma, 0x100FFEULL, 0U, &value));
    CHECK(!Bc250VmEncodeSystemLeaf(&dma, ~0ULL, 0U, &value));
    CHECK(!Bc250VmEncodeSystemLeaf(NULL, (1ULL << 44) - 1ULL, 0U, &value));
    for (domain = 1U; domain <= 5U; ++domain) {
        if (domain == 3U) continue;
        bad = dma; bad.Domain = domain;
        CHECK(!Bc250VmEncodeSystemLeaf(&bad, (1ULL << 44) - 1ULL, 0U, &value));
    }
    bad = dma; ++bad.Start; ++bad.Last;
    CHECK(!Bc250VmEncodeSystemLeaf(&bad, (1ULL << 44) - 1ULL, 0U, &value));
    bad = dma; --bad.Bytes; --bad.Last;
    CHECK(!Bc250VmEncodeSystemLeaf(&bad, (1ULL << 44) - 1ULL, 0U, &value));
    CHECK(Bc250DescribeAddressNumbers(3U, 1ULL << 48, 4096ULL, ~0ULL, &bad));
    CHECK(!Bc250VmEncodeSystemLeaf(&bad, BC250_VM_MAX_ADDRESS, 0U, &value));
    CHECK(value == sentinel);
    for (level = 1U; level <= 3U; ++level) {
        CHECK(Bc250VmEncodeSystemDirectory(&dma, (1ULL << 44) - 1ULL, level, 0U, &value));
        CHECK(value == 0x100003ULL);
        value = sentinel;
        CHECK(!Bc250VmEncodeSystemDirectory(&dma, (1ULL << 44) - 1ULL, level, 1U, &value));
        CHECK(value == sentinel);
    }
    CHECK(!Bc250VmEncodeSystemDirectory(&dma, (1ULL << 44) - 1ULL, 4U, 0U, &value));
    CHECK(!Bc250VmEncodeSystemDirectory(&dma, (1ULL << 44) - 1ULL, 0U, 0U, &value));
}

typedef union NODE NODE;
union NODE { NODE *Child[512]; BC250_GART_U64 Leaf[512]; };
static unsigned int live_nodes, allocation_attempts, fail_allocation, fail_write;
typedef struct { NODE *Parent; unsigned int Slot; NODE *Owned; } NEW_LINK;
typedef struct { BC250_GART_U64 *Slot; BC250_GART_U64 Old; } OLD_LEAF;

/* Fake two-page transaction, with a small owned-node/write journal. */
static int fake_map_two(NODE *root, BC250_GART_U64 first_va, BC250_GART_U64 value)
{
    BC250_VM_LAYOUT layout;
    NEW_LINK links[6]; OLD_LEAF leaves[2];
    unsigned int nlink = 0U, nleaf = 0U, page, level;
    if ((first_va & 4095ULL) || first_va > BC250_VM_MAX_ADDRESS - 8191ULL) return 0;
    CHECK(Bc250VmPlanLayout(1U, 9U, 1ULL << 36, &layout));
    for (page = 0U; page < 2U; ++page) {
        BC250_VM_INDICES index; NODE *node = root;
        if (!Bc250VmDecode(&layout, first_va + (BC250_GART_U64)page * 4096ULL, &index)) goto rollback;
        for (level = 1U; level <= 3U; ++level) {
            unsigned int slot = index.Index[level];
            if (!node->Child[slot]) {
                NODE *created;
                ++allocation_attempts;
                if (allocation_attempts == fail_allocation) goto rollback;
                created = calloc(1U, sizeof(*created)); CHECK(created != NULL);
                CHECK(nlink < 6U); ++live_nodes;
                links[nlink].Parent = node; links[nlink].Slot = slot; links[nlink].Owned = created; ++nlink;
                node->Child[slot] = created;
            }
            node = node->Child[slot];
        }
        leaves[nleaf].Slot = &node->Leaf[index.Index[4]];
        leaves[nleaf].Old = *leaves[nleaf].Slot; ++nleaf;
        *leaves[nleaf - 1U].Slot = value + (BC250_GART_U64)page * 4096ULL;
        /* Failure AFTER write exercises actual restoration, not only prechecks. */
        if (page + 1U == fail_write) goto rollback;
    }
    return 1;
rollback:
    while (nleaf) { --nleaf; *leaves[nleaf].Slot = leaves[nleaf].Old; }
    while (nlink) {
        --nlink; links[nlink].Parent->Child[links[nlink].Slot] = NULL;
        free(links[nlink].Owned); CHECK(live_nodes != 0U); --live_nodes;
    }
    return 0;
}

static void destroy_fake(NODE *node, unsigned int level)
{
    unsigned int i;
    if (level <= 3U) for (i = 0U; i < 512U; ++i) if (node->Child[i]) {
        destroy_fake(node->Child[i], level + 1U); free(node->Child[i]); --live_nodes;
    }
}
static BC250_GART_U64 fake_read(NODE *root, BC250_GART_U64 va)
{
    BC250_VM_LAYOUT layout; BC250_VM_INDICES index; unsigned int level;
    CHECK(Bc250VmPlanLayout(1U, 9U, 1ULL << 36, &layout));
    CHECK(Bc250VmDecode(&layout, va, &index));
    for (level = 1U; level <= 3U; ++level) {
        if (!root->Child[index.Index[level]]) return 0ULL;
        root = root->Child[index.Index[level]];
    }
    return root->Leaf[index.Index[4]];
}
static void test_fake_rollback(void)
{
    NODE root = {0}; unsigned int n, before;
    CHECK(!fake_map_two(&root, ~0ULL, 0x100003ULL));
    CHECK(!fake_map_two(&root, 1ULL, 0x100003ULL));
    for (n = 1U; n <= 4U; ++n) {
        allocation_attempts = 0U; fail_allocation = n;
        CHECK(!fake_map_two(&root, 0x1FF000ULL, 0x100003ULL));
        CHECK(live_nodes == 0U);
        CHECK(fake_read(&root, 0x1FF000ULL) == 0ULL && fake_read(&root, 0x200000ULL) == 0ULL);
    }
    fail_allocation = 0U;
    CHECK(fake_map_two(&root, 0x1FF000ULL, 0x100003ULL)); before = live_nodes;
    CHECK(fake_read(&root, 0x1FF000ULL) == 0x100003ULL);
    CHECK(fake_read(&root, 0x200000ULL) == 0x101003ULL);
    for (n = 1U; n <= 2U; ++n) {
        fail_write = n;
        CHECK(!fake_map_two(&root, 0x1FF000ULL, 0x200003ULL));
        CHECK(live_nodes == before);
        CHECK(fake_read(&root, 0x1FF000ULL) == 0x100003ULL);
        CHECK(fake_read(&root, 0x200000ULL) == 0x101003ULL);
    }
    fail_write = 2U;
    CHECK(!fake_map_two(&root, 0x40000000ULL, 0x300003ULL));
    CHECK(live_nodes == before && fake_read(&root, 0x40000000ULL) == 0ULL);
    CHECK(fake_read(&root, 0x1FF000ULL) == 0x100003ULL);
    fail_write = 0U; destroy_fake(&root, 1U); CHECK(live_nodes == 0U);
}

int main(void)
{
    test_indices(); test_codec(); test_fake_rollback();
    printf("PASS: %u assertions; source VM subset + RAM-only rollback; NOT hardware/DMA/mapper PASS.\n", checks);
    return 0;
}
