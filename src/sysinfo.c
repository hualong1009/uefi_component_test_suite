/* sysinfo.c - CPUID / SMBIOS / memory map inventory. */
#include "sysinfo.h"
#include "i18n.h"
#include "gfx.h"
#include "debug.h"

static EFI_BOOT_SERVICES *gBS;
static EFI_SYSTEM_TABLE  *gST;

/* ------------------------------------------------------------ CPUID / TSC */
static VOID Cpuid(UINT32 leaf, UINT32 sub, UINT32 *a, UINT32 *b, UINT32 *c, UINT32 *d)
{
  __asm__ __volatile__("cpuid"
                       : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                       : "a"(leaf), "c"(sub));
}

static UINT64 CpuidMaxBasic(VOID)
{
  UINT32 a, b, c, d;
  Cpuid(0, 0, &a, &b, &c, &d);
  return a;
}

static UINT64 CpuidMaxExt(VOID)
{
  UINT32 a, b, c, d;
  Cpuid(0x80000000u, 0, &a, &b, &c, &d);
  return a;
}

UINT64 SysTsc(VOID)
{
  UINT32 lo, hi;
  __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
  return ((UINT64)hi << 32) | lo;
}

UINT64 SysTscHz(VOID)
{
  UINT32 a, b, c, d;
  UINT64 maxBasic = CpuidMaxBasic();

  /* KVM / Hyper-V tsc frequency leaf */
  Cpuid(0x40000010u, 0, &a, &b, &c, &d);
  if (b >= 100 && b <= 10000000) {
    return (UINT64)b * 1000;
  }
  /* TSC / crystal ratio */
  if (maxBasic >= 0x15) {
    Cpuid(0x15, 0, &a, &b, &c, &d);
    if (a && b && c) {
      return ((UINT64)c * (UINT64)b) / (UINT64)a;
    }
  }
  if (maxBasic >= 0x16) {
    Cpuid(0x16, 0, &a, &b, &c, &d);
    if (a >= 100) {
      return (UINT64)a * 1000000;
    }
  }
  /* fall back to a Stall based calibration */
  {
    UINT64 t0 = SysTsc();
    if (gBS) {
      gBS->Stall(20000);
    }
    return (SysTsc() - t0) * 50;
  }
}

UINT64 SysMsSince(UINT64 startTsc)
{
  UINT64 hz = SysTscHz();
  UINT64 delta = SysTsc() - startTsc;
  if (hz == 0) {
    return 0;
  }
  return (delta * 1000) / hz;
}

VOID SysDelayMs(UINTN ms)
{
  if (gBS) {
    gBS->Stall(ms * 1000);
  }
}

/* --------------------------------------------------------------- helpers */
static UINTN StrLen(const char *s)
{
  UINTN n = 0;
  while (s && s[n]) {
    n++;
  }
  return n;
}

static VOID StrCopyN(char *dst, UINTN cap, const char *src)
{
  UINTN i = 0;
  if (cap == 0) {
    return;
  }
  while (src && src[i] && i + 1 < cap) {
    dst[i] = src[i];
    i++;
  }
  dst[i] = 0;
}

static UINT32 ParseUint(const char **pp)
{
  UINT32 v = 0;
  const char *p = *pp;
  while (*p >= '0' && *p <= '9') {
    v = v * 10 + (UINT32)(*p - '0');
    p++;
  }
  *pp = p;
  return v;
}

/* "3.60GHz" / "2.00 GHz" / "2400 MHz" -> MHz */
static UINT32 ParseBrandMhz(const char *brand)
{
  const char *p = brand;
  while (*p) {
    const char *start = p;
    UINT32 whole = 0, frac = 0;
    BOOLEAN digits = FALSE;
    while (*p >= '0' && *p <= '9') {
      whole = whole * 10 + (UINT32)(*p - '0');
      digits = TRUE;
      p++;
    }
    if (digits && *p == '.') {
      p++;
      if (*p >= '0' && *p <= '9') {
        frac = (UINT32)(*p - '0');
        p++;
        if (*p >= '0' && *p <= '9') {
          p++;
        }
      }
    }
    if (digits) {
      const char *q = p;
      while (*q == ' ') {
        q++;
      }
      if ((q[0] == 'G' || q[0] == 'g') && (q[1] == 'H' || q[1] == 'h')) {
        return whole * 1000 + frac * 100;
      }
      if ((q[0] == 'M' || q[0] == 'm') && (q[1] == 'H' || q[1] == 'h')) {
        return whole * 10 + frac;
      }
    }
    p = start + 1;
  }
  return 0;
}

