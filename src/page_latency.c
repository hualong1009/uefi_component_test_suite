/* page_latency.c - inter-core latency heat matrix.

   One cell per processor pair, the number written inside is the one way cache
   line hand-off latency in nanoseconds. Logical processors are sorted by
   package and core, so a multi socket machine shows its sockets as blocks on
   the diagonal and the inter-socket link as the off diagonal blocks - the
   pattern is the diagnostic, the absolute value is secondary. */
#include "pages.h"
#include "latency.h"
#include "results.h"
#include "mouse.h"
#include "debug.h"

#define LAT_ZOOM 24

static UINT32    gLatRow;
static UINT32    gLatCol;
static BOOLEAN   gLatZoom;
static BOOLEAN   gLatRunning;
static UINT32    gLatDone;
static UINT32    gLatTotal;
static GFX_RECT  gLatRect;      /* matrix pixels on screen                */
static UINT32    gLatCell = 1;  /* pixels per cell                        */
static UINT32    gLatFirst;     /* first visible index (zoomed window)    */
static UINT32    gLatWin = 1;   /* cells per side actually drawn          */
static UINT64    gLatStartTsc;  /* TSC of the running measurement         */

/* ------------------------------------------------------------- helpers */
static UINT32 Mix(UINT32 a, UINT32 b, UINT32 t)
{
  UINT32 ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
  UINT32 br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
  UINT32 r = (ar * (255 - t) + br * t) / 255;
  UINT32 g = (ag * (255 - t) + bg * t) / 255;
  UINT32 l = (ab * (255 - t) + bb * t) / 255;
  return GFX_RGB(r, g, l);
}

static UINT32 HeatColor(UINT32 v10, UINT32 lo, UINT32 hi)
{
  UINT32 t;

  if (v10 == 0) {
    return gTheme.IdleSoft;
  }
  if (hi <= lo) {
    hi = lo + 1;
  }
  if (v10 <= lo) {
    t = 0;
  } else if (v10 >= hi) {
    t = 1000;
  } else {
    t = (v10 - lo) * 1000 / (hi - lo);
  }
  if (t < 500) {
    return Mix(gTheme.Ok, gTheme.Warn, t * 255 / 500);
  }
  return Mix(gTheme.Warn, gTheme.Crit, (t - 500) * 255 / 500);
}

static const char *NsText(UINT32 v10, char *buf, UINTN cap)
{
  if (v10 == 0) {
    UiStrPrintf(buf, cap, "--");
  } else {
    UiStrPrintf(buf, cap, "%u.%u", v10 / 10, v10 % 10);
  }
  return buf;
}

static void LatRange(UINT32 *lo, UINT32 *hi)
{
  *lo = gLat.MinNs10 ? gLat.MinNs10 : 1;
  *hi = gLat.MaxNs10 ? gLat.MaxNs10 : (*lo + 1);
  if (*hi <= *lo) {
    *hi = *lo + 1;
  }
}

/* ------------------------------------------------------------ lifecycle */
static VOID LatClampSelection(VOID)
{
  UINT32 n = gLat.Count ? gLat.Count : 1;

  if (gLatRow >= n) {
    gLatRow = n - 1;
  }
  if (gLatCol >= n) {
    gLatCol = n - 1;
  }
}

VOID PageLatencyEnter(VOID)
{
  char buf[40];

  LatPrepare();
  LatClampSelection();
  if (gLatCol == gLatRow && gLat.Count > 1) {
    /* the diagonal has no measurement in it, so start on a real pair */
    gLatCol = (gLatCol + 1) % gLat.Count;
  }
  if (!gLat.Ran) {
    UiSetStatus(0, "按 F5 测量处理器之间的缓存行往返延迟");
  } else {
    UiStrPrintf(buf, sizeof(buf), "中位延迟 %u.%u ns", gLat.MedianNs10 / 10,
                gLat.MedianNs10 % 10);
    UiSetStatus(0, buf);
  }
}

static BOOLEAN LatTick(UINT32 done, UINT32 total)
{
  gLatDone = done;
  gLatTotal = total;
  /* keys and the pointer stay alive during the run so F6/Esc can abort */
  UiPump();
  UiDrawEverything();
  if (!gLatRunning) {
    return FALSE;
  }
  return !gStopRequested;
}

