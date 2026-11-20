/* bw.c - single core memory bandwidth per memory domain, plus the aggregate.

   Placement is what makes the numbers mean something: ACPI SRAT says which
   physical address ranges belong to which proximity domain, and the buffers are
   allocated inside those ranges on purpose. A cell then reads "core X pulling
   data out of the memory of domain Y", and the diagonal is the local bandwidth
   while the off diagonal cells cross the inter-socket link.

   One core is measured at a time, everything else stays parked on its own cache
   line, so a cell is the bandwidth of that core alone and not a contention
   number. The aggregate passes then run every core at once to answer the other
   half of the question. */
#include "bw.h"
#include "latency.h"
#include "ui.h"
#include "sysinfo.h"
#include "debug.h"

#define BW_MAX_RANGE   64
#define BW_MAX_APIC    1024
#define BW_CHUNK_MIN   (4ull * 1024 * 1024)
#define BW_CHUNK_MAX   (32ull * 1024 * 1024)
#define BW_NO_DOM      0xFFu

BW_REPORT gBw;
BW_CPU    gBwCpus[BW_MAX_CPU];

typedef struct __attribute__((aligned(64))) {
  volatile UINT64 Seq;         /* published by the orchestrator            */
  volatile UINT64 Done;        /* set by the worker when the job is over   */
  volatile UINT64 Cycles;      /* TSC ticks the kernel took                */
  UINT64          Bytes;       /* bytes streamed by one iteration          */
  UINT64          Off;         /* offset inside the domain buffer          */
  UINT32          Buf;         /* domain whose buffer is streamed          */
  UINT32          Mode;        /* 1 single sample, 2 aggregate pass        */
  UINT32          Iters;       /* passes over the slice                    */
  UINT32          Pad;
} BW_JOB;

typedef struct {
  UINT64 Base;
  UINT64 Len;
  UINT32 Dom;
} BW_RANGE;

typedef struct {
  UINT32 Apic;
  UINT32 Dom;
} BW_APIC_DOM;

static BW_JOB   gJob[BW_MAX_CPU];
static volatile BOOLEAN gBwExit __attribute__((aligned(64)));
static volatile UINT32  gBwReady __attribute__((aligned(64)));
static volatile UINT32  gBwAggDone __attribute__((aligned(64)));
static volatile UINT64  gBwDeadline __attribute__((aligned(64)));
static EFI_MP_SERVICES_PROTOCOL *gBwMp;
static UINT32  gBwSeq;
static UINT32  gBwByProc[BW_MAX_CPU];

static BW_RANGE gRanges[BW_MAX_RANGE];
static UINT32   gRangeCount;
static BW_APIC_DOM gApicDom[BW_MAX_APIC];
static UINT32   gApicDomCount;
static UINT32   gDomVal[BW_MAX_DOM];   /* SRAT proximity domain of each slot  */
static UINT32   gDomCount;
static UINT8   *gBuf[BW_MAX_DOM];
static UINTN    gBufBytes;             /* whole pool, gDomCount pools exist   */
static UINTN    gChunkBytes;           /* private stream per core             */
static BOOLEAN  gBwPrepared;
static UINT64   gBwSink;               /* keeps the read kernel alive         */

static UINT32 gHist[BW_MAX_CPU * BW_MAX_DOM];
static UINT32 gHistN;

/* ------------------------------------------------------------------ timing */
static UINT64 BwTsc(VOID)
{
  UINT32 lo, hi;
  __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi)::"memory");
  return ((UINT64)hi << 32) | (UINT64)lo;
}

static VOID BwPause(VOID)
{
  __asm__ __volatile__("pause" ::: "memory");
}

static VOID BwIdle(VOID)
{
  UINT32 i;
  for (i = 0; i < 8; i++) {
    BwPause();
  }
}

static VOID BwBarrier(VOID)
{
  __asm__ __volatile__("" ::: "memory");
}

/* ------------------------------------------------------------------ kernel */
/* Eight independent lines per iteration keeps eight loads in flight, which is
   what a single core needs to reach the memory controller instead of waiting
   for its own prefetcher to catch up. */
