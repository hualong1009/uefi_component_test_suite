/* page_about.c - product information, author, tool inventory and session state. */
#include "pages.h"
#include "results.h"
#include "cputest.h"
#include "disk.h"
#include "mouse.h"
#include "i18n.h"
#include "version.h"

typedef struct {
  const char *Name;
  const char *Source;
  const char *Where;
  UINT32      Level;   /* 1 内置, 2 外部镜像, 3 说明 */
} TOOL_INFO;

static const TOOL_INFO gInventory[] = {
  { "平台信息系统信息", "SMBIOS / CPUID / 内存映射", "系统信息页", 1 },
  { "PCIe 设备与拓扑",  "配置空间 / 桥窗口 / 协商链路", "系统拓扑页", 1 },
  { "内存图案检测",     "地址/数据图案写入回读比对", "内存检测页", 1 },
  { "内存 SPD 拓扑",    "SMBIOS 类型 17",           "系统信息页", 1 },
  { "颗粒级错误定位",   "出错数据位反推 DRAM 颗粒",  "内存拓扑页", 1 },
  { "硬盘健康 SMART",   "ATA PASS-THRU / NVMe 日志", "硬盘检测页", 1 },
  { "硬盘顺序扫描",     "EFI_BLOCK_IO 只读读盘",    "硬盘检测页", 1 },
  { "硬盘擦除",         "DoD 5220.22-M / Gutmann",   "硬盘擦除页", 1 },
  { "擦除回读校验",     "写后读比对",                "硬盘擦除页", 1 },
  { "处理器指令自检",   "整数 / SSE2 / AVX / RDRAND", "CPU 检测页", 1 },
  { "处理器吞吐测量",   "TSC 计时采样",              "CPU 检测页", 1 },
  { "多核压力测试",     "MP Services 并行 AP 加压",   "压力测试页", 1 },
  { "内存压力测试",     "按 DIMM 划分写入回读",      "压力测试页", 1 },
  { "硬盘压力测试",     "随机只读并发流量",          "压力测试页", 1 },
  { "报告包导出",       "HTML 分节 + 纯文本，共 5 个文件", "F2 快捷键", 1 },
  { "外部 EFI 工具启动", "启动卷 EFI\\TOOLS 目录",   "工具集页", 2 },
  { "UEFI_Test_Suite 组件", "外部二进制，放入 EFI\\TOOLS", "工具集页", 2 }
};

VOID PageAboutEnter(VOID)
{
  UiSetStatus(0, "UEFI 组件测试套件 · 平台诊断与压力测试");
}

/* One "label: value" line, used by the product card for the author block. */
static VOID InfoRow(GFX_RECT inner, UINT32 *y, const char *label, const char *value)
{
  GfxText(inner.X, *y, L(label), gTheme.Muted, 1);
  GfxTextClipped(inner.X + 62, *y, inner.W - 62, value, gTheme.Text, 1);
  *y += 19;
}

static VOID DrawProduct(GFX_RECT card)
{
  GFX_RECT inner = UiCardInner(card);
  UINT32 y = inner.Y;
  char buf[96];

  GfxText(inner.X, y, "UEFI 组件测试套件", gTheme.Text, 2);
  y += 34;
  GfxText(inner.X, y, "UEFI Component Test Suite · UCTS", gTheme.SubText, 1);
  y += 26;
  UiStrPrintf(buf, sizeof(buf), "版本 %s · 构建 %s %s", UCTS_VERSION, __DATE__, __TIME__);
  GfxText(inner.X, y, buf, gTheme.Muted, 1);
  y += 20;

  UiStrPrintf(buf, sizeof(buf), "运行于 %s · %s", gApp.Sys.SysProduct[0] ? gApp.Sys.SysProduct : L("未知平台"),
              gApp.Sys.BiosVendor[0] ? gApp.Sys.BiosVendor : L("固件"));
  GfxTextClipped(inner.X, y, inner.W, buf, gTheme.Text, 1);
  y += 20;
  UiStrPrintf(buf, sizeof(buf), "UEFI %u.%u · %u 逻辑处理器 · %u 块设备",
              gApp.Sys.SmbiosMajor, gApp.Sys.SmbiosMinor, gApp.Sys.LogicalCpus, DiskCount());
  GfxTextClipped(inner.X, y, inner.W, buf, gTheme.Text, 1);
  y += 26;

  UiDivider((GFX_RECT){ inner.X, y, inner.W, 1 });
  y += 12;
  GfxText(inner.X, y, "开发信息", gTheme.SubText, 1);
  y += 20;
  InfoRow(inner, &y, "开发", UCTS_AUTHOR);
  InfoRow(inner, &y, "GitHub", "hualong1009/uefi_component_test_suite");
  y += 8;
  UiWrapText((GFX_RECT){ inner.X, y, inner.W, inner.H - (y - inner.Y) - 4 },
             "设计思路参考知乎专栏文章 zhuanlan.zhihu.com/p/2100653235568633192："
             "把整机诊断拆成互相独立的页面，每页给出实时进度与结论。",
             gTheme.Muted, 1);
}

