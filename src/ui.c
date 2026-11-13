/* ui.c - light themed dashboard widgets and the application shell. */
#include "ui.h"
#include "mouse.h"
#include "debug.h"
#include "i18n.h"
#include "pages.h"
#include "disk.h"
#include "pci.h"
#include "version.h"

typedef __builtin_va_list va_list;
#define va_start(v, l) __builtin_va_start(v, l)
#define va_end(v)      __builtin_va_end(v)
#define va_arg(v, t)   __builtin_va_arg(v, t)

UI_APP gApp;
static EFI_BOOT_SERVICES *gBS;
volatile BOOLEAN gStopRequested;
volatile BOOLEAN gBusy;

UI_THEME gTheme;

static const char *const gNavLabels[] = {
  "概览", "系统信息", "系统拓扑", "内存检测", "内存拓扑", "硬盘检测", "硬盘擦除",
  "CPU 检测", "压力测试", "工具集", "关于"
};

static VOID ThemeInit(VOID)
{
  gTheme.Bg           = GFX_RGB(0xF1, 0xF4, 0xF9);
  gTheme.Panel        = GFX_RGB(0xFF, 0xFF, 0xFF);
  gTheme.PanelAlt     = GFX_RGB(0xF8, 0xFA, 0xFC);
  gTheme.Border       = GFX_RGB(0xE2, 0xE8, 0xF0);
  gTheme.Text         = GFX_RGB(0x0F, 0x17, 0x2A);
  gTheme.SubText      = GFX_RGB(0x64, 0x74, 0x8B);
  gTheme.Muted        = GFX_RGB(0x94, 0xA3, 0xB8);
  gTheme.Primary      = GFX_RGB(0x25, 0x63, 0xEB);
  gTheme.PrimaryDark  = GFX_RGB(0x1D, 0x4E, 0xD8);
  gTheme.PrimarySoft  = GFX_RGB(0xEF, 0xF6, 0xFF);
  gTheme.OnPrimary    = GFX_RGB(0xFF, 0xFF, 0xFF);
  gTheme.Ok           = GFX_RGB(0x10, 0xB9, 0x81);
  gTheme.OkSoft       = GFX_RGB(0xEC, 0xFD, 0xF5);
  gTheme.Warn         = GFX_RGB(0xF5, 0x9E, 0x0B);
  gTheme.WarnSoft     = GFX_RGB(0xFF, 0xFB, 0xEB);
  gTheme.Crit         = GFX_RGB(0xEF, 0x44, 0x44);
  gTheme.CritSoft     = GFX_RGB(0xFE, 0xF2, 0xF2);
  gTheme.Idle         = GFX_RGB(0x9C, 0xA3, 0xAF);
  gTheme.IdleSoft     = GFX_RGB(0xF1, 0xF5, 0xF9);
  gTheme.HeaderBg     = GFX_RGB(0xFF, 0xFF, 0xFF);
  gTheme.HeaderText   = GFX_RGB(0x0F, 0x17, 0x2A);
  gTheme.SidebarBg    = GFX_RGB(0xFF, 0xFF, 0xFF);
  gTheme.SidebarSel   = GFX_RGB(0xEF, 0xF6, 0xFF);
  gTheme.SidebarSelText = GFX_RGB(0x1D, 0x4E, 0xD8);
  gTheme.Grid         = GFX_RGB(0xF1, 0xF5, 0xF9);
  gTheme.Chart1       = GFX_RGB(0x3B, 0x82, 0xF6);
  gTheme.Chart2       = GFX_RGB(0x8B, 0x5C, 0xF6);
}

/* --------------------------------------------------------------- printf */
/* case-insensitive ASCII compare, used for tool name matching */
int UiStrIcmp(const char *a, const char *b)
{
  while (*a && *b) {
    char ca = *a, cb = *b;
    if (ca >= 'A' && ca <= 'Z') {
      ca = (char)(ca + 32);
    }
    if (cb >= 'A' && cb <= 'Z') {
      cb = (char)(cb + 32);
    }
    if (ca != cb) {
      return (int)(UINT8)ca - (int)(UINT8)cb;
    }
    a++;
    b++;
  }
  return (int)(UINT8)*a - (int)(UINT8)*b;
}

UINT32 UiStrLen(const char *s)
{
  UINT32 n = 0;
  while (s && s[n]) {
    n++;
  }
  return n;
}

static VOID AppendChar(char *dst, UINTN cap, UINTN *pos, char c)
{
  if (*pos + 1 < cap) {
    dst[(*pos)++] = c;
    dst[*pos] = 0;
  }
}

static VOID AppendNum(char *dst, UINTN cap, UINTN *pos, UINT64 value, UINT32 base,
                      UINT32 width, char pad)
{
  char tmp[24];
  UINTN n = 0;
  if (value == 0) {
    tmp[n++] = '0';
  }
  while (value) {
    UINT32 digit = (UINT32)(value % base);
    tmp[n++] = (char)(digit < 10 ? '0' + digit : 'A' + digit - 10);
    value /= base;
  }
  while (width > n) {
    AppendChar(dst, cap, pos, pad);
    width--;
  }
  while (n) {
    AppendChar(dst, cap, pos, tmp[--n]);
  }
}

static VOID AppendStr(char *dst, UINTN cap, UINTN *pos, const char *s, UINT32 width, char pad)
{
  UINT32 len = UiStrLen(s);
  while (width > len) {
    AppendChar(dst, cap, pos, pad);
    width--;
  }
  while (s && *s) {
    AppendChar(dst, cap, pos, *s++);
  }
}

VOID UiStrPrintf(char *dst, UINTN cap, const char *fmt, ...)
{
  va_list ap;
  UINTN pos = 0;
  if (cap == 0) {
    return;
  }
  dst[0] = 0;
  fmt = L(fmt);
  va_start(ap, fmt);
  while (*fmt) {
    if (*fmt != '%') {
      AppendChar(dst, cap, &pos, *fmt++);
      continue;
    }
    fmt++;
    {
      UINT32 width = 0;
      char pad = ' ';
      UINT32 isLong = 0;
      if (*fmt == '0') {
        pad = '0';
        fmt++;
      }
      while (*fmt >= '0' && *fmt <= '9') {
        width = width * 10 + (UINT32)(*fmt - '0');
        fmt++;
      }
      while (*fmt == 'l') {
        isLong++;
        fmt++;
      }
      switch (*fmt) {
      case 's':
        AppendStr(dst, cap, &pos, va_arg(ap, char *), width, pad);
        break;
      case 'c':
        AppendChar(dst, cap, &pos, (char)va_arg(ap, int));
        break;
      case 'd': {
        INT64 v = isLong ? va_arg(ap, INT64) : (INT64)va_arg(ap, int);
        if (v < 0) {
          AppendChar(dst, cap, &pos, '-');
          v = -v;
        }
        AppendNum(dst, cap, &pos, (UINT64)v, 10, width, pad);
        break;
      }
      case 'u':
        AppendNum(dst, cap, &pos, isLong ? va_arg(ap, UINT64) : (UINT64)va_arg(ap, UINT32), 10, width, pad);
        break;
      case 'x':
        AppendNum(dst, cap, &pos, isLong ? va_arg(ap, UINT64) : (UINT64)va_arg(ap, UINT32), 16, width, pad);
        break;
      case 'X': {
        UINTN before = pos;
        AppendNum(dst, cap, &pos, isLong ? va_arg(ap, UINT64) : (UINT64)va_arg(ap, UINT32), 16, width, pad);
        while (before < pos) {
          if (dst[before] >= 'a' && dst[before] <= 'f') {
            dst[before] = (char)(dst[before] - 'a' + 'A');
          }
          before++;
        }
        break;
      }
      case '%':
        AppendChar(dst, cap, &pos, '%');
        break;
      default:
        AppendChar(dst, cap, &pos, '%');
        AppendChar(dst, cap, &pos, *fmt);
        break;
      }
      fmt++;
    }
  }
  va_end(ap);
}

VOID UiFormatTime(char *buf, UINTN cap)
{
  EFI_TIME t;
  gApp.BS->SetMem(&t, sizeof(t), 0);
  if (EFI_ERROR(gApp.ST->RuntimeServices->GetTime(&t, 0))) {
    GfxUtf8EncodeCopy(buf, cap, "--:--");
    return;
  }
  UiStrPrintf(buf, cap, "%02u:%02u:%02u", t.Hour, t.Minute, t.Second);
}

UINT64 UiNowMs(VOID)
{
  return SysMsSince(0);
}

