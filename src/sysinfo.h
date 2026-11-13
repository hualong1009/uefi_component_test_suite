/* sysinfo.h - platform inventory gathered from SMBIOS, CPUID and the memory map. */
#ifndef UCTS_SYSINFO_H
#define UCTS_SYSINFO_H

#include "uefi.h"

#define SYS_MAX_DIMMS 32

typedef struct {
  UINT16  SmbiosHandle;
  UINT8   Socket;          /* physical socket index, 0 based */
  UINT8   Channel;         /* channel index inside the socket, 0 based */
  UINT8   Slot;            /* slot index on the channel, 0 based */
  UINT64  SizeBytes;
  UINT16  Speed;           /* MT/s as reported */
  UINT16  ConfiguredSpeed;
  UINT16  TotalWidth;
  UINT16  DataWidth;
  UINT8   FormFactor;
  UINT8   MemoryType;
  UINT8   Technology;
  UINT16  TypeDetail;
  UINT8   Rank;            /* SMBIOS type 17 rank count, 0 = not reported */
  char    Locator[48];
  char    BankLocator[40];
  char    Manufacturer[40];
  char    Serial[40];
  char    PartNumber[44];
  BOOLEAN Present;
  BOOLEAN Ecc;
  UINT32  VddMv;          /* configured (or minimum) voltage from SMBIOS   */
  UINT32  VddqMv;
  UINT32  VppMv;
} DIMM_INFO;

/* Derived module layout: SMBIOS reports bus widths and rank count but not the
   package of every DRAM device, so the device count comes from those widths. */
typedef struct {
  UINT32  Ranks;
  UINT32  DeviceWidth;    /* bits per DRAM device, assumed x8 when unknown */
  UINT32  DevicesPerRank;
  UINT32  TotalDevices;
  UINT32  FrontDevices;   /* rank 0 sits on the front side                  */
  UINT32  BackDevices;
  BOOLEAN HasRcd;         /* registered DIMM: RCD buffer in the middle      */
  BOOLEAN HasPmic;        /* DDR5 on-module PMIC + SPD hub                  */
  BOOLEAN WidthAssumed;   /* TRUE when the device width was not derivable   */
  UINT32  VddMv;
  UINT32  VddqMv;
  UINT32  VppMv;
  char    Note[48];
} DIMM_GEO;

typedef struct {
  /* SMBIOS */
  BOOLEAN SmbiosAvailable;
  UINT8   SmbiosMajor, SmbiosMinor;
  char    BiosVendor[48];
  char    BiosVersion[48];
  char    BiosDate[24];
  char    SysVendor[48];
  char    SysProduct[56];
  char    SysVersion[32];
  char    SysSerial[40];
  char    BoardVendor[48];
  char    BoardProduct[56];
  char    BoardVersion[32];
  UINT64  BiosCharacteristics;

  /* CPU */
  UINT32  CpuFamily;
  UINT32  CpuModel;
  UINT32  CpuStepping;
  UINT32  CpuBrandIndex;
  char    CpuBrand[64];
  char    CpuVendor[16];
  UINT32  MaxBasicLeaf;
  UINT32  MaxExtLeaf;
  UINT32  PhysAddrBits;
  UINT32  VirtAddrBits;
  UINT32  CacheLineBytes;
  char    CpuSocket[32];
  UINT32  CpuBaseMhz;
  UINT32  CpuMaxMhz;
  UINT32  Cores;
  UINT32  Threads;
  UINT32  LogicalCpus;
  UINT64  L1Bytes, L2Bytes, L3Bytes;
  UINT32  FeaturesEbx, FeaturesEcx, FeaturesEdx;      /* leaf 1 */
  UINT32  ExtEbx, ExtEcx, ExtEdx;                    /* leaf 0x80000001 */
  UINT32  Leaf7Ebx, Leaf7Ecx, Leaf7Edx;
  BOOLEAN HasLeaf7;
  UINT32  CpuPackageCount;

  /* Memory */
  UINT64  InstalledRam;
  UINT64  MaxCapacity;      /* from SMBIOS */
  UINT64  UsableRam;         /* conventional + loader + boot services */
  UINT64  TestableRam;       /* conventional memory only */
  UINT32  MemoryMapEntries;
  UINT32  DimmCount;
  UINT32  DimmPresentCount;
  UINT32  SmtPerCore;
  DIMM_INFO Dimms[SYS_MAX_DIMMS];
  UINT32  SocketCount;
  UINT32  ChannelCount;
  UINT8   DominantMemType;
  UINT16  DominantMemSpeed;

  /* Runtime */
  UINT64  TscHz;
  UINT32  FirmwareRevision;
  char    FirmwareVendor[40];

  /* Boot volume (the volume this application was loaded from) */
  EFI_FILE_PROTOCOL *BootRoot;
  EFI_DEVICE_PATH_PROTOCOL *BootVolumeDp;
  char    BootVolumeName[24];
} SYS_INFO;

VOID    SysCollect(SYS_INFO *s, EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *ST);
VOID    SysDimmGeometry(const DIMM_INFO *dimm, DIMM_GEO *geo);
/* slot index of the which-th installed DIMM, 0xFFFFFFFF when out of range:
   Dimms[] holds every slot, so counting installed modules means walking it */
UINT32  SysPresentDimm(const SYS_INFO *s, UINT32 which);
UINT64  SysTscHz(VOID);
UINT64  SysTsc(VOID);
UINT64  SysMsSince(UINT64 startTsc);
VOID    SysDelayMs(UINTN ms);
const char *SysMemTypeName(UINT8 type);
const char *SysFormFactorName(UINT8 ff);
const char *SysCoreBrandName(VOID);

/* convenience: find the root of the volume this image was loaded from */
EFI_STATUS SysOpenBootVolume(SYS_INFO *s, EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *ST);

#endif
