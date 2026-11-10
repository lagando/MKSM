#!/usr/bin/env bash
# Puts this project's changes into a clean PS2Recomp checkout at the pinned commit.
# Coloca as mudanças deste projeto num checkout limpo do PS2Recomp no commit fixado.
# usage/uso: scripts/aplicar_patches.sh [PS2Recomp dir, default ./PS2Recomp]
set -euo pipefail
RAIZ=$(cd "$(dirname "$0")/.." && pwd)
DEST=${1:-$RAIZ/PS2Recomp}
COMMIT=$(cat "$RAIZ/patches/UPSTREAM_COMMIT")
if [ ! -d "$DEST/.git" ]; then
  git clone https://github.com/ran-j/PS2Recomp.git "$DEST"
fi
git -C "$DEST" checkout -q "$COMMIT"
git -C "$DEST" submodule update --init --recursive
git -C "$DEST" apply "$RAIZ/patches/runtime_atual.diff"
grep -vE '^\s*(#|$)' "$RAIZ/patches/arquivos.txt" | while read -r file dir; do
  mkdir -p "$DEST/$dir"
  cp "$RAIZ/patches/$file" "$DEST/$dir/"
done
echo "PS2Recomp @ $COMMIT + patches: $DEST"
