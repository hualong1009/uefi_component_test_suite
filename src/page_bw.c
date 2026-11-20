/* page_bw.c - memory bandwidth heat matrix.

   The matrix is processor x processor like the latency page, so the two pages
   read the same way, but the column decides which memory is streamed: every
   processor of a domain stands for the memory of that domain. A row is one
   core, a column block is one memory domain, the diagonal blocks are local
   bandwidth and the off diagonal blocks cross the inter-socket link. */
#include "pages.h"
#include "bw.h"
#include "latency.h"
#include "results.h"
#include "mouse.h"
#include "debug.h"

#define BW_ZOOM 24

static UINT32   gBwRow;
static UINT32   gBwCol;
static BOOLEAN  gBwZoom;
static BOOLEAN  gBwRunning;
static UINT32   gBwDone;
static UINT32   gBwTotal;
static GFX_RECT gBwRect;
static UINT32   gBwCellPx = 1;
static UINT32   gBwFirst;
static UINT32   gBwWin = 1;
static UINT64   gBwStartTsc;

/* ------------------------------------------------------------- helpers */
static UINT32 MixC(UINT32 a, UINT32 b, UINT32 t)
{
  UINT32 ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
  UINT32 br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
  UINT32 r = (ar * (255 - t) + br * t) / 255;
  UINT32 g = (ag * (255 - t) + bg * t) / 255;
  UINT32 l = (ab * (255 - t) + bb * t) / 255;
  return GFX_RGB(r, g, l);
}

/* bandwidth is the opposite of latency: more is better, so the ramp runs from
   red (slow) to green (fast) */
static UINT32 BwColor(UINT32 v10, UINT32 lo, UINT32 hi)
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
    return MixC(gTheme.Crit, gTheme.Warn, t * 255 / 500);
  }
  return MixC(gTheme.Warn, gTheme.Ok, (t - 500) * 255 / 500);
}

static const char *BwText(UINT32 v10, char *buf, UINTN cap)
{
  if (v10 == 0) {
    UiStrPrintf(buf, cap, "--");
  } else {
    UiStrPrintf(buf, cap, "%u.%u", v10 / 10, v10 % 10);
  }
  return buf;
}

static void BwRange(UINT32 *lo, UINT32 *hi)
{
  /* while the cells trickle in the running min/max spans nothing yet, so use
     a fixed 0.1 .. 25 GB/s ramp to keep the colours readable */
  if (gBwRunning) {
    *lo = 1;
    *hi = 250;
    return;
  }
  *lo = gBw.MinMbs ? gBw.MinMbs : 1;
  *hi = gBw.MaxMbs ? gBw.MaxMbs : (*lo + 1);
  if (*hi <= *lo) {
    *hi = *lo + 1;
  }
}

static const char *BwDomName(UINT32 dom, char *buf, UINTN cap)
{
  UiStrPrintf(buf, cap, "N%u", dom);
  return buf;
}

/* ------------------------------------------------------------ lifecycle */
static VOID BwClampSelection(VOID)
{
  UINT32 n = gBw.Count ? gBw.Count : 1;

  if (gBwRow >= n) {
    gBwRow = n - 1;
  }
  if (gBwCol >= n) {
    gBwCol = n - 1;
  }
}

VOID PageBwEnter(VOID)
{
  char buf[40];

  BwPrepare();
  BwClampSelection();
  if (!gBw.Ran) {
    UiSetStatus(0, "按 F5 测量每个处理器到各内存域的读取带宽");
  } else if (gBw.TotalMbs) {
    UiStrPrintf(buf, sizeof(buf), "全核合计 %u.%u GB/s", gBw.TotalMbs / 10,
                gBw.TotalMbs % 10);
    UiSetStatus(0, buf);
  }
}

static BOOLEAN BwTick(UINT32 done, UINT32 total)
{
  gBwDone = done;
  gBwTotal = total;
  UiPump();
  UiDrawEverything();
  if (!gBwRunning) {
    return FALSE;
  }
  return !gStopRequested;
}

