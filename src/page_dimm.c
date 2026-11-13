/* page_dimm.c - memory slot topology and per DRAM device (chip) view.

   SMBIOS type 17 gives the module layout (bus widths, rank count, banks,
   part number) but never the package of every DRAM device, so the picture is
   derived: dataWidth / 8 devices per rank, one side per rank. Errors measured
   by the memory test are mapped back to a device through the failing data bit,
   which is the granularity a per-device checker reports.
*/
#include "pages.h"
#include "results.h"
#include "debug.h"

#define PCB_GREEN      GFX_RGB(0x1E, 0x69, 0x3B)
#define PCB_GREEN_DARK GFX_RGB(0x15, 0x4E, 0x2B)
#define CHIP_DARK      GFX_RGB(0x18, 0x1C, 0x22)
#define CHIP_EDGE      GFX_RGB(0x3A, 0x42, 0x4C)
#define CHIP_TEXT      GFX_RGB(0xE8, 0xEE, 0xF6)
#define CHIP_BAD       GFX_RGB(0x8C, 0x1D, 0x1D)
#define GOLD           GFX_RGB(0xD3, 0xA0, 0x22)
#define GOLD_DARK      GFX_RGB(0xA8, 0x7A, 0x12)
#define RCD_BLUE       GFX_RGB(0x25, 0x63, 0xEB)
#define PMIC_ORANGE    GFX_RGB(0xE8, 0x8B, 0x0C)
#define SPD_PURPLE     GFX_RGB(0x7C, 0x3A, 0xED)

static UINT32  gSel;
static BOOLEAN gDetail;

/* --------------------------------------------------------------- helpers */
static UINT32 SlotErrorCount(UINT32 dimm)
{
  UINT32 i;
  UINT32 total;
  if (dimm >= 16) {
    return 0;
  }
  total = gResults.MemDimmErrors[dimm];
  if (total) {
    return total;   /* every per-device hit is also counted here */
  }
  for (i = 0; i < 32; i++) {
    total += gResults.MemDeviceErrors[dimm][i];
  }
  return total;
}

static UINT32 DeviceErrorCount(UINT32 dimm, UINT32 device)
{
  if (dimm >= 16 || device >= 32) {
    return 0;
  }
  return gResults.MemDeviceErrors[dimm][device];
}

static UINT32 LevelOfSlot(UINT32 dimm)
{
  const DIMM_INFO *d = &gApp.Sys.Dimms[dimm];
  if (!d->Present) {
    return 0;
  }
  if (dimm < 16 && (gResults.MemDimmErrors[dimm] || gResults.MemDeviceMapped)) {
    UINT32 i;
    for (i = 0; i < 32; i++) {
      if (gResults.MemDeviceErrors[dimm][i]) {
        return 3;
      }
    }
  }
  if (dimm < 16 && gResults.MemDimmErrors[dimm]) {
    return 3;
  }
  return 1;
}

static const char *SlotStateText(UINT32 dimm)
{
  switch (LevelOfSlot(dimm)) {
  case 3: return "异常";
  case 1: return "健康";
  default: return "空";
  }
}

/* --------------------------------------------------------- module drawing */
static VOID DrawDevice(UINT32 x, UINT32 y, UINT32 w, UINT32 h, UINT32 index, UINT32 errors)
{
  GFX_RECT chip = { x, y, w, h };
  char label[12];

  GfxFillRound(chip, 2, errors ? CHIP_BAD : CHIP_DARK);
  GfxRoundOutline(chip, 2, errors ? gTheme.Crit : CHIP_EDGE);
  /* pin 1 marker */
  GfxFillCircle(x + w / 2, y + 5, 2, errors ? gTheme.CritSoft : GFX_RGB(0x5B, 0x66, 0x74));
  UiStrPrintf(label, sizeof(label), "D%u", index);
  if (w >= 34) {
    GfxTextCenter(x, w, y + h / 2 - 6, label, CHIP_TEXT, 1);
  } else {
    GfxTextCenter(x, w, y + h / 2 - 6, label + 1, CHIP_TEXT, 1);
  }
  if (errors) {
    UiStrPrintf(label, sizeof(label), "x%u", errors);
    if (h > 34) {
      GfxTextCenter(x, w, y + h / 2 + 10, label, GFX_RGB(0xFF, 0xD9, 0xD9), 1);
    }
  }
}

/* 0 both sides, 1 front only, 2 back only - 'B' on the detail view cycles it
   so a dual-rank module can be looked at one side at a time at full height */
static UINT8 gSideView;
static UINT8 gSideViewDimm;
/* the channel view and the per-module error list both scroll with the selected
   slot: a 2-socket platform exposes 16 channels / 32 slots, and hiding the
   rest behind "N more channels" made installed modules impossible to reach */
static UINT32 gChannelTop;
static UINT32 gHeatTop;

static UINT32 ChannelRowCount(VOID)
{
  UINT32 i, j, rows = 0;

  for (i = 0; i < gApp.Sys.DimmCount && i < SYS_MAX_DIMMS; i++) {
    BOOLEAN seen = FALSE;
    for (j = 0; j < i; j++) {
      if (gApp.Sys.Dimms[j].Socket == gApp.Sys.Dimms[i].Socket &&
          gApp.Sys.Dimms[j].Channel == gApp.Sys.Dimms[i].Channel) {
        seen = TRUE;
        break;
      }
    }
    if (!seen) {
      rows++;
    }
  }
  return rows;
}

static UINT32 ChannelRowStart(UINT32 row)
{
  UINT32 i, j, cur = 0;

  for (i = 0; i < gApp.Sys.DimmCount && i < SYS_MAX_DIMMS; i++) {
    BOOLEAN seen = FALSE;
    for (j = 0; j < i; j++) {
      if (gApp.Sys.Dimms[j].Socket == gApp.Sys.Dimms[i].Socket &&
          gApp.Sys.Dimms[j].Channel == gApp.Sys.Dimms[i].Channel) {
        seen = TRUE;
        break;
      }
    }
    if (seen) {
      continue;
    }
    if (cur == row) {
      return i;
    }
    cur++;
  }
  return 0;
}

