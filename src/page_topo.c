/* page_topo.c - system topology: processor, memory and the PCIe device tree. */
#include "pages.h"
#include "results.h"
#include "disk.h"
#include "pci.h"
#include "topo.h"

static UINT32 gTopoSel;
static UINT32 gTopoFirst;

/* keep the window where the user dropped the thumb; ClampScroll() then re-anchors
   the top row to the selection, so the selection goes to the bottom row */
static VOID TopoScrollList(UINT32 first, UINT32 visible)
{
  UINT32 target = first + (visible ? visible - 1 : 0);
  if (target >= TopoRowCount()) {
    target = TopoRowCount() ? TopoRowCount() - 1 : 0;
  }
  gTopoSel = target;
  gTopoFirst = first;
}

static VOID ClampScroll(UINT32 visible)
{
  if (TopoRowCount() == 0) {
    gTopoSel = 0;
    gTopoFirst = 0;
    return;
  }
  if (gTopoSel >= TopoRowCount()) {
    gTopoSel = TopoRowCount() - 1;
  }
  if (gTopoSel < gTopoFirst) {
    gTopoFirst = gTopoSel;
  }
  if (visible && gTopoSel >= gTopoFirst + visible) {
    gTopoFirst = gTopoSel - visible + 1;
  }
}

static VOID Rescan(VOID)
{
  DiskEnumerate();
  PciEnumerate();
  PciBindAssets();
  TopoRefresh();
  ClampScroll(1);
}

VOID PageTopoEnter(VOID)
{
  char buf[96];
  Rescan();
  UiStrPrintf(buf, sizeof(buf), "%u 个 PCI/PCIe 设备 · %u 个端点 · %u 个网络接口",
              PciCount(), PciEndpointCount(), NetCount());
  UiSetStatus(0, buf);
}

static VOID KVRow(GFX_RECT *col, const char *key, const char *value, UINT32 color)
{
  UINT32 kw = GfxTextWidth(key, 1) + 12;
  GfxText(col->X, col->Y, key, gTheme.Muted, 1);
  GfxTextClipped(col->X + kw, col->Y, col->W > kw ? col->W - kw : 1, value, color, 1);
  col->Y += 22;
}

