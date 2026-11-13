/* page_tools.c - external EFI tool launcher and built-in maintenance actions. */
#include "pages.h"
#include "disk.h"
#include "results.h"
#include "report.h"
#include "debug.h"

#define TOOL_MAX      16
#define TOOL_PATH_MAX 96

typedef struct {
  char     Name[44];
  char     Entry[TOOL_PATH_MAX];
  UINT64   Size;
  UINT32   Kind;       /* 0 external image, 1 built-in action */
  BOOLEAN  Ok;         /* external image loaded from the ESP */
} TOOL_ENTRY;

static TOOL_ENTRY gTools[TOOL_MAX];
static UINT32     gToolCount;
static UINT32     gToolSel;
static BOOLEAN    gToolScanned;
static char       gToolLast[96];

/* ------------------------------------------------------------ enumeration */
static VOID ToolAdd(const char *name, const char *entry, UINT64 size, UINT32 kind, BOOLEAN ok)
{
  TOOL_ENTRY *t;
  if (gToolCount >= TOOL_MAX) {
    return;
  }
  t = &gTools[gToolCount++];
  UiStrPrintf(t->Name, sizeof(t->Name), "%s", name);
  UiStrPrintf(t->Entry, sizeof(t->Entry), "%s", entry);
  t->Size = size;
  t->Kind = kind;
  t->Ok = ok;
}

static VOID ScanDirectory(const CHAR16 *dirPath, const char *display)
{
  EFI_FILE_PROTOCOL *dir = 0;
  EFI_STATUS status;
  char name[48];

  if (gApp.Sys.BootRoot == 0) {
    return;
  }
  status = gApp.Sys.BootRoot->Open(gApp.Sys.BootRoot, &dir, (CHAR16 *)dirPath,
                                   EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR(status) || dir == 0) {
    return;
  }
  for (;;) {
    UINT8 buf[512];
    UINTN size = sizeof(buf);
    EFI_FILE_INFO *info = (EFI_FILE_INFO *)buf;
    UINTN i, len = 0;

    status = dir->Read(dir, &size, buf);
    if (EFI_ERROR(status) || size == 0) {
      break;
    }
    if (info->Attribute & EFI_FILE_DIRECTORY) {
      continue;
    }
    for (i = 0; info->FileName[i] && i < 40; i++) {
      CHAR16 c = info->FileName[i];
      name[i] = (c < 0x80) ? (char)c : '?';
    }
    name[i] = 0;
    len = i;
    if (len < 5 || name[len - 4] != '.' ||
        (name[len - 3] != 'E' && name[len - 3] != 'e') ||
        (name[len - 2] != 'F' && name[len - 2] != 'f') ||
        (name[len - 1] != 'I' && name[len - 1] != 'i')) {
      continue;
    }
    if (UiStrIcmp(name, "BOOTX64.EFI") == 0) {
      continue;
    }
    if (gToolCount < TOOL_MAX) {
      char entry[TOOL_PATH_MAX];
      UiStrPrintf(entry, sizeof(entry), "%s\\%s", display, name);
      ToolAdd(name, entry, info->FileSize, 0, TRUE);
    }
  }
  dir->Close(dir);
}

static const struct {
  const char *Name;
  const char *Note;
  UINT32      Action;
} gBuiltins[] = {
  { "重新采集平台信息", "SMBIOS、CPUID、内存映射与启动卷", 1 },
  { "导出 HTML 报告",   "写入启动卷 UCTS-REPORT.HTM", 2 },
  { "刷新全部磁盘健康", "重新读取 ATA / NVMe SMART 数据", 3 },
  { "转到内存检测并运行", "使用当前图案配置开始扫描", 4 },
  { "转到硬盘检测并扫描", "对当前选中的整盘做只读扫描", 5 },
  { "转到 CPU 检测并运行", "指令自检与吞吐测量", 6 }
};

VOID PageToolsEnter(VOID);

static VOID ToolScan(VOID)
{
  UINT32 i;
  gToolCount = 0;
  gToolSel = 0;
  gToolScanned = TRUE;
  ScanDirectory((const CHAR16 *)L"\\EFI\\TOOLS", "EFI\\TOOLS");
  ScanDirectory((const CHAR16 *)L"\\EFI\\BOOT", "EFI\\BOOT");
  for (i = 0; i < sizeof(gBuiltins) / sizeof(gBuiltins[0]); i++) {
    ToolAdd(gBuiltins[i].Name, gBuiltins[i].Note, 0, 1, TRUE);
  }
}