static UINT32 ChannelRowOfDimm(UINT32 dimm)
{
  UINT32 i, j, row = 0;

  for (i = 0; i < gApp.Sys.DimmCount && i < SYS_MAX_DIMMS; i++) {
    BOOLEAN seen = FALSE;
    for (j = 0; j < i; j++) {
      if (gApp.Sys.Dimms[j].Socket == gApp.Sys.Dimms[i].Socket &&
          gApp.Sys.Dimms[j].Channel == gApp.Sys.Dimms[i].Channel) {
        seen = TRUE;
        break;
      }
    }
    if (seen) {
      continue;
    }
    for (j = i; j < gApp.Sys.DimmCount; j++) {
      if (gApp.Sys.Dimms[j].Socket == gApp.Sys.Dimms[i].Socket &&
          gApp.Sys.Dimms[j].Channel == gApp.Sys.Dimms[i].Channel) {
        if (j == dimm) {
          return row;
        }
      }
    }
    row++;
  }
  return 0;
}

static UINT32 PresentDimmAt(UINT32 which);

/* pointer scrolling: the channel view follows the selection, so dragging the bar
   moves the selected slot as well - the card that lands on top gets focus */
static VOID DimmScrollChannels(UINT32 first, UINT32 visible)
{
  UINT32 slot;

  (VOID)visible;
  gChannelTop = first;
  slot = PresentDimmAt(first);
  if (slot != 0xFFFFFFFFu) {
    gSel = slot;
  }
}

static VOID DimmScrollErrors(UINT32 first, UINT32 visible)
{
  (VOID)visible;
  gHeatTop = first;
}

static UINT32 PresentDimmAt(UINT32 which)
{
  return SysPresentDimm(&gApp.Sys, which);
}

static UINT32 PresentDimmRowOf(UINT32 dimm)
{
  UINT32 i, n = 0;

  for (i = 0; i < gApp.Sys.DimmCount && i < SYS_MAX_DIMMS; i++) {
    if (!gApp.Sys.Dimms[i].Present) {
      continue;
    }
    if (i == dimm) {
      return n;
    }
    n++;
  }
  return 0;
}

static UINT32 SideViewFor(UINT32 dimm)
{
  DIMM_GEO geo;

  if (gSideViewDimm != dimm) {
    gSideViewDimm = (UINT8)dimm;
    gSideView = 0;
  }
  SysDimmGeometry(&gApp.Sys.Dimms[dimm], &geo);
  if (!geo.BackDevices && gSideView == 2) {
    gSideView = 0;
  }
  return gSideView;
}

/* front / back side of one module: PCB, DRAM packages and support chips */
static VOID DrawSide(GFX_RECT r, UINT32 dimm, BOOLEAN back)
{
  const DIMM_INFO *d = &gApp.Sys.Dimms[dimm];
  DIMM_GEO geo;
  UINT32 count;
  UINT32 first;
  UINT32 i;
  UINT32 support = 0;
  char buf[64];

  SysDimmGeometry(d, &geo);
  count = back ? geo.BackDevices : geo.FrontDevices;
  first = back ? geo.FrontDevices : 0;
  if (!back && geo.HasRcd) {
    support = 46;
  }
  if (back && geo.HasPmic) {
    support = 46;
  }

  /* PCB */
  GfxFillRound(r, 5, PCB_GREEN);
  GfxFillRound((GFX_RECT){ r.X, r.Y + r.H - 3, r.W, 3 }, 2, PCB_GREEN_DARK);
  GfxFillRound((GFX_RECT){ r.X, r.Y, r.W, 3 }, 2, GFX_RGB(0x2A, 0x7C, 0x49));

  if (count == 0) {
    GfxTextCenter(r.X, r.W, r.Y + r.H / 2 - 8, "此面无颗粒", GFX_RGB(0xBF, 0xD8, 0xC6), 1);
  } else {
    UINT32 margin = 14;
    UINT32 gap = 18;
    UINT32 chipW;
    UINT32 chipH = r.H - 64;
    UINT32 y;
    UINT32 avail = r.W - margin * 2 - support;
    UINT32 total;

    if (chipH > 104) {
      chipH = 104;
    }
    if (chipH < 26) {
      chipH = 26;
    }
    y = r.Y + 12 + (r.H - 34 - chipH) / 2;
    chipW = avail / (count + (gap * (count - 1)) / 30);
    if (chipW > 54) {
      chipW = 54;
    }
    if (chipW < 22) {
      chipW = 22;
    }
    total = count * chipW + (count - 1) * gap;
    if (total + support > r.W - margin * 2) {
      gap = count > 1 ? (r.W - margin * 2 - support - count * chipW) / (count - 1) : 0;
      gap = gap > 20 ? 20 : gap;
      total = count * chipW + (count - 1) * gap;
    }
    {
      UINT32 startX = r.X + (r.W - total - support) / 2;
      UINT32 mid = startX + total / 2;
      UINT32 half = count / 2;
      UINT32 x = startX;
      UINT32 gapMiddle = support ? support + 12 : 0;

      for (i = 0; i < count; i++) {
        if (support && i == half) {
          x += gapMiddle;
        }
        DrawDevice(x, y, chipW, chipH, first + i + 1, DeviceErrorCount(dimm, first + i));
        x += chipW + gap;
      }
      /* support chips sit between the two halves */
      if (support && !back) {
        GFX_RECT rcd = { mid - 22, y + (chipH - 28) / 2, 44, 28 };
        GfxFillRound(rcd, 3, RCD_BLUE);
        GfxRoundOutline(rcd, 3, GFX_RGB(0x1B, 0x4A, 0xB8));
        GfxTextCenter(rcd.X, rcd.W, rcd.Y + 6, "RCD", GFX_RGB(0xEE, 0xF3, 0xFF), 1);
      } else if (support && back) {
        GFX_RECT pmic = { mid - 24, y + 2, 48, 22 };
        GFX_RECT spd = { mid - 17, y + 30, 34, 16 };
        GfxFillRound(pmic, 3, PMIC_ORANGE);
        GfxTextCenter(pmic.X, pmic.W, pmic.Y + 3, "PMIC", GFX_RGB(0x3B, 0x22, 0x00), 1);
        GfxFillRound(spd, 3, SPD_PURPLE);
        GfxTextCenter(spd.X, spd.W, spd.Y + 1, "SPD", GFX_RGB(0xF3, 0xEC, 0xFF), 1);
      }
    }
  }

  /* silk printing: side / rank label and a pin-1 triangle */
  {
    char silk[32];
    UINT32 sx = r.X + 10;
    UINT32 sy = r.Y + 5;
    UiStrPrintf(silk, sizeof(silk), "%s %s", back ? "R1" : "R0",
                geo.HasRcd ? "RDIMM" : (geo.HasPmic ? "DDR5" : "UDIMM"));
    GfxTextClipped(sx, sy, r.W / 2, silk, GFX_RGB(0xC9, 0xE3, 0xD2), 1);
    /* pin 1 marker just above the fingers, left edge */
    {
      UINT32 ty = r.Y + r.H - 28;
      UINT32 tx = r.X + 6;
      GfxFillRect((GFX_RECT){ tx, ty, 10, 2 }, GFX_RGB(0xD8, 0xEC, 0xE0));
      GfxFillRect((GFX_RECT){ tx, ty + 2, 7, 2 }, GFX_RGB(0xD8, 0xEC, 0xE0));
      GfxFillRect((GFX_RECT){ tx, ty + 4, 4, 2 }, GFX_RGB(0xD8, 0xEC, 0xE0));
    }
  }

  /* gold edge connector */
  {
    UINT32 fy = r.Y + r.H - 22;
    UINT32 w = r.W - 24;
    UINT32 x = r.X + 12;
    GfxFillRect((GFX_RECT){ x, fy, w, 12 }, GOLD);
    for (i = 0; i < w; i += 4) {
      GfxVLine(x + i, fy, fy + 12, GOLD_DARK);
    }
    /* key notch (DDR4/DDR5 position is not reported, keep it off centre) */
    GfxFillRect((GFX_RECT){ r.X + r.W / 2 - 4, fy, 9, 12 }, PCB_GREEN);
    GfxHLine(r.X + 12, r.X + 12 + w, fy, GOLD_DARK);
  }

  (VOID)buf;
}

