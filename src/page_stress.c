/* page_stress.c - sustained multi component load test. */
#include "pages.h"
#include "stress.h"
#include "disk.h"
#include "results.h"

#define GROUP_CPU  0
#define GROUP_MEM  1
#define GROUP_DISK 2
#define GROUP_COUNT 3

static STRESS_CONFIG gCfg;
static BOOLEAN gCfgInit;
static UINT32  gGroup;          /* focused group in the target list */
static UINT32  gItem;           /* focused item inside that group   */
static BOOLEAN gRunning;
static STRESS_CONFIG gRunCfg;

static const char *const gGroupName[GROUP_COUNT] = { "CPU", "内存", "硬盘" };

static UINT32 GroupCount(UINT32 group)
{
  switch (group) {
  case GROUP_CPU:  return StressCpuCount();
  case GROUP_MEM:  return StressDimmCount() > STRESS_MAX_DIMM ? STRESS_MAX_DIMM : StressDimmCount();
  default:         return DiskCount() > STRESS_MAX_DISK ? STRESS_MAX_DISK : DiskCount();
  }
}

static BOOLEAN ItemOn(UINT32 group, UINT32 index)
{
  switch (group) {
  case GROUP_CPU:  return StressCpuSelected(gCfg.CpuMask, index);
  case GROUP_MEM:  return (gCfg.DimmMask & (1u << index)) ? TRUE : FALSE;
  default:         return (gCfg.DiskMask & (1u << index)) ? TRUE : FALSE;
  }
}

static VOID SetItem(UINT32 group, UINT32 index, BOOLEAN on)
{
  switch (group) {
  case GROUP_CPU:
    StressCpuSetMask(gCfg.CpuMask, index, on);
    break;
  case GROUP_MEM:
    if (on) {
      gCfg.DimmMask |= (1u << index);
    } else {
      gCfg.DimmMask &= ~(1u << index);
    }
    break;
  default:
    if (on) {
      gCfg.DiskMask |= (1u << index);
    } else {
      gCfg.DiskMask &= ~(1u << index);
    }
    break;
  }
}

static UINT32 SelectedCount(UINT32 group)
{
  UINT32 n = 0, i, count = GroupCount(group);
  for (i = 0; i < count; i++) {
    if (ItemOn(group, i)) {
      n++;
    }
  }
  return n;
}

static BOOLEAN GroupOn(UINT32 group)
{
  switch (group) {
  case GROUP_CPU:  return gCfg.CpuOn;
  case GROUP_MEM:  return gCfg.MemOn;
  default:         return gCfg.DiskOn;
  }
}

static VOID SetGroup(UINT32 group, BOOLEAN on)
{
  UINT32 i, count;
  switch (group) {
  case GROUP_CPU:
    gCfg.CpuOn = on;
    break;
  case GROUP_MEM:
    gCfg.MemOn = on;
    break;
  default:
    gCfg.DiskOn = on;
    break;
  }
  if (!on) {
    return;
  }
  /* turning a group on with nothing selected selects everything in it */
  if (SelectedCount(group) == 0) {
    count = GroupCount(group);
    for (i = 0; i < count; i++) {
      SetItem(group, i, TRUE);
    }
  }
}

static VOID ConfigDefaults(VOID)
{
  UINT32 i, count;

  if (gCfgInit) {
    return;
  }
  gCfg.CpuOn = TRUE;
  gCfg.MemOn = TRUE;
  gCfg.DiskOn = FALSE;
  gCfg.Seconds = 300;
  gCfg.MemBytesPerDimm = 0;

  count = GroupCount(GROUP_CPU);
  for (i = 0; i < count; i++) {
    SetItem(GROUP_CPU, i, TRUE);
  }
  count = GroupCount(GROUP_MEM);
  for (i = 0; i < count; i++) {
    SetItem(GROUP_MEM, i, TRUE);
  }
  count = GroupCount(GROUP_DISK);
  for (i = 0; i < count; i++) {
    SetItem(GROUP_DISK, i, FALSE);
  }
  gCfgInit = TRUE;
}

/* -------------------------------------------------------------- execution */
static BOOLEAN StressTick(STRESS_STATE *st)
{
  (VOID)st;
  if (UiPump() || gStopRequested) {
    return FALSE;
  }
  UiDrawEverything();
  return TRUE;
}