static VOID LatStart(VOID)
{
  EFI_STATUS status;

  gStopRequested = FALSE;
  gBusy = TRUE;
  gLatRunning = TRUE;
  gLatDone = 0;
  gLatTotal = 0;
  gLatZoom = FALSE;
  gLatFirst = 0;
  gLatStartTsc = SysTsc();
  UiRedraw();
  status = LatRun(LatTick);
  gLatRunning = FALSE;
  gBusy = FALSE;
  gLatDone = gLatTotal;

  if (gLat.Ran) {
    gResults.LatTested = TRUE;
    gResults.LatCpus = gLat.Count;
    gResults.LatSockets = gLat.SocketCount;
    gResults.LatPairs = gLat.Pairs;
    gResults.LatElapsedMs = gLat.ElapsedMs;
    gResults.LatMedianNs10 = gLat.MedianNs10;
    gResults.LatIntraNs10 = gLat.IntraNs10;
    gResults.LatCrossNs10 = gLat.CrossNs10;
    gResults.LatSiblingNs10 = gLat.SiblingNs10;
    gResults.LatMinNs10 = gLat.MinNs10;
    gResults.LatMaxNs10 = gLat.MaxNs10;
    gResults.LatSocketCount = gLat.SocketCount;
    {
      UINT32 i, j;
      for (i = 0; i < 8 && i < LAT_MAX_SOCKET; i++) {
        for (j = 0; j < 8 && j < LAT_MAX_SOCKET; j++) {
          gResults.LatSockNs10[i][j] = gLat.SockNs10[i][j];
        }
      }
    }
    if (gLat.Aborted) {
      UiSetStatus(2, "延迟测量已中止，矩阵不完整");
    } else if (gLat.Failed) {
      UiSetStatus(2, "部分处理器对未能完成测量");
    } else {
      UiSetStatus(1, "核间延迟测量完成");
    }
  } else if (status == EFI_UNSUPPORTED) {
    UiSetStatus(2, "未找到可用的处理器服务，无法测量");
  } else {
    UiSetStatus(3, "延迟测量失败");
  }
  UiRedraw();
}

/* ------------------------------------------------------------ navigation */
static UINT32 LatStep(UINT32 v, INT32 d, UINT32 n)
{
  INT32 t = (INT32)v + d;

  if (t < 0) {
    t = 0;
  }
  if (t > (INT32)n - 1) {
    t = (INT32)n - 1;
  }
  return (UINT32)t;
}

static VOID LatMove(INT32 dRow, INT32 dCol)
{
  UINT32 n = gLat.Count ? gLat.Count : 1;

  gLatRow = LatStep(gLatRow, dRow, n);
  gLatCol = LatStep(gLatCol, dCol, n);
}

static VOID LatCellClick(UINT32 id)
{
  UINT32 x, y, col, row;

  (VOID)id;
  x = MouseX();
  y = MouseY();
  if (x < gLatRect.X || y < gLatRect.Y || gLatCell == 0) {
    return;
  }
  if (x >= gLatRect.X + gLatWin * gLatCell || y >= gLatRect.Y + gLatWin * gLatCell) {
    return;
  }
  col = gLatFirst + (x - gLatRect.X) / gLatCell;
  row = gLatFirst + (y - gLatRect.Y) / gLatCell;
  if (row < gLat.Count && col < gLat.Count) {
    gLatRow = row;
    gLatCol = col;
    UiRedraw();
  }
}

