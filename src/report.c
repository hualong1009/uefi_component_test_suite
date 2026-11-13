/* report.c - self-contained HTML report, written next to the loader. */
#include "report.h"
#include "results.h"
#include "disk.h"
#include "pci.h"
#include "debug.h"
#include "i18n.h"
#include "version.h"

typedef struct {
  char  *Buf;
  UINTN  Cap;
  UINTN  Len;
  BOOLEAN Overflow;
} STRBUF;

static VOID SbInit(STRBUF *sb, char *buf, UINTN cap)
{
  sb->Buf = buf;
  sb->Cap = cap;
  sb->Len = 0;
  sb->Overflow = FALSE;
  if (cap) {
    buf[0] = 0;
  }
}

static VOID SbAddN(STRBUF *sb, const char *s, UINTN n)
{
  UINTN i;
  for (i = 0; i < n; i++) {
    if (sb->Len + 1 >= sb->Cap) {
      if (sb->Len + 1 < sb->Cap + 1) {
        sb->Overflow = TRUE;
      }
      break;
    }
    sb->Buf[sb->Len++] = s[i];
    sb->Buf[sb->Len] = 0;
  }
}

static VOID SbAdd(STRBUF *sb, const char *s)
{
  s = L(s);
  SbAddN(sb, s, GfxStrLen(s));
}

static VOID SbHtml(STRBUF *sb, const char *text)
{
  text = L(text);
  while (text && *text) {
    switch (*text) {
    case '<': SbAdd(sb, "&lt;"); break;
    case '>': SbAdd(sb, "&gt;"); break;
    case '&': SbAdd(sb, "&amp;"); break;
    case '"': SbAdd(sb, "&quot;"); break;
    default: SbAddN(sb, text, 1); break;
    }
    text++;
  }
}

static const char *LevelWord(UINT32 level)
{
  switch (level) {
  case 1: return L("正常");
  case 2: return L("注意");
  case 3: return L("异常");
  default: return L("未检测");
  }
}

static VOID RowKV(STRBUF *sb, const char *key, const char *value)
{
  SbAdd(sb, "<tr><th>");
  SbHtml(sb, key);
  SbAdd(sb, "</th><td>");
  SbHtml(sb, (value && value[0]) ? value : "n/a");
  SbAdd(sb, "</td></tr>\n");
}

/* what hangs off a controller: block device or network interface */
static VOID AssetText(const PCI_DEV *dev, char *buf, UINTN cap)
{
  buf[0] = 0;
  if (dev->DiskIndex != PCI_NO_INDEX) {
    DISK_DEV *disk = DiskAt(dev->DiskIndex);
    if (disk) {
      char size[24];
      GfxFormatSize(size, sizeof(size), disk->SizeBytes);
      UiStrPrintf(buf, cap, "%s %s", disk->Model[0] ? disk->Model : L("块设备"), size);
      return;
    }
  }
  if (dev->NetIndex != PCI_NO_INDEX) {
    NET_DEV *net = NetAt(dev->NetIndex);
    if (net) {
      char mac[24];
      PciFormatMac(net->Mac, mac, sizeof(mac));
      UiStrPrintf(buf, cap, "%s %s", mac, net->MediaPresent ? L("链路已连接") : L("链路未连接"));
    }
  }
}

static VOID PciLinkText(const PCI_DEV *dev, char *buf, UINTN cap)
{
  if (PciHasLink(dev)) {
    UiStrPrintf(buf, cap, "Gen%u x%u", dev->LinkGen, dev->LinkWidth);
  } else {
    UiStrPrintf(buf, cap, "%s", L("非 PCIe 链路"));
  }
}

/* Reads the file back and compares it with what we intended to write, so the
   status line can state whether the report is really on the volume. */
static EFI_STATUS ReportVerify(EFI_FILE_PROTOCOL *root, const char *name, const char *data,
                               UINTN len)
{
  EFI_FILE_PROTOCOL *file = 0;
  CHAR16 wname[64];
  UINTN i, off = 0;
  EFI_STATUS status;

  if (root == 0) {
    return EFI_NOT_FOUND;
  }
  for (i = 0; i + 1 < sizeof(wname) / sizeof(wname[0]) && name[i]; i++) {
    wname[i] = (CHAR16)name[i];
  }
  wname[i] = 0;
  status = root->Open(root, &file, wname, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR(status)) {
    return status;
  }
  while (off < len) {
    UINT8 chunk[2048];
    UINTN size = (len - off) < sizeof(chunk) ? (len - off) : sizeof(chunk);
    status = file->Read(file, &size, chunk);
    if (EFI_ERROR(status) || size == 0) {
      file->Close(file);
      return EFI_DEVICE_ERROR;
    }
    if (memcmp(chunk, data + off, size) != 0) {
      file->Close(file);
      return EFI_DEVICE_ERROR;
    }
    off += size;
  }
  file->Close(file);
  return EFI_SUCCESS;
}

