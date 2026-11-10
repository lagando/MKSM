#!/usr/bin/env bash
# Monta o MKSM recompilado do zero a partir da ISO do usuário.
# uso: scripts/instalar.sh "caminho/da/sua.iso"
# Cada etapa pula se já foi feita; rode de novo se algo falhar no meio.
set -euo pipefail
cd "$(dirname "$0")/.."
RAIZ=$PWD
JOBS=${JOBS:-2}  # build do código gerado usa ~5 GB por job

ISO=${1:-"Mortal Kombat - Shaolin Monks (USA).iso"}
[ -f "$ISO" ] || { echo "ISO não encontrada: $ISO"; echo "uso: $0 caminho/da/sua.iso"; exit 1; }

for cmd in git cmake ninja 7z g++ glslc; do
  command -v "$cmd" >/dev/null || { echo "Falta instalar: $cmd"; exit 1; }
done

echo "== 1/5 Extraindo a ISO"
if [ ! -f extraido_full/SLUS_210.87 ]; then
  7z x -y "$ISO" -oextraido_full >/dev/null
fi
[ -f extraido_full/SLUS_210.87 ] || { echo "SLUS_210.87 não está na ISO: é a versão USA?"; exit 1; }
mkdir -p extraido && cp -u extraido_full/SLUS_210.87 extraido/  # caminho que o sm.toml espera

echo "== 2/5 Baixando o PS2Recomp e aplicando os patches"
if [ ! -d PS2Recomp ]; then
  scripts/aplicar_patches.sh "$RAIZ/PS2Recomp"
else
  echo "PS2Recomp/ já existe, mantendo como está"
fi

echo "== 3/5 Configurando (GCC: SSE4.1, sem LTO)"
cd PS2Recomp
[ -f out/build/build.ninja ] || cmake -S . -B out/build -G Ninja -DCMAKE_BUILD_TYPE=None \
  -DCMAKE_CXX_FLAGS="-msse4.1 -O1 -g0" \
  -DPS2X_ENABLE_RUNNER_UNITY_BUILD=OFF -DPS2X_ENABLE_RUNNER_PCH=OFF

echo "== 4/5 Recompilando o ELF para C++"
if [ ! -f ps2xRuntime/src/runner/.mksm_ok ]; then
  cmake --build out/build -j"$JOBS" --target ps2_recomp
  cd "$RAIZ"
  PS2Recomp/out/build/ps2xRecomp/ps2_recomp sm.toml  # sm.toml já vem pronto; não rodar o analyzer
  cp output/*.cpp PS2Recomp/ps2xRuntime/src/runner/
  cp output/*.h PS2Recomp/ps2xRuntime/include/
  touch PS2Recomp/ps2xRuntime/src/runner/.mksm_ok
  cd PS2Recomp
fi

echo "== 5/5 Compilando o jogo (demora horas na primeira vez)"
cmake --build out/build -j"$JOBS" --target ps2EntryRunner

cat <<EOF

Pronto. Para jogar:
  cd PS2Recomp && MKSM_SKIP_INTRO=1 ./out/build/ps2xRuntime/ps2EntryRunner ../extraido_full/SLUS_210.87
EOF
