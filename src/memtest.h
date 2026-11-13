/* memtest.h - memory pattern engine: write and read back patterns. */
#ifndef UCTS_MEMTEST_H
#define UCTS_MEMTEST_H

#include "uefi.h"
#include "results.h"

#define MEM_MAX_PATTERNS 20

typedef struct {
  const char *Name;
  const char *Detail;
  UINT8   Kind;
  UINT64  Value;
  BOOLEAN Enabled;
} MEM_PATTERN;

enum {
  MEM_KIND_FIXED = 0,   /* constant 64-bit word                       */
  MEM_KIND_SPLIT,       /* alternating word / complement in adjacent  */
  MEM_KIND_ADDR,        /* physical address of the cell               */
  MEM_KIND_ADDR_XOR,    /* physical address XOR a mask                */
  MEM_KIND_LFSR,        /* pseudo random sequence                     */
  MEM_KIND_WALK1,       /* walking one inside the word                */
  MEM_KIND_WALK0        /* walking zero inside the word               */
};

/* one progress sample.  OverallPm spans every enabled pattern, PatternPm only
   the pattern being run: a run of ten patterns is 10% done when the first one
   reaches the end of the region, not 100%. */
typedef struct {
  UINT32 OverallPm;       /* 0..1000 across all enabled patterns            */
  UINT32 PatternPm;       /* 0..1000 inside the current pattern             */
  UINT32 PatternOrdinal;  /* 0 based, counts enabled patterns only          */
  UINT32 PatternCount;    /* enabled patterns in this run                   */
  UINT32 Mbs;             /* average since the run started                  */
  UINT32 ElapsedMs;
  UINT64 PatternBytes;    /* bytes done inside the current pattern          */
  UINT64 PassBytes;       /* bytes in one pass over the tested region       */
} MEM_STEP;

/* returns FALSE when the user asked to stop */
typedef BOOLEAN (*MEM_STEP_CB)(const MEM_STEP *step);

extern MEM_PATTERN gMemPatterns[];
extern const UINT32 gMemPatternCount;

UINT32     MemEnabledPatterns(VOID);
EFI_STATUS MemTestRun(MEM_STEP_CB cb, BOOLEAN wideCoverage);

#endif
