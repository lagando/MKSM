#!/usr/bin/env bash
# Regenerates patches/ from your PS2Recomp/ working tree. Run before every commit that
# touches PS2Recomp/ (it is never committed directly).
# Regenera patches/ a partir da sua árvore PS2Recomp/. Rode antes de cada commit que
# mexa no PS2Recomp/ (ele nunca é commitado diretamente).
set -euo pipefail
RAIZ=$(cd "$(dirname "$0")/.." && pwd)
SRC=${1:-$RAIZ/PS2Recomp}
git -C "$SRC" diff -- . ':!ps2xRuntime/src/runner' > "$RAIZ/patches/runtime_atual.diff"
grep -vE '^\s*(#|$)' "$RAIZ/patches/arquivos.txt" | while read -r file dir; do
  cp "$SRC/$dir/$file" "$RAIZ/patches/$file"
done
# New untracked source files not yet listed in patches/arquivos.txt:
git -C "$SRC" ls-files --others --exclude-standard -- '*.cpp' '*.h' '*.comp' '*.vert' '*.frag' |
  grep -v '^ps2xRuntime/src/runner/' | while read -r path; do
    grep -qE "^$(basename "$path")\s" "$RAIZ/patches/arquivos.txt" ||
      echo "warning: $path is new and not in patches/arquivos.txt"
  done
echo "patches/ updated"
