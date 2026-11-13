/* gfx.c - software 2D renderer writing through GOP. */
#include "gfx.h"
#include "i18n.h"
#include "font.h"

static EFI_SYSTEM_TABLE             *gST;
static EFI_BOOT_SERVICES            *gBS;
static EFI_GRAPHICS_OUTPUT_PROTOCOL *gGop;
static UINT32                        gOrigMode;
static BOOLEAN                       gModeChanged;
static UINT32                        gPixelFormat;
static UINT8                         gLatinAdvance[95];

GFX_SURFACE gScreen;
static GFX_RECT gClip;

/* ------------------------------------------------------------- helpers */

static UINT32 MinU32(UINT32 a, UINT32 b) { return a < b ? a : b; }
static UINT32 MaxU32(UINT32 a, UINT32 b) { return a > b ? a : b; }

static UINT32 BlendPair(UINT32 dst, UINT32 src, UINT32 a)
{
  INT32 d0 = (INT32)(dst & 0xFF);
  INT32 s0 = (INT32)(src & 0xFF);
  INT32 d1 = (INT32)((dst >> 16) & 0xFF);
  INT32 s1 = (INT32)((src >> 16) & 0xFF);
  UINT32 o0 = (UINT32)(d0 + (((s0 - d0) * (INT32)a) >> 8)) & 0xFF;
  UINT32 o1 = (UINT32)(d1 + (((s1 - d1) * (INT32)a) >> 8)) & 0xFF;
  return o0 | (o1 << 16);
}

static UINT32 Blend(UINT32 dst, UINT32 src, UINT32 a)
{
  UINT32 rb = BlendPair(dst & 0xFF00FF, src & 0xFF00FF, a);
  UINT32 g  = BlendPair((dst >> 8) & 0xFF, (src >> 8) & 0xFF, a);
  return rb | (g << 8);
}

static BOOLEAN ClipPixel(UINT32 x, UINT32 y)
{
  return (x >= gClip.X && x < gClip.X + gClip.W && y >= gClip.Y && y < gClip.Y + gClip.H);
}

VOID GfxResetClip(VOID)
{
  gClip.X = 0;
  gClip.Y = 0;
  gClip.W = gScreen.Width;
  gClip.H = gScreen.Height;
}

VOID GfxSetClip(GFX_RECT r)
{
  UINT32 x0 = MaxU32(r.X, 0);
  UINT32 y0 = MaxU32(r.Y, 0);
  UINT32 x1 = MinU32(r.X + r.W, gScreen.Width);
  UINT32 y1 = MinU32(r.Y + r.H, gScreen.Height);
  gClip.X = x0;
  gClip.Y = y0;
  gClip.W = x1 > x0 ? x1 - x0 : 0;
  gClip.H = y1 > y0 ? y1 - y0 : 0;
}

/* ---------------------------------------------------------------- init */
static EFI_STATUS SelectMode(VOID)
{
  UINT32 bestMode = gGop->Mode->Mode;
  UINT32 bestScore = 0;
  UINT32 i;
  EFI_STATUS status = EFI_SUCCESS;

  for (i = 0; i < gGop->Mode->MaxMode; i++) {
    UINTN size = 0;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = 0;
    if (EFI_ERROR(gGop->QueryMode(gGop, i, &size, &info)) || info == 0) {
      continue;
    }
    if (info->PixelFormat == PixelBltOnly) {
      continue;
    }
    if (info->HorizontalResolution < 800 || info->VerticalResolution < 600) {
      continue;
    }
    if (info->HorizontalResolution > 1920 || info->VerticalResolution > 1200) {
      continue;
    }
    /* prefer the largest 4:3/16:9 mode that still fits in 1920x1200 */
    {
      UINT32 score = info->HorizontalResolution * info->VerticalResolution;
      if (info->HorizontalResolution == 1024 && info->VerticalResolution == 768) {
        score += 5000000; /* classic firmware panel: prefer it */
      }
      if (info->HorizontalResolution == 1280 && info->VerticalResolution == 800) {
        score += 4000000;
      }
      if (score > bestScore) {
        bestScore = score;
        bestMode = i;
      }
    }
  }
  if (bestMode != gGop->Mode->Mode) {
    gOrigMode = gGop->Mode->Mode;
    status = gGop->SetMode(gGop, bestMode);
    if (EFI_ERROR(status)) {
      return status;
    }
    gModeChanged = TRUE;
  }
  return EFI_SUCCESS;
}

