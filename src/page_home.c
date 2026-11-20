/* page_home.c - dashboard overview page. */
#include "pages.h"
#include "disk.h"
#include "results.h"
#include "pci.h"
#include "topo.h"

static UINT32 gHomeSel = 1;
static UINT32 gHomeTilesTop = 0;
static BOOLEAN gHomeEnumerated = FALSE;
static UINT32 gHomeTopoRow;    /* highlighted row in the compact diagram */
static UINT32 gHomeTopoFirst;  /* first visible row of the compact diagram */
static UINT32 gHomeTopoRows;

typedef struct {
  const char *Name;
  const char *Desc;
  UINT32      Page;
  UINT32      Icon;
} HOME_ENTRY;

static const HOME_ENTRY gHomeEntries[] = {
  { "系统信息", "SMBIOS / CPUID / 内存拓扑",       PAGE_SYSINFO, 1 },
  { "系统拓扑", "CPU / 内存 / PCIe 连接关系",      PAGE_TOPO,    11 },
  { "内存检测", "地址与数据图案写入回读自检",      PAGE_MEM,     2 },
  { "内存拓扑", "插槽 / rank / 颗粒级错误定位",     PAGE_DIMM,    3 },
  { "硬盘检测", "SMART / NVMe 健康与顺序读扫描",    PAGE_DISK,    4 },
  { "硬盘擦除", "DoD / Gutmann 多遍覆写 + 回读校验", PAGE_ERASE,   5 },
  { "CPU 检测", "整数 / FPU / 向量指令集自检",      PAGE_CPU,     6 },
  { "核间延迟", "处理器间缓存行往返延迟热力图",     PAGE_LATENCY, 15 },
  { "内存带宽", "核心到各内存域的带宽热力图",       PAGE_BW,      16 },
  { "压力测试", "多核 / 内存 / 硬盘持续加压计时",    PAGE_STRESS,  7 },
  { "工具集",   "启动管理器与外部诊断工具",         PAGE_TOOLS,   8 }
};

#define HOME_ENTRY_COUNT (sizeof(gHomeEntries) / sizeof(gHomeEntries[0]))

static UINT32 WorstLevel(VOID)
{
  UINT32 worst = 1;
  UINT32 lv[4];
  UINT32 i;
  lv[0] = gResults.HealthCpu;
  lv[1] = gResults.HealthMem;
  lv[2] = gResults.HealthDisk;
  lv[3] = gResults.HealthFw;
  for (i = 0; i < 4; i++) {
    if (lv[i] > worst) {
      worst = lv[i];
    }
  }
  return worst;
}

static const char *StatusTextFor(UINT32 page)
{
  switch (page) {
  case PAGE_MEM: return gResults.MemTested ? (gResults.MemErrorCount ? "发现错误" : "已通过") : "未运行";
  case PAGE_DIMM: return gApp.Sys.DimmPresentCount ? "已识别" : "无信息";
  case PAGE_TOPO: return PciCount() ? "已识别" : "无信息";
  case PAGE_DISK: return gResults.DiskTested ? (gResults.DiskErrorCount ? "发现错误" : "已通过") : "未运行";
  case PAGE_ERASE: return gResults.EraseCount ? "已完成" : "未运行";
  case PAGE_CPU: return gResults.CpuTested ? (gResults.CpuErrorCount ? "发现错误" : "已通过") : "未运行";
  case PAGE_LATENCY: return gResults.LatTested ? "已测量" : "未运行";
  case PAGE_BW: return gResults.BwTested ? "已测量" : "未运行";
  case PAGE_STRESS:
    return gResults.StressRan ? (gResults.StressErrorCount ? "发现错误" : "已通过") : "未运行";
  default: return "就绪";
  }
}

static UINT32 StatusLevelFor(UINT32 page)
{
  switch (page) {
  case PAGE_MEM: return gResults.MemTested ? (gResults.MemErrorCount ? 3 : 1) : 0;
  case PAGE_DIMM: return gApp.Sys.DimmPresentCount ? 1 : 0;
  case PAGE_TOPO: return PciCount() ? 1 : 0;
  case PAGE_DISK: return gResults.DiskTested ? (gResults.DiskErrorCount ? 3 : 1) : 0;
  case PAGE_ERASE: return gResults.EraseCount ? 1 : 0;
  case PAGE_CPU: return gResults.CpuTested ? (gResults.CpuErrorCount ? 3 : 1) : 0;
  case PAGE_LATENCY: return gResults.LatTested ? 1 : 0;
  case PAGE_BW: return gResults.BwTested ? 1 : 0;
  case PAGE_STRESS: return gResults.StressRan ? (gResults.StressErrorCount ? 3 : 1) : 0;
  default: return 1;
  }
}

