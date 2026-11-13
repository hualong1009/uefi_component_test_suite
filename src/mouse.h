/* mouse.h - pointer support: one cursor, absolute (tablet) or relative (mouse).

   Firmware exposes two shapes of the same idea: an absolute pointer reports the
   position inside the device's own coordinate space (a KVM or a tablet maps the
   whole screen onto it, which is what remote consoles usually provide), while a
   relative pointer reports deltas that have to be scaled by the device
   resolution. Both are polled, never event driven, so the UI stays a simple
   draw-then-poll loop. */
#ifndef UCTS_MOUSE_H
#define UCTS_MOUSE_H

#include "uefi.h"
#include "gfx.h"

#define MOUSE_LEFT   0x01
#define MOUSE_RIGHT  0x02

VOID    MouseInit(VOID);
BOOLEAN MousePresent(VOID);

/* Polls the device; returns TRUE when the pointer moved or a button changed. */
BOOLEAN MousePoll(VOID);

UINT32  MouseX(VOID);
UINT32  MouseY(VOID);
UINT32  MouseButtons(VOID);

/* Consumes a pending left-button press / double click / wheel step. */
BOOLEAN MouseTakeClick(UINT32 *x, UINT32 *y, BOOLEAN *isDouble);
INT32   MouseTakeWheel(VOID);

/* Cursor: hide before a full redraw, show after the frame is present. */
VOID    MouseHide(VOID);
/* Drops the cursor without restoring pixels (a full redraw replaces it). */
VOID    MouseForget(VOID);
VOID    MouseShow(BOOLEAN hot);

/* Live counters, shown on the about page so a support call can tell "wrong
   protocol" from "no protocol at all" without a serial cable. */
VOID    MouseStats(BOOLEAN *absFound, BOOLEAN *relFound, UINT32 *moves,
                   UINT32 *clicks, UINT32 *hits);

#endif