EFI_STATUS GfxInit(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *ST)
{
  EFI_STATUS status;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info;
  UINTN i;

  gST = ST;
  gBS = ST->BootServices;
  status = gBS->LocateProtocol((EFI_GUID *)&gEfiGraphicsOutputProtocolGuid, 0, (VOID **)&gGop);
  if (EFI_ERROR(status) || gGop == 0) {
    return EFI_NOT_FOUND;
  }
  gOrigMode = gGop->Mode->Mode;
  SelectMode();
  info = gGop->Mode->Info;
  if (info == 0) {
    return EFI_DEVICE_ERROR;
  }
  gPixelFormat = info->PixelFormat;
  gScreen.Width  = info->HorizontalResolution;
  gScreen.Height = info->VerticalResolution;
  gScreen.Stride = gScreen.Width;
  status = gBS->AllocatePool(EfiLoaderData,
                             (UINTN)gScreen.Stride * gScreen.Height * sizeof(UINT32),
                             (VOID **)&gScreen.Pixels);
  if (EFI_ERROR(status)) {
    return status;
  }
  gBS->SetMem(gScreen.Pixels, (UINTN)gScreen.Stride * gScreen.Height * 4, 0);
  GfxResetClip();

  /* precompute proportional advances for the Latin font */
  for (i = 0; i < 95; i++) {
    UINT32 x, y, last = 0;
    const UINT8 *glyph = gFontLatinData[i];
    for (y = 0; y < FONT_LATIN_H; y++) {
      for (x = 0; x < FONT_LATIN_W; x++) {
        if (glyph[y * FONT_LATIN_W + x] && x + 1 > last) {
          last = x + 1;
        }
      }
    }
    gLatinAdvance[i] = (UINT8)(last ? last : 4);
  }
  (VOID)ImageHandle;
  return EFI_SUCCESS;
}

VOID GfxShutdown(VOID)
{
  const EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = gGop;
  if (gop && gModeChanged) {
    gop->SetMode((EFI_GRAPHICS_OUTPUT_PROTOCOL *)gop, gOrigMode);
    gModeChanged = FALSE;
  }
}

/* ------------------------------------------------------------ presents */
static inline void StorePixel(UINT32 *fb, UINT32 color)
{
  if (gPixelFormat == PixelBlueGreenRedReserved8BitPerColor) {
    /* memory order B,G,R: colour is R<<16|G<<8|B */
    *fb = color | 0xFF000000;
  } else if (gPixelFormat == PixelRedGreenBlueReserved8BitPerColor) {
    /* memory order R,G,B */
    *fb = ((color & 0xFF) << 16) | (color & 0xFF00) | ((color >> 16) & 0xFF) | 0xFF000000;
  } else if (gPixelFormat == PixelBitMask) {
    EFI_PIXEL_BITMASK m = gGop->Mode->Info->PixelInformation;
    UINT32 out = 0;
    UINT32 r = (color >> 16) & 0xFF, g = (color >> 8) & 0xFF, b = color & 0xFF;
    UINT32 sh, v;
    for (sh = 0; sh < 32; sh++) if ((m.RedMask >> sh) & 1) break;
    v = (r * (((m.RedMask >> sh) & 0xFF))) / 255;
    out |= (v << sh) & m.RedMask;
    for (sh = 0; sh < 32; sh++) if ((m.GreenMask >> sh) & 1) break;
    v = (g * (((m.GreenMask >> sh) & 0xFF))) / 255;
    out |= (v << sh) & m.GreenMask;
    for (sh = 0; sh < 32; sh++) if ((m.BlueMask >> sh) & 1) break;
    v = (b * (((m.BlueMask >> sh) & 0xFF))) / 255;
    out |= (v << sh) & m.BlueMask;
    *fb = out;
  } else {
    *fb = color;
  }
}