static VOID BwStart(VOID)
{
  EFI_STATUS status;

  gStopRequested = FALSE;
  gBusy = TRUE;
  gBwRunning = TRUE;
  gBwDone = 0;
  gBwTotal = 0;
  gBwZoom = FALSE;
  gBwFirst = 0;
  gBwStartTsc = SysTsc();
  UiRedraw();
  status = BwRun(BwTick);
  gBwRunning = FALSE;
  gBusy = FALSE;
  gBwDone = gBwTotal;

  if (gBw.Ran) {
    gResults.BwTested = TRUE;
    gResults.BwCpus = gBw.Count;
    gResults.BwDoms = gBw.DomCount;
    gResults.BwCells = gBw.Cells;
    gResults.BwElapsedMs = gBw.ElapsedMs;
    gResults.BwBytes = gBw.Bytes;
    gResults.BwMedianMbs = gBw.MedianMbs;
    gResults.BwLocalMbs = gBw.LocalMbs;
    gResults.BwRemoteMbs = gBw.RemoteMbs;
    gResults.BwMinMbs = gBw.MinMbs;
    gResults.BwMaxMbs = gBw.MaxMbs;
    gResults.BwTotalMbs = gBw.TotalMbs;
    gResults.BwPeakMbs = gBw.PeakMbs;
    {
      UINT32 i;
      for (i = 0; i < 8; i++) {
        gResults.BwDomMbs[i] = (i < BW_MAX_DOM) ? gBw.DomAggMbs[i] : 0;
      }
    }
    if (gBw.Aborted) {
      UiSetStatus(2, "带宽测量已中止，矩阵不完整");
    } else {
      UiSetStatus(1, "内存带宽测量完成");
    }
  } else if (status == EFI_UNSUPPORTED) {
    UiSetStatus(2, "未找到可用的处理器服务，无法测量");
  } else if (status == EFI_OUT_OF_RESOURCES) {
    UiSetStatus(2, "可用内存不足以放置测量缓冲区");
  } else {
    UiSetStatus(3, "带宽测量失败");
  }
  UiRedraw();
}

/* ------------------------------------------------------------ navigation */
static UINT32 BwStep(UINT32 v, INT32 d, UINT32 n)
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

static VOID BwMove(INT32 dRow, INT32 dCol)
{
  UINT32 n = gBw.Count ? gBw.Count : 1;

  gBwRow = BwStep(gBwRow, dRow, n);
  gBwCol = BwStep(gBwCol, dCol, n);
}

static VOID BwCellClick(UINT32 id)
{
  UINT32 x, y, col, row;

  (VOID)id;
  x = MouseX();
  y = MouseY();
  if (x < gBwRect.X || y < gBwRect.Y || gBwCellPx == 0) {
    return;
  }
  if (x >= gBwRect.X + gBwWin * gBwCellPx || y >= gBwRect.Y + gBwWin * gBwCellPx) {
    return;
  }
  col = gBwFirst + (x - gBwRect.X) / gBwCellPx;
  row = gBwFirst + (y - gBwRect.Y) / gBwCellPx;
  if (row < gBw.Count && col < gBw.Count) {
    gBwRow = row;
    gBwCol = col;
    UiRedraw();
  }
}

