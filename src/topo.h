/* topo.h - the CPU / memory / PCIe connection diagram shared by the overview
   card and the dedicated topology page. */
#ifndef UCTS_TOPO_H
#define UCTS_TOPO_H

#include "ui.h"

/* Rebuilds the flattened tree (call after PciEnumerate / DiskEnumerate). */
VOID   TopoRefresh(VOID);
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
