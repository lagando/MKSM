#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
PS2=PS2Recomp/out/build
[ -x "$PS2/ps2xAnalyzer/ps2_analyzer" ] || { echo "ps2_analyzer não compilado" >&2; exit 1; }
"$PS2/ps2xAnalyzer/ps2_analyzer" extraido/SLUS_210.87 sm.toml
"$PS2/ps2xRecomp/ps2_recomp" sm.toml
cp output/*.cpp PS2Recomp/ps2xRuntime/src/runner/
cp output/*.h  PS2Recomp/ps2xRuntime/include/
