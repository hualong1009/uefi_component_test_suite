/* page_cpu.c - processor instruction self-test page. */
#include "pages.h"
#include "cputest.h"
#include "results.h"

static const char *gCheckName[CPU_CHECK_COUNT] = {
  "整数运算 ALU", "位操作指令", "SSE2 向量", "AVX 向量",
  "RDRAND 随机数", "缓存带宽", "TSC 计时器"
};
static const char *gCheckNote[CPU_CHECK_COUNT] = {
  "加减乘除、移位、字节序的逆运算校验",
  "POPCNT / BSF / BSR 与逐位扫描比对",
  "128 位整型通道与双精度通道",
  "256 位 YMM 寄存器与浮点通道",
  "硬件随机数指令取样",
  "8 MB 缓冲区顺序复制带宽",
  "用固件延时校准时间戳计数器"
};

static UINT32  gCpuMask = 0x7F;
static UINT32  gCpuSel;
static BOOLEAN gCpuRunning;
static UINT32  gCpuPermille;
static char    gCpuPhase[32];

static UINT32 MaskAll(VOID)
{
  return (1u << CPU_CHECK_COUNT) - 1u;
}

static VOID Repaint(const char *label, UINT32 permille)
{
  GFX_RECT content = UiContentRect();
  gCpuPermille = permille;
  if (label) {
    UiStrPrintf(gCpuPhase, sizeof(gCpuPhase), "%s", label);
  }
  (VOID)content;
  UiDrawEverything();
}

static BOOLEAN CpuTick(UINT32 permille, const char *label)
{
  if (UiPump() || gStopRequested) {
    return FALSE;
  }
  Repaint(label, permille);
  return TRUE;
}

static VOID RunChecks(VOID)
{
  EFI_STATUS status;
  char buf[128];

  if (gCpuMask == 0) {
    UiSetStatus(2, "请先选择至少一个自检项目");
    return;
  }
  gCpuRunning = TRUE;
  gStopRequested = FALSE;
  gCpuPermille = 0;
  UiStrPrintf(gCpuPhase, sizeof(gCpuPhase), "准备");
  UiSetStatus(0, "正在执行处理器自检，按 ESC 或 F6 中止");
  status = CpuSelfTest(gCpuMask, CpuTick);
  gCpuRunning = FALSE;
  if (status == EFI_ABORTED) {
    UiSetStatus(2, "自检已中止");
  } else if (gCpuReport.Errors) {
    UiStrPrintf(buf, sizeof(buf), "自检发现 %u 项异常", gCpuReport.Errors);
    UiSetStatus(3, buf);
  } else {
    UiStrPrintf(buf, sizeof(buf), "自检通过 · 整数 %u MOPS · 浮点 %u MFLOPS · 缓存 %u MB/s",
                gCpuReport.IntMops, gCpuReport.FloatMflops, gCpuReport.MemMbs);
    UiSetStatus(1, buf);
  }
  gResults.CpuTested = TRUE;
  gResults.CpuErrorCount = gCpuReport.Errors;
  gResults.CpuElapsedMs = gCpuReport.ElapsedMs;
  gResults.CpuMops = gCpuReport.IntMops;
  gResults.HealthCpu = gCpuReport.Errors ? 3 : 1;
  UiRedraw();
}

/* ------------------------------------------------------------------ drawing */
static VOID DrawChecks(GFX_RECT card)
{
  GFX_RECT inner = UiCardInner(card);
  UINT32 i;
  char buf[72];

  for (i = 0; i < CPU_CHECK_COUNT; i++) {
    GFX_RECT row = { inner.X, inner.Y + i * 44, inner.W, 40 };
    UINT32 level = gCpuReport.Check[i].Level;
    BOOLEAN on = (gCpuMask & (1u << i)) ? TRUE : FALSE;
    if (i == gCpuSel) {
      GfxFillRound(row, 8, gTheme.PrimarySoft);
    } else if (i & 1) {
      GfxFillRound(row, 8, gTheme.PanelAlt);
    }
    UiCheck((GFX_RECT){ row.X + 10, row.Y + 13, 14, 14 }, on);
    if (level) {
      GfxFillCircle(row.X + row.W - 16, row.Y + 20, 5, UiLevelColor(level));
    }
    GfxText(row.X + 34, row.Y + 5, gCheckName[i], on ? gTheme.Text : gTheme.Muted, 1);
    UiStrPrintf(buf, sizeof(buf), "%s", level ? gCpuReport.Check[i].Detail : gCheckNote[i]);
    GfxTextClipped(row.X + 34, row.Y + 22, row.W - 60, buf, gTheme.SubText, 1);
    UiHitRowBox(row, i);
  }
  UiStrPrintf(buf, sizeof(buf), "%u / %u 已选择", (UINT32)__builtin_popcount(gCpuMask),
              (UINT32)CPU_CHECK_COUNT);
  GfxTextRight(card.X + card.W - 16, card.Y + 14, buf, gTheme.SubText, 1);
}

