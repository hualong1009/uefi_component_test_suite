/* topo.c - draws the CPU / memory / PCIe connection diagram.

   The tree is flattened depth first so the same routine can render both the
   compact card on the overview page and the full topology page. */
#include "topo.h"
#include "pci.h"
#include "disk.h"
#include "results.h"
#include "i18n.h"

static UINT32 gRows[PCI_MAX_DEVS];
static UINT32 gDepths[PCI_MAX_DEVS];
static UINT32 gRowCount;

static VOID AddChildren(UINT32 parent, UINT32 depth)
{
  UINT32 i;
  for (i = 0; i < PciCount() && gRowCount < PCI_MAX_DEVS; i++) {
    if (PciAt(i)->Parent == parent && !PciAt(i)->IsRoot) {
      gRows[gRowCount] = i;
      gDepths[gRowCount] = depth;
      gRowCount++;
      AddChildren(i, depth + 1);
    }
  }
}

VOID TopoRefresh(VOID)
{
  UINT32 i;
  gRowCount = 0;
  for (i = 0; i < PciCount() && gRowCount < PCI_MAX_DEVS; i++) {
    if (!PciAt(i)->IsRoot) {
      continue;
    }
    gRows[gRowCount] = i;
    gDepths[gRowCount] = 0;
    gRowCount++;
    AddChildren(i, 1);
  }
}

UINT32 TopoRowCount(VOID)
{
  return gRowCount;
}

UINT32 TopoRowDevice(UINT32 row)
{
  return row < gRowCount ? gRows[row] : PCI_NO_INDEX;
}

INT32 TopoFindRow(UINT32 pciIndex)
{
  UINT32 i;
  for (i = 0; i < gRowCount; i++) {
    if (gRows[i] == pciIndex) {
      return (INT32)i;
    }
  }
  return -1;
}

UINT32 TopoRowHeight(BOOLEAN mini)
{
  return mini ? 24 : 30;
}

static UINT32 KindIcon(UINT8 kind)
{
  switch (kind) {
  case PCI_KIND_STORAGE: return 4;
  case PCI_KIND_NET:     return 9;
  case PCI_KIND_DISPLAY: return 10;
  case PCI_KIND_USB:     return 12;
  case PCI_KIND_AUDIO:   return 13;
  case PCI_KIND_ROOT:
  case PCI_KIND_BRIDGE:
  case PCI_KIND_SERIAL:  return 11;
  default:               return 14;
  }
}

static UINT32 KindColor(UINT8 kind)
{
  switch (kind) {
  case PCI_KIND_STORAGE: return gTheme.Primary;
  case PCI_KIND_NET:     return gTheme.Ok;
  case PCI_KIND_DISPLAY: return gTheme.Warn;
  case PCI_KIND_ROOT:
  case PCI_KIND_BRIDGE:  return gTheme.Muted;
  default:               return gTheme.SubText;
  }
}

/* what hangs off a controller node: a block device or a network interface */
static VOID AttachText(const PCI_DEV *dev, char *buf, UINTN cap)
{
  buf[0] = 0;
  if (dev->DiskIndex != PCI_NO_INDEX) {
    DISK_DEV *disk = DiskAt(dev->DiskIndex);
    if (disk) {
      char size[24];
      GfxFormatSize(size, sizeof(size), disk->SizeBytes);
      UiStrPrintf(buf, cap, "%s %s", disk->Model[0] ? disk->Model : L("块设备"), size);
      return;
    }
  }
  if (dev->NetIndex != PCI_NO_INDEX) {
    NET_DEV *net = NetAt(dev->NetIndex);
    if (net) {
      char mac[24];
      PciFormatMac(net->Mac, mac, sizeof(mac));
      UiStrPrintf(buf, cap, "%s · %s", mac, net->MediaPresent ? L("链路已连接") : L("链路未连接"));
    }
  }
}