static VOID DrawInventory(GFX_RECT card)
{
  GFX_RECT inner = UiCardInner(card);
  UINT32 y = inner.Y;
  UINT32 i;

  UINT32 whereW = GfxTextWidth("所在页面", 1);
  UINT32 nameW;

  for (i = 0; i < sizeof(gInventory) / sizeof(gInventory[0]); i++) {
    UINT32 w = GfxTextWidth(gInventory[i].Where, 1);
    if (w > whereW) {
      whereW = w;
    }
  }
  if (whereW > (inner.W * 42) / 100) {
    whereW = (inner.W * 42) / 100;
  }
  nameW = inner.W - whereW - 20;
  if (nameW < 80) {
    nameW = 80;
  }

  GfxText(inner.X, y, "能力", gTheme.Muted, 1);
  GfxTextRight(inner.X + inner.W, y, "所在页面", gTheme.Muted, 1);
  y += 20;
  UiDivider((GFX_RECT){ inner.X, y - 4, inner.W, 1 });
  for (i = 0; i < sizeof(gInventory) / sizeof(gInventory[0]); i++) {
    const TOOL_INFO *t = &gInventory[i];
    if (y + 20 > inner.Y + inner.H) {
      break;
    }
    GfxTextClipped(inner.X, y, nameW, t->Name, gTheme.Text, 1);
    if (GfxTextWidth(t->Where, 1) > whereW) {
      GfxTextClipped(inner.X + inner.W - whereW, y, whereW, t->Where,
                     t->Level == 2 ? gTheme.Warn : gTheme.SubText, 1);
    } else {
      GfxTextRight(inner.X + inner.W, y, t->Where,
                   t->Level == 2 ? gTheme.Warn : gTheme.SubText, 1);
    }
    y += 19;
  }
}

static VOID DrawKeys(GFX_RECT card)
{
  GFX_RECT inner = UiCardInner(card);
  UINT32 y = inner.Y;
  static const struct { const char *Key; const char *What; } keys[] = {
    { "←→",    "切换页面（1-9 直跳，0 关于）" },
    { "↑↓",    "在当前页的列表里移动选择" },
    { "空格",  "勾选 / 切换当前项或取值" },
    { "Tab",   "切换页内分组（压力测试）" },
    { "F2",    "导出 HTML 报告到启动卷" },
    { "F5",    "开始检测、扫描或自检" },
    { "F6",    "停止当前检测" },
    { "F7/F8", "全选 / 仅核心项" },
    { "Enter", "确认、启动外部工具或刷新信息" },
    { "点击",  "选中一行、切换页面或按下按钮" },
    { "双击",  "等同该行上的 Enter（打开、启动）" },
    { "滚轮",  "在列表与长页面里上下移动" },
    { "ESC",   "返回概览；检测中表示中止" },
    { "F10",   "退出套件并返回固件" }
  };
  UINT32 i;

  for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
    GFX_RECT box = { inner.X, y, 52, 18 };
    GfxFillRound(box, 5, gTheme.PanelAlt);
    GfxRoundOutline(box, 5, gTheme.Border);
    GfxText(box.X + 8, box.Y + 2, keys[i].Key, gTheme.Text, 1);
    GfxTextClipped(inner.X + 70, y + 1, inner.W - 70, keys[i].What, gTheme.SubText, 1);
    y += 24;
  }
}