/* Sections of the report; the shell can export any combination of them. */
#define SEC_INFO   0x01   /* platform, processor, memory topology */
#define SEC_TEST   0x02   /* memory test, storage, CPU test, erase log */
#define SEC_STRESS 0x04   /* stress test session */
#define SEC_ALL    (SEC_INFO | SEC_TEST | SEC_STRESS)

static EFI_STATUS ReportExportHtml(UI_APP *app, UINT32 mask, const char *name)
{
  char *buffer = 0;
  STRBUF sb;
  EFI_STATUS status;
  char num[64];
  UINT32 i;
  UINTN size = 128 * 1024;

  if (app == 0 || app->Sys.BootRoot == 0) {
    return EFI_NOT_FOUND;
  }
  status = app->BS->AllocatePool(EfiBootServicesData, size, (VOID **)&buffer);
  if (EFI_ERROR(status)) {
    return status;
  }
  SbInit(&sb, buffer, size);

  SbAdd(&sb, "<!DOCTYPE html>\n<html lang=\"");
  SbAdd(&sb, ILang() == LANG_ZH ? "zh-CN" : "en");
  SbAdd(&sb, "\"><head><meta charset=\"utf-8\">\n");
  SbAdd(&sb, "<title>UCTS 诊断报告</title>\n<style>\n");
  SbAdd(&sb, "body{font-family:'Segoe UI','Microsoft YaHei',sans-serif;margin:0;background:#f1f4f9;color:#0f172a}\n");
  SbAdd(&sb, "header{background:linear-gradient(90deg,#1d4ed8,#2563eb);color:#fff;padding:26px 34px}\n");
  SbAdd(&sb, "header h1{margin:0;font-size:22px;font-weight:600}\nheader p{margin:6px 0 0;opacity:.85;font-size:13px}\n");
  SbAdd(&sb, "main{padding:22px 34px 60px;max-width:1080px}\nh2{font-size:15px;margin:26px 0 10px;color:#1d4ed8}\n");
  SbAdd(&sb, "table{width:100%;border-collapse:collapse;background:#fff;border:1px solid #e2e8f0;border-radius:8px;overflow:hidden}\n");
  SbAdd(&sb, "th,td{text-align:left;padding:8px 12px;font-size:13px;border-bottom:1px solid #eef2f7}\n");
  SbAdd(&sb, "th{width:210px;color:#64748b;font-weight:500;background:#f8fafc}\n");
  SbAdd(&sb, "tr:last-child th,tr:last-child td{border-bottom:none}\n");
  SbAdd(&sb, ".ok{color:#10b981}.warn{color:#f59e0b}.crit{color:#ef4444}.off{color:#94a3b8}\n");
  SbAdd(&sb, ".badge{display:inline-block;padding:2px 10px;border-radius:999px;font-size:12px;color:#fff}\n");
  SbAdd(&sb, "footer{margin-top:30px;color:#94a3b8;font-size:12px}\n</style></head><body>\n");

  SbAdd(&sb, "<header><h1>UEFI 组件测试套件 · 诊断报告</h1><p>");
  SbHtml(&sb, app->Sys.SysProduct[0] ? app->Sys.SysProduct : "未知平台");
  SbAdd(&sb, " · ");
  SbHtml(&sb, app->Sys.CpuBrand[0] ? app->Sys.CpuBrand : "未知处理器");
  SbAdd(&sb, "</p></header>\n<main>\n");

  /* ---- platform ---- */
  if (mask & SEC_INFO) {
  SbAdd(&sb, "<h2>平台信息</h2><table>\n");
  RowKV(&sb, "系统厂商", app->Sys.SysVendor);
  RowKV(&sb, "系统型号", app->Sys.SysProduct);
  RowKV(&sb, "系统版本", app->Sys.SysVersion);
  RowKV(&sb, "序列号", app->Sys.SysSerial);
  RowKV(&sb, "主板", app->Sys.BoardProduct);
  RowKV(&sb, "BIOS 厂商", app->Sys.BiosVendor);
  RowKV(&sb, "BIOS 版本", app->Sys.BiosVersion);
  RowKV(&sb, "BIOS 日期", app->Sys.BiosDate);
  UiStrPrintf(num, sizeof(num), "%u.%02u", app->Sys.FirmwareRevision >> 16,
              (app->Sys.FirmwareRevision >> 8) & 0xFF);
  RowKV(&sb, "UEFI 规范版本", num);
  UiStrPrintf(num, sizeof(num), "SMBIOS %u.%u", app->Sys.SmbiosMajor, app->Sys.SmbiosMinor);
  RowKV(&sb, "SMBIOS", app->Sys.SmbiosAvailable ? num : "不可用");
  SbAdd(&sb, "</table>\n");

  /* ---- cpu ---- */
  SbAdd(&sb, "<h2>处理器</h2><table>\n");
  RowKV(&sb, "型号", app->Sys.CpuBrand);
  UiStrPrintf(num, sizeof(num), "Family %u / Model %u / Stepping %u", app->Sys.CpuFamily,
              app->Sys.CpuModel, app->Sys.CpuStepping);
  RowKV(&sb, "标识", num);
  UiStrPrintf(num, sizeof(num), "%u 核 / %u 线程", app->Sys.Cores, app->Sys.Threads);
  RowKV(&sb, "核心", num);
  if (app->Sys.CpuMaxMhz > app->Sys.CpuBaseMhz) {
    UiStrPrintf(num, sizeof(num), "%u MHz（最大 %u MHz）", app->Sys.CpuBaseMhz, app->Sys.CpuMaxMhz);
  } else {
    UiStrPrintf(num, sizeof(num), "%u MHz", app->Sys.CpuBaseMhz);
  }
  RowKV(&sb, "频率", num);
  {
    char cache[128];
    UiStrPrintf(cache, sizeof(cache), "L1 ");
    if (app->Sys.L1Bytes) {
      UiStrPrintf(num, sizeof(num), "%u KB", (UINT32)(app->Sys.L1Bytes / 1024));
    } else {
      UiStrPrintf(num, sizeof(num), "n/a");
    }
    GfxAppendStr(cache, sizeof(cache), num);
    GfxAppendStr(cache, sizeof(cache), " / L2 ");
    if (app->Sys.L2Bytes) {
      GfxFormatSize(num, sizeof(num), app->Sys.L2Bytes);
    } else {
      UiStrPrintf(num, sizeof(num), "n/a");
    }
    GfxAppendStr(cache, sizeof(cache), num);
    GfxAppendStr(cache, sizeof(cache), " / L3 ");
    if (app->Sys.L3Bytes) {
      GfxFormatSize(num, sizeof(num), app->Sys.L3Bytes);
    } else {
      UiStrPrintf(num, sizeof(num), "n/a");
    }
    GfxAppendStr(cache, sizeof(cache), num);
    RowKV(&sb, "缓存", cache);
  }
  SbAdd(&sb, "</table>\n");

  /* ---- memory topology ---- */
  SbAdd(&sb, "<h2>内存拓扑</h2><table>\n");
  GfxFormatSize(num, sizeof(num), app->Sys.InstalledRam);
  RowKV(&sb, "已安装容量", num);
  GfxFormatSize(num, sizeof(num), app->Sys.TestableRam);
  RowKV(&sb, "可测试容量", num);
  if (app->Sys.DominantMemSpeed) {
    UiStrPrintf(num, sizeof(num), "%s · %u MT/s", SysMemTypeName(app->Sys.DominantMemType),
                app->Sys.DominantMemSpeed);
  } else {
    UiStrPrintf(num, sizeof(num), "%s", SysMemTypeName(app->Sys.DominantMemType));
  }
  RowKV(&sb, "类型", num);
  SbAdd(&sb, "</table>\n");
  if (app->Sys.DimmCount) {
    SbAdd(&sb, "<table style=\"margin-top:8px\"><tr><th style=\"width:auto\">插槽</th>"
               "<th style=\"width:auto\">容量</th><th style=\"width:auto\">类型</th>"
               "<th style=\"width:auto\">速率</th><th style=\"width:auto\">厂商</th>"
               "<th style=\"width:auto\">颗粒编号</th>"
               "<th style=\"width:auto\">布局</th>"
               "<th style=\"width:auto\">颗粒错误</th></tr>\n");
    for (i = 0; i < app->Sys.DimmCount; i++) {
      DIMM_INFO *d = &app->Sys.Dimms[i];
      SbAdd(&sb, "<tr><td>");
      SbHtml(&sb, d->Locator);
      SbAdd(&sb, "</td><td>");
      GfxFormatSize(num, sizeof(num), d->SizeBytes);
      SbAdd(&sb, num);
      SbAdd(&sb, "</td><td>");
      SbHtml(&sb, SysMemTypeName(d->MemoryType));
      SbAdd(&sb, "</td><td>");
      if (d->ConfiguredSpeed || d->Speed) {
        UiStrPrintf(num, sizeof(num), "%u MT/s", d->ConfiguredSpeed ? d->ConfiguredSpeed : d->Speed);
        SbAdd(&sb, num);
      } else {
        SbAdd(&sb, "n/a");
      }
      SbAdd(&sb, "</td><td>");
      SbHtml(&sb, d->Manufacturer);
      SbAdd(&sb, "</td><td>");
      SbHtml(&sb, d->PartNumber);
      SbAdd(&sb, "</td><td>");
      {
        DIMM_GEO geo;
        SysDimmGeometry(d, &geo);
        UiStrPrintf(num, sizeof(num), "x%u / %uR / %u 颗", geo.DeviceWidth, geo.Ranks,
                    geo.TotalDevices);
        SbAdd(&sb, num);
      }
      SbAdd(&sb, "</td><td>");
      if (i < 16 && d->Present && gResults.MemDimmErrors[i]) {
        UINT32 dev, topDev = 0xFFFFFFFF, topCount = 0;
        for (dev = 0; dev < 32; dev++) {
          if (gResults.MemDeviceErrors[i][dev] > topCount) {
            topCount = gResults.MemDeviceErrors[i][dev];
            topDev = dev;
          }
        }
        UiStrPrintf(num, sizeof(num), "%u 次", gResults.MemDimmErrors[i]);
        SbAdd(&sb, num);
        if (topDev != 0xFFFFFFFF) {
          UiStrPrintf(num, sizeof(num), "（D%u ×%u）", topDev + 1, topCount);
          SbAdd(&sb, num);
        }
      } else {
        SbAdd(&sb, d->Present ? "0 次" : "-");
      }
      SbAdd(&sb, "</td></tr>\n");
    }
    SbAdd(&sb, "</table>\n");
  }

  /* ---- PCI/PCIe inventory ---- */
  if (PciCount()) {
    UINT32 i;
    SbAdd(&sb, "<h2>");
    SbAdd(&sb, "PCI/PCIe 设备");
    SbAdd(&sb, "</h2>\n<table><tr>");
    SbAdd(&sb, "<th style=\"width:auto\">");
    SbAdd(&sb, "地址");
    SbAdd(&sb, "</th><th style=\"width:auto\">");
    SbAdd(&sb, "类别");
    SbAdd(&sb, "</th><th style=\"width:auto\">");
    SbAdd(&sb, "厂商:设备");
    SbAdd(&sb, "</th><th style=\"width:auto\">");
    SbAdd(&sb, "链路");
    SbAdd(&sb, "</th><th style=\"width:auto\">");
    SbAdd(&sb, "挂载");
    SbAdd(&sb, "</th></tr>\n");
    for (i = 0; i < PciCount() && i < PCI_MAX_DEVS; i++) {
      PCI_DEV *dev = PciAt(i);
      char addr[24];
      char link[24];
      char asset[72];
      PciFormatAddress(dev, addr, sizeof(addr));
      PciLinkText(dev, link, sizeof(link));
      AssetText(dev, asset, sizeof(asset));
      SbAdd(&sb, "<tr><td>");
      SbAdd(&sb, addr);
      SbAdd(&sb, "</td><td>");
      SbAdd(&sb, L(PciClassText(dev)));
      SbAdd(&sb, "</td><td>");
      UiStrPrintf(num, sizeof(num), "%s %04X:%04X",
                  PciVendorName(dev->VendorId) ? PciVendorName(dev->VendorId) : L("未知厂商"),
                  dev->VendorId, dev->DeviceId);
      SbAdd(&sb, num);
      SbAdd(&sb, "</td><td>");
      SbAdd(&sb, link);
      SbAdd(&sb, "</td><td>");
      SbAdd(&sb, asset[0] ? asset : "-");
      SbAdd(&sb, "</td></tr>\n");
    }
    SbAdd(&sb, "</table>\n");
  }

  }

  /* ---- memory test ---- */
  if (mask & SEC_TEST) {
  SbAdd(&sb, "<h2>内存检测（软件图案）</h2><table>\n");
  UiStrPrintf(num, sizeof(num), "%s（%s）", gResults.MemTested ? L("已执行") : L("未执行"),
              LevelWord(gResults.HealthMem));
  RowKV(&sb, "状态", num);
  UiStrPrintf(num, sizeof(num), "%u / %u", gResults.MemPatternsRun, gResults.MemPatternCount);
  RowKV(&sb, "图案进度", num);
  UiStrPrintf(num, sizeof(num), "%u", gResults.MemIterations);
  RowKV(&sb, "迭代轮次", num);
  UiStrPrintf(num, sizeof(num), "%u 个", gResults.MemErrorCount);
  RowKV(&sb, "错误数", num);
  UiStrPrintf(num, sizeof(num), "%u%%", (gResults.MemCoveragePm + 5) / 10);
  RowKV(&sb, "覆盖率", num);
  UiStrPrintf(num, sizeof(num), "%u MB/s", gResults.MemBandwidthMbs);
  RowKV(&sb, "带宽", num);
  UiStrPrintf(num, sizeof(num), "%u ms", gResults.MemElapsedMs);
  RowKV(&sb, "耗时", num);
  if (gResults.MemFirstErrorOffsetValid) {
    UiStrPrintf(num, sizeof(num), "0x%llX", (unsigned long long)gResults.MemFirstErrorOffset);
    RowKV(&sb, "首个错误地址", num);
    UiStrPrintf(num, sizeof(num), "0x%llX (期望 0x%llX)",
                (unsigned long long)gResults.MemFirstErrorActual,
                (unsigned long long)gResults.MemFirstErrorExpected);
    RowKV(&sb, "首个错误数据", num);
  }
  UiStrPrintf(num, sizeof(num), "%u 次", gResults.MemDeviceMapped);
  RowKV(&sb, "已定位到颗粒的错误", num);
  SbAdd(&sb, "</table>\n");

  /* ---- storage ---- */
  SbAdd(&sb, "<h2>存储设备</h2>\n");
  if (DiskCount() == 0) {
    SbAdd(&sb, "<table><tr><td>未发现块设备</td></tr></table>\n");
  } else {
    SbAdd(&sb, "<table><tr><th style=\"width:auto\">设备</th><th style=\"width:auto\">容量</th>"
               "<th style=\"width:auto\">接口</th><th style=\"width:auto\">健康</th>"
               "<th style=\"width:auto\">扫描</th><th style=\"width:auto\">速度</th></tr>\n");
    for (i = 0; i < DiskCount(); i++) {
      DISK_DEV *d = DiskAt(i);
      SbAdd(&sb, "<tr><td>");
      SbHtml(&sb, d->Model[0] ? d->Model : d->PathText);
      SbAdd(&sb, "</td><td>");
      GfxFormatSize(num, sizeof(num), d->SizeBytes);
      SbAdd(&sb, num);
      SbAdd(&sb, "</td><td>");
      SbHtml(&sb, DiskIfaceName(d->Iface));
      SbAdd(&sb, "</td><td>");
      SbHtml(&sb, d->HealthText[0] ? d->HealthText : "n/a");
      SbAdd(&sb, "</td><td>");
      if (d->Scanned) {
        UiStrPrintf(num, sizeof(num), "%u 错误", d->ScanErrors);
        SbAdd(&sb, num);
      } else {
        SbAdd(&sb, "未扫描");
      }
      SbAdd(&sb, "</td><td>");
      if (d->Scanned) {
        UiStrPrintf(num, sizeof(num), "%u / %u MB/s", d->MinMbs, d->MaxMbs);
        SbAdd(&sb, num);
      } else {
        SbAdd(&sb, "-");
      }
      SbAdd(&sb, "</td></tr>\n");
    }
    SbAdd(&sb, "</table>\n");
  }

  /* ---- cpu test ---- */
  SbAdd(&sb, "<h2>CPU 检测</h2><table>\n");
  UiStrPrintf(num, sizeof(num), "%s（%s）", gResults.CpuTested ? L("已执行") : L("未执行"),
              LevelWord(gResults.HealthCpu));
  RowKV(&sb, "状态", num);
  UiStrPrintf(num, sizeof(num), "%u 个", gResults.CpuErrorCount);
  RowKV(&sb, "错误数", num);
  UiStrPrintf(num, sizeof(num), "%u ms", gResults.CpuElapsedMs);
  RowKV(&sb, "耗时", num);
  UiStrPrintf(num, sizeof(num), "%llu Mops", (unsigned long long)gResults.CpuMops);
  RowKV(&sb, "吞吐", num);
  SbAdd(&sb, "</table>\n");

  }

  /* ---- stress ---- */
  if (mask & SEC_STRESS) {
  SbAdd(&sb, "<h2>压力测试</h2><table>\n");
  UiStrPrintf(num, sizeof(num), "%s",
              gResults.StressRan ? (gResults.StressAborted ? L("已中止") : L("已执行")) : L("未执行"));
  RowKV(&sb, "状态", num);
  UiStrPrintf(num, sizeof(num), "%s",
              gResults.StressRan ? (gResults.StressErrorCount ? L("发现异常") : L("未发现异常"))
                                 : "n/a");
  RowKV(&sb, "结论", num);
  UiStrPrintf(num, sizeof(num), "%u 秒", gResults.StressSeconds);
  RowKV(&sb, "运行时长", num);
  UiStrPrintf(num, sizeof(num), "%u 线程 / %llu 次循环", gResults.StressCpuThreads,
              (unsigned long long)gResults.StressCpuLoops);
  RowKV(&sb, "处理器压力", num);
  UiStrPrintf(num, sizeof(num), "%llu MB",
              (unsigned long long)(gResults.StressMemBytes / (1024 * 1024)));
  RowKV(&sb, "内存压力流量", num);
  UiStrPrintf(num, sizeof(num), "%llu MB",
              (unsigned long long)(gResults.StressDiskBytes / (1024 * 1024)));
  RowKV(&sb, "硬盘只读流量", num);
  UiStrPrintf(num, sizeof(num), "%u 个", gResults.StressErrorCount);
  RowKV(&sb, "捕获异常", num);
  SbAdd(&sb, "</table>\n");

  /* ---- erase ---- */
  SbAdd(&sb, "<h2>擦除记录</h2><table>\n");
  UiStrPrintf(num, sizeof(num), "%u 次", gResults.EraseCount);
  RowKV(&sb, "擦除次数", num);
  RowKV(&sb, "最近目标", gResults.EraseLastTarget[0] ? gResults.EraseLastTarget : "n/a");
  SbAdd(&sb, "</table>\n");
  }

  SbAdd(&sb, "<footer>由 UCTS v");
  SbAdd(&sb, UCTS_VERSION);
  SbAdd(&sb, "（UEFI Component Test Suite）生成 · 内存 / 存储 / 处理器检测 · ");
  UiFormatTime(num, sizeof(num));
  SbAdd(&sb, num);
  SbAdd(&sb, "</footer>\n</main></body></html>\n");

  /* echo the report to COM1 so the content can be inspected in a harness that
     only reflects pre-existing host files (QEMU vvfat) */
  DbgStr("report: len=");
  DbgDec(sb.Len);
  DbgStr(" overflow=");
  DbgDec(sb.Overflow ? 1 : 0);
  DbgStr("\n----8<----\n");
  DbgStr(buffer);
  DbgStr("\n---->8----\n");

  status = UiWriteFile(app->Sys.BootRoot, name, buffer, sb.Len);
  if (!EFI_ERROR(status)) {
    status = ReportVerify(app->Sys.BootRoot, name, buffer, sb.Len);
  }
  app->BS->FreePool(buffer);
  return status;
}

