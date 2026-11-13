/* ui.h - theme, widgets and page framework. */
#ifndef UCTS_UI_H
#define UCTS_UI_H

#include "uefi.h"
#include "gfx.h"
#include "sysinfo.h"
#include "i18n.h"

#define UI_HEADER_H  56
#define UI_SIDEBAR_W 190
#define UI_FOOTER_H  26
#define UI_PAD       12

typedef struct {
  UINT32 Bg;
  UINT32 Panel;
  UINT32 PanelAlt;
  UINT32 Border;
  UINT32 Text;
  UINT32 SubText;
  UINT32 Muted;
  UINT32 Primary;
  UINT32 PrimaryDark;
  UINT32 PrimarySoft;
  UINT32 OnPrimary;
  UINT32 Ok;
  UINT32 OkSoft;
  UINT32 Warn;
  UINT32 WarnSoft;
  UINT32 Crit;
  UINT32 CritSoft;
  UINT32 Idle;
  UINT32 IdleSoft;
  UINT32 HeaderBg;
  UINT32 HeaderText;
  UINT32 SidebarBg;
  UINT32 SidebarSel;
  UINT32 SidebarSelText;
  UINT32 Grid;
  UINT32 Chart1;
  UINT32 Chart2;
} UI_THEME;

extern UI_THEME gTheme;

typedef struct {
  UINT32 Sel;
  UINT32 Scroll;
  UINT32 Count;
  UINT32 Visible;
} UI_LIST;

typedef struct {
  EFI_HANDLE        ImageHandle;
  EFI_SYSTEM_TABLE *ST;
  EFI_BOOT_SERVICES *BS;
  SYS_INFO          Sys;
  UINT32            Page;
  UINT32            Nav;
  BOOLEAN           Quit;
  BOOLEAN           Redraw;
  char              Status[192];
  UINT64            StatusTsc;
  UINT32            StatusLevel;   /* 0 info, 1 ok, 2 warn, 3 error */
  UINT32            Frame;
} UI_APP;

extern UI_APP gApp;
extern volatile BOOLEAN gStopRequested;
extern volatile BOOLEAN gBusy;

typedef struct {
  const char *Title;
  const char *Short;
  const char *Hints;
  VOID     (*Enter)(VOID);
  VOID     (*Leave)(VOID);
  VOID     (*Draw)(GFX_RECT content);
  BOOLEAN  (*Key)(EFI_INPUT_KEY key);
  /* mouse: move the page's own selection to this row; 0 when the page has no
     clickable rows */
  VOID     (*Point)(UINT32 row);
} UI_PAGE;

extern const UI_PAGE gPages[];
extern const UINT32   gPageCount;

/* ---- lifecycle ---- */
EFI_STATUS UiInit(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *ST);
VOID       UiRun(VOID);
VOID       UiSwitchPage(UINT32 page);
UINT32     UiPageCount(VOID);
VOID       UiRedraw(VOID);
VOID       UiDrawEverything(VOID);
GFX_RECT   UiContentRect(VOID);
VOID       UiSetStatus(UINT32 level, const char *text);
VOID       UiPresentAll(VOID);
/* Processes pending keys without blocking; used by long running tests.
   Returns TRUE when the user asked to stop the running operation. */
BOOLEAN    UiPump(VOID);
UINT64     UiNowMs(VOID);
VOID       UiTopBarClockTick(VOID);

/* ---- text helpers ---- */
VOID UiStrPrintf(char *dst, UINTN cap, const char *fmt, ...);
UINT32 UiStrLen(const char *s);
int  UiStrIcmp(const char *a, const char *b);
VOID UiFormatTime(char *buf, UINTN cap);

/* ---- widgets ---- */
UINT32 UiLevelColor(UINT32 level);
UINT32 UiLevelSoft(UINT32 level);
VOID UiCard(GFX_RECT r, const char *title, const char *rightText);
GFX_RECT UiCardInner(GFX_RECT card);
VOID UiSectionHeader(GFX_RECT r, const char *title, const char *rightText);
VOID UiBanner(GFX_RECT r, const char *title, const char *meta, UINT32 color0, UINT32 color1);
VOID UiProgressBar(GFX_RECT r, UINT32 permille, UINT32 color, const char *label);
VOID UiMiniBar(GFX_RECT r, UINT32 permille, UINT32 color);
VOID UiHealthDonut(GFX_RECT r, UINT32 ok, UINT32 warn, UINT32 crit, UINT32 off, const char *centerLabel);
VOID UiStatTile(GFX_RECT r, UINT32 color, const char *label, UINT32 value);
VOID UiPill(GFX_RECT r, UINT32 color, const char *text);