static VOID RunStress(VOID)
{
  EFI_STATUS status;
  char buf[128];
  UINT32 seconds = gCfg.Seconds;

  if (!gCfg.CpuOn && !gCfg.MemOn && !gCfg.DiskOn) {
    UiSetStatus(2, "请至少选择一个组件（C / M / D 开启）");
    return;
  }
  if ((gCfg.CpuOn && SelectedCount(GROUP_CPU) == 0) ||
      (gCfg.MemOn && SelectedCount(GROUP_MEM) == 0) ||
      (gCfg.DiskOn && SelectedCount(GROUP_DISK) == 0)) {
    UiSetStatus(2, "已选组件缺少目标项目，请用空格勾选");
    return;
  }
  if (gCfg.DiskOn) {
    if (!UiConfirm("硬盘压力测试",
                   "硬盘压力使用只读随机读取，不会写入或擦除数据。\n"
                   "同时运行多块盘的随机读可能降低其他组件的测量精度。",
                   "GO")) {
      UiSetStatus(2, "已取消压力测试");
      return;
    }
  }

  gRunCfg = gCfg;
  gRunning = TRUE;
  gStopRequested = FALSE;
  UiStrPrintf(buf, sizeof(buf), "压力测试开始，计划运行 %u 分 %u 秒",
              seconds / 60, seconds % 60);
  UiSetStatus(0, buf);
  status = StressRun(&gRunCfg, StressTick);
  gRunning = FALSE;
  gStopRequested = FALSE;

  if (status == EFI_INVALID_PARAMETER) {
    UiSetStatus(2, "压力测试参数无效");
  } else if (gStress.ErrorCount) {
    UiStrPrintf(buf, sizeof(buf), "压力测试%s · %u 秒 · 捕获 %u 个异常",
                gStress.Aborted ? L("中止") : L("完成"), gStress.ElapsedMs / 1000,
                gStress.ErrorCount);
    UiSetStatus(3, buf);
  } else if (gStress.Aborted) {
    UiStrPrintf(buf, sizeof(buf), "压力测试已中止 · 运行 %u 秒 · 未发现异常",
                gStress.ElapsedMs / 1000);
    UiSetStatus(2, buf);
  } else if (status == EFI_SUCCESS) {
    UiStrPrintf(buf, sizeof(buf), "压力测试通过 · 运行 %u 秒 · 无异常",
                gStress.ElapsedMs / 1000);
    UiSetStatus(1, buf);
  } else {
    UiSetStatus(2, "压力测试提前结束（没有可用的测试内存）");
  }
  UiRedraw();
}

/* --------------------------------------------------------------- drawing */
static void FormatDuration(char *buf, UINTN cap, UINT32 seconds)
{
  if (seconds >= 60) {
    UiStrPrintf(buf, cap, "%u 分 %02u 秒", seconds / 60, seconds % 60);
  } else {
    UiStrPrintf(buf, cap, "%u 秒", seconds);
  }
}

static VOID DrawConfig(GFX_RECT card)
{
  GFX_RECT inner = UiCardInner(card);
  UINT32 i;
  UINT32 y = inner.Y;
  char buf[64];

  for (i = 0; i < GROUP_COUNT; i++) {
    GFX_RECT row = { inner.X, y, inner.W, 34 };
    BOOLEAN on = GroupOn(i);
    UINT32 selCount = SelectedCount(i);
    UINT32 total = GroupCount(i);
    if (i == gGroup) {
      GfxFillRound(row, 8, gTheme.PrimarySoft);
    }
    UiCheck((GFX_RECT){ row.X + 8, row.Y + 10, 14, 14 }, on);
    GfxText(row.X + 32, row.Y + 8, L(gGroupName[i]), on ? gTheme.Text : gTheme.Muted, 1);
    UiStrPrintf(buf, sizeof(buf), "%u / %u", selCount, total);
    GfxTextRight(row.X + row.W - 4, row.Y + 8, buf,
                 on ? (selCount ? gTheme.SubText : gTheme.Warn) : gTheme.Muted, 1);
    y += 36;
  }

  y += 4;
  UiDivider((GFX_RECT){ inner.X, y, inner.W, 1 });
  y += 12;

  GfxText(inner.X, y, "持续时长", gTheme.SubText, 1);
  FormatDuration(buf, sizeof(buf), gCfg.Seconds);
  GfxTextRight(inner.X + inner.W, y, buf, gTheme.Text, 1);
  y += 22;
  GfxText(inner.X, y, "- / = 调 1 分 · PgUp/PgDn 调 5 分", gTheme.Muted, 1);
  y += 20;
  GfxText(inner.X, y, "硬盘只做随机读，不写入数据", gTheme.Muted, 1);
}