static VOID BwRead(UINT8 *p, UINTN len, UINT32 iters)
{
  UINT64 a0 = 0, a1 = 0, a2 = 0, a3 = 0, a4 = 0, a5 = 0, a6 = 0, a7 = 0;
  UINTN span = len & ~(UINTN)511;
  UINT32 it;

  if (span < 512) {
    span = len & ~(UINTN)63;
  }
  for (it = 0; it < iters; it++) {
    UINTN i;
    for (i = 0; i < span; i += 512) {
      const UINT64 *q = (const UINT64 *)(p + i);
      a0 += q[0];
      a1 += q[8];
      a2 += q[16];
      a3 += q[24];
      a4 += q[32];
      a5 += q[40];
      a6 += q[48];
      a7 += q[56];
    }
  }
  gBwSink += a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7;
}

/* ------------------------------------------------------------- ACPI tables */
EFI_GUID_DEF(gBwAcpi20Guid, 0x8868E871, 0xE4F1, 0x11D3, 0xBC, 0x22, 0x00, 0x80, 0xC7,
             0x3C, 0x88, 0x81);
EFI_GUID_DEF(gBwAcpi10Guid, 0xEB9D2D30, 0x2D88, 0x11D3, 0x9A, 0x16, 0x00, 0x90, 0x27,
             0x3F, 0xC1, 0x4D);

static BOOLEAN BwGuidEq(const EFI_GUID *a, const EFI_GUID *b)
{
  const UINT8 *x = (const UINT8 *)a;
  const UINT8 *y = (const UINT8 *)b;
  UINT32 i;

  for (i = 0; i < 16; i++) {
    if (x[i] != y[i]) {
      return FALSE;
    }
  }
  return TRUE;
}

static UINT8 *BwFindInSdt(UINT8 *sdt, UINT32 entrySize)
{
  UINT32 len, count, i;

  if (sdt == 0) {
    return 0;
  }
  len = *(UINT32 *)(sdt + 4);
  if (len < 36) {
    return 0;
  }
  count = (len - 36) / entrySize;
  for (i = 0; i < count; i++) {
    UINT8 *t;
    if (entrySize == 8) {
      t = (UINT8 *)(UINTN)(*(UINT64 *)(sdt + 36 + i * 8));
    } else {
      t = (UINT8 *)(UINTN)(*(UINT32 *)(sdt + 36 + i * 4));
    }
    if (t == 0) {
      continue;
    }
    if (t[0] == 'S' && t[1] == 'R' && t[2] == 'A' && t[3] == 'T') {
      return t;
    }
  }
  return 0;
}

static UINT8 *BwFindSrat(VOID)
{
  EFI_CONFIGURATION_TABLE *ct;
  UINTN i, n;
  UINT64 xsdt = 0, rsdt = 0;

  if (gApp.ST == 0) {
    return 0;
  }
  ct = gApp.ST->ConfigurationTable;
  n = gApp.ST->NumberOfTableEntries;
  for (i = 0; i < n; i++) {
    if (ct[i].VendorTable == 0) {
      continue;
    }
    if (BwGuidEq(&ct[i].VendorGuid, &gBwAcpi20Guid)) {
      UINT8 *rsdp = (UINT8 *)ct[i].VendorTable;
      if (rsdp[0] == 'R' && rsdp[1] == 'S' && rsdp[2] == 'D' && rsdp[3] == 'P') {
        xsdt = *(UINT64 *)(rsdp + 24);
      }
    } else if (BwGuidEq(&ct[i].VendorGuid, &gBwAcpi10Guid)) {
      UINT8 *rsdp = (UINT8 *)ct[i].VendorTable;
      if (rsdp[0] == 'R' && rsdp[1] == 'S' && rsdp[2] == 'D' && rsdp[3] == 'P') {
        rsdt = *(UINT32 *)(rsdp + 16);
      }
    }
  }
  {
    UINT8 *t = BwFindInSdt((UINT8 *)(UINTN)xsdt, 8);
    if (t != 0) {
      return t;
    }
    return BwFindInSdt((UINT8 *)(UINTN)rsdt, 4);
  }
}

static UINT32 BwAddDom(UINT32 dom)
{
  UINT32 i;

  for (i = 0; i < gDomCount; i++) {
    if (gDomVal[i] == dom) {
      return i;
    }
  }
  if (gDomCount < BW_MAX_DOM) {
    gDomVal[gDomCount] = dom;
    return gDomCount++;
  }
  return BW_NO_DOM;
}

