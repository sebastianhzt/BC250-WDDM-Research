/* SPDX-License-Identifier: Apache-2.0
 * Original metadata-only ABI. No device discovery, addresses or GPU permission.
 */
#ifndef BC250_CONTROL_ABI_H
#define BC250_CONTROL_ABI_H
#define BC250_CONTROL_DEVICE_TYPE 0x8000U
/* Vendor function 0x800, FILE_READ_ACCESS=1, METHOD_BUFFERED=0. */
#define BC250_CONTROL_QUERY_METADATA ((0x8000U << 16) | (1U << 14) | (0x800U << 2))
#define BC250_CONTROL_ABI_VERSION 1U
typedef struct BC250_CONTROL_METADATA {
    unsigned int SizeBytes;
    unsigned int AbiVersion;
    unsigned int PolicyVersion;
    unsigned int GpuRuntimeEnabled;
    unsigned int HardwareCapabilities;
    unsigned int Reserved[3];
} BC250_CONTROL_METADATA;
typedef char BC250_CONTROL_ABI_SIZE_CHECK[(sizeof(BC250_CONTROL_METADATA) == 32) ? 1 : -1];
#endif
