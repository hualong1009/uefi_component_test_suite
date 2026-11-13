/* page_erase.c - multi-pass overwrite and read-back verification of a disk. */
#include "pages.h"
#include "disk.h"
#include "results.h"

#define ERASE_ALGO_COUNT 4
#define ERASE_LOG_MAX    6
#define ERASE_CHUNK      (1024 * 1024)

typedef struct {
  char Dev[44];
  char Algo[20];
  char Result[40];
} ERASE_REC;

static UI_LIST  gEraseList;
static UINT32   gEraseAlgo = 1;
static BOOLEAN  gEraseVerify = TRUE;
static BOOLEAN  gEraseFull = TRUE;

static volatile BOOLEAN gEraseRunning;
static UINT32   gErasePermille;
static UINT32   gEraseMbs;
static UINT32   gErasePass;
static UINT32   gErasePassCount;
static UINT32   gErasePhase;        /* 0 write, 1 verify */
static UINT64   gEraseWritten;

static ERASE_REC gEraseLog[ERASE_LOG_MAX];
static UINT32    gEraseLogCount;

static const char *gAlgoName[ERASE_ALGO_COUNT] = {
  "快速清零", "DoD 5220.22-M", "Gutmann 精简", "随机覆写"
};
static const char *gAlgoDetail[ERASE_ALGO_COUNT] = {
  "单遍写入 0x00，最快，适用于返修前快速清理",
  "0x00 → 0xFF → 随机数，三遍覆写，通用标准",
  "八遍固定与随机图案交替，覆盖旧式磁记录残留",
  "单遍加密随机数，速度接近清零，残留不可读"
};
static const UINT32 gAlgoPasses[ERASE_ALGO_COUNT] = { 1, 3, 8, 1 };
/* fill kinds: 0 zero, 1 ones, 2 random, 3 walk 0x55, 4 walk 0xAA, 5 0x33, 6 0xCC */
static const UINT8 gAlgoPattern[ERASE_ALGO_COUNT][8] = {
  { 0 },
  { 0, 1, 2 },
  { 0, 1, 2, 3, 4, 5, 6, 2 },
  { 2 }
};

static UINT64 EraseScopeBytes(DISK_DEV *d)
{
  UINT64 want = 256ull * 1024 * 1024;
  if (d == 0) {
    return 0;
  }
  if (gEraseFull || want > d->SizeBytes) {
    return d->SizeBytes;
  }
  return want;
}

static VOID EraseScrollList(UINT32 first, UINT32 visible)
{
  (VOID)visible;
  gEraseList.Scroll = first;
  if (first < DiskCount()) {
    gEraseList.Sel = first;
  }
}

static VOID EraseFill(UINT8 *buf, UINT32 len, UINT32 kind, UINT64 offset)
{
  UINT32 i;
  UINT64 seed = offset / 4096 + 0x9E3779B97F4A7C15ull;

  switch (kind) {
  case 0:
    gApp.BS->SetMem(buf, len, 0x00);
    return;
  case 1:
    gApp.BS->SetMem(buf, len, 0xFF);
    return;
  case 3:
    gApp.BS->SetMem(buf, len, 0x55);
    return;
  case 4:
    gApp.BS->SetMem(buf, len, 0xAA);
    return;
  case 5:
    gApp.BS->SetMem(buf, len, 0x33);
    return;
  case 6:
    gApp.BS->SetMem(buf, len, 0xCC);
    return;
  default:
    break;
  }
  for (i = 0; i < len; i += 8) {
    UINT32 j;
    seed ^= seed << 13;
    seed ^= seed >> 7;
    seed ^= seed << 17;
    for (j = 0; j < 8 && i + j < len; j++) {
      buf[i + j] = (UINT8)(seed >> (j * 8));
    }
  }
}

static BOOLEAN EraseTick(UINT32 permille, UINT32 mbs)
{
  gErasePermille = permille;
  gEraseMbs = mbs;
  if (UiPump() || gStopRequested) {
    return FALSE;
  }
  UiDrawEverything();
  return TRUE;
}