/* -------------------------------------------------------------- launching */
static EFI_DEVICE_PATH_PROTOCOL *BuildFilePath(const CHAR16 *rel)
{
  EFI_DEVICE_PATH_PROTOCOL *vol = gApp.Sys.BootVolumeDp;
  UINTN volLen = 0;
  UINTN strLen = 0;
  UINT8 *buf;
  UINT8 *p;
  UINT16 nodeLen;

  if (vol == 0) {
    return 0;
  }
  while (vol->Type != 0x7F) {
    UINT16 len = (UINT16)(vol->Length[0] | ((UINT16)vol->Length[1] << 8));
    if (len < 4) {
      break;
    }
    volLen += len;
    vol = (EFI_DEVICE_PATH_PROTOCOL *)((UINT8 *)vol + len);
  }
  while (rel[strLen]) {
    strLen++;
  }
  nodeLen = (UINT16)(4 + (strLen + 1) * 2);
  buf = 0;
  if (EFI_ERROR(gApp.BS->AllocatePool(EfiLoaderData, volLen + nodeLen + 4, (VOID **)&buf)) || buf == 0) {
    return 0;
  }
  gApp.BS->CopyMem(buf, gApp.Sys.BootVolumeDp, volLen);
  p = buf + volLen;
  p[0] = 0x04;   /* MEDIA_DEVICE_PATH */
  p[1] = 0x04;   /* MEDIA_FILEPATH_DP */
  p[2] = (UINT8)(nodeLen & 0xFF);
  p[3] = (UINT8)(nodeLen >> 8);
  gApp.BS->CopyMem(p + 4, (VOID *)rel, strLen * 2 + 2);
  p += nodeLen;
  p[0] = 0x7F;
  p[1] = 0xFF;
  p[2] = 4;
  p[3] = 0;
  return (EFI_DEVICE_PATH_PROTOCOL *)buf;
}

static VOID LaunchImage(TOOL_ENTRY *t)
{
  CHAR16 path[TOOL_PATH_MAX];
  EFI_DEVICE_PATH_PROTOCOL *dp;
  EFI_HANDLE image = 0;
  EFI_STATUS status;
  UINTN exitSize = 0;
  CHAR16 *exitData = 0;
  char msg[128];
  UINTN i;

  for (i = 0; t->Entry[i] && i < TOOL_PATH_MAX - 1; i++) {
    path[i] = (CHAR16)(UINT8)t->Entry[i];
  }
  path[i] = 0;
  dp = BuildFilePath(path);
  if (dp == 0) {
    UiSetStatus(3, "无法定位启动卷设备路径");
    return;
  }
  UiSetStatus(0, "正在启动外部工具…");
  GfxPresent();
  GfxShutdown();
  status = gApp.BS->LoadImage(FALSE, gApp.ImageHandle, dp, 0, 0, &image);
  if (EFI_ERROR(status)) {
    UiStrPrintf(msg, sizeof(msg), "加载失败 0x%lx", (unsigned long)status);
    gApp.BS->FreePool(dp);
    GfxInit(gApp.ImageHandle, gApp.ST);
    UiSetStatus(3, msg);
    return;
  }
  status = gApp.BS->StartImage(image, &exitSize, &exitData);
  gApp.BS->UnloadImage(image);
  gApp.BS->FreePool(dp);
  if (gScreen.Pixels) {
    gApp.BS->FreePool(gScreen.Pixels);
    gScreen.Pixels = 0;
  }
  GfxInit(gApp.ImageHandle, gApp.ST);
  UiRedraw();
  UiStrPrintf(msg, sizeof(msg), "%s 返回 0x%lx", t->Name, (unsigned long)status);
  UiSetStatus(EFI_ERROR(status) ? 2 : 1, msg);
}

