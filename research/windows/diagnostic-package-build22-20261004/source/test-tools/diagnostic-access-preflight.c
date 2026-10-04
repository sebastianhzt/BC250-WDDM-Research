/* Build-22 diagnostic access check. No GPU register or memory access.
 * Run after installation as both an elevated and a standard user. */
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <stdio.h>
#include <string.h>
#include "..\inc\amdbc250_ioctl.h"

#define STATUS_DEVICE_NOT_READY_VALUE 0xC00000A3UL
#define REQUIRED_BLOCKERS (AMDBC250_RESOURCE_BLOCK_BAR_UNIDENTIFIED | \
    AMDBC250_RESOURCE_BLOCK_NO_VRAM_OWNER | \
    AMDBC250_RESOURCE_BLOCK_DMA_NOT_AUTHORIZED)

static int IsElevated(void)
{
    HANDLE token = NULL;
    TOKEN_ELEVATION elevation;
    DWORD size = 0;
    int result = -1;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return -1;
    if (GetTokenInformation(token, TokenElevation, &elevation,
        sizeof(elevation), &size) && size == sizeof(elevation))
        result = elevation.TokenIsElevated ? 1 : 0;
    CloseHandle(token);
    return result;
}

static int PrintDeviceDacl(HANDLE device)
{
    PSECURITY_DESCRIPTOR descriptor = NULL;
    PACL dacl = NULL;
    LPWSTR sddl = NULL;
    char printable[1024];
    DWORD error;
    BOOL converted;

    error = GetSecurityInfo(device, SE_KERNEL_OBJECT,
        DACL_SECURITY_INFORMATION, NULL, NULL, &dacl, NULL, &descriptor);
    if (error != ERROR_SUCCESS || descriptor == NULL || dacl == NULL) {
        fprintf(stderr, "ERROR: cannot read a non-null diagnostic DACL (%lu)\n",
            error);
        if (descriptor != NULL)
            LocalFree(descriptor);
        return 0;
    }
    converted = ConvertSecurityDescriptorToStringSecurityDescriptorW(
        descriptor, SDDL_REVISION_1, DACL_SECURITY_INFORMATION,
        &sddl, NULL);
    if (converted && sddl != NULL &&
        WideCharToMultiByte(CP_UTF8, 0, sddl, -1, printable,
            sizeof(printable), NULL, NULL) > 0)
        printf("Diagnostic DACL: %s\n", printable);
    else
        converted = FALSE;
    if (sddl != NULL)
        LocalFree(sddl);
    LocalFree(descriptor);
    if (!converted) {
        fprintf(stderr, "ERROR: could not convert diagnostic DACL\n");
        return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    WCHAR target[256];
    DWORD linkLength;
    DWORD error;
    DWORD returned = 0;
    HANDLE diagnostic = INVALID_HANDLE_VALUE;
    HANDLE normal = INVALID_HANDLE_VALUE;
    AMDBC250_IOCTL_PNP_PREFLIGHT pnp;
    AMDBC250_IOCTL_RESOURCE_PREFLIGHT resources;
    AMDBC250_IOCTL_PCI_CONFIG_PREFLIGHT pciConfig;
    BOOL ok;
    int elevated;
    int expectAdmin;
    int result = 2;

    if (argc != 2 ||
        (strcmp(argv[1], "admin") != 0 && strcmp(argv[1], "standard") != 0)) {
        fprintf(stderr, "Usage: diagnostic-access-preflight.exe admin|standard\n");
        return 2;
    }
    expectAdmin = strcmp(argv[1], "admin") == 0;
    elevated = IsElevated();
    if (elevated < 0 || elevated != expectAdmin) {
        fprintf(stderr, "ERROR: run with the requested token elevation\n");
        return 2;
    }
    linkLength = QueryDosDeviceW(L"AMDBC250DreamResearchV1", target,
        sizeof(target) / sizeof(target[0]));
    if (linkLength == 0) {
        fprintf(stderr, "ERROR: diagnostic link absent (Win32 %lu)\n",
            GetLastError());
        return 2;
    }

    diagnostic = CreateFileW(AMDBC250_DIAG_DEVICE_PATH, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (!expectAdmin) {
        if (diagnostic != INVALID_HANDLE_VALUE) {
            fprintf(stderr, "FAIL: standard token opened diagnostic device\n");
            CloseHandle(diagnostic);
            return 2;
        }
        error = GetLastError();
        if (error != ERROR_ACCESS_DENIED) {
            fprintf(stderr, "ERROR: expected access denied, got Win32 %lu\n",
                error);
            return 2;
        }
        normal = CreateFileW(AMDBC250_DEVICE_PATH, GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (normal != INVALID_HANDLE_VALUE) {
            fprintf(stderr, "FAIL: standard token opened normal control\n");
            CloseHandle(normal);
            return 2;
        }
        error = GetLastError();
        if (error != ERROR_ACCESS_DENIED) {
            fprintf(stderr, "ERROR: expected normal access denied, got %lu\n", error);
            return 2;
        }
        printf("PASS: this standard token denied both endpoints. No IOCTL sent.\n");
        return 0;
    }
    if (diagnostic == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "ERROR: elevated token cannot open diagnostic (%lu)\n",
            GetLastError());
        return 2;
    }
    if (!PrintDeviceDacl(diagnostic))
        goto Cleanup;

    {
        HANDLE subpath = CreateFileW(
            L"\\\\.\\AMDBC250DreamResearchV1\\forbidden", GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (subpath != INVALID_HANDLE_VALUE) {
            fprintf(stderr, "FAIL: diagnostic subpath unexpectedly opened\n");
            CloseHandle(subpath);
            goto Cleanup;
        }
        printf("PASS: diagnostic subpath rejected (Win32 %lu).\n",
            GetLastError());
    }

    normal = CreateFileW(AMDBC250_DEVICE_PATH, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (normal == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "ERROR: normal control unavailable (Win32 %lu)\n",
            GetLastError());
        goto Cleanup;
    }
    if (!PrintDeviceDacl(normal)) goto Cleanup;
    {
        HANDLE subpath = CreateFileW(L"\\\\.\\AMDBC250DreamV43\\forbidden", GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (subpath != INVALID_HANDLE_VALUE) {
            fprintf(stderr, "FAIL: normal subpath unexpectedly opened\n");
            CloseHandle(subpath);
            goto Cleanup;
        }
        if (GetLastError() != ERROR_FILE_NOT_FOUND) {
            fprintf(stderr, "FAIL: normal subpath did not reach name rejection\n");
            goto Cleanup;
        }
    }
    ZeroMemory(&pnp, sizeof(pnp));
    ok = DeviceIoControl(normal, IOCTL_AMDBC250_PNP_PREFLIGHT,
        NULL, 0, &pnp, sizeof(pnp), &returned, NULL);
    if (!ok || returned != sizeof(pnp) ||
        pnp.Version != AMDBC250_PNP_PREFLIGHT_VERSION ||
        pnp.DriverBuildId != 22) {
        fprintf(stderr, "ERROR: loaded driver is not verified build 22\n");
        goto Cleanup;
    }

    ZeroMemory(&resources, sizeof(resources));
    returned = 0;
    ok = DeviceIoControl(normal, IOCTL_AMDBC250_RESOURCE_PREFLIGHT,
        NULL, 0, &resources, sizeof(resources), &returned, NULL);
    if (ok || returned != 0) {
        fprintf(stderr, "FAIL: normal control exposed resource IOCTL\n");
        goto Cleanup;
    }
    printf("PASS: normal control rejected resource IOCTL (Win32 %lu).\n",
        GetLastError());

    returned = 0;
    ok = DeviceIoControl(diagnostic, IOCTL_AMDBC250_RESOURCE_PREFLIGHT,
        NULL, 0, &resources, sizeof(resources), &returned, NULL);
    if (!ok || returned != sizeof(resources) ||
        resources.Version != AMDBC250_RESOURCE_PREFLIGHT_VERSION ||
        resources.StructSize != sizeof(resources) ||
        resources.DriverBuildId != 22 ||
        (UINT32)resources.Status != STATUS_DEVICE_NOT_READY_VALUE ||
        (resources.BlockerFlags & REQUIRED_BLOCKERS) != REQUIRED_BLOCKERS ||
        resources.MemoryCount > AMDBC250_RESOURCE_MAX_MEMORY_ENTRIES) {
        fprintf(stderr, "FAIL: diagnostic ABI or fail-closed status invalid\n");
        goto Cleanup;
    }
    printf("PASS: elevated diagnostic is DISARMED; snapshot state %u, memory entries %u.\n",
        resources.SnapshotState, resources.MemoryCount);
    ZeroMemory(&pciConfig, sizeof(pciConfig));
    returned = 0;
    ok = DeviceIoControl(normal, IOCTL_AMDBC250_PCI_CONFIG_PREFLIGHT,
        NULL, 0, &pciConfig, sizeof(pciConfig), &returned, NULL);
    if (ok || returned != 0) {
        fprintf(stderr, "FAIL: normal control exposed PCI config IOCTL\n");
        goto Cleanup;
    }
    returned = 0;
    ok = DeviceIoControl(diagnostic, IOCTL_AMDBC250_PCI_CONFIG_PREFLIGHT,
        NULL, 0, &pciConfig, sizeof(pciConfig), &returned, NULL);
    if (!ok || returned != sizeof(pciConfig) ||
        pciConfig.Version != AMDBC250_PCI_CONFIG_PREFLIGHT_VERSION ||
        pciConfig.StructSize != sizeof(pciConfig) ||
        pciConfig.DriverBuildId != 22 ||
        pciConfig.BlockerFlags !=
            (AMDBC250_PCI_CONFIG_BLOCK_DMA_NOT_AUTHORIZED |
             AMDBC250_PCI_CONFIG_BLOCK_READ_DISABLED) ||
        pciConfig.BytesRead != 0 ||
        (UINT32)pciConfig.ReadStatus != 0xC00000BBUL) {
        fprintf(stderr, "FAIL: PCI config ABI or DISARMED flag invalid\n");
        goto Cleanup;
    }
    printf("PASS: PCI config read status 0x%08X, %u bytes; DMA remains blocked.\n",
        (UINT32)pciConfig.ReadStatus, pciConfig.BytesRead);
    printf("No write, mapping, firmware, ring or DMA IOCTL was sent.\n");
    result = 0;

Cleanup:
    if (normal != INVALID_HANDLE_VALUE)
        CloseHandle(normal);
    CloseHandle(diagnostic);
    return result;
}