BOOLEAN PageLatencyKey(EFI_INPUT_KEY key)
{
  UINT32 n = gLat.Count ? gLat.Count : 1;

  if (key.ScanCode == SCAN_F5) {
    if (!gLatRunning) {
      LatStart();
    }
    return TRUE;
  }
  if (key.ScanCode == SCAN_F6 || key.ScanCode == SCAN_ESC) {
    if (gLatRunning) {
      gStopRequested = TRUE;
      UiSetStatus(2, "正在停止测量…");
      return TRUE;
    }
    return FALSE;
  }
  if (key.UnicodeChar == 'z' || key.UnicodeChar == 'Z') {
    gLatZoom = gLatZoom ? FALSE : TRUE;
    return TRUE;
  }
  if (key.UnicodeChar == 'r' || key.UnicodeChar == 'R') {
    gLatRow = 0;
    gLatCol = 0;
    gLatFirst = 0;
    return TRUE;
  }
  switch (key.ScanCode) {
  case SCAN_LEFT:
    LatMove(0, -1);
    break;
  case SCAN_RIGHT:
    LatMove(0, 1);
    break;
  case SCAN_UP:
    LatMove(-1, 0);
    break;
  case SCAN_DOWN:
    LatMove(1, 0);
    break;
  case SCAN_PAGE_UP:
    LatMove(n > 8 ? -8 : -(INT32)n, 0);
    break;
  case SCAN_PAGE_DOWN:
    LatMove(n > 8 ? 8 : (INT32)n, 0);
    break;
  case SCAN_HOME:
    gLatRow = 0;
    gLatCol = 0;
    break;
  case SCAN_END:
    gLatRow = n - 1;
    gLatCol = n - 1;
    break;
  default:
    if (key.UnicodeChar == '\r' && gLat.Ran) {
      gLatZoom = gLatZoom ? FALSE : TRUE;
      return TRUE;
    }
    return FALSE;
  }
  return TRUE;
}

/* --------------------------------------------------------------- drawing */
static VOID DrawLegend(GFX_RECT r, UINT32 lo, UINT32 hi)
{
  UINT32 strips = 48;
  UINT32 w = 220;
  UINT32 i;
  char buf[32];

  for (i = 0; i < strips; i++) {
    UINT32 c = HeatColor(lo + (hi - lo) * i / (strips - 1), lo, hi);
    GfxFillRect((GFX_RECT){ r.X + (w - 4) * i / (strips - 1), r.Y, 6, 10 }, c);
  }
  UiStrPrintf(buf, sizeof(buf), "%u.%u ns", lo / 10, lo % 10);
  GfxText(r.X, r.Y + 14, buf, gTheme.Muted, 1);
  UiStrPrintf(buf, sizeof(buf), "%u.%u ns", hi / 10, hi % 10);
  GfxTextRight(r.X + w + 4, r.Y + 14, buf, gTheme.Muted, 1);
}