/* ------------------------------------------------------------ CPU gather */
static VOID GatherCpu(SYS_INFO *s)
{
  UINT32 a, b, c, d;
  UINT64 maxBasic = CpuidMaxBasic();
  UINT64 maxExt = CpuidMaxExt();
  char brand[64];
  UINT32 i;

  Cpuid(0, 0, &a, &b, &c, &d);
  s->MaxBasicLeaf = a;
  s->MaxExtLeaf = (UINT32)maxExt;
  ((UINT32 *)s->CpuVendor)[0] = b;
  ((UINT32 *)s->CpuVendor)[1] = d;
  ((UINT32 *)s->CpuVendor)[2] = c;
  s->CpuVendor[12] = 0;
  ((UINT32 *)brand)[0] = b;
  ((UINT32 *)brand)[1] = d;
  ((UINT32 *)brand)[2] = c;
  if (maxExt >= 0x80000004u) {
    for (i = 0; i < 3; i++) {
      Cpuid(0x80000002u + i, 0, &a, &b, &c, &d);
      ((UINT32 *)brand)[3 + i * 4 + 0] = a;
      ((UINT32 *)brand)[3 + i * 4 + 1] = b;
      ((UINT32 *)brand)[3 + i * 4 + 2] = c;
      ((UINT32 *)brand)[3 + i * 4 + 3] = d;
    }
    brand[48] = 0;
  } else {
    brand[0] = 0;
  }
  {
    /* trim leading whitespace */
    char *p = brand;
    while (*p == ' ') {
      p++;
    }
    StrCopyN(s->CpuBrand, sizeof(s->CpuBrand), p);
  }

  Cpuid(1, 0, &a, &b, &c, &d);
  s->CpuStepping = a & 0xF;
  s->CpuModel = (a >> 4) & 0xF;
  s->CpuFamily = (a >> 8) & 0xF;
  if (s->CpuFamily == 0xF) {
    s->CpuFamily += (a >> 20) & 0xFF;
  }
  if (s->CpuFamily == 0x6 || s->CpuFamily == 0xF) {
    s->CpuModel += ((a >> 16) & 0xF) << 4;
  }
  s->CpuBrandIndex = b & 0xFF;
  s->CacheLineBytes = ((b >> 8) & 0xFF) * 8;
  s->FeaturesEcx = c;
  s->FeaturesEbx = b;
  s->FeaturesEdx = d;

  if (maxBasic >= 7) {
    Cpuid(7, 0, &a, &b, &c, &d);
    s->HasLeaf7 = TRUE;
    s->Leaf7Ebx = b;
    s->Leaf7Ecx = c;
    s->Leaf7Edx = d;
  }
  if (maxExt >= 0x80000008u) {
    Cpuid(0x80000008u, 0, &a, &b, &c, &d);
    s->PhysAddrBits = a & 0xFF;
    s->VirtAddrBits = (a >> 8) & 0xFF;
  }
  if (maxExt >= 0x80000001u) {
    Cpuid(0x80000001u, 0, &a, &b, &c, &d);
    s->ExtEcx = c;
    s->ExtEdx = d;
    s->ExtEbx = b;
  }

  /* logical processors: MP services is authoritative */
  s->LogicalCpus = 1;
  {
    EFI_MP_SERVICES_PROTOCOL *mp = 0;
    if (!EFI_ERROR(gBS->LocateProtocol((EFI_GUID *)&gEfiMpServiceProtocolGuid, 0, (VOID **)&mp)) && mp) {
      UINTN total = 0, enabled = 0;
      if (!EFI_ERROR(mp->GetNumberOfProcessors(mp, &total, &enabled))) {
        s->LogicalCpus = (UINT32)(enabled ? enabled : total);
        if (s->LogicalCpus == 0) {
          s->LogicalCpus = 1;
        }
      }
    }
  }

  /* SMT / package topology from CPUID leaves 0x1F and 0x0B */
  {
    UINT32 topLeaf = 0;
    UINT32 threadsPerCore = 0, logicalPerPackage = 0;
    if (maxBasic >= 0x1F) {
      topLeaf = 0x1F;
    } else if (maxBasic >= 0x0B) {
      topLeaf = 0x0B;
    }
    if (topLeaf) {
      UINT32 sub;
      for (sub = 0; sub < 8; sub++) {
        UINT32 levelType, count;
        Cpuid(topLeaf, sub, &a, &b, &c, &d);
        levelType = (c >> 8) & 0xFF;
        count = b & 0xFFFF;
        if (levelType == 0 || count == 0) {
          break;
        }
        if (levelType == 1) {
          threadsPerCore = count;
        } else if (levelType == 2) {
          logicalPerPackage = count;
        }
      }
    }
    if (logicalPerPackage == 0) {
      UINT32 a1, b1, c1, d1;
      Cpuid(1, 0, &a1, &b1, &c1, &d1);
      logicalPerPackage = (b1 >> 16) & 0xFF;
      if (logicalPerPackage == 0 || logicalPerPackage > s->LogicalCpus) {
        logicalPerPackage = s->LogicalCpus;
      }
    }
    if (threadsPerCore == 0) {
      threadsPerCore = (s->FeaturesEdx & (1u << 28)) ? 2 : 1;
    }
    if (threadsPerCore > logicalPerPackage) {
      threadsPerCore = 1;
    }
    s->SmtPerCore = threadsPerCore;
    s->Threads = s->LogicalCpus;
    s->Cores = s->LogicalCpus / threadsPerCore;
    s->CpuPackageCount = s->LogicalCpus / logicalPerPackage;
    if (s->CpuPackageCount == 0) {
      s->CpuPackageCount = 1;
    }
  }
  if (s->Cores == 0) {
    s->Cores = s->LogicalCpus;
  }

  /* caches via deterministic cache parameters */
  if (maxBasic >= 4) {
    UINT32 idx;
    for (idx = 0; idx < 16; idx++) {
      UINT32 type, level;
      UINT64 ways, parts, line, sets, size;
      Cpuid(4, idx, &a, &b, &c, &d);
      type = a & 0x1F;
      if (type == 0) {
        break;
      }
      level = (a >> 5) & 0x7;
      ways = ((b >> 22) & 0x3FF) + 1;
      parts = ((b >> 12) & 0x3FF) + 1;
      line = (b & 0xFFF) + 1;
      sets = (UINT64)c + 1;
      size = ways * parts * line * sets;
      if (level == 1) {
        s->L1Bytes += size;
      } else if (level == 2) {
        s->L2Bytes = size;
      } else if (level == 3) {
        s->L3Bytes = size;
      }
    }
  }

  if (maxBasic >= 0x16) {
    Cpuid(0x16, 0, &a, &b, &c, &d);
    s->CpuBaseMhz = a;
    s->CpuMaxMhz = b;
  }
  if (s->CpuBaseMhz == 0) {
    s->CpuBaseMhz = ParseBrandMhz(s->CpuBrand);
  }
  if (s->CpuMaxMhz == 0) {
    s->CpuMaxMhz = s->CpuBaseMhz;
  }
}

