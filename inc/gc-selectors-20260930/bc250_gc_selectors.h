/* Pure source-field encoders. No MMIO, hardware backend, locking or GPU topology
 * validation. The output is a selector plan, NOT permission to execute it. */
#ifndef BC250_GC_SELECTORS_H
#define BC250_GC_SELECTORS_H
#include "../gc-source-map-20260930/bc250_gc_source_map.h"

typedef enum BC250_GC_SELECTOR_KIND {
    BC250_GC_SELECTOR_SE_SH = 1,
    BC250_GC_SELECTOR_QUEUE = 2
} BC250_GC_SELECTOR_KIND;

typedef struct BC250_GC_SELECTOR {
    BC250_GC_SELECTOR_KIND Kind;
    unsigned int ByteOffset;
    unsigned int Value;
} BC250_GC_SELECTOR;

#define BC250_GC_BROADCAST_INSTANCE 1U
#define BC250_GC_BROADCAST_SA 2U
#define BC250_GC_BROADCAST_SE 4U
#define BC250_GC_QUEUE_ME1_PIPE0_Q0_VM0 (1U << BC250_GC_GRBM_GFX_CNTL__MEID__SHIFT)
#define BC250_GC_QUEUE_ME0_PIPE0_Q0_VM0 0U

static __inline int Bc250GcBuildSeShSelector(
    unsigned int se, unsigned int sa, unsigned int instance,
    unsigned int broadcast, BC250_GC_SELECTOR *out)
{
    BC250_GC_SELECTOR plan;
    if (!out || (broadcast & ~7U) != 0U ||
        se > (BC250_GC_GRBM_GFX_INDEX__SE_INDEX_MASK >> BC250_GC_GRBM_GFX_INDEX__SE_INDEX__SHIFT) ||
        sa > (BC250_GC_GRBM_GFX_INDEX__SA_INDEX_MASK >> BC250_GC_GRBM_GFX_INDEX__SA_INDEX__SHIFT) ||
        instance > (BC250_GC_GRBM_GFX_INDEX__INSTANCE_INDEX_MASK >> BC250_GC_GRBM_GFX_INDEX__INSTANCE_INDEX__SHIFT))
        return 0;
    /* Broadcast fields must have zero indices: no silently ignored inputs. */
    if (((broadcast & BC250_GC_BROADCAST_SE) && se) ||
        ((broadcast & BC250_GC_BROADCAST_SA) && sa) ||
        ((broadcast & BC250_GC_BROADCAST_INSTANCE) && instance))
        return 0;
    plan.Kind = BC250_GC_SELECTOR_SE_SH;
    plan.ByteOffset = BC250_GC_GRBM_GFX_INDEX_BYTE_OFFSET;
    plan.Value = (se << BC250_GC_GRBM_GFX_INDEX__SE_INDEX__SHIFT) |
                 (sa << BC250_GC_GRBM_GFX_INDEX__SA_INDEX__SHIFT) |
                 (instance << BC250_GC_GRBM_GFX_INDEX__INSTANCE_INDEX__SHIFT);
    if (broadcast & BC250_GC_BROADCAST_INSTANCE)
        plan.Value |= BC250_GC_GRBM_GFX_INDEX__INSTANCE_BROADCAST_WRITES_MASK;
    if (broadcast & BC250_GC_BROADCAST_SA)
        plan.Value |= BC250_GC_GRBM_GFX_INDEX__SA_BROADCAST_WRITES_MASK;
    if (broadcast & BC250_GC_BROADCAST_SE)
        plan.Value |= BC250_GC_GRBM_GFX_INDEX__SE_BROADCAST_WRITES_MASK;
    *out = plan;
    return 1;
}

static __inline int Bc250GcBuildQueueSelector(
    unsigned int me, unsigned int pipe, unsigned int queue,
    unsigned int vmid, BC250_GC_SELECTOR *out)
{
    BC250_GC_SELECTOR plan;
    if (!out ||
        me > (BC250_GC_GRBM_GFX_CNTL__MEID_MASK >> BC250_GC_GRBM_GFX_CNTL__MEID__SHIFT) ||
        pipe > (BC250_GC_GRBM_GFX_CNTL__PIPEID_MASK >> BC250_GC_GRBM_GFX_CNTL__PIPEID__SHIFT) ||
        queue > (BC250_GC_GRBM_GFX_CNTL__QUEUEID_MASK >> BC250_GC_GRBM_GFX_CNTL__QUEUEID__SHIFT) ||
        vmid > (BC250_GC_GRBM_GFX_CNTL__VMID_MASK >> BC250_GC_GRBM_GFX_CNTL__VMID__SHIFT))
        return 0;
    plan.Kind = BC250_GC_SELECTOR_QUEUE;
    plan.ByteOffset = BC250_GC_GRBM_GFX_CNTL_BYTE_OFFSET;
    plan.Value = (me << BC250_GC_GRBM_GFX_CNTL__MEID__SHIFT) |
                 (pipe << BC250_GC_GRBM_GFX_CNTL__PIPEID__SHIFT) |
                 (queue << BC250_GC_GRBM_GFX_CNTL__QUEUEID__SHIFT) |
                 (vmid << BC250_GC_GRBM_GFX_CNTL__VMID__SHIFT);
    *out = plan;
    return 1;
}

static __inline int Bc250GcSelectorPlanValid(const BC250_GC_SELECTOR *plan)
{
    unsigned int mask;
    if (!plan) return 0;
    if (plan->Kind == BC250_GC_SELECTOR_SE_SH) {
        if (plan->ByteOffset != BC250_GC_GRBM_GFX_INDEX_BYTE_OFFSET) return 0;
        mask = BC250_GC_GRBM_GFX_INDEX__INSTANCE_INDEX_MASK |
               BC250_GC_GRBM_GFX_INDEX__SA_INDEX_MASK |
               BC250_GC_GRBM_GFX_INDEX__SE_INDEX_MASK | BC250_GC_GFX_INDEX_BROADCAST;
        if ((plan->Value & ~mask) != 0U) return 0;
        if ((plan->Value & BC250_GC_GRBM_GFX_INDEX__INSTANCE_BROADCAST_WRITES_MASK) &&
            (plan->Value & BC250_GC_GRBM_GFX_INDEX__INSTANCE_INDEX_MASK)) return 0;
        if ((plan->Value & BC250_GC_GRBM_GFX_INDEX__SA_BROADCAST_WRITES_MASK) &&
            (plan->Value & BC250_GC_GRBM_GFX_INDEX__SA_INDEX_MASK)) return 0;
        if ((plan->Value & BC250_GC_GRBM_GFX_INDEX__SE_BROADCAST_WRITES_MASK) &&
            (plan->Value & BC250_GC_GRBM_GFX_INDEX__SE_INDEX_MASK)) return 0;
    } else if (plan->Kind == BC250_GC_SELECTOR_QUEUE) {
        if (plan->ByteOffset != BC250_GC_GRBM_GFX_CNTL_BYTE_OFFSET) return 0;
        mask = BC250_GC_GRBM_GFX_CNTL__MEID_MASK | BC250_GC_GRBM_GFX_CNTL__PIPEID_MASK |
               BC250_GC_GRBM_GFX_CNTL__QUEUEID_MASK | BC250_GC_GRBM_GFX_CNTL__VMID_MASK;
        if ((plan->Value & ~mask) != 0U) return 0;
    } else return 0;
    return 1;
}
#endif
