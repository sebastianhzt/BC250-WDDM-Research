/* SPDX-License-Identifier: Apache-2.0
 * Original diagnostic-only routing policy. No hardware or Windows ownership.
 * ABI definitions must be included before this header.
 */
#ifndef BC250_DIAG_POLICY_H
#define BC250_DIAG_POLICY_H
#define BC250_DIAGNOSTIC_BUILD_ID 21U
#define BC250_DIAG_NORMAL 1U
#define BC250_DIAG_RESEARCH 2U
#define BC250_DIAG_NONE 0U
#define BC250_DIAG_PNP 1U
#define BC250_DIAG_W2P 2U
#define BC250_DIAG_RESOURCE 3U
#define BC250_DIAG_PCI_DISABLED 4U

static unsigned Bc250DiagRoute(unsigned endpoint, unsigned code)
{
    if (endpoint == BC250_DIAG_NORMAL) {
        if (code == IOCTL_AMDBC250_PNP_PREFLIGHT) return BC250_DIAG_PNP;
        if (code == IOCTL_AMDBC250_W2P_PREFLIGHT) return BC250_DIAG_W2P;
    } else if (endpoint == BC250_DIAG_RESEARCH) {
        if (code == IOCTL_AMDBC250_RESOURCE_PREFLIGHT) return BC250_DIAG_RESOURCE;
        if (code == IOCTL_AMDBC250_PCI_CONFIG_PREFLIGHT) return BC250_DIAG_PCI_DISABLED;
    }
    return BC250_DIAG_NONE;
}

static unsigned Bc250DiagSize(unsigned route)
{
    switch (route) {
    case BC250_DIAG_PNP: return sizeof(AMDBC250_IOCTL_PNP_PREFLIGHT);
    case BC250_DIAG_W2P: return sizeof(AMDBC250_IOCTL_W2P_PREFLIGHT);
    case BC250_DIAG_RESOURCE: return sizeof(AMDBC250_IOCTL_RESOURCE_PREFLIGHT);
    case BC250_DIAG_PCI_DISABLED: return sizeof(AMDBC250_IOCTL_PCI_CONFIG_PREFLIGHT);
    default: return 0U;
    }
}
#endif
