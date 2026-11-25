/* pci.c - PCI/PCIe inventory: config-space walk over every root bridge, then
   device-path matching so that disks and NICs land on their controller node.

   The walk is deliberately firmware agnostic: the root bridge IO protocol only
   hands out Pci.Read/Pci.Write, so buses are discovered by following the
   secondary bus of every bridge that is found (the usual tree walk), starting
   from bus 0 of each root bridge. */
#include "ui.h"
#include "pci.h"
#include "disk.h"
#include "i18n.h"
#include "debug.h"

static PCI_DEV gPci[PCI_MAX_DEVS];
static UINT32  gPciCount;
static UINT32  gPciSkipped;
static UINT32  gPciRoots;
static UINT32  gHostBridges;
static NET_DEV gNet[PCI_MAX_NETS];
static UINT32  gNetCount;
static BOOLEAN gNetScanned;

/* ------------------------------------------------------------------ names */

static const struct {
  UINT16 Vid;
  const char *Name;
} gVendors[] = {
  { 0x8086, "Intel" },        { 0x8087, "Intel" },
  { 0x1022, "AMD" },          { 0x1002, "AMD / ATI" },
  { 0x10DE, "NVIDIA" },       { 0x1AF4, "Red Hat virtio" },
  { 0x1B36, "Red Hat QEMU" }, { 0x1234, "Bochs" },
  { 0x15AD, "VMware" },       { 0x106B, "Apple" },
  { 0x14E4, "Broadcom" },     { 0x10EC, "Realtek" },
  { 0x1B21, "ASMedia" },      { 0x144D, "Samsung" },
  { 0x1C5C, "SK hynix" },     { 0x1344, "Micron" },
  { 0x1987, "Phison" },       { 0x126F, "Silicon Motion" },
  { 0x1E0F, "KIOXIA" },       { 0x1179, "Toshiba" },
  { 0x1D0F, "Amazon" },       { 0x1A03, "ASPEED" },
  { 0x19E5, "Huawei" },       { 0x15B3, "Mellanox" },
  { 0x0E11, "Compaq" },       { 0x1077, "QLogic" },
  { 0x19A2, "Emulex" },       { 0x1014, "IBM" },
  { 0x1CC1, "ADATA" },        { 0x025E, "Solidigm" }
};

const char *PciVendorName(UINT16 vid)
{
  UINT32 i;
  for (i = 0; i < sizeof(gVendors) / sizeof(gVendors[0]); i++) {
    if (gVendors[i].Vid == vid) {
      return gVendors[i].Name;
    }
  }
  return 0;
}

const char *PciKindName(UINT8 kind)
{
  switch (kind) {
  case PCI_KIND_ROOT:    return "根桥";
  case PCI_KIND_BRIDGE:  return "桥";
  case PCI_KIND_STORAGE: return "存储";
  case PCI_KIND_NET:     return "网络";
  case PCI_KIND_DISPLAY: return "显示";
  case PCI_KIND_USB:     return "USB";
  case PCI_KIND_AUDIO:   return "音频";
  case PCI_KIND_SERIAL:  return "串行总线";
  default:               return "其他";
  }
}

