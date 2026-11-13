/* gfx.h - software 2D drawing on top of GOP, with a RAM back buffer. */
#ifndef UCTS_GFX_H
#define UCTS_GFX_H

#include "uefi.h"

#define GFX_RGB(r, g, b) ((UINT32)(((r) << 16) | ((g) << 8) | (b)))
#define GFX_ARGB(a, c)   ((UINT32)(((UINT32)(a) << 24) | ((c) & 0x00FFFFFF)))

typedef struct {
  UINT32 *Pixels;
  UINT32  Width;
  UINT32  Height;
  UINT32  Stride;   /* pixels per scan line */
} GFX_SURFACE;

typedef struct {
  UINT32 X, Y, W, H;
} GFX_RECT;

extern GFX_SURFACE gScreen;

EFI_STATUS GfxInit(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *ST);
VOID       GfxShutdown(VOID);
VOID       GfxPresent(VOID);
VOID       GfxPresentRect(GFX_RECT r);

/* clip region (inclusive-exclusive), reset by GfxResetClip */
VOID GfxResetClip(VOID);
VOID GfxSetClip(GFX_RECT r);

/* primitives */
VOID GfxClear(UINT32 color);
VOID GfxFillRect(GFX_RECT r, UINT32 color);
VOID GfxBlendRect(GFX_RECT r, UINT32 color, UINT8 alpha);
VOID GfxFillRound(GFX_RECT r, UINT32 radius, UINT32 color);
VOID GfxRoundOutline(GFX_RECT r, UINT32 radius, UINT32 color);
VOID GfxFrameRect(GFX_RECT r, UINT32 color);
VOID GfxHLine(UINT32 x0, UINT32 x1, UINT32 y, UINT32 color);
VOID GfxVLine(UINT32 x, UINT32 y0, UINT32 y1, UINT32 color);
VOID GfxFillCircle(UINT32 cx, UINT32 cy, UINT32 radius, UINT32 color);
VOID GfxCircle(UINT32 cx, UINT32 cy, UINT32 radius, UINT32 color);
VOID GfxDonut(UINT32 cx, UINT32 cy, UINT32 rOuter, UINT32 rInner, UINT32 color,
              UINT32 startDeg, UINT32 endDeg);
VOID GfxGradientH(GFX_RECT r, UINT32 c0, UINT32 c1);
VOID GfxGradientV(GFX_RECT r, UINT32 c0, UINT32 c1);
VOID GfxPutPixel(UINT32 x, UINT32 y, UINT32 color);

/* text (UTF-8 input, 8-bit antialiased blending) */
UINT32 GfxTextHeight(UINT32 scale);
UINT32 GfxTextWidth(const char *text, UINT32 scale);
VOID   GfxText(UINT32 x, UINT32 y, const char *text, UINT32 color, UINT32 scale);
/* Draws text clipped to a width, appending an ellipsis when needed. */
VOID   GfxTextClipped(UINT32 x, UINT32 y, UINT32 maxW, const char *text, UINT32 color, UINT32 scale);
VOID   GfxTextRight(UINT32 right, UINT32 y, const char *text, UINT32 color, UINT32 scale);
VOID   GfxTextCenter(UINT32 x, UINT32 w, UINT32 y, const char *text, UINT32 color, UINT32 scale);

/* helpers */
CHAR16 *GfxUtf8ToUtf16(EFI_BOOT_SERVICES *BS, const char *src, UINTN *Len);
VOID    GfxUtf8EncodeCopy(char *dst, UINTN dstSize, const char *src);
UINT32  GfxStrLen(const char *s);
INT32   GfxStrCmp(const char *a, const char *b);

/* formatting helpers (no libc) */
VOID GfxFormatU64(char *buf, UINT64 value);
VOID GfxFormatHex(char *buf, UINT64 value, UINT32 digits);
VOID GfxAppendStr(char *dst, UINTN cap, const char *src);
VOID GfxFormatSize(char *buf, UINTN cap, UINT64 bytes);

#endif