/* ------------------------------------------------------------ lifecycle */
EFI_STATUS UiInit(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *ST)
{
  EFI_STATUS status;
  gApp.ST = ST;
  gApp.BS = ST->BootServices;
  gBS = ST->BootServices;
  gApp.ImageHandle = ImageHandle;
  IInit(ST);
  gApp.Page = 0;
  gApp.Nav = 0;
  gApp.Redraw = TRUE;
  ThemeInit();

  status = GfxInit(ImageHandle, ST);
  if (EFI_ERROR(status)) {
    return status;
  }
  SysCollect(&gApp.Sys, ImageHandle, ST);
  PciEnumerate();
  MouseInit();
  if (gPages[0].Enter) {
    gPages[0].Enter();
  }
  UiSetStatus(0, "系统信息已加载");
  return EFI_SUCCESS;
}

VOID UiSetStatus(UINT32 level, const char *text)
{
  GfxUtf8EncodeCopy(gApp.Status, sizeof(gApp.Status), text);
  gApp.StatusLevel = level;
  gApp.StatusTsc = SysTsc();
}

GFX_RECT UiContentRect(VOID)
{
  GFX_RECT r;
  r.X = UI_SIDEBAR_W + UI_PAD;
  r.Y = UI_HEADER_H + UI_PAD;
  r.W = gScreen.Width > (UI_SIDEBAR_W + UI_PAD * 2) ? gScreen.Width - UI_SIDEBAR_W - UI_PAD * 2 : 1;
  r.H = gScreen.Height > (UI_HEADER_H + UI_FOOTER_H + UI_PAD * 2)
            ? gScreen.Height - UI_HEADER_H - UI_FOOTER_H - UI_PAD * 2
            : 1;
  return r;
}

/* --------------------------------------------------------------- drawing */
static VOID DrawHeader(VOID)
{
  GFX_RECT r = { 0, 0, gScreen.Width, UI_HEADER_H };
  GFX_RECT logo = { 16, 12, 34, 34 };
  char buf[160];
  char right[160];
  GFX_RECT pill;

  GfxFillRect(r, gTheme.HeaderBg);
  GfxFillRect((GFX_RECT){ 0, UI_HEADER_H - 1, gScreen.Width, 1 }, gTheme.Border);

  GfxFillRound(logo, 8, gTheme.Primary);
  GfxTextCenter(logo.X, logo.W, logo.Y + 9, "U", gTheme.OnPrimary, 1);

  GfxText(60, 11, "UEFI 组件测试套件", gTheme.HeaderText, 1);
  GfxText(60, 31, "UEFI Component Test Suite · ", gTheme.SubText, 1);
  GfxText(60 + GfxTextWidth("UEFI Component Test Suite · ", 1), 31, "平台诊断", gTheme.SubText, 1);
  UINT32 titleEnd = 60 + GfxTextWidth("UEFI 组件测试套件", 1) + 24;

  if (gApp.Sys.SysProduct[0]) {
    UiStrPrintf(buf, sizeof(buf), "%s", gApp.Sys.SysProduct);
  } else {
    UiStrPrintf(buf, sizeof(buf), "未知平台");
  }
  UiStrPrintf(right, sizeof(right), "%s  ·  BIOS %s", buf,
              gApp.Sys.BiosVersion[0] ? gApp.Sys.BiosVersion : "n/a");
  {
    UINT32 limit = gScreen.Width - 150;
    UINT32 w = GfxTextWidth(right, 1);
    if (titleEnd + w + 8 > limit) {
      GfxTextClipped(titleEnd, 12, limit > titleEnd ? limit - titleEnd : 1, right, gTheme.SubText, 1);
    } else {
      GfxTextRight(limit, 12, right, gTheme.SubText, 1);
    }
  }

  pill.W = 116;
  pill.H = 24;
  pill.X = gScreen.Width - pill.W - 16;
  pill.Y = 18;
  GfxFillRound(pill, 12, gTheme.OkSoft);
  GfxFillCircle(pill.X + 14, pill.Y + 12, 4, gTheme.Ok);
  GfxText(pill.X + 26, pill.Y + 5, "诊断套件就绪", gTheme.Ok, 1);

  /* language switch chip: F4 toggles between English and Chinese */
  {
    GFX_RECT lang = { pill.X - 62, pill.Y, 52, 24 };
    GfxFillRound(lang, 12, gTheme.PrimarySoft);
    GfxRoundOutline(lang, 12, gTheme.Border);
    GfxTextCenter(lang.X, lang.W, lang.Y + 5, ILangTag(), gTheme.PrimaryDark, 1);
  }
}

static VOID DrawSidebar(VOID)
{
  GFX_RECT bar = { 0, UI_HEADER_H, UI_SIDEBAR_W, gScreen.Height - UI_HEADER_H - UI_FOOTER_H };
  UINT32 i;
  UINT32 y = UI_HEADER_H + 14;

  GfxFillRect(bar, gTheme.SidebarBg);
  GfxFillRect((GFX_RECT){ UI_SIDEBAR_W - 1, bar.Y, 1, bar.H }, gTheme.Border);
  GfxText(18, y, "诊断", gTheme.Muted, 1);
  y += 22;
  for (i = 0; i < gPageCount; i++) {
    GFX_RECT item = { 10, y, UI_SIDEBAR_W - 20, 36 };
    BOOLEAN sel = (i == gApp.Page);
    if (i == 8) {
      y += 10;
      GfxText(18, y, "工具", gTheme.Muted, 1);
      y += 22;
      item.Y = y;
    }
    if (sel) {
      GfxFillRound(item, 8, gTheme.SidebarSel);
      GfxFillRound((GFX_RECT){ item.X + 1, item.Y + 8, 3, 20 }, 2, gTheme.Primary);
    }
    UiIcon(i, item.X + 14, item.Y + 9, 18, sel ? gTheme.Primary : gTheme.SubText);
    GfxTextClipped(item.X + 44, item.Y + 10, item.W - 48, gNavLabels[i],
                   sel ? gTheme.SidebarSelText : gTheme.Text, 1);
    UiHitNav(item, i);
    y += 40;
  }

  /* the running version stays visible on every page, not only on the about page */
  {
    char ver[32];
    UiStrPrintf(ver, sizeof(ver), "UCTS v%s", UCTS_VERSION);
    GfxText(18, bar.Y + bar.H - 20, ver, gTheme.Muted, 1);
  }
}

static VOID DrawFooter(VOID)
{
  const UI_PAGE *page = &gPages[gApp.Page];
  GFX_RECT r = { 0, gScreen.Height - UI_FOOTER_H, gScreen.Width, UI_FOOTER_H };
  char clock[16];
  UINT32 level = gApp.StatusLevel;
  UINT32 color = gTheme.SubText;

  GfxFillRect(r, gTheme.HeaderBg);
  GfxFillRect((GFX_RECT){ 0, r.Y, gScreen.Width, 1 }, gTheme.Border);
  UiFormatTime(clock, sizeof(clock));
  {
    const char *hints = (page->Hints && page->Hints[0])
                            ? page->Hints
                            : "← → 切换页面    ↑ ↓ 选择    Enter 确认    F2 导出报告    F4 语言    F10 退出";
    UINT32 room = gScreen.Width - 16 - GfxTextWidth(clock, 1) - 24;
    UINT32 statusW = gApp.Status[0] ? GfxTextWidth(gApp.Status, 1) : 0;
    if (statusW) {
      room = room - statusW - 28;
    }
    GfxTextClipped(16, r.Y + 5, room > 60 ? room : 60, hints, gTheme.Muted, 1);
  }
  GfxTextRight(gScreen.Width - 16, r.Y + 5, clock, gTheme.Muted, 1);

  if (level == 1) {
    color = gTheme.Ok;
  } else if (level == 2) {
    color = gTheme.Warn;
  } else if (level == 3) {
    color = gTheme.Crit;
  }
  if (gApp.Status[0]) {
    UINT32 w = GfxTextWidth(gApp.Status, 1);
    UINT32 right = gScreen.Width - GfxTextWidth(clock, 1) - 28;
    UINT32 x = right > w ? right - w : 16;
    GfxTextClipped(x, r.Y + 5, right > x ? right - x : 1, gApp.Status, color, 1);
  }
}

static VOID DispatchKey(EFI_INPUT_KEY key);

/* ---------------------------------------------------------- hit regions */
#define UI_HIT_MAX 320

typedef struct {
  GFX_RECT   R;
  UI_HIT_FN  Fn;
  UINT32     Id;
} UI_HIT;

static UI_HIT  gHits[UI_HIT_MAX];
static UINT32  gHitCount;
static BOOLEAN gMouseDouble;
static UINT64  gAboutTsc;
static UINT32  gLastRowPage = 0xFFFFFFFFu;
static UINT32  gLastRowId;
static UINT64  gLastRowTsc;

static VOID ScrollThumb(GFX_RECT r, UINT32 first, UINT32 visible, UINT32 total,
                        UINT32 *thumbY, UINT32 *thumbH);

/* one entry per interactive scrollbar drawn this frame; the click handler gets
   the index, so it can page or start a drag on the right bar */