/* ---------------------------------------------------------------- SMBIOS */
static const char *SmbiosString(EFI_SMBIOS_TABLE_HEADER *h, UINT8 index)
{
  const char *p;
  UINT8 i;
  if (index == 0 || h == 0) {
    return "";
  }
  p = ((const char *)h) + h->Length;
  for (i = 1; i < index; i++) {
    p += StrLen(p) + 1;
    if (*p == 0) {
      return "";
    }
  }
  return p;
}

/* SMBIOS type 17 size encoding: a plain MB count, 0 = empty slot, 0xFFFF =
   unknown, bit 15 set = KB count, and 0x7FFF = "see Extended Size" (used by
   every module of 32 GB and larger). */
static UINT64 DimmSize(SMBIOS_TYPE17 *d)
{
  if (d->Size == 0xFFFF || d->Size == 0) {
    return 0;
  }
  if (d->Size == 0x7FFF) {
    if (d->Length >= 0x20) {
      return ((UINT64)d->ExtendedSize & 0x7FFFFFFFULL) * 1024ULL * 1024ULL;
    }
    return 0;
  }
  if (d->Size & 0x8000) {
    return (UINT64)(d->Size & 0x7FFF) * 1024ULL;
  }
  return (UINT64)d->Size * 1024ULL * 1024ULL;
}

