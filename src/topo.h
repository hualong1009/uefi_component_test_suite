/* topo.h - the CPU / memory / PCIe connection diagram shared by the overview
   card and the dedicated topology page. */
#ifndef UCTS_TOPO_H
#define UCTS_TOPO_H

#include "ui.h"

/* Rebuilds the flattened tree (call after PciEnumerate / DiskEnumerate). */
VOID   TopoRefresh(VOID);

/* Root ports / bridges with nothing behind them are dead wiring on a healthy
   machine, so they are hidden by default; H on the topology page toggles it. */
VOID    TopoSetHideEmpty(BOOLEAN on);
BOOLEAN TopoHideEmpty(VOID);
UINT32  TopoHiddenCount(VOID);
/* number of devices sitting below a node */
UINT32  TopoChildCount(UINT32 pciIndex);
UINT32 TopoRowCount(VOID);
UINT32 TopoRowDevice(UINT32 row);
INT32  TopoFindRow(UINT32 pciIndex);

/* Draws the diagram inside r: the processor block, the memory block and the
   PCIe tree rows. Rows outside [first, first + maxRows) are clipped, the
   caller owns the scroll offset and the selection. */
UINT32 TopoDraw(GFX_RECT r, UINT32 firstRow, UINT32 selRow, BOOLEAN mini);

/* height of one tree row for the given mode */
UINT32 TopoRowHeight(BOOLEAN mini);

#endif
