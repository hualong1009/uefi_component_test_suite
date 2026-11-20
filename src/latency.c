/* latency.c - core to core cache line hand-off latency measurement.

   The pair protocol is a strict ping-pong on one dedicated cache line: the timer
   side stores a token, the responder side observes it and stores the next one,
   the timer side observes that and stops its timestamp counter. A round trip is
   therefore "line leaves my L1, is modified in the peer L1, and comes back",
   and half of it is the one way latency reported on the page.

   All workers are parked on their own processor with a spin loop over a private
   cache line, so a measurement wakes up exactly the two cores that take part and
   the other 190 stay quiet - a shared broadcast variable would turn 192 cores
   into a coherency storm and ruin the numbers.

   Timing is a plain RDTSC on the timer core only: both ends of the interval are
   read by the same core, so TSC sync between sockets never enters the result. */
#include "latency.h"
#include "ui.h"
#include "sysinfo.h"
#include "debug.h"

#define LAT_ITERS     96
#define LAT_WARMUP    16
#define LAT_HIST      8192      /* 1 ns buckets, the median is reported back
                                   in tenths of a nanosecond */
#define LAT_NS_MAX    65535u
#define LAT_NO_INDEX  0xFFFFFFFFu

LAT_REPORT gLat;
LAT_CPU    gLatCpus[LAT_MAX_CPU];

typedef struct __attribute__((aligned(64))) {
  volatile UINT64 Seq;        /* published by the orchestrator            */
  volatile UINT64 State;      /* set by the worker when the pair is done  */
  volatile UINT64 Cycles;     /* median round trip in TSC cycles          */
  volatile UINT64 TStart;     /* diagnostics: when the role function ran  */
  volatile UINT64 TEnd;
  volatile UINT64 TSpin;      /* when the first token wait was entered    */
  volatile UINT32 IterDone;
  UINT32          Role;       /* 1 timer, 2 responder                     */
  UINT32          Iters;
  UINT32          Line;       /* index of the line both ends must use      */
  UINT64          Pad;
} LAT_CTRL;  /* Pad is reused for diagnostics */

typedef struct __attribute__((aligned(64))) {
  volatile UINT64 Ping;       /* counter written by the timing core       */
  volatile UINT64 Pong;       /* the same counter echoed back             */
  UINT64          Pad[6];
} LAT_LINE;

static LAT_CTRL gCtrl[LAT_MAX_CPU];
static LAT_LINE gLine[LAT_MAX_CPU];

static volatile BOOLEAN gLatExit __attribute__((aligned(64)));
static volatile UINT32  gLatReady __attribute__((aligned(64)));
static volatile UINT32  gLatBsp = LAT_NO_INDEX;
static volatile UINT64  gLatDeadline __attribute__((aligned(64)));
static EFI_MP_SERVICES_PROTOCOL *gLatMp;
static BOOLEAN gLatTopoDone;
static UINT32 gLatByProc[LAT_MAX_CPU];
static UINT32 gLatSeq;

/* ------------------------------------------------------------------ timing */
static UINT64 LatTsc(VOID)
{
  UINT32 lo, hi;
  __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi)::"memory");
  return ((UINT64)hi << 32) | (UINT64)lo;
}

static VOID LatPause(VOID)
{
  __asm__ __volatile__("pause" ::: "memory");
}

static VOID LatBarrier(VOID)
{
  __asm__ __volatile__("" ::: "memory");
}

/* ------------------------------------------------------------ pair workers */
static VOID LatTimer(LAT_CTRL *c, LAT_LINE *l)
{
  UINT64 samples[LAT_ITERS];
  UINT32 iter = c->Iters;
  UINT32 i, n = 0;

  c->Cycles = 0;
  c->IterDone = 0;
  c->TStart = LatTsc();
  c->TSpin = c->TStart;
  if (iter > LAT_ITERS) {
    iter = LAT_ITERS;
  }
  if (iter < LAT_WARMUP + 2) {
    iter = LAT_WARMUP + 2;
  }
  c->Pad = iter;
  /* every wait is a "reached at least" test: the other side only ever copies
     the counter, so it cannot be fooled by a late start */
  for (i = 1; i <= iter; i++) {
    UINT64 t0, t1;
    t0 = LatTsc();
    l->Ping = i;
    while (l->Pong < i) {
      if (gLatExit || LatTsc() > gLatDeadline) {
        return;
      }
      LatPause();
    }
    t1 = LatTsc();
    if (i > LAT_WARMUP) {
      samples[n++] = t1 - t0;
    }
  }
  c->IterDone = n;
  c->TEnd = LatTsc();
  if (n == 0) {
    return;
  }
  /* insertion sort, the sample count is small and this runs on an AP */
  for (i = 1; i < n; i++) {
    UINT64 v = samples[i];
    UINT32 j = i;
    while (j > 0 && samples[j - 1] > v) {
      samples[j] = samples[j - 1];
      j--;
    }
    samples[j] = v;
  }
  c->Cycles = samples[n / 2];
}