#define UI_SCROLL_MAX 8

typedef struct {
  GFX_RECT      R;
  UINT32        First;
  UINT32        Visible;
  UINT32        Total;
  UI_SCROLL_FN  Set;
} UI_SCROLL;

static UI_SCROLL gScrolls[UI_SCROLL_MAX];
static UINT32    gScrollCount;
static BOOLEAN   gDragOn;
static UI_SCROLL gDragState;
static UINT32    gDragGrab;
static UINT32    gDragLastY = 0xFFFFFFFFu;

static VOID HitScroll(UINT32 id)
{
  const UI_SCROLL *sc;
  UINT32 thumbY, thumbH;
  UINT32 y = MouseY();
  UINT32 maxFirst;

  if (id >= gScrollCount) {
    return;
  }
  sc = &gScrolls[id];
  if (sc->Set == 0 || sc->Total <= sc->Visible) {
    return;
  }
  ScrollThumb(sc->R, sc->First, sc->Visible, sc->Total, &thumbY, &thumbH);
  maxFirst = sc->Total - sc->Visible;
  if (y >= sc->R.Y + thumbY && y < sc->R.Y + thumbY + thumbH) {
    gDragOn = TRUE;
    gDragState = *sc;
    gDragGrab = y - (sc->R.Y + thumbY);
    gDragLastY = 0xFFFFFFFFu;
    return;
  }
  if (y < sc->R.Y + thumbY) {
    sc->Set(sc->First > sc->Visible ? sc->First - sc->Visible : 0, sc->Visible);
  } else {
    UINT32 next = sc->First + sc->Visible;
    sc->Set(next > maxFirst ? maxFirst : next, sc->Visible);
  }
  gApp.Redraw = TRUE;
}

/* keeps a dragged thumb under the pointer until the button comes up */
static VOID DragUpdate(VOID)
{
  const UI_SCROLL *sc = &gDragState;
  UINT32 thumbY, thumbH, y, span, maxFirst;

  if (!gDragOn) {
    return;
  }
  if (!(MouseButtons() & MOUSE_LEFT)) {
    gDragOn = FALSE;
    return;
  }
  y = MouseY();
  if (y == gDragLastY) {
    return;
  }
  gDragLastY = y;
  ScrollThumb(sc->R, sc->First, sc->Visible, sc->Total, &thumbY, &thumbH);
  span = sc->R.H > thumbH ? sc->R.H - thumbH : 0;
  thumbY = y > sc->R.Y + gDragGrab ? y - sc->R.Y - gDragGrab : 0;
  if (thumbY > span) {
    thumbY = span;
  }
  maxFirst = sc->Total > sc->Visible ? sc->Total - sc->Visible : 0;
  sc->Set(span ? (thumbY * maxFirst) / span : 0, sc->Visible);
  gApp.Redraw = TRUE;
}

VOID UiScrollbarScroll(GFX_RECT r, UINT32 first, UINT32 visible, UINT32 total,
                       UI_SCROLL_FN setFirst)
{
  UINT32 thumbY, thumbH;

  if (total <= visible || total == 0 || setFirst == 0) {
    return;
  }
  ScrollThumb(r, first, visible, total, &thumbY, &thumbH);
  GfxFillRound(r, r.W / 2, gTheme.IdleSoft);
  GfxFillRound((GFX_RECT){ r.X, r.Y + thumbY, r.W, thumbH }, r.W / 2, gTheme.Muted);
  if (gScrollCount < UI_SCROLL_MAX) {
    GFX_RECT grab = { r.X > 5 ? r.X - 5 : 0, r.Y, r.W + 10, r.H };
    UI_SCROLL *sc = &gScrolls[gScrollCount];
    sc->R = r;
    sc->First = first;
    sc->Visible = visible;
    sc->Total = total;
    sc->Set = setFirst;
    UiHitAdd(grab, HitScroll, gScrollCount);
    gScrollCount++;
  }
}

VOID UiHitClear(VOID)
{
  gHitCount = 0;
  gScrollCount = 0;
}

VOID UiHitAdd(GFX_RECT r, UI_HIT_FN fn, UINT32 id)
{
  if (!fn || gHitCount >= UI_HIT_MAX || r.W == 0 || r.H == 0) {
    return;
  }
  gHits[gHitCount].R = r;
  gHits[gHitCount].Fn = fn;
  gHits[gHitCount].Id = id;
  gHitCount++;
}

BOOLEAN UiHitTest(UINT32 x, UINT32 y, UI_HIT_FN *fn, UINT32 *id)
{
  UINT32 i = gHitCount;

  /* last registered wins: overlays are drawn, and registered, after pages */
  while (i--) {
    const UI_HIT *h = &gHits[i];
    if (x >= h->R.X && x < h->R.X + h->R.W && y >= h->R.Y && y < h->R.Y + h->R.H) {
      if (fn) {
        *fn = h->Fn;
      }
      if (id) {
        *id = h->Id;
      }
      return TRUE;
    }
  }
  return FALSE;
}

UINT32 UiHitCount(VOID)
{
  return gHitCount;
}

static VOID HitNav(UINT32 page)
{
  UiSwitchPage(page);
}

VOID UiHitNav(GFX_RECT r, UINT32 page)
{
  UiHitAdd(r, HitNav, page);
}

static VOID HitRow(UINT32 row)
{
  const UI_PAGE *page = &gPages[gApp.Page];
  BOOLEAN again;

  if (page->Point) {
    page->Point(row);
  }
  /* clicking the row that is already current opens it, so a second click
     never depends on how fast the console reports the button: the first
     click selects, the second one activates */
  again = (BOOLEAN)(gMouseDouble ||
                    (gLastRowPage == gApp.Page && gLastRowId == row));
  /* the row's Enter action runs: open, launch, refresh.  Pages whose rows are
     check boxes have no Enter action, so space (the key that toggles a row) is
     tried next. */
  if (again && page->Key) {
    EFI_INPUT_KEY enter;
    gBS->SetMem(&enter, sizeof(enter), 0);
    enter.UnicodeChar = '\r';
    if (!page->Key(enter)) {
      enter.UnicodeChar = ' ';
      (VOID)page->Key(enter);
    }
    gApp.Redraw = TRUE;
  }
  gLastRowPage = gApp.Page;
  gLastRowId = row;
  gLastRowTsc = SysTsc();
  gApp.Redraw = TRUE;
}

/* the check box of a row toggles on a single click: that is what a box looks
   like it should do, and it saves the "select, then press space" dance */
static VOID HitBox(UINT32 row)
{
  const UI_PAGE *page = &gPages[gApp.Page];
  EFI_INPUT_KEY key;

  if (page->Point) {
    page->Point(row);
  }
  gBS->SetMem(&key, sizeof(key), 0);
  key.UnicodeChar = ' ';
  if (page->Key) {
    (VOID)page->Key(key);
  }
  gLastRowPage = gApp.Page;
  gLastRowId = row;
  gLastRowTsc = SysTsc();
  gApp.Redraw = TRUE;
}

VOID UiHitRowBox(GFX_RECT row, UINT32 rowId)
{
  GFX_RECT box = row;

  UiHitAdd(row, HitRow, rowId);
  /* registered second, so it wins over the row itself */
  box.W = row.W > 30 ? 30 : row.W;
  UiHitAdd(box, HitBox, rowId);
}

VOID UiHitRow(GFX_RECT r, UINT32 row)
{
  if (gApp.Page < gPageCount && gPages[gApp.Page].Point) {
    UiHitAdd(r, HitRow, row);
  }
}

BOOLEAN UiMouseHot(VOID)
{
  UI_HIT_FN fn;
  UINT32 id;

  if (!MousePresent()) {
    return FALSE;
  }
  return UiHitTest(MouseX(), MouseY(), &fn, &id);
}

VOID UiMouseDraw(VOID)
{
  if (MousePresent()) {
    MouseShow(UiMouseHot());
  }
}

/* modal overlays pick through these three */
static BOOLEAN gDialogPicked;
static BOOLEAN gDialogCancel;
static UINT32  gDialogChoice;

static VOID DialogPickOption(UINT32 id)
{
  gDialogChoice = id;
  gDialogPicked = TRUE;
}

static VOID DialogCancel(UINT32 id)
{
  (VOID)id;
  gDialogCancel = TRUE;
  gDialogPicked = TRUE;
}

static VOID DialogAccept(UINT32 id)
{
  (VOID)id;
  gDialogCancel = FALSE;
  gDialogChoice = 0;
  gDialogPicked = TRUE;
}

static VOID SendSynthetic(EFI_INPUT_KEY key)
{
  DispatchKey(key);
}