BOOLEAN PageBwKey(EFI_INPUT_KEY key)
{
  UINT32 n = gBw.Count ? gBw.Count : 1;

  if (key.ScanCode == SCAN_F5) {
    if (!gBwRunning) {
      BwStart();
    }
    return TRUE;
  }
  if (key.ScanCode == SCAN_F6 || key.ScanCode == SCAN_ESC) {
    if (gBwRunning) {
      gStopRequested = TRUE;
      UiSetStatus(2, "正在停止测量…");
      return TRUE;
    }
    return FALSE;
  }
  if (key.UnicodeChar == 'z' || key.UnicodeChar == 'Z') {
    gBwZoom = gBwZoom ? FALSE : TRUE;
    return TRUE;
  }
  if (key.UnicodeChar == 'r' || key.UnicodeChar == 'R') {
    gBwRow = 0;
    gBwCol = 0;
    gBwFirst = 0;
    return TRUE;
  }
  if (key.UnicodeChar == 'l' || key.UnicodeChar == 'L') {
    UiSwitchPage(PAGE_LATENCY);
    return TRUE;
  }
  /* left/right always change pages, so the column axis has its own keys */
  if (key.UnicodeChar == '[') {
    BwMove(0, -1);
    return TRUE;
  }
  if (key.UnicodeChar == ']') {
    BwMove(0, 1);
    return TRUE;
  }
  if (key.UnicodeChar == 'n' || key.UnicodeChar == 'N') {
    UINT32 d = BwDomOf(gBwCol) + 1;
    if (d >= gBw.DomCount) {
      d = 0;
    }
    while (gBwCol + 1 < n && BwDomOf(gBwCol) != d) {
      gBwCol++;
    }
    return TRUE;
  }
  if (key.UnicodeChar == 'p' || key.UnicodeChar == 'P') {
    UINT32 d = BwDomOf(gBwCol);
    d = d ? d - 1 : gBw.DomCount - 1;
    gBwCol = 0;
    while (gBwCol + 1 < n && BwDomOf(gBwCol) != d) {
      gBwCol++;
    }
    return TRUE;
  }
  switch (key.ScanCode) {
  case SCAN_UP:
    BwMove(-1, 0);
    break;
  case SCAN_DOWN:
    BwMove(1, 0);
    break;
  case SCAN_PAGE_UP:
    BwMove(n > 8 ? -8 : -(INT32)n, 0);
    break;
  case SCAN_PAGE_DOWN:
    BwMove(n > 8 ? 8 : (INT32)n, 0);
    break;
  case SCAN_HOME:
    gBwRow = 0;
    gBwCol = 0;
    break;
  case SCAN_END:
    gBwRow = n - 1;
    gBwCol = n - 1;
    break;
  default:
    if (key.UnicodeChar == '\r' && gBw.Ran) {
      gBwZoom = gBwZoom ? FALSE : TRUE;
      return TRUE;
    }
    return FALSE;
  }
  return TRUE;
}

/* --------------------------------------------------------------- drawing */
static VOID DrawLegend(GFX_RECT r, UINT32 lo, UINT32 hi)
{
  const UINT32 strips = 48;
  const UINT32 w = 220;
  UINT32 i;
  char buf[32];

  for (i = 0; i < strips; i++) {
    GfxFillRect((GFX_RECT){ r.X + (w - 4) * i / (strips - 1), r.Y, 6, 10 },
                BwColor(lo + (hi - lo) * i / (strips - 1), lo, hi));
  }
  UiStrPrintf(buf, sizeof(buf), "%u.%u GB/s", lo / 10, lo % 10);
  GfxText(r.X, r.Y + 14, buf, gTheme.Muted, 1);
  UiStrPrintf(buf, sizeof(buf), "%u.%u GB/s", hi / 10, hi % 10);
  GfxTextRight(r.X + w + 4, r.Y + 14, buf, gTheme.Muted, 1);
}