static VOID LatResponder(LAT_CTRL *c, LAT_LINE *l)
{
  UINT32 iter = c->Iters;
  UINT64 limit, seen, echoes = 0;

  c->IterDone = 0;
  c->TStart = LatTsc();
  c->TSpin = c->TStart;
  if (iter > LAT_ITERS) {
    iter = LAT_ITERS;
  }
  if (iter < LAT_WARMUP + 2) {
    iter = LAT_WARMUP + 2;
  }
  limit = iter;
  seen = 0;
  while (echoes < limit) {
    UINT64 now = l->Ping;
    if (now != seen) {
      seen = now;
      l->Pong = now;
      echoes++;
    } else if (gLatExit || LatTsc() > gLatDeadline) {
      break;
    } else {
      LatPause();
    }
  }
  c->IterDone = echoes;
  c->TEnd = LatTsc();
}

/* StartupAllAPs entry point. Everything it touches is our own memory plus
   WhoAmI, the only protocol call the specification allows from an AP. */
static VOID LatWorker(VOID *arg)
{
  UINTN self = 0;
  UINT32 idx;
  LAT_CTRL *c;
  LAT_LINE *l;
  UINT64 seen;

  (VOID)arg;
  if (gLatMp == 0 || EFI_ERROR(gLatMp->WhoAmI(gLatMp, &self)) || self >= LAT_MAX_CPU) {
    return;
  }
  idx = gLatByProc[self];
  if (idx == LAT_NO_INDEX) {
    return;
  }
  c = &gCtrl[idx];
  l = 0;
  /* announce that this core is parked, the orchestrator waits for the count
     before it publishes the first pair: a worker that starts late must not
     mistake an already published pair for a finished one */
  __atomic_fetch_add(&gLatReady, 1, __ATOMIC_SEQ_CST);
  seen = 0;
  for (;;) {
    UINT64 seq = c->Seq;
    if (seq != seen && seq != 0) {
      UINT32 line = c->Line;
      seen = seq;
      /* the line is published with the pair, so it has to be read here and
         not once at park time: the pair the orchestrator is running decides
         which line both ends talk on */
      l = &gLine[line < LAT_MAX_CPU ? line : idx];
      if (c->Role == 1) {
        LatTimer(c, l);
      } else if (c->Role == 2) {
        LatResponder(c, l);
      }
      LatBarrier();
      c->State = seq;
    }
    if (gLatExit) {
      c->State = (UINT64)-1;
      return;
    }
    LatPause();
  }
}

/* ---------------------------------------------------------------- summary */
static UINT32 HistMedian(const UINT32 *hist, UINT32 total)
{
  UINT32 i, seen = 0;

  if (total == 0) {
    return 0;
  }
  for (i = 0; i < LAT_HIST; i++) {
    seen += hist[i];
    if (seen * 2 >= total) {
      return i * 10;
    }
  }
  return (LAT_HIST - 1) * 10;
}

static VOID HistAdd(UINT32 *hist, UINT32 v)
{
  v /= 10;                 /* one bucket per nanosecond */
  if (v >= LAT_HIST) {
    v = LAT_HIST - 1;
  }
  hist[v]++;
}

static UINT32 gHistAll[LAT_HIST];
static UINT32 gHistIntra[LAT_HIST];
static UINT32 gHistCross[LAT_HIST];
static UINT32 gHistSib[LAT_HIST];
static UINT32 gHistSock[LAT_MAX_SOCKET][LAT_MAX_SOCKET][LAT_HIST];

