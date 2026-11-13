/* page_disk.c - storage inventory, SMART health and read-only surface scan. */
#include "pages.h"
#include "disk.h"
#include "results.h"

static UI_LIST gDiskList;
static UINT32 gDiskPendingFocus = 0xFFFFFFFF;
static BOOLEAN gDiskRunning;
static UINT32  gDiskLivePermille;
static UINT32  gDiskLiveMbs;
static UINT32  gDiskScanning;

static VOID DiskScrollList(UINT32 first, UINT32 visible)
{
  (VOID)visible;
  gDiskList.Scroll = first;
  if (first < DiskCount()) {
    gDiskList.Sel = first;
  }
}

static VOID DrawDeviceList(GFX_RECT card)
{
  GFX_RECT inner = UiCardInner(card);
  UINT32 rows = inner.H / 52;
  UINT32 i;
  char buf[64];

  if (DiskCount() == 0) {
    GfxText(inner.X, inner.Y + 8, "未发现块设备", gTheme.Muted, 1);
    return;
  }
  for (i = 0; i < rows; i++) {
    UINT32 idx = i;
    DISK_DEV *d;
    GFX_RECT row;
    if (idx >= DiskCount()) {
      break;
    }
    d = DiskAt(idx);
    row = (GFX_RECT){ inner.X, inner.Y + i * 52, inner.W, 48 };
    UiHitRow(row, idx);
    if (idx == gDiskList.Sel) {
      GfxFillRound(row, 8, gTheme.PrimarySoft);
    } else if (i & 1) {
      GfxFillRound(row, 8, gTheme.PanelAlt);
    }
    UiIcon(3, row.X + 10, row.Y + 14, 18, idx == gDiskList.Sel ? gTheme.Primary : gTheme.SubText);
    {
      const char *name = d->Model[0] ? d->Model : d->PathText;
      GfxTextClipped(row.X + 38, row.Y + 8, row.W - 130, name, gTheme.Text, 1);
    }
    GfxFormatSize(buf, sizeof(buf), d->SizeBytes);
    UiStrPrintf(buf + GfxStrLen(buf), sizeof(buf) - GfxStrLen(buf), " · %s",
                DiskIfaceName(d->Iface));
    GfxTextClipped(row.X + 38, row.Y + 26, row.W - 110, buf, gTheme.SubText, 1);
    {
      GFX_RECT pill = { row.X + row.W - 74, row.Y + 14, 66, 18 };
      const char *text = d->HealthLevel == 3 ? "异常" : (d->HealthLevel == 2 ? "注意" :
                        (d->HealthLevel == 1 ? "正常" : "未知"));
      UiPill(pill, UiLevelColor(d->HealthLevel), text);
    }
  }
  UiScrollbarScroll((GFX_RECT){ card.X + card.W - 6, inner.Y, 4, inner.H },
                    gDiskList.Scroll, rows, DiskCount(), DiskScrollList);
  UiStrPrintf(buf, sizeof(buf), "%u 个设备", DiskCount());
  GfxTextRight(card.X + card.W - 16, card.Y + 14, buf, gTheme.SubText, 1);
}

static VOID SpeedChart(GFX_RECT r, DISK_DEV *d)
{
  UINT32 i;
  UINT32 bars = DISK_SPEED_BUCKETS;
  UINT32 bw;
  UINT32 peak = 0;
  char buf[48];
  GFX_RECT axis;

  GfxFillRound(r, 8, gTheme.PanelAlt);
  if (d->ProfileCount == 0) {
    GfxTextCenter(r.X, r.W, r.Y + r.H / 2 - 8, "尚未扫描", gTheme.Muted, 1);
    return;
  }
  bw = r.W / (bars ? bars : 1);
  if (bw == 0) {
    bw = 1;
  }
  GfxFillRect((GFX_RECT){ r.X + bars * bw, r.Y + 2, r.W - bars * bw, r.H - 4 }, gTheme.PanelAlt);
  {
    UINT32 full;
    UINT32 lane = r.H > 60 ? r.H - 34 : r.H / 2;
    for (i = 0; i < bars; i++) {
      if (d->SpeedProfile[i] > peak) {
        peak = d->SpeedProfile[i];
      }
    }
    if (peak == 0) {
      peak = 1;
    }
    /* round the ceiling up so the tallest bar never touches the label row */
    full = ((peak / 25) + 1) * 25;
    for (i = 0; i < bars; i++) {
      UINT32 h = lane * d->SpeedProfile[i] / full;
      if (h > lane) {
        h = lane;
      }
      GFX_RECT bar = { r.X + i * bw, r.Y + r.H - 12 - h, bw > 2 ? bw - 1 : 1, h };
      GfxFillRect(bar, d->SpeedProfile[i] * 3 < d->MaxMbs ? gTheme.Warn : gTheme.Chart1);
    }
  }
  axis = (GFX_RECT){ r.X, r.Y + r.H - 12, r.W, 1 };
  GfxFillRect(axis, gTheme.Border);
  UiStrPrintf(buf, sizeof(buf), "起点");
  GfxText(r.X + 4, r.Y + r.H - 10, buf, gTheme.Muted, 1);
  UiStrPrintf(buf, sizeof(buf), "终点");
  GfxTextRight(r.X + r.W - 6, r.Y + r.H - 10, buf, gTheme.Muted, 1);
  UiStrPrintf(buf, sizeof(buf), "%u MB/s", d->MaxMbs ? d->MaxMbs : peak);
  GfxTextRight(r.X + r.W - 10, r.Y + 6, buf, gTheme.SubText, 1);
}

