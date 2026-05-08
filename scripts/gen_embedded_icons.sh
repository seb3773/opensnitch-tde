#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ICONS_DIR="$ROOT_DIR/icons"
OUT_FILE="$ROOT_DIR/src/embedded_icons.h"

if ! command -v xxd >/dev/null 2>&1; then
  echo "error: xxd not found (install vim-common or xxd package)" >&2
  exit 1
fi

if [[ ! -d "$ICONS_DIR" ]]; then
  echo "error: icons directory not found: $ICONS_DIR" >&2
  exit 1
fi

TMP_FILE="${OUT_FILE}.tmp"

{
  echo "#pragma once"

  shopt -s nullglob
  mapfile -t PNGS < <(LC_ALL=C ls -1 "$ICONS_DIR"/*.png | LC_ALL=C sort)
  if [[ ${#PNGS[@]} -eq 0 ]]; then
    echo "error: no .png files found in $ICONS_DIR" >&2
    exit 1
  fi

  for f in "${PNGS[@]}"; do
    base="$(basename "$f")"
    sym="${base%.png}"
    sym="${sym//-/_}"
    sym="${sym//./_}"

    xxd -i "$f" | sed -E \
      -e "s/^unsigned char[[:space:]]+[^[]+\[/static const unsigned char ${sym}_png[/" \
      -e "s/^unsigned int[[:space:]]+[^=]+ =/static const unsigned int ${sym}_png_len =/"

    echo
  done
} > "$TMP_FILE"

mv -f "$TMP_FILE" "$OUT_FILE"
