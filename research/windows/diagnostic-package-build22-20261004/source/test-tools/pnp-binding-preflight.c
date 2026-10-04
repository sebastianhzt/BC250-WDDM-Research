/* WDM PnP lifecycle query. Calls only PNP_PREFLIGHT. */

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>
#include "..\inc\amdbc250_ioctl.h"

#define STATUS_DEVICE_NOT_READY_VALUE 0xC00000A3UL

static FILE *g_Log;
static WCHAR g_LogPath[MAX_PATH];

static void LogPrintf(const char *format, ...)
{
    va_list args;

    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    if (g_Log != NULL) {
        va_start(args, format);
        vfprintf(g_Log, format, args);
        va_end(args);
        fflush(g_Log);
    }
}

#define printf LogPrintf

static void OpenUniqueLog(UINT32 build)
{
    WCHAR modulePath[MAX_PATH];
    WCHAR *slash;
    SYSTEMTIME now;

    GetLocalTime(&now);
    if (GetModuleFileNameW(NULL, modulePath, MAX_PATH) == 0)
        return;
    slash = wcsrchr(modulePath, L'\\');
    if (slash == NULL)
        return;
    *slash = L'\0';
    _snwprintf_s(g_LogPath, MAX_PATH, _TRUNCATE,
        L"%s\\pnp-binding-preflight-%04u%02u%02u-%02u%02u%02u-%03u-build%u-p%lu.log",
        modulePath, (unsigned int)now.wYear, (unsigned int)now.wMonth,
        (unsigned int)now.wDay, (unsigned int)now.wHour,
        (unsigned int)now.wMinute, (unsigned int)now.wSecond,
        (unsigned int)now.wMilliseconds, build, GetCurrentProcessId());
    _wfopen_s(&g_Log, g_LogPath, L"w");
}

static const char *PnpStateName(UINT32 state)
{
    switch (state) {
    case AMDBC250_PNP_STATE_NOT_STARTED: return "not started";
    case AMDBC250_PNP_STATE_STARTED: return "started";
    case AMDBC250_PNP_STATE_STOP_PENDING: return "stop pending";
    case AMDBC250_PNP_STATE_STOPPED: return "stopped";
    case AMDBC250_PNP_STATE_REMOVE_PENDING: return "remove pending";
    case AMDBC250_PNP_STATE_SURPRISE_REMOVED: return "surprise removed";
    case AMDBC250_PNP_STATE_DELETED: return "deleted";
    default: return "unknown";
    }
}

int main(void)
{
    HANDLE device;
    AMDBC250_IOCTL_PNP_PREFLIGHT result;
    DWORD returned = 0;
    BOOL ok;
    BOOL bindingComplete;
    int exitCode = 3;

    ZeroMemory(&result, sizeof(result));
    device = CreateFileW(AMDBC250_DEVICE_PATH, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (device == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "ERROR: cannot open %ls (Win32 %lu)\n",
            AMDBC250_DEVICE_PATH, GetLastError());
        return 2;
    }
    ok = DeviceIoControl(device, IOCTL_AMDBC250_PNP_PREFLIGHT,
        NULL, 0, &result, sizeof(result), &returned, NULL);
    if (!ok) {
        DWORD error = GetLastError();
        CloseHandle(device);
        fprintf(stderr, "ERROR: PNP_PREFLIGHT failed (Win32 %lu)\n", error);
        return 2;
    }
    CloseHandle(device);

    if (returned != sizeof(result) ||
        result.Version != AMDBC250_PNP_PREFLIGHT_VERSION ||
        result.StructSize != sizeof(result)) {
        fprintf(stderr,
            "ERROR: ABI mismatch: returned=%lu version=%u size=%u expected=%zu\n",
            returned, result.Version, result.StructSize, sizeof(result));
        return 2;
    }

    OpenUniqueLog(result.DriverBuildId);
    printf("=== WDM PnP binding preflight build %u (NO DMA/MMIO/HW WRITES BY THIS TOOL) ===\n",
        result.DriverBuildId);
    printf("Inner status:             0x%08X (DISARMED expected)\n",
        (UINT32)result.Status);
    printf("Query IRQL:               %u\n", result.QueryIrql);
    printf("PnP state:                %u (%s)\n", result.PnpState,
        PnpStateName(result.PnpState));
    printf("Binding generation:       %u\n", result.BindingGeneration);
    printf("Add/start calls:          %u / %u\n",
        result.AddDeviceCalls, result.StartDeviceCalls);
    printf("Query-stop/stop calls:    %u / %u\n",
        result.QueryStopCalls, result.StopDeviceCalls);
    printf("Query-remove/surprise/remove: %u / %u / %u\n",
        result.QueryRemoveCalls, result.SurpriseRemoveCalls,
        result.RemoveDeviceCalls);
    printf("Binding/FDO/lower:        %u / %u / %u\n",
        result.BindingPresent, result.FdoCreated, result.LowerAttached);
    printf("HWID/started/PDO ref:     %u / %u / %u\n",
        result.HardwareIdMatched, result.Started,
        result.PdoReferenceAcquired);
    printf("FDO/lower stack sizes:    %u / %u\n",
        result.FdoStackSize, result.LowerStackSize);
    printf("Last add/start status:    0x%08X / 0x%08X\n",
        (UINT32)result.LastAddStatus, (UINT32)result.LastStartStatus);
    printf("HwInitGart/VM/SDMA:       %u / %u / %u\n",
        result.HwInitGart, result.HwInitVm, result.HwInitSdmaRing);
    printf("Safety/blocker flags:     0x%08X / 0x%08X\n",
        result.SafetyFlags, result.BlockerFlags);

    bindingComplete = result.QueryIrql == 0 &&
        result.PnpState == AMDBC250_PNP_STATE_STARTED &&
        result.AddDeviceCalls != 0 && result.StartDeviceCalls != 0 &&
        result.BindingPresent == 1 && result.FdoCreated == 1 &&
        result.LowerAttached == 1 && result.HardwareIdMatched == 1 &&
        result.Started == 1 && result.PdoReferenceAcquired == 1 &&
        result.FdoStackSize == result.LowerStackSize + 1;

    if ((UINT32)result.Status != STATUS_DEVICE_NOT_READY_VALUE) {
        printf("\nFAIL-CLOSED VIOLATION: inner status is not DISARMED.\n");
        exitCode = 2;
    } else if (result.HwInitGart != 0 || result.HwInitVm != 0 ||
        result.HwInitSdmaRing != 0) {
        printf("\nFAIL-CLOSED VIOLATION: a hardware interlock is enabled.\n");
        exitCode = 2;
    } else if (result.PdoReferenceAcquired != 0 &&
        (result.BindingPresent == 0 || result.LowerAttached == 0 ||
         result.HardwareIdMatched == 0 || result.Started == 0)) {
        printf("\nFAIL-CLOSED VIOLATION: PDO reference lacks binding proof.\n");
        exitCode = 2;
    } else {
        printf("\nPnP evidence complete:    %s\n",
            bindingComplete ? "YES" : "NO (see blockers)");
        printf("PNP PREFLIGHT DISARMED: lifecycle evidence only. Active GPU IOCTLs are disabled in this build.\n");
    }

    printf("This tool calls only PNP_PREFLIGHT.\n");
    if (g_Log != NULL) {
        printf("Unique log: %ls\n", g_LogPath);
        fclose(g_Log);
    }
    return exitCode;
}