static EFI_STATUS EraseOnePass(DISK_DEV *d, UINT64 bytes, UINT32 kind, UINT8 *buf, UINT64 *badBytes)
{
  EFI_STATUS status = EFI_SUCCESS;
  UINT64 done = 0;
  UINT64 started = UiNowMs();
  UINT32 lastMs = 0;

  while (done < bytes) {
    EFI_LBA lba = (EFI_LBA)(done / d->BlockSize);
    UINTN len = (UINTN)((bytes - done) < ERASE_CHUNK ? (bytes - done) : ERASE_CHUNK);
    UINT32 now;
    len = (len / d->BlockSize) * d->BlockSize;
    if (len == 0) {
      break;
    }
    EraseFill(buf, (UINT32)len, kind, done);
    status = d->Io->WriteBlocks(d->Io, d->Io->Media->MediaId, lba, len, buf);
    if (EFI_ERROR(status)) {
      break;
    }
    done += len;
    gEraseWritten = done;
    now = (UINT32)(UiNowMs() - started);
    if ((now - lastMs) > 120 || done == bytes) {
      UINT32 mbs = now ? (UINT32)((done / 1024 / 1024) * 1000 / now) : 0;
      lastMs = now;
      if (!EraseTick((UINT32)((done * 1000) / bytes), mbs)) {
        status = EFI_ABORTED;
        break;
      }
    }
  }
  d->Io->FlushBlocks(d->Io);
  (VOID)badBytes;
  return status;
}

static EFI_STATUS EraseVerifyPass(DISK_DEV *d, UINT64 bytes, UINT32 kind, UINT8 *buf,
                                  UINT8 *rbuf, UINT64 *badOffset, UINT32 *badCount)
{
  EFI_STATUS status = EFI_SUCCESS;
  UINT64 done = 0;
  UINT64 started = UiNowMs();
  UINT32 lastMs = 0;

  *badOffset = 0;
  *badCount = 0;
  while (done < bytes) {
    EFI_LBA lba = (EFI_LBA)(done / d->BlockSize);
    UINTN len = (UINTN)((bytes - done) < ERASE_CHUNK ? (bytes - done) : ERASE_CHUNK);
    UINT32 now;
    len = (len / d->BlockSize) * d->BlockSize;
    if (len == 0) {
      break;
    }
    gApp.BS->SetMem(rbuf, (UINTN)len, 0);
    status = d->Io->ReadBlocks(d->Io, d->Io->Media->MediaId, lba, len, rbuf);
    if (EFI_ERROR(status)) {
      break;
    }
    EraseFill(buf, (UINT32)len, kind, done);
    if (memcmp(buf, rbuf, (UINTN)len) != 0) {
      UINTN i;
      for (i = 0; i < len; i++) {
        if (buf[i] != rbuf[i]) {
          if (*badCount == 0) {
            *badOffset = done + i;
          }
          (*badCount)++;
          break;
        }
      }
    }
    done += len;
    gEraseWritten = done;
    now = (UINT32)(UiNowMs() - started);
    if ((now - lastMs) > 120 || done == bytes) {
      UINT32 mbs = now ? (UINT32)((done / 1024 / 1024) * 1000 / now) : 0;
      lastMs = now;
      if (!EraseTick((UINT32)((done * 1000) / bytes), mbs)) {
        status = EFI_ABORTED;
        break;
      }
    }
  }
  return status;
}

static VOID EraseLogAdd(const char *dev, const char *algo, const char *result)
{
  UINT32 i;
  if (gEraseLogCount < ERASE_LOG_MAX) {
    i = gEraseLogCount++;
  } else {
    for (i = 1; i < ERASE_LOG_MAX; i++) {
      gEraseLog[i - 1] = gEraseLog[i];
    }
    i = ERASE_LOG_MAX - 1;
  }
  UiStrPrintf(gEraseLog[i].Dev, sizeof(gEraseLog[i].Dev), "%s", dev);
  UiStrPrintf(gEraseLog[i].Algo, sizeof(gEraseLog[i].Algo), "%s", algo);
  UiStrPrintf(gEraseLog[i].Result, sizeof(gEraseLog[i].Result), "%s", result);
}

