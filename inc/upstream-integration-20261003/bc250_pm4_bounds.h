/* SPDX-License-Identifier: Apache-2.0
 * Original arithmetic hardening motivated by Keshas f8cd73a.
 * Bounds only: no free-space, ownership, locking, DMA or GPU validation.
 */
#ifndef BC250_PM4_BOUNDS_H
#define BC250_PM4_BOUNDS_H
typedef struct BC250_PM4_PLACEMENT {
    unsigned int PacketBytes;
    unsigned int PaddingBytes;
    unsigned int Start;
    unsigned int Next;
} BC250_PM4_PLACEMENT;

static __inline int Bc250Pm4Plan(unsigned long long ring_bytes,
    unsigned int write_pointer, unsigned int payload_dwords,
    BC250_PM4_PLACEMENT *out)
{
    BC250_PM4_PLACEMENT plan;
    unsigned int size;
    if (!out || ring_bytes > 0xFFFFFFFFULL || ring_bytes < 8ULL ||
        (ring_bytes & 3ULL) || (write_pointer & 3U) ||
        write_pointer > ring_bytes || !payload_dwords ||
        payload_dwords > 16384U) return 0;
    size = (unsigned int)ring_bytes;
    /* Header encodes payload_count-1 in 14 bits; arithmetic is now bounded. */
    plan.PacketBytes = (payload_dwords + 1U) * 4U;
    if (plan.PacketBytes > size) return 0;
    plan.PaddingBytes = plan.PacketBytes > size - write_pointer ?
                        size - write_pointer : 0U;
    plan.Start = plan.PacketBytes > size - write_pointer ? 0U : write_pointer;
    plan.Next = plan.Start + plan.PacketBytes;
    *out = plan;
    return 1;
}
#endif