static VOID BwMapApic(UINT32 apic, UINT32 dom)
{
  UINT32 i;

  for (i = 0; i < gApicDomCount; i++) {
    if (gApicDom[i].Apic == apic) {
      gApicDom[i].Dom = dom;
      return;
    }
  }
  if (gApicDomCount < BW_MAX_APIC) {
    gApicDom[gApicDomCount].Apic = apic;
    gApicDom[gApicDomCount].Dom = dom;
    gApicDomCount++;
  }
}

static BOOLEAN BwDomOfApic(UINT32 apic, UINT32 *dom)
{
  UINT32 i;

  for (i = 0; i < gApicDomCount; i++) {
    if (gApicDom[i].Apic == apic) {
      *dom = gApicDom[i].Dom;
      return TRUE;
    }
  }
  return FALSE;
}

static VOID BwParseSrat(UINT8 *srat)
{
  UINT32 len = *(UINT32 *)(srat + 4);
  UINT32 off = 40;

  while (off + 2 <= len) {
    UINT8 type = srat[off];
    UINT8 elen = srat[off + 1];
    if (elen < 2 || off + elen > len) {
      break;
    }
    if (type == 1 && elen >= 32) {
      UINT32 dom = *(UINT32 *)(srat + off + 2);
      UINT64 base = *(UINT64 *)(srat + off + 8);
      UINT64 rlen = *(UINT64 *)(srat + off + 16);
      UINT32 flags = *(UINT32 *)(srat + off + 28);
      if ((flags & 1) && rlen != 0 && gRangeCount < BW_MAX_RANGE) {
        UINT32 slot = BwAddDom(dom);
        if (slot != BW_NO_DOM) {
          gRanges[gRangeCount].Base = base;
          gRanges[gRangeCount].Len = rlen;
          gRanges[gRangeCount].Dom = slot;
          gRangeCount++;
        }
      }
    } else if (type == 2 && elen >= 16) {
      UINT32 dom = *(UINT32 *)(srat + off + 4);
      UINT32 apic = *(UINT32 *)(srat + off + 8);
      if (BwAddDom(dom) != BW_NO_DOM) {
        BwMapApic(apic, dom);
      }
    } else if (type == 0 && elen >= 12) {
      UINT32 dom = *(UINT32 *)(srat + off + 2);
      UINT32 apic = srat[off + 6];
      if (BwAddDom(dom) != BW_NO_DOM) {
        BwMapApic(apic, dom);
      }
    }
    off += elen;
  }
}

static VOID BwReadAcpi(VOID)
{
  UINT8 *srat;

  gRangeCount = 0;
  gApicDomCount = 0;
  gDomCount = 0;
  srat = BwFindSrat();
  if (srat == 0) {
    return;
  }
  BwParseSrat(srat);
  gBw.SratPresent = (gDomCount > 0) ? TRUE : FALSE;
  DbgStr("bw: srat doms=");
  DbgDec(gDomCount);
  DbgStr(" ranges=");
  DbgDec(gRangeCount);
  DbgStr(DBG_NL);
}

/* --------------------------------------------------------------- placement */
static UINT32 BwRangeDom(UINT64 addr)
{
  UINT32 i;

  for (i = 0; i < gRangeCount; i++) {
    if (addr >= gRanges[i].Base && addr < gRanges[i].Base + gRanges[i].Len) {
      return gRanges[i].Dom;
    }
  }
  return BW_NO_DOM;
}

static EFI_MEMORY_DESCRIPTOR *BwMemoryMap(UINTN *count, UINTN *descSize)
{
  EFI_STATUS status;
  UINTN size = 0, mapKey = 0;
  UINT32 descVer = 0;
  EFI_MEMORY_DESCRIPTOR *map = 0;
  UINT8 *buffer;
  UINT32 attempt;

  for (attempt = 0; attempt < 4; attempt++) {
    status = gApp.BS->GetMemoryMap(&size, map, &mapKey, descSize, &descVer);
    if (!EFI_ERROR(status) && map != 0) {
      *count = size / (*descSize);
      return map;
    }
    size += (*descSize ? *descSize * 8 : 4096);
    if (map != 0) {
      gApp.BS->FreePool(map);
      map = 0;
    }
    if (EFI_ERROR(gApp.BS->AllocatePool(EfiLoaderData, size, (VOID **)&buffer))) {
      return 0;
    }
    map = (EFI_MEMORY_DESCRIPTOR *)buffer;
  }
  if (map != 0) {
    gApp.BS->FreePool(map);
  }
  return 0;
}