static VOID RunErase(VOID)
{
  DISK_DEV *d = DiskAt(gEraseList.Sel);
  UINT8 *wbuf = 0;
  UINT8 *rbuf = 0;
  UINT64 bytes;
  UINT64 started;
  UINT64 badOffset = 0;
  UINT32 badCount = 0;
  UINT32 pass;
  UINT32 passes = gAlgoPasses[gEraseAlgo];
  EFI_STATUS status = EFI_SUCCESS;
  char msg[160];
  char result[40];
  char algo[20];

  if (d == 0) {
    UiSetStatus(2, "请先选择目标设备");
    return;
  }
  if (d->ReadOnly) {
    UiSetStatus(3, "该设备为只读，无法擦除");
    return;
  }
  bytes = EraseScopeBytes(d);
  if (bytes == 0) {
    UiSetStatus(2, "设备容量为 0");
    return;
  }
  UiStrPrintf(algo, sizeof(algo), "%s", gAlgoName[gEraseAlgo]);
  UiStrPrintf(msg, sizeof(msg),
              "目标 %s（%lu MB）\n算法 %s，共 %u 遍%s\n\n"
              "%s擦除将覆盖分区表与全部文件，且无法恢复。",
              d->Model[0] ? d->Model : d->PathText, (unsigned long)(bytes / 1024 / 1024),
              algo, passes, gEraseVerify ? L("，含回读校验") : "",
              d->BootVolume ? L("注意：该设备是当前启动盘，擦除后系统将无法启动。\n\n") : "");
  if (!UiConfirm("擦除磁盘", msg, "ERASE")) {
    UiSetStatus(0, "已取消擦除");
    return;
  }
  if (EFI_ERROR(gApp.BS->AllocatePool(EfiBootServicesData, ERASE_CHUNK, (VOID **)&wbuf)) ||
      EFI_ERROR(gApp.BS->AllocatePool(EfiBootServicesData, ERASE_CHUNK, (VOID **)&rbuf))) {
    if (wbuf) {
      gApp.BS->FreePool(wbuf);
    }
    UiSetStatus(3, "内存不足，无法开始擦除");
    return;
  }
  gStopRequested = FALSE;
  gEraseRunning = TRUE;
  gErasePermille = 0;
  gEraseMbs = 0;
  gEraseWritten = 0;
  gErasePassCount = passes * (gEraseVerify ? 2 : 1);
  started = UiNowMs();
  for (pass = 0; pass < passes && status == EFI_SUCCESS; pass++) {
    gErasePass = pass + 1;
    gErasePhase = 0;
    UiSetStatus(0, "擦除进行中，按 ESC 或 F6 中止");
    status = EraseOnePass(d, bytes, gAlgoPattern[gEraseAlgo][pass], wbuf, 0);
    if (EFI_ERROR(status)) {
      break;
    }
    if (gEraseVerify) {
      gErasePhase = 1;
      gErasePermille = 0;
      status = EraseVerifyPass(d, bytes, gAlgoPattern[gEraseAlgo][pass], wbuf, rbuf,
                               &badOffset, &badCount);
      if (EFI_ERROR(status)) {
        break;
      }
      if (badCount) {
        status = EFI_DEVICE_ERROR;
        break;
      }
    }
  }
  gEraseRunning = FALSE;
  gApp.BS->FreePool(wbuf);
  gApp.BS->FreePool(rbuf);

  if (status == EFI_ABORTED) {
    UiStrPrintf(result, sizeof(result), "中止于第 %u 遍", gErasePass);
    UiSetStatus(2, "擦除已中止，磁盘内容可能处于不完整状态");
  } else if (badCount) {
    UiStrPrintf(result, sizeof(result), "校验失败 %u 块", badCount);
    UiStrPrintf(msg, sizeof(msg), "回读校验失败：首个不一致位置 0x%lx",
                (unsigned long)badOffset);
    UiSetStatus(3, msg);
  } else if (EFI_ERROR(status)) {
    UiStrPrintf(result, sizeof(result), "失败 0x%lx", (unsigned long)status);
    UiSetStatus(3, "擦除过程中发生 I/O 错误");
  } else {
    UINT64 ms = UiNowMs() - started;
    UiStrPrintf(result, sizeof(result), "完成 %lu 秒", (unsigned long)(ms / 1000));
    UiStrPrintf(msg, sizeof(msg), "擦除完成：%u 遍 · %lu MB · 用时 %lu 秒",
                passes, (unsigned long)(bytes / 1024 / 1024), (unsigned long)(ms / 1000));
    UiSetStatus(1, msg);
    gResults.EraseCount++;
    UiStrPrintf(gResults.EraseLastTarget, sizeof(gResults.EraseLastTarget), "%s · %s",
                d->Model[0] ? d->Model : d->PathText, algo);
    gResults.HealthDisk = 1;
  }
  EraseLogAdd(d->Model[0] ? d->Model : d->PathText, algo, result);
  UiRedraw();
}

