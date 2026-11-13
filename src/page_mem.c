/* page_mem.c - memory pattern test page. */
#include "pages.h"
#include "memtest.h"
#include "results.h"

static UINT32  gMemSel;
static UINT32  gMemTop;
static BOOLEAN gMemWide;
static BOOLEAN gMemRunning;
static MEM_STEP gMemLive;

static UINT32 gMemNameCol;

/* DrawStatRow takes const char *, so the formatted sizes need a scratch area */
static char   gSizeBuf[2][16];
static UINT32 gSizeTurn;

static const char *SizeText(UINT64 bytes)
{
  char *b = gSizeBuf[gSizeTurn++ & 1];
  GfxFormatSize(b, sizeof(gSizeBuf[0]), bytes);
  return b;
}

/* the step reports the ordinal among enabled patterns, the names live in the
   full list: walk the enabled ones to find it */
static const char *EnabledPatternName(UINT32 ordinal)
{
  UINT32 seen = 0, k;
  for (k = 0; k < gMemPatternCount; k++) {
    if (!gMemPatterns[k].Enabled) {
      continue;
    }
    if (seen == ordinal) {
      return L(gMemPatterns[k].Name);
    }
    seen++;
  }
  return "?";
}

/* dragging the pattern list moves the selection with the window */
static VOID MemScrollPatterns(UINT32 first, UINT32 visible)
{
  (VOID)visible;
  gMemTop = first;
  if (first < gMemPatternCount) {
    gMemSel = first;
  }
}

static UINT32 VisibleRows(GFX_RECT list)
{
  return (list.H - 8) / 26;
}

static VOID DrawPatterns(GFX_RECT card)
{
  GFX_RECT inner = UiCardInner(card);
  UINT32 rows = VisibleRows(inner);
  UINT32 i;
  char buf[48];

  gMemNameCol = 0;
  for (i = 0; i < gMemPatternCount; i++) {
    UINT32 w = GfxTextWidth(gMemPatterns[i].Name, 1);
    if (w > gMemNameCol) {
      gMemNameCol = w;
    }
  }
  if (gMemNameCol > (inner.W * 3) / 5) {
    gMemNameCol = (inner.W * 3) / 5;
  }
  gMemNameCol += 4;

  UiStrPrintf(buf, sizeof(buf), "%u / %u 启用", MemEnabledPatterns(), gMemPatternCount);
  GfxTextRight(card.X + card.W - 16, card.Y + 14, buf, gTheme.SubText, 1);

  for (i = 0; i < rows; i++) {
    UINT32 idx = gMemTop + i;
    GFX_RECT row;
    char mark[8];
    if (idx >= gMemPatternCount) {
      break;
    }
    row.X = inner.X;
    row.Y = inner.Y + i * 26;
    row.W = inner.W;
    row.H = 24;
    if (idx == gMemSel) {
      GfxFillRound(row, 6, gTheme.PrimarySoft);
    }
    /* checkbox */
    UiCheck((GFX_RECT){ row.X + 6, row.Y + 5, 14, 14 }, gMemPatterns[idx].Enabled);
    {
      /* one name column measured from the longest name, then the detail */
      UINT32 nameW = gMemNameCol;
      UINT32 nameX = row.X + 30;
      GfxTextClipped(nameX, row.Y + 4, nameW, gMemPatterns[idx].Name,
                     idx == gMemSel ? gTheme.PrimaryDark : gTheme.Text, 1);
      GfxTextClipped(nameX + nameW + 10, row.Y + 4, row.W - 44 - nameW - 10,
                     gMemPatterns[idx].Detail, gTheme.Muted, 1);
    }
    UiHitRowBox(row, idx);
    (VOID)mark;
  }
  UiScrollbarScroll((GFX_RECT){ card.X + card.W - 8, inner.Y, 4, inner.H }, gMemTop, rows,
                    gMemPatternCount, MemScrollPatterns);
}

