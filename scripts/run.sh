#!/usr/bin/env bash
# uso: scripts/run.sh [arquivo_log] [segundos]
set -euo pipefail
cd "$(dirname "$0")/.."
LOG="${1:-run_raw.log}"
DUR="${2:-60}"
cd PS2Recomp
timeout "$DUR" script -qfc "./out/build/ps2xRuntime/ps2EntryRunner ../extraido_full/SLUS_210.87" "$LOG"
tr '\r' '\n' < "$LOG" | grep -a 'run:tick' | tail -2 | cut -c1-700