static VOID GatherSmbios(SYS_INFO *s)
{
  EFI_SMBIOS_PROTOCOL *smbios = 0;
  EFI_SMBIOS_TABLE_HEADER *rec = 0;
  UINT16 handle = 0xFFFE;   /* SMBIOS_HANDLE_PI_RESERVED: first record */
  UINT32 idx;
  UINT8 maxSocket = 0, maxChannel = 0;
  UINT32 typeCounts[64];
  UINT32 speedCounts[8];
  UINT16 speeds[8];

  for (idx = 0; idx < 64; idx++) {
    typeCounts[idx] = 0;
  }
  for (idx = 0; idx < 8; idx++) {
    speedCounts[idx] = 0;
    speeds[idx] = 0;
  }

  {
    EFI_STATUS st = gBS->LocateProtocol((EFI_GUID *)&gEfiSmbiosProtocolGuid, 0, (VOID **)&smbios);
    DbgStr("smbios: locate status=");
    DbgHex((UINT64)st);
    DbgStr("\n");
    if (EFI_ERROR(st) || smbios == 0) {
      return;
    }
  }
  s->SmbiosAvailable = TRUE;
  s->SmbiosMajor = smbios->MajorVersion;
  s->SmbiosMinor = smbios->MinorVersion;

  while (TRUE) {
    EFI_STATUS st = smbios->GetNext(smbios, &handle, 0, &rec, 0);
    DbgStr("smbios: getnext status=");
    DbgHex((UINT64)st);
    DbgStr(" type=");
    DbgDec(rec ? rec->Type : 999);
    DbgStr("\n");
    if (EFI_ERROR(st) || rec == 0) {
      break;
    }
    switch (rec->Type) {
    case 0: {
      SMBIOS_TYPE0 *t = (SMBIOS_TYPE0 *)rec;
      StrCopyN(s->BiosVendor, sizeof(s->BiosVendor), SmbiosString(rec, t->Vendor));
      StrCopyN(s->BiosVersion, sizeof(s->BiosVersion), SmbiosString(rec, t->BiosVersion));
      StrCopyN(s->BiosDate, sizeof(s->BiosDate), SmbiosString(rec, t->BiosReleaseDate));
      s->BiosCharacteristics = t->BiosCharacteristics;
      break;
    }
    case 1: {
      SMBIOS_TYPE1 *t = (SMBIOS_TYPE1 *)rec;
      StrCopyN(s->SysVendor, sizeof(s->SysVendor), SmbiosString(rec, t->Manufacturer));
      StrCopyN(s->SysProduct, sizeof(s->SysProduct), SmbiosString(rec, t->ProductName));
      StrCopyN(s->SysVersion, sizeof(s->SysVersion), SmbiosString(rec, t->Version));
      StrCopyN(s->SysSerial, sizeof(s->SysSerial), SmbiosString(rec, t->SerialNumber));
      break;
    }
    case 2: {
      SMBIOS_TYPE2 *t = (SMBIOS_TYPE2 *)rec;
      StrCopyN(s->BoardVendor, sizeof(s->BoardVendor), SmbiosString(rec, t->Manufacturer));
      StrCopyN(s->BoardProduct, sizeof(s->BoardProduct), SmbiosString(rec, t->ProductName));
      StrCopyN(s->BoardVersion, sizeof(s->BoardVersion), SmbiosString(rec, t->Version));
      break;
    }
    case 4: {
      SMBIOS_TYPE4 *t = (SMBIOS_TYPE4 *)rec;
      if (s->CpuSocket[0] == 0) {
        StrCopyN(s->CpuSocket, sizeof(s->CpuSocket), SmbiosString(rec, t->Socket));
      }
      if (s->Cores == 0) {
        UINT32 cores = t->CoreCount;
        UINT32 threads = t->ThreadCount;
        if (rec->Length >= 0x2A) {
          if (t->CoreCount2) {
            cores = t->CoreCount2;
          }
          if (t->ThreadCount2) {
            threads = t->ThreadCount2;
          }
        }
        if (cores && s->CpuPackageCount) {
          s->Cores = cores * s->CpuPackageCount;
        }
        if (threads && s->CpuPackageCount) {
          s->Threads = threads * s->CpuPackageCount;
        }
      }
      if (s->CpuBaseMhz == 0) {
        s->CpuBaseMhz = t->CurrentSpeed;
      }
      break;
    }
    case 16: {
      SMBIOS_TYPE16 *t = (SMBIOS_TYPE16 *)rec;
      if (t->MaximumCapacity != 0x80000000u) {
        s->MaxCapacity = (UINT64)t->MaximumCapacity * 1024ULL;
      }
      break;
    }
    case 17: {
      SMBIOS_TYPE17 *t = (SMBIOS_TYPE17 *)rec;
      DIMM_INFO *d;
      UINT64 size;
      if (s->DimmCount >= SYS_MAX_DIMMS) {
        break;
      }
      d = &s->Dimms[s->DimmCount];
      gBS->SetMem(d, sizeof(*d), 0);
      size = DimmSize(t);
      d->SmbiosHandle = rec->Handle;
      d->SizeBytes = size;
      d->Speed = t->Speed;
      d->ConfiguredSpeed = (rec->Length >= 0x22 && t->ConfiguredMemorySpeed) ? t->ConfiguredMemorySpeed : t->Speed;
      d->TotalWidth = t->TotalWidth;
      d->DataWidth = t->DataWidth;
      d->FormFactor = t->FormFactor;
      d->MemoryType = t->MemoryType;
      d->TypeDetail = t->TypeDetail;
      if (rec->Length >= 0x1C) {
        UINT8 rank = (UINT8)(t->Attributes & 0x0F);
        d->Rank = (rank >= 1 && rank <= 4) ? rank : 0;
      }
      d->Technology = (rec->Length >= 0x28) ? t->MemoryTechnology : 0;
      d->Ecc = (t->TotalWidth > t->DataWidth && t->TotalWidth != 0xFFFF) ? TRUE : FALSE;
      if (rec->Length >= 0x24) {
        d->VddMv = t->ConfiguredVoltage ? t->ConfiguredVoltage : t->MinimumVoltage;
      }
      StrCopyN(d->Locator, sizeof(d->Locator), SmbiosString(rec, t->DeviceLocator));
      StrCopyN(d->BankLocator, sizeof(d->BankLocator), SmbiosString(rec, t->BankLocator));
      StrCopyN(d->Manufacturer, sizeof(d->Manufacturer), SmbiosString(rec, t->Manufacturer));
      StrCopyN(d->Serial, sizeof(d->Serial), SmbiosString(rec, t->SerialNumber));
      StrCopyN(d->PartNumber, sizeof(d->PartNumber), SmbiosString(rec, t->PartNumber));
      d->Present = (size > 0) ? TRUE : FALSE;

      /* Locator topology heuristics: DIMM0_0_0 (BMC style), DIMM_A1, ChannelA-DIMM0 */
      d->Socket = 0;
      d->Channel = 0;
      d->Slot = 0;
      {
        const char *p = d->Locator;
        UINT32 nums[4];
        UINT32 n = 0;
        while (*p && n < 4) {
          if (*p >= '0' && *p <= '9') {
            nums[n++] = ParseUint(&p);
          } else {
            p++;
          }
        }
        if (n >= 3) {
          d->Socket = (UINT8)nums[0];
          d->Channel = (UINT8)nums[1];
          d->Slot = (UINT8)nums[2];
        } else if (n == 2) {
          d->Channel = (UINT8)(nums[0] ? nums[0] : 0);
          d->Slot = (UINT8)nums[1];
        } else if (n == 1) {
          d->Slot = (UINT8)nums[0];
          d->Channel = (UINT8)(s->DimmCount % 2);
        } else {
          d->Slot = (UINT8)s->DimmCount;
          d->Channel = (UINT8)(s->DimmCount % 2);
        }
      }
      if (d->Present) {
        s->DimmPresentCount++;
        s->InstalledRam += size;
        if (d->Socket > maxSocket) {
          maxSocket = d->Socket;
        }
        if (d->Channel > maxChannel) {
          maxChannel = d->Channel;
        }
        {
          UINT32 k, found = 0;
          for (k = 0; k < 8; k++) {
            if (speeds[k] == d->Speed) {
              speedCounts[k]++;
              found = 1;
              break;
            }
            if (speeds[k] == 0) {
              speeds[k] = d->Speed;
              speedCounts[k] = 1;
              found = 1;
              break;
            }
          }
          (VOID)found;
        }
      }
      s->DimmCount++;
      break;
    }
    default:
      break;
    }
    if (rec->Type < 64) {
      typeCounts[rec->Type]++;
    }
  }

  s->SocketCount = (UINT32)maxSocket + 1;
  s->ChannelCount = (UINT32)maxChannel + 1;
  if (s->ChannelCount == 0) {
    s->ChannelCount = 1;
  }
  /* dominant memory type / speed */
  {
    UINT32 k, best = 0;
    for (k = 0; k < 8; k++) {
      if (speedCounts[k] > best) {
        best = speedCounts[k];
        s->DominantMemSpeed = speeds[k];
      }
    }
    for (k = 0; k < s->DimmCount; k++) {
      if (s->Dimms[k].Present && s->Dimms[k].MemoryType) {
        s->DominantMemType = s->Dimms[k].MemoryType;
        break;
      }
    }
  }
  if (s->InstalledRam == 0) {
    UINT32 k;
    for (k = 0; k < s->DimmCount; k++) {
      s->InstalledRam += s->Dimms[k].SizeBytes;
    }
  }
  if (s->DimmCount == 0) {
    s->SocketCount = 1;
    s->ChannelCount = 2;
  }
}

