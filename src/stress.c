/* stress.c - sustained load test.

   Design notes
   ------------
   * CPU load is real parallelism: every selected logical processor except the
     boot strap processor runs CpuStressWorker on its own AP through the MP
     services protocol until the global stop flag is set.
   * Memory and disk load run on the BSP, alternating in short slices so both
     progress at the same time and the UI keeps updating.
   * Memory is written with an address derived pattern and compared right
     after the copy, exactly like the memory test page does. DIMM
     selection uses the same capacity proportional split that the memory page
     uses when it attributes an error to a slot (SMBIOS gives no physical
     address ranges, so this is the best available approximation).
   * Disk load is read only on purpose: a stress test must not destroy data.
*/
#include "stress.h"
#include "disk.h"
#include "ui.h"
#include "debug.h"

STRESS_STATE gStress;

typedef struct {
  volatile UINT32 Done;
  volatile UINT64 Loops;
  volatile UINT32 Errors;
  UINTN           Index;
} CPU_SLOT;

static CPU_SLOT         gSlots[STRESS_MAX_CPU];
static volatile UINT8   gStop;
static EFI_MP_SERVICES_PROTOCOL *gMp;
static UINTN            gBsp;

#define STRESS_CHUNK_BYTES  (1024 * 1024)
#define STRESS_DISK_BYTES   (256 * 1024)
#define STRESS_MAX_REGIONS  48

typedef struct {
  UINT64 Base;
  UINT64 Size;
} STRESS_REGION;

static STRESS_REGION gRegions[STRESS_MAX_REGIONS];
static UINT32        gRegionCount;

/* --------------------------------------------------------------- helpers */
UINT32 StressCpuCount(VOID)
{
  UINT32 n = gApp.Sys.LogicalCpus;
  if (n == 0) {
    n = 1;
  }
  return n > STRESS_MAX_CPU ? STRESS_MAX_CPU : n;
}

UINT32 StressDimmCount(VOID)
{
  return gApp.Sys.DimmPresentCount;
}

VOID StressLogError(const char *who, const char *what, UINT64 detail)
{
  char line[STRESS_ERR_LEN];
  UINT32 i;

  UiStrPrintf(line, sizeof(line), "%s：%s", who, what);
  if (detail) {
    UiStrPrintf(line + UiStrLen(line), sizeof(line) - UiStrLen(line), "（0x%lX）",
                (unsigned long long)detail);
  }
  for (i = STRESS_ERR_LOG - 1; i > 0; i--) {
    gApp.BS->CopyMem(gStress.Errors[i], gStress.Errors[i - 1], STRESS_ERR_LEN);
  }
  gApp.BS->CopyMem(gStress.Errors[0], line, STRESS_ERR_LEN);
  gApp.BS->CopyMem(gStress.LastError, line, STRESS_ERR_LEN);
  gStress.ErrorCount++;
}

static UINT64 Mix(UINT64 x)
{
  x ^= x << 13;
  x ^= x >> 7;
  x ^= x << 17;
  return x;
}

/* ------------------------------------------------------------ cpu worker */
static VOID CpuBatch(CPU_SLOT *slot, UINT64 *seedp)
{
  UINT64 seed = *seedp;
  {
    UINT32 n = 512 + (UINT32)(seed & 0x7FF);
    UINT64 acc = 0;
    UINT64 want;
    UINT32 k;

    /* integer pipeline: loop sum against the closed form */
    for (k = 1; k <= n; k++) {
      acc += k;
    }
    want = ((UINT64)n * (UINT64)(n + 1)) / 2;
    if (acc != want) {
      slot->Errors++;
      StressLogError("CPU", "整数累加结果异常", (UINT64)slot->Index);
    }

    /* bit manipulation + rotate/xor chain, checked by reversing the last step */
    {
      UINT64 y = seed;
      UINT64 check;
      for (k = 0; k < 64; k++) {
        y = Mix(y);
      }
      check = y;
      for (k = 0; k < 64; k++) {
        check = Mix(check);
      }
      seed = check;
      if (seed == 0) {
        seed = 0x2545F4914F6CDD1DULL;
      }
      slot->Loops++;
    }

    /* SSE2 double pipeline: 0.5 * k summed must equal n(n+1)/4 exactly */
    {
      double sum = 0.0;
      double half = 0.5;
      double expect;
      for (k = 1; k <= n; k++) {
        sum += (double)k * half;
      }
      expect = ((double)n * (double)(n + 1)) / 4.0;
      if (sum != expect) {
        slot->Errors++;
        StressLogError("CPU", "浮点累加结果异常", (UINT64)slot->Index);
      }
    }
  }
  *seedp = seed;
}

