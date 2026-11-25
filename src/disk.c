/* disk.c - block device inventory through EFI_BLOCK_IO, ATA/NVMe pass-thru health. */
#include "disk.h"
#include "ui.h"
#include "results.h"
#include "debug.h"

/* ATA pass-thru protocol (UEFI 2.10, 20.4) */
typedef struct {
  UINT32 Attributes;
  UINT32 IoAlign;
} EFI_ATA_PASS_THRU_MODE;

typedef struct {
  UINT8 Reserved1[2];
  UINT8 AtaCommand;
  UINT8 AtaFeatures;
  UINT8 AtaSectorNumber;
  UINT8 AtaCylinderLow;
  UINT8 AtaCylinderHigh;
  UINT8 AtaDeviceHead;
  UINT8 AtaSectorNumberExp;
  UINT8 AtaCylinderLowExp;
  UINT8 AtaCylinderHighExp;
  UINT8 AtaFeaturesExp;
  UINT8 AtaSectorCount;
  UINT8 AtaSectorCountExp;
  UINT8 Reserved2[6];
} EFI_ATA_COMMAND_BLOCK;

typedef struct {
  UINT8 Reserved1[2];
  UINT8 AtaStatus;
  UINT8 AtaError;
  UINT8 AtaSectorNumber;
  UINT8 AtaCylinderLow;
  UINT8 AtaCylinderHigh;
  UINT8 AtaDeviceHead;
  UINT8 AtaSectorNumberExp;
  UINT8 AtaCylinderLowExp;
  UINT8 AtaCylinderHighExp;
  UINT8 Reserved2;
  UINT8 AtaSectorCount;
  UINT8 AtaSectorCountExp;
  UINT8 Reserved3[6];
} EFI_ATA_STATUS_BLOCK;

typedef struct {
  EFI_ATA_STATUS_BLOCK  *Asb;
  EFI_ATA_COMMAND_BLOCK *Acb;
  UINT64                 Timeout;          /* 100ns units */
  VOID                  *InDataBuffer;
  VOID                  *OutDataBuffer;
  UINT32                 InTransferLength;
  UINT32                 OutTransferLength;
  UINT8                  Protocol;
  UINT8                  Length;
} EFI_ATA_PASS_THRU_COMMAND_PACKET;

#define ATA_PROTOCOL_PIO_DATA_IN   0x04
#define ATA_LENGTH_BYTES           0x80
#define ATA_LENGTH_SECTOR_COUNT    0x20

typedef struct EFI_ATA_PASS_THRU_PROTOCOL {
  EFI_ATA_PASS_THRU_MODE *Mode;
  EFI_STATUS (EFIAPI *PassThru)(struct EFI_ATA_PASS_THRU_PROTOCOL *This, UINT16 Port,
                                UINT16 PortMultiplierPort,
                                EFI_ATA_PASS_THRU_COMMAND_PACKET *Packet,
                                EFI_EVENT Event);
  EFI_STATUS (EFIAPI *GetNextPort)(struct EFI_ATA_PASS_THRU_PROTOCOL *This, UINT16 *Port);
  EFI_STATUS (EFIAPI *GetNextDevice)(struct EFI_ATA_PASS_THRU_PROTOCOL *This, UINT16 Port,
                                     UINT16 *PortMultiplierPort);
  EFI_STATUS (EFIAPI *BuildDevicePath)(struct EFI_ATA_PASS_THRU_PROTOCOL *This, UINT16 Port,
                                       UINT16 PortMultiplierPort, EFI_DEVICE_PATH_PROTOCOL **DevicePath);
  EFI_STATUS (EFIAPI *GetDevice)(struct EFI_ATA_PASS_THRU_PROTOCOL *This,
                                 EFI_DEVICE_PATH_PROTOCOL *DevicePath, UINT16 *Port,
                                 UINT16 *PortMultiplierPort);
  EFI_STATUS (EFIAPI *ResetPort)(struct EFI_ATA_PASS_THRU_PROTOCOL *This, UINT16 Port);
  EFI_STATUS (EFIAPI *ResetDevice)(struct EFI_ATA_PASS_THRU_PROTOCOL *This, UINT16 Port,
                                   UINT16 PortMultiplierPort);
} EFI_ATA_PASS_THRU_PROTOCOL;

/* NVMe pass-thru protocol (UEFI 2.10, 20.12) */
typedef struct {
  UINT32 Opcode : 8;
  UINT32 FusedOperation : 2;
  UINT32 Reserved : 22;
} NVME_CDW0;

typedef struct {
  NVME_CDW0 Cdw0;
  UINT8     Flags;
  UINT32    Nsid;
  UINT32    Cdw2;
  UINT32    Cdw3;
  UINT32    Cdw10;
  UINT32    Cdw11;
  UINT32    Cdw12;
  UINT32    Cdw13;
  UINT32    Cdw14;
  UINT32    Cdw15;
} EFI_NVM_EXPRESS_COMMAND;

typedef struct {
  UINT32 DW0;
  UINT32 DW1;
  UINT32 DW2;
  UINT32 DW3;
} EFI_NVM_EXPRESS_COMPLETION;

typedef struct {
  UINT64                      CommandTimeout;
  VOID                       *TransferBuffer;
  UINT32                      TransferLength;
  VOID                       *MetadataBuffer;
  UINT32                      MetadataLength;
  UINT8                       QueueType;
  EFI_NVM_EXPRESS_COMMAND    *NvmeCmd;
  EFI_NVM_EXPRESS_COMPLETION *NvmeCompletion;
} EFI_NVM_EXPRESS_PASS_THRU_COMMAND_PACKET;

