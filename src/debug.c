/* debug.c - minimal COM1 trace output for bring-up diagnostics. */
#define UCTS_DEBUG 1
#include "debug.h"

#define COM1 0x3F8

static inline void OutB(UINT16 port, UINT8 value)
{
  __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline UINT8 InB(UINT16 port)
{
  UINT8 v;
  __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
  return v;
}

static BOOLEAN gSerialReady;
static BOOLEAN gSerialBroken;

static void SerialInit(void)
{
  if (gSerialReady || gSerialBroken) {
    return;
  }
  OutB(COM1 + 1, 0x00);   /* disable interrupts */
  OutB(COM1 + 3, 0x80);   /* DLAB on */
  OutB(COM1 + 0, 0x01);   /* divisor low = 1 -> 115200 */
  OutB(COM1 + 1, 0x00);
  OutB(COM1 + 3, 0x03);   /* 8N1 */
  OutB(COM1 + 2, 0xC7);   /* FIFO on, clear, 14 byte threshold */
  OutB(COM1 + 4, 0x0B);   /* DTR, RTS, OUT2 */
  gSerialReady = TRUE;
}

VOID DbgStr(const char *s)
{
  SerialInit();
  while (s && *s) {
    UINT32 guard = 100000;
    if (*s == '\n') {
      OutB(COM1, '\r');
    }
    while (((InB(COM1 + 5) & 0x20) == 0) && guard--) {
    }
    OutB(COM1, (UINT8)*s);
    s++;
  }
}

VOID DbgHex(UINT64 value)
{
  char buf[20];
  UINTN i = 0;
  buf[i++] = '0';
  buf[i++] = 'x';
  for (INT32 shift = 60; shift >= 0; shift -= 4) {
    UINT32 digit = (UINT32)((value >> shift) & 0xF);
    buf[i++] = (char)(digit < 10 ? '0' + digit : 'a' + digit - 10);
  }
  buf[i] = 0;
  DbgStr(buf);
}

VOID DbgDec(UINT64 value)
{
  char buf[24];
  UINTN i = 0;
  if (value == 0) {
    DbgStr("0");
    return;
  }
  while (value && i + 1 < sizeof(buf)) {
    buf[i++] = (char)('0' + (value % 10));
    value /= 10;
  }
  buf[i] = 0;
  for (UINTN a = 0, b = i - 1; a < b; a++, b--) {
    char t = buf[a];
    buf[a] = buf[b];
    buf[b] = t;
  }
  DbgStr(buf);
}