/* ------------------------------------------------------------------ drawing */
static VOID DrawEraseTargets(GFX_RECT card)
{
  GFX_RECT inner = UiCardInner(card);
  UINT32 rows = inner.H / 52;
  UINT32 i;
  char buf[64];

  if (DiskCount() == 0) {
    GfxText(inner.X, inner.Y + 8, "未发现块设备", gTheme.Muted, 1);
    return;
  }
  for (i = 0; i < rows && i < DiskCount(); i++) {
    DISK_DEV *d = DiskAt(i);
    GFX_RECT row = { inner.X, inner.Y + i * 52, inner.W, 48 };
    UiHitRowBox(row, i);
    if (i == gEraseList.Sel) {
      GfxFillRound(row, 8, gTheme.CritSoft);
    } else if (i & 1) {
      GfxFillRound(row, 8, gTheme.PanelAlt);
    }
    UiIcon(3, row.X + 10, row.Y + 14, 18, i == gEraseList.Sel ? gTheme.Crit : gTheme.SubText);
    GfxTextClipped(row.X + 38, row.Y + 8, row.W - 120, d->Model[0] ? d->Model : d->PathText,
                   gTheme.Text, 1);
    GfxFormatSize(buf, sizeof(buf), d->SizeBytes);
    UiStrPrintf(buf + GfxStrLen(buf), sizeof(buf) - GfxStrLen(buf), " · %s",
                DiskIfaceName(d->Iface));
    GfxTextClipped(row.X + 38, row.Y + 26, row.W - 110, buf, gTheme.SubText, 1);
    if (d->ReadOnly || d->BootVolume) {
      const char *tag = d->ReadOnly ? "只读" : "启动盘";
      GFX_RECT pill;
      pill.W = GfxTextWidth(tag, 1) + 22;
      pill.H = 18;
      pill.X = row.X + row.W - pill.W - 10;
      pill.Y = row.Y + 14;
      UiPill(pill, d->ReadOnly ? gTheme.Muted : gTheme.Warn, tag);
    }
  }
  UiScrollbarScroll((GFX_RECT){ card.X + card.W - 6, inner.Y, 4, inner.H },
                    gEraseList.Scroll, rows, DiskCount(), EraseScrollList);
  UiStrPrintf(buf, sizeof(buf), "%u 个设备", DiskCount());
  GfxTextRight(card.X + card.W - 16, card.Y + 14, buf, gTheme.SubText, 1);
}