static VOID DrawMatrixCard(GFX_RECT card)
{
  GFX_RECT inner = UiCardInner(card);
  UINT32 n = gLat.Count ? gLat.Count : 1;
  UINT32 areaW, areaH, side, cell, win, first, row, col;
  UINT32 lo = 1, hi = 2;
  UINT32 mx, my, i, readoutY;
  char buf[160];
  char lab1[24], lab2[24], num[24];
  GFX_RECT crect;

  LatRange(&lo, &hi);

  win = (gLatZoom && n > LAT_ZOOM) ? LAT_ZOOM : n;
  readoutY = inner.Y + inner.H - 76;
  areaW = inner.W > 76 ? inner.W - 76 : 1;         /* left axis labels */
  areaH = (readoutY > inner.Y + 80) ? readoutY - inner.Y - 60 : 1;
  side = areaW < areaH ? areaW : areaH;
  cell = side / win;
  if (cell < 2) {
    cell = 2;
  }
  if (cell > 60) {
    cell = 60;
  }
  first = 0;
  if (win < n) {
    if (gLatRow >= win / 2) {
      first = gLatRow - win / 2;
    }
    if (first + win > n) {
      first = n - win;
    }
  }
  mx = inner.X + 60 + ((areaW > win * cell) ? (areaW - win * cell) / 2 : 0);
  my = inner.Y + 26 + ((areaH > win * cell) ? (areaH - win * cell) / 3 : 0);

  gLatCell = cell;
  gLatFirst = first;
  gLatWin = win;
  gLatRect.X = mx;
  gLatRect.Y = my;
  gLatRect.W = cell * win;
  gLatRect.H = cell * win;
  UiHitAdd(gLatRect, LatCellClick, 0);

  if (!gLat.Ran && !gLatRunning) {
    UiStrPrintf(buf, sizeof(buf), "%u 个逻辑处理器 · %u 个插槽 · %u 个物理核心",
                gLat.Count, gLat.SocketCount, gLat.CoreCount);
    GfxTextCenter(inner.X, inner.W, inner.Y + inner.H / 2 - 44, "核间延迟矩阵", gTheme.Text, 1);
    GfxTextCenter(inner.X, inner.W, inner.Y + inner.H / 2 - 20, buf, gTheme.SubText, 1);
    UiStrPrintf(buf, sizeof(buf), "共 %u 个处理器对 · 每对约 1 微秒 · 按 F5 开始",
                (UINT32)gLat.PairsTotal);
    GfxTextCenter(inner.X, inner.W, inner.Y + inner.H / 2 + 6, buf, gTheme.Muted, 1);
    if (!gLat.TopologyKnown) {
      GfxTextCenter(inner.X, inner.W, inner.Y + inner.H / 2 + 32,
                    "固件未提供处理器物理位置，矩阵按枚举顺序排列", gTheme.Warn, 1);
    }
    return;
  }

  for (row = 0; row < win; row++) {
    for (col = 0; col < win; col++) {
      UINT32 a = first + row, b = first + col;
      UINT32 v = LatNs10(a, b);
      crect.X = mx + col * cell;
      crect.Y = my + row * cell;
      crect.W = cell;
      crect.H = cell;
      GfxFillRect(crect, (a == b) ? gTheme.IdleSoft : HeatColor(v, lo, hi));
    }
  }

  /* crosshair guides go under the numbers so a value is never cut in half */
  if (gLatRow >= first && gLatRow < first + win && gLatCol >= first && gLatCol < first + win) {
    UINT32 rp = my + (gLatRow - first) * cell;
    UINT32 cp = mx + (gLatCol - first) * cell;
    GfxFillRect((GFX_RECT){ mx, rp + cell / 2, win * cell, 1 }, gTheme.Panel);
    GfxFillRect((GFX_RECT){ cp + cell / 2, my, 1, win * cell }, gTheme.Panel);
  }

  for (row = 0; row < win; row++) {
    for (col = 0; col < win; col++) {
      UINT32 a = first + row, b = first + col;
      UINT32 v = LatNs10(a, b);
      crect.X = mx + col * cell;
      crect.Y = my + row * cell;
      crect.W = cell;
      crect.H = cell;
      if (gLatZoom && v && cell >= 24) {
        UiStrPrintf(num, sizeof(num), "%u", (v + 5) / 10);
        GfxTextCenter(crect.X, cell, crect.Y + (cell - 16) / 2, num,
                      (v > (hi + lo) / 2) ? gTheme.OnPrimary : gTheme.Text, 1);
      }
    }
  }

  /* package separators: the block structure is what a multi socket run is about */
  for (i = 1; i < n; i++) {
    if (gLatCpus[i].Socket != gLatCpus[i - 1].Socket && i > first && i < first + win) {
      GfxFillRect((GFX_RECT){ mx + (i - first) * cell - 1, my, 2, win * cell }, gTheme.Panel);
      GfxFillRect((GFX_RECT){ mx, my + (i - first) * cell - 1, win * cell, 2 }, gTheme.Panel);
    }
  }

  /* axis labels: one per socket block plus a tick every 32 processors */
  {
    UINT32 a = first;
    while (a < first + win) {
      UINT32 blockEnd = a + 1;
      UINT32 labelX = mx + (a - first) * cell;
      UINT32 labelW;
      while (blockEnd < n && gLatCpus[blockEnd].Socket == gLatCpus[a].Socket) {
        blockEnd++;
      }
      if (blockEnd > first + win) {
        blockEnd = first + win;
      }
      UiStrPrintf(buf, sizeof(buf), "P%u", gLatCpus[a].Socket);
      labelW = GfxTextWidth(buf, 1);
      if (blockEnd - a >= 3) {
        GfxTextCenter(labelX, (blockEnd - a) * cell, my - 18, buf, gTheme.Text, 1);
      } else {
        GfxText(labelX, my - 18, buf, gTheme.Text, 1);
      }
      GfxText((mx > labelW + 6) ? mx - labelW - 6 : 0, my + (a - first) * cell + 1, buf,
              gTheme.SubText, 1);
      a = blockEnd;
    }
  }
  for (i = 32; i < win; i += 32) {
    UiStrPrintf(buf, sizeof(buf), "%u", first + i);
    GfxText(mx + i * cell, my - 34, buf, gTheme.Muted, 1);
    GfxTextRight(mx - 30, my + i * cell + 8, buf, gTheme.Muted, 1);
  }

  /* the frame of the selected cell sits above everything else */
  if (gLatRow >= first && gLatRow < first + win && gLatCol >= first && gLatCol < first + win) {
    UINT32 rp = my + (gLatRow - first) * cell;
    UINT32 cp = mx + (gLatCol - first) * cell;
    if (cell > 5) {
      GfxFrameRect((GFX_RECT){ cp + 1, rp + 1, cell - 2, cell - 2 }, gTheme.OnPrimary);
    }
    GfxFrameRect((GFX_RECT){ cp, rp, cell, cell }, gTheme.Text);
  }

  /* readout under the matrix, legend in the bottom right corner of the card */
  {
    UINT32 v = LatNs10(gLatRow, gLatCol);
    const char *kind;

    LatCpuLabel(gLatRow, lab1, sizeof(lab1));
    LatCpuLabel(gLatCol, lab2, sizeof(lab2));
    if (gLatRow == gLatCol) {
      kind = L("同一处理器");
    } else if (gLatCpus[gLatRow].Socket != gLatCpus[gLatCol].Socket) {
      kind = L("跨插槽");
    } else if (gLatCpus[gLatRow].Core == gLatCpus[gLatCol].Core) {
      kind = L("同核双线程");
    } else {
      kind = L("同插槽");
    }
    UiStrPrintf(buf, sizeof(buf), "%s  ->  %s   %s ns", lab1, lab2,
                NsText(v, num, sizeof(num)));
    GfxTextClipped(inner.X, readoutY - 20, inner.W, kind,
                   (gLatRow == gLatCol) ? gTheme.Muted : gTheme.SubText, 1);
    GfxTextClipped(inner.X, readoutY, inner.W, buf, gTheme.Text, 1);
    DrawLegend((GFX_RECT){ inner.X + inner.W - 250, readoutY + 20, 250, 24 }, lo, hi);
  }
  if (gLatZoom) {
    UiStrPrintf(buf, sizeof(buf), "放大视图 %u..%u（Z 退出）", first + 1, first + win);
    GfxTextRight(inner.X + inner.W, my - 34, buf, gTheme.Muted, 1);
  }
}

