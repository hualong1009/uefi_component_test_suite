/* mouse.c - absolute (tablet / KVM) and relative (PS/2, USB mouse) pointers.

   The UI is a poll-and-redraw loop, so the pointer is polled the same way the
   keyboard is: read the state, move a software cursor, ask the hit registry
   what is underneath. Nothing here assumes an event callback. */
#include "ui.h"
#include "mouse.h"
#include "debug.h"

#define CUR_W 10
#define CUR_H 16

static EFI_ABSOLUTE_POINTER_PROTOCOL *gAbs;
static EFI_SIMPLE_POINTER_PROTOCOL   *gRel;
static BOOLEAN gPresent;
static BOOLEAN gHasPos;

static UINT32  gX = 200;
static UINT32  gY = 200;
static UINT32  gButtons;
static UINT32  gPrevButtons;
static UINT8   gClickPending;
static UINT8   gDoublePending;
static UINT32  gClickX, gClickY;
static UINT64  gLastClick;
static UINT64  gLastGap;
static INT32   gWheel;
static UINT32  gMoveCount;
static UINT64  gLastZ;
static UINT32  gClickCount;

/* relative pointer gain, in screen pixels per device count */
static INT32   gGainX = 1;
static INT32   gGainY = 1;

/* cursor backing store */
static UINT32  gSave[CUR_W * CUR_H];
static GFX_RECT gSaveRect;
static BOOLEAN gShown;

static const char *gArrow[CUR_H] = {
  "K.........",
  "KK........",
  "KWK.......",
  "KWWK......",
  "KWWWK.....",
  "KWWWWK....",
  "KWWWWWK...",
  "KWWWWWWK..",
  "KWWWWWWWK.",
  "KWWWWWWWWK",
  "KWWWWWKKKK",
  "KWWWWK....",
  "KWKWWK....",
  "KK.KWWK...",
  "K..KWWK...",
  "....KKK..."
};

static EFI_ABSOLUTE_POINTER_PROTOCOL *FindAbsolute(VOID)
{
  EFI_HANDLE *handles = 0;
  UINTN count = 0;
  UINTN i;
  EFI_ABSOLUTE_POINTER_PROTOCOL *found = 0;

  if (EFI_ERROR(gApp.BS->LocateHandleBuffer(2, (EFI_GUID *)&gEfiAbsolutePointerProtocolGuid, 0,
                                            &count, &handles)) ||
      !handles) {
    return 0;
  }
  for (i = 0; i < count && !found; i++) {
    EFI_ABSOLUTE_POINTER_PROTOCOL *p = 0;
    if (!EFI_ERROR(gApp.BS->HandleProtocol(handles[i],
                                           (EFI_GUID *)&gEfiAbsolutePointerProtocolGuid,
                                           (VOID **)&p)) &&
        p && p->Mode && p->Mode->AbsoluteMaxX > p->Mode->AbsoluteMinX) {
      found = p;
    }
  }
  gApp.BS->FreePool(handles);
  return found;
}

static EFI_SIMPLE_POINTER_PROTOCOL *FindRelative(VOID)
{
  EFI_HANDLE *handles = 0;
  UINTN count = 0;
  UINTN i;
  EFI_SIMPLE_POINTER_PROTOCOL *found = 0;

  if (EFI_ERROR(gApp.BS->LocateHandleBuffer(2, (EFI_GUID *)&gEfiSimplePointerProtocolGuid, 0,
                                           &count, &handles)) ||
      !handles) {
    return 0;
  }
  for (i = 0; i < count && !found; i++) {
    EFI_SIMPLE_POINTER_PROTOCOL *p = 0;
    if (!EFI_ERROR(gApp.BS->HandleProtocol(handles[i],
                                           (EFI_GUID *)&gEfiSimplePointerProtocolGuid,
                                           (VOID **)&p)) &&
        p && p->Mode) {
      found = p;
    }
  }
  gApp.BS->FreePool(handles);
  return found;
}

