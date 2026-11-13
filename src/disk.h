/* disk.h - block device inventory, surface scan and erase support. */
#ifndef UCTS_DISK_H
#define UCTS_DISK_H

#include "uefi.h"

#define DISK_MAX_DEVICES 16
#define DISK_SPEED_BUCKETS 64

typedef struct {
  EFI_HANDLE             Handle;
  EFI_BLOCK_IO_PROTOCOL *Io;
  EFI_DEVICE_PATH_PROTOCOL *Dp;
  char     PathText[96];
  char     Model[48];
  char     Serial[28];
  char     Firmware[12];
  UINT64   SizeBytes;
  UINT32   BlockSize;
  EFI_LBA  LastBlock;
  BOOLEAN  ReadOnly;
  BOOLEAN  Removable;
  BOOLEAN  LogicalPartition;
  BOOLEAN  MediaPresent;
  UINT32   Iface;        /* 0 block io only, 1 ATA, 2 NVMe */
  UINT32   HealthLevel;  /* 0 unknown, 1 ok, 2 warn, 3 crit */
  UINT32   NamespaceId;
  char     HealthText[72];
  UINT32   Temperature;
  UINT32   PowerOnHours;
  UINT64   BytesWritten;
  BOOLEAN  Scanned;
  UINT32   ScanErrors;
  UINT32   MinMbs;
  UINT32   MaxMbs;
  UINT32   AvgMbs;
  UINT8    SpeedProfile[DISK_SPEED_BUCKETS];
  UINT32   ProfileCount;
  BOOLEAN  BootVolume;
  BOOLEAN  PciValid;      /* controller address parsed from the device path */
  UINT8    PciDev;
  UINT8    PciFunc;
} DISK_DEV;

VOID      DiskEnumerate(VOID);
UINT32    DiskCount(VOID);
DISK_DEV *DiskAt(UINT32 index);
VOID      DiskRefreshHealth(DISK_DEV *disk);
EFI_STATUS DiskSurfaceScan(DISK_DEV *disk, UINT32 chunkBytes,
                           BOOLEAN (*progress)(DISK_DEV *disk, UINT32 permille, UINT32 mbs, UINT64 doneBytes));
EFI_STATUS DiskWritePattern(DISK_DEV *disk, UINT64 startByte, UINT64 bytes, UINT8 pattern);
EFI_STATUS DiskVerifyPattern(DISK_DEV *disk, UINT64 startByte, UINT64 bytes, UINT8 pattern,
                             UINT64 *mismatchOffset);
BOOLEAN    DiskIsBootDevice(DISK_DEV *disk);
const char *DiskIfaceName(UINT32 iface);

#endif