static VOID DrawStatusCard(GFX_RECT card)
{
  GFX_RECT inner = UiCardInner(card);
  GFX_RECT col = inner;
  char buf[160];
  char v[64];
  UINT32 elapsed = gLatRunning ? (UINT32)SysMsSince(gLatStartTsc) : gLat.ElapsedMs;

  UiCard(card, "测量状态", gLatRunning ? "运行中" : (gLat.Ran ? "已完成" : "未运行"));
  if (gLatRunning) {
    UINT32 pm = gLatTotal ? (gLatDone * 1000 / gLatTotal) : 0;
    UiStrPrintf(buf, sizeof(buf), "%u / %u 处理器对", gLatDone, gLatTotal);
    UiProgressBar((GFX_RECT){ inner.X, inner.Y, inner.W, 14 }, pm, gTheme.Primary, buf);
    col.Y += 26;
  }
  UiStrPrintf(v, sizeof(v), "%u 个", gLat.Count);
  GfxText(col.X, col.Y, "矩阵维度", gTheme.Muted, 1);
  GfxTextRight(col.X + col.W, col.Y, v, gTheme.Text, 1);
  col.Y += 22;
  UiStrPrintf(buf, sizeof(buf), "%u / %u / %u", gLat.SocketCount, gLat.CoreCount, gLat.Count);
  GfxText(col.X, col.Y, "插槽 / 核心 / 线程", gTheme.Muted, 1);
  GfxTextRight(col.X + col.W, col.Y, buf, gTheme.Text, 1);
  col.Y += 22;
  UiStrPrintf(buf, sizeof(buf), "%u / %u", gLat.Pairs, (UINT32)gLat.PairsTotal);
  GfxText(col.X, col.Y, "已测处理器对", gTheme.Muted, 1);
  GfxTextRight(col.X + col.W, col.Y, buf, gTheme.Text, 1);
  col.Y += 22;
  UiStrPrintf(buf, sizeof(buf), "%u 毫秒", elapsed);
  GfxText(col.X, col.Y, "耗时", gTheme.Muted, 1);
  GfxTextRight(col.X + col.W, col.Y, buf, gTheme.Text, 1);
  col.Y += 22;
  GfxText(col.X, col.Y, "固件拓扑", gTheme.Muted, 1);
  if (!gLat.Count) {
    GfxTextRight(col.X + col.W, col.Y, "n/a", gTheme.Muted, 1);
  } else {
    const char *src;
    if (!gLat.TopologyKnown) {
      src = L("不可用");
    } else if (gLat.TopoSource == 2) {
      src = L("CPUID");
    } else {
      src = L("固件");
    }
    GfxTextRight(col.X + col.W, col.Y, src,
                 gLat.TopologyKnown ? gTheme.Ok : gTheme.Warn, 1);
  }
}