/* the window is derived from the focused item, so the dropped row becomes the
   focus at the bottom of the window */
static VOID StressScrollTargets(UINT32 first, UINT32 visible)
{
  UINT32 count = GroupCount(gGroup);
  UINT32 target = first + (visible ? visible - 1 : 0);
  if (target >= count) {
    target = count ? count - 1 : 0;
  }
  gItem = target;
}

static VOID DrawTargets(GFX_RECT card)
{
  GFX_RECT inner = UiCardInner(card);
  UINT32 count = GroupCount(gGroup);
  UINT32 rowH = 46;
  UINT32 visible = inner.H / rowH;
  UINT32 first;
  UINT32 i;
  char buf[80];
  char buf2[80];

  if (gItem >= count && count) {
    gItem = count - 1;
  }
  first = 0;
  if (visible && gItem >= visible) {
    first = gItem - visible + 1;
  }
  GfxText(inner.X, inner.Y, gGroup == GROUP_CPU ? "逻辑处理器" :
                            (gGroup == GROUP_MEM ? "内存插槽" : "块设备"), gTheme.Muted, 1);
  GfxTextRight(inner.X + inner.W, inner.Y, "选择目标", gTheme.Muted, 1);
  {
    UINT32 y = inner.Y + 20;
    UiDivider((GFX_RECT){ inner.X, y - 4, inner.W, 1 });
    for (i = first; i < count && (i - first) < visible; i++) {
      GFX_RECT row = { inner.X, y, inner.W, rowH - 4 };
      BOOLEAN on = ItemOn(gGroup, i);
      UiHitRowBox(row, (gGroup << 8) | i);
      if (i == gItem) {
        GfxFillRound(row, 8, gTheme.PrimarySoft);
      } else if ((i - first) & 1) {
        GfxFillRound(row, 8, gTheme.PanelAlt);
      }
      UiCheck((GFX_RECT){ row.X + 8, row.Y + 8, 14, 14 }, on);

      if (gGroup == GROUP_CPU) {
        UINT32 perSocket = gApp.Sys.CpuPackageCount
                               ? gApp.Sys.LogicalCpus / gApp.Sys.CpuPackageCount : 0;
        if (perSocket) {
          UiStrPrintf(buf, sizeof(buf), "S%u · 逻辑处理器 %u%s",
                      i / perSocket, i % perSocket, i == 0 ? L(" · 主处理器") : "");
        } else {
          UiStrPrintf(buf, sizeof(buf), "逻辑处理器 %u%s", i,
                      i == 0 ? L(" · 主处理器") : "");
        }
        if (i == 0 && (gCfg.MemOn || gCfg.DiskOn)) {
          UiStrPrintf(buf2, sizeof(buf2), "主处理器 · 兼做内存 / 硬盘加压");
        } else if (gStress.CpuLoopsPerCpu[i]) {
          UiStrPrintf(buf2, sizeof(buf2), "循环 %u 次 · 压力线程",
                      (UINT32)gStress.CpuLoopsPerCpu[i]);
        } else {
          UiStrPrintf(buf2, sizeof(buf2), "%s",
                      gStress.CpuAlive[i] ? L("压力线程运行中") : L("空闲可调度"));
        }
      } else if (gGroup == GROUP_MEM) {
        UINT32 slot = SysPresentDimm(&gApp.Sys, i);
        const DIMM_INFO *dimm = &gApp.Sys.Dimms[slot < SYS_MAX_DIMMS ? slot : 0];
        char size[16];
        GfxFormatSize(size, sizeof(size), dimm->SizeBytes);
        UiStrPrintf(buf, sizeof(buf), "%s · %s",
                    dimm->Locator[0] ? dimm->Locator : "DIMM", size);
        {
          UINT32 mt = dimm->ConfiguredSpeed ? dimm->ConfiguredSpeed : dimm->Speed;
          if (mt) {
            UiStrPrintf(buf2, sizeof(buf2), "%s %u MT/s%s", SysMemTypeName(dimm->MemoryType), mt,
                        dimm->Ecc ? " · ECC" : "");
          } else {
            UiStrPrintf(buf2, sizeof(buf2), "%s%s · SMBIOS 未给频率",
                        SysMemTypeName(dimm->MemoryType), dimm->Ecc ? " ECC" : "");
          }
        }
      } else {
        DISK_DEV *disk = DiskAt(i);
        char size[16];
        if (disk) {
          GfxFormatSize(size, sizeof(size), disk->SizeBytes);
          UiStrPrintf(buf, sizeof(buf), "%s · %s",
                      disk->Model[0] ? disk->Model : L("块设备"), size);
          UiStrPrintf(buf2, sizeof(buf2), "%s%s", DiskIfaceName(disk->Iface),
                      disk->BootVolume ? L(" · 启动盘") : L(" · 随机读"));
        } else {
          UiStrPrintf(buf, sizeof(buf), "块设备 %u", i);
          buf2[0] = 0;
        }
      }
      GfxText(row.X + 32, row.Y + 6, buf, on ? gTheme.Text : gTheme.Muted, 1);
      GfxTextClipped(row.X + 32, row.Y + 23, row.W - 44, buf2, gTheme.SubText, 1);
      y += rowH;
    }
    if (count == 0) {
      GfxText(inner.X, y + 8, "没有可用目标", gTheme.Warn, 1);
      GfxText(inner.X, y + 28, gGroup == GROUP_DISK ? "请进入硬盘检测页扫描设备" : "SMBIOS 未提供该部件信息",
              gTheme.Muted, 1);
    }
    if (count > visible && visible) {
      UiScrollbarScroll((GFX_RECT){ card.X + card.W - 8, inner.Y + 22, 4, inner.H - 30 },
                        first, visible, count, StressScrollTargets);
    }
  }
}