typedef struct {
  UINT32 Attributes;
  UINT32 IoAlign;
  UINT32 NvmeVersion;
} EFI_NVM_EXPRESS_PASS_THRU_MODE;

typedef struct EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL {
  EFI_NVM_EXPRESS_PASS_THRU_MODE *Mode;
  EFI_STATUS (EFIAPI *PassThru)(struct EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL *This,
                                UINT32 NamespaceId,
                                EFI_NVM_EXPRESS_PASS_THRU_COMMAND_PACKET *Packet,
                                EFI_EVENT Event);
  EFI_STATUS (EFIAPI *GetNextNamespace)(struct EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL *This,
                                        UINT32 *NamespaceId);
  EFI_STATUS (EFIAPI *BuildDevicePath)(struct EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL *This,
                                       UINT32 NamespaceId,
                                       EFI_DEVICE_PATH_PROTOCOL **DevicePath);
  EFI_STATUS (EFIAPI *GetNamespace)(struct EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL *This,
                                    EFI_DEVICE_PATH_PROTOCOL *DevicePath,
                                    UINT32 *NamespaceId);
} EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL;

static VOID TrimTails(char *s, UINT32 size)
{
  UINT32 n = 0, end = 0;
  while (n < size && s[n]) {
    UINT8 c = (UINT8)s[n];
    if (c < 0x20 || c > 0x7E) {
      break;
    }
    if (c != (UINT8)' ') {
      end = n + 1;
    }
    n++;
  }
  s[end] = 0;
}

static DISK_DEV gDisks[DISK_MAX_DEVICES];
static UINT32   gDiskCount;

/* ---------------------------------------------------------- path to text */
static UINTN DpNodeLength(const EFI_DEVICE_PATH_PROTOCOL *node)
{
  return (UINTN)node->Length[0] | ((UINTN)node->Length[1] << 8);
}

static VOID AppendText(char *dst, UINTN cap, const char *text)
{
  GfxAppendStr(dst, cap, text);
}

static VOID FormatDevicePath(EFI_DEVICE_PATH_PROTOCOL *dp, char *out, UINTN cap)
{
  out[0] = 0;
  while (dp && dp->Type != 0x7F && DpNodeLength(dp) > 0) {
    char node[64];
    UINT8 type = dp->Type;
    UINT8 sub = dp->SubType;
    if (out[0]) {
      AppendText(out, cap, "/");
    }
    if (type == 0x01 && sub == 0x01) {  /* PCI */
      UINT8 *d = (UINT8 *)dp;
      /* PCI node: Header, Function, Device */
      UiStrPrintf(node, sizeof(node), "PCI(%02x|%02x)", d[5], d[4]);
      AppendText(out, cap, node);
    } else if (type == 0x01 && sub == 0x03) { /* PC Card? */
      AppendText(out, cap, "PCCARD");
    } else if (type == 0x02 && sub == 0x01) { /* ACPI */
      AppendText(out, cap, "ACPI");
    } else if (type == 0x03 && sub == 0x05) { /* USB */
      AppendText(out, cap, "USB");
    } else if (type == 0x03 && sub == 0x12) { /* SATA */
      UINT16 port = *(UINT16 *)((UINT8 *)dp + 4);
      UINT16 pm = *(UINT16 *)((UINT8 *)dp + 6);
      UiStrPrintf(node, sizeof(node), "SATA(%x.%x)", port, pm);
      AppendText(out, cap, node);
    } else if (type == 0x03 && sub == 0x17) { /* NVMe namespace */
      UINT32 nsid = *(UINT32 *)((UINT8 *)dp + 8);
      UiStrPrintf(node, sizeof(node), "NVMe(%d)", nsid);
      AppendText(out, cap, node);
    } else if (type == 0x04 && sub == 0x01) { /* HD */
      UINT32 part = *(UINT32 *)((UINT8 *)dp + 16);
      UiStrPrintf(node, sizeof(node), "HD(%d)", part);
      AppendText(out, cap, node);
    } else if (type == 0x04 && sub == 0x06) { /* SCSI */
      UiStrPrintf(node, sizeof(node), "SCSI(%d)", *(UINT16 *)((UINT8 *)dp + 4));
      AppendText(out, cap, node);
    } else if (type == 0x04 && sub == 0x07) { /* CDROM */
      AppendText(out, cap, "CDROM");
    } else if (type == 0x04 && sub == 0x04) { /* FilePath */
      UINTN i = 0;
      CHAR16 *p = (CHAR16 *)((UINT8 *)dp + 4);
      while (*p && i + 1 < sizeof(node)) {
        node[i++] = (*p < 0x80) ? (char)*p : '?';
        p++;
      }
      node[i] = 0;
      if (node[0] == '\\' || node[0] == '/') {
        AppendText(out, cap, &node[1]);
      } else {
        AppendText(out, cap, node);
      }
    } else if (type == 0x05 && sub == 0x01) { /* MBR */
      AppendText(out, cap, "MBR");
    } else if (type == 0x03 && sub == 0x0A) {
      AppendText(out, cap, "VENDOR");
    } else {
      UiStrPrintf(node, sizeof(node), "%02x:%02x", type, sub);
      AppendText(out, cap, node);
    }
    dp = (EFI_DEVICE_PATH_PROTOCOL *)((UINT8 *)dp + DpNodeLength(dp));
  }
}

/* Storage device paths carry the controller node (PCI, SATA, NVMe, ...), so the
   last PCI node identifies the controller for the topology view even when the
   bus number is not part of the path. */
