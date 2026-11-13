/* page_sysinfo.c - full platform inventory: SMBIOS, CPUID, memory topology. */
#include "pages.h"
#include "results.h"
#include "disk.h"
#include "pci.h"

static UINT32 gInfoScroll;

static VOID KV(GFX_RECT *r, const char *key, const char *value, UINT32 valueColor)
{
  UINT32 kw = GfxTextWidth(key, 1) + 12;
  GfxText(r->X, r->Y, key, gTheme.Muted, 1);
  GfxTextClipped(r->X + kw, r->Y, r->W > kw ? r->W - kw : 1, value, valueColor, 1);
  r->Y += 22;
}

VOID PageSysInfoEnter(VOID)
{
  gInfoScroll = 0;
}

VOID PageSysInfoDraw(GFX_RECT content)
{
  UINT32 gap = 12;
  UINT32 leftW = (content.W * 62) / 100;
  UINT32 rightW = content.W - leftW - gap;
  GFX_RECT sysCard = { content.X, content.Y, leftW, 268 };
  GFX_RECT cpuCard = { content.X, content.Y + 268 + gap, leftW,
                       content.H - 268 - gap };
  GFX_RECT featCard = { content.X + leftW + gap, content.Y, rightW, 288 };
  GFX_RECT dimmCard = { content.X + leftW + gap, content.Y + 288 + gap, rightW,
                        content.H - 288 - gap };
  char buf[96];
  char buf2[96];

  /* ---------------- system / firmware ---------------- */
  UiCard(sysCard, "系统与固件", "SMBIOS 类型 0 / 1 / 2");
  {
    GFX_RECT inner = UiCardInner(sysCard);
    GFX_RECT col = inner;
    col.W = inner.W / 2 - 6;
    KV(&col, "系统厂商", gApp.Sys.SysVendor[0] ? gApp.Sys.SysVendor : "n/a", gTheme.Text);
    KV(&col, "系统型号", gApp.Sys.SysProduct[0] ? gApp.Sys.SysProduct : "n/a", gTheme.Text);
    KV(&col, "系统版本", gApp.Sys.SysVersion[0] ? gApp.Sys.SysVersion : "n/a", gTheme.Text);
    KV(&col, "系统序列号", gApp.Sys.SysSerial[0] ? gApp.Sys.SysSerial : "n/a", gTheme.Text);
    KV(&col, "主板厂商", gApp.Sys.BoardVendor[0] ? gApp.Sys.BoardVendor : "n/a", gTheme.Text);
    KV(&col, "主板型号", gApp.Sys.BoardProduct[0] ? gApp.Sys.BoardProduct : "n/a", gTheme.Text);
    KV(&col, "BIOS 厂商", gApp.Sys.BiosVendor[0] ? gApp.Sys.BiosVendor : "n/a", gTheme.Text);
    UiStrPrintf(buf, sizeof(buf), "%s %s", gApp.Sys.BiosVersion, gApp.Sys.BiosDate);
    KV(&col, "BIOS 版本", buf, gTheme.Text);

    col = inner;
    col.X = inner.X + inner.W / 2 + 6;
    col.W = inner.W / 2 - 6;
    UiStrPrintf(buf, sizeof(buf), "UEFI %u.%u",
                gApp.Sys.FirmwareRevision >> 16, (gApp.Sys.FirmwareRevision >> 8) & 0xFF);
    KV(&col, "固件规范", buf, gTheme.Text);
    KV(&col, "固件厂商", gApp.Sys.FirmwareVendor[0] ? gApp.Sys.FirmwareVendor : "n/a", gTheme.Text);
    UiStrPrintf(buf, sizeof(buf), "%u.%u（%s）", gApp.Sys.SmbiosMajor, gApp.Sys.SmbiosMinor,
                gApp.Sys.SmbiosAvailable ? L("可用") : L("不可用"));
    KV(&col, "SMBIOS", buf, gApp.Sys.SmbiosAvailable ? gTheme.Ok : gTheme.Crit);
    UiStrPrintf(buf, sizeof(buf), "%u 个描述符", gApp.Sys.MemoryMapEntries);
    KV(&col, "内存映射", buf, gTheme.Text);
    UiStrPrintf(buf, sizeof(buf), "%s", gApp.Sys.BootVolumeName[0] ? gApp.Sys.BootVolumeName : "n/a");
    KV(&col, "引导卷", buf, gApp.Sys.BootRoot ? gTheme.Text : gTheme.Warn);
    GfxFormatSize(buf, sizeof(buf), gApp.Sys.UsableRam);
    KV(&col, "可用内存", buf, gTheme.Text);
    UiStrPrintf(buf, sizeof(buf), "%u 个设备", DiskCount());
    KV(&col, "块设备", buf, gTheme.Text);
    if (PciSkipped()) {
      UiStrPrintf(buf, sizeof(buf), "%u 个设备 +%u 未列出", PciCount(), PciSkipped());
    } else {
      UiStrPrintf(buf, sizeof(buf), "%u 个设备", PciCount());
    }
    KV(&col, "PCIe 设备", buf, PciCount() ? gTheme.Text : gTheme.Warn);
    UiStrPrintf(buf, sizeof(buf), "x86-64 · %u 线程", gApp.Sys.LogicalCpus);
    KV(&col, "体系结构", buf, gTheme.Text);
  }

  /* ---------------- cpu ---------------- */
  UiCard(cpuCard, "处理器与缓存", "CPUID");
  {
    GFX_RECT inner = UiCardInner(cpuCard);
    GFX_RECT col = inner;
    col.W = inner.W / 2 - 6;
    GfxTextClipped(inner.X, inner.Y, inner.W,
                   gApp.Sys.CpuBrand[0] ? gApp.Sys.CpuBrand : "未知处理器", gTheme.Text, 1);
    col.Y += 26;
    UiStrPrintf(buf, sizeof(buf), "F%u / M%u / S%u",
                gApp.Sys.CpuFamily, gApp.Sys.CpuModel, gApp.Sys.CpuStepping);
    KV(&col, "标识", buf, gTheme.Text);
    UiStrPrintf(buf, sizeof(buf), "%u 核 / %u 线程", gApp.Sys.Cores, gApp.Sys.Threads);
    KV(&col, "拓扑", buf, gTheme.Text);
    if (gApp.Sys.CpuMaxMhz > gApp.Sys.CpuBaseMhz) {
      UiStrPrintf(buf, sizeof(buf), "%u / %u MHz", gApp.Sys.CpuBaseMhz, gApp.Sys.CpuMaxMhz);
    } else {
      UiStrPrintf(buf, sizeof(buf), "%u MHz", gApp.Sys.CpuBaseMhz);
    }
    KV(&col, "频率", buf, gTheme.Text);
    UiStrPrintf(buf, sizeof(buf), "%s", gApp.Sys.CpuSocket[0] ? gApp.Sys.CpuSocket : "n/a");
    KV(&col, "插槽", buf, gTheme.Text);

    col = inner;
    col.X = inner.X + inner.W / 2 + 6;
    col.W = inner.W / 2 - 6;
    col.Y += 26;
    if (gApp.Sys.L1Bytes) {
      UiStrPrintf(buf, sizeof(buf), "%u KB", (UINT32)(gApp.Sys.L1Bytes / 1024));
    } else {
      UiStrPrintf(buf, sizeof(buf), "n/a");
    }
    KV(&col, "L1 缓存", buf, gTheme.Text);
    if (gApp.Sys.L2Bytes) {
      GfxFormatSize(buf, sizeof(buf), gApp.Sys.L2Bytes);
    } else {
      UiStrPrintf(buf, sizeof(buf), "n/a");
    }
    KV(&col, "L2 缓存", buf, gTheme.Text);
    if (gApp.Sys.L3Bytes) {
      GfxFormatSize(buf, sizeof(buf), gApp.Sys.L3Bytes);
    } else {
      UiStrPrintf(buf, sizeof(buf), "n/a");
    }
    KV(&col, "L3 缓存", buf, gTheme.Text);
    UiStrPrintf(buf, sizeof(buf), "%u MHz", (UINT32)(SysTscHz() / 1000000));
    KV(&col, "TSC 频率", buf, gTheme.Text);
  }
  {
    GFX_RECT inner = UiCardInner(cpuCard);
    UINT32 y = inner.Y + 110;
    UiDivider((GFX_RECT){ inner.X, y, inner.W, 1 });
    {
      GFX_RECT col = inner;
      char v[48];
      col.Y = y + 12;
      col.W = inner.W / 2 - 6;
      UiStrPrintf(v, sizeof(v), "%s", gApp.Sys.CpuVendor[0] ? gApp.Sys.CpuVendor : "n/a");
      KV(&col, "厂商串", v, gTheme.Text);
      UiStrPrintf(v, sizeof(v), "0x%X / 0x%X", gApp.Sys.MaxBasicLeaf, gApp.Sys.MaxExtLeaf);
      KV(&col, "叶范围", v, gTheme.Text);
      UiStrPrintf(v, sizeof(v), "%u / %u 位", gApp.Sys.PhysAddrBits, gApp.Sys.VirtAddrBits);
      KV(&col, "地址位", v, gTheme.Text);
      col = inner;
      col.X = inner.X + inner.W / 2 + 6;
      col.W = inner.W / 2 - 6;
      col.Y = y + 12;
      UiStrPrintf(v, sizeof(v), "%u 字节", gApp.Sys.CacheLineBytes);
      KV(&col, "缓存行", v, gTheme.Text);
      UiStrPrintf(v, sizeof(v), "%u 条", gApp.Sys.DimmPresentCount);
      KV(&col, "内存条", v, gTheme.Text);
      if (gApp.Sys.MaxCapacity) {
        UiStrPrintf(v, sizeof(v), "%u GB", (UINT32)(gApp.Sys.MaxCapacity / (1024ull * 1024 * 1024)));
      } else {
        UiStrPrintf(v, sizeof(v), "%s", "未报告");
      }
      KV(&col, "最大容量", v, gTheme.Text);
    }
    y += 84;
    UiDivider((GFX_RECT){ inner.X, y, inner.W, 1 });
    UiStrPrintf(buf, sizeof(buf), "最大逻辑处理器 %u · SMT %u 线程/核",
                gApp.Sys.LogicalCpus, gApp.Sys.SmtPerCore ? gApp.Sys.SmtPerCore : 1);
    GfxTextClipped(inner.X, y + 10, inner.W, buf, gTheme.SubText, 1);
    UiStrPrintf(buf, sizeof(buf), L("缓存层级：L1 "));
    if (gApp.Sys.L1Bytes) {
      UiStrPrintf(buf2, sizeof(buf2), "%u KB", (UINT32)(gApp.Sys.L1Bytes / 1024));
    } else {
      UiStrPrintf(buf2, sizeof(buf2), "n/a");
    }
    GfxAppendStr(buf, sizeof(buf), buf2);
    GfxAppendStr(buf, sizeof(buf), " / L2 ");
    if (gApp.Sys.L2Bytes) {
      GfxFormatSize(buf2, sizeof(buf2), gApp.Sys.L2Bytes);
    } else {
      UiStrPrintf(buf2, sizeof(buf2), "n/a");
    }
    GfxAppendStr(buf, sizeof(buf), buf2);
    GfxAppendStr(buf, sizeof(buf), " / L3 ");
    if (gApp.Sys.L3Bytes) {
      GfxFormatSize(buf2, sizeof(buf2), gApp.Sys.L3Bytes);
    } else {
      UiStrPrintf(buf2, sizeof(buf2), "n/a");
    }
    GfxAppendStr(buf, sizeof(buf), buf2);
    GfxTextClipped(inner.X, y + 30, inner.W, buf, gTheme.Muted, 1);
  }

  /* ---------------- feature bits ---------------- */
  UiCard(featCard, "指令集特性", "绿色 = 支持");
  {
    GFX_RECT inner = UiCardInner(featCard);
    static const char *const names[] = {
      "FPU", "TSC", "MSR", "PAE", "CX8", "SEP", "CMOV", "CLFSH",
      "MMX", "SSE", "SSE2", "SSE3", "SSSE3", "SSE4.1", "SSE4.2", "POPCNT",
      "AES-NI", "XSAVE", "AVX", "AVX2", "AVX512F", "VMX", "SMEP", "SMAP",
      "LM", "NX", "RDRAND", "X2APIC", "TSC-DL", "PCID", "INVPCID", "UMIP"
    };
    UINT32 i;
    UINT32 cols = 3;
    UINT32 cw = inner.W / cols;
    UINT32 ch = 21;
    for (i = 0; i < 32; i++) {
      BOOLEAN has = FALSE;
      switch (i) {
      case 0: has = (gApp.Sys.FeaturesEdx & (1u << 0)) != 0; break;
      case 1: has = (gApp.Sys.FeaturesEdx & (1u << 4)) != 0; break;
      case 2: has = (gApp.Sys.FeaturesEdx & (1u << 5)) != 0; break;
      case 3: has = (gApp.Sys.FeaturesEdx & (1u << 6)) != 0; break;
      case 4: has = (gApp.Sys.FeaturesEdx & (1u << 8)) != 0; break;
      case 5: has = (gApp.Sys.FeaturesEdx & (1u << 11)) != 0; break;
      case 6: has = (gApp.Sys.FeaturesEdx & (1u << 15)) != 0; break;
      case 7: has = (gApp.Sys.FeaturesEdx & (1u << 19)) != 0; break;
      case 8: has = (gApp.Sys.FeaturesEdx & (1u << 23)) != 0; break;
      case 9: has = (gApp.Sys.FeaturesEdx & (1u << 25)) != 0; break;
      case 10: has = (gApp.Sys.FeaturesEdx & (1u << 26)) != 0; break;
      case 11: has = (gApp.Sys.FeaturesEcx & (1u << 0)) != 0; break;
      case 12: has = (gApp.Sys.FeaturesEcx & (1u << 9)) != 0; break;
      case 13: has = (gApp.Sys.FeaturesEcx & (1u << 19)) != 0; break;
      case 14: has = (gApp.Sys.FeaturesEcx & (1u << 20)) != 0; break;
      case 15: has = (gApp.Sys.FeaturesEcx & (1u << 23)) != 0; break;
      case 16: has = (gApp.Sys.FeaturesEcx & (1u << 25)) != 0; break;
      case 17: has = (gApp.Sys.FeaturesEcx & (1u << 26)) != 0; break;
      case 18: has = (gApp.Sys.FeaturesEcx & (1u << 28)) != 0; break;
      case 19: has = (gApp.Sys.Leaf7Ebx & (1u << 5)) != 0; break;
      case 20: has = (gApp.Sys.Leaf7Ebx & (1u << 16)) != 0; break;
      case 21: has = (gApp.Sys.FeaturesEcx & (1u << 5)) != 0; break;
      case 22: has = (gApp.Sys.Leaf7Ebx & (1u << 7)) != 0; break;
      case 23: has = (gApp.Sys.Leaf7Ebx & (1u << 20)) != 0; break;
      case 24: has = (gApp.Sys.ExtEdx & (1u << 29)) != 0; break;
      case 25: has = (gApp.Sys.ExtEdx & (1u << 20)) != 0; break;
      case 26: has = (gApp.Sys.FeaturesEcx & (1u << 30)) != 0; break;
      case 27: has = (gApp.Sys.FeaturesEcx & (1u << 21)) != 0; break;
      case 28: has = (gApp.Sys.Leaf7Ebx & (1u << 24)) != 0; break;
      case 29: has = (gApp.Sys.Leaf7Ebx & (1u << 17)) != 0; break;
      case 30: has = (gApp.Sys.Leaf7Ebx & (1u << 10)) != 0; break;
      default: has = (gApp.Sys.Leaf7Ecx & (1u << 2)) != 0; break;
      }
      {
        UINT32 cx = inner.X + (i % cols) * cw;
        UINT32 cy = inner.Y + (i / cols) * ch;
        GFX_RECT dot = { cx, cy + 5, 8, 8 };
        GfxFillRound(dot, 4, has ? gTheme.Ok : gTheme.IdleSoft);
        GfxText(cx + 14, cy + 1, names[i], has ? gTheme.Text : gTheme.Muted, 1);
      }
    }
    (VOID)buf2;
  }

  /* ---------------- memory topology ---------------- */
  UiCard(dimmCard, "内存拓扑", 0);
  {
    GFX_RECT inner = UiCardInner(dimmCard);
    UINT32 i;
    UINT32 rows = (inner.H - 28) / 26;
    GfxFormatSize(buf, sizeof(buf), gApp.Sys.InstalledRam);
    UiStrPrintf(buf2, sizeof(buf2), "已装 %s", buf);
    GfxTextRight(dimmCard.X + dimmCard.W - 16, dimmCard.Y + 14, buf2, gTheme.SubText, 1);

    {
      static const char *const cols[4] = { "插槽", "容量", "类型 / 速率", "厂商" };
      UINT32 widths[4];
      widths[0] = inner.W * 27 / 100;
      widths[1] = inner.W * 15 / 100;
      widths[2] = inner.W * 31 / 100;
      widths[3] = inner.W - widths[0] - widths[1] - widths[2];
      UiTableHeader((GFX_RECT){ inner.X, inner.Y, inner.W, 24 }, cols, widths, 4);
      for (i = 0; i < rows && i < gApp.Sys.DimmCount; i++) {
        DIMM_INFO *d = &gApp.Sys.Dimms[i];
        const char *cells[4];
        if (!d->Present) {
          cells[0] = d->Locator[0] ? d->Locator : "空槽";
          cells[1] = "空";
          cells[2] = "-";
          cells[3] = "-";
        } else {
          GfxFormatSize(buf, sizeof(buf), d->SizeBytes);
          cells[0] = d->Locator[0] ? d->Locator : "DIMM";
          cells[1] = buf;
          UiStrPrintf(buf2, sizeof(buf2), "%s-%u", SysMemTypeName(d->MemoryType),
                      d->ConfiguredSpeed ? d->ConfiguredSpeed : d->Speed);
          cells[2] = buf2;
          cells[3] = d->Manufacturer[0] ? d->Manufacturer : "n/a";
        }
        UiTableRow((GFX_RECT){ inner.X, inner.Y + 26 + i * 26, inner.W, 24 }, cells, widths, 4,
                   d->Present ? gTheme.Text : gTheme.Muted, FALSE, (i & 1) != 0);
      }
    }
    {
      UINT32 bottom = inner.Y + inner.H - 22;
      UiStrPrintf(buf, sizeof(buf), L("%u 槽位 · %u 通道 · 可测 "),
                  gApp.Sys.DimmCount, gApp.Sys.ChannelCount);
      GfxFormatSize(buf2, sizeof(buf2), gApp.Sys.TestableRam);
      GfxAppendStr(buf, sizeof(buf), buf2);
      GfxText(inner.X, bottom, buf, gTheme.SubText, 1);
    }
  }
}

VOID PageSysInfoPoint(UINT32 row)
{
  (VOID)row;
}

BOOLEAN PageSysInfoKey(EFI_INPUT_KEY key)
{
  if (key.UnicodeChar == '\r' || key.UnicodeChar == '\n') {
    SysCollect(&gApp.Sys, gApp.ImageHandle, gApp.ST);
    DiskEnumerate();
    PciEnumerate();
    PciBindAssets();
    UiSetStatus(1, "系统信息已重新采集");
    return TRUE;
  }
  if (key.ScanCode == SCAN_UP && gInfoScroll) {
    gInfoScroll--;
    return TRUE;
  }
  if (key.ScanCode == SCAN_DOWN) {
    gInfoScroll++;
    return TRUE;
  }
  return FALSE;
}