static VOID KVRow(GFX_RECT *col, const char *label, const char *value, UINT32 color)
{
  UINT32 kw = GfxTextWidth(label, 1) + 12;
  GfxText(col->X, col->Y, label, gTheme.Muted, 1);
  GfxTextClipped(col->X + kw, col->Y, col->W > kw ? col->W - kw : 1, value, color, 1);
  col->Y += 22;
}

static VOID DrawDetail(GFX_RECT card)
{
  DISK_DEV *d = DiskAt(gDiskList.Sel);
  GFX_RECT inner = UiCardInner(card);
  char buf[128];
  char buf2[96];

  if (d == 0) {
    GfxText(inner.X, inner.Y + 8, "未选择设备", gTheme.Muted, 1);
    return;
  }
  UiStrPrintf(buf, sizeof(buf), "%s", d->Model[0] ? d->Model : L("块设备"));
  GfxTextClipped(inner.X, inner.Y, inner.W - 100, buf, gTheme.Text, 1);
  {
    GFX_RECT pill = { inner.X + inner.W - 78, inner.Y - 3, 78, 20 };
    UiPill(pill, UiLevelColor(d->HealthLevel),
           d->HealthLevel == 3 ? "扫描异常" : (d->HealthLevel == 2 ? "注意" :
           (d->HealthLevel == 1 ? "健康" : "未知")));
  }

  {
    GFX_RECT col = inner;
    col.Y += 28;
    GfxFormatSize(buf, sizeof(buf), d->SizeBytes);
    KVRow(&col, "容量", buf, gTheme.Text);
    UiStrPrintf(buf, sizeof(buf), "%s · %u 字节/扇区", DiskIfaceName(d->Iface), d->BlockSize);
    KVRow(&col, "接口", buf, gTheme.Text);
    KVRow(&col, "序列号", d->Serial[0] ? d->Serial : "n/a", gTheme.Text);
    KVRow(&col, "固件版本", d->Firmware[0] ? d->Firmware : "n/a", gTheme.Text);
    KVRow(&col, "设备属性", d->ReadOnly ? "只读" : "可写（支持擦除与覆写）", gTheme.Text);
    KVRow(&col, "健康状态", d->HealthText[0] ? d->HealthText : "无可用的 SMART 通道",
          d->HealthLevel >= 2 ? gTheme.Warn : gTheme.Text);
    if (d->BytesWritten) {
      GfxFormatSize(buf2, sizeof(buf2), d->BytesWritten);
    } else {
      UiStrPrintf(buf2, sizeof(buf2), "n/a");
    }
    KVRow(&col, "累计写入", buf2, gTheme.SubText);
    KVRow(&col, "设备路径", d->PathText, gTheme.Muted);
  }

  {
    GFX_RECT chart;
    UINT32 cy = inner.Y + 206;
    UiDivider((GFX_RECT){ inner.X, cy, inner.W, 1 });
    cy += 10;
    if (d->Scanned) {
      UiStrPrintf(buf, sizeof(buf), "均 %u / 低 %u / 高 %u MB/s · 读取错误 %u",
                  d->AvgMbs, d->MinMbs, d->MaxMbs, d->ScanErrors);
    } else if (gDiskRunning && gDiskScanning == gDiskList.Sel) {
      UiStrPrintf(buf, sizeof(buf), "扫描中 %u%% · %u MB/s", gDiskLivePermille / 10, gDiskLiveMbs);
    } else {
      UiStrPrintf(buf, sizeof(buf), "按 F5 开始只读顺序扫描，按 F9 扫描全部设备");
    }
    GfxTextClipped(inner.X, cy, inner.W, buf, gTheme.SubText, 1);
    cy += 22;
    chart = (GFX_RECT){ inner.X, cy, inner.W, inner.H - (cy - inner.Y) - 4 };
    if (chart.H > 40) {
      SpeedChart(chart, d);
    }
  }
}