VOID GfxPresentRect(GFX_RECT r)
{
  UINT32 *fb = (UINT32 *)(UINTN)gGop->Mode->FrameBufferBase;
  UINTN fbStride = gGop->Mode->Info->PixelsPerScanLine;
  UINT32 x, y;
  UINT32 x0 = MaxU32(r.X, 0);
  UINT32 y0 = MaxU32(r.Y, 0);
  UINT32 x1 = MinU32(r.X + r.W, gScreen.Width);
  UINT32 y1 = MinU32(r.Y + r.H, gScreen.Height);

  if (gPixelFormat == PixelBltOnly || fb == 0) {
    gGop->Blt(gGop, gScreen.Pixels, EfiBltBufferToVideo, x0, y0, x0, y0, x1 - x0, y1 - y0,
              gScreen.Stride * sizeof(UINT32));
    return;
  }
  for (y = y0; y < y1; y++) {
    UINT32 *src = gScreen.Pixels + (UINTN)y * gScreen.Stride;
    UINT32 *dst = fb + (UINTN)y * fbStride;
    for (x = x0; x < x1; x++) {
      StorePixel(dst + x, src[x]);
    }
  }
}

VOID GfxPresent(VOID)
{
  GFX_RECT all = { 0, 0, gScreen.Width, gScreen.Height };
  GfxPresentRect(all);
}

/* ---------------------------------------------------------- primitives */
VOID GfxPutPixel(UINT32 x, UINT32 y, UINT32 color)
{
  if (!ClipPixel(x, y)) {
    return;
  }
  gScreen.Pixels[(UINTN)y * gScreen.Stride + x] = color;
}

VOID GfxClear(UINT32 color)
{
  UINTN count = (UINTN)gScreen.Stride * gScreen.Height;
  UINTN i;
  UINT32 *p = gScreen.Pixels;
  for (i = 0; i < count; i++) {
    p[i] = color;
  }
}

VOID GfxFillRect(GFX_RECT r, UINT32 color)
{
  UINT32 x0 = MaxU32(r.X, gClip.X);
  UINT32 y0 = MaxU32(r.Y, gClip.Y);
  UINT32 x1 = MinU32(r.X + r.W, gClip.X + gClip.W);
  UINT32 y1 = MinU32(r.Y + r.H, gClip.Y + gClip.H);
  UINT32 x, y;
  for (y = y0; y < y1; y++) {
    UINT32 *row = gScreen.Pixels + (UINTN)y * gScreen.Stride;
    for (x = x0; x < x1; x++) {
      row[x] = color;
    }
  }
}

VOID GfxBlendRect(GFX_RECT r, UINT32 color, UINT8 alpha)
{
  UINT32 x0 = MaxU32(r.X, gClip.X);
  UINT32 y0 = MaxU32(r.Y, gClip.Y);
  UINT32 x1 = MinU32(r.X + r.W, gClip.X + gClip.W);
  UINT32 y1 = MinU32(r.Y + r.H, gClip.Y + gClip.H);
  UINT32 x, y;
  for (y = y0; y < y1; y++) {
    UINT32 *row = gScreen.Pixels + (UINTN)y * gScreen.Stride;
    for (x = x0; x < x1; x++) {
      row[x] = Blend(row[x], color, alpha);
    }
  }
}

static UINT32 CornerInset(UINT32 radius, UINT32 dy)
{
  /* number of pixels to skip on a row dy from the top of a rounded box */
  if (dy >= radius) {
    return 0;
  }
  {
    INT32 r = (INT32)radius;
    INT32 y = r - (INT32)dy - 1;
    INT32 x2 = r * r - y * y;
    INT32 x = 0;
    if (x2 <= 0) {
      return radius;
    }
    while ((x + 1) * (x + 1) <= x2) {
      x++;
    }
    if (x + 1 >= r) {
      return 0;
    }
    return (UINT32)(r - x - 1);
  }
}