static VOID DiskPathPci(EFI_DEVICE_PATH_PROTOCOL *dp, UINT8 *dev, UINT8 *func, BOOLEAN *valid)
{
  UINT32 guard = 0;
  *valid = FALSE;
  while (dp && dp->Type != 0x7F && guard++ < 32) {
    UINT16 len = (UINT16)(dp->Length[0] | ((UINT16)dp->Length[1] << 8));
    if (len < 4) {
      break;
    }
    if (dp->Type == 0x01 && dp->SubType == 0x01) {
      UINT8 *raw = (UINT8 *)dp;
      *func = raw[4];
      *dev = raw[5];
      *valid = TRUE;
    }
    dp = (EFI_DEVICE_PATH_PROTOCOL *)((UINT8 *)dp + len);
  }
}

/* The pass-thru protocols expect the trailing messaging node, not the whole path. */
static EFI_DEVICE_PATH_PROTOCOL *PathLastNode(EFI_DEVICE_PATH_PROTOCOL *dp)
{
  EFI_DEVICE_PATH_PROTOCOL *node = dp;
  UINT32 guard = 0;

  if (dp == 0) {
    return 0;
  }
  while (node->Type != 0x7F && guard++ < 32) {
    UINT16 len = (UINT16)(node->Length[0] | ((UINT16)node->Length[1] << 8));
    EFI_DEVICE_PATH_PROTOCOL *next;
    if (len < 4) {
      break;
    }
    next = (EFI_DEVICE_PATH_PROTOCOL *)((UINT8 *)node + len);
    if (next->Type == 0x7F) {
      break;
    }
    node = next;
  }
  return node;
}

/* The pass-thru protocols live on the controller handle, not on the namespace
   or block-io child, so pick the instance that accepts this device path. */
static EFI_ATA_PASS_THRU_PROTOCOL *FindAtaPassThru(DISK_DEV *disk, UINT16 *port, UINT16 *pmport)
{
  EFI_HANDLE *handles = 0;
  UINTN count = 0, i;
  EFI_ATA_PASS_THRU_PROTOCOL *found = 0;

  if (EFI_ERROR(gApp.BS->LocateHandleBuffer(2, (EFI_GUID *)&gEfiAtaPassThruProtocolGuid, 0,
                                            &count, &handles))) {
    DbgStr("ata: no pass-thru protocol");
    return 0;
  }
  DbgStr("ata: pass-thru handles=");
  DbgDec(count);
  DbgStr(" ");
  for (i = 0; i < count && found == 0; i++) {
    EFI_ATA_PASS_THRU_PROTOCOL *ata = 0;
    UINT16 p = 0, pm = 0xFFFF;
    if (EFI_ERROR(gApp.BS->HandleProtocol(handles[i], (EFI_GUID *)&gEfiAtaPassThruProtocolGuid,
                                          (VOID **)&ata)) || ata == 0) {
      continue;
    }
    if (disk->Dp && ata->GetDevice) {
      EFI_STATUS st = ata->GetDevice(ata, PathLastNode(disk->Dp), &p, &pm);
      DbgStr("ata: GetDevice=");
      DbgHex((UINT64)st);
      DbgStr(" ");
      if (!EFI_ERROR(st)) {
        found = ata;
        *port = p;
        *pmport = pm;
      }
    }
  }
  gApp.BS->FreePool(handles);
  return found;
}

static EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL *FindNvmePassThru(DISK_DEV *disk)
{
  EFI_HANDLE *handles = 0;
  UINTN count = 0, i;
  EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL *found = 0;

  if (EFI_ERROR(gApp.BS->LocateHandleBuffer(2, (EFI_GUID *)&gEfiNvmExpressPassThruProtocolGuid, 0,
                                            &count, &handles))) {
    DbgStr("nvme: no pass-thru protocol");
    return 0;
  }
  DbgStr("nvme: pass-thru handles=");
  DbgDec(count);
  DbgStr(" ");
  for (i = 0; i < count && found == 0; i++) {
    EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL *nvme = 0;
    UINT32 nsid = 0;
    if (EFI_ERROR(gApp.BS->HandleProtocol(handles[i], (EFI_GUID *)&gEfiNvmExpressPassThruProtocolGuid,
                                          (VOID **)&nvme)) || nvme == 0) {
      continue;
    }
    if (disk->Dp && nvme->GetNamespace) {
      EFI_STATUS st = nvme->GetNamespace(nvme, PathLastNode(disk->Dp), &nsid);
      DbgStr("nvme: GetNamespace=");
      DbgHex((UINT64)st);
      DbgStr(" nsid=");
      DbgDec(nsid);
      if (!EFI_ERROR(st)) {
        found = nvme;
        disk->NamespaceId = nsid;
      }
    }
  }
  gApp.BS->FreePool(handles);
  return found;
}

/* ------------------------------------------------------------ SMART utils */
/* attributes worth keeping: reallocated / power-on hours / power cycles /
   reported uncorrectable / temperature / pending / offline uncorrectable /
   cable CRC / ssd life left / percentage used */
static const UINT8 gAtaAttrIds[DISK_ATTR_MAX] = {
  5, 9, 12, 187, 194, 197, 198, 199, 231, 202
};

const DISK_ATTR *DiskAttr(const DISK_DEV *disk, UINT8 id)
{
  UINT32 i;
  if (disk == 0) {
    return 0;
  }
  for (i = 0; i < disk->AttrCount && i < DISK_ATTR_MAX; i++) {
    if (disk->Attrs[i].Id == id) {
      return &disk->Attrs[i];
    }
  }
  return 0;
}

