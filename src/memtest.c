/* memtest.c - memory pattern engine: pattern write/read across
   the free memory described by the UEFI memory map. */
#include "memtest.h"
#include "ui.h"
#include "debug.h"

MEM_PATTERN gMemPatterns[] = {
  { "全 0 图案",       "0x0000000000000000", MEM_KIND_FIXED,    0x0000000000000000ULL, TRUE  },
  { "全 1 图案",       "0xFFFFFFFFFFFFFFFF", MEM_KIND_FIXED,    0xFFFFFFFFFFFFFFFFULL, TRUE  },
  { "棋盘 55/AA",      "0x5555.. / 0xAAAA..", MEM_KIND_SPLIT,   0x5555555555555555ULL, TRUE  },
  { "棋盘 AA/55",      "0xAAAA.. / 0x5555..", MEM_KIND_SPLIT,   0xAAAAAAAAAAAAAAAAULL, TRUE  },
  { "地址图案",        "写入单元物理地址",     MEM_KIND_ADDR,     0, TRUE  },
  { "地址 XOR 全 1",   "反码地址图案",         MEM_KIND_ADDR_XOR, 0xFFFFFFFFFFFFFFFFULL, TRUE  },
  { "地址 XOR 55AA",   "交错地址图案",         MEM_KIND_ADDR_XOR, 0x5555AAAA5555AAAAULL, TRUE  },
  { "伪随机 LFSR",     "x^64 线性反馈序列",    MEM_KIND_LFSR,     0x123456789ABCDEF0ULL, TRUE  },
  { "走动 1",          "单词内单个 1 游走",    MEM_KIND_WALK1,    0, TRUE  },
  { "走动 0",          "单词内单个 0 游走",    MEM_KIND_WALK0,    0xFFFFFFFFFFFFFFFFULL, TRUE  }
};

const UINT32 gMemPatternCount = sizeof(gMemPatterns) / sizeof(gMemPatterns[0]);

#define MEM_CHUNK_BYTES   (2 * 1024 * 1024)
#define MEM_MAX_REGIONS   48
#define MEM_MAX_ERRORS    100000

typedef struct {
  UINT64 Base;
  UINT64 Size;
} MEM_REGION;

static MEM_REGION gRegions[MEM_MAX_REGIONS];
static UINT32     gRegionCount;
static UINT8     *gScratch;
static UINTN      gScratchSize;

UINT32 MemEnabledPatterns(VOID)
{
  UINT32 i, n = 0;
  for (i = 0; i < gMemPatternCount; i++) {
    if (gMemPatterns[i].Enabled) {
      n++;
    }
  }
  return n;
}

/* ------------------------------------------------------------- patterns */
static UINT64 PatternWord(const MEM_PATTERN *p, UINT64 offset)
{
  UINT64 cell = offset >> 3;
  switch (p->Kind) {
  case MEM_KIND_FIXED:
    return p->Value;
  case MEM_KIND_SPLIT:
    return (cell & 1) ? ~p->Value : p->Value;
  case MEM_KIND_ADDR:
    return offset;
  case MEM_KIND_ADDR_XOR:
    return offset ^ p->Value;
  case MEM_KIND_LFSR: {
    UINT64 v = p->Value;
    UINT32 i;
    for (i = 0; i < 17; i++) {
      v = (v >> 1) ^ (0x9E3779B97F4A7C15ULL & (0ULL - (v & 1)));
    }
    return v * (cell | 1);
  }
  case MEM_KIND_WALK1:
    return 1ULL << (cell & 63);
  case MEM_KIND_WALK0:
    return ~(1ULL << (cell & 63));
  default:
    return 0;
  }
}

/* ------------------------------------------------------------- regions */
static UINT32 CollectRegions(BOOLEAN wide, UINT64 *totalBytes)
{
  EFI_STATUS status;
  UINTN size = 0, mapKey = 0, descSize = 0;
  UINT32 descVer = 0;
  EFI_MEMORY_DESCRIPTOR *map = 0, *copy = 0;
  UINT8 *buffer = 0;
  UINT32 attempts;

  gRegionCount = 0;
  for (attempts = 0; attempts < 4; attempts++) {
    status = gApp.BS->GetMemoryMap(&size, map, &mapKey, &descSize, &descVer);
    if (!EFI_ERROR(status)) {
      break;
    }
    size += descSize ? descSize * 8 : 32768;
    if (buffer) {
      gApp.BS->FreePool(buffer);
      buffer = 0;
    }
    if (EFI_ERROR(gApp.BS->AllocatePool(EfiLoaderData, size, (VOID **)&buffer))) {
      return 0;
    }
    map = (EFI_MEMORY_DESCRIPTOR *)buffer;
  }
  if (map == 0 || descSize == 0) {
    return 0;
  }
  /* copy, because testing memory may invalidate the map the firmware owns */
  if (EFI_ERROR(gApp.BS->AllocatePool(EfiLoaderData, size, (VOID **)&copy))) {
    gApp.BS->FreePool(buffer);
    return 0;
  }
  gApp.BS->CopyMem(copy, map, size);
  gApp.BS->FreePool(buffer);

  {
    UINTN count = size / descSize;
    UINTN i;
    for (i = 0; i < count && gRegionCount < MEM_MAX_REGIONS; i++) {
      EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)copy + i * descSize);
      UINT64 bytes = d->NumberOfPages * EFI_PAGE_SIZE;
      if (d->Type != EfiConventionalMemory || bytes < (16 * 1024 * 1024)) {
        continue;
      }
      /* leave a little headroom at both ends of every region */
      gRegions[gRegionCount].Base = d->PhysicalStart + 4096;
      gRegions[gRegionCount].Size = bytes - 8192;
      gRegionCount++;
    }
  }
  gApp.BS->FreePool(copy);

  /* limit the working set so the run stays bounded on very large machines */
  {
    UINT64 cap = wide ? (6ULL * 1024 * 1024 * 1024) : (2ULL * 1024 * 1024 * 1024);
    UINT64 used = 0;
    UINT32 i;
    for (i = 0; i < gRegionCount; i++) {
      if (used >= cap) {
        gRegionCount = i;
        break;
      }
      if (used + gRegions[i].Size > cap) {
        gRegions[i].Size = cap - used;
      }
      used += gRegions[i].Size;
    }
    *totalBytes = used;
  }
  return gRegionCount;
}