VOID PageHomeEnter(VOID)
{
  if (!gHomeEnumerated) {
    DiskEnumerate();
    gHomeEnumerated = TRUE;
  }
  PciBindAssets();
  TopoRefresh();
  if (gHomeTopoRow >= TopoRowCount()) {
    gHomeTopoRow = 0;
  }
}

static VOID DrawHeroCard(GFX_RECT r, UINT32 icon, const char *title, const char *value,
                         const char *sub, UINT32 level)
{
  GFX_RECT inner;
  UiCard(r, 0, 0);
  GfxFillRound((GFX_RECT){ r.X + 14, r.Y + 14, 34, 34 }, 9, gTheme.PrimarySoft);
  UiIcon(icon, r.X + 22, r.Y + 22, 18, gTheme.Primary);
  GfxText(r.X + 58, r.Y + 14, title, gTheme.SubText, 1);
  if (level) {
    GfxFillCircle(r.X + r.W - 18, r.Y + 20, 4, UiLevelColor(level));
  }
  GfxTextClipped(r.X + 58, r.Y + 34, r.W - 74, value, gTheme.Text, 1);
  inner.X = r.X + 16;
  inner.Y = r.Y + 66;
  inner.W = r.W - 32;
  inner.H = r.H - 74;
  GfxTextClipped(inner.X, inner.Y, inner.W, sub, gTheme.Muted, 1);
}

static VOID FillComponentCounts(UINT32 *ok, UINT32 *warn, UINT32 *crit, UINT32 *off)
{
  UINT32 lv[5];
  UINT32 i;
  lv[0] = gResults.HealthCpu;
  lv[1] = gResults.HealthMem;
  lv[2] = gResults.HealthDisk;
  lv[3] = gResults.HealthFw;
  lv[4] = gApp.Sys.BootRoot ? 1 : 2;
  *ok = *warn = *crit = *off = 0;
  for (i = 0; i < 5; i++) {
    switch (lv[i]) {
    case 1: (*ok)++; break;
    case 2: (*warn)++; break;
    case 3: (*crit)++; break;
    default: (*off)++; break;
    }
  }
}