static VOID DrawAlgoCard(GFX_RECT card)
{
  GFX_RECT inner = UiCardInner(card);
  UINT32 i;
  char buf[96];

  for (i = 0; i < ERASE_ALGO_COUNT; i++) {
    UINT32 y = inner.Y + i * 34;
    BOOLEAN sel = (i == gEraseAlgo);
    if (sel) {
      GfxFillRound((GFX_RECT){ inner.X - 6, y - 4, inner.W + 12, 32 }, 6, gTheme.PrimarySoft);
    }
    GfxCircle(inner.X + 6, y + 7, 5, sel ? gTheme.Primary : gTheme.Border);
    if (sel) {
      GfxFillCircle(inner.X + 6, y + 7, 2, gTheme.Primary);
    }
    GfxText(inner.X + 20, y, gAlgoName[i], sel ? gTheme.Text : gTheme.SubText, 1);
    UiStrPrintf(buf, sizeof(buf), "%u 遍", gAlgoPasses[i]);
    GfxTextRight(inner.X + inner.W, y, buf, gTheme.Muted, 1);
    if (sel) {
      GfxTextClipped(inner.X + 20, y + 16, inner.W - 72, gAlgoDetail[i], gTheme.Muted, 1);
    }
  }
  UiStrPrintf(buf, sizeof(buf), "范围：%s · 回读校验：%s",
              gEraseFull ? L("整盘") : L("前 256 MB"), gEraseVerify ? L("开启") : L("关闭"));
  GfxText(inner.X, inner.Y + 4 * 34 + 4, buf, gTheme.SubText, 1);
  GfxText(inner.X, inner.Y + 4 * 34 + 22, "R 切换范围 · V 切换校验 · Enter 开始擦除",
          gTheme.Muted, 1);
}

static VOID DrawProgress(GFX_RECT card)
{
  GFX_RECT inner = UiCardInner(card);
  DISK_DEV *d = DiskAt(gEraseList.Sel);
  char buf[128];
  UINT64 bytes = EraseScopeBytes(d);

  if (!gEraseRunning) {
    UiStrPrintf(buf, sizeof(buf), "目标：%s（%lu MB）",
                d ? (d->Model[0] ? d->Model : d->PathText) : L("未选择"),
                (unsigned long)(bytes / 1024 / 1024));
    GfxTextClipped(inner.X, inner.Y, inner.W, buf, gTheme.Text, 1);
    GfxTextClipped(inner.X, inner.Y + 22, inner.W,
                   "擦除会覆盖分区表与全部数据，工具在开始前要求二次确认。", gTheme.Muted, 1);
  } else {
    UiStrPrintf(buf, sizeof(buf), "%s · 第 %u/%u 步 · %u MB/s",
                gErasePhase ? L("回读校验") : L("写入覆写"), gErasePass, gErasePassCount, gEraseMbs);
    GfxTextClipped(inner.X, inner.Y, inner.W, buf, gTheme.Text, 1);
    UiProgressBar((GFX_RECT){ inner.X, inner.Y + 22, inner.W, 12 }, gErasePermille,
                  gErasePhase ? gTheme.Warn : gTheme.Primary, 0);
    GfxFormatSize(buf, sizeof(buf), gEraseWritten);
    UiStrPrintf(buf + GfxStrLen(buf), sizeof(buf) - GfxStrLen(buf), " / ");
    GfxFormatSize(buf + GfxStrLen(buf), sizeof(buf) - GfxStrLen(buf), bytes);
    UiStrPrintf(buf + GfxStrLen(buf), sizeof(buf) - GfxStrLen(buf), " · %u%%", gErasePermille / 10);
    GfxText(inner.X, inner.Y + 42, buf, gTheme.SubText, 1);
  }

  /* history */
  {
    UINT32 y = inner.Y + (gEraseRunning ? 70 : 44);
    UINT32 i;
    UiDivider((GFX_RECT){ inner.X, y, inner.W, 1 });
    y += 8;
    GfxText(inner.X, y, "擦除记录", gTheme.SubText, 1);
    y += 20;
    if (gEraseLogCount == 0) {
      GfxText(inner.X, y, "本次会话尚无擦除记录", gTheme.Muted, 1);
    }
    for (i = 0; i < gEraseLogCount && y + 18 < inner.Y + inner.H; i++) {
      UiStrPrintf(buf, sizeof(buf), "%s · %s", gEraseLog[i].Dev, gEraseLog[i].Algo);
      GfxTextClipped(inner.X, y, inner.W - 110, buf, gTheme.Text, 1);
      GfxTextRight(inner.X + inner.W, y, gEraseLog[i].Result, gTheme.SubText, 1);
      y += 18;
    }
  }
}

