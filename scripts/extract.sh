#!/usr/bin/env bash
# usage/uso: scripts/extract.sh "path/to/your.iso"
set -euo pipefail
cd "$(dirname "$0")/.."
ISO=${1:-"Mortal Kombat - Shaolin Monks (USA).iso"}
[ -f "$ISO" ] || { echo "ISO not found / não encontrada: $ISO" >&2; exit 1; }
7z x -y "$ISO" -oextraido_full >/dev/null
mkdir -p extraido && cp extraido_full/SLUS_210.87 extraido/