VOID UiMousePoll(VOID)
{
  UINT32 x = 0;
  UINT32 y = 0;
  BOOLEAN dbl = FALSE;
  INT32 wheel;
  BOOLEAN moved;

  if (!MousePresent()) {
    return;
  }
  moved = MousePoll();

  wheel = MouseTakeWheel();
  while (wheel) {
    EFI_INPUT_KEY key;
    gBS->SetMem(&key, sizeof(key), 0);
    key.ScanCode = wheel < 0 ? SCAN_UP : SCAN_DOWN;
    SendSynthetic(key);
    wheel += wheel < 0 ? 1 : -1;
  }

  if (MouseTakeClick(&x, &y, &dbl)) {
    UI_HIT_FN fn = 0;
    UINT32 id = 0;
    DbgStr("click x=");
    DbgDec(x);
    DbgStr(" y=");
    DbgDec(y);
    DbgStr(" dbl=");
    DbgDec(dbl ? 1 : 0);
    DbgStr(" hit=");
    DbgDec(UiHitTest(x, y, &fn, &id) ? 1 : 0);
    DbgStr("\n");
    gMouseDouble = dbl;
    if (UiHitTest(x, y, &fn, &id)) {
      char note[64];
      if (dbl) {
        UiStrPrintf(note, sizeof(note), "点击 %u,%u · %s", x, y, L("双击"));
      } else {
        UiStrPrintf(note, sizeof(note), "点击 %u,%u", x, y);
      }
      UiSetStatus(0, note);
      fn(id);
    }
    gMouseDouble = FALSE;
  }

  if (moved || gApp.Redraw) {
    if (!gApp.Redraw) {
      MouseHide();
      MouseShow(UiMouseHot());
    }
  }
  DragUpdate();

  /* the about page reports pointer statistics, so it has to repaint while the
     pointer is being used - throttled, a diagnostics page does not need 100 fps */
  if (gApp.Page == PAGE_ABOUT) {
    if (SysMsSince(gAboutTsc) > 300) {
      gAboutTsc = SysTsc();
      gApp.Redraw = TRUE;
    }
  }
}

VOID UiDrawEverything(VOID)
{
  GFX_RECT content = UiContentRect();
  UiHitClear();
  MouseForget();
  GfxResetClip();
  GfxClear(gTheme.Bg);
  DrawSidebar();
  DrawHeader();
  DrawFooter();
  GfxSetClip(content);
  if (gApp.Page < gPageCount && gPages[gApp.Page].Draw) {
    gPages[gApp.Page].Draw(content);
  }
  GfxResetClip();
  GfxPresent();
  gApp.Redraw = FALSE;
  gApp.Frame++;
  UiMouseDraw();
}

VOID UiRedraw(VOID)
{
  gApp.Redraw = TRUE;
}

VOID UiPresentAll(VOID)
{
  GfxPresent();
}

/* ---------------------------------------------------------------- input */
static BOOLEAN ReadKey(EFI_INPUT_KEY *key)
{
  EFI_STATUS status;
  gBS->SetMem(key, sizeof(*key), 0);
  status = gApp.ST->ConIn->ReadKeyStroke(gApp.ST->ConIn, key);
  return (BOOLEAN)(!EFI_ERROR(status));
}

VOID UiSwitchPage(UINT32 page)
{
  if (page >= gPageCount || page == gApp.Page) {
    return;
  }
  if (gPages[gApp.Page].Leave) {
    gPages[gApp.Page].Leave();
  }
  gApp.Page = page;
  gApp.Nav = page;
  /* a status line belongs to the page that produced it */
  UiSetStatus(0, "");
  if (gPages[gApp.Page].Enter) {
    gPages[gApp.Page].Enter();
  }
  gApp.Redraw = TRUE;
}

static VOID ExportReport(VOID);

static VOID DispatchKey(EFI_INPUT_KEY key)
{
  if (gBusy) {
    if (key.ScanCode == SCAN_ESC || key.ScanCode == SCAN_F6) {
      gStopRequested = TRUE;
    }
    return;
  }
  switch (key.ScanCode) {
  case SCAN_LEFT:
  case SCAN_RIGHT:
    /* arrows always change pages: letting some pages swallow them made the
       same key mean different things on different screens */
    if (key.ScanCode == SCAN_LEFT) {
      UiSwitchPage(gApp.Page == 0 ? gPageCount - 1 : gApp.Page - 1);
    } else {
      UiSwitchPage((gApp.Page + 1) % gPageCount);
    }
    return;
  case SCAN_F2:
    ExportReport();
    return;
  case SCAN_F4:
    IToggle();
    /* cached health/target strings are produced at scan time, so refresh them
       with the new language before the screen is redrawn */
    if (!gBusy) {
      UINTN i;
      for (i = 0; i < DiskCount(); i++) {
        DiskRefreshHealth(DiskAt(i));
      }
    }
    UiSetStatus(0, ILang() == LANG_ZH ? "界面语言：中文" : "UI language: English");
    UiRedraw();
    return;
  case SCAN_F10:
    gApp.Quit = TRUE;
    return;
  case SCAN_ESC:
    if (gApp.Page != 0) {
      UiSwitchPage(0);
    } else {
      gApp.Quit = TRUE;
    }
    return;
  default:
    break;
  }
  if (key.UnicodeChar >= '1' && key.UnicodeChar <= '9') {
    UiSwitchPage((UINT32)(key.UnicodeChar - '1'));
    return;
  }
  if (key.UnicodeChar == '0' && gPageCount > PAGE_ABOUT) {
    UiSwitchPage(PAGE_ABOUT);
    return;
  }
  if (gApp.Page < gPageCount && gPages[gApp.Page].Key) {
    if (gPages[gApp.Page].Key(key)) {
      gApp.Redraw = TRUE;
    }
  }
}

BOOLEAN UiPump(VOID)
{
  EFI_INPUT_KEY key;
  while (ReadKey(&key)) {
    DispatchKey(key);
  }
  UiMousePoll();
  return gStopRequested;
}

static EFI_INPUT_KEY WaitKey(UINTN timeoutMs)
{
  EFI_INPUT_KEY key;
  UINT64 start = SysTsc();
  gBS->SetMem(&key, sizeof(key), 0);
  for (;;) {
    if (ReadKey(&key)) {
      return key;
    }
    if (timeoutMs && SysMsSince(start) > timeoutMs) {
      return key;
    }
    /* modal overlays keep the cursor usable while they wait */
    UiMousePoll();
    gBS->Stall(8000);
  }
}

VOID UiRun(VOID)
{
  if (gPages[gApp.Page].Enter) {
    gPages[gApp.Page].Enter();
  }
  UiDrawEverything();
  while (!gApp.Quit) {
    EFI_INPUT_KEY key;

    if (ReadKey(&key)) {
      DispatchKey(key);
    }
    /* UiMousePoll moves the cursor itself and asks for a full redraw only when
       a click changed something */
    UiMousePoll();
    if (gApp.Redraw) {
      UiDrawEverything();
    } else {
      gBS->Stall(10000);
    }
  }
}

/* -------------------------------------------------------------- widgets */
UINT32 UiLevelColor(UINT32 level)
{
  switch (level) {
  case 1: return gTheme.Ok;
  case 2: return gTheme.Warn;
  case 3: return gTheme.Crit;
  default: return gTheme.Idle;
  }
}

UINT32 UiLevelSoft(UINT32 level)
{
  switch (level) {
  case 1: return gTheme.OkSoft;
  case 2: return gTheme.WarnSoft;
  case 3: return gTheme.CritSoft;
  default: return gTheme.IdleSoft;
  }
}

VOID UiCard(GFX_RECT r, const char *title, const char *rightText)
{
  GfxFillRound(r, 10, gTheme.Panel);
  GfxRoundOutline(r, 10, gTheme.Border);
  if (title) {
    GfxFillRound((GFX_RECT){ r.X + 16, r.Y + 16, 3, 14 }, 2, gTheme.Primary);
    {
      UINT32 titleW = r.W - 26 - 16;
      if (rightText) {
        titleW -= GfxTextWidth(rightText, 1) + 14;
      }
      GfxTextClipped(r.X + 26, r.Y + 14, titleW > 1 ? titleW : 1, title, gTheme.Text, 1);
    }
  }
  if (rightText) {
    GfxTextRight(r.X + r.W - 16, r.Y + 14, rightText, gTheme.SubText, 1);
  }
}

GFX_RECT UiCardInner(GFX_RECT card)
{
  GFX_RECT r;
  r.X = card.X + 16;
  r.Y = card.Y + 38;
  r.W = card.W > 32 ? card.W - 32 : 1;
  r.H = card.H > 46 ? card.H - 46 : 1;
  return r;
}

VOID UiSectionHeader(GFX_RECT r, const char *title, const char *rightText)
{
  GfxText(r.X, r.Y, title, gTheme.Text, 1);
  if (rightText) {
    GfxTextRight(r.X + r.W, r.Y, rightText, gTheme.SubText, 1);
  }
}

