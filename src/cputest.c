/* cputest.c - instruction level self-test plus throughput sampling. */
#include "cputest.h"
#include "ui.h"
#include "sysinfo.h"

CPU_REPORT gCpuReport;

#define CPU_CHK_MASK(i) (1u << (i))

static volatile UINT64 gSink;

static UINT64 CpuRdtsc(VOID)
{
  UINT32 lo, hi;
  __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
  return ((UINT64)hi << 32) | (UINT64)lo;
}

static UINT64 CpuXgetbv(VOID)
{
  UINT32 lo, hi;
  __asm__ __volatile__("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
  return ((UINT64)hi << 32) | (UINT64)lo;
}

static UINT64 CpuBitCount(UINT64 v)
{
  UINT64 n = 0;
  while (v) {
    n += v & 1;
    v >>= 1;
  }
  return n;
}

static UINT32 CpuFirstBit(UINT64 v)
{
  UINT32 i;
  if (v == 0) {
    return 64;
  }
  for (i = 0; i < 64; i++) {
    if (v & (1ull << i)) {
      return i;
    }
  }
  return 64;
}

static UINT32 CpuLastBit(UINT64 v)
{
  UINT32 i;
  if (v == 0) {
    return 64;
  }
  for (i = 63; i > 0; i--) {
    if (v & (1ull << i)) {
      return i;
    }
  }
  return 0;
}

static VOID CpuFail(UINT32 check, const char *msg)
{
  gCpuReport.Errors++;
  gCpuReport.Check[check].Level = 3;
  UiStrPrintf(gCpuReport.Check[check].Detail, sizeof(gCpuReport.Check[check].Detail), "%s", msg);
}

VOID CpuReportReset(VOID)
{
  gApp.BS->SetMem(&gCpuReport, sizeof(gCpuReport), 0);
}

/* ----------------------------------------------------------------- checks */
static VOID CheckAlu(VOID)
{
  static const UINT64 edges[] = {
    0ull, 1ull, 2ull, 0x7Full, 0x80ull, 0xFFFFull, 0x12345678ull, 0xFFFFFFFFull,
    0x7FFFFFFFFFFFFFFFull, 0x8000000000000000ull, 0xFFFFFFFFFFFFFFFFull,
    0x0123456789ABCDEFull, 0xFEDCBA9876543210ull
  };
  UINT32 i, j;
  UINT64 acc = 0;

  for (i = 0; i < sizeof(edges) / sizeof(edges[0]); i++) {
    for (j = 0; j < sizeof(edges) / sizeof(edges[0]); j++) {
      UINT64 a = edges[i], b = edges[j];
      UINT64 add = a + b, sub = a - b, mul = a * b;
      acc ^= add ^ sub ^ mul;
      if ((add - b) != a) {
        CpuFail(CPU_CHK_ALU, "整数加法不满足逆运算");
        return;
      }
      if (sub + b != a) {
        CpuFail(CPU_CHK_ALU, "整数减法不满足逆运算");
        return;
      }
      {
        UINT64 prod;
        if (!__builtin_mul_overflow(a, b, &prod) && b != 0) {
          if (prod / b != a) {
            CpuFail(CPU_CHK_ALU, "整数乘法不满足逆运算");
            return;
          }
          if (prod % b != 0) {
            CpuFail(CPU_CHK_ALU, "整数余数计算异常");
            return;
          }
        }
      }
      if (b != 0 && (a / b) * b + (a % b) != a) {
        CpuFail(CPU_CHK_ALU, "除法与余数不自洽");
        return;
      }
      {
        UINT32 k = (UINT32)(b & 63);
        UINT64 v = a;
        UINT64 rol = (v << k) | (k ? (v >> (64 - k)) : 0);
        UINT64 ror = (rol >> k) | (k ? (rol << (64 - k)) : 0);
        if (ror != v) {
          CpuFail(CPU_CHK_ALU, "循环移位不满足逆运算");
          return;
        }
        if (k != 0 && ((v << k) >> k) != (v & ((1ull << (64 - k)) - 1ull))) {
          CpuFail(CPU_CHK_ALU, "逻辑移位结果错误");
          return;
        }
      }
      {
        UINT64 s = a;
        UINT32 n;
        for (n = 0; n < 4; n++) {
          s = __builtin_bswap64(s);
        }
        if (s != a) {
          CpuFail(CPU_CHK_ALU, "字节序交换不自洽");
          return;
        }
      }
    }
  }
  gSink ^= acc;
  if (gCpuReport.Check[CPU_CHK_ALU].Level != 3) {
    gCpuReport.Check[CPU_CHK_ALU].Level = 1;
    UiStrPrintf(gCpuReport.Check[CPU_CHK_ALU].Detail,
                sizeof(gCpuReport.Check[CPU_CHK_ALU].Detail), "13×13 组边界值通过");
  }
}

static VOID CheckBitOps(VOID)
{
  UINT32 i;
  UINT64 acc = 0;

  for (i = 0; i < 64; i++) {
    UINT64 v = (1ull << i) | 0x5A5A000000000000ull;
    UINT64 pop = (UINT64)__builtin_popcountll(v);
    UINT32 ffs = v ? (UINT32)__builtin_ctzll(v) : 64;
    UINT32 fls = 63 - (UINT32)__builtin_clzll(v);

    if (pop != CpuBitCount(v)) {
      CpuFail(CPU_CHK_BITOPS, "POPCNT 结果与逐位统计不一致");
      return;
    }
    if (ffs != CpuFirstBit(v) || fls != CpuLastBit(v)) {
      CpuFail(CPU_CHK_BITOPS, "BSF/BSR 结果与扫描结果不一致");
      return;
    }
    {
      UINT64 a = v;
      UINT32 n = 0;
      while (a) {
        a &= a - 1;
        n++;
      }
      if (n != pop) {
        CpuFail(CPU_CHK_BITOPS, "位清除循环计数不一致");
        return;
      }
    }
    acc ^= v ^ ((UINT64)pop << 8) ^ ffs ^ fls;
  }
  gSink ^= acc;
  if (gCpuReport.Check[CPU_CHK_BITOPS].Level != 3) {
    gCpuReport.Check[CPU_CHK_BITOPS].Level = 1;
    UiStrPrintf(gCpuReport.Check[CPU_CHK_BITOPS].Detail,
                sizeof(gCpuReport.Check[CPU_CHK_BITOPS].Detail), "POPCNT/BSF/BSR 一致");
  }
}

typedef int    __attribute__((vector_size(16))) V4SI;
typedef double __attribute__((vector_size(16))) V2DF;
typedef float  __attribute__((vector_size(32))) V8SF;
typedef double __attribute__((vector_size(32))) V4DF;

static VOID CheckSse2(VOID)
{
  V4SI a = { 1, -7, 0x7FFFFFFF, -0x80000000 };
  V4SI b = { 3, 5, 1, -1 };
  V4SI sum = a + b;
  V4SI diff = sum - b;
  UINT32 i;
  UINT64 acc = 0;

  for (i = 0; i < 4; i++) {
    if (diff[i] != a[i]) {
      CpuFail(CPU_CHK_SSE2, "SSE2 32 位整型通道加减错误");
      return;
    }
    acc ^= (UINT64)(UINT32)sum[i];
  }
  {
    V2DF x = { 1.0, 4.0 };
    V2DF y = x * x;
    if (y[0] != 1.0 || y[1] != 16.0) {
      CpuFail(CPU_CHK_SSE2, "SSE2 双精度乘法错误");
      return;
    }
    acc ^= (UINT64)y[1];
  }
  {
    V4SI m = a * b;
    if (m[0] != 3 || m[1] != -35) {
      CpuFail(CPU_CHK_SSE2, "SSE2 向量乘法错误");
      return;
    }
    acc ^= (UINT64)(UINT32)m[0];
  }
  gSink ^= acc;
  if (gCpuReport.Check[CPU_CHK_SSE2].Level != 3) {
    gCpuReport.Check[CPU_CHK_SSE2].Level = 1;
    UiStrPrintf(gCpuReport.Check[CPU_CHK_SSE2].Detail,
                sizeof(gCpuReport.Check[CPU_CHK_SSE2].Detail), "128 位整型与双精度通过");
  }
}

__attribute__((target("avx"))) static VOID CheckAvxBody(BOOLEAN *ok, char *detail, UINT32 cap)
{
  V8SF f = { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f };
  V8SF g = f + f;
  V4DF d = { 1.0, 2.0, 3.0, 4.0 };
  V4DF e = d * d;
  UINT32 i;

  *ok = TRUE;
  for (i = 0; i < 8; i++) {
    if (g[i] != f[i] * 2.0f) {
      *ok = FALSE;
      UiStrPrintf(detail, cap, "AVX 单精度通道 %u 错误", i);
      return;
    }
  }
  for (i = 0; i < 4; i++) {
    if (e[i] != d[i] * d[i]) {
      *ok = FALSE;
      UiStrPrintf(detail, cap, "AVX 双精度通道 %u 错误", i);
      return;
    }
  }
  gSink ^= (UINT64)g[7] ^ (UINT64)e[3];
  UiStrPrintf(detail, cap, "256 位 YMM 通道通过");
}

static VOID CheckAvx(VOID)
{
  UINT32 ecx = gApp.Sys.FeaturesEcx;
  char detail[64];

  if (!(ecx & (1u << 28))) {
    gCpuReport.Check[CPU_CHK_AVX].Level = 4;
    UiStrPrintf(gCpuReport.Check[CPU_CHK_AVX].Detail,
                sizeof(gCpuReport.Check[CPU_CHK_AVX].Detail), "处理器不支持 AVX，已跳过");
    return;
  }
  /* OSXSAVE / XCR0 tell whether the firmware gave us AVX state to save; a
     server CPU that supports AVX still reports "no AVX" if we look at the
     wrong bit, which is misleading. */
  if (!(ecx & (1u << 27)) || (CpuXgetbv() & 0x6) != 0x6) {
    gCpuReport.Check[CPU_CHK_AVX].Level = 4;
    UiStrPrintf(gCpuReport.Check[CPU_CHK_AVX].Detail,
                sizeof(gCpuReport.Check[CPU_CHK_AVX].Detail), "固件未启用 AVX 状态保存，已跳过");
    return;
  }
  {
    BOOLEAN ok = FALSE;
    CheckAvxBody(&ok, detail, sizeof(detail));
    if (!ok) {
      CpuFail(CPU_CHK_AVX, detail);
      return;
    }
  }
  gCpuReport.Check[CPU_CHK_AVX].Level = 1;
  UiStrPrintf(gCpuReport.Check[CPU_CHK_AVX].Detail,
              sizeof(gCpuReport.Check[CPU_CHK_AVX].Detail), "256 位 YMM 通道通过");
}

static VOID CheckRand(VOID)
{
  UINT32 ecx = gApp.Sys.FeaturesEcx;
  UINT64 v1 = 0, v2 = 0;
  UINT8 ok1 = 0, ok2 = 0;

  if (!(ecx & (1u << 30))) {
    gCpuReport.Check[CPU_CHK_RAND].Level = 4;
    UiStrPrintf(gCpuReport.Check[CPU_CHK_RAND].Detail,
                sizeof(gCpuReport.Check[CPU_CHK_RAND].Detail), "无 RDRAND 指令，已跳过");
    return;
  }
  __asm__ __volatile__("rdrand %0; setc %1" : "=r"(v1), "=m"(ok1));
  __asm__ __volatile__("rdrand %0; setc %1" : "=r"(v2), "=m"(ok2));
  if (!ok1 || !ok2) {
    CpuFail(CPU_CHK_RAND, "RDRAND 未返回有效随机数");
    return;
  }
  gSink ^= v1 ^ v2;
  gCpuReport.Check[CPU_CHK_RAND].Level = 1;
  UiStrPrintf(gCpuReport.Check[CPU_CHK_RAND].Detail,
              sizeof(gCpuReport.Check[CPU_CHK_RAND].Detail), "两次取样成功");
}

static VOID CheckCache(VOID)
{
  UINT32 size = 8u * 1024 * 1024;
  UINT8 *a = 0;
  UINT8 *b = 0;
  UINT32 line = gApp.Sys.CacheLineBytes ? gApp.Sys.CacheLineBytes : 64;
  UINT64 t0, t1, ticks;
  UINT32 i;
  UINT32 rounds = 6;

  if (EFI_ERROR(gApp.BS->AllocatePool(EfiBootServicesData, size, (VOID **)&a)) ||
      EFI_ERROR(gApp.BS->AllocatePool(EfiBootServicesData, size, (VOID **)&b))) {
    if (a) {
      gApp.BS->FreePool(a);
    }
    gCpuReport.Check[CPU_CHK_CACHE].Level = 2;
    UiStrPrintf(gCpuReport.Check[CPU_CHK_CACHE].Detail,
                sizeof(gCpuReport.Check[CPU_CHK_CACHE].Detail), "缓冲区分配失败");
    return;
  }
  for (i = 0; i < size; i += line) {
    a[i] = (UINT8)(i >> 6);
  }
  t0 = CpuRdtsc();
  for (i = 0; i < rounds; i++) {
    UINT32 off;
    for (off = 0; off < size; off += line) {
      *(UINT64 *)(b + off) = *(UINT64 *)(a + off);
    }
  }
  t1 = CpuRdtsc();
  ticks = t1 - t0;
  gApp.BS->FreePool(a);
  gApp.BS->FreePool(b);
  {
    UINT64 hz = SysTscHz();
    UINT64 bytes = (UINT64)size * rounds * 2;
    if (hz && ticks) {
      UINT64 mbs = bytes * hz / ticks / (1024 * 1024);
      gCpuReport.MemMbs = (UINT32)mbs;
    }
  }
  if (gCpuReport.MemMbs == 0) {
    CpuFail(CPU_CHK_CACHE, "无法测算缓存带宽");
    return;
  }
  gCpuReport.Check[CPU_CHK_CACHE].Level = 1;
  UiStrPrintf(gCpuReport.Check[CPU_CHK_CACHE].Detail,
              sizeof(gCpuReport.Check[CPU_CHK_CACHE].Detail), "顺序复制 %u MB/s",
              gCpuReport.MemMbs);
}

static VOID CheckTsc(VOID)
{
  UINT64 hz = SysTscHz();
  UINT64 t0 = CpuRdtsc();

  gApp.BS->Stall(20000);   /* 20 ms */
  {
    UINT64 t1 = CpuRdtsc();
    UINT64 expect = hz / 50;
    UINT64 got = t1 - t0;
    UINT64 lo = expect - expect / 10;
    UINT64 hi = expect + expect / 10;

    gCpuReport.TscMhz = (UINT32)(hz / 1000000);
    if (hz == 0 || got < lo || got > hi) {
      gCpuReport.Check[CPU_CHK_TSC].Level = 2;
      UiStrPrintf(gCpuReport.Check[CPU_CHK_TSC].Detail,
                  sizeof(gCpuReport.Check[CPU_CHK_TSC].Detail),
                  "20 ms 实测 %lu kHz", (unsigned long)(got / 20));
      return;
    }
  }
  gCpuReport.Check[CPU_CHK_TSC].Level = 1;
  UiStrPrintf(gCpuReport.Check[CPU_CHK_TSC].Detail,
              sizeof(gCpuReport.Check[CPU_CHK_TSC].Detail), "%u MHz，20 ms 偏差 < 10%%",
              gCpuReport.TscMhz);
}

/* -------------------------------------------------------------- throughput */
static UINT32 MeasureIntMops(UINT32 budgetMs, BOOLEAN (*tick)(UINT32, const char *))
{
  UINT64 t0 = CpuRdtsc();
  UINT64 hz = SysTscHz();
  UINT64 limit = (hz / 1000) * budgetMs;
  UINT64 x = 1;
  UINT64 ops = 0;

  while ((CpuRdtsc() - t0) < limit) {
    UINT32 i;
    for (i = 0; i < 16384; i++) {
      x = x + (x >> 3) + 0x9E3779B9ull;
      ops += 3;
    }
    if (tick && !tick(0, 0)) {
      break;
    }
  }
  gSink ^= x;
  {
    UINT64 ticks = CpuRdtsc() - t0;
    if (!ticks) {
      return 0;
    }
    return (UINT32)((ops * hz + ticks / 2) / ticks / 1000000);
  }
}

static UINT32 MeasureFloatMflops(UINT32 budgetMs, BOOLEAN (*tick)(UINT32, const char *))
{
  UINT64 t0 = CpuRdtsc();
  UINT64 hz = SysTscHz();
  UINT64 limit = (hz / 1000) * budgetMs;
  volatile double d = 1.0000001;
  UINT64 ops = 0;

  while ((CpuRdtsc() - t0) < limit) {
    UINT32 i;
    for (i = 0; i < 16384; i++) {
      d = d * 1.0000001 + 1e-9;
      d = d / 1.0000001;
      ops += 2;
    }
    if (tick && !tick(0, 0)) {
      break;
    }
  }
  gSink ^= (UINT64)d;
  {
    UINT64 ticks = CpuRdtsc() - t0;
    if (!ticks) {
      return 0;
    }
    return (UINT32)((ops * hz + ticks / 2) / ticks / 1000000);
  }
}

EFI_STATUS CpuSelfTest(UINT32 mask, BOOLEAN (*tick)(UINT32 permille, const char *label))
{
  UINT64 started = UiNowMs();
  UINT32 done = 0;
  UINT32 total = 0;
  UINT32 i;

  CpuReportReset();
  gCpuReport.Ran = TRUE;
  for (i = 0; i < CPU_CHECK_COUNT; i++) {
    if (mask & CPU_CHK_MASK(i)) {
      total++;
    }
  }
  if (total == 0) {
    return EFI_INVALID_PARAMETER;
  }
  for (i = 0; i < CPU_CHECK_COUNT; i++) {
    if (!(mask & CPU_CHK_MASK(i))) {
      gCpuReport.Check[i].Level = 0;
      UiStrPrintf(gCpuReport.Check[i].Detail, sizeof(gCpuReport.Check[i].Detail), "未选择");
      continue;
    }
    if (tick && !tick(done * 1000 / total, "自检")) {
      return EFI_ABORTED;
    }
    switch (i) {
    case CPU_CHK_ALU:    CheckAlu();   break;
    case CPU_CHK_BITOPS: CheckBitOps(); break;
    case CPU_CHK_SSE2:   CheckSse2();  break;
    case CPU_CHK_AVX:    CheckAvx();   break;
    case CPU_CHK_RAND:   CheckRand();  break;
    case CPU_CHK_CACHE:  CheckCache(); break;
    case CPU_CHK_TSC:    CheckTsc();   break;
    default: break;
    }
    done++;
  }
  if (mask & CPU_CHK_MASK(CPU_CHK_ALU)) {
    gCpuReport.IntMops = MeasureIntMops(250, tick);
  }
  if (mask & CPU_CHK_MASK(CPU_CHK_SSE2)) {
    gCpuReport.FloatMflops = MeasureFloatMflops(250, tick);
  }
  gCpuReport.ElapsedMs = (UINT32)(UiNowMs() - started);
  if (tick) {
    tick(1000, "完成");
  }
  return EFI_SUCCESS;
}
