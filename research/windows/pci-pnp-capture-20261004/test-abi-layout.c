/* SPDX-License-Identifier: Apache-2.0 */
#include <windows.h>
#include <stddef.h>
#include <stdio.h>
#include "../diagnostic-package-build22-20261004/source/inc/amdbc250_ioctl.h"
#include "capture-wire-layout.h"
#define OFFSET(t,f,n) typedef char Verify_##t##_##f[(offsetof(t,f)==(n))?1:-1]
int main(void)
{
    typedef AMDBC250_IOCTL_RESOURCE_PREFLIGHT R;
    typedef AMDBC250_RESOURCE_MEMORY_ENTRY M;
    typedef char VerifyWireSize[(sizeof(R)==BC250_CAPTURE_WIRE_BYTES)?1:-1];
    typedef char VerifyEntrySize[(sizeof(M)==BC250_CW_ENTRY_BYTES)?1:-1];
    typedef char VerifyVersion[(AMDBC250_RESOURCE_PREFLIGHT_VERSION==BC250_CAPTURE_WIRE_VERSION)?1:-1];
    typedef char VerifyCount[(AMDBC250_RESOURCE_MAX_MEMORY_ENTRIES==8)?1:-1];
    OFFSET(R,Version,BC250_CW_VERSION);OFFSET(R,StructSize,BC250_CW_SIZE);
    OFFSET(R,DriverBuildId,BC250_CW_BUILD);OFFSET(R,Status,BC250_CW_STATUS);
    OFFSET(R,BlockerFlags,BC250_CW_BLOCKERS);OFFSET(R,PnpState,BC250_CW_STATE);
    OFFSET(R,SnapshotState,BC250_CW_SNAPSHOT);OFFSET(R,DescriptorCount,BC250_CW_DESCRIPTORS);
    OFFSET(R,MemoryCount,BC250_CW_COUNT);OFFSET(R,BindingGeneration,BC250_CW_GENERATION);
    OFFSET(R,Memory,BC250_CW_MEMORY);OFFSET(M,RawStart,BC250_CW_RAW);
    OFFSET(M,TranslatedStart,BC250_CW_TRANSLATED);OFFSET(M,Length,BC250_CW_LENGTH);
    OFFSET(M,RawFlags,BC250_CW_RAW_FLAGS);OFFSET(M,TranslatedFlags,BC250_CW_TRANSLATED_FLAGS);
    OFFSET(M,DescriptorOrdinal,BC250_CW_ORDINAL);OFFSET(M,Reserved,BC250_CW_RESERVED);
    puts("PASS: actual frozen Windows resource v2 ABI: 360/40 bytes, 19 offsets. Not a coherent source provider.");
    return 0;
}