static VOID CpuStressWorker(VOID *arg)
{
  CPU_SLOT *slot = (CPU_SLOT *)arg;
  UINT64 seed;

  if (slot == 0) {
    return;
  }
  seed = 0x9E3779B97F4A7C15ULL * (UINT64)(slot->Index + 1);
  while (!gStop) {
    CpuBatch(slot, &seed);
  }
  slot->Done = 1;
}

/* StartupAllAPs entry point: every AP decides from the mask whether it works.
   WhoAmI is the only protocol call the spec allows from an AP. */
static VOID CpuStressAllAPs(VOID *arg)
{
  UINTN self = 0;
  const UINT64 *mask = (const UINT64 *)arg;

  if (gMp == 0 || mask == 0 || EFI_ERROR(gMp->WhoAmI(gMp, &self))) {
    return;
  }
  if (self >= STRESS_MAX_CPU || self == gBsp || !StressCpuSelected(mask, (UINT32)self)) {
    return;
  }
  CpuStressWorker(&gSlots[self]);
}

/* ------------------------------------------------------------- memory job */
static UINT32 CollectRegions(UINT64 capBytes)
{
  UINTN size = 0, key = 0, descSize = 0;
  UINT32 ver = 0, tries;
  EFI_STATUS status = EFI_SUCCESS;
  EFI_MEMORY_DESCRIPTOR *map = 0, *copy = 0;
  UINT64 used = 0;
  UINTN i, count;

  gRegionCount = 0;
  for (tries = 0; tries < 4; tries++) {
    size = 0;
    status = gApp.BS->GetMemoryMap(&size, 0, &key, &descSize, &ver);
    if (status != EFI_BUFFER_TOO_SMALL && EFI_ERROR(status)) {
      return 0;
    }
    size += descSize * 8;
    if (!EFI_ERROR(gApp.BS->AllocatePool(EfiLoaderData, size, (VOID **)&map))) {
      status = gApp.BS->GetMemoryMap(&size, map, &key, &descSize, &ver);
      if (!EFI_ERROR(status)) {
        break;
      }
      gApp.BS->FreePool(map);
      map = 0;
    }
  }
  if (map == 0 || descSize == 0) {
    return 0;
  }
  if (EFI_ERROR(gApp.BS->AllocatePool(EfiLoaderData, size, (VOID **)&copy))) {
    gApp.BS->FreePool(map);
    return 0;
  }
  gApp.BS->CopyMem(copy, map, size);
  gApp.BS->FreePool(map);

  count = size / descSize;
  for (i = 0; i < count && gRegionCount < STRESS_MAX_REGIONS && used < capBytes; i++) {
    EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)copy + i * descSize);
    UINT64 bytes = d->NumberOfPages * EFI_PAGE_SIZE;
    UINT64 room;
    if (d->Type != EfiConventionalMemory || bytes < 8 * 1024 * 1024) {
      continue;
    }
    room = capBytes - used;
    if (bytes - 8192 > room) {
      bytes = room + 8192;
    }
    gRegions[gRegionCount].Base = d->PhysicalStart + 4096;
    gRegions[gRegionCount].Size = bytes - 8192;
    used += gRegions[gRegionCount].Size;
    gRegionCount++;
  }
  gApp.BS->FreePool(copy);
  return gRegionCount;
}