static VOID DrawComponentRow(GFX_RECT row, const char *name, const char *metrics,
                             BOOLEAN enabled, UINT32 color, BOOLEAN active)
{
  GfxFillRound(row, 8, gTheme.PanelAlt);
  GfxFillRound((GFX_RECT){ row.X, row.Y + 10, 3, row.H - 20 }, 2,
               enabled ? color : gTheme.Idle);
  GfxText(row.X + 12, row.Y + 6, name, enabled ? gTheme.Text : gTheme.Muted, 1);
  {
    const char *state;
    UINT32 color2;
    GFX_RECT pill;
    if (!enabled) {
      state = "未选择";
      color2 = gTheme.Idle;
    } else if (active) {
      state = "加压中";
      color2 = gTheme.Ok;
    } else if (gStress.ErrorCount) {
      state = "有异常";
      color2 = gTheme.Crit;
    } else if (gStress.Aborted && !gRunning && gResults.StressRan) {
      state = "已中止";
      color2 = gTheme.Warn;
    } else if (!gRunning && gResults.StressRan) {
      state = "已完成";
      color2 = gTheme.Idle;
    } else {
      state = "待开始";
      color2 = gTheme.Idle;
    }
    pill.W = GfxTextWidth(state, 1) + 22;
    pill.H = 18;
    pill.X = row.X + row.W - pill.W - 8;
    pill.Y = row.Y + 6;
    UiPill(pill, color2, state);
  }
  GfxTextClipped(row.X + 12, row.Y + 24, row.W - 90, metrics, gTheme.SubText, 1);
}