const char *PciClassText(const PCI_DEV *dev)
{
  switch (dev->Kind) {
  case PCI_KIND_ROOT:
    return "PCIe 根桥";
  case PCI_KIND_BRIDGE:
    if (dev->SubClass == 0x04) {
      return "PCIe 根端口";
    }
    if (dev->SubClass == 0x01) {
      return "ISA/LPC 桥";
    }
    return "桥接设备";
  case PCI_KIND_STORAGE:
    switch (dev->SubClass) {
    case 0x08: return "NVMe 控制器";
    case 0x06: return "SATA 控制器";
    case 0x07: return "SAS 控制器";
    case 0x01: return "IDE 控制器";
    case 0x05: return "ATA 控制器";
    default:   return "存储控制器";
    }
  case PCI_KIND_NET:
    return dev->SubClass == 0x11 ? "无线网卡" : "以太网控制器";
  case PCI_KIND_DISPLAY:
    return dev->SubClass == 0x02 ? "3D 显示控制器" : "显示控制器";
  case PCI_KIND_USB:
    if (dev->ProgIf >= 0x30) {
      return "USB 3 控制器";
    }
    return dev->ProgIf >= 0x20 ? "USB 2 控制器" : "USB 控制器";
  case PCI_KIND_AUDIO:
    return "高清音频设备";
  case PCI_KIND_SERIAL:
    if (dev->SubClass == 0x03) {
      return "USB 控制器";
    }
    return dev->SubClass == 0x05 ? "SMBus 控制器" : "串行总线控制器";
  default:
    /* servers are full of integrated endpoints whose class alone is the only
       description the firmware can give, so name them instead of "其他设备" */
    if (dev->Class == 0x08) {
      switch (dev->SubClass) {
      case 0x00: return "中断控制器";
      case 0x01: return "DMA 控制器";
      case 0x02: return "定时器";
      case 0x03: return "实时时钟";
      default:   return "系统外设";
      }
    }
    if (dev->Class == 0x07) {
      switch (dev->SubClass) {
      case 0x00: return "串口控制器";
      case 0x01: return "并行口控制器";
      case 0x03: return "调制解调器";
      default:   return "通信控制器";
      }
    }
    if (dev->Class == 0x0D) {
      return "无线控制器";
    }
    if (dev->Class == 0x05) {
      return "内存控制器";
    }
    if (dev->Class == 0x0B) {
      return "协处理器";
    }
    if (dev->Class == 0x12) {
      return "处理加速器";
    }
    if (dev->Class == 0x09) {
      return "输入设备";
    }
    if (dev->Class == 0x0A) {
      return "扩展坞";
    }
    if (dev->Class == 0x13) {
      return "测量仪器";
    }
    return "其他设备";
  }
}

VOID PciFormatAddress(const PCI_DEV *dev, char *buf, UINTN cap)
{
  if (dev->Segment) {
    UiStrPrintf(buf, cap, "%04x:%02x:%02x.%u", dev->Segment, dev->Bus, dev->Dev, dev->Func);
  } else {
    UiStrPrintf(buf, cap, "%02x:%02x.%u", dev->Bus, dev->Dev, dev->Func);
  }
}

VOID PciFormatMac(const UINT8 *mac, char *buf, UINTN cap)
{
  UiStrPrintf(buf, cap, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4],
              mac[5]);
}

BOOLEAN PciHasLink(const PCI_DEV *dev)
{
  return (BOOLEAN)(dev->LinkGen || dev->LinkWidth);
}

UINT32 PciDepth(const PCI_DEV *dev)
{
  UINT32 depth = 0;
  const PCI_DEV *walk = dev;

  while (walk && !walk->IsRoot && walk->Parent < PciCount() && depth < 32) {
    walk = PciAt(walk->Parent);
    depth++;
  }
  return depth;
}

/* -------------------------------------------------------------- access */

/* The firmware side layout of EFI_PCI_IO_PROTOCOL has shifted by one member
   between edk2 generations, so the two entry points that are actually needed
   (config space access and GetLocation) are located by probing instead of
   trusting a fixed offset. Candidate offsets are tried and the config accessor
   is the one that returns a real vendor id.

   Every PCI device the firmware has bound is reachable through its own
   EFI_PCI_IO_PROTOCOL instance, which is a safer source than walking root
   bridge windows by hand: GetLocation() reports the segment / bus / device /
   function the firmware assigned. */
typedef EFI_PCI_IO_PROTOCOL PCI_IO;

static UINTN gCfgOff; /* config accessor, probed once */
static UINTN gLocOff; /* GetLocation, gCfgOff + 64 in every known layout */

typedef UINT32 (EFIAPI *CFG_READ_PROC)(VOID *This, UINT32 Width, UINT32 Offset, UINTN Count,
                                       VOID *Buffer);
typedef EFI_STATUS (EFIAPI *GET_LOCATION_PROC)(VOID *This, UINTN *Seg, UINTN *Bus, UINTN *Dev,
                                               UINTN *Fn);

static UINT32 CfgReadAt(PCI_IO *io, UINTN off, UINT8 reg)
{
  UINT32 v = 0xFFFFFFFF;
  CFG_READ_PROC fn = (CFG_READ_PROC)(*(void **)((UINT8 *)io + off));
  if (fn) {
    fn(io, EfiPciWidthUint32, (UINT32)(reg & 0xFC), 1, &v);
  }
  return v;
}