/* ------------------------------------------------------------- page views */
static VOID DrawSummary(GFX_RECT card)
{
  GFX_RECT inner = UiCardInner(card);
  UINT32 total = gApp.Sys.DimmCount ? gApp.Sys.DimmCount : gApp.Sys.DimmPresentCount;
  UINT32 present = gApp.Sys.DimmPresentCount;
  UINT32 empty = total > present ? total - present : 0;
  UINT32 y = inner.Y;
  char buf[64];
  struct {
    const char *label;
    UINT32 value;
    UINT32 color;
  } rows[3];

  rows[0].label = "在位";
  rows[0].value = present;
  rows[0].color = gTheme.Primary;
  rows[1].label = "已启用";
  rows[1].value = present;
  rows[1].color = gTheme.Ok;
  rows[2].label = "空";
  rows[2].value = empty;
  rows[2].color = gTheme.Idle;

  for (UINT32 i = 0; i < 3; i++) {
    UINT32 pct = total ? (rows[i].value * 100) / total : 0;
    GfxText(inner.X, y, rows[i].label, gTheme.SubText, 1);
    UiStrPrintf(buf, sizeof(buf), "%u", rows[i].value);
    GfxTextRight(inner.X + inner.W - 42, y, buf, gTheme.Text, 1);
    UiStrPrintf(buf, sizeof(buf), "%u%%", pct);
    GfxTextRight(inner.X + inner.W, y, buf, gTheme.Muted, 1);
    y += 17;
    UiMiniBar((GFX_RECT){ inner.X, y, inner.W, 8 }, pct * 10, rows[i].color);
    y += 20;
  }
  UiStrPrintf(buf, sizeof(buf), "%u 插槽 · %u socket",
              total, gApp.Sys.SocketCount ? gApp.Sys.SocketCount : 1);
  GfxTextClipped(inner.X, y, inner.W, buf, gTheme.Muted, 1);
}

static VOID DrawHealth(GFX_RECT card)
{
  GFX_RECT inner = UiCardInner(card);
  UINT32 ok = 0, warn = 0, crit = 0, off = 0;
  UINT32 i;
  GFX_RECT donut;
  char buf[48];

  for (i = 0; i < gApp.Sys.DimmCount && i < SYS_MAX_DIMMS; i++) {
    switch (LevelOfSlot(i)) {
    case 3: crit++; break;
    case 1: ok++; break;
    case 0:
      if (gApp.Sys.Dimms[i].Present) {
        warn++;
      } else {
        off++;
      }
      break;
    default: break;
    }
  }
  donut = (GFX_RECT){ card.X + (card.W - 128) / 2, inner.Y, 128, 128 };
  UiHealthDonut(donut, ok, warn, crit, off,
                (ok + warn + crit) == 0 ? "未检测" : (crit ? "异常" : (warn ? "注意" : "良好")));

  UiStrPrintf(buf, sizeof(buf), "%u 条已安装", gApp.Sys.DimmPresentCount);
  GfxTextCenter(card.X, card.W, inner.Y + 136, buf, gTheme.SubText, 1);

  {
    UINT32 y = inner.Y + 162;
    const char *labels[4] = { "健康", "注意", "异常", "未安装" };
    UINT32 values[4];
    UINT32 colors[4];
    values[0] = ok;
    values[1] = warn;
    values[2] = crit;
    values[3] = off;
    colors[0] = gTheme.Ok;
    colors[1] = gTheme.Warn;
    colors[2] = gTheme.Crit;
    colors[3] = gTheme.Idle;
    for (i = 0; i < 4; i++) {
      GFX_RECT tile = { inner.X, y, inner.W, 24 };
      UiStatTile(tile, colors[i], labels[i], values[i]);
      y += 28;
    }
  }
}