VOID PageHomeDraw(GFX_RECT content)
{
  UINT32 gap = 12;
  UINT32 heroH = 96;
  UINT32 heroW = (content.W - gap * 3) / 4;
  UINT32 y = content.Y;
  UINT32 i;
  char buf[128];
  char buf2[128];
  UINT32 ok, warn, crit, off;
  GFX_RECT healthCard, listCard;

  /* ---- hero cards ---- */
  UiStrPrintf(buf, sizeof(buf), "%s", gApp.Sys.CpuBrand[0] ? gApp.Sys.CpuBrand : L("未知处理器"));
  UiStrPrintf(buf2, sizeof(buf2), "%u 核 / %u 线程 · 基频 %u MHz",
              gApp.Sys.Cores, gApp.Sys.Threads, gApp.Sys.CpuBaseMhz);
  DrawHeroCard((GFX_RECT){ content.X, y, heroW, heroH }, 5, "处理器", buf, buf2, gResults.HealthCpu);

  if (gApp.Sys.InstalledRam) {
    GfxFormatSize(buf, sizeof(buf), gApp.Sys.InstalledRam);
  } else {
    GfxFormatSize(buf, sizeof(buf), gApp.Sys.UsableRam);
  }
  if (gApp.Sys.DominantMemSpeed) {
    UiStrPrintf(buf2, sizeof(buf2), "%s%s · %u 条内存 · %u MT/s",
                SysMemTypeName(gApp.Sys.DominantMemType),
                gApp.Sys.DimmPresentCount && gApp.Sys.Dimms[0].Ecc ? " ECC" : "",
                gApp.Sys.DimmPresentCount, gApp.Sys.DominantMemSpeed);
  } else {
    UiStrPrintf(buf2, sizeof(buf2), "%s%s · %u 条内存",
                SysMemTypeName(gApp.Sys.DominantMemType),
                gApp.Sys.DimmPresentCount && gApp.Sys.Dimms[0].Ecc ? " ECC" : "",
                gApp.Sys.DimmPresentCount);
  }
  DrawHeroCard((GFX_RECT){ content.X + heroW + gap, y, heroW, heroH }, 2, "内存", buf, buf2,
               gResults.HealthMem);

  if (DiskCount() == 0) {
    UiStrPrintf(buf, sizeof(buf), "未发现设备");
    UiStrPrintf(buf2, sizeof(buf2), "无块设备可用");
  } else {
    UiStrPrintf(buf, sizeof(buf), "%u 个设备", DiskCount());
    {
      UINT64 total = 0;
      for (i = 0; i < DiskCount(); i++) {
        total += DiskAt(i)->SizeBytes;
      }
      GfxFormatSize(buf2, sizeof(buf2), total);
      UiStrPrintf(buf2 + GfxStrLen(buf2), sizeof(buf2) - GfxStrLen(buf2), " · %s",
                  DiskIfaceName(DiskAt(0)->Iface));
    }
  }
  DrawHeroCard((GFX_RECT){ content.X + (heroW + gap) * 2, y, heroW, heroH }, 3, "存储", buf, buf2,
               gResults.HealthDisk);

  UiStrPrintf(buf, sizeof(buf), "UEFI %u.%u", gApp.Sys.FirmwareRevision >> 16,
              (gApp.Sys.FirmwareRevision >> 8) & 0xFF);
  UiStrPrintf(buf2, sizeof(buf2), "%s · SMBIOS %s",
              gApp.Sys.FirmwareVendor[0] ? gApp.Sys.FirmwareVendor : L("固件"),
              gApp.Sys.SmbiosAvailable ? L("可用") : L("不可用"));
  DrawHeroCard((GFX_RECT){ content.X + (heroW + gap) * 3, y, heroW, heroH }, 0, "固件", buf, buf2,
               gResults.HealthFw);

  /* ---- health + quick tests ---- */
  y += heroH + gap;
  healthCard.X = content.X;
  healthCard.Y = y;
  healthCard.W = 296;
  healthCard.H = content.H - heroH - gap - 168 - gap - 24;
  listCard.X = content.X + healthCard.W + gap;
  listCard.Y = y;
  listCard.W = content.W - healthCard.W - gap;
  listCard.H = healthCard.H;

  UiCard(healthCard, "组件健康概览", 0);
  FillComponentCounts(&ok, &warn, &crit, &off);
  {
    GFX_RECT donut = { healthCard.X + 20, healthCard.Y + 44, 120, 120 };
    GFX_RECT tile;
    UINT32 ty = healthCard.Y + 46;
    const char *levels[4] = { "正常", "注意", "异常", "未检测" };
    UINT32 colors[4];
    UINT32 counts[4];
    colors[0] = gTheme.Ok;
    colors[1] = gTheme.Warn;
    colors[2] = gTheme.Crit;
    colors[3] = gTheme.Idle;
    counts[0] = ok;
    counts[1] = warn;
    counts[2] = crit;
    counts[3] = off;
    UiHealthDonut(donut, ok, warn, crit, off,
                  (ok + warn + crit) == 0 ? "未检测" : (WorstLevel() >= 3 ? "异常" : (WorstLevel() == 2 ? "注意" : "良好")));
    tile.X = healthCard.X + 146;
    tile.W = healthCard.W - 156;
    tile.H = 26;
    for (i = 0; i < 4; i++) {
      tile.Y = ty;
      UiStatTile(tile, colors[i], levels[i], counts[i]);
      ty += 30;
    }
    gHomeTilesTop = ty;
  }
  {
    UINT32 borderY = gHomeTilesTop + 6;
    UiDivider((GFX_RECT){ healthCard.X + 20, borderY, healthCard.W - 40, 1 });
    UiStrPrintf(buf, sizeof(buf), "内存覆盖：%u%% · 带宽 %u MB/s",
                (gResults.MemCoveragePm + 5) / 10, gResults.MemBandwidthMbs);
    GfxTextClipped(healthCard.X + 20, borderY + 12, healthCard.W - 40, buf, gTheme.SubText, 1);
    UiStrPrintf(buf, sizeof(buf), "最近扫描：%u 设备 · 平均 %u MB/s",
                gResults.DiskScannedDevices, gResults.DiskAvgMbs);
    GfxTextClipped(healthCard.X + 20, borderY + 32, healthCard.W - 40, buf, gTheme.SubText, 1);
  }

  UiCard(listCard, "快速检测", "Enter 打开");
  {
    GFX_RECT row;
    UINT32 ry = listCard.Y + 40;
    row.X = listCard.X + 10;
    row.W = listCard.W - 20;
    row.H = 30;
    for (i = 0; i < HOME_ENTRY_COUNT && ry + row.H < listCard.Y + listCard.H - 6; i++) {
      BOOLEAN sel = (gHomeSel == i);
      row.Y = ry;
      if (sel) {
        GfxFillRound(row, 8, gTheme.PrimarySoft);
      }
      UiIcon(gHomeEntries[i].Icon, row.X + 12, row.Y + 6, 18,
             sel ? gTheme.Primary : gTheme.SubText);
      GfxText(row.X + 40, row.Y + 7, gHomeEntries[i].Name,
              sel ? gTheme.PrimaryDark : gTheme.Text, 1);
      {
        UINT32 descX = row.X + 40 + GfxTextWidth(gHomeEntries[i].Name, 1) + 12;
        UINT32 limit = row.X + row.W - 92;
        GfxTextClipped(descX, row.Y + 7, limit > descX ? limit - descX : 1,
                       gHomeEntries[i].Desc, gTheme.Muted, 1);
      }
      {
        GFX_RECT pill = { row.X + row.W - 84, row.Y + 6, 72, 18 };
        UiPill(pill, UiLevelColor(StatusLevelFor(gHomeEntries[i].Page)),
               StatusTextFor(gHomeEntries[i].Page));
      }
      UiHitRow(row, i);
      ry += row.H + 2;
    }
  }

  /* ---- hardware topology + platform summary ---- */
  {
    GFX_RECT topo;
    char line[160];
    char size[24];

    topo.X = content.X;
    topo.Y = content.Y + content.H - 24 - 168;
    topo.W = content.W;
    topo.H = 168;
    UiCard(topo, "系统拓扑", "T 打开完整视图");
    UiHitNav(topo, PAGE_TOPO);

    {
      GFX_RECT inner = UiCardInner(topo);
      GFX_RECT area = inner;
      UINT32 visible;
      area.H = inner.H > 22 ? inner.H - 20 : inner.H;
      visible = area.H / TopoRowHeight(TRUE);
      if (visible > 0) {
        area.H = visible * TopoRowHeight(TRUE);
      }
      if (gHomeTopoRow < gHomeTopoFirst) {
        gHomeTopoFirst = gHomeTopoRow;
      }
      if (visible && gHomeTopoRow >= gHomeTopoFirst + visible) {
        gHomeTopoFirst = gHomeTopoRow - visible + 1;
      }
      gHomeTopoRows = TopoDraw(area, gHomeTopoFirst, gHomeTopoRow, TRUE);

      GfxFormatSize(size, sizeof(size), gApp.Sys.InstalledRam);
      UiStrPrintf(line, sizeof(line), "%s %s · %s %s · 内存 %s · %s",
                  gApp.Sys.SysVendor[0] ? gApp.Sys.SysVendor : "n/a",
                  gApp.Sys.SysProduct[0] ? gApp.Sys.SysProduct : "",
                  L("引导卷"),
                  gApp.Sys.BootVolumeName[0] ? gApp.Sys.BootVolumeName : "n/a", size,
                  gApp.Sys.BiosVersion);
      if (TopoRowCount() > gHomeTopoRows) {
        char more[40];
        UiStrPrintf(more, sizeof(more), "还有 %u 个设备 · 按 T 打开完整连接图",
                    TopoRowCount() - gHomeTopoRows);
        GfxTextRight(inner.X + inner.W, inner.Y + inner.H - 16, more, gTheme.Muted, 1);
      }
      GfxTextClipped(inner.X, inner.Y + inner.H - 16, inner.W * 60 / 100, line, gTheme.Muted, 1);
    }
  }
  UiStrPrintf(buf, sizeof(buf),
              "共 12 个页面 · 空格 勾选 · Enter 打开 · F2 导出报告 · 内存覆盖 %u%%",
              (gResults.MemCoveragePm + 5) / 10);
  GfxTextClipped(content.X, content.Y + content.H - 17, content.W, buf, gTheme.Muted, 1);
}

VOID PageHomePoint(UINT32 row)
{
  if (row < HOME_ENTRY_COUNT) {
    gHomeSel = row;
  }
}

BOOLEAN PageHomeKey(EFI_INPUT_KEY key)
{
  if (key.ScanCode == SCAN_UP) {
    if (gHomeSel == 0) {
      gHomeSel = HOME_ENTRY_COUNT - 1;
    } else {
      gHomeSel--;
    }
    return TRUE;
  }
  if (key.ScanCode == SCAN_DOWN) {
    gHomeSel = (gHomeSel + 1) % HOME_ENTRY_COUNT;
    return TRUE;
  }
  if (key.UnicodeChar == 't' || key.UnicodeChar == 'T') {
    UiNavigationJump(PAGE_TOPO);
    return TRUE;
  }
  if (key.UnicodeChar == '\r' || key.UnicodeChar == '\n') {
    UiNavigationJump(gHomeEntries[gHomeSel].Page);
    return TRUE;
  }
  return FALSE;
}
