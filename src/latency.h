/* latency.h - core to core cache line hand-off latency (heat matrix).

   Every logical processor gets one worker; a pair is measured by handing a
   dedicated cache line back and forth between the two cores, so the number that
   comes out is the cache coherency cost between them and not the execution
   speed of either core. The result is a symmetric matrix ordered by package and
   core, which is what makes multi socket machines readable: the matrix splits
   into package blocks, diagonal blocks are local traffic and off-diagonal
   blocks cross the inter-socket link. */
#ifndef UCTS_LATENCY_H
#define UCTS_LATENCY_H

#include "uefi.h"

#define LAT_MAX_CPU    256
#define LAT_MAX_SOCKET 8

typedef struct {
  UINT32  ProcId;         /* processor number used by MP services          */
  UINT8   Socket;         /* EFI_CPU_PHYSICAL_LOCATION.Package             */
  UINT8   Core;           /* EFI_CPU_PHYSICAL_LOCATION.Core                */
  UINT8   Thread;         /* EFI_CPU_PHYSICAL_LOCATION.Thread              */
  BOOLEAN Used;
} LAT_CPU;

typedef struct {
  BOOLEAN Ran;
  BOOLEAN Aborted;
  BOOLEAN TopologyKnown;  /* firmware filled in the physical location      */
  UINT32  Count;          /* matrix dimension: logical processors measured */
  UINT32  SocketCount;
  UINT32  CoreCount;      /* distinct socket/core pairs                    */
  UINT32  Pairs;          /* pairs actually measured                       */
  UINT32  PairsTotal;
  UINT32  ElapsedMs;
  UINT32  Failed;
  UINT16  Ns10[LAT_MAX_CPU][LAT_MAX_CPU];   /* one way latency, 0.1 ns  */
  UINT32  MinNs10, MaxNs10, MedianNs10;
  UINT32  IntraNs10, IntraCount;            /* same package, other core   */
  UINT32  CrossNs10, CrossCount;            /* package to package         */
  UINT32  SiblingNs10, SiblingCount;        /* the two threads of a core  */
  UINT32  SockNs10[LAT_MAX_SOCKET][LAT_MAX_SOCKET];
  UINT32  SockCount[LAT_MAX_SOCKET][LAT_MAX_SOCKET];
  UINT32  SockMinNs10[LAT_MAX_SOCKET][LAT_MAX_SOCKET];
  UINT32  SockMaxNs10[LAT_MAX_SOCKET][LAT_MAX_SOCKET];
} LAT_REPORT;

extern LAT_REPORT gLat;
extern LAT_CPU    gLatCpus[LAT_MAX_CPU];

VOID   LatReset(VOID);
/* collects the processor topology without measuring anything, so the page can
   show the matrix size before the first run */
EFI_STATUS LatPrepare(VOID);
/* 0.1 ns between two matrix entries, 0 when not measured */
UINT32 LatNs10(UINT32 a, UINT32 b);
const char *LatCpuLabel(UINT32 index, char *buf, UINTN cap);

/* Measures every pair. tick(done, total) is called while the matrix fills and
   may return FALSE to abort. */
EFI_STATUS LatRun(BOOLEAN (*tick)(UINT32 done, UINT32 total));

#endif