static UINT64 BwFreeBytes(EFI_MEMORY_DESCRIPTOR *map, UINTN count, UINTN descSize)
{
  UINT64 total = 0;
  UINTN i;

  for (i = 0; i < count; i++) {
    EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)map + i * descSize);
    if (d->Type == EfiConventionalMemory) {
      total += d->NumberOfPages * EFI_PAGE_SIZE;
    }
  }
  return total;
}

/* Allocates inside a physical range that SRAT attributes to this domain. */
static UINT8 *BwAllocInDomain(UINT32 dom, UINTN bytes)
{
  UINTN count = 0, descSize = 0;
  EFI_MEMORY_DESCRIPTOR *map = BwMemoryMap(&count, &descSize);
  UINTN pages = bytes / EFI_PAGE_SIZE;
  UINT8 *found = 0;
  UINTN i;

  if (map == 0) {
    return 0;
  }
  for (i = 0; i < count && found == 0; i++) {
    EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)map + i * descSize);
    UINT64 start, end;
    UINT32 tries = 0;
    if (d->Type != EfiConventionalMemory) {
      continue;
    }
    start = d->PhysicalStart;
    end = start + d->NumberOfPages * EFI_PAGE_SIZE;
    /* 2 MiB alignment keeps every sample on whole large pages */
    start = (start + 0x1FFFFFull) & ~0x1FFFFFull;
    while (start + bytes <= end && tries < 64) {
      UINT64 addr = start;
      tries++;
      if (BwRangeDom(addr) == dom && BwRangeDom(addr + bytes - 1) == dom) {
        if (!EFI_ERROR(gApp.BS->AllocatePages(AllocateAddress, EfiLoaderData, pages, &addr))) {
          found = (UINT8 *)(UINTN)addr;
          break;
        }
      }
      start += 0x200000ull;
    }
  }
  gApp.BS->FreePool(map);
  return found;
}

static UINT8 *BwAllocAny(UINTN bytes)
{
  EFI_PHYSICAL_ADDRESS addr = 0;

  if (EFI_ERROR(gApp.BS->AllocatePages(AllocateAnyPages, EfiLoaderData,
                                       bytes / EFI_PAGE_SIZE, &addr))) {
    return 0;
  }
  return (UINT8 *)(UINTN)addr;
}

/* Every core streams its own chunk: a shared buffer would be served from the
   cache of whoever read it last and the aggregate numbers would be fiction. */
static VOID BwPlaceBuffers(VOID)
{
  UINTN count = 0, descSize = 0;
  EFI_MEMORY_DESCRIPTOR *map;
  UINT64 freeBytes, chunk, pool;
  UINT32 d;

  if (gBufBytes != 0) {
    return;                       /* already placed once, keep the buffers   */
  }
  if (gBw.Count == 0) {
    return;
  }
  map = BwMemoryMap(&count, &descSize);
  if (map == 0) {
    return;
  }
  freeBytes = BwFreeBytes(map, count, descSize);
  gApp.BS->FreePool(map);

  chunk = BW_CHUNK_MAX;
  pool = (UINT64)gBw.Count * (UINT64)gDomCount;
  if (pool == 0) {
    return;
  }
  if (chunk * pool > freeBytes / 4) {
    chunk = (freeBytes / 4) / pool;
  }
  chunk &= ~0x1FFFFFull;          /* whole 2 MiB pages per chunk             */
  if (chunk < BW_CHUNK_MIN) {
    chunk = BW_CHUNK_MIN;
    if (chunk * pool > freeBytes / 2) {
      return;
    }
  }
  gBw.PlacementKnown = TRUE;
  for (d = 0; d < gDomCount; d++) {
    UINTN bytes = (UINTN)(chunk * gBw.Count);
    gBuf[d] = gBw.SratPresent ? BwAllocInDomain(d, bytes) : 0;
    if (gBuf[d] == 0) {
      if (gBw.SratPresent) {
        gBw.PlacementKnown = FALSE;
      }
      gBuf[d] = BwAllocAny(bytes);
    }
    if (gBuf[d] == 0) {
      if (d == 0) {
        return;
      }
      gBuf[d] = gBuf[0];
      gBw.SharedBuffer = TRUE;
      gBw.PlacementKnown = FALSE;
    }
  }
  gChunkBytes = (UINTN)chunk;
  gBufBytes = (UINTN)(chunk * gBw.Count);
  gBw.Bytes = (UINT32)(chunk / (1024 * 1024));
  DbgStr("bw: chunk mb=");
  DbgDec(chunk / (1024 * 1024));
  DbgStr(" pool mb=");
  DbgDec((chunk * pool) / (1024 * 1024));
  DbgStr(" doms=");
  DbgDec(gDomCount);
  DbgStr(" placed=");
  DbgDec(gBw.PlacementKnown ? 1 : 0);
  DbgStr(DBG_NL);
}