/* ----------------------------------------------------------- memory map */
static VOID GatherMemoryMap(SYS_INFO *s)
{
  EFI_STATUS status;
  UINTN size = 0, mapKey = 0, descSize = 0;
  UINT32 descVer = 0;
  EFI_MEMORY_DESCRIPTOR *map = 0;
  UINT8 *buffer;
  UINT32 attempts;

  for (attempts = 0; attempts < 4; attempts++) {
    status = gBS->GetMemoryMap(&size, map, &mapKey, &descSize, &descVer);
    if (status == EFI_SUCCESS) {
      break;
    }
    size += descSize ? descSize * 4 : 4096 * 4;
    if (EFI_ERROR(gBS->AllocatePool(EfiLoaderData, size, (VOID **)&buffer))) {
      return;
    }
    map = (EFI_MEMORY_DESCRIPTOR *)buffer;
    status = gBS->GetMemoryMap(&size, map, &mapKey, &descSize, &descVer);
    if (!EFI_ERROR(status)) {
      break;
    }
    gBS->FreePool(buffer);
    map = 0;
    size += 4096;
  }
  if (map == 0 || descSize == 0) {
    return;
  }
  {
    UINTN count = size / descSize;
    UINTN i;
    s->MemoryMapEntries = (UINT32)count;
    for (i = 0; i < count; i++) {
      EFI_MEMORY_DESCRIPTOR *d =
          (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)map + i * descSize);
      UINT64 bytes = d->NumberOfPages * EFI_PAGE_SIZE;
      switch (d->Type) {
      case EfiConventionalMemory:
        s->TestableRam += bytes;
        s->UsableRam += bytes;
        break;
      case EfiLoaderCode:
      case EfiLoaderData:
      case EfiBootServicesCode:
      case EfiBootServicesData:
        s->UsableRam += bytes;
        break;
      default:
        break;
      }
    }
  }
  gBS->FreePool(map);
}

