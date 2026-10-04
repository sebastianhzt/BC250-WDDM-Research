/* Passive Windows VRAM-ownership preflight. Never accesses BARs or submits
 * commands; the only DeviceIoControl call is W2P_PREFLIGHT. */
#include <windows.h>
#include <stdio.h>
#include "..\inc\amdbc250_ioctl.h"

#define STATUS_DEVICE_NOT_READY_VALUE 0xC00000A3UL
#define REQUIRED_W2P_BLOCKERS ( \
    AMDBC250_W2P_BLOCK_NO_BAR0_RESOURCE | \
    AMDBC250_W2P_BLOCK_NO_WINDOWS_VRAM_OWNER | \
    AMDBC250_W2P_BLOCK_GPU_MC_MAP_UNVALIDATED | \
    AMDBC250_W2P_BLOCK_NO_WINDOWS_GART_OWNER | \
    AMDBC250_W2P_BLOCK_DMA_NOT_AUTHORIZED )

int main(void)
{
    HANDLE device;
    AMDBC250_IOCTL_W2P_PREFLIGHT result;
    DWORD returned = 0;
    BOOL ok;

    ZeroMemory(&result, sizeof(result));
    device = CreateFileW(AMDBC250_DEVICE_PATH, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (device == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "ERROR: cannot open BC-250 control device (Win32 %lu)\n",
            GetLastError());
        return 2;
    }
    ok = DeviceIoControl(device, IOCTL_AMDBC250_W2P_PREFLIGHT,
        NULL, 0, &result, sizeof(result), &returned, NULL);
    if (!ok) {
        DWORD error = GetLastError();
        CloseHandle(device);
        fprintf(stderr, "ERROR: W2P_PREFLIGHT failed (Win32 %lu)\n", error);
        return 2;
    }
    CloseHandle(device);

    if (returned != sizeof(result) ||
        result.Version != AMDBC250_W2P_PREFLIGHT_VERSION ||
        result.StructSize != sizeof(result)) {
        fprintf(stderr, "ERROR: W2P ABI mismatch (%lu bytes, version %u, size %u)\n",
            returned, result.Version, result.StructSize);
        return 2;
    }

    printf("=== BC-250 W2P preflight build %u (NO GPU WRITES) ===\n",
        result.DriverBuildId);
    printf("Status:                   0x%08X\n", (UINT32)result.Status);
    printf("Blockers:                 0x%08X\n", result.BlockerFlags);
    printf("PnP state/started:        %u / %u\n",
        result.PnpState, result.PnpStarted);
    printf("PnP safety/blockers:      0x%08X / 0x%08X\n",
        result.PnpSafetyFlags, result.PnpBlockerFlags);
    printf("HwInitGart/VM/SDMA:       0x%08X / 0x%08X / 0x%08X\n",
        result.HwInitGart, result.HwInitVm, result.HwInitSdmaRing);
    printf("BAR0/VRAM/MC/GART proof:  %u / %u / %u / %u\n",
        result.Bar0TranslatedResourceKnown,
        result.WindowsOwnedVramReservation,
        result.GpuMcTranslationValidated,
        result.GartOwnershipValidated);
    printf("Active IOCTLs/DMA:        %u / %u\n",
        result.ActiveControlIoctlsEnabled, result.CanSubmitDma);

    if ((UINT32)result.Status != STATUS_DEVICE_NOT_READY_VALUE ||
        (result.BlockerFlags & REQUIRED_W2P_BLOCKERS) !=
            REQUIRED_W2P_BLOCKERS ||
        result.Bar0TranslatedResourceKnown != 0 ||
        result.WindowsOwnedVramReservation != 0 ||
        result.GpuMcTranslationValidated != 0 ||
        result.GartOwnershipValidated != 0 ||
        result.ActiveControlIoctlsEnabled != 0 ||
        result.CanSubmitDma != 0) {
        fprintf(stderr, "FAIL-CLOSED VIOLATION: stop; do not run active tests.\n");
        return 2;
    }

    printf("DISARMED: ownership and translation are not validated.\n");
    printf("This tool calls only W2P_PREFLIGHT; it never maps VRAM or submits DMA.\n");
    return 3;
}