static VOID LatSummarize(VOID)
{
  UINT32 a, b, n = gLat.Count;

  gApp.BS->SetMem(gHistAll, sizeof(gHistAll), 0);
  gApp.BS->SetMem(gHistIntra, sizeof(gHistIntra), 0);
  gApp.BS->SetMem(gHistCross, sizeof(gHistCross), 0);
  gApp.BS->SetMem(gHistSib, sizeof(gHistSib), 0);
  gApp.BS->SetMem(gHistSock, sizeof(gHistSock), 0);
  gLat.IntraCount = gLat.CrossCount = gLat.SiblingCount = 0;
  gLat.MinNs10 = 0xFFFFFFFFu;
  gLat.MaxNs10 = 0;

  for (a = 0; a < LAT_MAX_SOCKET; a++) {
    for (b = 0; b < LAT_MAX_SOCKET; b++) {
      gLat.SockCount[a][b] = 0;
      gLat.SockMinNs10[a][b] = 0xFFFFFFFFu;
      gLat.SockMaxNs10[a][b] = 0;
    }
  }

  for (a = 0; a < n; a++) {
    for (b = a + 1; b < n; b++) {
      UINT32 v = gLat.Ns10[a][b];
      UINT32 sa = gLatCpus[a].Socket;
      UINT32 sb = gLatCpus[b].Socket;
      if (v == 0) {
        continue;
      }
      HistAdd(gHistAll, v);
      if (v < gLat.MinNs10) {
        gLat.MinNs10 = v;
      }
      if (v > gLat.MaxNs10) {
        gLat.MaxNs10 = v;
      }
      if (sa < LAT_MAX_SOCKET && sb < LAT_MAX_SOCKET) {
        HistAdd(gHistSock[sa][sb], v);
        HistAdd(gHistSock[sb][sa], v);
        gLat.SockCount[sa][sb]++;
        gLat.SockCount[sb][sa]++;
        if (v < gLat.SockMinNs10[sa][sb]) {
          gLat.SockMinNs10[sa][sb] = v;
          gLat.SockMinNs10[sb][sa] = v;
        }
        if (v > gLat.SockMaxNs10[sa][sb]) {
          gLat.SockMaxNs10[sa][sb] = v;
          gLat.SockMaxNs10[sb][sa] = v;
        }
      }
      if (sa == sb) {
        if (gLatCpus[a].Core == gLatCpus[b].Core) {
          HistAdd(gHistSib, v);
          gLat.SiblingCount++;
        } else {
          HistAdd(gHistIntra, v);
          gLat.IntraCount++;
        }
      } else {
        HistAdd(gHistCross, v);
        gLat.CrossCount++;
      }
    }
  }

  gLat.MedianNs10 = HistMedian(gHistAll, gLat.IntraCount + gLat.CrossCount + gLat.SiblingCount);
  gLat.IntraNs10 = HistMedian(gHistIntra, gLat.IntraCount);
  gLat.CrossNs10 = HistMedian(gHistCross, gLat.CrossCount);
  gLat.SiblingNs10 = HistMedian(gHistSib, gLat.SiblingCount);
  for (a = 0; a < LAT_MAX_SOCKET; a++) {
    for (b = 0; b < LAT_MAX_SOCKET; b++) {
      gLat.SockNs10[a][b] = HistMedian(gHistSock[a][b], gLat.SockCount[a][b]);
      if (gLat.SockCount[a][b] == 0) {
        gLat.SockMinNs10[a][b] = 0;
      }
    }
  }
  if (gLat.MinNs10 == 0xFFFFFFFFu) {
    gLat.MinNs10 = 0;
  }
}

/* ------------------------------------------------------------------- setup */
VOID LatReset(VOID)
{
  gLatTopoDone = FALSE;
  if (gApp.BS != 0) {
    gApp.BS->SetMem(&gLat, sizeof(gLat), 0);
    gApp.BS->SetMem(&gLatCpus, sizeof(gLatCpus), 0);
  }
}

UINT32 LatNs10(UINT32 a, UINT32 b)
{
  if (a >= gLat.Count || b >= gLat.Count) {
    return 0;
  }
  return gLat.Ns10[a][b];
}

