/* results.h - aggregated test results shared by the pages and the report writer. */
#ifndef UCTS_RESULTS_H
#define UCTS_RESULTS_H

#include "uefi.h"

typedef struct {
  /* memory test */
  BOOLEAN MemTested;
  BOOLEAN MemRunning;
  BOOLEAN MemStopped;
  UINT32  MemErrorCount;
  UINT32  MemPatternCount;
  UINT32  MemPatternsRun;
  UINT64  MemTestedBytes;
  UINT64  MemTotalBytes;
  UINT32  MemCoveragePm;      /* per mille of testable memory */
  UINT32  MemBandwidthMbs;
  UINT32  MemElapsedMs;
  UINT32  MemFirstErrorOffsetValid;
  UINT64  MemFirstErrorOffset;
  UINT64  MemFirstErrorExpected;
  UINT64  MemFirstErrorActual;
  UINT32  MemDimmErrors[16];
  /* per module per DRAM device error counts, derived from the failing data bit */
  UINT16  MemDeviceErrors[16][32];
  UINT32  MemDeviceMapped;      /* number of errors that were placed on a device */
  UINT32  MemIterations;

  /* disk */
  BOOLEAN DiskTested;
  UINT32  DiskErrorCount;
  UINT32  DiskScannedDevices;
  UINT64  DiskScannedBytes;
  UINT32  DiskMinMbs;
  UINT32  DiskMaxMbs;
  UINT32  DiskAvgMbs;
  UINT32  DiskHealthLevel;    /* 0 unknown, 1 ok, 2 warn, 3 crit */

  /* cpu */
  BOOLEAN CpuTested;
  UINT32  CpuErrorCount;
  UINT32  CpuElapsedMs;
  UINT64  CpuMops;

  /* erase */
  UINT32  EraseCount;
  char    EraseLastTarget[64];

  /* stress */
  BOOLEAN StressRan;
  BOOLEAN StressAborted;
  UINT32  StressSeconds;
  UINT32  StressErrorCount;
  UINT64  StressCpuLoops;
  UINT64  StressMemBytes;
  UINT64  StressDiskBytes;
  UINT32  StressCpuThreads;

  /* per component health: 0 unknown, 1 ok, 2 warn, 3 critical */
  UINT32  HealthMem;
  UINT32  HealthDisk;
  UINT32  HealthCpu;
  UINT32  HealthFw;
} TEST_RESULTS;

extern TEST_RESULTS gResults;

VOID ResultsReset(VOID);

#endif
