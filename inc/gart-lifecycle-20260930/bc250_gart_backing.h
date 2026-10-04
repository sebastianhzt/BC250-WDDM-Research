/* SPDX-License-Identifier: Apache-2.0
 * CPU backing ownership only. No DMA address, GPU mapping or quiescence proof.
 * Callers MUST serialize all operations externally; state is not a lock.
 * The supplied allocator/free pair must match and remain valid throughout.
 * Tests supply RAM fakes; the kernel adapter supplies the contiguous-memory pair.
 */
#ifndef BC250_GART_BACKING_H
#define BC250_GART_BACKING_H

/* Deliberately independent of the numeric register-map permission. */
#define BC250_GART_CPU_BACKING_RUNTIME_ENABLED 0U

#define BC250_GART_EMPTY 0U
#define BC250_GART_ALLOCATING 1U
#define BC250_GART_CPU_ONLY 2U
#define BC250_GART_GPU_MAY_ACCESS 3U
#define BC250_GART_RELEASING 4U

typedef struct BC250_GART_BACKING {
    void *Address;
    unsigned int Bytes;
    unsigned int State;
} BC250_GART_BACKING;

typedef void *(*BC250_GART_ALLOCATE)(void *context, unsigned int bytes);
typedef void (*BC250_GART_FREE)(void *context, void *address, unsigned int bytes);

static __inline int Bc250GartBackingEmpty(const BC250_GART_BACKING *backing)
{
    return backing && backing->State == BC250_GART_EMPTY &&
           !backing->Address && !backing->Bytes;
}

static __inline int Bc250GartBackingReserve(
    BC250_GART_BACKING *backing, unsigned int bytes,
    BC250_GART_ALLOCATE allocate, void *context)
{
    void *address;
    if (!Bc250GartBackingEmpty(backing) || !bytes || (bytes & 4095U) || !allocate)
        return 0;
    backing->State = BC250_GART_ALLOCATING;
    backing->Bytes = bytes;
    address = allocate(context, bytes);
    if (!address) {
        backing->Bytes = 0U;
        backing->State = BC250_GART_EMPTY;
        return 0;
    }
    backing->Address = address;
    backing->State = BC250_GART_CPU_ONLY;
    return 1;
}

/* BEFORE any possible GPU exposure. This is a one-way quarantine here;
 * no boolean from a caller is accepted as evidence of hardware quiescence.
 * Active backing cannot be released until a future validated protocol exists.
 */
static __inline int Bc250GartBackingMarkGpuMayAccess(BC250_GART_BACKING *backing)
{
    if (!backing || backing->State != BC250_GART_CPU_ONLY || !backing->Address ||
        !backing->Bytes || (backing->Bytes & 4095U)) return 0;
    backing->State = BC250_GART_GPU_MAY_ACCESS;
    return 1;
}

static __inline int Bc250GartBackingRelease(
    BC250_GART_BACKING *backing, BC250_GART_FREE release, void *context)
{
    if (Bc250GartBackingEmpty(backing)) return 1; /* repeated cleanup is a no-op */
    if (!backing || backing->State != BC250_GART_CPU_ONLY || !backing->Address ||
        !backing->Bytes || (backing->Bytes & 4095U) || !release) return 0;
    backing->State = BC250_GART_RELEASING;
    release(context, backing->Address, backing->Bytes);
    backing->Address = 0;
    backing->Bytes = 0U;
    backing->State = BC250_GART_EMPTY;
    return 1;
}

#endif