VOID PageEraseEnter(VOID)
{
  DiskEnumerate();
  gEraseList.Sel = 0;
  gEraseList.Scroll = 0;
  gEraseList.Count = DiskCount();
  gEraseList.Visible = 4;
  if (DiskCount() == 0) {
    UiSetStatus(2, "未发现可擦除的块设备");
  } else {
    UiSetStatus(0, "选择设备后按 Enter 开始，擦除前会二次确认");
  }
}

VOID PageEraseDraw(GFX_RECT content)
{
  UINT32 gap = 12;
  GFX_RECT left = { content.X, content.Y, 360, content.H };
  GFX_RECT rightX = { content.X + 360 + gap, content.Y, content.W - 360 - gap, content.H };
  GFX_RECT algoCard = { rightX.X, rightX.Y, rightX.W, 238 };
  GFX_RECT warnCard = { left.X, left.Y, left.W, 96 };
  GFX_RECT progCard = { rightX.X, rightX.Y + 238 + gap, rightX.W, rightX.H - 238 - gap };
  char buf[64];

  {
    UINT32 n = DiskCount();
    UINT32 h = 120 + n * 62;
    if (h > left.H - 108) {
      h = left.H - 108;
    }
    if (h < 200) {
      h = 200;
    }
    left.H = h;
  }
  UiCard(left, "目标设备", 0);
  DrawEraseTargets(left);
  warnCard.Y = left.Y + left.H + gap;

  UiCard(warnCard, "安全提示", 0);
  UiWrapText(UiCardInner(warnCard),
             "擦除属于破坏性操作，会清除分区表、引导记录与全部文件。"
             "工具仅对整盘设备提供擦除，并在写入后回读校验。", gTheme.SubText, 1);

  UiCard(algoCard, "擦除算法", 0);
  DrawAlgoCard(algoCard);

  UiStrPrintf(buf, sizeof(buf), "已完成 %u 次擦除", gResults.EraseCount);
  UiCard(progCard, "进度与记录", buf);
  DrawProgress(progCard);
}

VOID PageErasePoint(UINT32 row)
{
  if (row < DiskCount()) {
    gEraseList.Sel = row;
  }
}

BOOLEAN PageEraseKey(EFI_INPUT_KEY key)
{
  if (gEraseRunning) {
    if (key.ScanCode == SCAN_ESC || key.ScanCode == SCAN_F6) {
      gStopRequested = TRUE;
    }
    return TRUE;
  }
  gEraseList.Count = DiskCount();
  gEraseList.Visible = 4;
  if (UiListKey(&gEraseList, key)) {
    UiListClamp(&gEraseList, gEraseList.Visible);
    return TRUE;
  }
  /* arrows switch pages now, so the algorithm cycles with the space bar */
  if (key.UnicodeChar == ' ') {
    gEraseAlgo = (gEraseAlgo + 1) % ERASE_ALGO_COUNT;
    return TRUE;
  }
  if (key.UnicodeChar == 'r' || key.UnicodeChar == 'R') {
    gEraseFull = gEraseFull ? FALSE : TRUE;
    return TRUE;
  }
  if (key.UnicodeChar == 'v' || key.UnicodeChar == 'V') {
    gEraseVerify = gEraseVerify ? FALSE : TRUE;
    return TRUE;
  }
  if (key.UnicodeChar == '\r' || key.UnicodeChar == '\n') {
    RunErase();
    return TRUE;
  }
  return FALSE;
}