VOID UiBanner(GFX_RECT r, const char *title, const char *meta, UINT32 color0, UINT32 color1)
{
  GfxFillRound(r, 10, color0);
  GfxGradientH((GFX_RECT){ r.X + 1, r.Y + 1, r.W - 2, 22 }, color0, color1);
  GfxText(r.X + 16, r.Y + 10, title, gTheme.OnPrimary, 1);
  if (meta) {
    GfxTextRight(r.X + r.W - 16, r.Y + 10, meta, gTheme.OnPrimary, 1);
  }
}

VOID UiProgressBar(GFX_RECT r, UINT32 permille, UINT32 color, const char *label)
{
  UINT32 radius = r.H / 2;
  UINT32 fillW = (r.W * (permille > 1000 ? 1000 : permille)) / 1000;
  GfxFillRound(r, radius, gTheme.Grid);
  if (fillW) {
    GfxFillRound((GFX_RECT){ r.X, r.Y, fillW < r.H ? r.H : fillW, r.H }, radius, color);
  }
  if (label) {
    GfxTextCenter(r.X, r.W, r.Y + (r.H > 16 ? (r.H - 16) / 2 : 0), label,
                  gTheme.Text, 1);
  }
}

VOID UiMiniBar(GFX_RECT r, UINT32 permille, UINT32 color)
{
  UINT32 w = (r.W * (permille > 1000 ? 1000 : permille)) / 1000;
  GfxFillRound(r, r.H / 2, gTheme.IdleSoft);
  if (w) {
    GfxFillRound((GFX_RECT){ r.X, r.Y, w < r.H ? r.H : w, r.H }, r.H / 2, color);
  }
}

VOID UiHealthDonut(GFX_RECT r, UINT32 ok, UINT32 warn, UINT32 crit, UINT32 off,
                   const char *centerLabel)
{
  UINT32 cx = r.X + r.W / 2;
  UINT32 cy = r.Y + r.H / 2;
  UINT32 radius = (r.W < r.H ? r.W : r.H) / 2 - 2;
  UINT32 thickness = radius / 4;
  UINT32 total = ok + warn + crit + off;
  UINT32 start = 0;
  char value[24];
  struct {
    UINT32 count;
    UINT32 color;
  } seg[4];
  UINT32 i;

  seg[0].count = ok;
  seg[0].color = gTheme.Ok;
  seg[1].count = warn;
  seg[1].color = gTheme.Warn;
  seg[2].count = crit;
  seg[2].color = gTheme.Crit;
  seg[3].count = off;
  seg[3].color = gTheme.Idle;

  if (total == 0) {
    GfxDonut(cx, cy, radius, radius - thickness, gTheme.Grid, 0, 360);
  } else {
    for (i = 0; i < 4; i++) {
      UINT32 sweep;
      if (seg[i].count == 0) {
        continue;
      }
      sweep = (seg[i].count * 360) / total;
      if (sweep < 3) {
        sweep = 3;
      }
      GfxDonut(cx, cy, radius, radius - thickness, seg[i].color, start, start + sweep);
      start += sweep;
    }
  }
  GfxFormatU64(value, total);
  GfxTextCenter(r.X, r.W, cy - 20, value, gTheme.Text, 2);
  if (centerLabel) {
    GfxTextCenter(r.X, r.W, cy + 10, centerLabel, gTheme.SubText, 1);
  }
}

VOID UiStatTile(GFX_RECT r, UINT32 color, const char *label, UINT32 value)
{
  char buf[24];
  GfxFillRound(r, 8, gTheme.Panel);
  GfxRoundOutline(r, 8, gTheme.Border);
  GfxFillCircle(r.X + 18, r.Y + r.H / 2, 5, color);
  GfxFormatU64(buf, value);
  GfxTextClipped(r.X + 32, r.Y + (r.H - 16) / 2, r.W - 32 - 14 - GfxTextWidth(buf, 1) - 10,
                 label, gTheme.SubText, 1);
  GfxTextRight(r.X + r.W - 14, r.Y + (r.H - 16) / 2, buf, gTheme.Text, 1);
}

VOID UiPill(GFX_RECT r, UINT32 color, const char *text)
{
  if (r.H < 20) {
    r.Y -= 1;
    r.H = 20;
  }
  GfxFillRound(r, r.H / 2, color);
  GfxTextCenter(r.X, r.W, r.Y + (r.H - 16) / 2, text, gTheme.OnPrimary, 1);
}

VOID UiCheck(GFX_RECT box, BOOLEAN on)
{
  if (!(on)) {
    GfxRoundOutline(box, 4, gTheme.Muted);
    return;
  }
  GfxFillRound(box, 4, gTheme.Primary);
  /* two strokes of a check mark: short down-right, long up-right */
  {
    UINT32 x = box.X + 3;
    UINT32 y = box.Y + box.H / 2;
    UINT32 i;
    for (i = 0; i < 4; i++) {
      GfxFillRect((GFX_RECT){ x + i, y + i, 2, 2 }, gTheme.OnPrimary);
    }
    for (i = 0; i < 6; i++) {
      GfxFillRect((GFX_RECT){ x + 3 + i, y + 3 - i, 2, 2 }, gTheme.OnPrimary);
    }
  }
}

VOID UiKeyHint(UINT32 x, UINT32 y, const char *key, const char *label)
{
  UINT32 kw = GfxTextWidth(key, 1) + 10;
  GFX_RECT box = { x, y, kw, 18 };
  GfxFillRound(box, 5, gTheme.IdleSoft);
  GfxRoundOutline(box, 5, gTheme.Border);
  GfxTextCenter(box.X, box.W, y + 1, key, gTheme.SubText, 1);
  GfxText(x + kw + 6, y + 1, label, gTheme.Muted, 1);
}

VOID UiDivider(GFX_RECT r)
{
  GfxFillRect((GFX_RECT){ r.X, r.Y, r.W, 1 }, gTheme.Border);
}

VOID UiTableHeader(GFX_RECT r, const char *const *cols, const UINT32 *widths, UINT32 count)
{
  UINT32 x = r.X;
  UINT32 i;
  GfxFillRound(r, 6, gTheme.PanelAlt);
  for (i = 0; i < count; i++) {
    GfxTextClipped(x + 10, r.Y + (r.H - 16) / 2, widths[i] > 20 ? widths[i] - 16 : 1,
                   cols[i], gTheme.SubText, 1);
    x += widths[i];
  }
}

VOID UiTableRow(GFX_RECT r, const char *const *cells, const UINT32 *widths, UINT32 count,
                UINT32 textColor, BOOLEAN selected, BOOLEAN zebra)
{
  UINT32 x = r.X;
  UINT32 i;
  if (selected) {
    GfxFillRound(r, 6, gTheme.PrimarySoft);
  } else if (zebra) {
    GfxFillRect(r, gTheme.PanelAlt);
  }
  for (i = 0; i < count; i++) {
    GfxTextClipped(x + 10, r.Y + (r.H - 16) / 2, widths[i] > 20 ? widths[i] - 16 : 1,
                   cells[i], selected ? gTheme.PrimaryDark : textColor, 1);
    x += widths[i];
  }
}

VOID UiListRow(GFX_RECT r, const char *left, const char *right, UINT32 leftColor,
               UINT32 rightColor, BOOLEAN selected, BOOLEAN zebra)
{
  if (selected) {
    GfxFillRound(r, 6, gTheme.PrimarySoft);
    GfxFillRound((GFX_RECT){ r.X + 2, r.Y + 6, 3, r.H - 12 }, 2, gTheme.Primary);
  } else if (zebra) {
    GfxFillRect(r, gTheme.PanelAlt);
  }
  if (left) {
    GfxTextClipped(r.X + 12, r.Y + (r.H - 16) / 2, r.W > 160 ? r.W - 150 : 1, left, leftColor, 1);
  }
  if (right) {
    GfxTextRight(r.X + r.W - 12, r.Y + (r.H - 16) / 2, right, rightColor, 1);
  }
}

static VOID ScrollThumb(GFX_RECT r, UINT32 first, UINT32 visible, UINT32 total,
                        UINT32 *thumbY, UINT32 *thumbH)
{
  UINT32 h, y;
  if (total <= visible || total == 0) {
    *thumbY = 0;
    *thumbH = r.H;
    return;
  }
  h = (r.H * visible) / total;
  if (h < 16) {
    h = 16;
  }
  y = ((r.H - h) * first) / (total - visible);
  *thumbY = y;
  *thumbH = h;
}

VOID UiScrollbar(GFX_RECT r, UINT32 first, UINT32 visible, UINT32 total)
{
  UINT32 thumbH, thumbY;
  if (total <= visible || total == 0) {
    return;
  }
  ScrollThumb(r, first, visible, total, &thumbY, &thumbH);
  GfxFillRound(r, r.W / 2, gTheme.IdleSoft);
  GfxFillRound((GFX_RECT){ r.X, r.Y + thumbY, r.W, thumbH }, r.W / 2, gTheme.Muted);
}