/* ---------------------------------------------------------------- plain text */
static VOID TxtHead(STRBUF *sb, const char *title)
{
  SbAdd(sb, "\n[");
  SbAdd(sb, title);
  SbAdd(sb, "]\n");
}

static VOID TxtRow(STRBUF *sb, const char *key, const char *value)
{
  SbAdd(sb, "  ");
  SbAdd(sb, key);
  SbAdd(sb, ": ");
  SbAdd(sb, value && value[0] ? value : "n/a");
  SbAdd(sb, "\n");
}

static VOID TxtRowNum(STRBUF *sb, const char *key, const char *fmt, UINT32 value)
{
  char num[64];
  UiStrPrintf(num, sizeof(num), fmt, value);
  TxtRow(sb, key, num);
}

/* Machine readable session log: same data as the HTML report, one field per
   line, so it can be attached to a ticket or diffed between runs. */
static EFI_STATUS ReportExportText(UI_APP *app, const char *name)
{
  char *buffer = 0;
  STRBUF sb;
  EFI_STATUS status;
  char num[96];
  UINT32 i;
  UINTN size = 64 * 1024;

  if (app == 0 || app->Sys.BootRoot == 0) {
    return EFI_NOT_FOUND;
  }
  status = app->BS->AllocatePool(EfiBootServicesData, size, (VOID **)&buffer);
  if (EFI_ERROR(status)) {
    return status;
  }
  SbInit(&sb, buffer, size);

  SbAdd(&sb, "UCTS - UEFI Component Test Suite\n");
  SbAdd(&sb, "Diagnostic report (plain text)\n");
  TxtRow(&sb, "Version", UCTS_VERSION);
  TxtRow(&sb, "Author", UCTS_AUTHOR);
  UiFormatTime(num, sizeof(num));
  TxtRow(&sb, "Generated", num);
  TxtRow(&sb, "Language", ILangName());

  TxtHead(&sb, "Platform");
  TxtRow(&sb, "System vendor", app->Sys.SysVendor);
  TxtRow(&sb, "System model", app->Sys.SysProduct);
  TxtRow(&sb, "System version", app->Sys.SysVersion);
  TxtRow(&sb, "System serial", app->Sys.SysSerial);
  TxtRow(&sb, "Board", app->Sys.BoardProduct);
  TxtRow(&sb, "BIOS vendor", app->Sys.BiosVendor);
  TxtRow(&sb, "BIOS version", app->Sys.BiosVersion);
  TxtRow(&sb, "BIOS date", app->Sys.BiosDate);
  UiStrPrintf(num, sizeof(num), "%u.%02u", app->Sys.FirmwareRevision >> 16,
              (app->Sys.FirmwareRevision >> 8) & 0xFF);
  TxtRow(&sb, "UEFI revision", num);
  UiStrPrintf(num, sizeof(num), "SMBIOS %u.%u", app->Sys.SmbiosMajor, app->Sys.SmbiosMinor);
  TxtRow(&sb, "SMBIOS", app->Sys.SmbiosAvailable ? num : "不可用");

  TxtHead(&sb, "Processor");
  TxtRow(&sb, "Model", app->Sys.CpuBrand);
  UiStrPrintf(num, sizeof(num), "Family %u / Model %u / Stepping %u", app->Sys.CpuFamily,
              app->Sys.CpuModel, app->Sys.CpuStepping);
  TxtRow(&sb, "ID", num);
  UiStrPrintf(num, sizeof(num), "%u cores / %u threads", app->Sys.Cores, app->Sys.Threads);
  TxtRow(&sb, "Topology", num);
  TxtRowNum(&sb, "Base clock MHz", "%u", app->Sys.CpuBaseMhz);
  TxtRowNum(&sb, "Max clock MHz", "%u", app->Sys.CpuMaxMhz);

  TxtHead(&sb, "Memory topology");
  GfxFormatSize(num, sizeof(num), app->Sys.InstalledRam);
  TxtRow(&sb, "Installed", num);
  GfxFormatSize(num, sizeof(num), app->Sys.TestableRam);
  TxtRow(&sb, "Testable", num);
  if (app->Sys.DominantMemSpeed) {
    UiStrPrintf(num, sizeof(num), "%s-%u MT/s", SysMemTypeName(app->Sys.DominantMemType),
                app->Sys.DominantMemSpeed);
  } else {
    UiStrPrintf(num, sizeof(num), "%s", SysMemTypeName(app->Sys.DominantMemType));
  }
  TxtRow(&sb, "Type", num);
  for (i = 0; i < app->Sys.DimmCount && i < 16; i++) {
    DIMM_INFO *d = &app->Sys.Dimms[i];
    DIMM_GEO geo;
    SysDimmGeometry(d, &geo);
    GfxFormatSize(num, sizeof(num), d->SizeBytes);
    SbAdd(&sb, "  Slot ");
    SbAdd(&sb, d->Locator);
    SbAdd(&sb, "  ");
    SbAdd(&sb, num);
    SbAdd(&sb, "  ");
    SbAdd(&sb, SysMemTypeName(d->MemoryType));
    UiStrPrintf(num, sizeof(num), "  %u MT/s  x%u/%uR/%u chips  errors %u\n",
                d->ConfiguredSpeed ? d->ConfiguredSpeed : d->Speed, geo.DeviceWidth, geo.Ranks,
                geo.TotalDevices, gResults.MemDimmErrors[i]);
    SbAdd(&sb, num);
  }

  if (PciCount()) {
    TxtHead(&sb, "PCI/PCIe devices");
    UiStrPrintf(num, sizeof(num), "%u devices / %u endpoints / %u networks", PciCount(),
                PciEndpointCount(), NetCount());
    TxtRow(&sb, "Inventory", num);
    for (i = 0; i < PciCount() && i < PCI_MAX_DEVS; i++) {
      PCI_DEV *dev = PciAt(i);
      char addr[24];
      char link[24];
      char asset[72];
      PciFormatAddress(dev, addr, sizeof(addr));
      PciLinkText(dev, link, sizeof(link));
      AssetText(dev, asset, sizeof(asset));
      SbAdd(&sb, "  ");
      SbAdd(&sb, addr);
      SbAdd(&sb, "  ");
      SbAdd(&sb, L(PciClassText(dev)));
      UiStrPrintf(num, sizeof(num), "  %04X:%04X  %s", dev->VendorId, dev->DeviceId, link);
      SbAdd(&sb, num);
      if (asset[0]) {
        SbAdd(&sb, "  ");
        SbAdd(&sb, asset);
      }
      SbAdd(&sb, "\n");
    }
  }

  TxtHead(&sb, "Memory test");
  UiStrPrintf(num, sizeof(num), "%s / %s", gResults.MemTested ? L("已执行") : L("未执行"),
              LevelWord(gResults.HealthMem));
  TxtRow(&sb, "Status", num);
  UiStrPrintf(num, sizeof(num), "%u / %u", gResults.MemPatternsRun, gResults.MemPatternCount);
  TxtRow(&sb, "Patterns", num);
  TxtRowNum(&sb, "Iterations", "%u", gResults.MemIterations);
  TxtRowNum(&sb, "Errors", "%u", gResults.MemErrorCount);
  UiStrPrintf(num, sizeof(num), "%u%%", (gResults.MemCoveragePm + 5) / 10);
  TxtRow(&sb, "Coverage", num);
  TxtRowNum(&sb, "Bandwidth MB/s", "%u", gResults.MemBandwidthMbs);
  TxtRowNum(&sb, "Elapsed ms", "%u", gResults.MemElapsedMs);
  TxtRowNum(&sb, "Errors located on a chip", "%u", gResults.MemDeviceMapped);
  if (gResults.MemFirstErrorOffsetValid) {
    UiStrPrintf(num, sizeof(num), "0x%llX", (unsigned long long)gResults.MemFirstErrorOffset);
    TxtRow(&sb, "First error address", num);
  }

  TxtHead(&sb, "Storage");
  if (DiskCount() == 0) {
    SbAdd(&sb, "  ");
    SbAdd(&sb, "未发现块设备");
    SbAdd(&sb, "\n");
  }
  for (i = 0; i < DiskCount(); i++) {
    DISK_DEV *d = DiskAt(i);
    SbAdd(&sb, "  #");
    UiStrPrintf(num, sizeof(num), "%u", i + 1);
    SbAdd(&sb, num);
    SbAdd(&sb, "  ");
    SbAdd(&sb, d->Model[0] ? d->Model : d->PathText);
    SbAdd(&sb, "  ");
    SbAdd(&sb, DiskIfaceName(d->Iface));
    SbAdd(&sb, "  health ");
    SbAdd(&sb, d->HealthText[0] ? d->HealthText : "n/a");
    if (d->Scanned) {
      UiStrPrintf(num, sizeof(num), "  scan %u errors  %u/%u MB/s\n", d->ScanErrors, d->MinMbs,
                  d->MaxMbs);
      SbAdd(&sb, num);
    } else {
      SbAdd(&sb, "  ");
      SbAdd(&sb, "未扫描");
      SbAdd(&sb, "\n");
    }
  }

  TxtHead(&sb, "CPU test");
  UiStrPrintf(num, sizeof(num), "%s / %s", gResults.CpuTested ? L("已执行") : L("未执行"),
              LevelWord(gResults.HealthCpu));
  TxtRow(&sb, "Status", num);
  TxtRowNum(&sb, "Errors", "%u", gResults.CpuErrorCount);
  TxtRowNum(&sb, "Elapsed ms", "%u", gResults.CpuElapsedMs);
  UiStrPrintf(num, sizeof(num), "%llu", (unsigned long long)gResults.CpuMops);
  TxtRow(&sb, "MOPS", num);

  TxtHead(&sb, "Stress test");
  SbAdd(&sb, "  ");
  SbAdd(&sb, gResults.StressRan ? (gResults.StressAborted ? "已中止" : "已完成") : "未执行");
  if (gResults.StressRan) {
    SbAdd(&sb, "  ");
    SbAdd(&sb, gResults.StressErrorCount ? "发现异常" : "未发现异常");
  }
  SbAdd(&sb, "\n");
  TxtRowNum(&sb, "Seconds", "%u", gResults.StressSeconds);
  UiStrPrintf(num, sizeof(num), "%u threads / %llu loops", gResults.StressCpuThreads,
              (unsigned long long)gResults.StressCpuLoops);
  TxtRow(&sb, "CPU stress", num);
  UiStrPrintf(num, sizeof(num), "%llu MB", (unsigned long long)(gResults.StressMemBytes >> 20));
  TxtRow(&sb, "Memory traffic", num);
  UiStrPrintf(num, sizeof(num), "%llu MB", (unsigned long long)(gResults.StressDiskBytes >> 20));
  TxtRow(&sb, "Disk read", num);
  TxtRowNum(&sb, "Faults", "%u", gResults.StressErrorCount);

  TxtHead(&sb, "Erase log");
  TxtRowNum(&sb, "Erases", "%u", gResults.EraseCount);
  TxtRow(&sb, "Last target", gResults.EraseLastTarget);

  SbAdd(&sb, "\n-- end of report --\n");

  DbgStr("report(txt): len=");
  DbgDec(sb.Len);
  DbgStr(" overflow=");
  DbgDec(sb.Overflow ? 1 : 0);
  DbgStr("\n----8<----\n");
  DbgStr(buffer);
  DbgStr("\n---->8----\n");

  status = UiWriteFile(app->Sys.BootRoot, name, buffer, sb.Len);
  if (!EFI_ERROR(status)) {
    status = ReportVerify(app->Sys.BootRoot, name, buffer, sb.Len);
  }
  app->BS->FreePool(buffer);
  return status;
}