/* ---------------------------------------------------------- boot volume */
static BOOLEAN DevPathPrefix(EFI_DEVICE_PATH_PROTOCOL *prefix, EFI_DEVICE_PATH_PROTOCOL *path)
{
  while (prefix && prefix->Type != 0x7F) {
    UINTN len = prefix->Length[0] | ((UINTN)prefix->Length[1] << 8);
    if (path == 0 || path->Type != prefix->Type || path->SubType != prefix->SubType) {
      return FALSE;
    }
    if (memcmp(prefix, path, len) != 0) {
      return FALSE;
    }
    prefix = (EFI_DEVICE_PATH_PROTOCOL *)((UINT8 *)prefix + len);
    path = (EFI_DEVICE_PATH_PROTOCOL *)((UINT8 *)path + len);
  }
  return TRUE;
}

EFI_STATUS SysOpenBootVolume(SYS_INFO *s, EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *ST)
{
  EFI_LOADED_IMAGE_PROTOCOL *li = 0;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = 0;
  EFI_FILE_PROTOCOL *root = 0;
  EFI_STATUS status;
  EFI_HANDLE *handles = 0;
  UINTN count = 0, i;

  if (EFI_ERROR(ST->BootServices->HandleProtocol(ImageHandle, (EFI_GUID *)&gEfiLoadedImageProtocolGuid, (VOID **)&li)) || li == 0) {
    return EFI_NOT_FOUND;
  }
  if (!EFI_ERROR(ST->BootServices->HandleProtocol(li->DeviceHandle, (EFI_GUID *)&gEfiSimpleFileSystemProtocolGuid, (VOID **)&fs)) && fs) {
    if (!EFI_ERROR(fs->OpenVolume(fs, &root)) && root) {
      ST->BootServices->HandleProtocol(li->DeviceHandle, (EFI_GUID *)&gEfiDevicePathProtocolGuid,
                                       (VOID **)&s->BootVolumeDp);
      s->BootRoot = root;
      StrCopyN(s->BootVolumeName, sizeof(s->BootVolumeName), "bootfs");
      return EFI_SUCCESS;
    }
  }
  /* walk all simple file system handles and match the device path prefix */
  status = ST->BootServices->LocateHandleBuffer(2 /* ByProtocol */, (EFI_GUID *)&gEfiSimpleFileSystemProtocolGuid, 0, &count, &handles);
  if (EFI_ERROR(status)) {
    return status;
  }
  for (i = 0; i < count; i++) {
    EFI_DEVICE_PATH_PROTOCOL *dp = 0;
    if (EFI_ERROR(ST->BootServices->HandleProtocol(handles[i], (EFI_GUID *)&gEfiDevicePathProtocolGuid, (VOID **)&dp)) || dp == 0) {
      continue;
    }
    if (li->FilePath && DevPathPrefix(dp, li->FilePath)) {
      if (EFI_ERROR(ST->BootServices->HandleProtocol(handles[i], (EFI_GUID *)&gEfiSimpleFileSystemProtocolGuid, (VOID **)&fs)) || fs == 0) {
        continue;
      }
      if (!EFI_ERROR(fs->OpenVolume(fs, &root)) && root) {
        s->BootVolumeDp = dp;
        s->BootRoot = root;
        StrCopyN(s->BootVolumeName, sizeof(s->BootVolumeName), "bootfs");
        ST->BootServices->FreePool(handles);
        return EFI_SUCCESS;
      }
    }
  }
  ST->BootServices->FreePool(handles);
  return EFI_NOT_FOUND;
}