static VOID DrawStatRow(GFX_RECT *r, const char *label, const char *value, UINT32 color)
{
  UINT32 kw = GfxTextWidth(label, 1) + 14;
  GfxText(r->X, r->Y + 2, label, gTheme.SubText, 1);
  GfxTextClipped(r->X + kw, r->Y + 2, r->W > kw ? r->W - kw : 1, value, color, 1);
  r->Y += 24;
}

VOID PageMemEnter(VOID)
{
  gMemSel = 0;
  gMemTop = 0;
}

VOID PageMemLeave(VOID) {}

VOID PageMemDraw(GFX_RECT content)
{
  UINT32 gap = 12;
  UINT32 tileH = 84;
  UINT32 tileW = (content.W - gap * 3) / 4;
  GFX_RECT listCard, progCard, dimmCard;
  char buf[128];
  char buf2[128];
  UINT32 y = content.Y;

  /* ---- summary tiles ---- */
  {
    struct {
      const char *label;
      char value[48];
      const char *sub;
      UINT32 color;
    } tiles[4];
    UINT32 i;

    UiStrPrintf(tiles[0].value, sizeof(tiles[0].value), "%u GB",
                (UINT32)(gApp.Sys.TestableRam / (1024ull * 1024 * 1024)));
    tiles[0].label = "可测试内存";
    tiles[0].sub = "内存映射中空闲区域";
    tiles[0].color = gTheme.Primary;

    tiles[1].label = "已测试";
    if (gResults.MemTested && gResults.MemTestedBytes) {
      GfxFormatSize(tiles[1].value, sizeof(tiles[1].value), gResults.MemTestedBytes);
    } else {
      UiStrPrintf(tiles[1].value, sizeof(tiles[1].value), "-");
    }
    tiles[1].sub = "按启用图案累计";
    tiles[1].color = gTheme.Chart2;

    UiStrPrintf(tiles[2].value, sizeof(tiles[2].value), "%u", gResults.MemErrorCount);
    tiles[2].label = "检测到的错误";
    tiles[2].sub = gResults.MemErrorCount ? "存在位翻转或寻址故障" : "尚未发现问题";
    tiles[2].color = gResults.MemErrorCount ? gTheme.Crit : gTheme.Ok;

    if (gResults.MemElapsedMs) {
      UiStrPrintf(tiles[3].value, sizeof(tiles[3].value), "%u MB/s", gResults.MemBandwidthMbs);
    } else {
      UiStrPrintf(tiles[3].value, sizeof(tiles[3].value), "-");
    }
    tiles[3].label = "有效带宽";
    tiles[3].sub = gResults.MemTested ? "读写往返平均" : "运行后显示";
    tiles[3].color = gTheme.Warn;

    for (i = 0; i < 4; i++) {
      GFX_RECT r = { content.X + i * (tileW + gap), y, tileW, tileH };
      UiCard(r, 0, 0);
      GfxFillRound((GFX_RECT){ r.X + 14, r.Y + 14, 3, 14 }, 2, tiles[i].color);
      GfxText(r.X + 24, r.Y + 12, tiles[i].label, gTheme.SubText, 1);
      GfxText(r.X + 24, r.Y + 34, tiles[i].value, gTheme.Text, 1);
      GfxTextClipped(r.X + 24, r.Y + 58, r.W - 38, tiles[i].sub, gTheme.Muted, 1);
    }
  }

  /* ---- pattern list + progress ---- */
  y += tileH + gap;
  {
    UINT32 h = content.H - tileH - gap - 168 - gap;
    listCard = (GFX_RECT){ content.X, y, ILang() == LANG_ZH ? 330 : 448, h };
    progCard = (GFX_RECT){ content.X + listCard.W + gap, y, content.W - listCard.W - gap, h };
  }
  UiCard(listCard, "测试图案", 0);
  DrawPatterns(listCard);

  UiCard(progCard, gMemRunning ? "检测进行中" : "检测结果", 0);
  {
    GFX_RECT inner = UiCardInner(progCard);
    GFX_RECT row = inner;
    /* the headline number spans every enabled pattern: a run of ten patterns
       is half done after five, not 99% because one pattern filled the region */
    UINT32 permille = gMemRunning ? gMemLive.OverallPm
                                  : (gResults.MemTested ? gResults.MemCoveragePm : 0);
    UINT32 color = gResults.MemErrorCount ? gTheme.Crit : gTheme.Primary;

    UiStrPrintf(buf, sizeof(buf), "%u%%", permille / 10);
    UiProgressBar((GFX_RECT){ inner.X, inner.Y, inner.W, 14 }, permille, color, 0);
    GfxTextRight(inner.X + inner.W, inner.Y - 24, buf, gTheme.SubText, 1);

    row.Y += 26;
    if (gMemRunning) {
      UiStrPrintf(buf, sizeof(buf), "%u/%u · %s · %u MB/s",
                  gMemLive.PatternOrdinal + 1,
                  gMemLive.PatternCount ? gMemLive.PatternCount : 1,
                  EnabledPatternName(gMemLive.PatternOrdinal), gMemLive.Mbs);
      DrawStatRow(&row, "当前图案", buf, gTheme.PrimaryDark);
    } else if (gResults.MemTested) {
      UiStrPrintf(buf, sizeof(buf), "%u 个图案 · %s",
                  gResults.MemPatternsRun, gResults.MemStopped ? L("已手动停止") : L("完整通过"));
      DrawStatRow(&row, "执行情况", buf, gResults.MemStopped ? gTheme.Warn : gTheme.Ok);
    } else {
      DrawStatRow(&row, "执行情况", "尚未运行（按 F5 开始）", gTheme.Muted);
    }

    if (gMemRunning) {
      UINT64 pass = gMemLive.PassBytes ? gMemLive.PassBytes : gResults.MemTotalBytes;
      UiStrPrintf(buf, sizeof(buf), "%u%% · %s / %s", gMemLive.PatternPm / 10,
                  SizeText(gMemLive.PatternBytes), SizeText(pass));
      DrawStatRow(&row, "本图案覆盖", buf, gTheme.Text);
      UiStrPrintf(buf, sizeof(buf), "%u / %u", gMemLive.PatternOrdinal,
                  gMemLive.PatternCount);
      DrawStatRow(&row, "整体进度", buf, gTheme.SubText);
      UiStrPrintf(buf, sizeof(buf), "%u 秒 · 平均 %u MB/s", gMemLive.ElapsedMs / 1000,
                  gMemLive.Mbs);
      DrawStatRow(&row, "耗时 / 带宽", buf, gTheme.Muted);
    } else if (gResults.MemTested) {
      GfxFormatSize(buf2, sizeof(buf2), gResults.MemTestedBytes);
      UiStrPrintf(buf, sizeof(buf), "%s · 覆盖率 %u%%", buf2, gResults.MemCoveragePm / 10);
      DrawStatRow(&row, "覆盖内存", buf, gTheme.Text);
      GfxFormatSize(buf2, sizeof(buf2), gResults.MemTotalBytes);
      UiStrPrintf(buf, sizeof(buf), "%s", buf2);
      DrawStatRow(&row, "本轮容量", buf, gTheme.Text);
      UiStrPrintf(buf, sizeof(buf), "%u ms · %u MB/s", gResults.MemElapsedMs, gResults.MemBandwidthMbs);
      DrawStatRow(&row, "耗时 / 带宽", buf, gTheme.Text);
    } else {
      DrawStatRow(&row, "覆盖内存", "-", gTheme.Muted);
      DrawStatRow(&row, "本轮容量", "-", gTheme.Muted);
      DrawStatRow(&row, "耗时 / 带宽", "-", gTheme.Muted);
    }

    if (gResults.MemFirstErrorOffsetValid) {
      UiStrPrintf(buf, sizeof(buf), "0x%llX", (unsigned long long)gResults.MemFirstErrorOffset);
      DrawStatRow(&row, "首个错误地址", buf, gTheme.Crit);
      UiStrPrintf(buf, sizeof(buf), "0x%llX  期望 0x%llX",
                  (unsigned long long)gResults.MemFirstErrorActual,
                  (unsigned long long)gResults.MemFirstErrorExpected);
      DrawStatRow(&row, "读回 / 期望", buf, gTheme.Crit);
    }
    UiStrPrintf(buf, sizeof(buf), "%s", gMemWide ? "宽覆盖（最多 6 GB）" : L("标准覆盖（最多 2 GB）"));
    {
      UINT32 leftW = GfxTextWidth(buf, 1);
      GfxTextClipped(inner.X, inner.Y + inner.H - 16, inner.W, buf, gTheme.Muted, 1);
      UiStrPrintf(buf2, sizeof(buf2), "%s", "有效带宽按读写往返计算");
      GfxTextClipped(inner.X + (inner.W > leftW + 24 ? leftW + 14 : 0), inner.Y + inner.H - 16,
                     inner.W > leftW + 24 ? inner.W - leftW - 14 : 1, buf2, gTheme.Muted, 1);
    }
  }

  /* ---- DIMM map ---- */
  dimmCard = (GFX_RECT){ content.X, content.Y + content.H - 156, content.W, 156 };
  UiCard(dimmCard, "内存拓扑与错误分布", 0);
  {
    GFX_RECT inner = UiCardInner(dimmCard);
    UINT32 count = gApp.Sys.DimmPresentCount ? gApp.Sys.DimmPresentCount : 0;
    UINT32 i;
    UINT32 bw = 0;
    if (count == 0) {
      GfxText(inner.X, inner.Y, "SMBIOS 未提供内存条信息", gTheme.Muted, 1);
    }
    for (i = 0; i < count && i < 8; i++) {
      /* Dimms[] lists every slot: only the installed modules get a card, an
         empty slot would otherwise read 0B and Unknown */
      UINT32 slot = SysPresentDimm(&gApp.Sys, i);
      DIMM_INFO *d;
      GFX_RECT block;
      GFX_RECT bar;
      UINT32 err;
      if (slot >= SYS_MAX_DIMMS) {
        continue;
      }
      d = &gApp.Sys.Dimms[slot];
      bw = (inner.W - 8 * (count - 1)) / count;
      err = slot < 16 ? gResults.MemDimmErrors[slot] : 0;
      block = (GFX_RECT){ inner.X + i * (bw + 8), inner.Y, bw, 62 };
      GfxFillRound(block, 8, err ? gTheme.CritSoft : gTheme.PanelAlt);
      GfxRoundOutline(block, 8, err ? gTheme.Crit : gTheme.Border);
      UiStrPrintf(buf, sizeof(buf), "%s", d->Locator[0] ? d->Locator : "DIMM");
      GfxTextClipped(block.X + 10, block.Y + 8, block.W - 20, buf, gTheme.Text, 1);
      GfxFormatSize(buf, sizeof(buf), d->SizeBytes);
      UiStrPrintf(buf2, sizeof(buf2), "%s · %s-%u", buf, SysMemTypeName(d->MemoryType),
                  d->ConfiguredSpeed ? d->ConfiguredSpeed : d->Speed);
      GfxTextClipped(block.X + 10, block.Y + 28, block.W - 20, buf2, gTheme.SubText, 1);
      UiStrPrintf(buf, sizeof(buf), "错误 %u", err);
      GfxTextClipped(block.X + 10, block.Y + 44, block.W - 20, buf, err ? gTheme.Crit : gTheme.Ok, 1);
      bar = (GFX_RECT){ inner.X + i * (bw + 8), inner.Y + 70, bw, 6 };
      UiMiniBar(bar, err ? 800 : 120, err ? gTheme.Crit : gTheme.Ok);
    }
    if (count) {
      UiStrPrintf(buf, sizeof(buf), "错误按物理地址折算到内存条 · T 打开颗粒视图");
      GfxText(inner.X, inner.Y + 86, buf, gTheme.Muted, 1);
    }
  }
}