static VOID DrawStatus(GFX_RECT card)
{
  GFX_RECT inner = UiCardInner(card);
  UINT32 y = inner.Y;
  UINT32 totalMs = (gRunning ? gRunCfg.Seconds : gCfg.Seconds) * 1000;
  UINT32 elapsed = gStress.ElapsedMs;
  UINT32 permille;
  char buf[128];
  char buf2[64];
  BOOLEAN running = gRunning;

  if (!running) {
    elapsed = gResults.StressRan ? gStress.ElapsedMs : 0;
  }
  permille = totalMs ? (elapsed >= totalMs ? 999 : (UINT32)(((UINT64)elapsed * 1000) / totalMs)) : 0;

  UiStrPrintf(buf, sizeof(buf), L("已运行 "));
  FormatDuration(buf2, sizeof(buf2), elapsed / 1000);
  UiStrPrintf(buf + UiStrLen(buf), sizeof(buf) - UiStrLen(buf), "%s", buf2);
  GfxText(inner.X, y, buf, gTheme.SubText, 1);

  {
    UINT32 leftMs = totalMs > elapsed ? totalMs - elapsed : 0;
    FormatDuration(buf2, sizeof(buf2), leftMs / 1000);
    UiStrPrintf(buf, sizeof(buf), "剩余 %s", buf2);
    GfxTextRight(inner.X + inner.W, y, buf,
                 running ? (leftMs < 11000 ? gTheme.Warn : gTheme.Text) : gTheme.Muted, 1);
  }
  y += 24;
  UiProgressBar((GFX_RECT){ inner.X, y, inner.W, 10 }, permille,
                permille >= 900 ? gTheme.Warn : gTheme.Primary, 0);
  y += 22;

  UiStrPrintf(buf, sizeof(buf), "%u 线程 · 循环 %u 次 · 异常 %u",
              gRunning ? gStress.CpuThreads : SelectedCount(GROUP_CPU),
              (UINT32)gStress.CpuLoops, gStress.CpuErrors);
  DrawComponentRow((GFX_RECT){ inner.X, y, inner.W, 46 }, "CPU", buf, gCfg.CpuOn,
                   gTheme.Chart1, running && gCfg.CpuOn);
  y += 52;

  GfxFormatSize(buf2, sizeof(buf2), gStress.MemBytes);
  UiStrPrintf(buf, sizeof(buf), "%s 写入并回读 · %u MB/s · 异常 %u", buf2,
              gStress.MemMbs, gStress.MemErrors);
  DrawComponentRow((GFX_RECT){ inner.X, y, inner.W, 46 }, "内存", buf, gCfg.MemOn,
                   gTheme.Chart2, running && gCfg.MemOn);
  y += 52;

  GfxFormatSize(buf2, sizeof(buf2), gStress.DiskBytes);
  UiStrPrintf(buf, sizeof(buf), "%s 随机读取 · %u MB/s · 异常 %u", buf2,
              gStress.DiskMbs, gStress.DiskErrors);
  DrawComponentRow((GFX_RECT){ inner.X, y, inner.W, 46 }, "硬盘", buf, gCfg.DiskOn,
                   gTheme.Ok, running && gCfg.DiskOn);

  y += 60;
  UiDivider((GFX_RECT){ inner.X, y, inner.W, 1 });
  y += 10;
  GfxText(inner.X, y, "异常记录", gTheme.Muted, 1);
  UiStrPrintf(buf, sizeof(buf), "共 %u 条", gStress.ErrorCount);
  GfxTextRight(inner.X + inner.W, y, buf, gStress.ErrorCount ? gTheme.Crit : gTheme.Muted, 1);
  y += 20;
  if (gStress.ErrorCount == 0) {
    GfxText(inner.X, y, running ? "加压中，未发现数据或指令异常" : "本次会话暂无异常", gTheme.SubText, 1);
  } else {
    UINT32 i;
    for (i = 0; i < STRESS_ERR_LOG && y + 18 < inner.Y + inner.H; i++) {
      if (!gStress.Errors[i][0]) {
        break;
      }
      UiStrPrintf(buf, sizeof(buf), "%u. %s", i + 1, gStress.Errors[i]);
      GfxTextClipped(inner.X, y, inner.W, buf, i ? gTheme.SubText : gTheme.Crit, 1);
      y += 19;
    }
  }
}

/* ------------------------------------------------------------------ page */
VOID PageStressEnter(VOID)
{
  if (DiskCount() == 0) {
    DiskEnumerate();
  }
  ConfigDefaults();
  if (gGroup > GROUP_DISK) {
    gGroup = 0;
  }
  UiSetStatus(0, "空格勾选目标，C/M/D 开关组件");
}

VOID PageStressDraw(GFX_RECT content)
{
  UINT32 gap = 12;
  UINT32 leftW = 300;
  GFX_RECT left = { content.X, content.Y, leftW, content.H };
  GFX_RECT right = { content.X + leftW + gap, content.Y, content.W - leftW - gap, content.H };
  GFX_RECT cfg = { left.X, left.Y, left.W, 238 };
  GFX_RECT targets = { left.X, left.Y + 238 + gap, left.W, left.H - 238 - gap };
  char buf[64];

  UiCard(cfg, "压力组件", gRunning ? "运行中" : "配置");
  DrawConfig(cfg);

  UiCard(targets, "目标项目", L(gGroupName[gGroup]));
  DrawTargets(targets);

  UiStrPrintf(buf, sizeof(buf), "%u 项已选 · 异常 %u",
              SelectedCount(GROUP_CPU) * (gCfg.CpuOn ? 1 : 0) +
              SelectedCount(GROUP_MEM) * (gCfg.MemOn ? 1 : 0) +
              SelectedCount(GROUP_DISK) * (gCfg.DiskOn ? 1 : 0),
              gStress.ErrorCount);
  UiCard(right, "实时状态", buf);
  DrawStatus(right);
}

