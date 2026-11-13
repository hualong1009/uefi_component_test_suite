#!/usr/bin/env bash
# build.sh - compile UCTS into BOOTX64.EFI (and optionally a bootable ISO).
#
# Works both on Windows (Git Bash + llvm-mingw) and on Linux (clang/lld from
# the distro), which is what the release workflow uses:
#
#   SKIP_FONT=1 MKISO=1 OUT=build bash tools/build.sh

set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# Windows keeps the toolchain in a local directory, Linux finds it on PATH.
TOOLBIN="${TOOLBIN:-/d/OneDrive/software/llvm-mingw-20251104-ucrt-x86_64/llvm-mingw-20251104-ucrt-x86_64/bin}"
if [ ! -x "$TOOLBIN/clang" ]; then
  TOOLBIN=""
fi
CC="${CC:-${TOOLBIN:+$TOOLBIN/}clang}"
OUT="${OUT:-$ROOT/build}"
PYTHON="${PYTHON:-python3}"
command -v "$PYTHON" >/dev/null 2>&1 || PYTHON=python

# ld.lld needs "-flavor link", the standalone lld-link driver does not.
if [ -z "${LD:-}" ]; then
  for cand in "${TOOLBIN:+$TOOLBIN/}lld-link" "${TOOLBIN:+$TOOLBIN/}ld.lld" lld-link ld.lld; do
    if command -v "$cand" >/dev/null 2>&1; then
      LD="$cand"
      break
    fi
  done
fi
# Windows ships ld.lld.exe; match both spellings when deciding the flavour.
case "$(basename "${LD:-ld.lld}" .exe)" in
  lld-link) LINK_FLAVOR="" ;;
  *)        LINK_FLAVOR="-flavor link" ;;
esac

CFLAGS=(
  --target=x86_64-w64-windows-gnu
  -std=c11 -O2 -g
  -ffreestanding -fno-builtin -fno-stack-protector -fno-stack-check
  -fshort-wchar -mno-red-zone -mno-stack-arg-probe
  -fno-asynchronous-unwind-tables -fno-unwind-tables
  -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
  -Iinclude -Isrc
  ${EXTRA_CFLAGS}
)

mkdir -p "$OUT/obj"
if command -v cygpath >/dev/null 2>&1; then
  OBJDIR="$(cygpath -m "$OUT")/obj"
else
  OBJDIR="$(cd "$OUT" && pwd)/obj"
fi

if [ "${SKIP_FONT:-0}" != "1" ]; then
  "$PYTHON" tools/genfont.py
fi
"$PYTHON" tools/geni18n.py

OBJS=()
for src in src/*.c src/generated/font_data.c src/generated/i18n_data.c; do
  name="$(basename "${src%.c}")"
  obj="$OBJDIR/$name.o"
  echo "  CC  $src"
  "$CC" "${CFLAGS[@]}" -c "$src" -o "$obj"
  OBJS+=("$obj")
done

echo "  LD  $OUT/BOOTX64.EFI ($LD)"
MSYS_NO_PATHCONV=1 "$LD" $LINK_FLAVOR \
  /entry:efi_main /subsystem:EFI_APPLICATION /nodefaultlib \
  /out:"$OBJDIR/../BOOTX64.EFI" "${OBJS[@]}"

ls -l "$OUT/BOOTX64.EFI"

if [ "${MKISO:-0}" = "1" ]; then
  echo "  ISO $OUT/UCTS.iso"
  "$PYTHON" tools/mkiso.py --efi "$OUT/BOOTX64.EFI" --out "$OUT/UCTS.iso"
fi