static VOID DrawChannels(GFX_RECT card)
{
  GFX_RECT inner = UiCardInner(card);
  UINT32 y = inner.Y;
  UINT32 i, j;
  UINT32 shown = 0;
  UINT32 rows = ChannelRowCount();
  UINT32 rowH = 84;
  UINT32 visible;
  UINT32 row;
  char buf[64];

  GfxText(inner.X, y, "socket · 通道", gTheme.Muted, 1);
  GfxTextRight(inner.X + inner.W, y, gDetail ? "颗粒视图已打开" : "Enter 打开颗粒视图", gTheme.Muted, 1);
  y += 20;
  UiDivider((GFX_RECT){ inner.X, y - 4, inner.W, 1 });

  visible = (inner.H - 24) / rowH;
  if (visible == 0) {
    visible = 1;
  }
  {
    UINT32 selRow = ChannelRowOfDimm(gSel);
    if (selRow < gChannelTop) {
      gChannelTop = selRow;
    }
    if (selRow >= gChannelTop + visible) {
      gChannelTop = selRow - visible + 1;
    }
    if (gChannelTop + visible > rows) {
      gChannelTop = rows > visible ? rows - visible : 0;
    }
  }

  for (row = gChannelTop; row < rows; row++) {
    const DIMM_INFO *d;
    UINT32 slotsOnChannel = 0;
    UINT32 firstOnChannel;

    /* every channel row starts at its first slot, however many slots share it */
    i = ChannelRowStart(row);
    firstOnChannel = i;
    d = &gApp.Sys.Dimms[i];
    for (j = i; j < gApp.Sys.DimmCount; j++) {
      if (gApp.Sys.Dimms[j].Socket == d->Socket && gApp.Sys.Dimms[j].Channel == d->Channel) {
        slotsOnChannel++;
      }
    }
    if (slotsOnChannel == 0) {
      slotsOnChannel = 1;
    }

    if (y + rowH > inner.Y + inner.H) {
      break;
    }
    UiStrPrintf(buf, sizeof(buf), "S%u · CH%u", d->Socket, d->Channel);
    GfxText(inner.X, y + 20, buf, gTheme.SubText, 1);

    {
      UINT32 x = inner.X + 68;
      UINT32 avail = inner.W - 68;
      UINT32 cardW = (avail - (slotsOnChannel - 1) * 10) / slotsOnChannel;
      UINT32 k = 0;
      if (cardW > 200) {
        cardW = 200;
      }
      for (j = i; j < gApp.Sys.DimmCount; j++) {
        const DIMM_INFO *s = &gApp.Sys.Dimms[j];
        GFX_RECT slot;
        if (s->Socket != d->Socket || s->Channel != d->Channel) {
          continue;
        }
        slot = (GFX_RECT){ x + k * (cardW + 10), y, cardW, 76 };
        UiHitRow(slot, j);
        k++;
        if (j == gSel) {
          GfxFillRound(slot, 8, gTheme.PrimarySoft);
          GfxRoundOutline(slot, 8, gTheme.Primary);
        } else if (!s->Present) {
          GfxFillRound(slot, 8, gTheme.PanelAlt);
          GfxRoundOutline(slot, 8, gTheme.Border);
        } else {
          GfxFillRound(slot, 8, gTheme.Panel);
          GfxRoundOutline(slot, 8, GFX_RGB(0xD8, 0xE1, 0xEC));
        }
        UiStrPrintf(buf, sizeof(buf), "%s", s->Locator[0] ? s->Locator : "DIMM");
        GfxTextClipped(slot.X + 10, slot.Y + 8, cardW - 20, buf,
                       s->Present ? gTheme.Text : gTheme.Muted, 1);
        if (s->Present) {
          const char *state = SlotStateText(j);
          GFX_RECT pill = { slot.X + 10, slot.Y + 26, GfxTextWidth(state, 1) + 20, 16 };
          DIMM_GEO geo;
          UiPill(pill, UiLevelColor(LevelOfSlot(j)), state);
          GfxFormatSize(buf, sizeof(buf), s->SizeBytes);
          GfxTextClipped(slot.X + pill.W + 16, slot.Y + 26, cardW - pill.W - 24, buf,
                         gTheme.SubText, 1);
          SysDimmGeometry(s, &geo);
          UiStrPrintf(buf, sizeof(buf), "x%u · %uR · %u 颗粒", geo.DeviceWidth,
                      geo.Ranks, geo.TotalDevices);
          GfxTextClipped(slot.X + 10, slot.Y + 50, cardW - 20, buf, gTheme.Muted, 1);
        } else {
          GfxText(slot.X + 10, slot.Y + 28, "空插槽", gTheme.Muted, 1);
          GfxText(slot.X + 10, slot.Y + 50, "无 SPD 信息", gTheme.Muted, 1);
        }
      }
      (VOID)firstOnChannel;
      y += 84;
      shown++;
    }
  }
  if (shown == 0) {
    GfxText(inner.X, y + 8, "SMBIOS 未提供 DIMM 插槽信息", gTheme.Muted, 1);
  } else if (rows > visible) {
    UiScrollbarScroll((GFX_RECT){ inner.X + inner.W - 4, inner.Y + 20, 4, inner.H - 24 },
                      gChannelTop, visible, rows, DimmScrollChannels);
  }
}