/* --------------------------------------------------------------- runner */
/* Which DRAM device owns the failing data bit. Device k drives data bits
   k*width .. k*width+width-1, so the XOR of expected/actual gives the package
   when the module is a plain 64/72 bit bus (the address to rank mapping stays
   firmware specific, so this is an approximation and is labelled as such). */
static VOID NoteDeviceError(UINT32 dimm, UINT64 expected, UINT64 actual)
{
  UINT64 diff = expected ^ actual;
  UINT32 bit = 0;
  UINT32 width;
  UINT32 device;
  DIMM_GEO geo;

  if (diff == 0 || dimm >= gApp.Sys.DimmCount || dimm >= 16) {
    return;
  }
  SysDimmGeometry(&gApp.Sys.Dimms[dimm], &geo);
  width = geo.DeviceWidth ? geo.DeviceWidth : 8;
  while (bit < 63 && ((diff >> bit) & 1) == 0) {
    bit++;
  }
  device = bit / width;
  if (device >= 32) {
    device = 31;
  }
  if (gResults.MemDeviceErrors[dimm][device] < 0xFFFF) {
    gResults.MemDeviceErrors[dimm][device]++;
  }
  gResults.MemDeviceMapped++;
}

static VOID NoteError(UINT64 offset, UINT64 expected, UINT64 actual)
{
  UINT32 dimm = 0;
  if (gResults.MemErrorCount == 0) {
    gResults.MemFirstErrorOffsetValid = 1;
    gResults.MemFirstErrorOffset = offset;
    gResults.MemFirstErrorExpected = expected;
    gResults.MemFirstErrorActual = actual;
  }
  if (gResults.MemErrorCount < MEM_MAX_ERRORS) {
    gResults.MemErrorCount++;
  }
  if (gApp.Sys.DimmPresentCount > 1 && gResults.MemTotalBytes) {
    dimm = (UINT32)((offset / (gResults.MemTotalBytes / gApp.Sys.DimmPresentCount)));
    if (dimm >= gApp.Sys.DimmPresentCount) {
      dimm = gApp.Sys.DimmPresentCount - 1;
    }
    if (dimm < 16) {
      gResults.MemDimmErrors[dimm]++;
    }
    NoteDeviceError(dimm, expected, actual);
  } else {
    if (gResults.MemDimmErrors[0] < 0xFFFFFFFFu) {
      gResults.MemDimmErrors[0]++;
    }
    NoteDeviceError(0, expected, actual);
  }
}