/* ---------------------------------------------------------------- public API */
EFI_STATUS ReportExportAll(UI_APP *app)
{
  return ReportExportHtml(app, SEC_ALL, "UCTS-REPORT.HTM");
}

EFI_STATUS ReportExportSystemInfo(UI_APP *app)
{
  return ReportExportHtml(app, SEC_INFO, "UCTS-SYSINFO.HTM");
}

EFI_STATUS ReportExportTests(UI_APP *app)
{
  return ReportExportHtml(app, SEC_TEST, "UCTS-TEST.HTM");
}

EFI_STATUS ReportExportStress(UI_APP *app)
{
  return ReportExportHtml(app, SEC_STRESS, "UCTS-STRESS.HTM");
}

EFI_STATUS ReportExportPlain(UI_APP *app)
{
  return ReportExportText(app, "UCTS-REPORT.TXT");
}

/* Writes the whole session bundle and returns how many files landed on the
   volume, so one key press archives everything. */
UINT32 ReportExportBundle(UI_APP *app, UINT32 *failed)
{
  UINT32 ok = 0;
  UINT32 bad = 0;
  EFI_STATUS status;

  status = ReportExportHtml(app, SEC_ALL, "UCTS-REPORT.HTM");
  if (EFI_ERROR(status)) { bad++; } else { ok++; }
  status = ReportExportSystemInfo(app);
  if (EFI_ERROR(status)) { bad++; } else { ok++; }
  status = ReportExportTests(app);
  if (EFI_ERROR(status)) { bad++; } else { ok++; }
  status = ReportExportStress(app);
  if (EFI_ERROR(status)) { bad++; } else { ok++; }
  status = ReportExportPlain(app);
  if (EFI_ERROR(status)) { bad++; } else { ok++; }

  if (failed) {
    *failed = bad;
  }
  return ok;
}