static VOID DrawStatRow(GFX_RECT *col, const char *label, UINT32 ns10, UINT32 color)
{
  char buf[32];

  GfxText(col->X, col->Y, label, gTheme.Muted, 1);
  if (ns10) {
    UiStrPrintf(buf, sizeof(buf), "%u.%u ns", ns10 / 10, ns10 % 10);
  } else {
    UiStrPrintf(buf, sizeof(buf), "--");
  }
  GfxTextRight(col->X + col->W, col->Y, buf, color, 1);
  col->Y += 22;
}

static VOID DrawStatsCard(GFX_RECT card)
{
  GFX_RECT inner = UiCardInner(card);
  GFX_RECT col = inner;

  UiCard(card, "延迟统计", "单程");
  DrawStatRow(&col, "同核双线程", gLat.SiblingNs10, gTheme.Chart2);
  DrawStatRow(&col, "同插槽跨核", gLat.IntraNs10, gTheme.Chart1);
  DrawStatRow(&col, "跨插槽", gLat.CrossNs10, gTheme.Crit);
  col.Y += 6;
  UiDivider((GFX_RECT){ col.X, col.Y, col.W, 1 });
  col.Y += 10;
  DrawStatRow(&col, "全部中位", gLat.MedianNs10, gTheme.Text);
  DrawStatRow(&col, "最小", gLat.MinNs10, gTheme.Ok);
  DrawStatRow(&col, "最大", gLat.MaxNs10, gTheme.Crit);
  if (gLat.CrossNs10 && gLat.IntraNs10) {
    char buf[64];
    UINT32 ratio = gLat.CrossNs10 * 100 / (gLat.IntraNs10 ? gLat.IntraNs10 : 1);
    UiStrPrintf(buf, sizeof(buf), "跨插槽是同插槽的 %u.%u 倍", ratio / 100, ratio % 100);
    GfxTextClipped(col.X, col.Y + 4, col.W, buf, gTheme.SubText, 1);
  }
}

