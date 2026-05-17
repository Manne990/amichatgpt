#!/usr/bin/env bash
set -euo pipefail

export PATH="/opt/amiga/bin:/tools/bin:/tools/usr/bin:${PATH}"

if ! command -v m68k-amigaos-gcc >/dev/null 2>&1; then
  echo "m68k-amigaos-gcc was not found in PATH."
  echo "PATH=${PATH}"
  echo "Known Amiga GCC candidates:"
  find /opt /tools /usr/local -type f -name 'm68k-amigaos-gcc*' 2>/dev/null | sort || true
  exit 127
fi

m68k-amigaos-gcc --version
make clean package XDFTOOL=
