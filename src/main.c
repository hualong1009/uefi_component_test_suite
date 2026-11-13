/* main.c - UEFI entry point. */
#include "ui.h"
#include "pages.h"
#include "results.h"
#include "debug.h"

TEST_RESULTS gResults;

VOID ResultsReset(VOID)
{
  gApp.BS->SetMem(&gResults, sizeof(gResults), 0);
}

static VOID ConsolePrint(EFI_SYSTEM_TABLE *ST, const char *msg)
{
  CHAR16 buf[160];
  UINTN i = 0;
  while (msg[i] && i + 1 < sizeof(buf) / sizeof(buf[0])) {
    buf[i] = (CHAR16)(UINT8)msg[i];
    i++;
  }
  buf[i] = 0;
  ST->ConOut->OutputString(ST->ConOut, buf);
}

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *ST)
{
  EFI_STATUS status;

  ST->BootServices->SetWatchdogTimer(0, 0, 0, 0);
  DbgStr("\nUCTS: efi_main entry\n");
  status = UiInit(ImageHandle, ST);
  if (EFI_ERROR(status)) {
    ST->ConOut->ClearScreen(ST->ConOut);
    ConsolePrint(ST, "UCTS: graphics output protocol is not available.\r\n");
    ConsolePrint(ST, "UCTS: this application needs a GOP capable firmware (1024x768 or better).\r\n");
    return status;
  }
  DbgStr("UCTS: UiInit done cpus=");
  DbgDec(gApp.Sys.LogicalCpus);
  DbgStr(" dimms=");
  DbgDec(gApp.Sys.DimmCount);
  DbgStr(" ");
  ResultsReset();
  UiRun();
  GfxShutdown();
  ST->ConOut->Reset(ST->ConOut, FALSE);
  ST->ConOut->ClearScreen(ST->ConOut);
  return EFI_SUCCESS;
}