static VOID DrawMatrixCard(GFX_RECT card)
{
  GFX_RECT inner = UiCardInner(card);
  UINT32 n = gBw.Count ? gBw.Count : 1;
  UINT32 areaW, areaH, side, cell, win, first, row, col;
  UINT32 lo = 1, hi = 2;
  UINT32 mx, my, i, readoutY;
  char buf[160];
  char lab1[24], num[24], dom[16];
  GFX_RECT crect;

  BwRange(&lo, &hi);

  win = n;
  readoutY = inner.Y + inner.H - 76;
  areaW = inner.W > 76 ? inner.W - 76 : 1;
  areaH = (readoutY > inner.Y + 80) ? readoutY - inner.Y - 60 : 1;
  side = areaW < areaH ? areaW : areaH;
  if (gBwZoom && n > 8) {
    /* a zoom window is only worth it while a cell can still hold a number */
    UINT32 fit = side / 40;
    if (fit < 8) {
      fit = 8;
    }
    if (fit > BW_ZOOM) {
      fit = BW_ZOOM;
    }
    win = (fit < n) ? fit : n;
  }
  cell = side / win;
  if (cell < 2) {
    cell = 2;
  }
  if (cell > 60) {
    cell = 60;
  }
  first = 0;
  if (win < n) {
    if (gBwRow >= win / 2) {
      first = gBwRow - win / 2;
    }
    if (first + win > n) {
      first = n - win;
    }
  }
  mx = inner.X + 60 + ((areaW > win * cell) ? (areaW - win * cell) / 2 : 0);
  my = inner.Y + 26 + ((areaH > win * cell) ? (areaH - win * cell) / 3 : 0);

  gBwCellPx = cell;
  gBwFirst = first;
  gBwWin = win;
  gBwRect.X = mx;
  gBwRect.Y = my;
  gBwRect.W = cell * win;
  gBwRect.H = cell * win;
  UiHitAdd(gBwRect, BwCellClick, 0);

  if (!gBw.Ran && !gBwRunning) {
    UiStrPrintf(buf, sizeof(buf), "%u 个逻辑处理器 · %u 个内存域", gBw.Count, gBw.DomCount);
    GfxTextCenter(inner.X, inner.W, inner.Y + inner.H / 2 - 44, "内存带宽矩阵", gTheme.Text, 1);
    GfxTextCenter(inner.X, inner.W, inner.Y + inner.H / 2 - 20, buf, gTheme.SubText, 1);
    UiStrPrintf(buf, sizeof(buf), "%u 个插槽 · %u 个采样 · 每次 %u MB · 按 F5 开始",
                gBw.SocketCount, gBw.CellsTotal, gBw.Bytes);
    GfxTextCenter(inner.X, inner.W, inner.Y + inner.H / 2 + 6, buf, gTheme.Muted, 1);
    if (!gBw.PlacementKnown) {
      GfxTextCenter(inner.X, inner.W, inner.Y + inner.H / 2 + 32,
                    gBw.SharedBuffer ? "SRAT 未提供分域内存，各列共用同一缓冲区"
                                     : "SRAT 未提供分域内存，各列的物理放置并不确定",
                    gTheme.Warn, 1);
    }
    return;
  }

  for (row = 0; row < win; row++) {
    for (col = 0; col < win; col++) {
      UINT32 a = first + row, b = first + col;
      crect.X = mx + col * cell;
      crect.Y = my + row * cell;
      crect.W = cell;
      crect.H = cell;
      GfxFillRect(crect, BwColor(BwCell(a, BwDomOf(b)), lo, hi));
    }
  }

  if (gBwRow >= first && gBwRow < first + win && gBwCol >= first && gBwCol < first + win) {
    UINT32 rp = my + (gBwRow - first) * cell;
    UINT32 cp = mx + (gBwCol - first) * cell;
    GfxFillRect((GFX_RECT){ mx, rp + cell / 2, win * cell, 1 }, gTheme.Panel);
    GfxFillRect((GFX_RECT){ cp + cell / 2, my, 1, win * cell }, gTheme.Panel);
  }

  if (gBwZoom && cell >= 24) {
    for (row = 0; row < win; row++) {
      for (col = 0; col < win; col++) {
        UINT32 a = first + row, b = first + col;
        UINT32 v = BwCell(a, BwDomOf(b));
        crect.X = mx + col * cell;
        crect.Y = my + row * cell;
        crect.W = cell;
        crect.H = cell;
        if (v) {
          UiStrPrintf(num, sizeof(num), "%u.%u", v / 10, v % 10);
          if ((UINT32)GfxTextWidth(num, 1) + 6 > cell) {
            UiStrPrintf(num, sizeof(num), "%u", (v + 5) / 10);
          }
          GfxTextCenter(crect.X, cell, crect.Y + (cell - 16) / 2, num,
                        (v < (hi + lo) / 2) ? gTheme.OnPrimary : gTheme.Text, 1);
        }
      }
    }
  }

  /* domain separators: local memory block against the remote ones */
  for (i = 1; i < n; i++) {
    if (BwDomOf(i) != BwDomOf(i - 1) && i > first && i < first + win) {
      GfxFillRect((GFX_RECT){ mx + (i - first) * cell - 1, my, 2, win * cell }, gTheme.Panel);
      GfxFillRect((GFX_RECT){ mx, my + (i - first) * cell - 1, win * cell, 2 }, gTheme.Panel);
    }
  }

  /* axis labels: one per domain block plus a tick every 32 processors */
  {
    UINT32 a = first;
    while (a < first + win) {
      UINT32 blockEnd = a + 1;
      UINT32 labelX = mx + (a - first) * cell;
      UINT32 labelW;
      UINT32 d = BwDomOf(a);
      while (blockEnd < n && BwDomOf(blockEnd) == d) {
        blockEnd++;
      }
      if (blockEnd > first + win) {
        blockEnd = first + win;
      }
      BwDomName(d, dom, sizeof(dom));
      labelW = GfxTextWidth(dom, 1);
      if (blockEnd - a >= 3) {
        GfxTextCenter(labelX, (blockEnd - a) * cell, my - 18, dom, gTheme.Text, 1);
      } else {
        GfxText(labelX, my - 18, dom, gTheme.Text, 1);
      }
      GfxText((mx > labelW + 6) ? mx - labelW - 6 : 0, my + (a - first) * cell + 1, dom,
              gTheme.SubText, 1);
      a = blockEnd;
    }
  }
  for (i = 32; i < win; i += 32) {
    UiStrPrintf(buf, sizeof(buf), "%u", first + i);
    GfxText(mx + i * cell, my - 34, buf, gTheme.Muted, 1);
    GfxTextRight(mx - 30, my + i * cell + 8, buf, gTheme.Muted, 1);
  }

  if (gBwRow >= first && gBwRow < first + win && gBwCol >= first && gBwCol < first + win) {
    UINT32 rp = my + (gBwRow - first) * cell;
    UINT32 cp = mx + (gBwCol - first) * cell;
    if (cell > 5) {
      GfxFrameRect((GFX_RECT){ cp + 1, rp + 1, cell - 2, cell - 2 }, gTheme.OnPrimary);
    }
    GfxFrameRect((GFX_RECT){ cp, rp, cell, cell }, gTheme.Text);
  }

  {
    UINT32 d = BwDomOf(gBwCol);
    UINT32 v = BwCell(gBwRow, d);
    const char *kind;

    LatCpuLabel(gBwRow, lab1, sizeof(lab1));
    if (gBwCpus[gBwRow].Dom == d) {
      kind = L("本地内存");
    } else {
      kind = L("远端内存");
    }
    UiStrPrintf(buf, sizeof(buf), "%s  ->  %s %s  %s GB/s", lab1,
                BwDomName(d, dom, sizeof(dom)), L("内存"), BwText(v, num, sizeof(num)));
    GfxTextClipped(inner.X, readoutY - 20, inner.W, kind,
                   (gBwCpus[gBwRow].Dom == d) ? gTheme.Ok : gTheme.Warn, 1);
    GfxTextClipped(inner.X, readoutY, inner.W, buf, gTheme.Text, 1);
    DrawLegend((GFX_RECT){ inner.X + inner.W - 250, readoutY + 20, 250, 24 }, lo, hi);
  }
  if (gBwZoom) {
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
  UINT32 elapsed = gBwRunning ? (UINT32)SysMsSince(gBwStartTsc) : gBw.ElapsedMs;

  UiCard(card, "测量状态", gBwRunning ? "运行中" : (gBw.Ran ? "已完成" : "未运行"));
  if (gBwRunning) {
    UINT32 pm = gBwTotal ? (gBwDone * 1000 / gBwTotal) : 0;
    UiStrPrintf(buf, sizeof(buf), "%u / %u 采样", gBwDone, gBwTotal);
    UiProgressBar((GFX_RECT){ inner.X, inner.Y, inner.W, 14 }, pm, gTheme.Primary, buf);
    col.Y += 26;
  }
  UiStrPrintf(buf, sizeof(buf), "%u x %u", gBw.Count, gBw.DomCount);
  GfxText(col.X, col.Y, "矩阵维度", gTheme.Muted, 1);
  GfxTextRight(col.X + col.W, col.Y, buf, gTheme.Text, 1);
  col.Y += 22;
  UiStrPrintf(v, sizeof(v), "%u 个", gBw.DomCount);
  GfxText(col.X, col.Y, "内存域", gTheme.Muted, 1);
  GfxTextRight(col.X + col.W, col.Y, v, gTheme.Text, 1);
  col.Y += 22;
  UiStrPrintf(buf, sizeof(buf), "%u / %u", gBw.Cells, gBw.CellsTotal);
  GfxText(col.X, col.Y, "已测单元", gTheme.Muted, 1);
  GfxTextRight(col.X + col.W, col.Y, buf, gTheme.Text, 1);
  col.Y += 22;
  UiStrPrintf(buf, sizeof(buf), "%u MB", gBw.Bytes);
  GfxText(col.X, col.Y, "每次采样", gTheme.Muted, 1);
  GfxTextRight(col.X + col.W, col.Y, buf, gTheme.Text, 1);
  col.Y += 22;
  UiStrPrintf(buf, sizeof(buf), "%u 毫秒", elapsed);
  GfxText(col.X, col.Y, "耗时", gTheme.Muted, 1);
  GfxTextRight(col.X + col.W, col.Y, buf, gTheme.Text, 1);
  col.Y += 22;
  GfxText(col.X, col.Y, "内存放置", gTheme.Muted, 1);
  if (!gBw.Count) {
    GfxTextRight(col.X + col.W, col.Y, "n/a", gTheme.Muted, 1);
  } else if (gBw.PlacementKnown) {
    GfxTextRight(col.X + col.W, col.Y, L("SRAT"), gTheme.Ok, 1);
  } else {
    GfxTextRight(col.X + col.W, col.Y, L("不可用"), gTheme.Warn, 1);
  }
}

static VOID DrawStatRow(GFX_RECT *col, const char *label, UINT32 v10, UINT32 color)
{
  char buf[32];

  GfxText(col->X, col->Y, label, gTheme.Muted, 1);
  if (v10) {
    UiStrPrintf(buf, sizeof(buf), "%u.%u GB/s", v10 / 10, v10 % 10);
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

  UiCard(card, "带宽统计", "单核");
  DrawStatRow(&col, "本地内存", gBw.LocalMbs, gTheme.Ok);
  DrawStatRow(&col, "远端内存", gBw.RemoteMbs, gTheme.Warn);
  DrawStatRow(&col, "全部中位", gBw.MedianMbs, gTheme.Text);
  col.Y += 6;
  UiDivider((GFX_RECT){ col.X, col.Y, col.W, 1 });
  col.Y += 10;
  DrawStatRow(&col, "最小", gBw.MinMbs, gTheme.Crit);
  DrawStatRow(&col, "最大", gBw.MaxMbs, gTheme.Ok);
  if (gBw.LocalMbs && gBw.RemoteMbs) {
    char buf[64];
    UINT32 ratio = gBw.RemoteMbs * 1000 / (gBw.LocalMbs ? gBw.LocalMbs : 1);
    UiStrPrintf(buf, sizeof(buf), "远端是本地内存的 %u.%u 倍", ratio / 1000,
                ratio % 1000 / 100);
    GfxTextClipped(col.X, col.Y + 4, col.W, buf, gTheme.SubText, 1);
  }
}

/* aggregate card: one bar per pass plus the platform total */
static VOID DrawTotalCard(GFX_RECT card)
{
  GFX_RECT inner = UiCardInner(card);
  GFX_RECT col = inner;
  char buf[48];
  char line[96];
  UINT32 i, top;

  UiCard(card, "总带宽", "全核");
  if (!gBw.Ran) {
    GfxTextClipped(inner.X, inner.Y + 6, inner.W, "测量后显示所有核心同时读取的总带宽", gTheme.Muted, 1);
    GfxTextClipped(inner.X, inner.Y + 30, inner.W, "以及每个内存域各自能跑到的上限", gTheme.SubText, 1);
    return;
  }
  top = gBw.TotalMbs;
  for (i = 0; i < gBw.DomCount; i++) {
    if (gBw.DomAggMbs[i] > top) {
      top = gBw.DomAggMbs[i];
    }
  }
  if (top == 0) {
    top = 1;
  }
  GfxText(col.X, col.Y, "全核合计（本地内存）", gTheme.Muted, 1);
  col.Y += 18;
  UiStrPrintf(buf, sizeof(buf), "%u.%u GB/s", gBw.TotalMbs / 10, gBw.TotalMbs % 10);
  GfxText(col.X, col.Y, buf, gTheme.Text, 1);
  col.Y += 18;
  UiProgressBar((GFX_RECT){ col.X, col.Y, col.W, 8 }, 1000, gTheme.Primary, 0);
  col.Y += 16;
  for (i = 0; i < gBw.DomCount && i < BW_MAX_DOM; i++) {
    UINT32 pm = (UINT32)((UINT64)gBw.DomAggMbs[i] * 1000 / top);
    char dname[16];
    BwDomName(i, dname, sizeof(dname));
    UiStrPrintf(buf, sizeof(buf), "%s %u.%u GB/s", dname, gBw.DomAggMbs[i] / 10,
                gBw.DomAggMbs[i] % 10);
    UiProgressBar((GFX_RECT){ col.X, col.Y, col.W, 14 }, pm, gTheme.Chart2, buf);
    col.Y += 20;
  }
  col.Y += 4;
  UiStrPrintf(line, sizeof(line), "单核峰值 %u.%u GB/s", gBw.PeakMbs / 10, gBw.PeakMbs % 10);
  GfxTextClipped(col.X, col.Y, col.W, line, gTheme.Text, 1);
  if (gBw.LocalCount && gBw.RemoteCount) {
    UiStrPrintf(line, sizeof(line), "本地 %u 格 / 远端 %u 格", gBw.LocalCount, gBw.RemoteCount);
    GfxTextClipped(col.X, col.Y + 22, col.W, line, gTheme.Muted, 1);
  }
}

VOID PageBwDraw(GFX_RECT content)
{
  const UINT32 gap = 12;
  const UINT32 rightW = 392;
  GFX_RECT matrixCard, statusCard, statsCard, totalCard;

  matrixCard.X = content.X;
  matrixCard.Y = content.Y;
  matrixCard.W = content.W - rightW - gap;
  matrixCard.H = content.H;

  statusCard.X = content.X + matrixCard.W + gap;
  statusCard.Y = content.Y;
  statusCard.W = rightW;
  statusCard.H = gBwRunning ? 200 : 180;
  statsCard = statusCard;
  statsCard.Y += statusCard.H + gap;
  statsCard.H = 190;
  totalCard = statsCard;
  totalCard.Y += statsCard.H + gap;
  totalCard.H = content.Y + content.H - totalCard.Y;

  UiCard(matrixCard, "内存带宽热力图",
         gBwRunning ? "测量中" : (gBw.Ran ? "F5 重测" : "F5 测量"));
  DrawMatrixCard(matrixCard);
  DrawStatusCard(statusCard);
  DrawStatsCard(statsCard);
  if (totalCard.H > 60) {
    DrawTotalCard(totalCard);
  }
}