static UINT32 AttrValue(const DISK_DEV *disk, UINT8 id)
{
  const DISK_ATTR *a = DiskAttr(disk, id);
  return a ? a->Value : 0;
}

static UINT64 AttrRaw(const DISK_DEV *disk, UINT8 id)
{
  const DISK_ATTR *a = DiskAttr(disk, id);
  return a ? a->Raw : 0;
}

/* one compact line with the four attributes people actually look at */
VOID DiskSmartSummary(const DISK_DEV *disk, char *buf, UINTN cap)
{
  buf[0] = 0;
  if (disk == 0) {
    return;
  }
  if (disk->AttrCount == 0) {
    return;
  }
  UiStrPrintf(buf, cap, "5=%llu 197=%llu 198=%llu 199=%llu",
              (unsigned long long)AttrRaw(disk, 5), (unsigned long long)AttrRaw(disk, 197),
              (unsigned long long)AttrRaw(disk, 198), (unsigned long long)AttrRaw(disk, 199));
  if (AttrRaw(disk, 187)) {
    UINTN n = GfxStrLen(buf);
    UiStrPrintf(buf + n, cap - n, " 187=%llu", (unsigned long long)AttrRaw(disk, 187));
  }
}

static UINT64 LeBytes(const UINT8 *p, UINT32 n)
{
  UINT64 v = 0;
  UINT32 i;
  for (i = 0; i < n && i < 8; i++) {
    v |= (UINT64)p[i] << (8 * i);
  }
  return v;
}

/* ------------------------------------------------------------- ATA SMART */
static UINT8 gAtaBuffer[512] __attribute__((aligned(64)));
static EFI_ATA_STATUS_BLOCK gAtaStatus;

static EFI_STATUS AtaIdentify(DISK_DEV *disk)
{
  EFI_ATA_PASS_THRU_PROTOCOL *ata = 0;
  EFI_ATA_PASS_THRU_COMMAND_PACKET packet;
  EFI_ATA_COMMAND_BLOCK acb;
  EFI_STATUS status;
  UINT16 *id;
  UINT16 port = 0, pmport = 0xFFFF;
  UINTN i;

  ata = FindAtaPassThru(disk, &port, &pmport);
  if (ata == 0) {
    return EFI_NOT_FOUND;
  }
  gApp.BS->SetMem(&packet, sizeof(packet), 0);
  gApp.BS->SetMem(&acb, sizeof(acb), 0);
  gApp.BS->SetMem(&gAtaStatus, sizeof(gAtaStatus), 0);
  gApp.BS->SetMem(gAtaBuffer, sizeof(gAtaBuffer), 0);
  packet.Asb = &gAtaStatus;
  packet.Acb = &acb;
  packet.Timeout = 50000000ULL;   /* 5 s */
  packet.InDataBuffer = gAtaBuffer;
  packet.InTransferLength = sizeof(gAtaBuffer);
  packet.Protocol = ATA_PROTOCOL_PIO_DATA_IN;
  packet.Length = ATA_LENGTH_BYTES | ATA_LENGTH_SECTOR_COUNT;
  packet.Acb->AtaCommand = 0xEC;  /* IDENTIFY DEVICE */
  status = ata->PassThru(ata, port, pmport, &packet, 0);
  if (EFI_ERROR(status)) {
    return status;
  }
  id = (UINT16 *)gAtaBuffer;
  {
    char *model = disk->Model;
    char *serial = disk->Serial;
    char *fw = disk->Firmware;
    for (i = 0; i < 20; i++) {
      UINT16 w = id[27 + i];
      model[i * 2] = (char)(w >> 8);
      model[i * 2 + 1] = (char)(w & 0xFF);
    }
    model[40] = 0;
    model[41] = 0;
    for (i = 0; i < 10; i++) {
      UINT16 w = id[10 + i];
      serial[i * 2] = (char)(w >> 8);
      serial[i * 2 + 1] = (char)(w & 0xFF);
    }
    serial[20] = 0;
    for (i = 0; i < 4; i++) {
      UINT16 w = id[23 + i];
      fw[i * 2] = (char)(w >> 8);
      fw[i * 2 + 1] = (char)(w & 0xFF);
    }
    fw[8] = 0;
  }
  TrimTails(disk->Model, sizeof(disk->Model));
  TrimTails(disk->Serial, sizeof(disk->Serial));
  TrimTails(disk->Firmware, sizeof(disk->Firmware));
  disk->Iface = 1;

  /* SMART READ DATA (0xB0, feature 0xD0) */
  gApp.BS->SetMem(&packet, sizeof(packet), 0);
  gApp.BS->SetMem(&acb, sizeof(acb), 0);
  gApp.BS->SetMem(&gAtaStatus, sizeof(gAtaStatus), 0);
  gApp.BS->SetMem(gAtaBuffer, sizeof(gAtaBuffer), 0);
  packet.Asb = &gAtaStatus;
  packet.Acb = &acb;
  packet.Timeout = 50000000ULL;
  packet.InDataBuffer = gAtaBuffer;
  packet.InTransferLength = sizeof(gAtaBuffer);
  packet.Protocol = ATA_PROTOCOL_PIO_DATA_IN;
  packet.Length = ATA_LENGTH_BYTES | ATA_LENGTH_SECTOR_COUNT;
  packet.Acb->AtaCommand = 0xB0;
  packet.Acb->AtaFeatures = 0xD0;
  packet.Acb->AtaSectorNumber = 0x4F;
  packet.Acb->AtaCylinderLow = 0xC2;
  packet.Acb->AtaCylinderHigh = 0xA0;
  if (!EFI_ERROR(ata->PassThru(ata, port, pmport, &packet, 0))) {
    UINT8 *smart = gAtaBuffer;
    UINT32 temp = smart[194] ? smart[194] : smart[190];
    UINT32 k;
    disk->Temperature = temp;
    disk->PowerOnHours = (UINT32)smart[113] | ((UINT32)smart[114] << 8);
    /* 30 attribute entries of 12 bytes each start at offset 2 */
    disk->AttrCount = 0;
    for (k = 0; k < DISK_ATTR_MAX; k++) {
      UINT32 i;
      for (i = 0; i < 30; i++) {
        UINT8 *a = smart + 2 + i * 12;
        if (a[0] == 0) {
          break;
        }
        if (a[0] != gAtaAttrIds[k]) {
          continue;
        }
        disk->Attrs[disk->AttrCount].Id = a[0];
        disk->Attrs[disk->AttrCount].Value = a[3];
        disk->Attrs[disk->AttrCount].Worst = a[4];
        disk->Attrs[disk->AttrCount].Raw = LeBytes(a + 5, 6);
        disk->AttrCount++;
        break;
      }
    }
    disk->PowerCycles = (UINT32)AttrRaw(disk, 12);
    disk->LifePercent = DISK_LIFE_UNKNOWN;
    if (DiskAttr(disk, 231)) {
      disk->LifePercent = AttrValue(disk, 231) > 100 ? 100 : AttrValue(disk, 231);
    } else if (DiskAttr(disk, 202)) {
      disk->LifePercent = AttrValue(disk, 202) > 100 ? 100 : AttrValue(disk, 202);
    }
    if (smart[0] <= 0x02) {
      disk->HealthLevel = (smart[0] == 0) ? 1 : 2;
      UiStrPrintf(disk->HealthText, sizeof(disk->HealthText),
                  "SMART %s · %uC · 通电 %u h",
                  smart[0] == 0 ? L("通过") : L("预警"), (UINT32)temp, disk->PowerOnHours);
    } else {
      disk->HealthLevel = 1;
      UiStrPrintf(disk->HealthText, sizeof(disk->HealthText),
                  "SMART 可用 · %uC · 通电 %u h", (UINT32)temp, disk->PowerOnHours);
    }
  } else {
    disk->HealthLevel = 1;
    UiStrPrintf(disk->HealthText, sizeof(disk->HealthText), "ATA 设备已识别（SMART 不可读）");
  }
  return EFI_SUCCESS;
}

