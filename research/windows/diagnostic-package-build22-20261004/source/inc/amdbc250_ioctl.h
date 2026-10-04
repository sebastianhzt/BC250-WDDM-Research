/* Copyright (c) 2026 AMD BC-250 "Dream Drivers" Project -- Version 4.3
 * Selected passive ABI from Keshas-dev Dream Drivers and local PnP research.
 * Original notices retained; diagnostic-only selection, build 22.
 * No legacy/active commands are part of this interface.
 */
#ifndef _AMDBC250_IOCTL_H_
#define _AMDBC250_IOCTL_H_
#include <windef.h>
#define AMDBC250_DEVICE_PATH L"\\\\.\\AMDBC250DreamV43"
#define AMDBC250_DIAG_DEVICE_PATH L"\\\\.\\AMDBC250DreamResearchV1"
#define FILE_DEVICE_AMDBC250 0x8000
#define IOCTL_AMDBC250_PNP_PREFLIGHT 0x80000998UL
#define IOCTL_AMDBC250_W2P_PREFLIGHT 0x8000099CUL
#define IOCTL_AMDBC250_RESOURCE_PREFLIGHT 0x800049A0UL
#define IOCTL_AMDBC250_PCI_CONFIG_PREFLIGHT 0x800049A4UL
#pragma pack(push, 8)
/* WDM PnP-binding preflight. No pointers, resource addresses or DMA handles. */
#define AMDBC250_PNP_PREFLIGHT_VERSION               1
#define AMDBC250_PNP_STATE_NOT_STARTED               0
#define AMDBC250_PNP_STATE_STARTED                   1
#define AMDBC250_PNP_STATE_STOP_PENDING              2
#define AMDBC250_PNP_STATE_STOPPED                   3
#define AMDBC250_PNP_STATE_REMOVE_PENDING            4
#define AMDBC250_PNP_STATE_SURPRISE_REMOVED          5
#define AMDBC250_PNP_STATE_DELETED                   6

#define AMDBC250_PNP_SAFE_PASSIVE_LEVEL              0x00000001UL
#define AMDBC250_PNP_SAFE_ADD_DEVICE_CALLED          0x00000002UL
#define AMDBC250_PNP_SAFE_HARDWARE_ID_MATCH          0x00000004UL
#define AMDBC250_PNP_SAFE_FDO_CREATED                0x00000008UL
#define AMDBC250_PNP_SAFE_LOWER_ATTACHED             0x00000010UL
#define AMDBC250_PNP_SAFE_START_SUCCEEDED            0x00000020UL
#define AMDBC250_PNP_SAFE_PDO_REFERENCED             0x00000040UL
#define AMDBC250_PNP_SAFE_INTERLOCKS_OFF             0x00000080UL

#define AMDBC250_PNP_BLOCK_WRONG_IRQL                0x00000001UL
#define AMDBC250_PNP_BLOCK_NO_ADD_DEVICE             0x00000002UL
#define AMDBC250_PNP_BLOCK_HARDWARE_ID_MISMATCH      0x00000004UL
#define AMDBC250_PNP_BLOCK_NO_FDO                     0x00000008UL
#define AMDBC250_PNP_BLOCK_NO_LOWER_DEVICE           0x00000010UL
#define AMDBC250_PNP_BLOCK_NOT_STARTED               0x00000020UL
#define AMDBC250_PNP_BLOCK_PDO_REFERENCE_FAILED      0x00000040UL
#define AMDBC250_PNP_BLOCK_INTERLOCK_ON              0x00000080UL
#define AMDBC250_PNP_BLOCK_ACTIVE_DMA_NOT_AUTHORIZED 0x00000100UL
#define AMDBC250_PNP_BLOCK_REMOVING                  0x00000200UL

typedef struct _AMDBC250_IOCTL_PNP_PREFLIGHT {
    UINT32 Version;
    UINT32 StructSize;
    UINT32 DriverBuildId;
    INT32  Status;                  /* always STATUS_DEVICE_NOT_READY */
    UINT32 SafetyFlags;
    UINT32 BlockerFlags;
    UINT32 QueryIrql;
    UINT32 PnpState;
    UINT32 BindingGeneration;
    UINT32 AddDeviceCalls;
    UINT32 StartDeviceCalls;
    UINT32 QueryStopCalls;
    UINT32 StopDeviceCalls;
    UINT32 QueryRemoveCalls;
    UINT32 SurpriseRemoveCalls;
    UINT32 RemoveDeviceCalls;
    UINT32 BindingPresent;
    UINT32 FdoCreated;
    UINT32 LowerAttached;
    UINT32 HardwareIdMatched;
    UINT32 Started;
    UINT32 PdoReferenceAcquired;
    UINT32 FdoStackSize;
    UINT32 LowerStackSize;
    INT32  LastAddStatus;
    INT32  LastStartStatus;
    UINT32 HwInitGart;
    UINT32 HwInitVm;
    UINT32 HwInitSdmaRing;
    UINT32 Reserved[3];
} AMDBC250_IOCTL_PNP_PREFLIGHT, *PAMDBC250_IOCTL_PNP_PREFLIGHT;
C_ASSERT(sizeof(AMDBC250_IOCTL_PNP_PREFLIGHT) == 128);

/* A passive, fail-closed Windows VRAM-ownership contract. No addresses or
 * kernel pointers cross this ABI. Zero means no validated ownership, never
 * an implicit reservation. The IOCTL itself returns successfully so callers
 * can inspect Status and BlockerFlags. */