/* one 1 MB slice of pattern write + readback inside the selected DIMM windows */
static void MemorySlice(UINT64 *cursor, UINT32 dimmMask, UINT32 dimmCount, UINT64 perDimm,
                        UINT8 *buffer)
{
  UINT32 guard = 0;

  while (gRegionCount && guard++ < 64) {
    UINT64 total = 0;
    UINT64 off;
    UINT32 r;
    UINT64 remaining;
    UINT64 base = 0, room = 0;

    /* advance the virtual cursor until we land in a selected DIMM window */
    for (r = 0; r < gRegionCount; r++) {
      total += gRegions[r].Size;
    }
    if (total == 0) {
      return;
    }
    if (*cursor >= total) {
      *cursor = 0;
    }
    if (dimmCount > 0 && perDimm > 0) {
      UINT32 dimm = (UINT32)(*cursor / perDimm);
      if (dimm >= dimmCount) {
        dimm = dimmCount - 1;
      }
      if ((dimmMask & (1u << (dimm & 31))) == 0) {
        /* skip to the end of this slot's window */
        *cursor = (UINT64)(dimm + 1) * perDimm;
        continue;
      }
    }
    /* map the virtual cursor to a physical region */
    off = *cursor;
    for (r = 0; r < gRegionCount; r++) {
      if (off < gRegions[r].Size) {
        base = gRegions[r].Base + off;
        room = gRegions[r].Size - off;
        break;
      }
      off -= gRegions[r].Size;
    }
    if (room == 0) {
      *cursor += STRESS_CHUNK_BYTES;
      continue;
    }
    remaining = STRESS_CHUNK_BYTES < room ? STRESS_CHUNK_BYTES : room;
    {
      UINT64 i;
      UINT64 *q = (UINT64 *)buffer;
      UINT64 words = remaining / 8;
      for (i = 0; i < words; i++) {
        q[i] = (base + i * 8) ^ 0x5AA5C33CC33C5AA5ULL;
      }
      gApp.BS->CopyMem((VOID *)(UINTN)base, buffer, (UINTN)remaining);
      gApp.BS->CopyMem(buffer, (VOID *)(UINTN)base, (UINTN)remaining);
      for (i = 0; i < words; i++) {
        UINT64 want = (base + i * 8) ^ 0x5AA5C33CC33C5AA5ULL;
        if (q[i] != want) {
          gStress.MemErrors++;
          if (gStress.MemErrors <= 8) {
            StressLogError("内存", "图案回读不一致", base + i * 8);
          }
        }
      }
      gStress.MemBytes += remaining;
    }
    *cursor += remaining;
    return;
  }
}

/* --------------------------------------------------------------- disk job */
static void DiskSlice(const STRESS_CONFIG *cfg, DISK_DEV *disk, UINT8 *buffer, UINT32 bytes)
{
  UINT64 lba;
  UINTN len;
  EFI_STATUS status;

  if (disk == 0 || disk->Io == 0) {
    return;
  }
  lba = (UINT64)(Mix(SysTsc()) % ((UINT64)disk->LastBlock ? (UINT64)disk->LastBlock : 1));
  len = bytes;
  if (len > disk->SizeBytes) {
    len = (UINTN)disk->SizeBytes;
  }
  if (disk->LastBlock) {
    UINT64 maxLba = (UINT64)disk->LastBlock + 1;
    UINT64 blocks = len / disk->BlockSize;
    if (lba + blocks > maxLba) {
      lba = maxLba > blocks ? maxLba - blocks : 0;
    }
  }
  len = (len / disk->BlockSize) * disk->BlockSize;
  if (len == 0) {
    return;
  }
  status = disk->Io->ReadBlocks(disk->Io, disk->Io->Media->MediaId, (EFI_LBA)lba, len, buffer);
  if (EFI_ERROR(status)) {
    gStress.DiskErrors++;
    if (gStress.DiskErrors <= 8) {
      StressLogError("硬盘", "随机读取失败", lba * disk->BlockSize);
    }
    return;
  }
  gStress.DiskBytes += len;
}

/* --------------------------------------------------------------- control */
VOID StressStop(VOID)
{
  gStop = 1;
}