static BOOLEAN MemProgress(const MEM_STEP *step)
{
  gMemLive = *step;
  if (UiPump() || gStopRequested) {
    return FALSE;
  }
  UiDrawEverything();
  return TRUE;
}

static VOID StartMemoryTest(VOID)
{
  EFI_STATUS status;
  char buf[128];

  if (MemEnabledPatterns() == 0) {
    UiSetStatus(2, "请至少启用一个测试图案（空格 切换）");
    UiRedraw();
    return;
  }
  gMemRunning = TRUE;
  gApp.BS->SetMem(&gMemLive, sizeof(gMemLive), 0);
  gStopRequested = FALSE;
  UiSetStatus(0, "内存检测进行中，按 F6 或 ESC 停止");
  status = MemTestRun(MemProgress, gMemWide);
  gMemRunning = FALSE;
  if (status == EFI_NOT_FOUND) {
    UiSetStatus(2, "未找到可测试的空闲内存区域");
  } else if (status == EFI_ABORTED) {
    UiSetStatus(2, "检测已停止");
  } else if (gResults.MemErrorCount) {
    UiStrPrintf(buf, sizeof(buf), "检测完成：发现 %u 个错误", gResults.MemErrorCount);
    UiSetStatus(3, buf);
  } else {
    UiStrPrintf(buf, sizeof(buf), "检测完成：%u 个图案全部通过，带宽 %u MB/s",
                gResults.MemPatternsRun, gResults.MemBandwidthMbs);
    UiSetStatus(1, buf);
  }
  UiRedraw();
}