/* -------------------------------------------------------------- NVMe ID */
static UINT8 gNvmeIdentifyBuf[4096] __attribute__((aligned(4096)));
static UINT8 gNvmeSmartBuf[4096] __attribute__((aligned(4096)));

static EFI_STATUS NvmeIdentify(DISK_DEV *disk)
{
  EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL *nvme = 0;
  EFI_NVM_EXPRESS_PASS_THRU_COMMAND_PACKET packet;
  EFI_NVM_EXPRESS_COMMAND cmd;
  EFI_NVM_EXPRESS_COMPLETION completion;
  EFI_STATUS status;
  UINTN i;

  nvme = FindNvmePassThru(disk);
  if (nvme == 0) {
    return EFI_NOT_FOUND;
  }
  gApp.BS->SetMem(&packet, sizeof(packet), 0);
  gApp.BS->SetMem(&cmd, sizeof(cmd), 0);
  gApp.BS->SetMem(&completion, sizeof(completion), 0);
  gApp.BS->SetMem(gNvmeIdentifyBuf, sizeof(gNvmeIdentifyBuf), 0);
  packet.CommandTimeout = 5000000;   /* ms */
  packet.TransferBuffer = gNvmeIdentifyBuf;
  packet.TransferLength = sizeof(gNvmeIdentifyBuf);
  packet.QueueType = 0;              /* admin queue */
  packet.NvmeCmd = &cmd;
  packet.NvmeCompletion = &completion;
  cmd.Cdw0.Opcode = 0x06;            /* Identify */
  cmd.Flags = 0x04;                  /* CDW10_VALID */
  cmd.Nsid = 0;
  cmd.Cdw10 = 0x01;                  /* CNS: controller structure */
  status = nvme->PassThru(nvme, 0, &packet, 0);
  disk->Iface = 2;
  {
    char b[64];
    UiStrPrintf(b, sizeof(b), "nvme identify st=%X dw3=%X ", (UINT32)status, completion.DW3);
    DbgStr(b);
  }
  if (!EFI_ERROR(status)) {
    UINT8 *id = gNvmeIdentifyBuf;
    UINTN mn = 24, sn = 4, fr = 64;
    for (i = 0; i < 40; i++) {
      disk->Model[i] = (char)id[mn + i];
    }
    disk->Model[40] = 0;
    for (i = 0; i < 20; i++) {
      disk->Serial[i] = (char)id[sn + i];
    }
    disk->Serial[20] = 0;
    for (i = 0; i < 8; i++) {
      disk->Firmware[i] = (char)id[fr + i];
    }
    disk->Firmware[8] = 0;
  }
  TrimTails(disk->Model, sizeof(disk->Model));
  TrimTails(disk->Serial, sizeof(disk->Serial));
  TrimTails(disk->Firmware, sizeof(disk->Firmware));
  /* SMART / health information log page (LID 0x02) */
  gApp.BS->SetMem(&packet, sizeof(packet), 0);
  gApp.BS->SetMem(&cmd, sizeof(cmd), 0);
  gApp.BS->SetMem(&completion, sizeof(completion), 0);
  gApp.BS->SetMem(gNvmeSmartBuf, sizeof(gNvmeSmartBuf), 0);
  packet.CommandTimeout = 5000000;
  packet.TransferBuffer = gNvmeSmartBuf;
  packet.TransferLength = 512;       /* 128 dwords as declared in NUMDL */
  packet.QueueType = 0;
  packet.NvmeCmd = &cmd;
  packet.NvmeCompletion = &completion;
  cmd.Cdw0.Opcode = 0x02;            /* Get Log Page */
  cmd.Flags = 0x04;                  /* CDW10_VALID */
  cmd.Nsid = 0xFFFFFFFF;
  cmd.Cdw10 = 0x02 | (127u << 16);   /* LID 2, NUMDL 127 dwords */
  {
    EFI_STATUS slog = nvme->PassThru(nvme, 0xFFFFFFFF, &packet, 0);
    char m[72];
    UiStrPrintf(m, sizeof(m), "smart st=%X dw3=%X ", (UINT32)slog, completion.DW3);
    DbgStr(m);
  }
  if (1) {
    UINT8 *smart = gNvmeSmartBuf;
    UINT32 tempK = (UINT32)smart[1] | ((UINT32)smart[2] << 8);
    UINT8 flags = smart[0];
    UINT32 usedPct = smart[5];
    UINT64 used = (UINT64)smart[48] | ((UINT64)smart[49] << 8) | ((UINT64)smart[50] << 16)
                  | ((UINT64)smart[51] << 24);
    used |= ((UINT64)smart[52] | ((UINT64)smart[53] << 8) | ((UINT64)smart[54] << 16)
             | ((UINT64)smart[55] << 24)) << 32;
    disk->Temperature = tempK > 273 ? tempK - 273 : 0;
    disk->PowerOnHours = (UINT32)smart[128] | ((UINT32)smart[129] << 8)
                         | ((UINT32)smart[130] << 16) | ((UINT32)smart[131] << 24);
    disk->BytesWritten = used * 512000ULL; /* data unit = 512000 bytes */
    disk->BytesRead = LeBytes(smart + 32, 8) * 512000ULL;
    disk->LifePercent = usedPct >= 100 ? 0 : 100 - usedPct;
    disk->SparePercent = smart[3];
    disk->SpareThreshold = smart[4];
    disk->PowerCycles = (UINT32)LeBytes(smart + 112, 8);
    disk->UnsafeShutdowns = (UINT32)LeBytes(smart + 144, 8);
    disk->MediaErrors = (UINT32)LeBytes(smart + 160, 8);
    disk->AttrCount = 0;
    disk->HealthLevel = (flags & 0x01) ? 2 : 1;
    UiStrPrintf(disk->HealthText, sizeof(disk->HealthText), "NVMe %s · %uC · 通电 %u h",
                (flags & 0x01) ? L("警告") : L("正常"), disk->Temperature, disk->PowerOnHours);
  } else {
    disk->HealthLevel = 1;
    UiStrPrintf(disk->HealthText, sizeof(disk->HealthText), "NVMe 控制器已识别");
  }
  return EFI_SUCCESS;
}