static VOID DrawDetail(GFX_RECT card, UINT32 row)
{
  GFX_RECT inner = UiCardInner(card);
  PCI_DEV *dev = row < TopoRowCount() ? PciAt(TopoRowDevice(row)) : 0;
  GFX_RECT col = inner;
  char buf[96];
  char addr[24];

  if (!dev) {
    GfxText(inner.X, inner.Y, "未发现 PCI/PCIe 设备", gTheme.Warn, 1);
    return;
  }
  col.W = inner.W / 2 - 8;
  PciFormatAddress(dev, addr, sizeof(addr));
  KVRow(&col, "地址", addr, gTheme.Text);
  UiStrPrintf(buf, sizeof(buf), "%s", L(PciClassText(dev)));
  KVRow(&col, "类别", buf, gTheme.Text);
  {
    const char *vendor = PciVendorName(dev->VendorId);
    UiStrPrintf(buf, sizeof(buf), "%s · %04X:%04X", vendor ? vendor : L("未知厂商"),
                dev->VendorId, dev->DeviceId);
    KVRow(&col, "厂商与设备", buf, gTheme.Text);
  }
  UiStrPrintf(buf, sizeof(buf), "类 %02X:%02X · 编程接口 %02X · 修订 %02X", dev->Class,
              dev->SubClass, dev->ProgIf, dev->Revision);
  KVRow(&col, "标识", buf, gTheme.SubText);
  if (PciHasLink(dev)) {
    UiStrPrintf(buf, sizeof(buf), "PCIe Gen%u x%u", dev->LinkGen, dev->LinkWidth);
  } else {
    UiStrPrintf(buf, sizeof(buf), "%s", L("非 PCIe 链路"));
  }
  KVRow(&col, "链路", buf, PciHasLink(dev) ? gTheme.PrimaryDark : gTheme.Muted);

  col = inner;
  col.X = inner.X + inner.W / 2 + 8;
  col.W = inner.W / 2 - 8;
  if (dev->DiskIndex != PCI_NO_INDEX) {
    DISK_DEV *disk = DiskAt(dev->DiskIndex);
    char size[24];
    GfxFormatSize(size, sizeof(size), disk->SizeBytes);
    UiStrPrintf(buf, sizeof(buf), "%s · %s", disk->Model[0] ? disk->Model : L("块设备"), size);
    KVRow(&col, "挂载块设备", buf, gTheme.Text);
    UiStrPrintf(buf, sizeof(buf), "%s · %s", DiskIfaceName(disk->Iface),
                disk->BootVolume ? L("当前启动盘") : L("数据盘"));
    KVRow(&col, "存储接口", buf, gTheme.SubText);
    KVRow(&col, "健康状态", disk->HealthText[0] ? disk->HealthText : L("尚未读取"),
          disk->HealthLevel == 1 ? gTheme.Ok : (disk->HealthLevel ? gTheme.Warn : gTheme.Muted));
  } else if (dev->NetIndex != PCI_NO_INDEX) {
    NET_DEV *net = NetAt(dev->NetIndex);
    char mac[24];
    PciFormatMac(net->Mac, mac, sizeof(mac));
    KVRow(&col, "MAC 地址", mac, gTheme.Text);
    KVRow(&col, "链路状态", net->MediaPresent ? "链路已连接" : "链路未连接",
          net->MediaPresent ? gTheme.Ok : gTheme.Warn);
    KVRow(&col, "接口", L(net->Name), gTheme.SubText);
  } else if (dev->IsBridge) {
    UiStrPrintf(buf, sizeof(buf), "次总线 %u · 从属总线 %u", dev->SecondaryBus, dev->SubordinateBus);
    KVRow(&col, "下游总线", dev->IsRoot ? L("全部总线") : buf, gTheme.SubText);
    UiStrPrintf(buf, sizeof(buf), "%u", dev->IsRoot ? PciCount() - 1
                                                   : TopoChildCount(TopoRowDevice(row)));
    KVRow(&col, "挂载设备", buf, gTheme.Muted);
    if (dev->IsRoot && NetCount() && !PciClassCount(PCI_KIND_NET)) {
      /* firmware bound the NICs but never published a PCI node for them, so
         they cannot be placed in the tree - say so instead of hiding them */
      UiStrPrintf(buf, sizeof(buf), "%u 个网络接口只有固件协议栈，没有对应的 PCI 节点",
                  NetCount());
      KVRow(&col, "说明", buf, gTheme.Warn);
    } else {
      KVRow(&col, "说明", "桥的下游设备在左侧树中缩进显示", gTheme.Muted);
    }
  } else {
    KVRow(&col, "挂载块设备", "无", gTheme.Muted);
    KVRow(&col, "说明", "该设备未被本套件直接使用", gTheme.Muted);
  }
}