VOID PageDiskFocus(UINT32 diskIndex)
{
  gDiskPendingFocus = diskIndex;
}

VOID PageDiskEnter(VOID)
{
  DiskEnumerate();
  gDiskList.Sel = 0;
  gDiskList.Scroll = 0;
  if (gDiskPendingFocus < DiskCount()) {
    gDiskList.Sel = gDiskPendingFocus;
    gDiskPendingFocus = 0xFFFFFFFF;
  }
  gDiskList.Count = DiskCount();
  gDiskList.Visible = (UiContentRect().H - 56) / 52;
  if (DiskCount() == 0) {
    UiSetStatus(2, "未发现可用的块设备");
  }
}

static BOOLEAN DiskProgress(DISK_DEV *disk, UINT32 permille, UINT32 mbs, UINT64 doneBytes)
{
  gDiskLivePermille = permille;
  gDiskLiveMbs = mbs;
  if (UiPump() || gStopRequested) {
    return FALSE;
  }
  PageDiskDraw(UiContentRect());
  GfxPresent();
  (VOID)disk;
  (VOID)doneBytes;
  return TRUE;
}

static VOID ScanSelected(VOID)
{
  DISK_DEV *d = DiskAt(gDiskList.Sel);
  EFI_STATUS status;
  char buf[128];
  if (d == 0) {
    return;
  }
  gDiskRunning = TRUE;
  gDiskScanning = gDiskList.Sel;
  gDiskLivePermille = 0;
  gDiskLiveMbs = 0;
  gStopRequested = FALSE;
  UiSetStatus(0, "只读扫描进行中，按 F6 停止");
  status = DiskSurfaceScan(d, 1024 * 1024, DiskProgress);
  gDiskRunning = FALSE;
  if (status == EFI_ABORTED) {
    UiSetStatus(2, "扫描已停止");
  } else if (d->ScanErrors) {
    UiStrPrintf(buf, sizeof(buf), "扫描完成：%u 个读取错误", d->ScanErrors);
    UiSetStatus(3, buf);
  } else {
    UiStrPrintf(buf, sizeof(buf), "扫描完成：平均 %u MB/s，无读取错误", d->AvgMbs);
    UiSetStatus(1, buf);
  }
  gResults.HealthDisk = d->HealthLevel;
  UiRedraw();
}

VOID PageDiskDraw(GFX_RECT content)
{
  UINT32 gap = 12;
  GFX_RECT listCard = { content.X, content.Y, 396, content.H };
  GFX_RECT detailCard = { content.X + 396 + gap, content.Y, content.W - 396 - gap, content.H };
  char buf[128];

  UiCard(listCard, "块设备", 0);
  DrawDeviceList(listCard);

  UiCard(detailCard, "设备详情与扫描", 0);
  DrawDetail(detailCard);

  (VOID)buf;
  if (gResults.DiskScannedDevices) {
    UiStrPrintf(buf, sizeof(buf), L("本次会话已扫描 %u 个设备 · 累计读取 "), gResults.DiskScannedDevices);
    GfxFormatSize(buf + GfxStrLen(buf), sizeof(buf) - GfxStrLen(buf), gResults.DiskScannedBytes);
  }
}

VOID PageDiskPoint(UINT32 row)
{
  if (row < DiskCount()) {
    gDiskList.Sel = row;
  }
}

BOOLEAN PageDiskKey(EFI_INPUT_KEY key)
{
  if (gDiskRunning) {
    if (key.ScanCode == SCAN_F6 || key.ScanCode == SCAN_ESC) {
      gStopRequested = TRUE;
    }
    return TRUE;
  }
  gDiskList.Count = DiskCount();
  gDiskList.Visible = (UiContentRect().H - 56) / 52;
  if (UiListKey(&gDiskList, key)) {
    UiListClamp(&gDiskList, gDiskList.Visible);
    return TRUE;
  }
  if (key.ScanCode == SCAN_F5) {
    ScanSelected();
    return TRUE;
  }
  if (key.ScanCode == SCAN_F9) {
    UINT32 i;
    for (i = 0; i < DiskCount(); i++) {
      gDiskList.Sel = i;
      ScanSelected();
      if (gStopRequested) {
        break;
      }
    }
    return TRUE;
  }
  if (key.UnicodeChar == '\r' || key.UnicodeChar == '\n') {
    DISK_DEV *d = DiskAt(gDiskList.Sel);
    if (d) {
      DiskRefreshHealth(d);
      UiSetStatus(1, "已刷新设备健康信息");
      return TRUE;
    }
  }
  return FALSE;
}