static BOOLEAN PciProbeLayout(PCI_IO *io)
{
  static const UINTN cands[] = { 64, 48, 32 };
  UINT32 i;
  for (i = 0; i < sizeof(cands) / sizeof(cands[0]); i++) {
    UINT32 v = CfgReadAt(io, cands[i], 0x00);
    if ((UINT16)v != 0xFFFF && (UINT16)v != 0) {
      gCfgOff = cands[i];
      gLocOff = cands[i] + 64;
      DbgStr("pci: layout cfg=");
      DbgDec((UINT32)gCfgOff);
      DbgStr("\n");
      return TRUE;
    }
  }
  return FALSE;
}

static EFI_STATUS IoLocation(PCI_IO *io, UINTN *seg, UINTN *bus, UINTN *dev, UINTN *fn)
{
  GET_LOCATION_PROC proc = (GET_LOCATION_PROC)(*(void **)((UINT8 *)io + gLocOff));
  if (!proc) {
    return 0x8000000000000003ULL;
  }
  return proc(io, seg, bus, dev, fn);
}

static UINT32 Cfg32(PCI_IO *io, UINT8 reg)
{
  return CfgReadAt(io, gCfgOff, reg);
}

static UINT8 Cfg8(PCI_IO *io, UINT8 reg)
{
  UINT32 v = Cfg32(io, reg);
  return (UINT8)((v >> ((reg & 3) * 8)) & 0xFF);
}

/* Walks the capability list and reports the negotiated PCIe link, which is how
   a root port / endpoint is shown as "Gen3 x4" in the topology. */
static VOID ReadPcieLink(PCI_IO *io, UINT8 *gen, UINT8 *width)
{
  UINT32 status = Cfg32(io, 0x04);
  UINT32 cap;
  UINT32 guard = 0;

  *gen = 0;
  *width = 0;
  if ((status & 0x00100000) == 0) { /* status bit 4: capability list present */
    return;
  }
  cap = Cfg8(io, 0x34);
  while (cap >= 0x40 && guard++ < 48) {
    UINT32 hdr = Cfg32(io, (UINT8)cap);
    UINT8 id;
    UINT8 next;
    if (hdr == 0xFFFFFFFF) {
      return;
    }
    id = (UINT8)(hdr & 0xFF);
    next = (UINT8)((hdr >> 8) & 0xFF);
    if (id == 0x10) { /* PCI Express capability */
      UINT32 lnkCap = Cfg32(io, (UINT8)(cap + 0x0C));
      UINT32 lnkSta = Cfg32(io, (UINT8)(cap + 0x10));
      UINT8 curGen = (UINT8)(lnkSta & 0x0F);
      UINT8 curWidth = (UINT8)((lnkSta >> 4) & 0x3F);
      if (curGen && curWidth) {
        *gen = curGen;
        *width = curWidth;
      } else {
        *gen = (UINT8)(lnkCap & 0x0F);
        *width = (UINT8)((lnkCap >> 4) & 0x3F);
      }
      return;
    }
    if (next == 0 || next == (UINT8)cap) {
      return;
    }
    cap = next;
  }
}

/* ----------------------------------------------------------- enumerate */

static UINT8 ClassKind(UINT8 cls, UINT8 sub)
{
  switch (cls) {
  case 0x01: return PCI_KIND_STORAGE;
  case 0x02: return PCI_KIND_NET;
  case 0x03: return PCI_KIND_DISPLAY;
  case 0x04: return PCI_KIND_AUDIO;
  case 0x05: return PCI_KIND_OTHER;
  case 0x06: return PCI_KIND_BRIDGE;
  case 0x0C: return sub == 0x03 ? PCI_KIND_USB : PCI_KIND_SERIAL;
  case 0x0D: return PCI_KIND_NET;
  case 0x12: return PCI_KIND_OTHER;
  default:   return sub == 0x03 ? PCI_KIND_USB : PCI_KIND_OTHER;
  }
}