BOOLEAN UiListKey(UI_LIST *list, EFI_INPUT_KEY key)
{
  switch (key.ScanCode) {
  case SCAN_UP:
    if (list->Sel > 0) {
      list->Sel--;
    }
    break;
  case SCAN_DOWN:
    if (list->Sel + 1 < list->Count) {
      list->Sel++;
    }
    break;
  case SCAN_PAGE_UP:
    list->Sel = list->Sel > list->Visible ? list->Sel - list->Visible : 0;
    break;
  case SCAN_PAGE_DOWN:
    list->Sel = list->Sel + list->Visible < list->Count ? list->Sel + list->Visible : list->Count - 1;
    break;
  case SCAN_HOME:
    list->Sel = 0;
    break;
  case SCAN_END:
    list->Sel = list->Count ? list->Count - 1 : 0;
    break;
  default:
    return FALSE;
  }
  UiListClamp(list, list->Visible);
  return TRUE;
}

VOID UiListClamp(UI_LIST *list, UINT32 visible)
{
  list->Visible = visible;
  if (list->Sel < list->Scroll) {
    list->Scroll = list->Sel;
  }
  if (list->Sel >= list->Scroll + visible) {
    list->Scroll = list->Sel - visible + 1;
  }
  if (list->Count < visible) {
    list->Scroll = 0;
  }
}

/* ---------------------------------------------------------------- icons */
VOID UiIcon(UINT32 kind, UINT32 x, UINT32 y, UINT32 size, UINT32 color)
{
  UINT32 s = size;
  switch (kind) {
  case 0: /* home */
    GfxFillRect((GFX_RECT){ x + s / 4, y + s / 2, s / 2, s / 2 - 1 }, color);
    {
      UINT32 i;
      for (i = 0; i < s / 2; i++) {
        GfxHLine(x + s / 4 - i / 2 + (i / 2 == 0 ? 0 : 0), x + s / 4 + i, y + s / 2 - i / 2, color);
        GfxHLine(x + s - s / 4 - i, x + s - s / 4 + i / 2, y + s / 2 - i / 2, color);
      }
    }
    break;
  case 1: /* chip */
    GfxRoundOutline((GFX_RECT){ x + s / 4, y + s / 4, s / 2, s / 2 }, 2, color);
    GfxFillRect((GFX_RECT){ x + s / 4 + 2, y + s / 4 + 2, s / 2 - 4, s / 2 - 4 }, color);
    GfxHLine(x, x + s / 4, y + s / 3, color);
    GfxHLine(x, x + s / 4, y + s - s / 3, color);
    GfxHLine(x + s - s / 4, x + s, y + s / 3, color);
    GfxHLine(x + s - s / 4, x + s, y + s - s / 3, color);
    GfxVLine(x + s / 3, y, y + s / 4, color);
    GfxVLine(x + s - s / 3, y, y + s / 4, color);
    GfxVLine(x + s / 3, y + s - s / 4, y + s, color);
    GfxVLine(x + s - s / 3, y + s - s / 4, y + s, color);
    break;
  case 2: /* memory */
    GfxRoundOutline((GFX_RECT){ x, y + s / 4, s, s / 2 }, 2, color);
    GfxFillRect((GFX_RECT){ x + 3, y + s / 3, 2, s / 3 }, color);
    GfxFillRect((GFX_RECT){ x + 8, y + s / 3, 2, s / 3 }, color);
    GfxFillRect((GFX_RECT){ x + 13, y + s / 3, 2, s / 3 }, color);
    GfxHLine(x + 2, x + s - 2, y + s - 3, color);
    break;
  case 3: /* dimm: module with packages */
    GfxRoundOutline((GFX_RECT){ x, y + s / 3, s, s / 3 }, 1, color);
    GfxFillRect((GFX_RECT){ x + s / 8, y + s / 3 + 3, s / 8, s / 3 - 6 }, color);
    GfxFillRect((GFX_RECT){ x + s / 8 + s / 4, y + s / 3 + 3, s / 8, s / 3 - 6 }, color);
    GfxFillRect((GFX_RECT){ x + s / 8 + s / 2, y + s / 3 + 3, s / 8, s / 3 - 6 }, color);
    GfxHLine(x + 2, x + s - 2, y + s - 2, color);
    break;
  case 4: /* disk */
    GfxRoundOutline((GFX_RECT){ x, y + s / 4, s, s / 2 }, 3, color);
    GfxFillCircle(x + s / 4, y + s / 2, 2, color);
    GfxHLine(x + s / 2, x + s - 3, y + s / 2, color);
    break;
  case 5: /* erase */
    GfxFillRound((GFX_RECT){ x + s / 4, y + s / 4, s / 2, s / 2 }, 2, color);
    GfxHLine(x + s / 5, x + s - s / 5, y + s / 6, color);
    GfxVLine(x + s / 2, y + s / 6, y + s / 4, color);
    break;
  case 6: /* cpu */
    GfxFillRound((GFX_RECT){ x + s / 5, y + s / 5, s - 2 * s / 5, s - 2 * s / 5 }, 3, color);
    GfxRoundOutline((GFX_RECT){ x + s / 5, y + s / 5, s - 2 * s / 5, s - 2 * s / 5 }, 3, color);
    GfxFillRect((GFX_RECT){ x + s / 2 - 2, y + s / 2 - 2, 4, 4 }, gTheme.Panel);
    break;
  case 7: /* stress: rising bars */
    GfxFillRound((GFX_RECT){ x + 1, y + s / 2 + 1, s / 4, s / 2 - 1 }, 1, color);
    GfxFillRound((GFX_RECT){ x + s / 2 - s / 8, y + s / 3, s / 4, s - s / 3 - 1 }, 1, color);
    GfxFillRound((GFX_RECT){ x + s - s / 4 - 1, y + 1, s / 4, s - 2 }, 1, color);
    break;
  case 8: /* tools */
    GfxFillRound((GFX_RECT){ x + s / 8, y + s / 2, s / 2, s / 4 }, 3, color);
    GfxFillCircle(x + s - s / 4, y + s / 4, s / 4, color);
    GfxFillCircle(x + s - s / 4, y + s / 4, s / 9, gTheme.Panel);
    GfxVLine(x + s / 2, y + s / 2, y + s, color);
    break;
  case 9: /* network: globe */
    GfxCircle(x + s / 2, y + s / 2, s / 2 - 1, color);
    GfxHLine(x + 1, x + s - 1, y + s / 2, color);
    GfxVLine(x + s / 2, y + 1, y + s - 1, color);
    break;
  case 10: /* display */
    GfxRoundOutline((GFX_RECT){ x, y + s / 5, s, (s * 3) / 5 }, 2, color);
    GfxVLine(x + s / 2, y + s / 5 + (s * 3) / 5, y + s - 2, color);
    GfxHLine(x + s / 3, x + s - s / 3, y + s - 2, color);
    break;
  case 11: /* bridge / bus */
    GfxRoundOutline((GFX_RECT){ x + s / 5, y + s / 3, (s * 3) / 5, s / 3 }, 2, color);
    GfxHLine(x, x + s, y + s / 2, color);
    break;
  case 12: /* usb */
    GfxRoundOutline((GFX_RECT){ x + s / 3, y + s / 2, s / 3, s / 2 - 1 }, 2, color);
    GfxVLine(x + s / 2, y + s / 6, y + s / 2, color);
    GfxFillRect((GFX_RECT){ x + s / 2 - 2, y + 1, 5, 3 }, color);
    break;
  case 13: /* audio */
    GfxFillRound((GFX_RECT){ x + s / 6, y + s / 3, s / 3, s / 3 }, 1, color);
    GfxVLine(x + s / 2 + 2, y + s / 4, y + s - s / 4, color);
    GfxVLine(x + s / 2 + 5, y + s / 3, y + s - s / 3, color);
    break;
  case 14: /* generic chip */
    GfxRoundOutline((GFX_RECT){ x + s / 4, y + s / 4, s / 2, s / 2 }, 2, color);
    GfxVLine(x + s / 2, y + s / 4, y + s - s / 4, color);
    break;
  default: /* info */
    GfxCircle(x + s / 2, y + s / 2, s / 2 - 1, color);
    GfxCircle(x + s / 2, y + s / 2, s / 2 - 2, color);
    GfxFillRect((GFX_RECT){ x + s / 2 - 1, y + s / 2 - 4, 2, 3 }, color);
    GfxFillRect((GFX_RECT){ x + s / 2 - 1, y + s / 2, 2, 5 }, color);
    break;
  }
}