VOID GfxFillRound(GFX_RECT r, UINT32 radius, UINT32 color)
{
  UINT32 y;
  if (radius == 0) {
    GfxFillRect(r, color);
    return;
  }
  if (radius * 2 > r.H) {
    radius = r.H / 2;
  }
  for (y = 0; y < r.H; y++) {
    UINT32 inset = 0;
    UINT32 y0 = y + r.Y;
    if (y0 < gClip.Y || y0 >= gClip.Y + gClip.H) {
      continue;
    }
    if (y < radius) {
      inset = CornerInset(radius, y);
    } else if (y >= r.H - radius) {
      inset = CornerInset(radius, r.H - 1 - y);
    }
    if (inset * 2 >= r.W) {
      continue;
    }
    {
      GFX_RECT row = { r.X + inset, y0, r.W - inset * 2, 1 };
      GfxFillRect(row, color);
    }
  }
}

VOID GfxRoundOutline(GFX_RECT r, UINT32 radius, UINT32 color)
{
  UINT32 y;
  if (radius * 2 > r.H) {
    radius = r.H / 2;
  }
  for (y = 0; y < r.H; y++) {
    UINT32 inset = 0;
    if (y < radius) {
      inset = CornerInset(radius, y);
    } else if (y >= r.H - radius) {
      inset = CornerInset(radius, r.H - 1 - y);
    }
    if (inset * 2 >= r.W) {
      continue;
    }
    if (y == 0 || y == r.H - 1) {
      GfxHLine(r.X + inset + 1, r.X + r.W - inset - 2, r.Y + y, color);
    } else {
      GfxPutPixel(r.X + inset, r.Y + y, color);
      GfxPutPixel(r.X + r.W - 1 - inset, r.Y + y, color);
    }
  }
}

VOID GfxFrameRect(GFX_RECT r, UINT32 color)
{
  GfxHLine(r.X, r.X + r.W, r.Y, color);
  GfxHLine(r.X, r.X + r.W, r.Y + r.H - 1, color);
  GfxVLine(r.X, r.Y, r.Y + r.H, color);
  GfxVLine(r.X + r.W - 1, r.Y, r.Y + r.H, color);
}

VOID GfxHLine(UINT32 x0, UINT32 x1, UINT32 y, UINT32 color)
{
  GFX_RECT r = { x0, y, x1 > x0 ? x1 - x0 : 0, 1 };
  GfxFillRect(r, color);
}

VOID GfxVLine(UINT32 x, UINT32 y0, UINT32 y1, UINT32 color)
{
  GFX_RECT r = { x, y0, 1, y1 > y0 ? y1 - y0 : 0 };
  GfxFillRect(r, color);
}

VOID GfxFillCircle(UINT32 cx, UINT32 cy, UINT32 radius, UINT32 color)
{
  INT32 y;
  for (y = -(INT32)radius; y <= (INT32)radius; y++) {
    INT32 x2 = (INT32)(radius * radius) - y * y;
    INT32 x = 0;
    if (x2 < 0) {
      continue;
    }
    while ((x + 1) * (x + 1) <= x2) {
      x++;
    }
    GfxHLine(cx - x, cx + x + 1, cy + y, color);
  }
}

VOID GfxCircle(UINT32 cx, UINT32 cy, UINT32 radius, UINT32 color)
{
  INT32 a, b;
  INT32 r = (INT32)radius;
  for (a = 0; a < r; a++) {
    for (b = 0; b < r; b++) {
      INT32 d = a * a + b * b;
      if (d <= r * r && d >= (r - 1) * (r - 1)) {
        GfxPutPixel(cx + a, cy + b, color);
        GfxPutPixel(cx - a, cy + b, color);
        GfxPutPixel(cx + a, cy - b, color);
        GfxPutPixel(cx - a, cy - b, color);
      }
    }
  }
}

/* Integer atan2 in degrees, result in -180..180. */
static INT32 Atan2Deg(INT32 y, INT32 x)
{
  INT32 ax = x < 0 ? -x : x;
  INT32 ay = y < 0 ? -y : y;
  INT32 a;
  if (ax >= ay) {
    a = ax ? (ay * 45) / ax : 0;
  } else {
    a = ay ? 90 - (ax * 45) / ay : 90;
  }
  if (x >= 0) {
    return y >= 0 ? a : -a;
  }
  return y >= 0 ? 180 - a : a - 180;
}

