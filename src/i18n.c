/* i18n.c - dictionary lookup with a small open addressing hash index. */
#include "i18n.h"
#include "gfx.h"

#define I18N_SLOTS 4096
#define I18N_EMPTY 0xFFFF

extern const char *const gI18nZh[];
extern const char *const gI18nEn[];
extern const unsigned int gI18nCount;

static UINT32 gLang = LANG_EN;
static UINT16 gIndex[I18N_SLOTS];
static BOOLEAN gReady;
static EFI_RUNTIME_SERVICES *gRT;

EFI_GUID_DEF(gLangGuid, 0x7B1D6C24, 0x9A54, 0x4C63, 0x8E, 0x2B, 0x51, 0x0A, 0xF3, 0x8C, 0x44, 0xD9);

static UINT32 HashStr(const char *s)
{
  UINT32 h = 2166136261u;
  while (*s) {
    h ^= (UINT8)*s++;
    h *= 16777619u;
  }
  return h;
}

static VOID BuildIndex(VOID)
{
  UINT32 i;
  if (gReady) {
    return;
  }
  for (i = 0; i < I18N_SLOTS; i++) {
    gIndex[i] = I18N_EMPTY;
  }
  for (i = 0; i < gI18nCount; i++) {
    UINT32 slot = HashStr(gI18nZh[i]) & (I18N_SLOTS - 1);
    UINT32 probe;
    for (probe = 0; probe < I18N_SLOTS; probe++) {
      if (gIndex[slot] == I18N_EMPTY) {
        gIndex[slot] = (UINT16)i;
        break;
      }
      slot = (slot + 1) & (I18N_SLOTS - 1);
    }
  }
  gReady = TRUE;
}

VOID IInit(EFI_SYSTEM_TABLE *ST)
{
  UINT8 value = LANG_EN;
  UINTN size = sizeof(value);
  BuildIndex();
  if (ST == 0 || ST->RuntimeServices == 0) {
    return;
  }
  gRT = ST->RuntimeServices;
  if (!EFI_ERROR(gRT->GetVariable((CHAR16 *)L"UctsLang", (EFI_GUID *)&gLangGuid, 0, &size,
                                  &value))) {
    if (value == LANG_ZH) {
      gLang = LANG_ZH;
    }
  }
}

const char *L(const char *zh)
{
  UINT32 slot;
  UINT32 probe;

  if (zh == 0 || gLang == LANG_ZH) {
    return zh;
  }
  if (!gReady) {
    BuildIndex();
  }
  if (gI18nCount == 0) {
    return zh;
  }
  slot = HashStr(zh) & (I18N_SLOTS - 1);
  for (probe = 0; probe < 16; probe++) {
    UINT16 idx = gIndex[slot];
    if (idx == I18N_EMPTY) {
      return zh;
    }
    if (GfxStrCmp(gI18nZh[idx], zh) == 0) {
      return gI18nEn[idx];
    }
    slot = (slot + 1) & (I18N_SLOTS - 1);
  }
  return zh;
}

UINT32 ILang(VOID)
{
  return gLang;
}

VOID ISetLang(UINT32 lang)
{
  UINT8 value = (lang == LANG_ZH) ? LANG_ZH : LANG_EN;
  gLang = value;
  if (gRT && gRT->SetVariable) {
    (VOID)gRT->SetVariable((CHAR16 *)L"UctsLang", (EFI_GUID *)&gLangGuid,
                           EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS |
                               EFI_VARIABLE_NON_VOLATILE,
                           sizeof(value), &value);
  }
}

VOID IToggle(VOID)
{
  ISetLang(gLang == LANG_ZH ? LANG_EN : LANG_ZH);
}

const char *ILangTag(VOID)
{
  return gLang == LANG_ZH ? "中文" : "EN";
}

const char *ILangName(VOID)
{
  return gLang == LANG_ZH ? "中文" : "English";
}