const char *LatCpuLabel(UINT32 index, char *buf, UINTN cap)
{
  if (index >= gLat.Count) {
    UiStrPrintf(buf, cap, "n/a");
    return buf;
  }
  UiStrPrintf(buf, cap, "P%u C%u T%u", gLatCpus[index].Socket, gLatCpus[index].Core,
              gLatCpus[index].Thread);
  return buf;
}

static UINT32 LatIndexForProc(UINT32 proc)
{
  return (proc < LAT_MAX_CPU) ? gLatByProc[proc] : LAT_NO_INDEX;
}

/* Collects enabled processors and sorts them by package, core and thread so the
   matrix shows one block per socket instead of the firmware's enumeration order. */
typedef struct {
  UINT32 Pkg;
  UINT32 Core;
  UINT32 Thread;
  UINT32 Proc;
  BOOLEAN En;
} LAT_RAW;

static LAT_RAW gRaw[LAT_MAX_CPU];

/* Collects the processors the firmware offers and orders them by package, core
   and thread, so the matrix shows one block per socket instead of the
   firmware's enumeration order.

   Two firmware habits are handled here rather than trusted: StatusFlag is not
   always filled in (some builds leave it zero, so the enabled bit says nothing
   and every processor would look disabled), and a package number is not always
   dense or small (one build reports package 6 for a single socket machine), so
   packages are remapped onto 0..n-1 before anything is drawn. */

/* ------------------------------------------------------------- topology */
static VOID LatCpuid(UINT32 leaf, UINT32 sub, UINT32 out[4])
{
  UINT32 a, b, c, d;

  __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(leaf), "c"(sub));
  out[0] = a;
  out[1] = b;
  out[2] = c;
  out[3] = d;
}

/* Bit layout of the APIC id, which is what EFI_PROCESSOR_INFORMATION calls
   ProcessorId on x86. The firmware location is used first, but several server
   firmwares leave it full of duplicates, so the hardware description wins
   whenever the processor is new enough to have one. */
static VOID LatTopoDetect(UINT32 *smtShift, UINT32 *coreShift)
{
  UINT32 r[4], leaf, maxLeaf, sub, smt = 0, core = 0;

  *smtShift = 0;
  *coreShift = 0;
  LatCpuid(0, 0, r);
  maxLeaf = r[0];
  leaf = 0;
  if (maxLeaf >= 0x1F) {
    leaf = 0x1F;          /* extended topology, v2 */
  } else if (maxLeaf >= 0x0B) {
    leaf = 0x0B;
  }
  if (leaf != 0) {
    for (sub = 0; sub < 8; sub++) {
      UINT32 type, width;
      LatCpuid(leaf, sub, r);
      type = (r[2] >> 8) & 0xFF;
      width = r[0] & 0x1F;
      if (width == 0 || type == 0) {
        break;
      }
      if (type == 1) {
        smt = width;
      } else if (type == 2) {
        core = width;
      } else if (core == 0) {
        core = width;    /* module or tile level: treat it as the core part */
      }
    }
  }
  if (core == 0 && maxLeaf >= 4) {
    UINT32 cores, logical = 1, perCore;
    LatCpuid(4, 0, r);
    cores = ((r[0] >> 26) & 0x3F) + 1;
    LatCpuid(1, 0, r);
    /* EBX[23:16] is every addressable logical processor in a package, so it
       covers both the cores and the threads of one */
    logical = (r[1] >> 16) & 0xFF;
    if (logical == 0) {
      logical = cores;
    }
    perCore = logical / (cores ? cores : 1);
    if (perCore < 1) {
      perCore = 1;
    }
    /* classic layout: thread bits below core bits, socket bits above */
    while ((1u << smt) < perCore) {
      smt++;
    }
    while ((1u << core) < logical) {
      core++;
    }
  }
  if (core > 0) {
    *smtShift = smt;
    *coreShift = core;
  }
}

static UINT32 LatTopoMask(UINT32 shift)
{
  return shift >= 32 ? 0xFFFFFFFFu : ((1u << shift) - 1);
}

