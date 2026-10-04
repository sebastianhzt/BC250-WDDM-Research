/* Passive PnP resource observation. Calls only RESOURCE_PREFLIGHT. */
#include <windows.h>
#include <stdio.h>
#include "..\inc\amdbc250_ioctl.h"

#define STATUS_DEVICE_NOT_READY_VALUE 0xC00000A3UL
#define REQUIRED_BLOCKERS (AMDBC250_RESOURCE_BLOCK_BAR_UNIDENTIFIED | \
    AMDBC250_RESOURCE_BLOCK_NO_VRAM_OWNER | \
    AMDBC250_RESOURCE_BLOCK_DMA_NOT_AUTHORIZED)

int main(void)
{
    HANDLE device;
    AMDBC250_IOCTL_RESOURCE_PREFLIGHT result;
    DWORD returned = 0;
    DWORD index;
    BOOL ok;

    ZeroMemory(&result, sizeof(result));
    device = CreateFileW(AMDBC250_DIAG_DEVICE_PATH, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (device == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "ERROR: cannot open BC-250 control device (Win32 %lu)\n",
            GetLastError());
        return 2;
    }
    ok = DeviceIoControl(device, IOCTL_AMDBC250_RESOURCE_PREFLIGHT,
        NULL, 0, &result, sizeof(result), &returned, NULL);
    if (!ok) {
        DWORD error = GetLastError();
        CloseHandle(device);
        fprintf(stderr, "ERROR: RESOURCE_PREFLIGHT failed (Win32 %lu)\n",
            error);
        return 2;
    }
    CloseHandle(device);

    if (returned != sizeof(result) ||
        result.Version != AMDBC250_RESOURCE_PREFLIGHT_VERSION ||
        result.StructSize != sizeof(result) ||
        result.MemoryCount > AMDBC250_RESOURCE_MAX_MEMORY_ENTRIES ||
        (result.SnapshotState != AMDBC250_RESOURCE_SNAPSHOT_VALID &&
         result.MemoryCount != 0)) {
        fprintf(stderr, "ERROR: resource ABI mismatch or invalid entry count\n");
        return 2;
    }
    printf("=== BC-250 PnP resource preflight build %u (NO GPU WRITES) ===\n",
        result.DriverBuildId);
    printf("Status/blockers: 0x%08X / 0x%08X\n",
        (UINT32)result.Status, result.BlockerFlags);
    printf("PnP state/snapshot: %u / %u\n",
        result.PnpState, result.SnapshotState);
    printf("Descriptor/memory counts: %u / %u\n",
        result.DescriptorCount, result.MemoryCount);
    for (index = 0; index < result.MemoryCount; ++index) {
        const AMDBC250_RESOURCE_MEMORY_ENTRY *entry = &result.Memory[index];
        if (entry->DescriptorOrdinal >= result.DescriptorCount ||
            entry->Reserved != 0 ||
            (index != 0 &&
             entry->DescriptorOrdinal <=
                 result.Memory[index - 1].DescriptorOrdinal)) {
            fprintf(stderr, "ERROR: invalid resource descriptor ordinal\n");
            return 2;
        }
        printf("Memory[%lu] descriptor[%u]: raw=0x%016I64X translated=0x%016I64X "
            "length=0x%016I64X flags=0x%04X/0x%04X\n", index,
            entry->DescriptorOrdinal,
            (unsigned __int64)entry->RawStart,
            (unsigned __int64)entry->TranslatedStart,
            (unsigned __int64)entry->Length,
            entry->RawFlags, entry->TranslatedFlags);
    }
    if ((UINT32)result.Status != STATUS_DEVICE_NOT_READY_VALUE ||
        (result.BlockerFlags & REQUIRED_BLOCKERS) != REQUIRED_BLOCKERS) {
        fprintf(stderr, "FAIL-CLOSED VIOLATION: stop; do not run active tests.\n");
        return 2;
    }
    printf("DISARMED: descriptor ordinal is not a BAR number or VRAM ownership.\n");
    printf("This tool never maps resources or submits DMA.\n");
    return 3;
}
