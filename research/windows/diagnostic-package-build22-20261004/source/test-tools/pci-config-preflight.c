/* SPDX-License-Identifier: Apache-2.0
 * Disabled PCI query and cached PnP metadata agreement only.
 * No PCI header reader, BAR parser, MMIO, mapping or DMA path.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../inc/amdbc250_ioctl.h"
#define BC250_EXPECTED_BUILD_ID 22U
#define STATUS_DEVICE_NOT_READY_VALUE 0xC00000A3UL
#define STATUS_NOT_SUPPORTED_VALUE 0xC00000BBUL
#define REQUIRED_RESOURCE_BLOCKERS (AMDBC250_RESOURCE_BLOCK_BAR_UNIDENTIFIED | \
    AMDBC250_RESOURCE_BLOCK_NO_VRAM_OWNER | \
    AMDBC250_RESOURCE_BLOCK_DMA_NOT_AUTHORIZED)

/* Equal observations are NOT an atomic global snapshot; an intervening
 * STOP/START (ABA) or unexported StateEpoch change is not excluded. */
static BOOL Bc250MetadataMatch(
    const AMDBC250_IOCTL_PCI_CONFIG_PREFLIGHT *config,
    const AMDBC250_IOCTL_RESOURCE_PREFLIGHT *resources)
{
    unsigned index;
    UINT32 expectedBlockers = REQUIRED_RESOURCE_BLOCKERS;
    const AMDBC250_RESOURCE_MEMORY_ENTRY empty = {0};
    if (resources->SnapshotState == AMDBC250_RESOURCE_SNAPSHOT_REJECTED)
        expectedBlockers |= AMDBC250_RESOURCE_BLOCK_REJECTED;
    else if (resources->SnapshotState == AMDBC250_RESOURCE_SNAPSHOT_NONE)
        expectedBlockers |= AMDBC250_RESOURCE_BLOCK_NO_SNAPSHOT;
    if (config->Version != AMDBC250_PCI_CONFIG_PREFLIGHT_VERSION ||
        config->StructSize != sizeof(*config) ||
        resources->Version != AMDBC250_RESOURCE_PREFLIGHT_VERSION ||
        resources->StructSize != sizeof(*resources) ||
        config->DriverBuildId != BC250_EXPECTED_BUILD_ID ||
        resources->DriverBuildId != BC250_EXPECTED_BUILD_ID ||
        (UINT32)config->ReadStatus != STATUS_NOT_SUPPORTED_VALUE ||
        config->BytesRead != 0 ||
        config->BlockerFlags != (AMDBC250_PCI_CONFIG_BLOCK_READ_DISABLED |
            AMDBC250_PCI_CONFIG_BLOCK_DMA_NOT_AUTHORIZED) ||
        (UINT32)resources->Status != STATUS_DEVICE_NOT_READY_VALUE ||
        resources->BlockerFlags != expectedBlockers ||
        resources->SnapshotState > AMDBC250_RESOURCE_SNAPSHOT_REJECTED ||
        resources->DescriptorCount > 32 ||
        resources->MemoryCount > AMDBC250_RESOURCE_MAX_MEMORY_ENTRIES ||
        (resources->SnapshotState != AMDBC250_RESOURCE_SNAPSHOT_VALID &&
            resources->MemoryCount != 0) ||
        config->PnpState > AMDBC250_PNP_STATE_DELETED ||
        resources->PnpState > AMDBC250_PNP_STATE_DELETED ||
        config->BindingGeneration == 0xffffffffU ||
        config->PnpState != resources->PnpState ||
        config->BindingGeneration != resources->BindingGeneration)
        return FALSE;
    for (index = 0; index < sizeof(config->Header); ++index)
        if (config->Header[index] != 0) return FALSE;
    for (index = 0; index < resources->MemoryCount; ++index)
        if (resources->Memory[index].Reserved != 0 ||
            resources->Memory[index].DescriptorOrdinal >= resources->DescriptorCount ||
            (index != 0 && resources->Memory[index].DescriptorOrdinal <=
                resources->Memory[index-1].DescriptorOrdinal)) return FALSE;
    for (index = resources->MemoryCount; index < AMDBC250_RESOURCE_MAX_MEMORY_ENTRIES; ++index)
        if (memcmp(&resources->Memory[index], &empty, sizeof(empty)) != 0) return FALSE;
    return TRUE;
}

int main(void)
{
    HANDLE device;
    AMDBC250_IOCTL_PCI_CONFIG_PREFLIGHT config;
    AMDBC250_IOCTL_RESOURCE_PREFLIGHT resources;
    DWORD returned = 0;
    BOOL ok;
    ZeroMemory(&config, sizeof(config));
    ZeroMemory(&resources, sizeof(resources));
    device = CreateFileW(AMDBC250_DIAG_DEVICE_PATH, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (device == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "ERROR: research device unavailable (%lu)\n", GetLastError());
        return 2;
    }
    ok = DeviceIoControl(device, IOCTL_AMDBC250_PCI_CONFIG_PREFLIGHT,
        NULL, 0, &config, sizeof(config), &returned, NULL);
    if (!ok || returned != sizeof(config)) {
        fprintf(stderr, "ERROR: PCI metadata query failed (%lu)\n", GetLastError());
        CloseHandle(device);
        return 2;
    }
    returned = 0;
    ok = DeviceIoControl(device, IOCTL_AMDBC250_RESOURCE_PREFLIGHT,
        NULL, 0, &resources, sizeof(resources), &returned, NULL);
    CloseHandle(device);
    if (!ok || returned != sizeof(resources)) {
        fprintf(stderr, "ERROR: resource metadata query failed\n");
        return 2;
    }
    printf("=== BC-250 disabled PCI/PnP metadata check, build %u ===\n",
        config.DriverBuildId);
    printf("PCI read: NTSTATUS=0x%08X bytes=%u; PnP state=%u generation=%u\n",
        (UINT32)config.ReadStatus, config.BytesRead,
        config.PnpState, config.BindingGeneration);
    printf("Resources: PnP state=%u generation=%u snapshot=%u\n",
        resources.PnpState, resources.BindingGeneration, resources.SnapshotState);
    if (!Bc250MetadataMatch(&config, &resources)) {
        fprintf(stderr, "UNVERIFIED: schema, closed-read status or PnP metadata differs/is unknown; stop.\n");
        return 2;
    }
    printf("PASS: PCI/PnP metadata observations agree; not a global snapshot.\n");
    printf("DISARMED: PCI config read is disabled in this build.\n");
    return 3;
}
