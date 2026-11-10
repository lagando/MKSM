#!/usr/bin/env bash
# Regenerates the C++ from the ELF with sm.toml and copies it into the runtime.
# Do not run ps2_analyzer: it rewrites sm.toml, which is hand-tuned for this game.
set -euo pipefail
cd "$(dirname "$0")/.."
PS2=PS2Recomp/out/build
[ -x "$PS2/ps2xRecomp/ps2_recomp" ] || cmake --build "$PS2" --target ps2_recomp
"$PS2/ps2xRecomp/ps2_recomp" sm.toml
cp output/*.cpp PS2Recomp/ps2xRuntime/src/runner/
cp output/*.h  PS2Recomp/ps2xRuntime/include/