static VOID DrawStatus(GFX_RECT card)
{
  GFX_RECT inner = UiCardInner(card);
  UINT32 y = inner.Y;
  char buf[96];

  GfxText(inner.X, y, "本次会话状态", gTheme.SubText, 1);
  y += 22;
  UiStrPrintf(buf, sizeof(buf), "内存 %s · 覆盖 %u%% · 图案 %u · 错误 %u",
              gResults.MemTested ? L("已测") : L("未测"), ((gResults.MemCoveragePm + 5) / 10),
              gResults.MemPatternsRun, gResults.MemErrorCount);
  GfxTextClipped(inner.X, y, inner.W, buf, gTheme.Text, 1);
  y += 20;
  UiStrPrintf(buf, sizeof(buf), "磁盘 %s · 已扫描 %u 台 · 错误 %u",
              gResults.DiskTested ? L("已测") : L("未测"), gResults.DiskScannedDevices,
              gResults.DiskErrorCount);
  GfxTextClipped(inner.X, y, inner.W, buf, gTheme.Text, 1);
  y += 20;
  UiStrPrintf(buf, sizeof(buf), "CPU %s · 异常 %u · %u MOPS",
              gResults.CpuTested ? L("已测") : L("未测"), gResults.CpuErrorCount, gResults.CpuMops);
  GfxTextClipped(inner.X, y, inner.W, buf, gTheme.Text, 1);
  y += 20;
  UiStrPrintf(buf, sizeof(buf), "擦除 %u 次 · %s", gResults.EraseCount,
              gResults.EraseLastTarget[0] ? gResults.EraseLastTarget : L("无记录"));
  GfxTextClipped(inner.X, y, inner.W, buf, gTheme.Text, 1);
  y += 26;

  {
    BOOLEAN absFound = FALSE;
    BOOLEAN relFound = FALSE;
    UINT32 moves = 0;
    UINT32 clicks = 0;
    UINT32 hits = 0;
    const char *kind;

    MouseStats(&absFound, &relFound, &moves, &clicks, &hits);
    if (absFound && relFound) {
      kind = L("绝对 + 相对");
    } else if (absFound) {
      kind = L("绝对（KVM / 平板）");
    } else if (relFound) {
      kind = L("相对（普通鼠标）");
    } else {
      kind = L("未发现，仅键盘可用");
    }
    UiStrPrintf(buf, sizeof(buf), "指针 %s", kind);
    GfxTextClipped(inner.X, y, inner.W, buf,
                   (absFound || relFound) ? gTheme.Text : gTheme.Warn, 1);
    y += 20;
    UiStrPrintf(buf, sizeof(buf), "移动 %u 次 · 点击 %u 次 · 本页热区 %u 个",
                moves, clicks, hits);
    GfxTextClipped(inner.X, y, inner.W, buf, gTheme.SubText, 1);
    y += 20;
    if (absFound || relFound) {
      UiStrPrintf(buf, sizeof(buf), "光标位置 %u,%u · 屏幕 %ux%u",
                  MouseX(), MouseY(), gScreen.Width, gScreen.Height);
      GfxTextClipped(inner.X, y, inner.W, buf, gTheme.Muted, 1);
      y += 20;
    }
  }

  UiDivider((GFX_RECT){ inner.X, y, inner.W, 1 });
  y += 12;
  UiWrapText((GFX_RECT){ inner.X, y, inner.W, inner.H - (y - inner.Y) - 4 },
             "所有检测均在内存中进行，除硬盘擦除外不对磁盘做任何写入。"
             "结果可通过 F2 导出为 HTML，便于随工单归档。", gTheme.Muted, 1);
}

VOID PageAboutDraw(GFX_RECT content)
{
  UINT32 gap = 12;
  UINT32 topH = content.H * 60 / 100;
  GFX_RECT product = { content.X, content.Y, content.W * 58 / 100, topH };
  GFX_RECT inv = { product.X + product.W + gap, content.Y,
                   content.W - product.W - gap, topH };
  GFX_RECT keys = { content.X, content.Y + topH + gap, content.W * 44 / 100,
                    content.H - topH - gap };
  GFX_RECT state = { keys.X + keys.W + gap, keys.Y, content.W - keys.W - gap,
                     content.H - topH - gap };

  UiCard(product, "关于本套件", 0);
  DrawProduct(product);

  UiCard(inv, "已集成能力", "全部内置");
  DrawInventory(inv);

  UiCard(keys, "按键约定", 0);
  DrawKeys(keys);

  UiCard(state, "会话摘要", 0);
  DrawStatus(state);
}

VOID PageAboutPoint(UINT32 row)
{
  (VOID)row;
}

BOOLEAN PageAboutKey(EFI_INPUT_KEY key)
{
  (VOID)key;
  return FALSE;
}
