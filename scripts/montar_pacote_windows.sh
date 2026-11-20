#!/usr/bin/env bash
# Monta dist/ShaolinMonksRPG-Windows.zip: o repositório, o PS2Recomp com os patches, o código das bibliotecas e um kit
# de compilação portátil (WinLibs: GCC, CMake, Ninja) para o Instalar.bat compilar no próprio Windows, sem instalar
# nada e sem internet. Nada do jogo entra.
# uso: scripts/montar_pacote_windows.sh <winlibs.zip>
#   Precisa do build do Linux (fontes das bibliotecas) e do build do Windows (shaders, zip do FFmpeg) já feitos.
set -euo pipefail
cd "$(dirname "$0")/.."
RAIZ=$PWD
WINLIBS=$(realpath "$1")
DEPS=$RAIZ/PS2Recomp/out/build/_deps BW=$RAIZ/PS2Recomp/out/build-win
for f in "$DEPS" "$BW/ps2xRuntime/gs_shaders" "$WINLIBS"; do [ -e "$f" ] || { echo "Falta $f" >&2; exit 1; }; done
FFZIP=$(find "$BW/ThirdParty/ffmpeg-prefix/src" -maxdepth 1 -name 'ffmpeg-*.zip' | head -1)
[ -n "$FFZIP" ] || { echo "Falta o zip do FFmpeg (rode scripts/build_windows.sh uma vez)" >&2; exit 1; }
OBRA=$(mktemp -d)
P=$OBRA/ShaolinMonksRPG

echo "== Repositório (só o que está no git)"
git archive --format=tar --prefix=ShaolinMonksRPG/ HEAD | tar -x -C "$OBRA"

echo "== PS2Recomp no commit fixado, com os patches"
git clone -q "$RAIZ/PS2Recomp" "$OBRA/ps2recomp"
scripts/aplicar_patches.sh "$OBRA/ps2recomp" > /dev/null
mkdir -p "$P/vendor/deps"
rsync -a --exclude=.git --exclude=out "$OBRA/ps2recomp/" "$P/vendor/PS2Recomp/"

echo "== Código das bibliotecas"
for n in elfio toml11 fmt libdwarf rabbitizer raylib imgui rlimgui vulkan_headers; do
    rsync -a --exclude=.git --exclude=/tests --exclude=/examples "$DEPS/$n-src/" "$P/vendor/deps/$n/"
done

echo "== Shaders compilados, FFmpeg, lista do Vulkan e modelo da pasta portátil"
cp -r "$BW/ps2xRuntime/gs_shaders" "$P/vendor/gs_shaders"
cp "$FFZIP" "$P/vendor/ffmpeg.zip"
# Perfil do PGO do runtime, com os nomes relativos à pasta de compilação (-fprofile-prefix-path): o Instalar.bat
# compila com ele, como o executável de teste (o runtime fica ~5-10% mais leve).
mkdir -p "$P/vendor/pgo"
find "$BW/pgo" -maxdepth 1 -name "*.gcda" ! -name "#*" -exec cp {} "$P/vendor/pgo/" \;
{ echo "LIBRARY vulkan-1.dll"; echo EXPORTS
  grep -rhoE '\bvk[A-Z][A-Za-z0-9]+\(' "$P/vendor/PS2Recomp/ps2xRuntime/src" | tr -d '(' | sort -u; } > "$P/vendor/vulkan-1.def"
mkdir -p "$P/vendor/modelo_portatil"
for f in Jogar.bat Jogar-medindo.bat LEIA-ME.txt TESTAR.txt; do cp "$RAIZ/portatil/MKSM-windows/$f" "$P/vendor/modelo_portatil/"; done
{ echo "# Configuração do jogo: tire o # da frente de uma linha VAR=valor para usar."
  sed -n '3,/^set -euo/{/^#/p}' scripts/jogar.sh; } | sed 's/$/\r/' > "$P/vendor/modelo_portatil/mksm.ini"

echo "== Kit de compilação (WinLibs)"
mkdir -p "$P/ferramentas"
unzip -q "$WINLIBS" -d "$P/ferramentas"
[ -x "$P/ferramentas/mingw64/bin/g++.exe" ] || [ -f "$P/ferramentas/mingw64/bin/g++.exe" ] || { echo "WinLibs sem mingw64/bin/g++.exe" >&2; exit 1; }

# Nada do jogo pode ir junto.
if find "$P" \( -iname '*.iso' -o -name 'SLUS_*' -o -name '*.gsr' \) | grep -q .; then
    echo "Arquivo do jogo no pacote: abortado" >&2; exit 1
fi
mkdir -p dist
rm -f dist/ShaolinMonksRPG-Windows.zip
(cd "$OBRA" && zip -qr -9 "$RAIZ/dist/ShaolinMonksRPG-Windows.zip" ShaolinMonksRPG)
rm -rf "$OBRA"
ls -la dist/ShaolinMonksRPG-Windows.zip