/* checkbox with a real check mark (14x14 recommended) */
VOID UiCheck(GFX_RECT box, BOOLEAN on);
VOID UiKeyHint(UINT32 x, UINT32 y, const char *key, const char *label);
VOID UiTableHeader(GFX_RECT r, const char *const *cols, const UINT32 *widths, UINT32 count);
VOID UiTableRow(GFX_RECT r, const char *const *cells, const UINT32 *widths, UINT32 count,
                UINT32 textColor, BOOLEAN selected, BOOLEAN zebra);
VOID UiListRow(GFX_RECT r, const char *left, const char *right, UINT32 leftColor,
               UINT32 rightColor, BOOLEAN selected, BOOLEAN zebra);
VOID UiScrollbar(GFX_RECT r, UINT32 first, UINT32 visible, UINT32 total);
VOID UiIcon(UINT32 kind, UINT32 x, UINT32 y, UINT32 size, UINT32 color);
VOID UiWrapText(GFX_RECT r, const char *text, UINT32 color, UINT32 scale);
VOID UiDivider(GFX_RECT r);
VOID UiModal(GFX_RECT r, const char *title, const char *const *lines, UINT32 lineCount, UINT32 tone);
/* Width reserved for the label column of a key/value grid: English labels are
   wider than the Chinese originals, so the column follows the language. */
UINT32 UiKeyCol(VOID);

UINT32 UiDialogChoose(const char *title, const char *const *options, UINT32 count, UINT32 initial);
BOOLEAN UiConfirm(const char *title, const char *message, const char *acceptKey);
BOOLEAN UiInputLine(const char *title, const char *prompt, char *buffer, UINTN cap);

/* ---- pointer hit regions ----
   Every frame registers the rectangles that can be clicked; a click then goes
   through the table instead of pages second-guessing coordinates. The table is
   rebuilt by UiDrawEverything and by any modal overlay that draws on top. */
typedef VOID (*UI_HIT_FN)(UINT32 id);

VOID    UiHitClear(VOID);
VOID    UiHitAdd(GFX_RECT r, UI_HIT_FN fn, UINT32 id);
BOOLEAN UiHitTest(UINT32 x, UINT32 y, UI_HIT_FN *fn, UINT32 *id);
UINT32  UiHitCount(VOID);

/* sidebar entry and page row helpers (row uses the current page's Point/Key) */
VOID    UiHitNav(GFX_RECT r, UINT32 page);
VOID    UiHitRow(GFX_RECT r, UINT32 row);
VOID    UiHitRowBox(GFX_RECT r, UINT32 row);

/* a scrollbar that can be used with the pointer: click above or below the thumb
   to page, press the thumb and drag it. The setter receives the new first row. */
typedef VOID (*UI_SCROLL_FN)(UINT32 first, UINT32 visible);
VOID    UiScrollbarScroll(GFX_RECT r, UINT32 first, UINT32 visible, UINT32 total,
                          UI_SCROLL_FN setFirst);

/* poll the pointer and dispatch a click or a wheel step; safe when there is no
   pointer at all */
VOID    UiMousePoll(VOID);
/* TRUE when something clickable is under the pointer (cursor turns blue) */
BOOLEAN UiMouseHot(VOID);
/* redraws the software cursor on top of the current frame */
VOID    UiMouseDraw(VOID);

/* list navigation helper: handles up/down/page keys, returns TRUE when consumed */
BOOLEAN UiListKey(UI_LIST *list, EFI_INPUT_KEY key);
VOID    UiListClamp(UI_LIST *list, UINT32 visible);

/* scans a volume tree for a file, used by the launcher and report writer */
EFI_STATUS UiWriteFile(EFI_FILE_PROTOCOL *root, const char *name, const char *data, UINTN len);

#endif
