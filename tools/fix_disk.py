import io, sys
p = 'src/disk.c'
s = io.open(p, encoding='utf-8').read()

start = s.index('/* ATA pass-thru protocol')
end = s.index('static DISK_DEV gDisks')
new = r'''/* ATA pass-thru protocol (UEFI 2.10, 20.4) */
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
                                EFI_DEVICE_PATH_PROTOCOL *NamespaceId,
                                EFI_NVM_EXPRESS_PASS_THRU_COMMAND_PACKET *Packet,
                                EFI_EVENT Event);
  EFI_STATUS (EFIAPI *GetNextNamespace)(struct EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL *This,
                                        EFI_DEVICE_PATH_PROTOCOL **NamespaceId);
  EFI_STATUS (EFIAPI *BuildDevicePath)(struct EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL *This,
                                       EFI_DEVICE_PATH_PROTOCOL *NamespaceId,
                                       EFI_DEVICE_PATH_PROTOCOL **DevicePath);
  EFI_STATUS (EFIAPI *GetNamespace)(struct EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL *This,
                                    EFI_DEVICE_PATH_PROTOCOL *DevicePath,
                                    EFI_DEVICE_PATH_PROTOCOL **NamespaceId);
} EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL;

'''
s = s[:start] + new + s[end:]
s = s.replace('#include "disk.h"\n#include "ui.h"', '#include "disk.h"\n#include "ui.h"\n#include "results.h"')

a0 = s.index('/* ------------------------------------------------------------- ATA SMART */')
a1 = s.index('/* -------------------------------------------------------------- NVMe ID */')
new_ata = r'''/* ------------------------------------------------------------- ATA SMART */
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

  status = gApp.BS->HandleProtocol(disk->Handle, (EFI_GUID *)&gEfiAtaPassThruProtocolGuid, (VOID **)&ata);
  if (EFI_ERROR(status) || ata == 0) {
    return EFI_NOT_FOUND;
  }
  if (disk->Dp && ata->GetDevice) {
    if (EFI_ERROR(ata->GetDevice(ata, disk->Dp, &port, &pmport))) {
      port = 0;
      pmport = 0xFFFF;
    }
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
    disk->Temperature = temp;
    disk->PowerOnHours = (UINT32)smart[113] | ((UINT32)smart[114] << 8);
    if (smart[0] <= 0x02) {
      disk->HealthLevel = (smart[0] == 0) ? 1 : 2;
      UiStrPrintf(disk->HealthText, sizeof(disk->HealthText),
                  "SMART %s · %uC · 通电 %u h",
                  smart[0] == 0 ? "通过" : "预警", (UINT32)temp, disk->PowerOnHours);
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

'''
s = s[:a0] + new_ata + s[a1:]

n0 = s.index('/* -------------------------------------------------------------- NVMe ID */')
n1 = s.index('/* -------------------------------------------------------------- public API */')
new_nvme = r'''/* -------------------------------------------------------------- NVMe ID */
static UINT8 gNvmeIdentifyBuf[4096] __attribute__((aligned(64)));
static UINT8 gNvmeSmartBuf[512] __attribute__((aligned(64)));

static EFI_STATUS NvmeIdentify(DISK_DEV *disk)
{
  EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL *nvme = 0;
  EFI_NVM_EXPRESS_PASS_THRU_COMMAND_PACKET packet;
  EFI_NVM_EXPRESS_COMMAND cmd;
  EFI_NVM_EXPRESS_COMPLETION completion;
  EFI_STATUS status;
  UINTN i;

  status = gApp.BS->HandleProtocol(disk->Handle, (EFI_GUID *)&gEfiNvmExpressPassThruProtocolGuid, (VOID **)&nvme);
  if (EFI_ERROR(status) || nvme == 0) {
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
  cmd.Nsid = 0;
  cmd.Cdw10 = 0x01;                  /* CNS: controller structure */
  status = nvme->PassThru(nvme, 0, &packet, 0);
  disk->Iface = 2;
  if (EFI_ERROR(status)) {
    return status;
  }
  {
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
  /* SMART / health information log page (LID 0x02) */
  gApp.BS->SetMem(&packet, sizeof(packet), 0);
  gApp.BS->SetMem(&cmd, sizeof(cmd), 0);
  gApp.BS->SetMem(&completion, sizeof(completion), 0);
  gApp.BS->SetMem(gNvmeSmartBuf, sizeof(gNvmeSmartBuf), 0);
  packet.CommandTimeout = 5000000;
  packet.TransferBuffer = gNvmeSmartBuf;
  packet.TransferLength = sizeof(gNvmeSmartBuf);
  packet.QueueType = 0;
  packet.NvmeCmd = &cmd;
  packet.NvmeCompletion = &completion;
  cmd.Cdw0.Opcode = 0x02;            /* Get Log Page */
  cmd.Nsid = 0xFFFFFFFF;
  cmd.Cdw10 = 0x02 | (127u << 16);   /* LID 2, NUMDL 127 dwords */
  if (!EFI_ERROR(nvme->PassThru(nvme, 0, &packet, 0))) {
    UINT8 *smart = gNvmeSmartBuf;
    UINT32 tempK = (UINT32)smart[1] | ((UINT32)smart[2] << 8);
    UINT8 flags = smart[0];
    UINT64 used = (UINT64)smart[48] | ((UINT64)smart[49] << 8) | ((UINT64)smart[50] << 16)
                  | ((UINT64)smart[51] << 24);
    used |= ((UINT64)smart[52] | ((UINT64)smart[53] << 8) | ((UINT64)smart[54] << 16)
             | ((UINT64)smart[55] << 24)) << 32;
    disk->Temperature = tempK > 273 ? tempK - 273 : 0;
    disk->PowerOnHours = (UINT32)smart[128] | ((UINT32)smart[129] << 8)
                         | ((UINT32)smart[130] << 16) | ((UINT32)smart[131] << 24);
    disk->BytesWritten = used * 512000ULL; /* data unit = 512000 bytes */
    disk->HealthLevel = (flags & 0x01) ? 2 : 1;
    UiStrPrintf(disk->HealthText, sizeof(disk->HealthText), "NVMe %s · %uC · 通电 %u h",
                (flags & 0x01) ? "警告" : "正常", disk->Temperature, disk->PowerOnHours);
  } else {
    disk->HealthLevel = 1;
    UiStrPrintf(disk->HealthText, sizeof(disk->HealthText), "NVMe 控制器已识别");
  }
  return EFI_SUCCESS;
}

'''
s = s[:n0] + new_nvme + s[n1:]
io.open(p, 'w', encoding='utf-8', newline='\n').write(s)
print("rewrote disk.c", len(s))
