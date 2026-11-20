/* pages.h - page entry points. */
#ifndef UCTS_PAGES_H
#define UCTS_PAGES_H

#include "ui.h"

/* page indexes; keep in sync with the gPages table in pages.c */
enum {
  PAGE_HOME = 0,
  PAGE_SYSINFO,
  PAGE_TOPO,
  PAGE_MEM,
  PAGE_DIMM,
  PAGE_DISK,
  PAGE_ERASE,
  PAGE_CPU,
  PAGE_LATENCY,
  PAGE_BW,
  PAGE_STRESS,
  PAGE_TOOLS,
  PAGE_ABOUT,
  PAGE_COUNT
};

VOID    PageHomeEnter(VOID);
VOID    PageHomeDraw(GFX_RECT content);
BOOLEAN PageHomeKey(EFI_INPUT_KEY key);
VOID    PageHomePoint(UINT32 row);

VOID    PageLatencyEnter(VOID);
VOID    PageLatencyDraw(GFX_RECT content);
BOOLEAN PageLatencyKey(EFI_INPUT_KEY key);

VOID    PageBwEnter(VOID);
VOID    PageBwDraw(GFX_RECT content);
BOOLEAN PageBwKey(EFI_INPUT_KEY key);

VOID    PageTopoEnter(VOID);
VOID    PageTopoDraw(GFX_RECT content);
BOOLEAN PageTopoKey(EFI_INPUT_KEY key);
VOID    PageTopoPoint(UINT32 row);

VOID    PageSysInfoEnter(VOID);
VOID    PageSysInfoDraw(GFX_RECT content);
BOOLEAN PageSysInfoKey(EFI_INPUT_KEY key);
VOID    PageSysInfoPoint(UINT32 row);

VOID    PageMemEnter(VOID);
VOID    PageMemLeave(VOID);
VOID    PageMemDraw(GFX_RECT content);
BOOLEAN PageMemKey(EFI_INPUT_KEY key);
VOID    PageMemPoint(UINT32 row);

VOID    PageDimmEnter(VOID);
VOID    PageDimmDraw(GFX_RECT content);
BOOLEAN PageDimmKey(EFI_INPUT_KEY key);
VOID    PageDimmPoint(UINT32 row);
/* opens the topology page with a given DIMM selected */
VOID    PageDimmFocus(UINT32 dimmIndex);

VOID    PageDiskEnter(VOID);
VOID    PageDiskDraw(GFX_RECT content);
BOOLEAN PageDiskKey(EFI_INPUT_KEY key);
VOID    PageDiskPoint(UINT32 row);

/* opens the storage page with a given block device selected */
VOID    PageDiskFocus(UINT32 diskIndex);

VOID    PageEraseEnter(VOID);
VOID    PageEraseDraw(GFX_RECT content);
BOOLEAN PageEraseKey(EFI_INPUT_KEY key);
VOID    PageErasePoint(UINT32 row);

VOID    PageCpuEnter(VOID);
VOID    PageCpuDraw(GFX_RECT content);
BOOLEAN PageCpuKey(EFI_INPUT_KEY key);
VOID    PageCpuPoint(UINT32 row);

VOID    PageStressEnter(VOID);
VOID    PageStressDraw(GFX_RECT content);
BOOLEAN PageStressKey(EFI_INPUT_KEY key);
VOID    PageStressPoint(UINT32 row);

VOID    PageToolsEnter(VOID);
VOID    PageToolsDraw(GFX_RECT content);
BOOLEAN PageToolsKey(EFI_INPUT_KEY key);
VOID    PageToolsPoint(UINT32 row);

VOID    PageAboutEnter(VOID);
VOID    PageAboutDraw(GFX_RECT content);
BOOLEAN PageAboutKey(EFI_INPUT_KEY key);
VOID    PageAboutPoint(UINT32 row);

/* shared helpers used by several pages */
VOID    UiNavigationJump(UINT32 page);

#endif