EFI_STATUS StressRun(const STRESS_CONFIG *cfg, STRESS_TICK tick)
{
  UINT8 *memBuffer = 0;
  UINT8 *diskBuffer = 0;
  UINT64 startTsc, endTsc = 0, cursor = 0;
  UINT32 dimmCount, cpuCount, i;
  UINT64 perDimm = 0;
  UINT64 lastTick = 0;
  BOOLEAN finishedOnTime = FALSE;
  EFI_EVENT apEvent = 0;
  UINT64 bspSeed = 0x2545F4914F6CDD1DULL;
  const UINT64 *apMask = 0;
  DISK_DEV *disks[STRESS_MAX_DISK];
  UINT32 diskCount = 0;
  UINT32 diskTurn = 0;
  UINT32 cpuSelected = 0;

  gApp.BS->SetMem(&gStress, sizeof(gStress), 0);
  gApp.BS->SetMem((VOID *)gSlots, sizeof(gSlots), 0);
  gStop = 0;

  cpuCount = StressCpuCount();
  dimmCount = StressDimmCount();
  for (i = 0; i < cpuCount && i < STRESS_MAX_CPU; i++) {
    if (StressCpuSelected(cfg->CpuMask, i)) {
      cpuSelected++;
    }
  }
  for (i = 0; i < DiskCount() && diskCount < STRESS_MAX_DISK; i++) {
    if (cfg->DiskMask & (1u << i)) {
      DISK_DEV *d = DiskAt(i);
      if (d && d->MediaPresent) {
        disks[diskCount++] = d;
      }
    }
  }

  if (!cfg->CpuOn && !cfg->MemOn && !cfg->DiskOn) {
    return EFI_INVALID_PARAMETER;
  }
  if (cfg->Seconds == 0) {
    return EFI_INVALID_PARAMETER;
  }

  /* locate the MP services protocol for the APs */
  gMp = 0;
  gBsp = 0;
  if (cpuSelected) {
    if (EFI_ERROR(gApp.BS->LocateProtocol((EFI_GUID *)&gEfiMpServiceProtocolGuid, 0,
                                          (VOID **)&gMp))) {
      gMp = 0;
    }
    if (gMp) {
      UINTN self = 0;
      if (!EFI_ERROR(gMp->WhoAmI(gMp, &self))) {
        gBsp = self;
      }
      DbgStr("stress: mp ok bsp=");
      DbgDec(gBsp);
      DbgStr("\n");
    } else {
      DbgStr("stress: no mp protocol\n");
    }
  }

  /* working buffers */
  if (cfg->MemOn) {
    if (!CollectRegions(4ULL * 1024 * 1024 * 1024)) {
      return EFI_NOT_FOUND;
    }
    if (EFI_ERROR(gApp.BS->AllocatePool(EfiBootServicesData, STRESS_CHUNK_BYTES,
                                        (VOID **)&memBuffer))) {
      return EFI_OUT_OF_RESOURCES;
    }
    {
      UINT64 total = 0;
      UINT32 r;
      for (r = 0; r < gRegionCount; r++) {
        total += gRegions[r].Size;
      }
      perDimm = dimmCount ? total / dimmCount : total;
    }
  }
  if (cfg->DiskOn && diskCount) {
    if (EFI_ERROR(gApp.BS->AllocatePool(EfiBootServicesData, STRESS_DISK_BYTES,
                                        (VOID **)&diskBuffer))) {
      if (memBuffer) {
        gApp.BS->FreePool(memBuffer);
      }
      return EFI_OUT_OF_RESOURCES;
    }
  }

  gStopRequested = FALSE;
  gBusy = TRUE;
  startTsc = SysTsc();
  endTsc = startTsc + (UINT64)cfg->Seconds * gApp.Sys.TscHz;

  /* fire up the APs.
     MP services only runs asynchronously when a wait event is supplied; a NULL
     event means blocking mode with a zero timeout, i.e. wait forever. A single
     StartupAllAPs call keeps one call per run and works on firmware that only
     tracks one outstanding AP dispatch. */
  if (gMp && cpuSelected) {
    if (EFI_ERROR(gApp.BS->CreateEvent(0, TPL_CALLBACK, 0, 0, &apEvent))) {
      apEvent = 0;
      StressLogError("CPU", "无法创建 MP 事件", 0);
    }
    for (i = 0; i < cpuCount && i < STRESS_MAX_CPU; i++) {
      if (!StressCpuSelected(cfg->CpuMask, i) || i == gBsp) {
        continue;
      }
      gSlots[i].Index = i;
      gSlots[i].Done = 0;
      gSlots[i].Loops = 0;
      gSlots[i].Errors = 0;
    }
    if (apEvent) {
      /* StartupAllAPs takes a UINTN processor mask, i.e. 64 bits at most; 0
         means "every AP" and each AP then filters itself through WhoAmI, which
         is the only way to reach processor 191 on a two socket box */
      apMask = cfg->CpuMask;
      EFI_STATUS apStatus = gMp->StartupAllAPs(gMp, CpuStressAllAPs, FALSE, apEvent, 0,
                                               (VOID *)apMask, 0);
      DbgStr("stress: StartupAllAPs st=");
      DbgHex((UINT64)apStatus);
      DbgStr("\n");
      if (EFI_ERROR(apStatus)) {
        for (i = 0; i < STRESS_MAX_CPU; i++) {
          gSlots[i].Done = 1;
        }
        StressLogError("CPU", "无法启动 AP 压力线程", (UINT64)apStatus);
      } else {
        for (i = 0; i < cpuCount && i < STRESS_MAX_CPU; i++) {
          if (StressCpuSelected(cfg->CpuMask, i) && i != gBsp) {
            gStress.CpuAlive[i] = 1;
          }
        }
      }
    }
  }

  /* the BSP always joins in when it was selected as a CPU target */
  gStress.CpuThreads = 0;
  for (i = 0; i < STRESS_MAX_CPU; i++) {
    if (gStress.CpuAlive[i]) {
      gStress.CpuThreads++;
    }
  }
  if (cfg->CpuOn && StressCpuSelected(cfg->CpuMask, 0)) {
    gStress.CpuAlive[0] = 1;
    gStress.CpuThreads++;
  }

  DbgStr("stress: loop start threads=");
  DbgDec(gStress.CpuThreads);
  DbgStr("\n");
  while (!gStop && !gStopRequested) {
    UINT64 now = SysTsc();
    if (now >= endTsc) {
      finishedOnTime = TRUE;
      break;
    }
    if (cfg->MemOn && memBuffer) {
      MemorySlice(&cursor, cfg->DimmMask, dimmCount, perDimm, memBuffer);
    }
    if (cfg->DiskOn && diskCount && diskBuffer) {
      UINT32 pick = diskTurn++ % diskCount;
      DiskSlice(cfg, disks[pick], diskBuffer, STRESS_DISK_BYTES);
    }
    if (cfg->CpuOn && StressCpuSelected(cfg->CpuMask, 0)) {
      /* the processor running this loop also carries a share of the CPU load */
      CpuBatch(&gSlots[0], &bspSeed);
    }
    {
      UINT32 elapsedMs = (UINT32)SysMsSince(startTsc);
      if (elapsedMs - lastTick >= 100) {
        UINT32 span = elapsedMs ? elapsedMs : 1;
        lastTick = elapsedMs;
        gStress.CpuLoops = 0;
        for (i = 0; i < STRESS_MAX_CPU; i++) {
          gStress.CpuLoopsPerCpu[i] = gSlots[i].Loops;
          gStress.CpuLoops += gSlots[i].Loops;
          gStress.CpuErrors += gSlots[i].Errors;
          gSlots[i].Errors = 0;
        }
        gStress.RemainingMs = (cfg->Seconds * 1000) > elapsedMs
                                  ? (UINT32)(cfg->Seconds * 1000) - elapsedMs
                                  : 0;
        gStress.ElapsedMs = elapsedMs;
        gStress.MemMbs = (UINT32)((gStress.MemBytes / 1024) / span);
        gStress.DiskMbs = (UINT32)((gStress.DiskBytes / 1024) / span);
        if (tick && !tick(&gStress)) {
          gStop = 1;
        }
      }
    }
    gApp.BS->Stall(4000);
  }

  /* stop the APs and give them a moment to leave their loops */
  DbgStr("stress: loop end elapsed=");
  DbgDec(SysMsSince(startTsc));
  DbgStr("\n");
  gStop = 1;
  {
    UINT32 wait;
    for (wait = 0; wait < 200; wait++) {
      BOOLEAN allDone = TRUE;
      for (i = 0; i < cpuCount && i < STRESS_MAX_CPU; i++) {
        if (gStress.CpuAlive[i] && !gSlots[i].Done) {
          allDone = FALSE;
        }
      }
      if (allDone) {
        break;
      }
      gApp.BS->Stall(2000);
    }
  }
  for (i = 0; i < STRESS_MAX_CPU; i++) {
    gStress.CpuErrors += gSlots[i].Errors;
    gStress.CpuLoopsPerCpu[i] = gSlots[i].Loops;
    gSlots[i].Errors = 0;
    gStress.CpuAlive[i] = 0;
  }

  gStress.RemainingMs = 0;
  gStress.ElapsedMs = (UINT32)SysMsSince(startTsc);
  if (gStress.ElapsedMs == 0) {
    gStress.ElapsedMs = 1;
  }
  gStress.MemMbs = (UINT32)((gStress.MemBytes / 1024) / gStress.ElapsedMs);
  gStress.DiskMbs = (UINT32)((gStress.DiskBytes / 1024) / gStress.ElapsedMs);

  gStress.Aborted = finishedOnTime ? 0 : 1;
  gResults.StressRan = TRUE;
  gResults.StressAborted = gStress.Aborted ? TRUE : FALSE;
  gResults.StressSeconds = gStress.ElapsedMs / 1000;
  gResults.StressErrorCount = gStress.ErrorCount;
  gResults.StressCpuLoops = gStress.CpuLoops;
  gResults.StressMemBytes = gStress.MemBytes;
  gResults.StressDiskBytes = gStress.DiskBytes;
  gResults.StressCpuThreads = gStress.CpuThreads;
  if (gStress.ErrorCount) {
    if (cfg->CpuOn) {
      gResults.HealthCpu = 3;
    }
    if (cfg->MemOn) {
      gResults.HealthMem = 3;
    }
    if (cfg->DiskOn) {
      gResults.HealthDisk = 3;
    }
  }

  if (apEvent) {
    gApp.BS->CloseEvent(apEvent);
  }
  gBusy = FALSE;
  gStopRequested = FALSE;
  if (memBuffer) {
    gApp.BS->FreePool(memBuffer);
  }
  if (diskBuffer) {
    gApp.BS->FreePool(diskBuffer);
  }
  return EFI_SUCCESS;
}