/* --------------------------------------------------------------- names */
/* Module layout derived from SMBIOS type 17.
   Total width covers every populated data bit (including ECC), so the rank
   count is totalWidth / dataWidth. The DRAM package itself is not reportable,
   so x8 is assumed - the common case for DIMMs - and the assumption is
   flagged in the result so the UI can label the picture accordingly. */
UINT32 SysPresentDimm(const SYS_INFO *s, UINT32 which)
{
  UINT32 i, n = 0;

  if (s == 0) {
    return 0xFFFFFFFFu;
  }
  for (i = 0; i < s->DimmCount && i < SYS_MAX_DIMMS; i++) {
    if (!s->Dimms[i].Present) {
      continue;
    }
    if (n == which) {
      return i;
    }
    n++;
  }
  return 0xFFFFFFFFu;
}

VOID SysDimmGeometry(const DIMM_INFO *dimm, DIMM_GEO *geo)
{
  UINT32 dataWidth = dimm->DataWidth;
  UINT32 totalWidth = dimm->TotalWidth;
  UINT32 ranks;
  UINT32 perRank;
  UINT32 mt = dimm->MemoryType;

  gBS->SetMem(geo, sizeof(*geo), 0);
  if (dataWidth == 0 || dataWidth == 0xFFFF) {
    dataWidth = 64;
    geo->WidthAssumed = TRUE;
  }
  if (totalWidth == 0 || totalWidth == 0xFFFF) {
    totalWidth = dataWidth;
  }
  /* type 17 reports the rank count directly; the width ratio only works when
     the module has no ECC (TOTAL WIDTH == DATA WIDTH) and DDR5 splits every
     rank into two 40-bit subchannels, so the ratio alone reads 2R as 1R. */
  ranks = dimm->Rank ? dimm->Rank : (dataWidth ? totalWidth / dataWidth : 1);
  if (ranks == 0) {
    ranks = 1;
  }
  if (ranks > 4) {
    ranks = 4;
  }
  geo->DeviceWidth = 8;
  perRank = dataWidth / geo->DeviceWidth;
  if (perRank == 0) {
    perRank = 1;
  }
  if (perRank > 16) {
    perRank = 16;
  }
  geo->Ranks = ranks;
  geo->DevicesPerRank = perRank;
  geo->TotalDevices = ranks * perRank;
  if (geo->TotalDevices > 32) {
    geo->TotalDevices = 32;
  }
  geo->FrontDevices = perRank;
  geo->BackDevices = geo->TotalDevices > perRank ? geo->TotalDevices - perRank : 0;

  /* registered modules carry an RCD; DDR5 moves PMIC and SPD onto the module */
  geo->HasRcd = (dataWidth == 72) ? TRUE : FALSE;
  geo->HasPmic = (mt == 0x22) ? TRUE : FALSE;

  {
    char num[16];
    GfxAppendStr(geo->Note, sizeof(geo->Note), L("x8 颗粒 · "));
    GfxFormatU64(num, geo->Ranks);
    GfxAppendStr(geo->Note + GfxStrLen(geo->Note),
                 sizeof(geo->Note) - GfxStrLen(geo->Note), num);
    GfxAppendStr(geo->Note + GfxStrLen(geo->Note),
                 sizeof(geo->Note) - GfxStrLen(geo->Note), " Rank · ");
    GfxFormatU64(num, geo->TotalDevices);
    GfxAppendStr(geo->Note + GfxStrLen(geo->Note),
                 sizeof(geo->Note) - GfxStrLen(geo->Note), num);
    GfxAppendStr(geo->Note + GfxStrLen(geo->Note),
                 sizeof(geo->Note) - GfxStrLen(geo->Note), L(" 颗粒"));
  }

  /* voltages: SMBIOS value when present, otherwise the JEDEC typical one */
  if (dimm->VddMv) {
    geo->VddMv = dimm->VddMv;
  } else if (mt == 0x22) {
    geo->VddMv = 1100;
  } else if (mt == 0x1A || mt == 0x1B) {
    geo->VddMv = 1200;
  } else if (mt == 0x18 || mt == 0x19) {
    geo->VddMv = 1500;
  } else {
    geo->VddMv = 0;
  }
  geo->VddqMv = geo->VddMv;
  if (mt == 0x22) {
    geo->VppMv = 1800;
  } else if (mt == 0x1A || mt == 0x1B) {
    geo->VppMv = 2500;
  } else {
    geo->VppMv = 0;
  }
}