/* --------------------------------------------------------------- topology */
static UINT32 BwIndexForProc(UINT32 proc)
{
  return (proc < BW_MAX_CPU) ? gBwByProc[proc] : 0xFFFFFFFFu;
}

EFI_STATUS BwPrepare(VOID)
{
  EFI_MP_SERVICES_PROTOCOL *mp = 0;
  UINT32 i, usedDom = 0;
  BOOLEAN domSeen[BW_MAX_DOM];

  if (gApp.BS == 0) {
    return EFI_NOT_READY;
  }
  if (gBwPrepared) {
    return gBw.Count ? EFI_SUCCESS : EFI_UNSUPPORTED;
  }
  BwReset();
  gBwPrepared = TRUE;
  if (EFI_ERROR(LatPrepare()) || gLat.Count < 1) {
    gBw.Count = 0;
    return EFI_UNSUPPORTED;
  }
  gBw.Count = gLat.Count;
  gBw.SocketCount = gLat.SocketCount;
  gBw.CoreCount = gLat.CoreCount;

  BwReadAcpi();
  if (gDomCount == 0) {
    gDomCount = gBw.SocketCount ? gBw.SocketCount : 1;
    if (gDomCount > BW_MAX_DOM) {
      gDomCount = BW_MAX_DOM;
    }
  }
  if (gDomCount > BW_MAX_DOM) {
    gDomCount = BW_MAX_DOM;
  }

  if (EFI_ERROR(gApp.BS->LocateProtocol((EFI_GUID *)&gEfiMpServiceProtocolGuid, 0,
                                        (VOID **)&mp)) || mp == 0) {
    gBw.Count = 0;
    return EFI_UNSUPPORTED;
  }
  for (i = 0; i < BW_MAX_DOM; i++) {
    domSeen[i] = FALSE;
  }
  for (i = 0; i < gBw.Count; i++) {
    EFI_PROCESSOR_INFORMATION info;
    UINT32 apic = 0, dom = BW_NO_DOM, slot;

    gBwCpus[i].ProcId = gLatCpus[i].ProcId;
    gBwCpus[i].Socket = gLatCpus[i].Socket;
    gBwCpus[i].Core = gLatCpus[i].Core;
    gBwCpus[i].Thread = gLatCpus[i].Thread;
    gBwByProc[gBwCpus[i].ProcId] = i;
    if (!EFI_ERROR(mp->GetProcessorInfo(mp, gBwCpus[i].ProcId, &info))) {
      apic = (UINT32)info.ProcessorId;
    }
    if (gBw.SratPresent && BwDomOfApic(apic, &dom)) {
      slot = BW_NO_DOM;
      {
        UINT32 s;
        for (s = 0; s < gDomCount; s++) {
          if (gDomVal[s] == dom) {
            slot = s;
            break;
          }
        }
      }
    } else {
      slot = (gBwCpus[i].Socket < gDomCount) ? gBwCpus[i].Socket : (gDomCount - 1);
    }
    if (slot == BW_NO_DOM) {
      slot = (gBwCpus[i].Socket < gDomCount) ? gBwCpus[i].Socket : (gDomCount - 1);
    }
    gBwCpus[i].Dom = (UINT8)slot;
    if (slot < BW_MAX_DOM) {
      domSeen[slot] = TRUE;
    }
  }
  mp = 0;
  /* domains nobody belongs to would only add empty columns */
  for (i = 0; i < gDomCount; i++) {
    if (domSeen[i]) {
      usedDom++;
    }
  }
  if (usedDom == 0) {
    gBw.DomCount = 1;
  } else {
    gBw.DomCount = gDomCount;
  }
  gBw.CellsTotal = gBw.Count * gBw.DomCount;
  BwPlaceBuffers();
  DbgStr("bw: cpus=");
  DbgDec(gBw.Count);
  DbgStr(" doms=");
  DbgDec(gBw.DomCount);
  DbgStr(DBG_NL);
  return EFI_SUCCESS;
}