static VOID DrawDeviceHeat(GFX_RECT card)
{
  GFX_RECT inner = UiCardInner(card);
  UINT32 y = inner.Y;
  UINT32 i, j;
  UINT32 rows = 0;
  char buf[96];

  {
    const char *note = gResults.MemTested ? "来自最近一次内存检测" : "尚未运行内存检测";
    UINT32 noteW = GfxTextWidth(note, 1);
    GfxTextClipped(inner.X, y, inner.W - noteW - 16, "每格一个颗粒，数字为错误次数",
                   gTheme.Muted, 1);
    GfxTextRight(inner.X + inner.W, y, note, gTheme.Muted, 1);
  }
  y += 20;
  UiDivider((GFX_RECT){ inner.X, y - 4, inner.W, 1 });

  {
    UINT32 present = 0;
    UINT32 listBottom = inner.Y + inner.H - 44;
    UINT32 visible;
    UINT32 row;

    for (i = 0; i < gApp.Sys.DimmCount && i < SYS_MAX_DIMMS; i++) {
      if (gApp.Sys.Dimms[i].Present) {
        present++;
      }
    }
    visible = (listBottom > y) ? (listBottom - y) / 24 : 1;
    if (visible == 0) {
      visible = 1;
    }
    if (present > visible) {
      UINT32 selRow = PresentDimmRowOf(gSel);
      if (selRow < gHeatTop) {
        gHeatTop = selRow;
      }
      if (selRow >= gHeatTop + visible) {
        gHeatTop = selRow - visible + 1;
      }
      if (gHeatTop + visible > present) {
        gHeatTop = present - visible;
      }
    } else {
      gHeatTop = 0;
    }
    for (row = 0; row < present; row++) {
    const DIMM_INFO *d;
    DIMM_GEO geo;
    UINT32 cells;
    UINT32 cw;
    UINT32 dimmErrors;
    UINT32 shownErrors = 0;

    if (row < gHeatTop) {
      continue;
    }
    if (y + 26 > listBottom) {
      break;
    }
    i = PresentDimmAt(row);
    d = &gApp.Sys.Dimms[i];
    SysDimmGeometry(d, &geo);
    dimmErrors = SlotErrorCount(i);
    UiStrPrintf(buf, sizeof(buf), "%s", d->Locator[0] ? d->Locator : "DIMM");
    GfxTextClipped(inner.X, y, 132, buf, gTheme.Text, 1);
    if (dimmErrors) {
      UiStrPrintf(buf, sizeof(buf), "%u 次", dimmErrors);
      GfxTextClipped(inner.X + 136, y, 78, buf, gTheme.Crit, 1);
    } else if (gResults.MemTested) {
      GfxTextClipped(inner.X + 136, y, 78, "正常", gTheme.Ok, 1);
    } else {
      GfxTextClipped(inner.X + 136, y, 84, "未测", gTheme.Muted, 1);
    }

    cells = geo.TotalDevices ? geo.TotalDevices : 8;
    if (cells > 24) {
      cells = 24;
    }
    cw = (inner.W - 232 - (cells - 1) * 3) / cells;
    if (cw > 22) {
      cw = 22;
    }
    for (j = 0; j < cells; j++) {
      UINT32 err = DeviceErrorCount(i, j);
      GFX_RECT cell = { inner.X + 220 + j * (cw + 3), y + 2, cw, 12 };
      GfxFillRound(cell, 2, err ? gTheme.Crit : GFX_RGB(0xE3, 0xE9, 0xF1));
      if (err) {
        shownErrors++;
        if (cw >= 14) {
          UiStrPrintf(buf, sizeof(buf), "%u", err > 99 ? 99 : err);
          GfxTextCenter(cell.X, cell.W, cell.Y + 1, buf, gTheme.OnPrimary, 1);
        }
      }
    }
    y += 24;
    rows++;
    (VOID)shownErrors;
    }
    if (present > visible) {
      UiScrollbarScroll((GFX_RECT){ inner.X + inner.W - 4, inner.Y + 20, 4,
                                    listBottom - inner.Y - 20 },
                        gHeatTop, visible, present, DimmScrollErrors);
    }
  }

  if (rows == 0) {
    GfxText(inner.X, y + 6, "SMBIOS 未提供已安装的内存条，无法给出颗粒映射", gTheme.Warn, 1);
    return;
  }

  /* bit -> package map for the first module: this is how an error offset is
     turned into a device number, so it is worth showing explicitly */
  if (y + 120 < inner.Y + inner.H - 30) {
    UINT32 pick = gApp.Sys.Dimms[gSel].Present ? gSel : PresentDimmAt(0);
    const DIMM_INFO *d = &gApp.Sys.Dimms[pick];
    DIMM_GEO geo;
    UINT32 dev;
    UINT32 perRow;
    UINT32 colW;
    UINT32 startY;

    SysDimmGeometry(d, &geo);
    y += 14;
    UiDivider((GFX_RECT){ inner.X, y, inner.W, 1 });
    y += 10;
    UiStrPrintf(buf, sizeof(buf), "数据位 → 颗粒映射（x%u，同一 rank 内，%s的数据位）",
                geo.DeviceWidth, d->Locator[0] ? d->Locator : "DIMM");
    GfxTextClipped(inner.X, y, inner.W, buf, gTheme.SubText, 1);
    y += 20;
    perRow = geo.DevicesPerRank > 4 ? 4 : geo.DevicesPerRank;
    if (perRow < 1) {
      perRow = 1;
    }
    colW = inner.W / perRow;
    startY = y;
    for (dev = 0; dev < geo.DevicesPerRank && dev < 16; dev++) {
      UINT32 col = dev % perRow;
      UINT32 row = dev / perRow;
      UINT32 bit = dev * geo.DeviceWidth;
      UiStrPrintf(buf, sizeof(buf), "D%u  bit%u-%u", dev + 1, bit, bit + geo.DeviceWidth - 1);
      GfxTextClipped(inner.X + col * colW, startY + row * 18, colW - 8, buf,
                     DeviceErrorCount(pick, dev) ? gTheme.Crit : gTheme.SubText, 1);
    }
  }

  /* legend + explanation pinned to the bottom of the card */
  {
    UINT32 ly = inner.Y + inner.H - 40;
    UINT32 x = inner.X;
    GfxFillRound((GFX_RECT){ x, ly + 3, 12, 12 }, 2, GFX_RGB(0xE3, 0xE9, 0xF1));
    GfxText(x + 18, ly, "未报错颗粒", gTheme.SubText, 1);
    x += 104;
    GfxFillRound((GFX_RECT){ x, ly + 3, 12, 12 }, 2, gTheme.Crit);
    GfxText(x + 18, ly, "报错颗粒", gTheme.SubText, 1);
    if (gResults.MemDeviceMapped) {
      UiStrPrintf(buf, sizeof(buf), "%u 次错误已定位到具体颗粒", gResults.MemDeviceMapped);
      GfxTextClipped(inner.X, ly + 20, inner.W, buf, gTheme.Crit, 1);
    } else if (!gResults.MemTested) {
      GfxTextClipped(inner.X, ly + 20, inner.W, "运行内存检测后，此页按颗粒给出错误位置", gTheme.Muted, 1);
    } else if (gResults.MemErrorCount == 0) {
      GfxTextClipped(inner.X, ly + 20, inner.W, "图案写入与回读完全一致，全部颗粒通过", gTheme.Ok, 1);
    } else {
      GfxTextClipped(inner.X, ly + 20, inner.W, "错误未落在可测试的内存区间上，无法定位颗粒",
                     gTheme.Warn, 1);
    }
  }
}