/* Degrees measured clockwise from 12 o'clock. */
static UINT32 ScreenAngleDeg(INT32 dx, INT32 dy)
{
  INT32 a = Atan2Deg(dx, -dy);
  return (UINT32)((a + 360) % 360);
}

VOID GfxDonut(UINT32 cx, UINT32 cy, UINT32 rOuter, UINT32 rInner, UINT32 color,
              UINT32 startDeg, UINT32 endDeg)
{
  INT32 x, y;
  INT64 ro2 = (INT64)rOuter * rOuter;
  INT64 ri2 = (INT64)rInner * rInner;
  for (y = (INT32)cy - (INT32)rOuter; y <= (INT32)cy + (INT32)rOuter; y++) {
    for (x = (INT32)cx - (INT32)rOuter; x <= (INT32)cx + (INT32)rOuter; x++) {
      INT32 dx = x - (INT32)cx;
      INT32 dy = y - (INT32)cy;
      INT64 d = (INT64)dx * dx + (INT64)dy * dy;
      if (d > ro2 || d < ri2) {
        continue;
      }
      if (startDeg != 0 || endDeg != 360) {
        UINT32 deg = ScreenAngleDeg(dx, dy);
        if (deg < startDeg || deg >= endDeg) {
          continue;
        }
      }
      GfxPutPixel((UINT32)x, (UINT32)y, color);
    }
  }
}

VOID GfxGradientH(GFX_RECT r, UINT32 c0, UINT32 c1)
{
  UINT32 x;
  for (x = 0; x < r.W; x++) {
    UINT32 t = r.W > 1 ? (x * 255) / (r.W - 1) : 0;
    UINT32 col = Blend(c0, c1, t);
    GFX_RECT colRect = { r.X + x, r.Y, 1, r.H };
    GfxFillRect(colRect, col);
  }
}

VOID GfxGradientV(GFX_RECT r, UINT32 c0, UINT32 c1)
{
  UINT32 y;
  for (y = 0; y < r.H; y++) {
    UINT32 t = r.H > 1 ? (y * 255) / (r.H - 1) : 0;
    GFX_RECT row = { r.X, r.Y + y, r.W, 1 };
    GfxFillRect(row, Blend(c0, c1, t));
  }
}

/* ----------------------------------------------------------------- text */
static UINT32 Utf8Decode(const UINT8 **p)
{
  UINT32 c = *(*p)++;
  if (c < 0x80) {
    return c;
  }
  if ((c & 0xE0) == 0xC0) {
    c = ((c & 0x1F) << 6) | (*(*p)++ & 0x3F);
    return c;
  }
  if ((c & 0xF0) == 0xE0) {
    c = ((c & 0x0F) << 12) | ((*(*p)++ & 0x3F) << 6);
    c |= (*(*p)++ & 0x3F);
    return c;
  }
  if ((c & 0xF8) == 0xF0) {
    c = ((c & 0x07) << 18) | ((*(*p)++ & 0x3F) << 12);
    c |= ((*(*p)++ & 0x3F) << 6);
    c |= (*(*p)++ & 0x3F);
    return c;
  }
  return '?';
}