static UINT32 LatCollect(VOID)
{
  UINTN total = 0, enabled = 0;
  UINT32 i, j, n = 0, enCount = 0, pkgCnt = 0;
  UINT32 pkgVal[LAT_MAX_SOCKET];
  UINT64 *keys;
  UINT32 *procs;
  EFI_PROCESSOR_INFORMATION info;
  BOOLEAN known, useEnabled;
  UINT32 smtShift, coreShift, topoSource;

  if (gApp.BS->SetMem != 0) {
    gApp.BS->SetMem(gLatByProc, sizeof(gLatByProc), 0xFF);
    gApp.BS->SetMem(gRaw, sizeof(gRaw), 0);
  }
  if (gLatMp == 0) {
    return 0;
  }
  if (EFI_ERROR(gLatMp->GetNumberOfProcessors(gLatMp, &total, &enabled))) {
    DbgStr("lat: GetNumberOfProcessors failed" DBG_NL);
    return 0;
  }
  if (total > LAT_MAX_CPU) {
    total = LAT_MAX_CPU;
  }
  DbgStr("lat: total=");
  DbgDec(total);
  DbgStr(" enabled=");
  DbgDec(enabled);
  DbgStr(DBG_NL);

  LatTopoDetect(&smtShift, &coreShift);
  topoSource = coreShift > 0 ? 2 : 1;
  DbgStr("lat: topo smt=");
  DbgDec(smtShift);
  DbgStr(" core=");
  DbgDec(coreShift);
  DbgStr(DBG_NL);

  for (i = 0; i < total; i++) {
    if (EFI_ERROR(gLatMp->GetProcessorInfo(gLatMp, i, &info))) {
      continue;
    }
    if (topoSource == 2) {
      UINT32 apic = (UINT32)info.ProcessorId;
      gRaw[i].Pkg = apic >> coreShift;
      gRaw[i].Core = (apic >> smtShift) & LatTopoMask(coreShift - smtShift);
      gRaw[i].Thread = apic & LatTopoMask(smtShift);
    } else {
      gRaw[i].Pkg = info.Location.Package;
      gRaw[i].Core = info.Location.Core;
      gRaw[i].Thread = info.Location.Thread;
    }
    gRaw[i].Proc = i;
    gRaw[i].En = ((info.StatusFlag & 2) != 0) ? TRUE : FALSE;   /* ENABLED bit */
    if (gRaw[i].En) {
      enCount++;
    }
    if (i < 4) {
      DbgStr("lat: cpu ");
      DbgDec(i);
      DbgStr(" flag=");
      DbgHex(info.StatusFlag);
      DbgStr(" pkg=");
      DbgDec(info.Location.Package);
      DbgStr(" core=");
      DbgDec(info.Location.Core);
      DbgStr(" thr=");
      DbgDec(info.Location.Thread);
      DbgStr(DBG_NL);
    }
  }

  useEnabled = enCount > 0 ? TRUE : FALSE;
  for (i = 0; i < total; i++) {
    if (gRaw[i].Proc != i) {           /* GetProcessorInfo failed earlier */
      continue;
    }
    if (useEnabled && !gRaw[i].En) {
      continue;
    }
    gRaw[n++] = gRaw[i];
  }
  if (n < 2) {
    DbgStr("lat: too few processors" DBG_NL);
    return n;
  }

  /* dense package numbering, ascending order keeps the lowest socket first */
  for (i = 0; i < n; i++) {
    UINT32 v = gRaw[i].Pkg;
    BOOLEAN seen = FALSE;
    if (v == 0xFFFFFFFFu) {
      continue;
    }
    for (j = 0; j < pkgCnt; j++) {
      if (pkgVal[j] == v) {
        seen = TRUE;
        break;
      }
    }
    if (!seen && pkgCnt < LAT_MAX_SOCKET) {
      pkgVal[pkgCnt++] = v;
    }
  }
  for (i = 1; i < pkgCnt; i++) {
    UINT32 v = pkgVal[i];
    UINT32 k = i;
    while (k > 0 && pkgVal[k - 1] > v) {
      pkgVal[k] = pkgVal[k - 1];
      k--;
    }
    pkgVal[k] = v;
  }
  known = (pkgCnt > 0 && pkgCnt < LAT_MAX_SOCKET) ? TRUE : FALSE;

  /* the location is only usable when every processor is a distinct
     package/core/thread triple, otherwise the matrix would claim that
     unrelated cores are the two threads of one core */
  if (known) {
    for (i = 0; i < n && known; i++) {
      for (j = 0; j < i; j++) {
        if (gRaw[i].Pkg == gRaw[j].Pkg && gRaw[i].Core == gRaw[j].Core &&
            gRaw[i].Thread == gRaw[j].Thread) {
          known = FALSE;
          break;
        }
      }
    }
  }

  if (EFI_ERROR(gApp.BS->AllocatePool(EfiBootServicesData, sizeof(UINT64) * (n + 1),
                                      (VOID **)&keys))) {
    return 0;
  }
  if (EFI_ERROR(gApp.BS->AllocatePool(EfiBootServicesData, sizeof(UINT32) * (n + 1),
                                      (VOID **)&procs))) {
    gApp.BS->FreePool(keys);
    return 0;
  }

  for (i = 0; i < n; i++) {
    UINT32 sock = 0, core, thread;
    if (known) {
      for (j = 0; j < pkgCnt; j++) {
        if (pkgVal[j] == gRaw[i].Pkg) {
          sock = j;
          break;
        }
      }
      core = gRaw[i].Core & 0xFF;
      thread = gRaw[i].Thread & 0xFF;
    } else {
      /* no usable location: processors become one block in enumeration order */
      sock = 0;
      core = i;
      thread = 0;
    }
    gLatCpus[i].ProcId = gRaw[i].Proc;
    gLatCpus[i].Socket = (UINT8)sock;
    gLatCpus[i].Core = (UINT8)core;
    gLatCpus[i].Thread = (UINT8)thread;
    gLatCpus[i].Used = TRUE;
    procs[i] = gRaw[i].Proc;
    keys[i] = ((UINT64)sock << 56) | ((UINT64)core << 40) | ((UINT64)thread << 24) |
              (gRaw[i].Proc & 0xFFFFFF);
  }

  /* stable insertion sort on the packed key, processor number breaks ties */
  for (i = 1; i < n; i++) {
    UINT64 kv = keys[i];
    UINT32 pv = procs[i];
    LAT_CPU cv = gLatCpus[i];
    UINT32 k = i;
    while (k > 0 && keys[k - 1] > kv) {
      keys[k] = keys[k - 1];
      procs[k] = procs[k - 1];
      gLatCpus[k] = gLatCpus[k - 1];
      k--;
    }
    keys[k] = kv;
    procs[k] = pv;
    gLatCpus[k] = cv;
  }
  for (i = 0; i < n; i++) {
    gLatByProc[procs[i]] = i;
  }

  gApp.BS->FreePool(keys);
  gApp.BS->FreePool(procs);
  gLat.TopologyKnown = known;
  gLat.TopoSource = known ? topoSource : 0;
  DbgStr("lat: collected=");
  DbgDec(n);
  DbgStr(" packages=");
  DbgDec(pkgCnt);
  DbgStr(" known=");
  DbgDec(known ? 1 : 0);
  DbgStr(DBG_NL);
  return n;
}