VOID MouseInit(VOID)
{
  gAbs = 0;
  gRel = 0;
  gPresent = FALSE;

  /* both may exist: a server can have a BMC tablet and a PS/2 or USB mouse,
     and whichever the user moves should drive the same cursor */
  gAbs = FindAbsolute();
  gRel = FindRelative();

  if (gRel) {
    /* ResolutionX counts per millimetre; a 400 count crossing of a 300 mm
       screen is a comfortable gain, clamped so odd report rates stay usable */
    UINT32 resX = gRel->Mode->ResolutionX ? (UINT32)gRel->Mode->ResolutionX : 4;
    UINT32 resY = gRel->Mode->ResolutionY ? (UINT32)gRel->Mode->ResolutionY : resX;
    gGainX = (INT32)(gScreen.Width / (resX * 120));
    gGainY = (INT32)(gScreen.Height / (resY * 120));
    if (gGainX < 1) {
      gGainX = 1;
    }
    if (gGainY < 1) {
      gGainY = 1;
    }
    if (gGainX > 8) {
      gGainX = 8;
    }
    if (gGainY > 8) {
      gGainY = 8;
    }
  }

  gPresent = (BOOLEAN)(gAbs || gRel);
  gX = gScreen.Width / 2;
  gY = gScreen.Height / 2;
  gHasPos = FALSE;
  if (gPresent) {
    if (gAbs) {
      gAbs->Reset(gAbs, FALSE);
    }
    if (gRel) {
      gRel->Reset(gRel, FALSE);
    }
    DbgStr("mouse: absolute=");
    DbgDec(gAbs ? 1 : 0);
    DbgStr(" relative=");
    DbgDec(gRel ? 1 : 0);
    DbgStr(" gain=");
    DbgDec((UINT32)gGainX);
    DbgStr(" maxz=");
    DbgDec(gAbs ? (UINT32)gAbs->Mode->AbsoluteMaxZ : 0);
    DbgStr("\n");
  }
}

VOID MouseStats(BOOLEAN *absFound, BOOLEAN *relFound, UINT32 *moves,
                UINT32 *clicks, UINT32 *hits)
{
  if (absFound) {
    *absFound = (BOOLEAN)(gAbs != 0);
  }
  if (relFound) {
    *relFound = (BOOLEAN)(gRel != 0);
  }
  if (moves) {
    *moves = gMoveCount;
  }
  if (clicks) {
    *clicks = gClickCount;
  }
  if (hits) {
    *hits = UiHitCount();
  }
}

BOOLEAN MousePresent(VOID)
{
  return gPresent;
}

UINT32 MouseX(VOID)
{
  return gX;
}

UINT32 MouseY(VOID)
{
  return gY;
}

UINT32 MouseButtons(VOID)
{
  return gButtons;
}

static UINT32 AbsoluteToScreen(UINT64 value, UINT64 min, UINT64 max, UINT32 span)
{
  UINT64 range;

  if (max <= min) {
    return 0;
  }
  if (value < min) {
    value = min;
  }
  if (value > max) {
    value = max;
  }
  range = max - min;
  return (UINT32)(((value - min) * (span ? span - 1 : 0)) / range);
}

BOOLEAN MousePoll(VOID)
{
  BOOLEAN changed = FALSE;
  UINT32 buttons = gButtons;

  if (!gPresent) {
    return FALSE;
  }

  if (gAbs) {
    EFI_ABSOLUTE_POINTER_STATE st;
    gApp.BS->SetMem(&st, sizeof(st), 0);
    if (!EFI_ERROR(gAbs->GetState(gAbs, &st))) {
      UINT32 x = AbsoluteToScreen(st.CurrentX, gAbs->Mode->AbsoluteMinX,
                                 gAbs->Mode->AbsoluteMaxX, gScreen.Width);
      UINT32 y = AbsoluteToScreen(st.CurrentY, gAbs->Mode->AbsoluteMinY,
                                 gAbs->Mode->AbsoluteMaxY, gScreen.Height);
      if (x != gX || y != gY) {
        gX = x;
        gY = y;
        gHasPos = TRUE;
        changed = TRUE;
        gMoveCount++;
      }
      /* a wheel on an absolute device shows up as a Z step; devices that
         use Z for pressure report a wide range, so only a narrow one is a wheel */
      if (gAbs->Mode->AbsoluteMaxZ && gAbs->Mode->AbsoluteMaxZ <= 4 &&
          st.CurrentZ != gLastZ) {
        gWheel += st.CurrentZ > gLastZ ? 1 : -1;
        gLastZ = st.CurrentZ;
      }
      buttons = (st.ActiveButtons & 0x01) ? MOUSE_LEFT : 0;
      if (st.ActiveButtons & 0x02) {
        buttons |= MOUSE_RIGHT;
      }
    }
  }

  if (gRel) {
    EFI_SIMPLE_POINTER_STATE st;
    gApp.BS->SetMem(&st, sizeof(st), 0);
    if (!EFI_ERROR(gRel->GetState(gRel, &st))) {
      if (st.RelativeMovementX || st.RelativeMovementY) {
        INT32 nx = (INT32)gX + st.RelativeMovementX * gGainX;
        INT32 ny = (INT32)gY + st.RelativeMovementY * gGainY;
        if (nx < 0) {
          nx = 0;
        }
        if (ny < 0) {
          ny = 0;
        }
        if ((UINT32)nx > gScreen.Width - 1) {
          nx = (INT32)gScreen.Width - 1;
        }
        if ((UINT32)ny > gScreen.Height - 1) {
          ny = (INT32)gScreen.Height - 1;
        }
        gX = (UINT32)nx;
        gY = (UINT32)ny;
        gHasPos = TRUE;
        changed = TRUE;
      }
      if (st.RelativeMovementZ) {
        gWheel += st.RelativeMovementZ > 0 ? -1 : 1;
      }
      if (!gAbs) {
        buttons = 0;
        if (st.LeftButton) {
          buttons |= MOUSE_LEFT;
        }
        if (st.RightButton) {
          buttons |= MOUSE_RIGHT;
        }
      }
    }
  }

  if (buttons != gButtons) {
    gButtons = buttons;
    changed = TRUE;
    if ((buttons & MOUSE_LEFT) && !(gPrevButtons & MOUSE_LEFT)) {
      gClickX = gX;
      gClickY = gY;
      gClickPending = 1;
      gClickCount++;
      gLastGap = gLastClick ? SysMsSince(gLastClick) : 0;
      if (gLastClick && gLastGap < 900) {
        gDoublePending = 1;
      }
      gLastClick = SysTsc();
    }
    gPrevButtons = buttons;
  }
  return changed;
}