static const UINT8 *CjkGlyph(UINT32 cp)
{
  UINT32 lo = 0, hi = gFontCjkCount;
  while (lo < hi) {
    UINT32 mid = (lo + hi) / 2;
    if (gFontCjkCodes[mid] == cp) {
      return gFontCjkData[mid];
    }
    if (gFontCjkCodes[mid] < cp) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return 0;
}

UINT32 GfxTextHeight(UINT32 scale)
{
  return FONT_LATIN_H * scale;
}

static UINT32 GlyphAdvance(UINT32 cp, UINT32 scale)
{
  if (cp >= 0x20 && cp < 0x7F) {
    return ((UINT32)gLatinAdvance[cp - 0x20] + 1) * scale;
  }
  if (cp == ' ') {
    return 5 * scale;
  }
  return (FONT_CJK_W + 1) * scale;
}

UINT32 GfxTextWidth(const char *text, UINT32 scale)
{
  UINT32 w = 0;
  text = L(text);
  const UINT8 *p = (const UINT8 *)text;
  while (*p) {
    UINT32 cp = Utf8Decode(&p);
    w += GlyphAdvance(cp, scale);
  }
  return w;
}

static VOID DrawGlyph(UINT32 x, UINT32 y, const UINT8 *glyph, UINT32 gw, UINT32 gh,
                      UINT32 color, UINT32 scale)
{
  UINT32 sx, sy, px, py;
  for (sy = 0; sy < gh; sy++) {
    for (sx = 0; sx < gw; sx++) {
      UINT32 a = glyph[sy * gw + sx];
      if (a == 0) {
        continue;
      }
      for (py = 0; py < scale; py++) {
        UINT32 yy = y + sy * scale + py;
        if (yy < gClip.Y || yy >= gClip.Y + gClip.H) {
          continue;
        }
        for (px = 0; px < scale; px++) {
          UINT32 xx = x + sx * scale + px;
          if (!ClipPixel(xx, yy)) {
            continue;
          }
          gScreen.Pixels[(UINTN)yy * gScreen.Stride + xx] =
              Blend(gScreen.Pixels[(UINTN)yy * gScreen.Stride + xx], color, a);
        }
      }
    }
  }
}

VOID GfxText(UINT32 x, UINT32 y, const char *text, UINT32 color, UINT32 scale)
{
  const UINT8 *p;
  text = L(text);
  p = (const UINT8 *)text;
  UINT32 cx = x;
  while (*p) {
    UINT32 cp = Utf8Decode(&p);
    if (cp == '\n') {
      cx = x;
      y += FONT_LATIN_H * scale;
      continue;
    }
    if (cp == ' ') {
      cx += 5 * scale;
      continue;
    }
    if (cp < 0x7F) {
      const UINT8 *glyph = gFontLatinData[cp - 0x20];
      DrawGlyph(cx, y, glyph, FONT_LATIN_W, FONT_LATIN_H, color, scale);
      cx += GlyphAdvance(cp, scale);
    } else {
      const UINT8 *glyph = CjkGlyph(cp);
      if (glyph) {
        DrawGlyph(cx, y, glyph, FONT_CJK_W, FONT_CJK_H, color, scale);
        cx += GlyphAdvance(cp, scale);
      } else {
        const UINT8 *q = gFontLatinData['?' - 0x20];
        DrawGlyph(cx, y, q, FONT_LATIN_W, FONT_LATIN_H, color, scale);
        cx += 8 * scale;
      }
    }
  }
}

VOID GfxTextClipped(UINT32 x, UINT32 y, UINT32 maxW, const char *text, UINT32 color, UINT32 scale)
{
  UINT32 w;
  text = L(text);
  w = GfxTextWidth(text, scale);
  if (w <= maxW) {
    GfxText(x, y, text, color, scale);
    return;
  }
  {
    char buf[192];
    UINT32 i = 0;
    UINT32 used = 0;
    const UINT8 *p = (const UINT8 *)text;
    buf[0] = 0;
    while (*p && i + 8 < sizeof(buf)) {
      const UINT8 *start = p;
      UINT32 cp = Utf8Decode(&p);
      UINT32 adv = GlyphAdvance(cp, scale);
      if (used + adv + GfxTextWidth("…", scale) > maxW) {
        break;
      }
      used += adv;
      while (start < p && i + 1 < sizeof(buf)) {
        buf[i++] = (char)*start++;
      }
    }
    buf[i] = 0;
    GfxAppendStr(buf, sizeof(buf), "…");
    GfxText(x, y, buf, color, scale);
  }
}

/* -------------------------------------------------------------- strings */
UINT32 GfxStrLen(const char *s)
{
  UINT32 n = 0;
  while (s && s[n]) {
    n++;
  }
  return n;
}

INT32 GfxStrCmp(const char *a, const char *b)
{
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return (INT32)(UINT8)*a - (INT32)(UINT8)*b;
}

VOID GfxUtf8EncodeCopy(char *dst, UINTN dstSize, const char *src)
{
  UINTN i = 0;
  if (dstSize == 0) {
    return;
  }
  while (src && src[i] && i + 1 < dstSize) {
    dst[i] = src[i];
    i++;
  }
  dst[i] = 0;
}

CHAR16 *GfxUtf8ToUtf16(EFI_BOOT_SERVICES *BS, const char *src, UINTN *Len)
{
  UINTN cap = GfxStrLen(src) + 1;
  CHAR16 *out;
  UINTN i = 0;
  const UINT8 *p = (const UINT8 *)src;
  if (EFI_ERROR(BS->AllocatePool(EfiLoaderData, cap * sizeof(CHAR16), (VOID **)&out))) {
    return 0;
  }
  while (*p && i + 1 < cap) {
    UINT32 cp = Utf8Decode(&p);
    if (cp > 0xFFFF) {
      cp = '?';
    }
    out[i++] = (CHAR16)cp;
  }
  out[i] = 0;
  if (Len) {
    *Len = i;
  }
  return out;
}

VOID GfxAppendStr(char *dst, UINTN cap, const char *src)
{
  UINTN n = 0;
  while (n < cap && dst[n]) {
    n++;
  }
  while (src && *src && n + 1 < cap) {
    dst[n++] = *src++;
  }
  if (cap) {
    dst[n] = 0;
  }
}

VOID GfxFormatU64(char *buf, UINT64 value)
{
  char tmp[24];
  UINTN i = 0, j = 0;
  if (value == 0) {
    buf[0] = '0';
    buf[1] = 0;
    return;
  }
  while (value && i < sizeof(tmp)) {
    tmp[i++] = (char)('0' + (value % 10));
    value /= 10;
  }
  while (i) {
    buf[j++] = tmp[--i];
  }
  buf[j] = 0;
}

VOID GfxFormatHex(char *buf, UINT64 value, UINT32 digits)
{
  const char *hex = "0123456789ABCDEF";
  UINT32 i;
  for (i = 0; i < digits; i++) {
    buf[i] = hex[(value >> ((digits - 1 - i) * 4)) & 0xF];
  }
  buf[digits] = 0;
}

VOID GfxFormatSize(char *buf, UINTN cap, UINT64 bytes){
  char num[24];
  UINT64 unit = 1;
  const char *suffix = "B";
  UINT64 whole, frac;
  if (bytes >= (1ULL << 40)) {
    unit = 1ULL << 40;
    suffix = "TB";
  } else if (bytes >= (1ULL << 30)) {
    unit = 1ULL << 30;
    suffix = "GB";
  } else if (bytes >= (1ULL << 20)) {
    unit = 1ULL << 20;
    suffix = "MB";
  } else if (bytes >= (1ULL << 10)) {
    unit = 1ULL << 10;
    suffix = "KB";
  }
  whole = bytes / unit;
  frac = ((bytes % unit) * 10) / unit;
  GfxFormatU64(num, whole);
  buf[0] = 0;
  GfxAppendStr(buf, cap, num);
  if (frac && unit >= (1ULL << 20)) {
    char f[4];
    f[0] = '.';
    f[1] = (char)('0' + (UINT32)frac);
    f[2] = 0;
    GfxAppendStr(buf, cap, f);
  }
  GfxAppendStr(buf, cap, suffix);
}

VOID GfxTextRight(UINT32 right, UINT32 y, const char *text, UINT32 color, UINT32 scale)
{
  UINT32 w;
  text = L(text);
  w = GfxTextWidth(text, scale);
  GfxText(right > w ? right - w : 0, y, text, color, scale);
}

VOID GfxTextCenter(UINT32 x, UINT32 w, UINT32 y, const char *text, UINT32 color, UINT32 scale)
{
  UINT32 tw;
  text = L(text);
  tw = GfxTextWidth(text, scale);
  GfxText(x + (tw < w ? (w - tw) / 2 : 0), y, text, color, scale);
}
