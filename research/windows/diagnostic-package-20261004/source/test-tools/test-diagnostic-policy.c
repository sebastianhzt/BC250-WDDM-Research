/* SPDX-License-Identifier: Apache-2.0 -- CPU only. No device handles. */
#include <windows.h>
#include <stdio.h>
#include "../inc/amdbc250_ioctl.h"
#include "../inc/bc250_diag_policy.h"
static unsigned checks;
static int Check(int condition, int line)
{
    ++checks;
    if (!condition) { printf("FAIL line %d\n", line); return 0; }
    return 1;
}
#define CHECK(x) do { if (!Check((x), __LINE__)) return 1; } while (0)
int main(void)
{
    unsigned low, endpoint, code, route, expected;
    CHECK(sizeof(AMDBC250_IOCTL_PNP_PREFLIGHT) == 128U);
    CHECK(sizeof(AMDBC250_IOCTL_W2P_PREFLIGHT) == 96U);
    CHECK(sizeof(AMDBC250_IOCTL_RESOURCE_PREFLIGHT) == 360U);
    for (endpoint = 0; endpoint <= 3; ++endpoint) {
        for (low = 0; low < 65536U; ++low) {
            code = 0x80000000U | low;
            expected = 0;
            if (endpoint == BC250_DIAG_NORMAL && code == IOCTL_AMDBC250_PNP_PREFLIGHT) expected = BC250_DIAG_PNP;
            if (endpoint == BC250_DIAG_NORMAL && code == IOCTL_AMDBC250_W2P_PREFLIGHT) expected = BC250_DIAG_W2P;
            if (endpoint == BC250_DIAG_RESEARCH && code == IOCTL_AMDBC250_RESOURCE_PREFLIGHT) expected = BC250_DIAG_RESOURCE;
            if (endpoint == BC250_DIAG_RESEARCH && code == IOCTL_AMDBC250_PCI_CONFIG_PREFLIGHT) expected = BC250_DIAG_PCI_DISABLED;
            route = Bc250DiagRoute(endpoint, code);
            CHECK(route == expected);
            CHECK((Bc250DiagSize(route) != 0) == (expected != 0));
        }
        CHECK(Bc250DiagRoute(endpoint, 0U) == 0);
        CHECK(Bc250DiagRoute(endpoint, 0xffffffffU) == 0);
    }
    CHECK(Bc250DiagSize(5U) == 0);
    CHECK(Bc250DiagSize(0xffffffffU) == 0);
    printf("PASS: %u routing/ABI checks; CPU-only, no device opened.\n", checks);
    return 0;
}