static VOID DrawThroughput(GFX_RECT card)
{
  GFX_RECT inner = UiCardInner(card);
  UINT32 tw = (inner.W - 10) / 2;
  UINT32 i;
  struct {
    const char *label;
    UINT32 value;
    UINT32 color;
  } tiles[4];
  char buf[40];

  tiles[0].label = "整数 MOPS";
  tiles[0].value = gCpuReport.IntMops;
  tiles[0].color = gTheme.Chart1;
  tiles[1].label = "浮点 MFLOPS";
  tiles[1].value = gCpuReport.FloatMflops;
  tiles[1].color = gTheme.Chart2;
  tiles[2].label = "复制 MB/s";
  tiles[2].value = gCpuReport.MemMbs;
  tiles[2].color = gTheme.Ok;
  tiles[3].label = "TSC MHz";
  tiles[3].value = gCpuReport.TscMhz ? gCpuReport.TscMhz : (UINT32)(SysTscHz() / 1000000);
  tiles[3].color = gTheme.Primary;

  for (i = 0; i < 4; i++) {
    GFX_RECT tile = { inner.X + (i % 2) * (tw + 10), inner.Y + (i / 2) * 62, tw, 54 };
    GfxFillRound(tile, 8, gTheme.PanelAlt);
    GfxFillRound((GFX_RECT){ tile.X, tile.Y + 12, 3, 16 }, 1, tiles[i].color);
    UiStrPrintf(buf, sizeof(buf), "%u", tiles[i].value);
    GfxTextClipped(tile.X + 12, tile.Y + 10, tile.W - 20, buf, gTheme.Text, 2);
    GfxText(tile.X + 12, tile.Y + 36, tiles[i].label, gTheme.SubText, 1);
  }
  if (gCpuRunning) {
    UiProgressBar((GFX_RECT){ inner.X, inner.Y + 130, inner.W, 10 }, gCpuPermille,
                  gTheme.Primary, 0);
    UiStrPrintf(buf, sizeof(buf), "%s · %u%%", gCpuPhase, gCpuPermille / 10);
    GfxText(inner.X, inner.Y + 146, buf, gTheme.SubText, 1);
  } else if (gCpuReport.Ran) {
    if (gCpuReport.ElapsedMs >= 1000) {
      UiStrPrintf(buf, sizeof(buf), "上次运行 %u.%u 秒 · 异常 %u 项",
                  gCpuReport.ElapsedMs / 1000, (gCpuReport.ElapsedMs % 1000) / 100,
                  gCpuReport.Errors);
    } else {
      UiStrPrintf(buf, sizeof(buf), "上次运行 %u ms · 异常 %u 项", gCpuReport.ElapsedMs,
                  gCpuReport.Errors);
    }
    GfxText(inner.X, inner.Y + 132, buf, gCpuReport.Errors ? gTheme.Warn : gTheme.SubText, 1);
  } else {
    UiStrPrintf(buf, sizeof(buf), "%u 线程 · Enter 开始自检", gApp.Sys.LogicalCpus);
    GfxTextClipped(inner.X, inner.Y + 132, inner.W, buf, gTheme.Muted, 1);
  }
}