/* -------------------------------------------------------------- public API */
const char *DiskIfaceName(UINT32 iface)
{
  switch (iface) {
  case 1: return "SATA / ATA";
  case 2: return "NVMe";
  default: return "Block I/O";
  }
}

VOID DiskRefreshHealth(DISK_DEV *disk)
{
  if (disk == 0) {
    return;
  }
  if (EFI_ERROR(NvmeIdentify(disk))) {
    if (EFI_ERROR(AtaIdentify(disk))) {
      disk->Iface = 0;
      disk->HealthLevel = 0;
      disk->LifePercent = DISK_LIFE_UNKNOWN;
      disk->SparePercent = DISK_LIFE_UNKNOWN;
      disk->AttrCount = 0;
      disk->PowerOnHours = 0;
      disk->PowerCycles = 0;
      UiStrPrintf(disk->HealthText, sizeof(disk->HealthText), "仅块设备访问（无 SMART 通道）");
    }
  }
}

/* TRUE when the boot volume's device path extends this disk's device path,
   i.e. the firmware booted from a partition on this device. */
BOOLEAN DiskIsBootDevice(DISK_DEV *disk)
{
  EFI_DEVICE_PATH_PROTOCOL *a;
  EFI_DEVICE_PATH_PROTOCOL *b;
  UINT32 guard = 0;

  if (disk == 0 || disk->Dp == 0 || gApp.Sys.BootVolumeDp == 0) {
    return FALSE;
  }
  a = disk->Dp;
  b = gApp.Sys.BootVolumeDp;
  while (a->Type != 0x7F && guard++ < 32) {
    UINTN la = DpNodeLength(a);
    UINTN lb;
    if (b->Type == 0x7F || la < 4) {
      return FALSE;
    }
    lb = DpNodeLength(b);
    if (la != lb || a->Type != b->Type || a->SubType != b->SubType) {
      return FALSE;
    }
    if (la > 4 && memcmp(a, b, la) != 0) {
      return FALSE;
    }
    a = (EFI_DEVICE_PATH_PROTOCOL *)((UINT8 *)a + la);
    b = (EFI_DEVICE_PATH_PROTOCOL *)((UINT8 *)b + lb);
  }
  return TRUE;
}

