/* debug.h - COM1 trace helpers (enabled by setting UCTS_DEBUG). */
#ifndef UCTS_DEBUG_H
#define UCTS_DEBUG_H

#include "uefi.h"

/* kept as a macro so a trace line does not need a formatted escape */
#define DBG_NL "\n"

#ifdef UCTS_DEBUG
VOID DbgStr(const char *s);
VOID DbgHex(UINT64 value);
VOID DbgDec(UINT64 value);
#else
#define DbgStr(s)        ((VOID)0)
#define DbgHex(v)        ((VOID)0)
#define DbgDec(v)        ((VOID)0)
#endif

#endif