EFI_STATUS MemTestRun(MEM_STEP_CB cb, BOOLEAN wideCoverage)
{
  UINT64 totalBytes = 0;
  UINT64 done = 0;
  UINT64 startTsc;
  UINT32 patternIdx;
  UINT32 runPatterns = 0;
  UINT32 enabledPatterns;
  UINT32 ordinal = 0;
  UINT32 lastDraw = 0;
  BOOLEAN stopped = FALSE;

  if (MemEnabledPatterns() == 0) {
    return EFI_INVALID_PARAMETER;
  }
  gApp.BS->SetMem(&gResults.MemDimmErrors, sizeof(gResults.MemDimmErrors), 0);
  gApp.BS->SetMem(&gResults.MemDeviceErrors, sizeof(gResults.MemDeviceErrors), 0);
  gResults.MemDeviceMapped = 0;
  gResults.MemErrorCount = 0;
  gResults.MemFirstErrorOffsetValid = 0;
  gResults.MemTestedBytes = 0;
  gResults.MemCoveragePm = 0;
  gResults.MemBandwidthMbs = 0;
  gResults.MemRunning = TRUE;
  gResults.MemStopped = FALSE;

  if (CollectRegions(wideCoverage, &totalBytes) == 0) {
    gResults.MemRunning = FALSE;
    return EFI_NOT_FOUND;
  }
  gResults.MemTotalBytes = totalBytes;
  enabledPatterns = MemEnabledPatterns();
  gResults.MemPatternCount = enabledPatterns;

  gScratchSize = MEM_CHUNK_BYTES;
  if (EFI_ERROR(gApp.BS->AllocatePool(EfiBootServicesData, gScratchSize, (VOID **)&gScratch))) {
    gResults.MemRunning = FALSE;
    return EFI_OUT_OF_RESOURCES;
  }

  gStopRequested = FALSE;
  gBusy = TRUE;
  startTsc = SysTsc();

  for (patternIdx = 0; patternIdx < gMemPatternCount && !stopped; patternIdx++) {
    MEM_PATTERN *p = &gMemPatterns[patternIdx];
    UINT32 r;
    UINT64 bytesInPattern = 0;
    if (!p->Enabled) {
      continue;
    }
    runPatterns++;
    for (r = 0; r < gRegionCount && !stopped; r++) {
      UINT64 base = gRegions[r].Base;
      UINT64 size = gRegions[r].Size;
      UINT64 off;
      for (off = 0; off < size; off += gScratchSize) {
        UINT64 len = (size - off) < gScratchSize ? (size - off) : gScratchSize;
        UINT64 i;
        UINT64 chunkBase = base + off;
        /* write pass */
        if (p->Kind == MEM_KIND_FIXED) {
          UINT64 n = len / 8;
          UINT64 *q = (UINT64 *)gScratch;
          for (i = 0; i < n; i++) {
            q[i] = p->Value;
          }
        } else {
          UINT64 *q = (UINT64 *)gScratch;
          for (i = 0; i < len / 8; i++) {
            q[i] = PatternWord(p, chunkBase + i * 8);
          }
        }
        gApp.BS->CopyMem((VOID *)(UINTN)chunkBase, gScratch, (UINTN)len);
        /* read back and compare */
        gApp.BS->CopyMem(gScratch, (VOID *)(UINTN)chunkBase, (UINTN)len);
        {
          UINT64 *q = (UINT64 *)gScratch;
          for (i = 0; i < len / 8; i++) {
            UINT64 want = PatternWord(p, chunkBase + i * 8);
            if (q[i] != want) {
              NoteError(chunkBase + i * 8, want, q[i]);
            }
          }
        }
        bytesInPattern += len;
        done += len;
        {
          UINT32 now = (UINT32)(SysMsSince(startTsc));
          if (cb && (now - lastDraw >= 120 || bytesInPattern == totalBytes)) {
            MEM_STEP step;
            UINT64 elapsed = now ? now : 1;
            UINT64 overall = (UINT64)ordinal * totalBytes + bytesInPattern;
            step.PatternPm = totalBytes ? (UINT32)((bytesInPattern * 1000) / totalBytes) : 0;
            step.OverallPm = (UINT32)((overall * 1000) /
                                      ((UINT64)enabledPatterns * totalBytes));
            if (step.PatternPm > 999) {
              step.PatternPm = 999;
            }
            if (step.OverallPm > 999) {
              step.OverallPm = 999;
            }
            step.PatternOrdinal = ordinal;
            step.PatternCount = enabledPatterns;
            step.Mbs = (UINT32)((done * 1000) / (elapsed * (1024ULL * 1024ULL)));
            step.ElapsedMs = now;
            step.PatternBytes = bytesInPattern;
            step.PassBytes = totalBytes;
            lastDraw = now;
            if (!cb(&step)) {
              stopped = TRUE;
              break;
            }
          }
        }
        if (gStopRequested || UiPump()) {
          stopped = TRUE;
          break;
        }
      }
    }
    (VOID)bytesInPattern;
    ordinal++;
  }

  gResults.MemElapsedMs = (UINT32)SysMsSince(startTsc);
  gResults.MemTestedBytes = done;
  gResults.MemPatternsRun = runPatterns;
  gResults.MemIterations = 1;
  /* coverage of one pass: every pattern walks the whole region, so a completed
     run is 100% and never more */
  if (totalBytes == 0) {
    gResults.MemCoveragePm = 0;
  } else if (done >= totalBytes) {
    gResults.MemCoveragePm = 1000;
  } else {
    gResults.MemCoveragePm = (UINT32)((done * 1000) / totalBytes);
  }
  if (gResults.MemElapsedMs) {
    gResults.MemBandwidthMbs =
        (UINT32)(((done / 1024) * 1000) / ((UINT64)gResults.MemElapsedMs * 1024));
  }
  gResults.MemTested = TRUE;
  gResults.MemRunning = FALSE;
  gResults.MemStopped = stopped;
  gResults.HealthMem = gResults.MemErrorCount ? 3 : 1;
  gBusy = FALSE;
  gApp.BS->FreePool(gScratch);
  gScratch = 0;
  return stopped ? EFI_ABORTED : EFI_SUCCESS;
}