VOID DiskEnumerate(VOID)
{
  EFI_HANDLE *handles = 0;
  UINTN count = 0, i;
  EFI_STATUS status;

  gDiskCount = 0;
  status = gApp.BS->LocateHandleBuffer(2, (EFI_GUID *)&gEfiBlockIoProtocolGuid, 0, &count, &handles);
  DbgStr("disk: LocateHandleBuffer status=");
  DbgHex((UINT64)status);
  DbgStr(" count=");
  DbgDec(count);
  DbgStr("\n");
  if (EFI_ERROR(status)) {
    return;
  }
  for (i = 0; i < count && gDiskCount < DISK_MAX_DEVICES; i++) {
    EFI_BLOCK_IO_PROTOCOL *io = 0;
    EFI_BLOCK_IO_MEDIA *media;
    DISK_DEV *d;
    if (EFI_ERROR(gApp.BS->HandleProtocol(handles[i], (EFI_GUID *)&gEfiBlockIoProtocolGuid, (VOID **)&io)) || io == 0) {
      continue;
    }
    media = io->Media;
    DbgStr("  blk: present=");
    DbgDec(media ? media->MediaPresent : 0);
    DbgStr(" last=");
    DbgDec(media ? media->LastBlock : 0);
    DbgStr(" bs=");
    DbgDec(media ? media->BlockSize : 0);
    DbgStr(" logical=");
    DbgDec(media ? media->LogicalPartition : 0);
    DbgStr("\n");
    if (media == 0 || media->MediaPresent == 0 || media->LastBlock == 0) {
      continue;
    }
    /* the launcher lists whole devices only; partitions are browsed on the
       storage page once a device is opened */
    if (media->LogicalPartition) {
      continue;
    }
    d = &gDisks[gDiskCount];
    gApp.BS->SetMem(d, sizeof(*d), 0);
    d->LifePercent = DISK_LIFE_UNKNOWN;
    d->SparePercent = DISK_LIFE_UNKNOWN;
    d->Handle = handles[i];
    d->Io = io;
    d->BlockSize = media->BlockSize;
    d->LastBlock = media->LastBlock;
    d->SizeBytes = (media->LastBlock + 1) * (UINT64)media->BlockSize;
    d->ReadOnly = media->ReadOnly;
    d->Removable = media->RemovableMedia;
    d->LogicalPartition = media->LogicalPartition;
    d->MediaPresent = media->MediaPresent;
    if (!EFI_ERROR(gApp.BS->HandleProtocol(handles[i], (EFI_GUID *)&gEfiDevicePathProtocolGuid, (VOID **)&d->Dp))) {
      FormatDevicePath(d->Dp, d->PathText, sizeof(d->PathText));
      DiskPathPci(d->Dp, &d->PciDev, &d->PciFunc, &d->PciValid);
    } else {
      UiStrPrintf(d->PathText, sizeof(d->PathText), "设备 #%u", gDiskCount + 1);
    }
    gDiskCount++;
  }
  gApp.BS->FreePool(handles);
  for (i = 0; i < gDiskCount; i++) {
    gDisks[i].BootVolume = DiskIsBootDevice(&gDisks[i]);
    DiskRefreshHealth(&gDisks[i]);
  }
}

UINT32 DiskCount(VOID)
{
  return gDiskCount;
}

DISK_DEV *DiskAt(UINT32 index)
{
  return index < gDiskCount ? &gDisks[index] : 0;
}

/* ---------------------------------------------------------------- scanning */
static VOID FillSpeedProfile(DISK_DEV *disk, UINT32 bucket, UINT32 mbs)
{
  UINT32 scaled;
  if (bucket >= DISK_SPEED_BUCKETS) {
    return;
  }
  scaled = mbs > 250 ? 250 : mbs;
  if (scaled < 1) {
    scaled = 1;
  }
  disk->SpeedProfile[bucket] = (UINT8)scaled;
  if (scaled > disk->MaxMbs) {
    disk->MaxMbs = scaled;
  }
  if (bucket + 1 > disk->ProfileCount) {
    disk->ProfileCount = bucket + 1;
  }
}