static VOID DrawCpuBlock(GFX_RECT r, BOOLEAN mini)
{
  char buf[96];
  GfxFillRound(r, 8, gTheme.PanelAlt);
  GfxRoundOutline(r, 8, gTheme.Border);
  GfxFillRound((GFX_RECT){ r.X + 10, r.Y + 10, 26, 26 }, 6, gTheme.PrimarySoft);
  UiIcon(6, r.X + 16, r.Y + 16, 14, gTheme.Primary);
  GfxTextClipped(r.X + 44, r.Y + 8, r.W - 52,
                 gApp.Sys.CpuBrand[0] ? gApp.Sys.CpuBrand : L("处理器"), gTheme.Text, 1);
  UiStrPrintf(buf, sizeof(buf), "%u 核 / %u 线程", gApp.Sys.Cores, gApp.Sys.Threads);
  GfxTextClipped(r.X + 44, r.Y + 26, r.W - 52, buf, gTheme.SubText, 1);
  if (!mini) {
    UiStrPrintf(buf, sizeof(buf), "%u MHz · %u socket · %u 逻辑 CPU", gApp.Sys.CpuBaseMhz,
                gApp.Sys.SocketCount ? gApp.Sys.SocketCount : 1, gApp.Sys.LogicalCpus);
    GfxTextClipped(r.X + 14, r.Y + 46, r.W - 28, buf, gTheme.Muted, 1);
    UiStrPrintf(buf, sizeof(buf), "%u 根桥 · %u 主机桥", PciRootCount(), PciHostBridgeCount());
    GfxTextClipped(r.X + 14, r.Y + 64, r.W - 28, buf, gTheme.Muted, 1);
  }
}

static VOID DrawMemoryBlock(GFX_RECT r, BOOLEAN mini)
{
  char buf[96];
  char size[24];
  UINT32 y = r.Y;
  UINT32 shown = 0;
  UINT32 cap = mini ? 2 : 4;

  if (!mini) {
    GfxFillRound(r, 8, gTheme.PanelAlt);
    GfxRoundOutline(r, 8, gTheme.Border);
    GfxFillRound((GFX_RECT){ r.X + 10, r.Y + 8, 22, 16 }, 4, gTheme.PrimarySoft);
    UiIcon(3, r.X + 15, r.Y + 9, 12, gTheme.Primary);
    GfxFormatSize(size, sizeof(size), gApp.Sys.InstalledRam);
    if (gApp.Sys.DominantMemSpeed) {
      UiStrPrintf(buf, sizeof(buf), "%s · %u 条 · %u MT/s", size, gApp.Sys.DimmPresentCount,
                  gApp.Sys.DominantMemSpeed);
    } else {
      UiStrPrintf(buf, sizeof(buf), "%s · %u 条", size, gApp.Sys.DimmPresentCount);
    }
    GfxTextClipped(r.X + 38, r.Y + 8, r.W - 46, buf, gTheme.Text, 1);
    y = r.Y + 30;
  } else {
    GfxFormatSize(size, sizeof(size), gApp.Sys.InstalledRam);
    UiStrPrintf(buf, sizeof(buf), "%s · %u 条内存", size, gApp.Sys.DimmPresentCount);
    GfxTextClipped(r.X, y, r.W, buf, gTheme.SubText, 1);
    y += 18;
  }

  {
    UINT32 i;
    for (i = 0; i < gApp.Sys.DimmCount && shown < cap; i++) {
      DIMM_INFO *d = &gApp.Sys.Dimms[i];
      DIMM_GEO geo;
      if (!d->Present) {
        continue;
      }
      if (y + 18 > r.Y + r.H) {
        break;
      }
      SysDimmGeometry(d, &geo);
      GfxFormatSize(size, sizeof(size), d->SizeBytes);
      UiStrPrintf(buf, sizeof(buf), "%s · %s · x%u/%uR", d->Locator[0] ? d->Locator : "DIMM",
                  size, geo.DeviceWidth, geo.Ranks);
      GfxFillRound((GFX_RECT){ r.X + 2, y + 7, 4, 4 }, 2, gTheme.Primary);
      GfxTextClipped(r.X + 12, y, r.W - 14, buf, gTheme.SubText, 1);
      y += 18;
      shown++;
    }
    if (shown == 0) {
      GfxTextClipped(r.X + 12, y, r.W - 14, "SMBIOS 未提供内存条信息", gTheme.Muted, 1);
    }
  }
}