static VOID RunBuiltin(UINT32 action)
{
  char msg[128];
  switch (action) {
  case 1:
    SysCollect(&gApp.Sys, gApp.ImageHandle, gApp.ST);
    gResults.HealthFw = gApp.Sys.SmbiosAvailable ? 1 : 2;
    UiSetStatus(1, "平台信息已重新采集");
    break;
  case 2:
    if (EFI_ERROR(ReportExportAll(&gApp))) {
      UiSetStatus(3, "报告导出失败，请确认启动卷可写");
    } else {
      UiSetStatus(1, "报告已写入启动卷 UCTS-REPORT.HTM");
    }
    break;
  case 3: {
    UINT32 i;
    for (i = 0; i < DiskCount(); i++) {
      DiskRefreshHealth(DiskAt(i));
    }
    UiStrPrintf(msg, sizeof(msg), "已刷新 %u 个设备的健康信息", DiskCount());
    UiSetStatus(1, msg);
    break;
  }
  case 4:
    UiSwitchPage(PAGE_MEM);
    UiSetStatus(0, "在内存检测页按 F5 开始扫描");
    break;
  case 5:
    UiSwitchPage(PAGE_DISK);
    UiSetStatus(0, "在硬盘检测页按 F5 开始扫描");
    break;
  case 6:
    UiSwitchPage(PAGE_CPU);
    UiSetStatus(0, "在 CPU 检测页按 F5 开始自检");
    break;
  default:
    break;
  }
}

/* ---------------------------------------------------------------- drawing */
/* the window follows the selection, so put the dropped row at the top */
static VOID ToolScrollList(UINT32 first, UINT32 visible)
{
  UINT32 target = first + (visible ? visible - 1 : 0);
  if (target >= gToolCount) {
    target = gToolCount ? gToolCount - 1 : 0;
  }
  gToolSel = target;
}

static VOID DrawToolList(GFX_RECT card)
{
  GFX_RECT inner = UiCardInner(card);
  UINT32 i;
  UINT32 rows = inner.H / 38;
  char buf[64];

  for (i = 0; i < rows && i < gToolCount; i++) {
    TOOL_ENTRY *t = &gTools[i];
    GFX_RECT row = { inner.X, inner.Y + i * 38, inner.W, 34 };
    BOOLEAN sel = (i == gToolSel);
    if (sel) {
      GfxFillRound(row, 8, gTheme.PrimarySoft);
    } else if (i & 1) {
      GfxFillRound(row, 8, gTheme.PanelAlt);
    }
    UiHitRow(row, i);
    UiIcon(t->Kind == 0 ? 6 : 1, row.X + 10, row.Y + 8, 16,
           sel ? gTheme.Primary : gTheme.SubText);
    GfxTextClipped(row.X + 34, row.Y + 2, row.W - 118, t->Name, gTheme.Text, 1);
    if (t->Kind == 0) {
      GfxFormatSize(buf, sizeof(buf), t->Size);
      UiStrPrintf(buf + GfxStrLen(buf), sizeof(buf) - GfxStrLen(buf), L(" · 外部镜像"));
    } else {
      UiStrPrintf(buf, sizeof(buf), "%s", t->Entry);
    }
    GfxTextClipped(row.X + 34, row.Y + 18, row.W - 40, buf, gTheme.SubText, 1);
    if (sel) {
      GfxTextRight(row.X + row.W - 14, row.Y + 2, "Enter",
                   gTheme.Primary, 1);
    }
  }
  UiScrollbarScroll((GFX_RECT){ card.X + card.W - 6, inner.Y, 4, inner.H },
                    gToolSel > rows ? gToolSel - rows + 1 : 0, rows, gToolCount,
                    ToolScrollList);
  UiStrPrintf(buf, sizeof(buf), "%u 项", gToolCount);
  GfxTextRight(card.X + card.W - 16, card.Y + 14, buf, gTheme.SubText, 1);
}