#define AMDBC250_W2P_PREFLIGHT_VERSION              1
#define AMDBC250_W2P_BLOCK_PNP_NOT_STARTED           0x00000001UL
#define AMDBC250_W2P_BLOCK_NO_BAR0_RESOURCE          0x00000002UL
#define AMDBC250_W2P_BLOCK_NO_WINDOWS_VRAM_OWNER     0x00000004UL
#define AMDBC250_W2P_BLOCK_GPU_MC_MAP_UNVALIDATED    0x00000008UL
#define AMDBC250_W2P_BLOCK_NO_WINDOWS_GART_OWNER     0x00000010UL
#define AMDBC250_W2P_BLOCK_INTERLOCK_UNKNOWN_OR_ON   0x00000020UL
#define AMDBC250_W2P_BLOCK_DMA_NOT_AUTHORIZED        0x00000040UL

typedef struct _AMDBC250_IOCTL_W2P_PREFLIGHT {
    UINT32 Version;
    UINT32 StructSize;
    UINT32 DriverBuildId;
    INT32  Status;                    /* STATUS_DEVICE_NOT_READY */
    UINT32 BlockerFlags;
    UINT32 PnpSafetyFlags;
    UINT32 PnpBlockerFlags;
    UINT32 PnpState;
    UINT32 PnpStarted;
    UINT32 HwInitGart;                /* 0xFFFFFFFF if missing/malformed */
    UINT32 HwInitVm;
    UINT32 HwInitSdmaRing;
    UINT32 Bar0TranslatedResourceKnown;
    UINT32 WindowsOwnedVramReservation;
    UINT32 GpuMcTranslationValidated;
    UINT32 GartOwnershipValidated;
    UINT32 ActiveControlIoctlsEnabled;
    UINT32 CanSubmitDma;
    UINT32 Reserved[6];
} AMDBC250_IOCTL_W2P_PREFLIGHT, *PAMDBC250_IOCTL_W2P_PREFLIGHT;
C_ASSERT(sizeof(AMDBC250_IOCTL_W2P_PREFLIGHT) == 96);

/* PnP resource descriptors are observations, not BAR identities or memory
 * ownership. No descriptor in this ABI authorizes mapping or DMA. */
#define AMDBC250_RESOURCE_PREFLIGHT_VERSION        2
#define AMDBC250_RESOURCE_MAX_MEMORY_ENTRIES       8
#define AMDBC250_RESOURCE_SNAPSHOT_NONE            0
#define AMDBC250_RESOURCE_SNAPSHOT_VALID           1
#define AMDBC250_RESOURCE_SNAPSHOT_REJECTED        2
#define AMDBC250_RESOURCE_BLOCK_NO_SNAPSHOT        0x00000001UL
#define AMDBC250_RESOURCE_BLOCK_REJECTED           0x00000002UL
#define AMDBC250_RESOURCE_BLOCK_BAR_UNIDENTIFIED   0x00000004UL
#define AMDBC250_RESOURCE_BLOCK_NO_VRAM_OWNER      0x00000008UL
#define AMDBC250_RESOURCE_BLOCK_DMA_NOT_AUTHORIZED 0x00000010UL

typedef struct _AMDBC250_RESOURCE_MEMORY_ENTRY {
    UINT64 RawStart;
    UINT64 TranslatedStart;
    UINT64 Length;
    UINT32 RawFlags;
    UINT32 TranslatedFlags;
    UINT32 DescriptorOrdinal; /* START_DEVICE list index, NOT a PCI BAR */
    UINT32 Reserved;
} AMDBC250_RESOURCE_MEMORY_ENTRY, *PAMDBC250_RESOURCE_MEMORY_ENTRY;
C_ASSERT(sizeof(AMDBC250_RESOURCE_MEMORY_ENTRY) == 40);

typedef struct _AMDBC250_IOCTL_RESOURCE_PREFLIGHT {
    UINT32 Version;
    UINT32 StructSize;
    UINT32 DriverBuildId;
    INT32  Status;                 /* always STATUS_DEVICE_NOT_READY */
    UINT32 BlockerFlags;
    UINT32 PnpState;
    UINT32 SnapshotState;
    UINT32 DescriptorCount;
    UINT32 MemoryCount;
    UINT32 BindingGeneration;
    AMDBC250_RESOURCE_MEMORY_ENTRY Memory[AMDBC250_RESOURCE_MAX_MEMORY_ENTRIES];
} AMDBC250_IOCTL_RESOURCE_PREFLIGHT, *PAMDBC250_IOCTL_RESOURCE_PREFLIGHT;
C_ASSERT(sizeof(AMDBC250_IOCTL_RESOURCE_PREFLIGHT) == 360);

/* The first 64 PCI configuration bytes are raw evidence only. In particular,
 * BAR base values do not identify PnP descriptor ownership or VRAM. */
#define AMDBC250_PCI_CONFIG_PREFLIGHT_VERSION 1
#define AMDBC250_PCI_CONFIG_HEADER_BYTES 64
#define AMDBC250_PCI_CONFIG_BLOCK_DMA_NOT_AUTHORIZED 0x00000001UL
#define AMDBC250_PCI_CONFIG_BLOCK_READ_DISABLED      0x00000002UL

typedef struct _AMDBC250_IOCTL_PCI_CONFIG_PREFLIGHT {
    UINT32 Version;
    UINT32 StructSize;
    UINT32 DriverBuildId;
    INT32  ReadStatus;
    UINT32 PnpState;
    UINT32 BindingGeneration;
    UINT32 BytesRead;
    UINT32 BlockerFlags;
    UCHAR  Header[AMDBC250_PCI_CONFIG_HEADER_BYTES];
} AMDBC250_IOCTL_PCI_CONFIG_PREFLIGHT,
  *PAMDBC250_IOCTL_PCI_CONFIG_PREFLIGHT;
C_ASSERT(sizeof(AMDBC250_IOCTL_PCI_CONFIG_PREFLIGHT) == 96);

#pragma pack(pop)
#endif