/* --------------------------------------------------------------------- run */
static EFI_STATUS LatLocate(VOID)
{
  gLatMp = 0;
  if (EFI_ERROR(gApp.BS->LocateProtocol((EFI_GUID *)&gEfiMpServiceProtocolGuid, 0,
                                        (VOID **)&gLatMp)) || gLatMp == 0) {
    gLatMp = 0;
    return EFI_UNSUPPORTED;
  }
  return EFI_SUCCESS;
}

EFI_STATUS LatPrepare(VOID)
{
  UINT32 n;

  if (gApp.BS == 0) {
    return EFI_NOT_READY;
  }
  if (gLatTopoDone) {
    return gLat.Count >= 2 ? EFI_SUCCESS : EFI_UNSUPPORTED;
  }
  LatReset();
  gLatTopoDone = TRUE;
  if (EFI_ERROR(LatLocate())) {
    DbgStr("lat: MP services not found" DBG_NL);
    gLat.Count = 0;
    gLat.PairsTotal = 0;
    return EFI_UNSUPPORTED;
  }
  n = LatCollect();
  gLatMp = 0;
  gLat.Count = n;
  if (n < 2) {
    gLat.PairsTotal = 0;
    return EFI_UNSUPPORTED;
  }

  /* distinct packages and physical cores */
  {
    UINT32 i, j;
    for (i = 0; i < n; i++) {
      BOOLEAN seenSock = FALSE, seenCore = FALSE;
      for (j = 0; j < i; j++) {
        if (gLatCpus[j].Socket == gLatCpus[i].Socket) {
          seenSock = TRUE;
          if (gLatCpus[j].Core == gLatCpus[i].Core) {
            seenCore = TRUE;
          }
        }
      }
      if (!seenSock) {
        gLat.SocketCount++;
      }
      if (!seenCore) {
        gLat.CoreCount++;
      }
    }
  }
  gLat.PairsTotal = (UINT64)n * (n - 1) / 2;
  return EFI_SUCCESS;
}

