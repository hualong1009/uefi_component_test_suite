/* cputest.h - processor instruction self-test and throughput measurement. */
#ifndef UCTS_CPUTEST_H
#define UCTS_CPUTEST_H

#include "uefi.h"

#define CPU_CHECK_COUNT 7

/* Check identifiers, in display order. */
#define CPU_CHK_ALU      0
#define CPU_CHK_BITOPS   1
#define CPU_CHK_SSE2     2
#define CPU_CHK_AVX      3
#define CPU_CHK_RAND     4
#define CPU_CHK_CACHE    5
#define CPU_CHK_TSC      6

typedef struct {
  UINT32 Level;        /* 0 not run, 1 pass, 2 warn, 3 fail, 4 skipped */
  char   Detail[64];
} CPU_CHECK;

typedef struct {
  CPU_CHECK Check[CPU_CHECK_COUNT];
  UINT32    Errors;
  UINT32    ElapsedMs;
  UINT32    IntMops;      /* dependent integer adds, million ops/s */
  UINT32    FloatMflops;  /* scalar double multiplies, million flops/s */
  UINT32    MemMbs;       /* buffer copy bandwidth, MB/s */
  UINT32    TscMhz;
  BOOLEAN   Ran;
} CPU_REPORT;

extern CPU_REPORT gCpuReport;

/* Runs the selected checks; mask bit i enables CPU_CHK_*. Returns EFI_SUCCESS when
   the run completed (individual check failures are recorded, not returned). */
EFI_STATUS CpuSelfTest(UINT32 mask, BOOLEAN (*tick)(UINT32 permille, const char *label));

VOID CpuReportReset(VOID);

#endif