UINT32 TopoDraw(GFX_RECT r, UINT32 firstRow, UINT32 selRow, BOOLEAN mini)
{
  UINT32 rowH = TopoRowHeight(mini);
  UINT32 leftW = mini ? 160 : 216;
  UINT32 gap = mini ? 6 : 12;
  UINT32 cpuH = mini ? 48 : 82;
  GFX_RECT cpu = { r.X, r.Y, leftW, cpuH };
  UINT32 memY = r.Y + cpuH + gap;
  GFX_RECT mem = { r.X, memY, leftW, r.H - (memY - r.Y) > 40 ? r.H - (memY - r.Y) : 40 };
  UINT32 trunkX = r.X + leftW + (mini ? 14 : 22);
  UINT32 treeX = trunkX + (mini ? 12 : 18);
  UINT32 treeW = (r.X + r.W > treeX) ? r.X + r.W - treeX : 1;
  UINT32 visible = (r.H - 2) / rowH;
  UINT32 count;
  UINT32 k;
  UINT32 lastCenter;

  DrawCpuBlock(cpu, mini);
  DrawMemoryBlock(mem, mini);

  if (firstRow > gRowCount) {
    firstRow = 0;
  }
  count = gRowCount - firstRow < visible ? gRowCount - firstRow : visible;

  /* bus trunk from the processor down to the last drawn row */
  lastCenter = r.Y + (count ? (count - 1) * rowH + rowH / 2 : rowH / 2);
  GfxFillRect((GFX_RECT){ cpu.X + cpu.W, cpu.Y + cpu.H / 2 - 1, trunkX - cpu.X - cpu.W + 1, 2 },
              gTheme.Border);
  GfxFillRect((GFX_RECT){ trunkX, cpu.Y + cpu.H / 2, 2, lastCenter - (cpu.Y + cpu.H / 2) + 1 },
              gTheme.Border);
  if (count == 0) {
    GfxTextClipped(treeX, r.Y + 4, treeW, "未发现 PCI/PCIe 设备", gTheme.Muted, 1);
  }

  for (k = 0; k < count; k++) {
    UINT32 row = firstRow + k;
    PCI_DEV *dev = PciAt(gRows[row]);
    UINT32 y = r.Y + k * rowH;
    UINT32 indent = gDepths[row] * (mini ? 10 : 14);
    UINT32 nameX;
    UINT32 right;
    UINT32 colAddr;
    UINT32 colLink;
    UINT32 colAttach;
    char addr[24];
    char link[24];
    char attach[80];
    BOOLEAN sel = (row == selRow);

    if (!dev) {
      continue;
    }
    GfxFillRound((GFX_RECT){ treeX, y + 1, treeW, rowH - 3 },
                 mini ? 5 : 6, sel ? gTheme.PrimarySoft : ((k & 1) ? gTheme.PanelAlt : gTheme.Panel));
    GfxFillRect((GFX_RECT){ trunkX, y + rowH / 2 - 1, treeX - trunkX, 2 }, gTheme.Border);
    if (sel) {
      GfxFillRound((GFX_RECT){ treeX + 1, y + 5, 3, rowH - 10 }, 2, gTheme.Primary);
    }

    nameX = treeX + 10 + indent;
    right = treeX + treeW - 8;
    colAttach = right - (mini ? 0 : 168);
    colLink = colAttach - (mini ? 0 : 60);
    colAddr = colLink - (mini ? 56 : 62);

    UiIcon(KindIcon(dev->Kind), nameX, y + (rowH - 14) / 2, 14,
           sel ? gTheme.Primary : KindColor(dev->Kind));
    GfxTextClipped(nameX + 20, y + (rowH - 16) / 2,
                   colAddr > nameX + 26 ? colAddr - nameX - 26 : 1, L(dev->Name),
                   sel ? gTheme.PrimaryDark : gTheme.Text, 1);

    PciFormatAddress(dev, addr, sizeof(addr));
    GfxTextRight(colAddr + 54, y + (rowH - 16) / 2, addr, gTheme.Muted, 1);
    if (!mini && PciHasLink(dev)) {
      UiStrPrintf(link, sizeof(link), "Gen%u x%u", dev->LinkGen, dev->LinkWidth);
      GfxTextRight(colLink + 56, y + (rowH - 16) / 2, link, gTheme.SubText, 1);
    }
    if (!mini) {
      AttachText(dev, attach, sizeof(attach));
      if (attach[0]) {
        GfxTextClipped(colAttach, y + (rowH - 16) / 2, right - colAttach, attach,
                       dev->DiskIndex != PCI_NO_INDEX ? gTheme.Text : gTheme.SubText, 1);
      }
    }
  }
  return count;
}