static PCI_DEV *AddNode(VOID)
{
  PCI_DEV *dev;
  if (gPciCount >= PCI_MAX_DEVS) {
    gPciSkipped++;
    return 0;
  }
  dev = &gPci[gPciCount];
  gApp.BS->SetMem(dev, sizeof(*dev), 0);
  dev->Parent = PCI_NO_INDEX;
  dev->DiskIndex = PCI_NO_INDEX;
  dev->NetIndex = PCI_NO_INDEX;
  gPciCount++;
  return dev;
}

/* A bridge's secondary/subordinate window tells which bus belongs to it, so the
   parent of a device is the bridge with the deepest window that still contains
   the device bus; everything else hangs off the root complex. */
static VOID ResolveParents(UINT32 rootIndex)
{
  UINT32 i;
  for (i = 0; i < gPciCount; i++) {
    PCI_DEV *dev = &gPci[i];
    UINT32 best = PCI_NO_INDEX;
    UINT8 bestSecondary = 0;
    UINT32 j;

    if (dev->IsRoot) {
      continue;
    }
    for (j = 0; j < gPciCount; j++) {
      PCI_DEV *br = &gPci[j];
      if (!br->IsBridge || br->IsRoot || br->Segment != dev->Segment) {
        continue;
      }
      if (br->SecondaryBus && br->SecondaryBus <= dev->Bus &&
          dev->Bus <= (br->SubordinateBus ? br->SubordinateBus : br->SecondaryBus)) {
        if (best == PCI_NO_INDEX || br->SecondaryBus >= bestSecondary) {
          best = j;
          bestSecondary = br->SecondaryBus;
        }
      }
    }
    dev->Parent = (best != PCI_NO_INDEX) ? best : rootIndex;
  }
}

VOID PciEnumerate(VOID)
{
  EFI_HANDLE *handles = 0;
  UINTN count = 0;
  UINTN i;
  EFI_STATUS status;
  UINT32 rootIndex = PCI_NO_INDEX;

  gPciCount = 0;
  gPciSkipped = 0;
  gPciRoots = 0;
  gHostBridges = 0;
  gNetCount = 0;
  gNetScanned = FALSE;
  gCfgOff = 0;
  gLocOff = 0;

  /* root bridge instances: counted for the summary line only */
  status = gApp.BS->LocateHandleBuffer(2, (EFI_GUID *)&gEfiPciRootBridgeIoProtocolGuid, 0, &count,
                                       &handles);
  if (!EFI_ERROR(status) && handles) {
    gPciRoots = (UINT32)count;
    gApp.BS->FreePool(handles);
  }
  handles = 0;
  count = 0;

  status = gApp.BS->LocateHandleBuffer(2, (EFI_GUID *)&gEfiPciIoProtocolGuid, 0, &count, &handles);
  if (EFI_ERROR(status) || !handles || count == 0) {
    if (handles) {
      gApp.BS->FreePool(handles);
    }
    DbgStr("pci: no pci io handles\n");
    return;
  }

  for (i = 0; i < count; i++) {
    if (gPciCount >= PCI_MAX_DEVS) {
      gPciSkipped += (UINT32)(count - i);
      break;
    }
    PCI_IO *io = 0;
    UINTN seg = 0;
    UINTN bus = 0;
    UINTN devNo = 0;
    UINTN fn = 0;
    UINT32 id;
    UINT32 cls;
    UINT16 vendor;
    UINT8 base;
    UINT8 sub;
    PCI_DEV *n;

    if (EFI_ERROR(gApp.BS->HandleProtocol(handles[i], (EFI_GUID *)&gEfiPciIoProtocolGuid,
                                          (VOID **)&io)) || !io) {
      continue;
    }
    if (gCfgOff == 0 && !PciProbeLayout(io)) {
      continue;
    }
    if (EFI_ERROR(IoLocation(io, &seg, &bus, &devNo, &fn))) {
      continue;
    }
    id = Cfg32(io, 0x00);
    vendor = (UINT16)(id & 0xFFFF);
    if (vendor == 0xFFFF || vendor == 0) {
      continue;
    }
    cls = Cfg32(io, 0x08);
    base = (UINT8)((cls >> 24) & 0xFF);
    sub = (UINT8)((cls >> 16) & 0xFF);

    n = AddNode();
    if (!n) {
      break;
    }
    n->Segment = (UINT16)seg;
    n->Bus = (UINT8)bus;
    n->Dev = (UINT8)devNo;
    n->Func = (UINT8)fn;
    n->VendorId = vendor;
    n->DeviceId = (UINT16)((id >> 16) & 0xFFFF);
    n->Revision = (UINT8)(cls & 0xFF);
    n->Class = base;
    n->SubClass = sub;
    n->ProgIf = (UINT8)((cls >> 8) & 0xFF);
    n->Kind = ClassKind(base, sub);
    n->IsBridge = (base == 0x06) ? 1 : 0;

    if (base == 0x06 && sub == 0x00) {
      /* host bridge / root complex */
      gHostBridges++;
      if (rootIndex == PCI_NO_INDEX && bus == 0) {
        rootIndex = gPciCount - 1;
        n->IsRoot = 1;
        n->Kind = PCI_KIND_ROOT;
        /* stored untranslated: the page translates it while drawing, so a
           runtime language switch does not leave a stale label behind */
        UiStrPrintf(n->Name, sizeof(n->Name), "%s", "PCIe 根桥");
      } else {
        n->Kind = PCI_KIND_BRIDGE;
        UiStrPrintf(n->Name, sizeof(n->Name), "%s", "主机桥");
      }
    } else if (n->IsBridge) {
      UINT32 buses = Cfg32(io, 0x18);
      n->SecondaryBus = (UINT8)((buses >> 8) & 0xFF);
      n->SubordinateBus = (UINT8)((buses >> 16) & 0xFF);
      UiStrPrintf(n->Name, sizeof(n->Name), "%s", PciClassText(n));
    } else {
      UiStrPrintf(n->Name, sizeof(n->Name), "%s", PciClassText(n));
    }
    if (!n->IsRoot) {
      ReadPcieLink(io, &n->LinkGen, &n->LinkWidth);
    }
  }
  gApp.BS->FreePool(handles);
  ResolveParents(rootIndex);

  DbgStr("pci: devices=");
  DbgDec(gPciCount);
  DbgStr(" roots=");
  DbgDec(gPciRoots);
  DbgStr(" hosts=");
  DbgDec(gHostBridges);
  DbgStr("\n");
}