/* ------------------------------------------------------------ detail view */
static VOID DrawDetailInfo(GFX_RECT r, UINT32 dimm)
{
  const DIMM_INFO *d = &gApp.Sys.Dimms[dimm];
  DIMM_GEO geo;
  UINT32 y = r.Y;
  UINT32 colW = r.W / 2;
  UINT32 kw;
  char buf[80];
  char num[16];

  SysDimmGeometry(d, &geo);
  GfxText(r.X, y, "丝印信息", gTheme.Muted, 1);
  GfxText(r.X + colW, y, "电气参数", gTheme.Muted, 1);
  y += 18;
  UiDivider((GFX_RECT){ r.X, y - 4, r.W, 1 });

  UiStrPrintf(buf, sizeof(buf), "%s", d->Manufacturer[0] ? d->Manufacturer : "n/a");
  GfxText(r.X, y, "制造商", gTheme.Muted, 1);
  kw = GfxTextWidth("制造商", 1) + 12;
  GfxTextClipped(r.X + kw, y, colW - kw - 12, buf, gTheme.Text, 1);
  if (geo.VddMv) {
    UiStrPrintf(buf, sizeof(buf), "%u mV", geo.VddMv);
  } else {
    UiStrPrintf(buf, sizeof(buf), "n/a");
  }
  GfxText(r.X + colW, y, "VDD", gTheme.Muted, 1);
  GfxTextClipped(r.X + colW + 50, y, colW - 60, buf, gTheme.Text, 1);
  y += 18;

  UiStrPrintf(buf, sizeof(buf), "%s", d->PartNumber[0] ? d->PartNumber : "n/a");
  GfxText(r.X, y, "部件号", gTheme.Muted, 1);
  kw = GfxTextWidth("部件号", 1) + 12;
  GfxTextClipped(r.X + kw, y, colW - kw - 12, buf, gTheme.Primary, 1);
  if (geo.VddqMv) {
    UiStrPrintf(buf, sizeof(buf), "%u mV", geo.VddqMv);
  } else {
    UiStrPrintf(buf, sizeof(buf), "n/a");
  }
  GfxText(r.X + colW, y, "VDDQ", gTheme.Muted, 1);
  GfxTextClipped(r.X + colW + 50, y, colW - 60, buf, gTheme.Text, 1);
  y += 18;

  UiStrPrintf(buf, sizeof(buf), "%s", d->Serial[0] ? d->Serial : "n/a");
  GfxText(r.X, y, "序列号", gTheme.Muted, 1);
  kw = GfxTextWidth("序列号", 1) + 12;
  GfxTextClipped(r.X + kw, y, colW - kw - 12, buf, gTheme.SubText, 1);
  if (geo.VppMv) {
    UiStrPrintf(buf, sizeof(buf), "%u mV", geo.VppMv);
    GfxText(r.X + colW, y, "VPP", gTheme.Muted, 1);
    GfxTextClipped(r.X + colW + 50, y, colW - 60, buf, gTheme.Text, 1);
  }
  y += 18;

  if (d->ConfiguredSpeed || d->Speed) {
    UiStrPrintf(buf, sizeof(buf), "%s %u MT/s", SysMemTypeName(d->MemoryType),
                d->ConfiguredSpeed ? d->ConfiguredSpeed : d->Speed);
  } else {
    UiStrPrintf(buf, sizeof(buf), "%s（SMBIOS 未报速度）", SysMemTypeName(d->MemoryType));
  }
  GfxText(r.X, y, "速率", gTheme.Muted, 1);
  kw = GfxTextWidth("速率", 1) + 12;
  GfxTextClipped(r.X + kw, y, colW - kw - 12, buf, gTheme.Text, 1);
  UiStrPrintf(buf, sizeof(buf), "%u / %u 位%s",
              geo.DevicesPerRank * geo.DeviceWidth, geo.TotalDevices * geo.DeviceWidth / geo.Ranks,
              geo.WidthAssumed ? L("（推算）") : "");
  GfxText(r.X + colW, y, "位宽", gTheme.Muted, 1);
  GfxTextClipped(r.X + colW + 50, y, colW - 60, buf, gTheme.Text, 1);
  y += 18;

  (VOID)num;
}