UINT32 BwCell(UINT32 index, UINT32 dom)
{
  if (index >= gBw.Count || dom >= gBw.DomCount) {
    return 0;
  }
  return gBw.Mbs[index][dom];
}

UINT32 BwDomOf(UINT32 index)
{
  if (index >= gBw.Count) {
    return 0;
  }
  return gBwCpus[index].Dom;
}

/* ---------------------------------------------------------------- workers */
static VOID BwWorker(VOID *arg)
{
  UINTN self = 0;
  UINT32 idx;
  UINT64 seen = 0;

  (VOID)arg;
  if (gBwMp == 0 || EFI_ERROR(gBwMp->WhoAmI(gBwMp, &self))) {
    return;
  }
  idx = BwIndexForProc((UINT32)self);
  if (idx >= gBw.Count) {
    return;
  }
  __asm__ __volatile__("" ::: "memory");
  gBwReady++;
  for (;;) {
    UINT64 seq;
    if (gBwExit) {
      break;
    }
    seq = gJob[idx].Seq;
    if (seq == 0 || seq == seen) {
      BwIdle();
      continue;
    }
    seen = seq;
    {
      UINT32 buf = gJob[idx].Buf;
      UINT32 mode = gJob[idx].Mode;
      UINTN len = (UINTN)gJob[idx].Bytes;
      UINT64 t0, t1;
      if (buf >= BW_MAX_DOM || gBuf[buf] == 0) {
        gJob[idx].Cycles = 0;
      } else {
        t0 = BwTsc();
        BwRead(gBuf[buf] + (UINTN)gJob[idx].Off, len, gJob[idx].Iters);
        BwBarrier();
        t1 = BwTsc();
        gJob[idx].Cycles = t1 - t0;
      }
      if (mode == 2) {
        __asm__ __volatile__("lock incl %0" : "+m"(gBwAggDone));
      }
      gJob[idx].Done = seq;
    }
  }
}

static UINT32 BwMbsFrom(UINTN bytes, UINT32 iters, UINT64 cycles, UINT64 hz)
{
  UINT64 total;

  if (cycles == 0) {
    return 0;
  }
  total = (UINT64)bytes * (UINT64)(iters ? iters : 1);
  /* KiB per second keeps the intermediate product inside 64 bits even for the
     six gigabyte aggregate passes, then 97656 KiB/s is one tenth of a GB/s */
  return (UINT32)((((total >> 10) * hz) / cycles) / 97656ull);
}

static UINT32 BwCellOnce(UINT32 idx, UINT32 dom, UINT32 bspIdx, UINT64 hz)
{
  UINT64 cycles = 0;

  if (dom >= gDomCount || gBuf[dom] == 0 || gChunkBytes == 0) {
    return 0;
  }
  if (idx == bspIdx) {
    UINT64 t0 = BwTsc();
    BwRead(gBuf[dom] + (UINTN)idx * gChunkBytes, gChunkBytes, 1);
    BwBarrier();
    cycles = BwTsc() - t0;
  } else {
    UINT64 seq;
    gJob[idx].Bytes = gChunkBytes;
    gJob[idx].Off = (UINT64)idx * gChunkBytes;
    gJob[idx].Buf = dom;
    gJob[idx].Mode = 1;
    gJob[idx].Iters = 1;
    gJob[idx].Cycles = 0;
    gJob[idx].Done = 0;
    BwBarrier();
    seq = (UINT64)++gBwSeq;
    gBwDeadline = BwTsc() + hz * 20;
    gJob[idx].Seq = seq;
    while (gJob[idx].Done != seq) {
      if (gBwExit || BwTsc() > gBwDeadline) {
        return 0;
      }
      BwPause();
    }
    cycles = gJob[idx].Cycles;
  }
  return BwMbsFrom(gChunkBytes, 1, cycles, hz);
}