static UINT32 CharWidthAt(const char *s, const UINT8 **next)
{
  const UINT8 *p = (const UINT8 *)s;
  UINT32 cp = *p++;
  if (cp >= 0x80) {
    if ((cp & 0xE0) == 0xC0) {
      p++;
    } else if ((cp & 0xF0) == 0xE0) {
      p += 2;
    } else if ((cp & 0xF8) == 0xF0) {
      p += 3;
    }
  }
  *next = p;
  return (cp < 0x80) ? 8 : 16;
}

VOID UiWrapText(GFX_RECT r, const char *text, UINT32 color, UINT32 scale)
{
  char line[320];
  char tail[320];
  UINT32 y = r.Y;
  UINT32 len = 0;
  UINT32 lineW = 0;
  INT32  spaceAt = -1;
  const UINT8 *p;

  text = L(text);
  p = (const UINT8 *)text;
  line[0] = 0;
  while (*p) {
    const UINT8 *next;
    UINT32 cw;
    UINT32 i;

    if (*p == '\n') {
      GfxText(r.X, y, line, color, scale);
      y += 18 * scale;
      len = 0;
      lineW = 0;
      spaceAt = -1;
      line[0] = 0;
      p++;
      if (y + 16 * scale > r.Y + r.H) {
        return;
      }
      continue;
    }

    cw = CharWidthAt((const char *)p, &next) * scale;
    if (lineW + cw > r.W && len) {
      if (spaceAt >= 0) {
        /* break at the last word boundary and carry the tail over */
        UINT32 n = len - (UINT32)spaceAt - 1;
        for (i = 0; i < n; i++) {
          tail[i] = line[spaceAt + 1 + i];
        }
        tail[n] = 0;
        line[spaceAt] = 0;
        GfxText(r.X, y, line, color, scale);
        y += 18 * scale;
        if (y + 16 * scale > r.Y + r.H) {
          return;
        }
        for (i = 0; i < n; i++) {
          line[i] = tail[i];
        }
        len = n;
        line[len] = 0;
        lineW = GfxTextWidth(line, scale);
        spaceAt = -1;
        continue;
      }
      GfxText(r.X, y, line, color, scale);
      y += 18 * scale;
      if (y + 16 * scale > r.Y + r.H) {
        return;
      }
      len = 0;
      lineW = 0;
      line[0] = 0;
      continue;
    }

    {
      UINT32 n = (UINT32)(next - (const UINT8 *)p);
      if (len + n + 1 >= sizeof(line)) {
        GfxText(r.X, y, line, color, scale);
        return;
      }
      if (*p == ' ') {
        spaceAt = (INT32)len;
      }
      for (i = 0; i < n; i++) {
        line[len++] = (char)p[i];
      }
      line[len] = 0;
      lineW += cw;
      p = next;
    }
  }
  if (len) {
    GfxText(r.X, y, line, color, scale);
  }
}

/* --------------------------------------------------------------- modals */
VOID UiModal(GFX_RECT r, const char *title, const char *const *lines, UINT32 lineCount, UINT32 tone)
{
  UINT32 i;
  UINT32 y;
  GfxBlendRect((GFX_RECT){ 0, 0, gScreen.Width, gScreen.Height }, GFX_RGB(0x0F, 0x17, 0x2A), 0x60);
  GfxFillRound(r, 12, gTheme.Panel);
  GfxRoundOutline(r, 12, gTheme.Border);
  GfxFillRound((GFX_RECT){ r.X + 16, r.Y + 18, 4, 18 }, 2, UiLevelColor(tone));
  GfxText(r.X + 28, r.Y + 18, title, gTheme.Text, 1);
  UiDivider((GFX_RECT){ r.X + 16, r.Y + 46, r.W - 32, 1 });
  y = r.Y + 60;
  for (i = 0; i < lineCount; i++) {
    if (lines[i] && lines[i][0]) {
      GfxText(r.X + 20, y, lines[i], gTheme.SubText, 1);
    }
    y += 20;
  }
}

UINT32 UiKeyCol(VOID)
{
  return ILang() == LANG_ZH ? 84 : 118;
}

UINT32 UiDialogChoose(const char *title, const char *const *options, UINT32 count, UINT32 initial)
{
  UINT32 sel = initial < count ? initial : 0;
  GFX_RECT content = UiContentRect();
  for (;;) {
    GFX_RECT panel;
    UINT32 i;
    EFI_INPUT_KEY key;
    UINT32 w = content.W > 420 ? 420 : content.W - 20;
    UINT32 h = 90 + count * 28;
    panel.X = content.X + (content.W - w) / 2;
    panel.Y = content.Y + (content.H > h ? (content.H - h) / 2 : 0);
    panel.W = w;
    panel.H = h > content.H ? content.H - 10 : h;

    UiDrawEverything();
    GfxResetClip();
    GfxBlendRect((GFX_RECT){ 0, 0, gScreen.Width, gScreen.Height }, GFX_RGB(0x0F, 0x17, 0x2A), 0x60);
    GfxFillRound(panel, 12, gTheme.Panel);
    GfxRoundOutline(panel, 12, gTheme.Border);
    GfxText(panel.X + 20, panel.Y + 18, title, gTheme.Text, 1);
    UiDivider((GFX_RECT){ panel.X + 16, panel.Y + 44, panel.W - 32, 1 });
    for (i = 0; i < count; i++) {
      GFX_RECT row = { panel.X + 12, panel.Y + 56 + i * 28, panel.W - 24, 26 };
      UiListRow(row, options[i], 0, gTheme.Text, gTheme.SubText, (BOOLEAN)(i == sel), FALSE);
    }
    GfxText(panel.X + 20, panel.Y + panel.H - 26, "↑↓ 或点击选择    Enter 确认    Esc 取消",
            gTheme.Muted, 1);
    GfxPresent();

    /* nothing on the dimmed page underneath is clickable any more; the panel
       itself stands for cancel so a stray click outside is harmless */
    UiHitClear();
    UiHitAdd(panel, DialogCancel, 0);
    for (i = 0; i < count; i++) {
      UiHitAdd((GFX_RECT){ panel.X + 12, panel.Y + 56 + i * 28, panel.W - 24, 26 },
               DialogPickOption, i);
    }
    MouseForget();
    UiMouseDraw();

    for (;;) {
      if (gDialogPicked) {
        BOOLEAN cancelled = gDialogCancel;
        UINT32 choice = gDialogChoice;
        gDialogPicked = FALSE;
        gDialogCancel = FALSE;
        if (cancelled) {
          return count;
        }
        return choice;
      }
      key = WaitKey(30);
      if (key.ScanCode || key.UnicodeChar) {
        break;
      }
    }
    if (key.ScanCode == SCAN_ESC) {
      return count;
    }
    if (key.ScanCode == SCAN_UP) {
      sel = sel ? sel - 1 : count - 1;
    } else if (key.ScanCode == SCAN_DOWN) {
      sel = (sel + 1) % count;
    } else if (key.UnicodeChar == '\r' || key.UnicodeChar == '\n') {
      return sel;
    }
  }
}

