/* stress.h - sustained load test across CPU cores, DIMM slots and disks. */
#ifndef UCTS_STRESS_H
#define UCTS_STRESS_H

#include "uefi.h"
#include "results.h"

/* two 96 core / 192 thread sockets fit, so the former 64 does not: the APs are
   addressed one by one through MP services, the selection mask needs its own
   words because a UINTN only holds 64 processors */
#define STRESS_MAX_CPU     256
#define STRESS_CPU_WORDS   (STRESS_MAX_CPU / 64)
#define STRESS_MAX_DIMM    32
#define STRESS_MAX_DISK    16
#define STRESS_ERR_LOG     8
#define STRESS_ERR_LEN     88

/* how the working set for the memory component is chosen */
enum {
  STRESS_MEM_DIMM = 0
};

typedef struct {
  BOOLEAN CpuOn;
  BOOLEAN MemOn;
  BOOLEAN DiskOn;
  UINT64  CpuMask[STRESS_CPU_WORDS];      /* logical processor bit mask     */
  UINT32  DimmMask;                       /* present DIMM bit mask          */
  UINT32  DiskMask;                       /* disk index bit mask            */
  UINT32  Seconds;                        /* requested duration             */
  UINT32  MemBytesPerDimm;                /* 0 = use the whole testable ram */
} STRESS_CONFIG;

typedef struct {
  /* cpu */
  UINT32   CpuThreads;
  UINT64   CpuLoops;
  UINT32   CpuErrors;
  UINT8    CpuAlive[STRESS_MAX_CPU];      /* 1 while that AP is hammering   */
  UINT64   CpuLoopsPerCpu[STRESS_MAX_CPU];
  /* memory */
  UINT64   MemBytes;
  UINT32   MemMbs;
  UINT32   MemErrors;
  /* disk */
  UINT64   DiskBytes;
  UINT32   DiskMbs;
  UINT32   DiskErrors;
  /* overall */
  UINT8    Aborted;
  UINT32   ElapsedMs;
  UINT32   RemainingMs;
  char     LastError[STRESS_ERR_LEN];
  char     Errors[STRESS_ERR_LOG][STRESS_ERR_LEN];
  UINT32   ErrorCount;                    /* total, may exceed the log      */
} STRESS_STATE;

extern STRESS_STATE gStress;

static inline BOOLEAN StressCpuSelected(const UINT64 *mask, UINT32 cpu)
{
  if (cpu >= STRESS_MAX_CPU) {
    return FALSE;
  }
  return (mask[cpu >> 6] >> (cpu & 63)) & 1 ? TRUE : FALSE;
}

static inline VOID StressCpuSetMask(UINT64 *mask, UINT32 cpu, BOOLEAN on)
{
  if (cpu >= STRESS_MAX_CPU) {
    return;
  }
  if (on) {
    mask[cpu >> 6] |= 1ull << (cpu & 63);
  } else {
    mask[cpu >> 6] &= ~(1ull << (cpu & 63));
  }
}

/* returns FALSE when the user asked to abort */
typedef BOOLEAN (*STRESS_TICK)(STRESS_STATE *st);

EFI_STATUS StressRun(const STRESS_CONFIG *cfg, STRESS_TICK tick);
VOID       StressStop(VOID);
UINT32     StressCpuCount(VOID);          /* enabled logical processors      */
UINT32     StressDimmCount(VOID);         /* present DIMM slots              */
VOID       StressLogError(const char *who, const char *what, UINT64 detail);

#endif
