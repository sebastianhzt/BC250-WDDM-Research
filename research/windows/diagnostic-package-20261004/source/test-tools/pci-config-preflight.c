/* Passive PCI header/START_DEVICE resource correlation. No GPU writes. */
#include <windows.h>
#include <stdio.h>
#include "..\inc\amdbc250_ioctl.h"

#define STATUS_DEVICE_NOT_READY_VALUE 0xC00000A3UL
#define STATUS_NOT_SUPPORTED_VALUE 0xC00000BBUL
#define REQUIRED_RESOURCE_BLOCKERS (AMDBC250_RESOURCE_BLOCK_BAR_UNIDENTIFIED | \
    AMDBC250_RESOURCE_BLOCK_NO_VRAM_OWNER | \
    AMDBC250_RESOURCE_BLOCK_DMA_NOT_AUTHORIZED)

static UINT32 Read32(const UCHAR *bytes)
{
    return (UINT32)bytes[0] | ((UINT32)bytes[1] << 8) |
        ((UINT32)bytes[2] << 16) | ((UINT32)bytes[3] << 24);
}

int main(void)
{
    HANDLE device;
    AMDBC250_IOCTL_PCI_CONFIG_PREFLIGHT config;
    AMDBC250_IOCTL_RESOURCE_PREFLIGHT resources;
    DWORD returned = 0;
    DWORD index;
    BOOL ok;

    ZeroMemory(&config, sizeof(config));
    ZeroMemory(&resources, sizeof(resources));
    device = CreateFileW(AMDBC250_DIAG_DEVICE_PATH, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (device == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "ERROR: research diagnostic device unavailable (Win32 %lu)\n",
            GetLastError());
        return 2;
    }
    ok = DeviceIoControl(device, IOCTL_AMDBC250_PCI_CONFIG_PREFLIGHT,
        NULL, 0, &config, sizeof(config), &returned, NULL);
    if (!ok || returned != sizeof(config) ||
        config.Version != AMDBC250_PCI_CONFIG_PREFLIGHT_VERSION ||
        config.StructSize != sizeof(config) ||
        (config.BlockerFlags & AMDBC250_PCI_CONFIG_BLOCK_DMA_NOT_AUTHORIZED)
            == 0 ||
        (config.BlockerFlags & ~(
            AMDBC250_PCI_CONFIG_BLOCK_DMA_NOT_AUTHORIZED |
            AMDBC250_PCI_CONFIG_BLOCK_READ_DISABLED)) != 0 ||
        config.BytesRead > AMDBC250_PCI_CONFIG_HEADER_BYTES ||
        ((config.ReadStatus == 0) !=
         (config.BytesRead == AMDBC250_PCI_CONFIG_HEADER_BYTES))) {
        fprintf(stderr, "ERROR: PCI config diagnostic failed or ABI invalid (Win32 %lu)\n",
            GetLastError());
        CloseHandle(device);
        return 2;
    }
    returned = 0;
    ok = DeviceIoControl(device, IOCTL_AMDBC250_RESOURCE_PREFLIGHT,
        NULL, 0, &resources, sizeof(resources), &returned, NULL);
    CloseHandle(device);
    if (!ok || returned != sizeof(resources) ||
        resources.Version != AMDBC250_RESOURCE_PREFLIGHT_VERSION ||
        resources.StructSize != sizeof(resources) ||
        resources.MemoryCount > AMDBC250_RESOURCE_MAX_MEMORY_ENTRIES ||
        resources.DriverBuildId != config.DriverBuildId ||
        (UINT32)resources.Status != STATUS_DEVICE_NOT_READY_VALUE ||
        (resources.BlockerFlags & REQUIRED_RESOURCE_BLOCKERS) !=
            REQUIRED_RESOURCE_BLOCKERS) {
        fprintf(stderr, "ERROR: PnP resource diagnostic failed or not DISARMED\n");
        return 2;
    }

    printf("=== BC-250 PCI/PnP passive correlation, build %u ===\n",
        config.DriverBuildId);
    printf("PCI read: NTSTATUS=0x%08X bytes=%u; PnP state=%u generation=%u\n",
        (UINT32)config.ReadStatus, config.BytesRead,
        config.PnpState, config.BindingGeneration);
    if (config.BindingGeneration != resources.BindingGeneration) {
        fprintf(stderr, "STALE: PnP binding changed between observations\n");
        return 3;
    }
    if (config.BlockerFlags & AMDBC250_PCI_CONFIG_BLOCK_READ_DISABLED) {
        for (index = 0; index < AMDBC250_PCI_CONFIG_HEADER_BYTES; ++index) {
            if (config.Header[index] != 0) {
                fprintf(stderr, "ERROR: disabled PCI read returned data\n");
                return 2;
            }
        }
        if ((UINT32)config.ReadStatus != STATUS_NOT_SUPPORTED_VALUE ||
            config.BytesRead != 0) {
            fprintf(stderr, "ERROR: disabled PCI read reported success\n");
            return 2;
        }
        printf("DISARMED: PCI config read is disabled in this build.\n");
        return 3;
    }
    if (config.ReadStatus != 0 ||
        config.BytesRead != AMDBC250_PCI_CONFIG_HEADER_BYTES) {
        printf("DISARMED: no complete PCI header; no BAR correlation possible.\n");
        return 3;
    }
    if ((UINT16)(config.Header[0] | (config.Header[1] << 8)) != 0x1002 ||
        (UINT16)(config.Header[2] | (config.Header[3] << 8)) != 0x13FE ||
        (config.Header[0x0E] & 0x7F) != 0) {
        fprintf(stderr, "ERROR: unexpected PCI vendor/device/header type\n");
        return 2;
    }
    printf("PCI 1002:13FE header type 0x%02X; PnP snapshot=%u entries=%u\n",
        config.Header[0x0E], resources.SnapshotState,
        resources.MemoryCount);
    for (index = 0; index < resources.MemoryCount; ++index) {
        const AMDBC250_RESOURCE_MEMORY_ENTRY *entry =
            &resources.Memory[index];
        if (entry->Reserved != 0 ||
            entry->DescriptorOrdinal >= resources.DescriptorCount ||
            (index != 0 && entry->DescriptorOrdinal <=
                resources.Memory[index - 1].DescriptorOrdinal)) {
            fprintf(stderr, "ERROR: invalid resource descriptor ordinal\n");
            return 2;
        }
    }

    for (index = 0; index < 6; ++index) {
        UINT32 bar = Read32(&config.Header[0x10 + index * 4]);
        unsigned __int64 base;
        DWORD resourceIndex;
        BOOL is64;
        BOOL matched = FALSE;

        if ((bar & 1) != 0) {
            printf("BAR%lu: I/O 0x%08X (no memory correlation)\n", index, bar);
            continue;
        }
        if ((bar & 0x6) == 0x4 && index == 5) {
            printf("BAR5: malformed 64-bit BAR in final slot\n");
            continue;
        }
        is64 = (bar & 0x6) == 0x4;
        base = (unsigned __int64)(bar & ~0xFUL);
        if (is64)
            base |= (unsigned __int64)
                Read32(&config.Header[0x10 + (index + 1) * 4]) << 32;
        printf("BAR%lu: raw=0x%08X base=0x%016I64X%s",
            index, bar, base, is64 ? " (64-bit)" : "");
        if (base != 0 && bar != 0xFFFFFFFFUL &&
            resources.SnapshotState == AMDBC250_RESOURCE_SNAPSHOT_VALID) {
            for (resourceIndex = 0; resourceIndex < resources.MemoryCount;
                 ++resourceIndex) {
                const AMDBC250_RESOURCE_MEMORY_ENTRY *entry =
                    &resources.Memory[resourceIndex];
                if (entry->RawStart == base && entry->Length != 0 &&
                    entry->DescriptorOrdinal < resources.DescriptorCount) {
                    printf(" candidate descriptor[%u] raw length=0x%016I64X"
                        " translated=0x%016I64X",
                        entry->DescriptorOrdinal,
                        (unsigned __int64)entry->Length,
                        (unsigned __int64)entry->TranslatedStart);
                    matched = TRUE;
                }
            }
        }
        if (!matched)
            printf(" (no exact raw-base descriptor match)");
        printf("\n");
        if (is64)
            ++index;
    }
    printf("DISARMED: BAR matching is not VRAM ownership, mapping or DMA permission.\n");
    printf("Only PCI READ_CONFIG and RESOURCE_PREFLIGHT were requested.\n");
    return 3;
}