/* --------------------------------------------------------- asset binding */

/* Device paths carry the PCI node(s) but not the bus number, so the last node
   is matched against the enumerated tree; a class hint resolves the (rare)
   case of two buses sharing a device/function pair. */
static BOOLEAN PathLastPci(EFI_DEVICE_PATH_PROTOCOL *dp, UINT8 *dev, UINT8 *func)
{
  BOOLEAN found = FALSE;
  UINT32 guard = 0;

  while (dp && dp->Type != 0x7F && guard++ < 32) {
    UINT16 len = (UINT16)(dp->Length[0] | ((UINT16)dp->Length[1] << 8));
    if (len < 4) {
      break;
    }
    if (dp->Type == 0x01 && dp->SubType == 0x01) {
      UINT8 *d = (UINT8 *)dp;
      *func = d[4];
      *dev = d[5];
      found = TRUE;
    }
    dp = (EFI_DEVICE_PATH_PROTOCOL *)((UINT8 *)dp + len);
  }
  return found;
}

static UINT32 FindNode(UINT8 dev, UINT8 func, UINT8 kindHint)
{
  UINT32 i;

  /* dev/func alone is ambiguous on a server (20 root bridges, the same
     device/function on every bus), and a path that points at an unenumerated
     function would otherwise be attached to whatever shares its dev/func - a
     NIC landing on the xHCI controller. The class hint is what makes the match
     meaningful, so an unmatched hint stays unmatched. */
  for (i = 0; i < gPciCount; i++) {
    PCI_DEV *n = &gPci[i];
    if (n->IsRoot || n->Dev != dev || n->Func != func) {
      continue;
    }
    if (kindHint == PCI_NO_INDEX || n->Kind == kindHint) {
      return i;
    }
  }
  return PCI_NO_INDEX;
}