/* One pair, one attempt. Returns the median round trip in TSC cycles, or 0 when
   the two ends did not finish in time. A firmware that never wakes its APs is
   the reason this has a deadline at all; a real pair needs microseconds. */
static UINT64 LatPairOnce(UINT32 a, UINT32 b, UINT32 bspIdx, UINT64 hz)
{
  UINT32 timerId = a, respId = b;
  UINT64 seq;

  /* the BSP cannot be the timer: it is the one waiting for the result, so it
     has to be the side that answers inline */
  if (respId != bspIdx && timerId == bspIdx) {
    timerId = b;
    respId = a;
  }

  gLine[timerId].Ping = 0;
  gLine[timerId].Pong = 0;
  gCtrl[timerId].Role = 1;
  gCtrl[timerId].Iters = LAT_ITERS;
  gCtrl[timerId].Line = timerId;
  gCtrl[timerId].State = 0;
  gCtrl[respId].Role = 2;
  gCtrl[respId].Iters = LAT_ITERS;
  gCtrl[respId].Line = timerId;
  gCtrl[respId].State = 0;
  LatBarrier();
  gLatDeadline = LatTsc() + hz * 10;
  seq = (UINT64)++gLatSeq;
  gCtrl[timerId].Seq = seq;
  gCtrl[respId].Seq = seq;

  if (respId == bspIdx) {
    LatResponder(&gCtrl[respId], &gLine[timerId]);
    LatBarrier();
    gCtrl[respId].State = seq;
  }

  while (gCtrl[timerId].State != seq) {
    if (LatTsc() > gLatDeadline) {
      return 0;
    }
    LatPause();
  }
  if (respId != bspIdx) {
    while (gCtrl[respId].State != seq) {
      if (LatTsc() > gLatDeadline) {
        break;
      }
      LatPause();
    }
  }
  return gCtrl[timerId].Cycles;
}