VOID PageMemPoint(UINT32 row)
{
  if (row < gMemPatternCount) {
    gMemSel = row;
  }
}

BOOLEAN PageMemKey(EFI_INPUT_KEY key)
{
  GFX_RECT list = UiCardInner((GFX_RECT){ UiContentRect().X, 168,
                                         330, UiContentRect().H - 168 - 168 });
  UINT32 rows = VisibleRows(list);

  if (gMemRunning) {
    if (key.ScanCode == SCAN_F6 || key.ScanCode == SCAN_F5 ||
        key.UnicodeChar == 0x1B) {
      gStopRequested = TRUE;
      return TRUE;
    }
    return TRUE;  /* swallow everything else while running */
  }

  if (key.ScanCode == SCAN_UP) {
    if (gMemSel) {
      gMemSel--;
    }
    if (gMemSel < gMemTop) {
      gMemTop = gMemSel;
    }
    return TRUE;
  }
  if (key.ScanCode == SCAN_DOWN) {
    if (gMemSel + 1 < gMemPatternCount) {
      gMemSel++;
    }
    if (gMemSel >= gMemTop + rows) {
      gMemTop = gMemSel - rows + 1;
    }
    return TRUE;
  }
  if (key.UnicodeChar == ' ') {
    gMemPatterns[gMemSel].Enabled = gMemPatterns[gMemSel].Enabled ? FALSE : TRUE;
    return TRUE;
  }
  if (key.ScanCode == SCAN_F5) {
    StartMemoryTest();
    return TRUE;
  }
  if (key.ScanCode == SCAN_F7) {
    UINT32 i;
    for (i = 0; i < gMemPatternCount; i++) {
      gMemPatterns[i].Enabled = TRUE;
    }
    UiSetStatus(0, "已启用全部测试图案");
    return TRUE;
  }
  if (key.ScanCode == SCAN_F8) {
    gMemWide = gMemWide ? FALSE : TRUE;
    UiSetStatus(0, gMemWide ? "宽覆盖模式：最多测试 6 GB" : "标准覆盖模式：最多测试 2 GB");
    return TRUE;
  }
  /* Enter toggles the row like space: rows are check boxes, and a click on
     the already selected row arrives here.  The chip view keeps F9 / T. */
  if (key.UnicodeChar == '\r' || key.UnicodeChar == '\n') {
    gMemPatterns[gMemSel].Enabled = gMemPatterns[gMemSel].Enabled ? FALSE : TRUE;
    return TRUE;
  }
  if (key.ScanCode == SCAN_F9 || key.UnicodeChar == 't' || key.UnicodeChar == 'T') {
    PageDimmFocus(0);
    UiSwitchPage(PAGE_DIMM);
    UiSetStatus(0, "内存拓扑：↑↓ 选择插槽，Enter 查看颗粒视图");
    return TRUE;
  }
  return FALSE;
}