static VOID DrawDetail(GFX_RECT card)
{
  GFX_RECT inner = UiCardInner(card);
  UINT32 i;
  UINT32 y = inner.Y;
  char buf[128];

  GfxText(inner.X, y, "项目", gTheme.Muted, 1);
  GfxTextRight(inner.X + inner.W, y, "状态", gTheme.Muted, 1);
  y += 20;
  UiDivider((GFX_RECT){ inner.X, y - 4, inner.W, 1 });
  for (i = 0; i < CPU_CHECK_COUNT && y + 20 < inner.Y + inner.H - 60; i++) {
    UINT32 level = gCpuReport.Check[i].Level;
    const char *state = level == 1 ? "通过" : (level == 2 ? "注意" :
                        (level == 3 ? "失败" : (level == 4 ? "跳过" : "未运行")));
    GfxTextClipped(inner.X, y, 150, gCheckName[i], gTheme.Text, 1);
    GfxTextRight(inner.X + inner.W, y, state,
                 level ? UiLevelColor(level) : gTheme.Muted, 1);
    UiStrPrintf(buf, sizeof(buf), "%s", gCpuReport.Check[i].Detail);
    GfxTextClipped(inner.X + 150, y, inner.W - 200, buf, gTheme.SubText, 1);
    UiHitRow((GFX_RECT){ inner.X, y - 2, inner.W, 22 }, i);
    y += 22;
  }
  y += 6;
  UiDivider((GFX_RECT){ inner.X, y, inner.W, 1 });
  y += 10;
  UiWrapText((GFX_RECT){ inner.X, y, inner.W, 60 },
             "自检使用可逆性校验与逐位参考实现比对，吞吐数值为单线程测量，"
             "受固件环境与虚拟化影响仅作参考。", gTheme.Muted, 1);
}

VOID PageCpuEnter(VOID)
{
  gCpuSel = 0;
  if (!gCpuReport.Ran) {
    CpuReportReset();
  }
  UiSetStatus(0, "空格勾选项目，F5 运行自检");
}

VOID PageCpuDraw(GFX_RECT content)
{
  UINT32 gap = 12;
  GFX_RECT left = { content.X, content.Y, 420, content.H };
  GFX_RECT right = { content.X + 420 + gap, content.Y, content.W - 420 - gap, content.H };
  GFX_RECT thru = { right.X, right.Y, right.W, 198 };
  GFX_RECT detail = { right.X, right.Y + 198 + gap, right.W, right.H - 198 - gap };
  char buf[64];

  UiCard(left, "自检项目", 0);
  DrawChecks(left);

  UiCard(thru, "吞吐测量", gCpuReport.Ran ? "已测量" : "未运行");
  DrawThroughput(thru);

  UiStrPrintf(buf, sizeof(buf), "异常 %u 项", gResults.CpuErrorCount);
  UiCard(detail, "检测结果", buf);
  DrawDetail(detail);
}

VOID PageCpuPoint(UINT32 row)
{
  if (row < CPU_CHECK_COUNT) {
    gCpuSel = row;
  }
}

BOOLEAN PageCpuKey(EFI_INPUT_KEY key)
{
  if (gCpuRunning) {
    if (key.ScanCode == SCAN_ESC || key.ScanCode == SCAN_F6) {
      gStopRequested = TRUE;
    }
    return TRUE;
  }
  switch (key.ScanCode) {
  case SCAN_UP:
    if (gCpuSel > 0) {
      gCpuSel--;
    }
    return TRUE;
  case SCAN_DOWN:
    if (gCpuSel + 1 < CPU_CHECK_COUNT) {
      gCpuSel++;
    }
    return TRUE;
  case SCAN_HOME:
    gCpuSel = 0;
    return TRUE;
  case SCAN_END:
    gCpuSel = CPU_CHECK_COUNT - 1;
    return TRUE;
  case SCAN_F5:
    RunChecks();
    return TRUE;
  case SCAN_F7:
    gCpuMask = MaskAll();
    return TRUE;
  case SCAN_F8:
    gCpuMask = (1u << CPU_CHK_ALU) | (1u << CPU_CHK_BITOPS) | (1u << CPU_CHK_SSE2)
               | (1u << CPU_CHK_CACHE);
    return TRUE;
  default:
    break;
  }
  if (key.UnicodeChar == ' ') {
    gCpuMask ^= (1u << gCpuSel);
    return TRUE;
  }
  if (key.UnicodeChar == '\r' || key.UnicodeChar == '\n') {
    RunChecks();
    return TRUE;
  }
  /* the latency page is the CPU page's second question: how fast do these
     cores talk to each other */
  if (key.UnicodeChar == 'l' || key.UnicodeChar == 'L') {
    UiSwitchPage(PAGE_LATENCY);
    return TRUE;
  }
  /* and how much memory traffic each of them can pull */
  if (key.UnicodeChar == 'b' || key.UnicodeChar == 'B') {
    UiSwitchPage(PAGE_BW);
    return TRUE;
  }
  /* and how much memory traffic each of them can pull */
  if (key.UnicodeChar == 'b' || key.UnicodeChar == 'B') {
    UiSwitchPage(PAGE_BW);
    return TRUE;
  }
  return FALSE;
}