/* Every core streams a slice of one domain buffer at the same time; mode 0
   gives each core its own domain, mode 1 points them all at the same one. */
static UINT32 BwAggregate(UINT32 mode, UINT32 dom, UINT32 bspIdx, UINT64 hz)
{
  UINTN slice;
  UINT32 n = gBw.Count, i;
  UINT64 seq, t0, cycles;

  if (gChunkBytes == 0 || n == 0) {
    return 0;
  }
  slice = gChunkBytes;

  gBwAggDone = 0;
  seq = (UINT64)++gBwSeq;
  BwBarrier();
  gBwDeadline = BwTsc() + hz * 30;
  t0 = BwTsc();
  for (i = 0; i < n; i++) {
    UINT32 buf = mode ? dom : gBwCpus[i].Dom;
    if (buf >= gDomCount) {
      buf = 0;
    }
    gJob[i].Bytes = slice;
    gJob[i].Off = (UINT64)i * slice;
    gJob[i].Buf = buf;
    gJob[i].Mode = 2;
    gJob[i].Iters = 1;
    gJob[i].Cycles = 0;
    gJob[i].Done = 0;
    BwBarrier();
    gJob[i].Seq = seq;
  }
  /* the boot processor takes its own share while the others work */
  if (bspIdx < n) {
    UINT32 buf = mode ? dom : gBwCpus[bspIdx].Dom;
    if (buf >= gDomCount) {
      buf = 0;
    }
    BwRead(gBuf[buf] + (UINTN)bspIdx * slice, slice, 1);
  }
  while (gBwAggDone < n - 1) {
    if (BwTsc() > gBwDeadline) {
      break;
    }
    BwPause();
  }
  BwBarrier();
  cycles = BwTsc() - t0;
  return BwMbsFrom(slice, n, cycles, hz);
}

/* ------------------------------------------------------------------- stats */
static UINT32 BwMedian(UINT32 *v, UINT32 n)
{
  UINT32 i;

  for (i = 1; i < n; i++) {
    UINT32 x = v[i];
    UINT32 j = i;
    while (j > 0 && v[j - 1] > x) {
      v[j] = v[j - 1];
      j--;
    }
    v[j] = x;
  }
  return n ? v[n / 2] : 0;
}

static VOID BwSummarize(VOID)
{
  UINT32 i, d;
  UINT32 local[BW_MAX_CPU * BW_MAX_DOM];
  UINT32 remote[BW_MAX_CPU * BW_MAX_DOM];
  UINT32 localN = 0, remoteN = 0;

  gBw.MinMbs = gBw.MaxMbs = 0;
  gBw.PeakMbs = 0;
  for (i = 0; i < gBw.Count; i++) {
    for (d = 0; d < gBw.DomCount; d++) {
      UINT32 v = gBw.Mbs[i][d];
      if (v == 0) {
        continue;
      }
      if (gHistN < BW_MAX_CPU * BW_MAX_DOM) {
        gHist[gHistN++] = v;
      }
      if (gBw.MinMbs == 0 || v < gBw.MinMbs) {
        gBw.MinMbs = v;
      }
      if (v > gBw.MaxMbs) {
        gBw.MaxMbs = v;
      }
      if (v > gBw.PeakMbs) {
        gBw.PeakMbs = v;
      }
      if (gBwCpus[i].Dom == d) {
        if (localN < BW_MAX_CPU * BW_MAX_DOM) {
          local[localN++] = v;
        }
      } else {
        if (remoteN < BW_MAX_CPU * BW_MAX_DOM) {
          remote[remoteN++] = v;
        }
      }
    }
  }
  gBw.MedianMbs = BwMedian(gHist, gHistN);
  gBw.LocalMbs = BwMedian(local, localN);
  gBw.RemoteMbs = BwMedian(remote, remoteN);
  gBw.LocalCount = localN;
  gBw.RemoteCount = remoteN;
}

/* --------------------------------------------------------------------- run */
VOID BwReset(VOID)
{
  gHistN = 0;
  if (gApp.BS != 0) {
    gApp.BS->SetMem(&gBw, sizeof(gBw), 0);
  }
}