BOOLEAN MouseTakeClick(UINT32 *x, UINT32 *y, BOOLEAN *isDouble)
{
  if (!gClickPending) {
    return FALSE;
  }
  gClickPending = 0;
  if (x) {
    *x = gClickX;
  }
  if (y) {
    *y = gClickY;
  }
  if (isDouble) {
    *isDouble = gDoublePending ? TRUE : FALSE;
  }
  gDoublePending = 0;
  return TRUE;
}

INT32 MouseTakeWheel(VOID)
{
  INT32 w = gWheel;
  gWheel = 0;
  return w;
}

/* ------------------------------------------------------------------ cursor */
VOID MouseHide(VOID)
{
  UINT32 row, col;

  if (!gShown) {
    return;
  }
  for (row = 0; row < gSaveRect.H; row++) {
    UINT32 *dst = gScreen.Pixels + (UINTN)(gSaveRect.Y + row) * gScreen.Stride + gSaveRect.X;
    const UINT32 *src = gSave + (UINTN)row * gSaveRect.W;
    for (col = 0; col < gSaveRect.W; col++) {
      dst[col] = src[col];
    }
  }
  GfxPresentRect(gSaveRect);
  gShown = FALSE;
}

VOID MouseForget(VOID)
{
  gShown = FALSE;
}

VOID MouseShow(BOOLEAN hot)
{
  UINT32 x = gX;
  UINT32 y = gY;
  UINT32 row, col;
  UINT32 white = hot ? gTheme.Primary : 0x00FFFFFFu;
  UINT32 outline = 0x000F172Au;

  if (!gPresent) {
    return;
  }
  if (x + CUR_W >= gScreen.Width) {
    x = gScreen.Width > CUR_W ? gScreen.Width - CUR_W - 1 : 0;
  }
  if (y + CUR_H >= gScreen.Height) {
    y = gScreen.Height > CUR_H ? gScreen.Height - CUR_H - 1 : 0;
  }
  gSaveRect = (GFX_RECT){ x, y, CUR_W, CUR_H };
  if (!gHasPos) {
    gShown = FALSE;
    return;
  }
  for (row = 0; row < CUR_H; row++) {
    UINT32 *dst = gScreen.Pixels + (UINTN)(y + row) * gScreen.Stride + x;
    UINT32 *save = gSave + (UINTN)row * CUR_W;
    for (col = 0; col < CUR_W; col++) {
      save[col] = dst[col];
      if (gArrow[row][col] == 'W') {
        dst[col] = white;
      } else if (gArrow[row][col] == 'K') {
        dst[col] = outline;
      }
    }
  }
  GfxPresentRect(gSaveRect);
  gShown = TRUE;
}
