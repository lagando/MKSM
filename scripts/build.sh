#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
cd PS2Recomp
cmake --build out/build -j2 --target ps2EntryRunner 2>&1 | tee ../build.log