static VOID DrawSocketCard(GFX_RECT card)
{
  GFX_RECT inner = UiCardInner(card);
  UINT32 k = gLat.Count ? gLat.SocketCount : 1;
  UINT32 cell, i, j, mx, my;
  char buf[16];

  UiCard(card, "插槽间延迟矩阵", "中位");
  if (k == 0) {
    k = 1;
  }
  if (k > LAT_MAX_SOCKET) {
    k = LAT_MAX_SOCKET;
  }
  if (!gLat.Ran) {
    GfxTextClipped(inner.X, inner.Y + 6, inner.W, "测量后显示各插槽之间的平均延迟", gTheme.Muted, 1);
    GfxTextClipped(inner.X, inner.Y + 30, inner.W,
                   k > 1 ? "多插槽机器可借此判断跨片互连是否对称" : "当前为单插槽平台",
                   gTheme.SubText, 1);
    return;
  }
  cell = (inner.W - 40) / (k + 1);
  if (cell > 62) {
    cell = 62;
  }
  if (cell < 26) {
    cell = 26;
  }
  mx = inner.X + 34;
  my = inner.Y + 24;
  for (j = 0; j < k; j++) {
    UiStrPrintf(buf, sizeof(buf), "P%u", j);
    GfxTextCenter(mx + j * cell, cell, my - 18, buf, gTheme.SubText, 1);
    GfxTextRight(mx - 8, my + j * cell + cell / 2 - 8, buf, gTheme.SubText, 1);
  }
  for (i = 0; i < k; i++) {
    for (j = 0; j < k; j++) {
      GFX_RECT c = { mx + j * cell, my + i * cell, cell - 2, cell - 2 };
      UINT32 v = gLat.SockNs10[i][j];
      UINT32 lo = 1, hi = 2;
      char num[16];
      LatRange(&lo, &hi);
      GfxFillRound(c, 6, (i == j) ? gTheme.PrimarySoft : HeatColor(v, lo, hi));
      if (v == 0) {
        GfxTextCenter(c.X, c.W, c.Y + (c.H - 16) / 2, "--", gTheme.Muted, 1);
      } else {
        UiStrPrintf(num, sizeof(num), "%u.%u", v / 10, v % 10);
        GfxTextCenter(c.X, c.W, c.Y + (c.H - 16) / 2, num,
                      (i == j) ? gTheme.PrimaryDark
                               : (v > (hi + lo) / 2 ? gTheme.OnPrimary : gTheme.Text), 1);
      }
    }
  }
  {
    UINT32 y = my + k * cell + 12;
    char line[120];
    if (k > 1) {
      UINT32 sum = 0, cnt = 0;
      for (i = 0; i < k; i++) {
        for (j = 0; j < k; j++) {
          if (i != j && gLat.SockCount[i][j]) {
            sum += gLat.SockNs10[i][j];
            cnt++;
          }
        }
      }
      UiStrPrintf(line, sizeof(line), "跨插槽平均 %u.%u ns（%u 个方向）", (sum / (cnt ? cnt : 1)) / 10,
                  (sum / (cnt ? cnt : 1)) % 10, cnt);
    } else {
      UiStrPrintf(line, sizeof(line), "单插槽平台，无跨片数据");
    }
    GfxTextClipped(inner.X, y, inner.W, line, gTheme.Text, 1);
    if (gLat.SockMinNs10[0][1] && k > 1) {
      UiStrPrintf(line, sizeof(line), "P0-P1 %u.%u ~ %u.%u ns",
                  gLat.SockMinNs10[0][1] / 10, gLat.SockMinNs10[0][1] % 10,
                  gLat.SockMaxNs10[0][1] / 10, gLat.SockMaxNs10[0][1] % 10);
      GfxTextClipped(inner.X, y + 22, inner.W, line, gTheme.SubText, 1);
    }
  }
}

VOID PageLatencyDraw(GFX_RECT content)
{
  UINT32 gap = 12;
  UINT32 rightW = 392;
  GFX_RECT matrixCard, statusCard, statsCard, sockCard;
  UINT32 rightH;

  matrixCard.X = content.X;
  matrixCard.Y = content.Y;
  matrixCard.W = content.W - rightW - gap;
  matrixCard.H = content.H;

  rightH = content.H - gap * 2;
  statusCard.X = content.X + matrixCard.W + gap;
  statusCard.Y = content.Y;
  statusCard.W = rightW;
  statusCard.H = gLatRunning ? 190 : 158;
  statsCard = statusCard;
  statsCard.Y += statusCard.H + gap;
  statsCard.H = 210;
  sockCard = statsCard;
  sockCard.Y += statsCard.H + gap;
  sockCard.H = content.Y + content.H - sockCard.Y;

  UiCard(matrixCard, "核间延迟热力图",
         gLatRunning ? "测量中" : (gLat.Ran ? "F5 重测" : "F5 测量"));
  DrawMatrixCard(matrixCard);
  DrawStatusCard(statusCard);
  DrawStatsCard(statsCard);
  if (sockCard.H > 60) {
    DrawSocketCard(sockCard);
  }
  (VOID)rightH;
}