static VOID ScanNets(VOID)
{
  EFI_HANDLE *handles = 0;
  UINTN count = 0;
  UINTN i;

  gNetCount = 0;
  gNetScanned = TRUE;
  if (EFI_ERROR(gApp.BS->LocateHandleBuffer(2, (EFI_GUID *)&gEfiSimpleNetworkProtocolGuid,
                                            0, &count, &handles))) {
    return;
  }
  for (i = 0; i < count && gNetCount < PCI_MAX_NETS; i++) {
    EFI_SIMPLE_NETWORK_PROTOCOL *snp = 0;
    EFI_DEVICE_PATH_PROTOCOL *dp = 0;
    NET_DEV *net;
    UINT8 dev = 0;
    UINT8 func = 0;
    UINT8 k;

    if (EFI_ERROR(gApp.BS->HandleProtocol(handles[i],
                                          (EFI_GUID *)&gEfiSimpleNetworkProtocolGuid,
                                          (VOID **)&snp)) || !snp) {
      continue;
    }
    net = &gNet[gNetCount];
    gApp.BS->SetMem(net, sizeof(*net), 0);
    net->Present = TRUE;
    net->PciIndex = PCI_NO_INDEX;
    UiStrPrintf(net->Name, sizeof(net->Name), "网卡 %u", gNetCount);

    if (!EFI_ERROR(gApp.BS->HandleProtocol(handles[i], (EFI_GUID *)&gEfiDevicePathProtocolGuid,
                                           (VOID **)&dp)) && dp) {
      if (PathLastPci(dp, &dev, &func)) {
        net->PciIndex = (UINT8)FindNode(dev, func, PCI_KIND_NET);
      }
    }
    if (snp->Mode) {
      for (k = 0; k < 6; k++) {
        net->Mac[k] = snp->Mode->CurrentAddress.Addr[k];
      }
      net->MediaPresent = snp->Mode->MediaPresent ? TRUE : FALSE;
    }
    if (net->PciIndex != PCI_NO_INDEX) {
      gPci[net->PciIndex].NetIndex = (UINT8)gNetCount;
    }
    gNetCount++;
  }
  if (handles) {
    gApp.BS->FreePool(handles);
  }
  DbgStr("pci: nets=");
  DbgDec(gNetCount);
  DbgStr("\n");
}

VOID PciBindAssets(VOID)
{
  UINT32 i;

  if (!gNetScanned) {
    ScanNets();
  }
  for (i = 0; i < gPciCount; i++) {
    gPci[i].DiskIndex = PCI_NO_INDEX;
  }
  for (i = 0; i < DiskCount() && i < PCI_MAX_DEVS; i++) {
    DISK_DEV *disk = DiskAt(i);
    UINT8 dev = 0;
    UINT8 func = 0;
    UINT8 hint = PCI_KIND_STORAGE;
    UINT32 node;

    if (!disk->PciValid) {
      continue;
    }
    dev = disk->PciDev;
    func = disk->PciFunc;
    node = FindNode(dev, func, hint);
    if (node == PCI_NO_INDEX) {
      node = FindNode(dev, func, PCI_NO_INDEX);
    }
    if (node != PCI_NO_INDEX) {
      if (gPci[node].DiskIndex == PCI_NO_INDEX) {
        gPci[node].DiskIndex = (UINT8)i;
      }
    }
  }
}

/* --------------------------------------------------------------- getters */

UINT32 PciCount(VOID)
{
  return gPciCount;
}

/* devices the firmware exposed but that did not fit the table above */
UINT32 PciSkipped(VOID)
{
  return gPciSkipped;
}

PCI_DEV *PciAt(UINT32 index)
{
  return index < gPciCount ? &gPci[index] : 0;
}

UINT32 PciRootCount(VOID)
{
  return gPciRoots;
}

UINT32 PciHostBridgeCount(VOID)
{
  return gHostBridges;
}

UINT32 PciEndpointCount(VOID)
{
  UINT32 i;
  UINT32 n = 0;
  for (i = 0; i < gPciCount; i++) {
    if (!gPci[i].IsRoot && !gPci[i].IsBridge) {
      n++;
    }
  }
  return n;
}

UINT32 PciClassCount(UINT8 kind)
{
  UINT32 i;
  UINT32 n = 0;
  for (i = 0; i < gPciCount; i++) {
    if (gPci[i].Kind == kind && !gPci[i].IsRoot) {
      n++;
    }
  }
  return n;
}

UINT32 NetCount(VOID)
{
  return gNetCount;
}

NET_DEV *NetAt(UINT32 index)
{
  return index < gNetCount ? &gNet[index] : 0;
}