const char *SysMemTypeName(UINT8 type)
{
  switch (type) {
  case 0x01: return "Other";
  case 0x02: return "Unknown";
  case 0x03: return "DRAM";
  case 0x0F: return "SDRAM";
  case 0x11: return "SDRAM";
  case 0x12: return "DDR";
  case 0x13: return "DDR2";
  case 0x18: return "DDR3";
  case 0x1A: return "DDR4";
  case 0x22: return "DDR5";
  case 0x23: return "LPDDR5";
  case 0x1B: return "LPDDR";
  case 0x1C: return "LPDDR2";
  case 0x1D: return "LPDDR3";
  case 0x1E: return "LPDDR4";
  default:   return "DRAM";
  }
}

const char *SysFormFactorName(UINT8 ff)
{
  switch (ff) {
  case 0x01: return "Other";
  case 0x02: return "Unknown";
  case 0x03: return "SIMM";
  case 0x04: return "SIP";
  case 0x05: return "Chip";
  case 0x06: return "DIP";
  case 0x07: return "ZIP";
  case 0x08: return "Proprietary Card";
  case 0x09: return "DIMM";
  case 0x0A: return "TSOP";
  case 0x0B: return "Row of chips";
  case 0x0C: return "RIMM";
  case 0x0D: return "SODIMM";
  case 0x0E: return "SRIMM";
  case 0x0F: return "FB-DIMM";
  default:   return "DIMM";
  }
}

const char *SysCoreBrandName(VOID)
{
  return "UEFI Component Test Suite";
}

/* --------------------------------------------------------------- collect */
VOID SysCollect(SYS_INFO *s, EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *ST)
{
  gBS = ST->BootServices;
  gST = ST;
  gBS->SetMem(s, sizeof(*s), 0);
  s->TscHz = SysTscHz();
  s->FirmwareRevision = ST->FirmwareRevision;
  {
    CHAR16 *v = ST->FirmwareVendor ? ST->FirmwareVendor : (CHAR16 *)L"";
    UINTN i = 0;
    while (v[i] && i + 1 < sizeof(s->FirmwareVendor)) {
      s->FirmwareVendor[i] = (v[i] < 0x80) ? (char)v[i] : '?';
      i++;
    }
    s->FirmwareVendor[i] = 0;
  }
  GatherCpu(s);
  GatherSmbios(s);
  GatherMemoryMap(s);
  SysOpenBootVolume(s, ImageHandle, ST);
  if (s->TestableRam == 0) {
    s->TestableRam = s->UsableRam;
  }
}
