/* SPDX-License-Identifier: Apache-2.0
 * Production fill/comparator bodies are extracted verbatim by the runner.
 * Lock/IRQL/atomic services and the fakeExtension are RAM FAKES, not WDM tests.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../inc/amdbc250_ioctl.h"
#undef PASSIVE_LEVEL
#define PASSIVE_LEVEL 0U
#ifndef MAXULONG
#define MAXULONG 0xffffffffUL
#endif
#undef STATUS_DEVICE_NOT_READY
#undef STATUS_NOT_SUPPORTED
#define STATUS_DEVICE_NOT_READY ((LONG)0xc00000a3L)
#define STATUS_NOT_SUPPORTED ((LONG)0xc00000bbL)
#define STATUS_DEVICE_NOT_READY_VALUE 0xC00000A3UL
#define STATUS_NOT_SUPPORTED_VALUE 0xC00000BBUL
#define REQUIRED_RESOURCE_BLOCKERS (AMDBC250_RESOURCE_BLOCK_BAR_UNIDENTIFIED | \
    AMDBC250_RESOURCE_BLOCK_NO_VRAM_OWNER | AMDBC250_RESOURCE_BLOCK_DMA_NOT_AUTHORIZED)
typedef struct {
    ULONG Signature;
    volatile LONG State;
    AMDBC250_IOCTL_RESOURCE_PREFLIGHT ResourceSnapshot;
} DREAM_V3_WDM_PNP_EXTENSION, *PDREAM_V3_WDM_PNP_EXTENSION;
static DREAM_V3_WDM_PNP_EXTENSION fakeExtension;
static PDREAM_V3_WDM_PNP_EXTENSION g_DreamV3PnpBinding;
static volatile LONG g_DreamV3BindingGeneration;
static ULONG g_DreamV3PnpBindingLock;
static unsigned irql, critical, locked, enters, leaves, acquires, releases;
static unsigned faults, mutateOnAcquire, checks;
static ULONG KeGetCurrentIrql(void) { return irql; }
static VOID KeEnterCriticalRegion(void) { ++enters; ++critical; }
static VOID KeLeaveCriticalRegion(void) {
    if (locked || critical != 1) ++faults;
    ++leaves; --critical;
}
static VOID ExAcquirePushLockShared(ULONG *lock) {
    if (lock != &g_DreamV3PnpBindingLock || critical != 1 || locked) ++faults;
    ++acquires; ++locked;
    if (mutateOnAcquire) {
        g_DreamV3BindingGeneration += 17;
        fakeExtension.State = (fakeExtension.State + 1) % 7;
        mutateOnAcquire = 0;
    }
}
static VOID ExReleasePushLockShared(ULONG *lock) {
    if (lock != &g_DreamV3PnpBindingLock || critical != 1 || locked != 1) ++faults;
    ++releases; --locked;
}
static LONG FakeCompareExchange(volatile LONG *value, LONG exchange, LONG compare) {
    LONG old;
    if (!locked || critical != 1 ||
        (value != &fakeExtension.State && value != &g_DreamV3BindingGeneration)) ++faults;
    old = *value;
    if (old == compare) *value = exchange;
    return old;
}
#undef InterlockedCompareExchange
#define InterlockedCompareExchange FakeCompareExchange
#include "metadata-body.inc"
#define CHECK(x) do { ++checks; if (!(x)) { printf("FAIL metadata line %d\n",__LINE__); return 1; } } while (0)
int main(void)
{
    const UINT32 states[]={0,1,2,3,4,5,6,MAXULONG};
    const UINT32 generations[]={0,1,71,MAXULONG};
    const ULONG levels[]={0,1,2};
    const UINT32 counts[]={0,3,8};
    AMDBC250_IOCTL_RESOURCE_PREFLIGHT r, expectedResource, changedResource;
    AMDBC250_IOCTL_PCI_CONFIG_PREFLIGHT p, expectedPci, changedPci;
    unsigned st, gen, bind, level, snap, count, mutation, index, mismatch;
    UINT32 sampledState, sampledGeneration;
    for (st=0; st<8; ++st) for (gen=0; gen<4; ++gen)
    for (bind=0; bind<3; ++bind) for (level=0; level<3; ++level)
    for (snap=0; snap<3; ++snap) for (count=0; count<3; ++count)
    for (mutation=0; mutation<2; ++mutation) {
        memset(&fakeExtension,0,sizeof(fakeExtension));
        fakeExtension.Signature=bind==2?0:DREAM_V3_PNP_EXTENSION_SIGNATURE;
        fakeExtension.State=(LONG)states[st];
        fakeExtension.ResourceSnapshot.SnapshotState=snap;
        fakeExtension.ResourceSnapshot.DescriptorCount=9;
        fakeExtension.ResourceSnapshot.MemoryCount=snap==1?counts[count]:0;
        for(index=0;index<fakeExtension.ResourceSnapshot.MemoryCount;++index) {
            fakeExtension.ResourceSnapshot.Memory[index].DescriptorOrdinal=index;
            fakeExtension.ResourceSnapshot.Memory[index].RawStart=0x100000ULL+index*4096ULL;
            fakeExtension.ResourceSnapshot.Memory[index].TranslatedStart=0x200000ULL+index*4096ULL;
            fakeExtension.ResourceSnapshot.Memory[index].Length=4096;
        }
        g_DreamV3PnpBinding=bind?&fakeExtension:NULL;
        g_DreamV3BindingGeneration=(LONG)generations[gen];
        irql=levels[level];
        critical=locked=enters=leaves=acquires=releases=faults=0;
        mutateOnAcquire=mutation;
        /* Never mutate the signed LONG at MAXLONG; fake values are tiny or -1. */
        memset(&r,0xa5,sizeof(r)); memset(&p,0xa5,sizeof(p));
        DreamV3FillResourcePreflight(&r,BC250_EXPECTED_BUILD_ID);
        DreamV3FillPciConfigPreflight(&p,BC250_EXPECTED_BUILD_ID);
        sampledState=irql?MAXULONG:!bind?0:bind==2?MAXULONG:(UINT32)fakeExtension.State;
        sampledGeneration=irql?MAXULONG:(UINT32)g_DreamV3BindingGeneration;
        memset(&expectedResource,0,sizeof(expectedResource));
        expectedResource.Version=AMDBC250_RESOURCE_PREFLIGHT_VERSION;
        expectedResource.StructSize=sizeof(expectedResource);
        expectedResource.DriverBuildId=BC250_EXPECTED_BUILD_ID;
        expectedResource.Status=STATUS_DEVICE_NOT_READY;
        expectedResource.BlockerFlags=REQUIRED_RESOURCE_BLOCKERS;
        expectedResource.PnpState=sampledState;
        expectedResource.BindingGeneration=sampledGeneration;
        if (!irql && bind==1 && sampledState==AMDBC250_PNP_STATE_STARTED) {
            expectedResource.SnapshotState=snap;
            expectedResource.DescriptorCount=9;
            expectedResource.MemoryCount=fakeExtension.ResourceSnapshot.MemoryCount;
            memcpy(expectedResource.Memory,fakeExtension.ResourceSnapshot.Memory,sizeof(expectedResource.Memory));
        }
        if (expectedResource.SnapshotState==AMDBC250_RESOURCE_SNAPSHOT_REJECTED)
            expectedResource.BlockerFlags|=AMDBC250_RESOURCE_BLOCK_REJECTED;
        else if (expectedResource.SnapshotState!=AMDBC250_RESOURCE_SNAPSHOT_VALID)
            expectedResource.BlockerFlags|=AMDBC250_RESOURCE_BLOCK_NO_SNAPSHOT;
        memset(&expectedPci,0,sizeof(expectedPci));
        expectedPci.Version=AMDBC250_PCI_CONFIG_PREFLIGHT_VERSION;
        expectedPci.StructSize=sizeof(expectedPci);
        expectedPci.DriverBuildId=BC250_EXPECTED_BUILD_ID;
        expectedPci.ReadStatus=STATUS_NOT_SUPPORTED;
        expectedPci.BlockerFlags=AMDBC250_PCI_CONFIG_BLOCK_DMA_NOT_AUTHORIZED|AMDBC250_PCI_CONFIG_BLOCK_READ_DISABLED;
        expectedPci.PnpState=sampledState;
        expectedPci.BindingGeneration=sampledGeneration;
        CHECK(memcmp(&r,&expectedResource,sizeof(r))==0);
        CHECK(memcmp(&p,&expectedPci,sizeof(p))==0);
        CHECK(faults==0); CHECK(critical==0); CHECK(locked==0);
        CHECK(enters==(irql?0U:2U)); CHECK(leaves==enters);
        CHECK(acquires==enters); CHECK(releases==enters);
        CHECK(Bc250MetadataMatch(&p,&r)==(sampledState<=AMDBC250_PNP_STATE_DELETED && sampledGeneration!=MAXULONG));
        for (index=0;index<sizeof(p.Header);++index) CHECK(p.Header[index]==0);
    }
    /* Start, query-stop/cancel, stop/start, query-remove/cancel, surprise/delete:
     * serial RAM observations only; no actual IRPs or asynchronous transitions. */
    memset(&fakeExtension,0,sizeof(fakeExtension));
    fakeExtension.Signature=DREAM_V3_PNP_EXTENSION_SIGNATURE;
    fakeExtension.ResourceSnapshot.SnapshotState=AMDBC250_RESOURCE_SNAPSHOT_VALID;
    g_DreamV3PnpBinding=&fakeExtension; irql=0; mutateOnAcquire=0;
    g_DreamV3BindingGeneration=71;
    for (index=0;index<9;++index) {
        const UINT32 sequence[]={1,2,1,3,1,4,1,5,6};
        fakeExtension.State=(LONG)sequence[index];
        DreamV3FillResourcePreflight(&r,BC250_EXPECTED_BUILD_ID);
        DreamV3FillPciConfigPreflight(&p,BC250_EXPECTED_BUILD_ID);
        CHECK(Bc250MetadataMatch(&p,&r));
        CHECK(p.PnpState==sequence[index]);
    }
    fakeExtension.State=1;
    DreamV3FillResourcePreflight(&r,BC250_EXPECTED_BUILD_ID);
    DreamV3FillPciConfigPreflight(&p,BC250_EXPECTED_BUILD_ID);
    for (mismatch=0;mismatch<25;++mismatch) {
        changedPci=p; changedResource=r;
        switch(mismatch) {
        case 0: ++changedPci.Version; break;
        case 1: --changedPci.StructSize; break;
        case 2: changedPci.DriverBuildId=21; break;
        case 3: ++changedResource.Version; break;
        case 4: --changedResource.StructSize; break;
        case 5: changedResource.DriverBuildId=21; break;
        case 6: changedPci.ReadStatus=0; break;
        case 7: changedPci.BytesRead=64; break;
        case 8: changedPci.BlockerFlags=1; break;
        case 9: changedPci.Header[63]=1; break;
        case 10: ++changedPci.BindingGeneration; break;
        case 11: changedPci.PnpState=0; break;
        case 12: changedPci.PnpState=MAXULONG; changedResource.PnpState=MAXULONG; break;
        case 13: changedResource.Status=0; break;
        case 14: changedResource.BlockerFlags=0; break;
        case 15: changedResource.MemoryCount=9; break;
        case 16: changedResource.SnapshotState=3; break;
        case 17: changedResource.DescriptorCount=33; break;
        case 18: changedPci.BindingGeneration=MAXULONG; changedResource.BindingGeneration=MAXULONG; break;
        case 19: changedResource.MemoryCount=1; changedResource.DescriptorCount=1; changedResource.Memory[0].Reserved=1; break;
        case 20: changedResource.MemoryCount=1; changedResource.DescriptorCount=1; changedResource.Memory[0].DescriptorOrdinal=1; break;
        case 21: changedResource.MemoryCount=2; changedResource.DescriptorCount=2; break;
        case 22: changedResource.Memory[7].Length=1; break;
        case 23: changedResource.BlockerFlags|=AMDBC250_RESOURCE_BLOCK_NO_SNAPSHOT; break;
        case 24: changedResource.SnapshotState=0; changedResource.MemoryCount=1; break;
        }
        CHECK(!Bc250MetadataMatch(&changedPci,&changedResource));
    }
    /* ABA remains invisible: equal metadata after STOP/START is not global proof. */
    fakeExtension.State=3; fakeExtension.State=1;
    DreamV3FillPciConfigPreflight(&p,BC250_EXPECTED_BUILD_ID);
    CHECK(Bc250MetadataMatch(&p,&r));
    printf("PASS: %u production-metadata checks; locks/IRQL/atomics are RAM fakes.\n",checks);
    return 0;
}