EFI_STATUS DiskSurfaceScan(DISK_DEV *disk, UINT32 chunkBytes,
                           BOOLEAN (*progress)(DISK_DEV *disk, UINT32 permille, UINT32 mbs, UINT64 doneBytes))
{
  EFI_STATUS status = EFI_SUCCESS;
  UINT8 *buffer = 0;
  UINT64 total, done = 0;
  UINT64 startTsc;
  UINT32 buckets = DISK_SPEED_BUCKETS;
  UINT32 minMbs = 0xFFFFFFFF, maxMbs = 0;
  UINT32 sumMbs = 0, samples = 0;
  UINT32 errors = 0;

  if (disk == 0 || disk->Io == 0) {
    return EFI_INVALID_PARAMETER;
  }
  if (chunkBytes < disk->BlockSize) {
    chunkBytes = disk->BlockSize;
  }
  chunkBytes = (chunkBytes / disk->BlockSize) * disk->BlockSize;
  if (EFI_ERROR(gApp.BS->AllocatePool(EfiBootServicesData, chunkBytes, (VOID **)&buffer))) {
    return EFI_OUT_OF_RESOURCES;
  }
  total = disk->SizeBytes;
  disk->ProfileCount = 0;
  disk->MinMbs = 0;
  disk->MaxMbs = 0;
  startTsc = SysTsc();
  gStopRequested = FALSE;
  gBusy = TRUE;

  while (done < total) {
    EFI_LBA lba = (EFI_LBA)(done / disk->BlockSize);
    UINTN bytes = (UINTN)((total - done) < chunkBytes ? (total - done) : chunkBytes);
    UINT64 t0;
    UINT64 elapsedMs;
    UINT32 mbs;
    UINT32 bucket;

    t0 = SysTsc();
    status = disk->Io->ReadBlocks(disk->Io, disk->Io->Media->MediaId, lba, bytes, buffer);
    elapsedMs = SysMsSince(t0);
    if (EFI_ERROR(status)) {
      errors++;
      if (errors > 64) {
        break;
      }
    }
    if (elapsedMs == 0) {
      elapsedMs = 1;
    }
    mbs = (UINT32)(((UINT64)bytes * 1000) / (elapsedMs * 1024 * 1024));
    bucket = (UINT32)((done * buckets) / total);
    FillSpeedProfile(disk, bucket, mbs);
    if (mbs < minMbs) {
      minMbs = mbs;
    }
    if (mbs > maxMbs) {
      maxMbs = mbs;
    }
    sumMbs += mbs;
    samples++;
    done += bytes;
    if (progress && !progress(disk, (UINT32)((done * 1000) / total), mbs, done)) {
      gStopRequested = TRUE;
    }
    if (UiPump()) {
      disk->ScanErrors = errors;
      status = EFI_ABORTED;
      break;
    }
  }
  disk->ScanErrors = errors;
  disk->Scanned = TRUE;
  disk->MinMbs = minMbs == 0xFFFFFFFF ? 0 : minMbs;
  disk->MaxMbs = maxMbs;
  disk->AvgMbs = samples ? sumMbs / samples : 0;
  disk->HealthLevel = (errors || disk->HealthLevel == 3) ? 3 : (disk->HealthLevel ? disk->HealthLevel : 1);
  gBusy = FALSE;
  gApp.BS->FreePool(buffer);
  (VOID)startTsc;
  if (gResults.DiskScannedDevices < 100) {
    gResults.DiskScannedDevices++;
  }
  gResults.DiskErrorCount += errors;
  gResults.DiskScannedBytes += done;
  if (disk->MinMbs && (gResults.DiskMinMbs == 0 || disk->MinMbs < gResults.DiskMinMbs)) {
    gResults.DiskMinMbs = disk->MinMbs;
  }
  if (disk->MaxMbs > gResults.DiskMaxMbs) {
    gResults.DiskMaxMbs = disk->MaxMbs;
  }
  if (samples) {
    gResults.DiskAvgMbs = (gResults.DiskAvgMbs + disk->AvgMbs) / 2;
  }
  gResults.DiskTested = TRUE;
  gResults.HealthDisk = disk->HealthLevel;
  return status;
}

EFI_STATUS DiskWritePattern(DISK_DEV *disk, UINT64 startByte, UINT64 bytes, UINT8 pattern)
{
  EFI_STATUS status = EFI_SUCCESS;
  UINT8 *buffer = 0;
  UINT32 chunk = 1024 * 1024;
  UINT64 done = 0;

  if (disk == 0 || disk->Io == 0 || disk->ReadOnly) {
    return EFI_WRITE_PROTECTED;
  }
  if (chunk < disk->BlockSize) {
    chunk = disk->BlockSize;
  }
  chunk = (chunk / disk->BlockSize) * disk->BlockSize;
  if (EFI_ERROR(gApp.BS->AllocatePool(EfiBootServicesData, chunk, (VOID **)&buffer))) {
    return EFI_OUT_OF_RESOURCES;
  }
  gApp.BS->SetMem(buffer, chunk, pattern);
  while (done < bytes) {
    EFI_LBA lba = (EFI_LBA)((startByte + done) / disk->BlockSize);
    UINTN len = (UINTN)((bytes - done) < chunk ? (bytes - done) : chunk);
    status = disk->Io->WriteBlocks(disk->Io, disk->Io->Media->MediaId, lba, len, buffer);
    if (EFI_ERROR(status)) {
      break;
    }
    done += len;
    if (UiPump()) {
      status = EFI_ABORTED;
      break;
    }
  }
  disk->Io->FlushBlocks(disk->Io);
  gApp.BS->FreePool(buffer);
  return status;
}

EFI_STATUS DiskVerifyPattern(DISK_DEV *disk, UINT64 startByte, UINT64 bytes, UINT8 pattern,
                             UINT64 *mismatchOffset)
{
  EFI_STATUS status = EFI_SUCCESS;
  UINT8 *buffer = 0;
  UINT32 chunk = 1024 * 1024;
  UINT64 done = 0;

  if (mismatchOffset) {
    *mismatchOffset = 0;
  }
  if (disk == 0 || disk->Io == 0) {
    return EFI_INVALID_PARAMETER;
  }
  if (chunk < disk->BlockSize) {
    chunk = disk->BlockSize;
  }
  chunk = (chunk / disk->BlockSize) * disk->BlockSize;
  if (EFI_ERROR(gApp.BS->AllocatePool(EfiBootServicesData, chunk, (VOID **)&buffer))) {
    return EFI_OUT_OF_RESOURCES;
  }
  while (done < bytes) {
    EFI_LBA lba = (EFI_LBA)((startByte + done) / disk->BlockSize);
    UINTN len = (UINTN)((bytes - done) < chunk ? (bytes - done) : chunk);
    UINTN i;
    gApp.BS->SetMem(buffer, chunk, 0);
    status = disk->Io->ReadBlocks(disk->Io, disk->Io->Media->MediaId, lba, len, buffer);
    if (EFI_ERROR(status)) {
      break;
    }
    for (i = 0; i < len; i++) {
      if (buffer[i] != pattern) {
        if (mismatchOffset) {
          *mismatchOffset = startByte + done + i;
        }
        status = EFI_DEVICE_ERROR;
        break;
      }
    }
    if (EFI_ERROR(status)) {
      break;
    }
    done += len;
    if (UiPump()) {
      status = EFI_ABORTED;
      break;
    }
  }
  gApp.BS->FreePool(buffer);
  return status;
}