EFI_STATUS LatRun(BOOLEAN (*tick)(UINT32 done, UINT32 total))
{
  EFI_EVENT apEvent = 0;
  EFI_STATUS status;
  UINT64 hz, start, lastTick;
  UINT32 n, a, b, bspIdx = LAT_NO_INDEX;
  UINTN self = 0;
  UINT32 done = 0;

  gLatExit = FALSE;
  gLatSeq = 0;
  hz = SysTscHz();
  if (hz == 0) {
    hz = 1000000000ull;
  }

  status = LatPrepare();
  if (EFI_ERROR(status)) {
    return status;
  }
  if (EFI_ERROR(LatLocate())) {
    gLatMp = 0;
    return EFI_UNSUPPORTED;
  }
  n = gLat.Count;
  if (!EFI_ERROR(gLatMp->WhoAmI(gLatMp, &self))) {
    bspIdx = LatIndexForProc((UINT32)self);
  }
  gLatBsp = bspIdx;
  DbgStr("lat: bsp self=");
  DbgDec(self);
  DbgStr(" idx=");
  DbgDec(bspIdx);
  DbgStr(DBG_NL);

  /* a new run replaces the previous matrix but keeps the topology */
  gApp.BS->SetMem(gLat.Ns10, sizeof(gLat.Ns10), 0);
  gApp.BS->SetMem(gHistAll, sizeof(gHistAll), 0);
  gApp.BS->SetMem(gHistIntra, sizeof(gHistIntra), 0);
  gApp.BS->SetMem(gHistCross, sizeof(gHistCross), 0);
  gApp.BS->SetMem(gHistSib, sizeof(gHistSib), 0);
  gApp.BS->SetMem(gHistSock, sizeof(gHistSock), 0);
  gLat.Pairs = 0;
  gLat.Failed = 0;
  gLat.Aborted = FALSE;
  gLat.Ran = FALSE;
  gLat.MinNs10 = gLat.MaxNs10 = gLat.MedianNs10 = 0;
  gLat.IntraNs10 = gLat.CrossNs10 = gLat.SiblingNs10 = 0;
  gLat.IntraCount = gLat.CrossCount = gLat.SiblingCount = 0;
  gLat.ElapsedMs = 0;
  gApp.BS->SetMem(gCtrl, sizeof(gCtrl), 0);
  gApp.BS->SetMem(gLine, sizeof(gLine), 0);
  gLatReady = 0;

  /* park every AP on its own core; one broadcast, then only the two cores of
     the pair under measurement do any work */
  status = gApp.BS->CreateEvent(0, TPL_CALLBACK, 0, 0, &apEvent);
  if (EFI_ERROR(status)) {
    gLatMp = 0;
    return status;
  }
  status = gLatMp->StartupAllAPs(gLatMp, LatWorker, FALSE, apEvent, 0, 0, 0);
  if (EFI_ERROR(status)) {
    gApp.BS->CloseEvent(apEvent);
    gLatMp = 0;
    return status;
  }

  /* every processor except the BSP parks on its own core before the first pair
     is published */
  {
    UINT64 guard = LatTsc() + hz * 3;
    while (gLatReady < n - 1 && LatTsc() < guard) {
      LatPause();
    }
    DbgStr("lat: workers parked=");
    DbgDec(gLatReady);
    DbgStr(DBG_NL);
  }

  start = LatTsc();
  lastTick = start;
  for (a = 0; a < n; a++) {
    for (b = a + 1; b < n; b++) {
      UINT64 cycles = LatPairOnce(a, b, bspIdx, hz);

      if (cycles == 0) {
        DbgStr("lat: timeout a=");
        DbgDec(a);
        DbgStr(" b=");
        DbgDec(b);
        DbgStr(DBG_NL);
      }

      {
        UINT32 v = 0;
        if (cycles > 0) {
          /* one way = round trip / 2, expressed in tenths of a nanosecond */
          UINT64 ns10 = cycles * 5000000000ull / hz;
          v = (ns10 > LAT_NS_MAX) ? LAT_NS_MAX : (UINT32)ns10;
          if (v == 0) {
            v = 1;
          }
        }
        gLat.Ns10[a][b] = (UINT16)v;
        gLat.Ns10[b][a] = (UINT16)v;
        if (v != 0) {
          gLat.Pairs++;
          if (gLat.MinNs10 == 0 || v < gLat.MinNs10) {
            gLat.MinNs10 = v;
          }
          if (v > gLat.MaxNs10) {
            gLat.MaxNs10 = v;
          }
        }
      }
      done++;

      if (tick != 0 && SysMsSince(lastTick) >= 180) {
        lastTick = SysTsc();
        if (!tick(done, (UINT32)gLat.PairsTotal)) {
          gLat.Aborted = TRUE;
          gLatExit = TRUE;
          gLatDeadline = LatTsc();
          break;
        }
      }
      if (gLat.Aborted || gStopRequested) {
        gLat.Aborted = TRUE;
        gLatExit = TRUE;
        gLatDeadline = LatTsc();
        break;
      }
    }
    if (gLat.Aborted) {
      break;
    }
  }

  gLatExit = TRUE;
  gLatDeadline = LatTsc();
  {
    /* wait for the parked workers to leave; give up after two seconds so a
       firmware that never signals the event cannot hang the page */
    UINT64 guard = LatTsc() + hz * 2;
    while (EFI_ERROR(gApp.BS->CheckEvent(apEvent)) && LatTsc() < guard) {
      LatPause();
    }
  }
  gApp.BS->CloseEvent(apEvent);

  gLat.ElapsedMs = (UINT32)SysMsSince(start);
  LatSummarize();
  gLat.Ran = TRUE;
  return gLat.Aborted ? EFI_ABORTED : EFI_SUCCESS;
}