EFI_STATUS BwRun(BOOLEAN (*tick)(UINT32 done, UINT32 total))
{
  EFI_EVENT apEvent = 0;
  EFI_STATUS status;
  UINT64 hz, start, lastTick;
  UINT32 n, i, d, bspIdx = 0xFFFFFFFFu, done = 0;
  UINTN self = 0;

  gBwExit = FALSE;
  gBwSeq = 0;
  hz = SysTscHz();
  if (hz == 0) {
    hz = 1000000000ull;
  }
  status = BwPrepare();
  if (EFI_ERROR(status)) {
    return status;
  }
  if (gBufBytes == 0) {
    return EFI_OUT_OF_RESOURCES;
  }
  if (EFI_ERROR(gApp.BS->LocateProtocol((EFI_GUID *)&gEfiMpServiceProtocolGuid, 0,
                                        (VOID **)&gBwMp)) || gBwMp == 0) {
    gBwMp = 0;
    return EFI_UNSUPPORTED;
  }
  if (!EFI_ERROR(gBwMp->WhoAmI(gBwMp, &self))) {
    bspIdx = BwIndexForProc((UINT32)self);
  }

  n = gBw.Count;
  gApp.BS->SetMem(gBw.Mbs, sizeof(gBw.Mbs), 0);
  gApp.BS->SetMem(gBw.DomAggMbs, sizeof(gBw.DomAggMbs), 0);
  gHistN = 0;
  gBw.Cells = 0;
  gBw.TotalMbs = 0;
  gBw.Aborted = FALSE;
  gBw.Ran = FALSE;
  gBw.ElapsedMs = 0;
  gApp.BS->SetMem(gJob, sizeof(gJob), 0);
  gBwReady = 0;
  gBwAggDone = 0;

  status = gApp.BS->CreateEvent(0, TPL_CALLBACK, 0, 0, &apEvent);
  if (EFI_ERROR(status)) {
    gBwMp = 0;
    return status;
  }
  status = gBwMp->StartupAllAPs(gBwMp, BwWorker, FALSE, apEvent, 0, 0, 0);
  if (EFI_ERROR(status)) {
    gApp.BS->CloseEvent(apEvent);
    gBwMp = 0;
    return status;
  }
  {
    UINT64 guard = BwTsc() + hz * 3;
    while (gBwReady < n - 1 && BwTsc() < guard) {
      BwPause();
    }
    DbgStr("bw: parked=");
    DbgDec(gBwReady);
    DbgStr(DBG_NL);
  }

  start = BwTsc();
  lastTick = start;
  for (i = 0; i < n && !gBw.Aborted; i++) {
    for (d = 0; d < gBw.DomCount; d++) {
      UINT32 mbs = BwCellOnce(i, d, bspIdx, hz);
      gBw.Mbs[i][d] = mbs;
      if (mbs) {
        gBw.Cells++;
      }
      done++;
      if (tick != 0 && SysMsSince(lastTick) >= 180) {
        lastTick = SysTsc();
        if (!tick(done, gBw.CellsTotal + 1 + gBw.DomCount)) {
          gBw.Aborted = TRUE;
          break;
        }
      }
      if (gBw.Aborted || gStopRequested) {
        gBw.Aborted = TRUE;
        break;
      }
    }
  }

  if (!gBw.Aborted) {
    gBw.TotalMbs = BwAggregate(0, 0, bspIdx, hz);
    for (d = 0; d < gBw.DomCount; d++) {
      gBw.DomAggMbs[d] = BwAggregate(1, d, bspIdx, hz);
      done++;
      if (tick != 0) {
        if (!tick(done, gBw.CellsTotal + 1 + gBw.DomCount)) {
          gBw.Aborted = TRUE;
          break;
        }
      }
      if (gStopRequested) {
        gBw.Aborted = TRUE;
        break;
      }
    }
  }

  gBwExit = TRUE;
  gBwDeadline = BwTsc();
  {
    UINT64 guard = BwTsc() + hz * 2;
    while (EFI_ERROR(gApp.BS->CheckEvent(apEvent)) && BwTsc() < guard) {
      BwPause();
    }
  }
  gApp.BS->CloseEvent(apEvent);
  gBwMp = 0;

  gBw.ElapsedMs = (UINT32)SysMsSince(start);
  BwSummarize();
  gBw.Ran = TRUE;
  return gBw.Aborted ? EFI_ABORTED : EFI_SUCCESS;
}