static VOID DrawDetailOverlay(GFX_RECT content, UINT32 dimm)
{
  const DIMM_INFO *d = &gApp.Sys.Dimms[dimm];
  DIMM_GEO geo;
  GFX_RECT panel = { content.X, content.Y, content.W, content.H };
  GFX_RECT head;
  UINT32 y;
  UINT32 err = SlotErrorCount(dimm);
  char buf[160];
  char size[20];

  SysDimmGeometry(d, &geo);
  DbgStr("dimm: overlay index=");
  DbgDec(dimm);
  DbgStr("\n");

  GfxResetClip();
  GfxBlendRect((GFX_RECT){ 0, 0, gScreen.Width, gScreen.Height }, GFX_RGB(0x0F, 0x17, 0x2A), 0x50);
  GfxFillRound(panel, 10, gTheme.Panel);
  GfxRoundOutline(panel, 10, gTheme.Border);

  head = (GFX_RECT){ panel.X + 1, panel.Y + 1, panel.W - 2, 52 };
  GfxFillRound(head, 9, gTheme.Primary);
  GfxFillRect((GFX_RECT){ head.X, head.Y + 26, head.W, 26 }, gTheme.Primary);
  GfxFormatSize(size, sizeof(size), d->SizeBytes);
  UiStrPrintf(buf, sizeof(buf), "DIMM 详情：%s", d->Locator[0] ? d->Locator : "DIMM");
  GfxText(head.X + 16, head.Y + 8, buf, gTheme.OnPrimary, 1);
  UiStrPrintf(buf, sizeof(buf), "%s  |  x%u  |  %uR  |  %s", size, geo.DeviceWidth, geo.Ranks,
              err ? L("检测到错误") : "OK");
  GfxText(head.X + 16, head.Y + 28, buf, GFX_RGB(0xD7, 0xE3, 0xFF), 1);
  UiStrPrintf(buf, sizeof(buf), "S%u · CH%u · 槽位 %u", d->Socket, d->Channel, d->Slot);
  GfxTextRight(head.X + head.W - 16, head.Y + 8, buf, GFX_RGB(0xD7, 0xE3, 0xFF), 1);
  UiStrPrintf(buf, sizeof(buf), "Esc 关闭   ↑↓ 切换内存条   B 正/背面");
  GfxTextRight(head.X + head.W - 16, head.Y + 28, buf, GFX_RGB(0xD7, 0xE3, 0xFF), 1);

  y = head.Y + head.H + 8;
  DrawDetailInfo((GFX_RECT){ panel.X + 20, y, panel.W - 40, 100 }, dimm);
  y += 108;

  {
    UINT32 legendY = panel.Y + panel.H - 34;
    UINT32 noteY = legendY - 22;
    UINT32 verdictY = noteY - 22;
    UINT32 moduleH;
    UINT32 avail = verdictY > y + 8 ? verdictY - 8 - y : 100;

    UINT32 side = SideViewFor(dimm);
    BOOLEAN twoSides = (side == 0 && geo.BackDevices) ? TRUE : FALSE;

    if (twoSides) {
      moduleH = (avail - 2 * 18 - 10) / 2;
    } else {
      moduleH = avail - 18;
    }
    if (moduleH > (twoSides ? 220u : 300u)) {
      moduleH = twoSides ? 220 : 300;
    }
    if (moduleH < 86) {
      moduleH = 86;
    }
    if (!twoSides && avail > 260) {
      y += (avail - 18 - moduleH) / 2 - 10;
    }
    if (side != 2) {
      UiStrPrintf(buf, sizeof(buf), "DIMM 正面 - S%u R0", d->Socket);
      GfxTextCenter(panel.X, panel.W, y, buf, gTheme.SubText, 1);
      y += 18;
      DrawSide((GFX_RECT){ panel.X + 20, y, panel.W - 40, moduleH }, dimm, FALSE);
      y += moduleH + (twoSides ? 10 : 8);
    }
    if (side != 1 && geo.BackDevices) {
      UiStrPrintf(buf, sizeof(buf), "DIMM 背面 - S%u R1", d->Socket);
      GfxTextCenter(panel.X, panel.W, y, buf, gTheme.SubText, 1);
      y += 18;
      DrawSide((GFX_RECT){ panel.X + 20, y, panel.W - 40, moduleH }, dimm, TRUE);
      y += moduleH + 8;
    }
  }

  /* legend + verdict */
  {
    GFX_RECT legend = { panel.X + 20, panel.Y + panel.H - 34, panel.W - 40, 20 };
    UINT32 x = legend.X;
    GfxFillRound((GFX_RECT){ x, legend.Y + 4, 12, 12 }, 2, CHIP_DARK);
    GfxText(x + 18, legend.Y + 4, "颗粒", gTheme.SubText, 1);
    x += 66;
    GfxFillRound((GFX_RECT){ x, legend.Y + 4, 12, 12 }, 2, CHIP_BAD);
    GfxText(x + 18, legend.Y + 4, "报错颗粒", gTheme.SubText, 1);
    x += 84;
    if (geo.HasRcd) {
      GfxFillRound((GFX_RECT){ x, legend.Y + 4, 12, 12 }, 2, RCD_BLUE);
      GfxText(x + 18, legend.Y + 4, "RCD", gTheme.SubText, 1);
      x += 60;
    }
    if (geo.HasPmic) {
      GfxFillRound((GFX_RECT){ x, legend.Y + 4, 12, 12 }, 2, PMIC_ORANGE);
      GfxText(x + 18, legend.Y + 4, "PMIC", gTheme.SubText, 1);
      x += 66;
      GfxFillRound((GFX_RECT){ x, legend.Y + 4, 12, 12 }, 2, SPD_PURPLE);
      GfxText(x + 18, legend.Y + 4, "SPD", gTheme.SubText, 1);
      x += 56;
    }
    GfxTextRight(legend.X + legend.W, legend.Y + 4, geo.Note, gTheme.Muted, 1);
  }
  y = panel.Y + panel.H - 78;
  if (err) {
    UINT32 i;
    UINT32 topDevice = 0xFFFFFFFF;
    UINT32 topCount = 0;
    for (i = 0; i < 32; i++) {
      if (gResults.MemDeviceErrors[dimm][i] > topCount) {
        topCount = gResults.MemDeviceErrors[dimm][i];
        topDevice = i;
      }
    }
    if (topDevice != 0xFFFFFFFF) {
      UiStrPrintf(buf, sizeof(buf), "D%u 报错 %u 次 · 该 DIMM 共 %u 次错误，建议整条更换或降频复测",
                  topDevice + 1, topCount, err);
    } else {
      UiStrPrintf(buf, sizeof(buf), "该 DIMM 共 %u 次错误（未能定位到具体颗粒）", err);
    }
    GfxTextClipped(panel.X + 20, y, panel.W - 40, buf, gTheme.Crit, 1);
  } else {
    GfxText(panel.X + 20, y, gResults.MemTested ? "此 DIMM 未检测到错误" : "尚未运行内存检测", 
            gResults.MemTested ? gTheme.Ok : gTheme.Muted, 1);
  }
  UiStrPrintf(buf, sizeof(buf), "颗粒位置由出错数据位反推，地址到 rank 的映射取决于固件，仅供参考");
  GfxTextClipped(panel.X + 20, y + 18, panel.W - 40, buf, gTheme.Muted, 1);
}

