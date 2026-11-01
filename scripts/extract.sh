#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
ISO="Mortal Kombat - Shaolin Monks (USA).iso"
[ -f "$ISO" ] || { echo "ISO ausente: $ISO" >&2; exit 1; }
mkdir -p extraido extraido_full
7z x "$ISO" SYSTEM.CNF SLUS_210.87 -oextraido
7z x "$ISO" -oextraido_full