BOOLEAN UiConfirm(const char *title, const char *message, const char *acceptKey)
{
  GFX_RECT content = UiContentRect();
  char typed[32];
  UINT32 len = 0;
  UINT32 keyLen = (UINT32)UiStrLen(acceptKey);

  typed[0] = 0;
  for (;;) {
    GFX_RECT panel;
    UINT32 w = content.W > 520 ? 520 : content.W - 20;
    UINT32 h = 214;
    char line[128];
    EFI_INPUT_KEY key;

    panel.X = content.X + (content.W - w) / 2;
    panel.Y = content.Y + (content.H > h ? (content.H - h) / 2 : 0);
    panel.W = w;
    panel.H = h;

    UiDrawEverything();
    GfxResetClip();
    GfxBlendRect((GFX_RECT){ 0, 0, gScreen.Width, gScreen.Height }, GFX_RGB(0x0F, 0x17, 0x2A), 0x60);
    GfxFillRound(panel, 12, gTheme.Panel);
    GfxRoundOutline(panel, 12, gTheme.Border);
    GfxFillRound((GFX_RECT){ panel.X + 16, panel.Y + 18, 4, 18 }, 2, gTheme.Warn);
    GfxText(panel.X + 28, panel.Y + 18, title, gTheme.Text, 1);
    UiDivider((GFX_RECT){ panel.X + 16, panel.Y + 46, panel.W - 32, 1 });
    UiWrapText((GFX_RECT){ panel.X + 20, panel.Y + 58, panel.W - 40, 96 }, message, gTheme.SubText, 1);

    UiStrPrintf(line, sizeof(line), "请键入 %s 确认：", acceptKey);
    GfxText(panel.X + 20, panel.Y + 158, line, gTheme.SubText, 1);
    GfxFillRound((GFX_RECT){ panel.X + 20, panel.Y + 178, 180, 26 }, 6, gTheme.PanelAlt);
    GfxRoundOutline((GFX_RECT){ panel.X + 20, panel.Y + 178, 180, 26 }, 6, gTheme.Border);
    if (typed[0]) {
      GfxText(panel.X + 30, panel.Y + 183, typed, gTheme.Text, 1);
    } else {
      GfxText(panel.X + 30, panel.Y + 183, acceptKey, gTheme.Muted, 1);
    }
    GfxText(panel.X + 220, panel.Y + 183, "Enter 确认 · Esc 取消", gTheme.Muted, 1);
    GfxPresent();

    UiHitClear();
    UiHitAdd((GFX_RECT){ 0, 0, gScreen.Width, gScreen.Height }, DialogCancel, 0);
    UiHitAdd(panel, DialogAccept, 0);
    MouseForget();
    UiMouseDraw();

    for (;;) {
      if (gDialogPicked) {
        BOOLEAN cancelled = gDialogCancel;
        gDialogPicked = FALSE;
        gDialogCancel = FALSE;
        if (cancelled) {
          return FALSE;
        }
        return (BOOLEAN)(UiStrLen(typed) == keyLen && memcmp(typed, acceptKey, keyLen) == 0);
      }
      key = WaitKey(30);
      if (key.ScanCode || key.UnicodeChar) {
        break;
      }
    }
    if (key.ScanCode == SCAN_ESC) {
      return FALSE;
    }
    if (key.UnicodeChar == '\r' || key.UnicodeChar == '\n') {
      return (BOOLEAN)(UiStrLen(typed) == keyLen && memcmp(typed, acceptKey, keyLen) == 0);
    }
    if (key.UnicodeChar == 0x08) {
      if (len) {
        typed[--len] = 0;
      }
      continue;
    }
    if (key.UnicodeChar >= 0x20 && key.UnicodeChar < 0x7F && len + 1 < sizeof(typed)) {
      typed[len++] = (char)key.UnicodeChar;
      typed[len] = 0;
    }
  }
}

BOOLEAN UiInputLine(const char *title, const char *prompt, char *buffer, UINTN cap)
{
  GFX_RECT content = UiContentRect();
  UINTN len = UiStrLen(buffer);
  for (;;) {
    GFX_RECT panel;
    UINT32 w = content.W > 460 ? 460 : content.W - 20;
    UINT32 h = 150;
    EFI_INPUT_KEY key;
    panel.X = content.X + (content.W - w) / 2;
    panel.Y = content.Y + (content.H > h ? (content.H - h) / 2 : 0);
    panel.W = w;
    panel.H = h;

    UiDrawEverything();
    GfxResetClip();
    GfxBlendRect((GFX_RECT){ 0, 0, gScreen.Width, gScreen.Height }, GFX_RGB(0x0F, 0x17, 0x2A), 0x60);
    GfxFillRound(panel, 12, gTheme.Panel);
    GfxRoundOutline(panel, 12, gTheme.Border);
    GfxText(panel.X + 20, panel.Y + 18, title, gTheme.Text, 1);
    UiDivider((GFX_RECT){ panel.X + 16, panel.Y + 44, panel.W - 32, 1 });
    GfxText(panel.X + 20, panel.Y + 58, prompt, gTheme.SubText, 1);
    GfxFillRound((GFX_RECT){ panel.X + 20, panel.Y + 84, panel.W - 40, 28 }, 6, gTheme.PanelAlt);
    GfxRoundOutline((GFX_RECT){ panel.X + 20, panel.Y + 84, panel.W - 40, 28 }, 6, gTheme.Border);
    GfxText(panel.X + 30, panel.Y + 90, buffer, gTheme.Text, 1);
    GfxText(panel.X + 20, panel.Y + 122, "Enter 确认    Esc 取消", gTheme.Muted, 1);
    GfxPresent();

    key = WaitKey(0);
    if (key.ScanCode == SCAN_ESC) {
      return FALSE;
    }
    if (key.UnicodeChar == '\r' || key.UnicodeChar == '\n') {
      return TRUE;
    }
    if (key.UnicodeChar == 0x08) {
      if (len) {
        buffer[--len] = 0;
      }
      continue;
    }
    if (key.UnicodeChar >= 0x20 && key.UnicodeChar < 0x7F && len + 1 < cap) {
      buffer[len++] = (char)key.UnicodeChar;
      buffer[len] = 0;
    }
  }
}

/* ------------------------------------------------------------ file save */
/* Shrink an open file to Length bytes. Firmware FAT drivers support this
   through SetInfo; when it is unsupported the file simply keeps its old tail. */
static VOID TruncateFile(EFI_FILE_PROTOCOL *file, UINTN length)
{
  UINT8 info[sizeof(EFI_FILE_INFO) + 128];
  UINTN size = sizeof(info);
  EFI_FILE_INFO *fi = (EFI_FILE_INFO *)info;

  if (file == 0 || file->GetInfo == 0 || file->SetInfo == 0) {
    return;
  }
  if (EFI_ERROR(file->GetInfo(file, (EFI_GUID *)&gEfiFileInfoGuid, &size, info))) {
    return;
  }
  if (fi->FileSize == length) {
    return;
  }
  fi->FileSize = length;
  fi->Size = size;
  (VOID)file->SetInfo(file, (EFI_GUID *)&gEfiFileInfoGuid, size, info);
}

EFI_STATUS UiWriteFile(EFI_FILE_PROTOCOL *root, const char *name, const char *data, UINTN len)
{
  EFI_FILE_PROTOCOL *file = 0;
  CHAR16 wname[64];
  UINTN i;
  EFI_STATUS status;

  if (root == 0) {
    return EFI_NOT_FOUND;
  }
  for (i = 0; i + 1 < sizeof(wname) / sizeof(wname[0]) && name[i]; i++) {
    wname[i] = (CHAR16)name[i];
  }
  wname[i] = 0;
  status = root->Open(root, &file, wname, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
  if (EFI_ERROR(status)) {
    return status;
  }
  /* rewrite from the first byte, then shrink the file to the new length: the
     FAT driver keeps the old size otherwise, which leaves a stale tail behind */
  file->SetPosition(file, 0);
  {
    UINTN wrote = len;
    status = file->Write(file, &wrote, (VOID *)data);
  }
  TruncateFile(file, len);
  file->Flush(file);
  file->Close(file);
  return status;
}

/* Report export is implemented in report.c; these forwarders keep the shell
   independent of the module. */
extern EFI_STATUS ReportExportAll(UI_APP *app);
extern EFI_STATUS ReportExportSystemInfo(UI_APP *app);
extern EFI_STATUS ReportExportTests(UI_APP *app);
extern EFI_STATUS ReportExportStress(UI_APP *app);
extern EFI_STATUS ReportExportPlain(UI_APP *app);
extern UINT32 ReportExportBundle(UI_APP *app, UINT32 *failed);

static VOID ReportStatus(EFI_STATUS status, const char *name)
{
  char line[160];
  if (EFI_ERROR(status)) {
    UiStrPrintf(line, sizeof(line), "报告导出失败：启动卷不可写（%s）", name);
    UiSetStatus(3, line);
  } else {
    UiStrPrintf(line, sizeof(line), "报告已导出并回读校验：%s", name);
    UiSetStatus(1, line);
  }
  UiRedraw();
}

/* F2 picker: one file per topic, or the whole bundle in one go. */
static VOID ExportReport(VOID)
{
  static const char *const options[] = {
    "全部（完整 HTML 报告）",
    "仅系统信息（HTML）",
    "仅检测结果（HTML）",
    "仅压力测试（HTML）",
    "纯文本摘要（TXT）",
    "以上全部文件（5 个）",
  };
  UINT32 sel = UiDialogChoose("导出诊断报告到启动卷", options, 6, 0);

  switch (sel) {
  case 0:
    ReportStatus(ReportExportAll(&gApp), "UCTS-REPORT.HTM");
    break;
  case 1:
    ReportStatus(ReportExportSystemInfo(&gApp), "UCTS-SYSINFO.HTM");
    break;
  case 2:
    ReportStatus(ReportExportTests(&gApp), "UCTS-TEST.HTM");
    break;
  case 3:
    ReportStatus(ReportExportStress(&gApp), "UCTS-STRESS.HTM");
    break;
  case 4:
    ReportStatus(ReportExportPlain(&gApp), "UCTS-REPORT.TXT");
    break;
  case 5: {
    UINT32 failed = 0;
    UINT32 ok = ReportExportBundle(&gApp, &failed);
    char line[160];
    UiStrPrintf(line, sizeof(line), "已导出 %u 个报告文件 · 失败 %u 个（启动卷根目录）", ok,
                failed);
    UiSetStatus(failed ? 3 : 1, line);
    UiRedraw();
    break;
  }
  default:
    UiRedraw();
    break;
  }
}