/* ------------------------------------------------------------------ page */
VOID PageDimmFocus(UINT32 dimmIndex)
{
  if (dimmIndex < gApp.Sys.DimmCount) {
    gSel = dimmIndex;
  }
  gDetail = FALSE;
}

VOID PageDimmEnter(VOID)
{
  UINT32 i;
  gDetail = FALSE;
  if (gSel >= gApp.Sys.DimmCount) {
    gSel = 0;
    for (i = 0; i < gApp.Sys.DimmCount; i++) {
      if (gApp.Sys.Dimms[i].Present) {
        gSel = i;
        break;
      }
    }
  } else if (gApp.Sys.DimmCount) {
    UINT32 present = 0;
    for (i = 0; i < gApp.Sys.DimmCount; i++) {
      if (gApp.Sys.Dimms[i].Present) {
        present++;
      }
    }
    if (present == 0) {
      gSel = 0;
    }
  }
  UiSetStatus(0, "↑↓ 选择插槽：通道视图与颗粒定位会跟随滚动，Enter 打开颗粒视图");
}

VOID PageDimmDraw(GFX_RECT content)
{
  UINT32 gap = 12;
  UINT32 leftW = 240;
  GFX_RECT left = { content.X, content.Y, leftW, content.H };
  GFX_RECT right = { content.X + leftW + gap, content.Y, content.W - leftW - gap, content.H };
  GFX_RECT summary = { left.X, left.Y, left.W, 206 };
  GFX_RECT health = { left.X, left.Y + 206 + gap, left.W, left.H - 206 - gap };
  UINT32 channelRows = 0;
  UINT32 channelH;
  UINT32 i, j;
  GFX_RECT channels;
  GFX_RECT heat;
  char buf[64];

  for (i = 0; i < gApp.Sys.DimmCount && i < SYS_MAX_DIMMS; i++) {
    BOOLEAN seen = FALSE;
    for (j = 0; j < i; j++) {
      if (gApp.Sys.Dimms[j].Socket == gApp.Sys.Dimms[i].Socket &&
          gApp.Sys.Dimms[j].Channel == gApp.Sys.Dimms[i].Channel) {
        seen = TRUE;
        break;
      }
    }
    if (!seen) {
      channelRows++;
    }
  }
  channelH = 40 + channelRows * 84;
  if (channelH > right.H * 62 / 100) {
    channelH = right.H * 62 / 100;
  }
  if (channelH < 152) {
    channelH = 152;
  }
  channels = (GFX_RECT){ right.X, right.Y, right.W, channelH };
  heat = (GFX_RECT){ right.X, right.Y + channelH + gap, right.W, right.H - channelH - gap };

  if (gDetail && gApp.Sys.DimmCount) {
    DrawDetailOverlay(content, gSel);
    return;
  }

  UiCard(summary, "插槽摘要", 0);
  DrawSummary(summary);

  UiCard(health, "DIMM 健康概览", 0);
  DrawHealth(health);

  UiStrPrintf(buf, sizeof(buf), "%u 个插槽 · %u 条在位 · %u 个通道", gApp.Sys.DimmCount,
              gApp.Sys.DimmPresentCount, ChannelRowCount());
  UiCard(channels, "通道视图", buf);
  DrawChannels(channels);

  UiCard(heat, "颗粒定位", gResults.MemErrorCount ? "发现错误" : "运行检测后可用");
  DrawDeviceHeat(heat);
}

VOID PageDimmPoint(UINT32 row)
{
  if (row < gApp.Sys.DimmCount) {
    gSel = row;
  }
}

BOOLEAN PageDimmKey(EFI_INPUT_KEY key)
{
  UINT32 count = gApp.Sys.DimmCount;
  DbgStr("dimm: key scan=");
  DbgDec(key.ScanCode);
  DbgStr(" ch=");
  DbgDec(key.UnicodeChar);
  DbgStr(" count=");
  DbgDec(count);
  DbgStr("\n");

  if (count == 0) {
    return FALSE;
  }
  if (gDetail) {
    switch (key.ScanCode) {
    case SCAN_ESC:
      gDetail = FALSE;
      return TRUE;
    case SCAN_UP:
      gSel = gSel ? gSel - 1 : count - 1;
      return TRUE;
    case SCAN_DOWN:
      gSel = (gSel + 1) % count;
      return TRUE;
    default:
      break;
    }
    if (key.UnicodeChar == 'b' || key.UnicodeChar == 'B') {
      DIMM_GEO geo;

      SysDimmGeometry(&gApp.Sys.Dimms[gSel], &geo);
      gSideViewDimm = (UINT8)gSel;
      gSideView = (UINT8)((gSideView + 1) % (geo.BackDevices ? 3 : 2));
      return TRUE;
    }
    if (key.UnicodeChar == '\r' || key.UnicodeChar == '\n' || key.UnicodeChar == ' ') {
      gDetail = FALSE;
      return TRUE;
    }
    return TRUE;
  }

  switch (key.ScanCode) {
  case SCAN_UP:
    gSel = gSel ? gSel - 1 : count - 1;
    return TRUE;
  case SCAN_DOWN:
    gSel = (gSel + 1) % count;
    return TRUE;
  case SCAN_HOME:
    gSel = 0;
    return TRUE;
  case SCAN_END:
    gSel = count - 1;
    return TRUE;
  case SCAN_PAGE_UP:
    gSel = gSel > 4 ? gSel - 4 : 0;
    return TRUE;
  case SCAN_PAGE_DOWN:
    gSel = gSel + 4 < count ? gSel + 4 : count - 1;
    return TRUE;
  default:
    break;
  }
  if (key.UnicodeChar == '\r' || key.UnicodeChar == '\n' || key.UnicodeChar == ' ') {
    if (!gApp.Sys.Dimms[gSel].Present) {
      UiSetStatus(2, "空插槽没有颗粒信息，请选择一个已安装的内存条");
      return TRUE;
    }
    gDetail = TRUE;
    return TRUE;
  }
  return FALSE;
}