VOID PageTopoDraw(GFX_RECT content)
{
  UINT32 gap = 12;
  UINT32 diagH = (content.H * 56) / 100;
  GFX_RECT diag = { content.X, content.Y, content.W, diagH };
  GFX_RECT detail = { content.X, content.Y + diagH + gap, content.W, content.H - diagH - gap };
  GFX_RECT inner;
  UINT32 visible;
  char buf[96];

  if (TopoHiddenCount()) {
    UiStrPrintf(buf, sizeof(buf), "%u 个设备 · %u 个端点 · 隐藏 %u 个空端口", PciCount(),
                PciEndpointCount(), TopoHiddenCount());
  } else {
    UiStrPrintf(buf, sizeof(buf), "%u 个设备 · %u 个端点 · %u 条链路", PciCount(),
                PciEndpointCount(), PciRootCount());
  }
  UiCard(diag, "连接图", buf);
  inner = UiCardInner(diag);
  visible = inner.H / TopoRowHeight(FALSE);
  ClampScroll(visible);
  TopoDraw(inner, gTopoFirst, gTopoSel, FALSE);
  {
    UINT32 rowH = TopoRowHeight(FALSE);
    UINT32 row;
    for (row = gTopoFirst; row < TopoRowCount() && row < gTopoFirst + visible; row++) {
      UiHitRow((GFX_RECT){ inner.X, inner.Y + (row - gTopoFirst) * rowH, inner.W, rowH }, row);
    }
  }
  if (TopoRowCount() > visible) {
    UiScrollbarScroll((GFX_RECT){ diag.X + diag.W - 8, inner.Y, 4, inner.H }, gTopoFirst,
                      visible, TopoRowCount(), TopoScrollList);
  }

  UiStrPrintf(buf, sizeof(buf), "存储 %u · 网络 %u · 显示 %u · USB %u",
              PciClassCount(PCI_KIND_STORAGE), NetCount(),
              PciClassCount(PCI_KIND_DISPLAY), PciClassCount(PCI_KIND_USB));
  UiCard(detail, "设备详情", buf);
  DrawDetail(detail, gTopoSel);
}

VOID PageTopoPoint(UINT32 row)
{
  if (row < TopoRowCount()) {
    gTopoSel = row;
  }
}

BOOLEAN PageTopoKey(EFI_INPUT_KEY key)
{
  char buf[96];

  switch (key.ScanCode) {
  case SCAN_UP:
    if (gTopoSel > 0) {
      gTopoSel--;
    }
    return TRUE;
  case SCAN_DOWN:
    if (gTopoSel + 1 < TopoRowCount()) {
      gTopoSel++;
    }
    return TRUE;
  case SCAN_F5:
  case SCAN_F9:
    Rescan();
    UiSetStatus(1, "PCI/PCIe 拓扑已重新扫描");
    return TRUE;
  default:
    break;
  }
  if (key.UnicodeChar == 'h' || key.UnicodeChar == 'H') {
    /* hidden root ports are the ones with nothing plugged in */
    UINT32 keep = (gTopoSel < TopoRowCount()) ? TopoRowDevice(gTopoSel) : PCI_NO_INDEX;
    INT32 found;
    TopoSetHideEmpty(TopoHideEmpty() ? FALSE : TRUE);
    TopoRefresh();
    found = (keep == PCI_NO_INDEX) ? -1 : TopoFindRow(keep);
    gTopoSel = (found >= 0) ? (UINT32)found : 0;
    ClampScroll(1);
    if (TopoHideEmpty()) {
      UiStrPrintf(buf, sizeof(buf), "已隐藏 %u 个下游无设备的 root port", TopoHiddenCount());
      UiSetStatus(1, buf);
    } else {
      UiSetStatus(0, "已显示全部 root port，包括下游没有设备的端口");
    }
    return TRUE;
  }
  if (key.UnicodeChar == '\r' || key.UnicodeChar == '\n') {
    if (gTopoSel < TopoRowCount()) {
      PCI_DEV *dev = PciAt(TopoRowDevice(gTopoSel));
      if (dev && dev->DiskIndex != PCI_NO_INDEX) {
        PageDiskFocus(dev->DiskIndex);
        UiSwitchPage(PAGE_DISK);
        UiSetStatus(0, "已定位到该控制器挂载的块设备");
        return TRUE;
      }
      if (dev && dev->NetIndex != PCI_NO_INDEX) {
        UiSetStatus(0, "网卡不做链路吞吐测试，MAC 与链路状态见下方设备详情");
        return TRUE;
      }
    }
    UiSetStatus(0, "存储控制器可按 Enter 跳到硬盘检测页");
    return TRUE;
  }
  return FALSE;
}
