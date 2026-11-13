/* i18n.h - UI string translation (English default, Chinese optional).

   The sources are written in Chinese; every string that reaches the user is
   passed through L() which looks the text up in the generated dictionary and
   returns the English form when the active language is English. Strings that
   are not in the dictionary (file names, register names, "OK", ...) pass
   through untouched.
*/
#ifndef UCTS_I18N_H
#define UCTS_I18N_H

#include "uefi.h"

#define LANG_EN 0
#define LANG_ZH 1

/* Builds the lookup index and restores the saved choice from NVRAM. */
VOID IInit(EFI_SYSTEM_TABLE *ST);

/* Translates one string; safe for NULL and for non-dictionary text. */
const char *L(const char *zh);

UINT32      ILang(VOID);
VOID        ISetLang(UINT32 lang);   /* also persists the choice */
VOID        IToggle(VOID);
const char *ILangTag(VOID);          /* "EN" / "中文" */
const char *ILangName(VOID);         /* "English" / "中文" */

#endif