static VOID DrawSide(GFX_RECT card)
{
  GFX_RECT inner = UiCardInner(card);
  UINT32 y = inner.Y;
  char buf[128];
  UINT32 i;

  GfxText(inner.X, y, "运行环境", gTheme.SubText, 1);
  y += 20;
  UiStrPrintf(buf, sizeof(buf), "%s  ·  %s", gApp.Sys.SysProduct[0] ? gApp.Sys.SysProduct : "n/a",
              gApp.Sys.BiosVersion[0] ? gApp.Sys.BiosVersion : L("固件信息不可用"));
  GfxTextClipped(inner.X, y, inner.W, buf, gTheme.Text, 1);
  y += 20;
  UiStrPrintf(buf, sizeof(buf), "启动卷 %s · %s", gApp.Sys.BootVolumeName[0] ? gApp.Sys.BootVolumeName : "n/a",
              gApp.Sys.BootRoot ? L("可访问") : L("不可访问"));
  GfxTextClipped(inner.X, y, inner.W, buf, gApp.Sys.BootRoot ? gTheme.Text : gTheme.Warn, 1);
  y += 20;
  UiStrPrintf(buf, sizeof(buf), "%u 个块设备 · %u 个启动项", DiskCount(), gToolCount);
  GfxTextClipped(inner.X, y, inner.W, buf, gTheme.Text, 1);
  y += 28;
  UiDivider((GFX_RECT){ inner.X, y, inner.W, 1 });
  y += 10;
  GfxText(inner.X, y, "外部工具集成", gTheme.SubText, 1);
  y += 20;
  UiWrapText((GFX_RECT){ inner.X, y, inner.W, 80 },
             "将 UEFI_Test_Suite 的 .efi 工具放入启动卷的 EFI\\TOOLS 目录，"
             "仿真器启动时即可在此列出并直接运行，无需重新编译本套件。",
             gTheme.Muted, 1);
  y += 84;
  UiDivider((GFX_RECT){ inner.X, y, inner.W, 1 });
  y += 10;
  GfxText(inner.X, y, "最近操作", gTheme.SubText, 1);
  y += 20;
  GfxTextClipped(inner.X, y, inner.W,
                 gToolLast[0] ? gToolLast : "尚无操作记录", gTheme.Text, 1);

  (VOID)i;
}

VOID PageToolsEnter(VOID)
{
  DiskEnumerate();
  ToolScan();
  if (gToolCount == 0) {
    UiSetStatus(2, "未找到可运行的工具");
  } else {
    UiSetStatus(0, "外部工具可放入启动卷 EFI\\TOOLS 目录");
  }
}

VOID PageToolsDraw(GFX_RECT content)
{
  UINT32 gap = 12;
  GFX_RECT left = { content.X, content.Y, content.W * 3 / 5, content.H };
  GFX_RECT right = { left.X + left.W + gap, content.Y, content.W - left.W - gap, content.H };
  char buf[64];

  UiStrPrintf(buf, sizeof(buf), "共 %u 项", gToolCount);
  UiCard(left, "工具与操作", buf);
  DrawToolList(left);

  UiCard(right, "环境与说明", 0);
  DrawSide(right);
}

VOID PageToolsPoint(UINT32 row)
{
  if (row < gToolCount) {
    gToolSel = row;
  }
}

BOOLEAN PageToolsKey(EFI_INPUT_KEY key)
{
  switch (key.ScanCode) {
  case SCAN_UP:
    if (gToolSel > 0) {
      gToolSel--;
    }
    return TRUE;
  case SCAN_DOWN:
    if (gToolSel + 1 < gToolCount) {
      gToolSel++;
    }
    return TRUE;
  case SCAN_HOME:
    gToolSel = 0;
    return TRUE;
  case SCAN_END:
    gToolSel = gToolCount ? gToolCount - 1 : 0;
    return TRUE;
  case SCAN_F5:
    ToolScan();
    UiSetStatus(1, "已重新扫描启动卷");
    return TRUE;
  default:
    break;
  }
  if (key.UnicodeChar == '\r' || key.UnicodeChar == '\n') {
    TOOL_ENTRY *t = (gToolSel < gToolCount) ? &gTools[gToolSel] : 0;
    if (t == 0) {
      return TRUE;
    }
    UiStrPrintf(gToolLast, sizeof(gToolLast), "%s", t->Name);
    if (t->Kind == 0) {
      LaunchImage(t);
    } else {
      UINT32 i;
      for (i = 0; i < sizeof(gBuiltins) / sizeof(gBuiltins[0]); i++) {
        if (UiStrIcmp(t->Name, gBuiltins[i].Name) == 0) {
          RunBuiltin(gBuiltins[i].Action);
          break;
        }
      }
    }
    return TRUE;
  }
  return FALSE;
}