VOID PageStressPoint(UINT32 row)
{
  UINT32 group = row >> 8;
  UINT32 item = row & 0xFF;

  if (group >= GROUP_COUNT) {
    return;
  }
  gGroup = group;
  gItem = item;
}

BOOLEAN PageStressKey(EFI_INPUT_KEY key)
{
  UINT32 count;

  if (gRunning) {
    if (key.ScanCode == SCAN_ESC || key.ScanCode == SCAN_F6) {
      gStopRequested = TRUE;
    }
    return TRUE;
  }

  count = GroupCount(gGroup);
  switch (key.ScanCode) {
  case SCAN_UP:
    if (gItem > 0) {
      gItem--;
    }
    return TRUE;
  case SCAN_DOWN:
    if (count && gItem + 1 < count) {
      gItem++;
    }
    return TRUE;
  case SCAN_HOME:
    gItem = 0;
    return TRUE;
  case SCAN_END:
    gItem = count ? count - 1 : 0;
    return TRUE;
  case SCAN_PAGE_UP:
    gCfg.Seconds = gCfg.Seconds > 360 ? gCfg.Seconds - 300 : 60;
    return TRUE;
  case SCAN_PAGE_DOWN:
    gCfg.Seconds = gCfg.Seconds + 300 < 7200 ? gCfg.Seconds + 300 : 7200;
    return TRUE;
  case SCAN_F5:
    RunStress();
    return TRUE;
  case SCAN_F6:
    UiSetStatus(2, "压力测试未在运行");
    return TRUE;
  case SCAN_F7:
    {
      UINT32 i;
      for (i = 0; i < count; i++) {
        SetItem(gGroup, i, TRUE);
      }
      SetGroup(gGroup, TRUE);
    }
    return TRUE;
  case SCAN_F8:
    {
      UINT32 i;
      for (i = 0; i < count; i++) {
        SetItem(gGroup, i, FALSE);
      }
    }
    return TRUE;
  default:
    break;
  }

  if (key.UnicodeChar == '-' || key.UnicodeChar == '_') {
    gCfg.Seconds = gCfg.Seconds > 120 ? gCfg.Seconds - 60 : 60;
    return TRUE;
  }
  if (key.UnicodeChar == '=' || key.UnicodeChar == '+') {
    gCfg.Seconds = gCfg.Seconds + 60 < 7200 ? gCfg.Seconds + 60 : 7200;
    return TRUE;
  }
  if (key.UnicodeChar == '\t') {
    gGroup = (gGroup + 1) % GROUP_COUNT;
    gItem = 0;
    return TRUE;
  }
  if (key.UnicodeChar == ' ') {
    if (count) {
      SetItem(gGroup, gItem, ItemOn(gGroup, gItem) ? FALSE : TRUE);
      if (ItemOn(gGroup, gItem)) {
        SetGroup(gGroup, TRUE);
      }
    }
    return TRUE;
  }
  if (key.UnicodeChar == 'c' || key.UnicodeChar == 'C') {
    SetGroup(GROUP_CPU, gCfg.CpuOn ? FALSE : TRUE);
    gGroup = GROUP_CPU;
    return TRUE;
  }
  if (key.UnicodeChar == 'm' || key.UnicodeChar == 'M') {
    SetGroup(GROUP_MEM, gCfg.MemOn ? FALSE : TRUE);
    gGroup = GROUP_MEM;
    return TRUE;
  }
  if (key.UnicodeChar == 'd' || key.UnicodeChar == 'D') {
    SetGroup(GROUP_DISK, gCfg.DiskOn ? FALSE : TRUE);
    gGroup = GROUP_DISK;
    return TRUE;
  }
  if (key.UnicodeChar == '\r' || key.UnicodeChar == '\n') {
    RunStress();
    return TRUE;
  }
  return FALSE;
}
