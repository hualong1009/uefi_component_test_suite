/* bw.h - memory bandwidth heat matrix.

   Two questions are answered here: how much bandwidth the machine can pull in
   total when every core streams memory, and how much a single core gets from
   the memory attached to a given socket. The measurement walks one core at a
   time (every other core parked) and streams a buffer that is larger than the
   last level cache, so what the cell shows is real memory traffic and not an
   LLC hit. Buffers are placed inside the physical ranges that ACPI SRAT
   attributes to each proximity domain, which is what makes the local/remote
   split meaningful. */
#ifndef UCTS_BW_H
#define UCTS_BW_H

#include "uefi.h"

#define BW_MAX_CPU 256
#define BW_MAX_DOM 8

typedef struct {
  UINT32 ProcId;          /* processor number used by MP services          */
  UINT8  Socket;
  UINT8  Core;
  UINT8  Thread;
  UINT8  Dom;             /* memory domain this processor belongs to       */
} BW_CPU;

/* bandwidth values are stored in tenths of a GB/s, 0 means "not measured" */
typedef struct {
  BOOLEAN Ran;
  BOOLEAN Aborted;
  BOOLEAN SratPresent;    /* ACPI SRAT gave us domains and memory ranges   */
  BOOLEAN PlacementKnown; /* buffers really sit in the domain they claim   */
  BOOLEAN SharedBuffer;   /* one buffer served every domain (no SRAT)      */
  UINT32  Count;          /* logical processors measured                   */
  UINT32  SocketCount;
  UINT32  CoreCount;
  UINT32  DomCount;
  UINT32  Cells;
  UINT32  CellsTotal;
  UINT32  ElapsedMs;
  UINT32  Bytes;          /* MiB streamed per single core sample           */
  UINT32  Mbs[BW_MAX_CPU][BW_MAX_DOM];  /* core -> domain, 0.1 GB/s      */
  UINT32  MinMbs, MaxMbs, MedianMbs;
  UINT32  LocalMbs, LocalCount;         /* core reads its own domain     */
  UINT32  RemoteMbs, RemoteCount;
  UINT32  PeakMbs;                      /* best single core cell         */
  UINT32  TotalMbs;                     /* every core at once, local mem */
  UINT32  DomAggMbs[BW_MAX_DOM];        /* every core at once, one domain*/
} BW_REPORT;

extern BW_REPORT gBw;
extern BW_CPU    gBwCpus[BW_MAX_CPU];

VOID BwReset(VOID);
/* collects topology and places one buffer per memory domain, without
   streaming anything, so the page can show the matrix size first */
EFI_STATUS BwPrepare(VOID);
/* 0.1 GB/s from a core to a memory domain, 0 when not measured */
UINT32 BwCell(UINT32 index, UINT32 dom);
/* memory domain of a matrix index */
UINT32 BwDomOf(UINT32 index);

/* Measures every core/domain cell plus the aggregate runs. tick(done, total)
   is called while the matrix fills and may return FALSE to abort. */
EFI_STATUS BwRun(BOOLEAN (*tick)(UINT32 done, UINT32 total));

#endif
