#!/usr/bin/env bash
# Compila o jogo para Windows a partir do Linux (MinGW-w64) e monta portatil/MKSM-windows.
# uso: scripts/build_windows.sh [raiz com PS2Recomp/ e extraido_full/]   (padrão: a raiz deste repositório)
# Precisa de mingw-w64-gcc, cmake, ninja e glslc. O primeiro build baixa o FFmpeg para Windows (GitHub, o
# mesmo pacote que o PS2Recomp usa no Windows, 34 MB). O código gerado vai em lotes de 32 arquivos com cabeçalho
# pré-compilado: ~0,5 GB por job, menos de 1 h com JOBS = número de threads (o padrão).
set -euo pipefail
AQUI=$(cd "$(dirname "$0")/.." && pwd)
RAIZ=$(cd "${1:-$AQUI}" && pwd)
SRC=$RAIZ/PS2Recomp B=$RAIZ/PS2Recomp/out/build-win OUT=$RAIZ/portatil/MKSM-windows DEPS=$RAIZ/PS2Recomp/out/build/_deps
command -v x86_64-w64-mingw32-g++ >/dev/null || { echo "Falta o MinGW: sudo pacman -S mingw-w64-gcc" >&2; exit 1; }
mkdir -p "$B"

# vulkan-1.dll vem com o driver de vídeo; a biblioteca de ligação sai da lista de funções que o runtime chama.
{ echo "LIBRARY vulkan-1.dll"; echo EXPORTS; grep -rhoE '\bvk[A-Z][A-Za-z0-9]+\(' "$SRC/ps2xRuntime/src" | tr -d '(' | sort -u; } > "$B/vulkan-1.def"
# Carga atrasada (-y): o Windows só procura o vulkan-1.dll na primeira chamada. Sem isso, PC sem Vulkan ou com um
# Vulkan 1.0 antigo nem abria ("ponto de entrada vkGetPhysicalDeviceFeatures2 não encontrado"); o runtime confere
# antes (ps2xVulkanLoaderUsable) e desenha pela CPU.
x86_64-w64-mingw32-dlltool -d "$B/vulkan-1.def" -y "$B/libvulkan-1.a"

# Reaproveita as fontes que o build do Linux já baixou.
fontes=()
for par in raylib:RAYLIB imgui:IMGUI rlimgui:RLIMGUI vulkan_headers:VULKAN_HEADERS; do
  [[ -d "$DEPS/${par%%:*}-src" ]] && fontes+=("-DFETCHCONTENT_SOURCE_DIR_${par##*:}=$DEPS/${par%%:*}-src")
done
# Rodar de outro checkout (um worktree) muda o caminho do toolchain, e o CMake apaga o cache: o PS2X_PGO=usar do
# scripts/pgo_windows.sh sumia e o .exe saía sem PGO (~4% mais lento). Guarda a escolha antes.
pgo=$(sed -n 's/^PS2X_PGO:STRING=//p' "$B/CMakeCache.txt" 2>/dev/null || true)
cmake -S "$SRC" -B "$B" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$AQUI/scripts/mingw-w64.cmake" ${pgo:+-DPS2X_PGO=$pgo} \
  -DCMAKE_CXX_FLAGS="-msse4.1 -O1 -g0" -DCMAKE_EXE_LINKER_FLAGS="-static" \
  -DPS2X_BUILD_RECOMP=OFF -DPS2X_BUILD_ANALYZER=OFF -DPS2X_BUILD_STUDIO=OFF -DPS2X_BUILD_TEST=OFF \
  -DPS2X_NATIVE_ARCH=OFF -DPS2X_OPTIMIZE_RUNTIME_LIB=ON -DPS2X_SHOW_WINDOWS_CONSOLE=OFF \
  -DPS2X_VULKAN_LIBRARY="$B/libvulkan-1.a" "${fontes[@]}"
ninja -C "$B" -j "${JOBS:-$(nproc)}" ps2EntryRunner

mkdir -p "$OUT/bin" "$OUT/jogo"
cp "$B/ps2xRuntime/ps2EntryRunner.exe" "$OUT/bin/MKSM.exe"
cp "$B"/ThirdParty/ffmpeg-prefix/src/ffmpeg_external/bin/*.dll "$OUT/bin/" # FFmpeg e o que ele usa
# Os arquivos do jogo viram links para extraido_full (o jogo só os lê; copiar a pasta para um pendrive copia o
# conteúdo). Os memory cards são cópias de verdade, só na primeira vez, e vêm da pasta Linux se você já jogou nela.
rsync -a --exclude=mc0 --exclude=mc1 --link-dest="$RAIZ/extraido_full/" "$RAIZ/extraido_full/" "$OUT/jogo/"
saves=$RAIZ/extraido_full
[[ -d "$RAIZ/portatil/MKSM-linux/jogo/mc0" ]] && saves=$RAIZ/portatil/MKSM-linux/jogo
cp -rn "$saves/mc0" "$saves/mc1" "$OUT/jogo/" 2>/dev/null || true

crlf() { sed 's/$/\r/'; }
crlf > "$OUT/Jogar.bat" <<'EOF'
@echo off
rem Roda o jogo. Configuracao em mksm.ini (ao lado deste arquivo).
setlocal
set "D=%~dp0"
if exist "%D%mksm.ini" for /f "usebackq eol=# tokens=1,* delims==" %%a in ("%D%mksm.ini") do set "%%a=%%b"
if not defined PS2X_CAPTURE_DIR set "PS2X_CAPTURE_DIR=%D%capturas"
if not defined PS2X_GS_RECORD_DIR set "PS2X_GS_RECORD_DIR=%D%gsrec"
set "MKSM_INI=%D%mksm.ini"
if not exist "%PS2X_CAPTURE_DIR%" mkdir "%PS2X_CAPTURE_DIR%"
if not exist "%PS2X_GS_RECORD_DIR%" mkdir "%PS2X_GS_RECORD_DIR%"
rem O runtime cria mc0\ na pasta atual antes de saber onde esta o ELF.
cd /d "%D%jogo"
start "" "%D%bin\MKSM.exe" "%D%jogo\SLUS_210.87"
EOF
# Igual ao Jogar.bat, com a medição ligada: a cada 2 s, quadros do jogo, imagens novas e o pior intervalo vão para
# medida.txt (o usuário manda o arquivo; N22, A1).
crlf > "$OUT/Jogar-medindo.bat" <<'EOF'
@echo off
rem Igual ao Jogar.bat, mas grava a medida de desempenho em medida.txt (nesta pasta). Jogue onde engasga e feche o jogo.
setlocal
set "D=%~dp0"
if exist "%D%mksm.ini" for /f "usebackq eol=# tokens=1,* delims==" %%a in ("%D%mksm.ini") do set "%%a=%%b"
if not defined PS2X_CAPTURE_DIR set "PS2X_CAPTURE_DIR=%D%capturas"
if not defined PS2X_GS_RECORD_DIR set "PS2X_GS_RECORD_DIR=%D%gsrec"
set "MKSM_INI=%D%mksm.ini"
set "PS2X_PERF=1"
if not exist "%PS2X_CAPTURE_DIR%" mkdir "%PS2X_CAPTURE_DIR%"
if not exist "%PS2X_GS_RECORD_DIR%" mkdir "%PS2X_GS_RECORD_DIR%"
echo Medindo. Jogue onde engasga e feche o jogo; a medida fica em medida.txt nesta pasta.
cd /d "%D%jogo"
"%D%bin\MKSM.exe" "%D%jogo\SLUS_210.87" > "%D%medida.txt" 2>&1
EOF

if [[ ! -f "$OUT/mksm.ini" ]]; then  # o jogo regrava as chaves gráficas e mantém o resto
  { echo "# Configuração do jogo: tire o # da frente de uma linha VAR=valor para usar."
    sed -n '3,/^set -euo/{/^#/p}' "$AQUI/scripts/jogar.sh"; } | crlf > "$OUT/mksm.ini"
fi
grep -q 'RPG_' "$OUT/mksm.ini" || # pasta de antes do modo RPG: só acrescenta as chaves dele
  sed -n '/^# Modo RPG/,/^# Cada slot RPG/p' "$AQUI/scripts/jogar.sh" | crlf >> "$OUT/mksm.ini"

crlf > "$OUT/LEIA-ME.txt" <<'EOF'
ShaolinMonks RPG: Mortal Kombat: Shaolin Monks recompilado para PC, versão Windows portátil

COMO JOGAR
  Dê dois cliques em Jogar.bat. Não precisa instalar nada.
  Engasgando? Use o Jogar-medindo.bat: ele grava a medida de desempenho em medida.txt (mande esse arquivo).
  Num notebook, use o plano de energia "Alto desempenho" e o carregador ligado: no "Equilibrado" o Windows
  baixa o clock do processador e o jogo perde quadros.
  Se o Windows mostrar "O Windows protegeu o computador", clique em "Mais informações" e
  "Executar assim mesmo" (o programa não tem assinatura digital).

PRECISA
  Windows 10 ou 11, 64 bits, com driver de vídeo atualizado (Vulkan e OpenGL: NVIDIA, AMD ou Intel).
  Sem Vulkan, ponha PS2X_GS_HW=0 no mksm.ini (renderizador antigo, só na resolução do PS2).

CONTROLES
  Controle (gamepad): funciona direto; um segundo controle joga como jogador 2.
  Teclado (só jogador 1):
    Setas ou WASD = direcional        Enter = Start       Tab = Select
    X ou Espaço = X                   C ou Esc = Círculo
    Z = Quadrado                      V = Triângulo
    Q/E = L1/R1, Shift esquerdo/direito = L2/R2
  Para trocar os botões: F1 (ou R3 nos menus), L1/R1 até a aba Controles.
  Jogador 2 (controle genérico com botões trocados): aperte o R3 do controle 2 nos menus ou na pausa,
  a janela abre nos botões dele. Controle 2, direcional para cima: liga/desliga a defesa contínua
  (como segurar L2), para jogar o Ko-op com um controle só.
  F1 = menu Gráfico    F11 = tela cheia    F10 = salva a tela em capturas\

CONFIGURAÇÃO
  mksm.ini (abra no Bloco de Notas) tem todas as opções comentadas (volume, tela 16:9,
  resolução interna, pular os filmes...). O menu Gráfico (F1, ou R3 nos menus) grava as escolhas ali.
  PC com placa de vídeo fraca: no menu Gráfico, ponha a Predefinição em "Mínimo".

MODO RPG (opcional, só em jogo novo)
  Single Player ou Ko-op -> personagem -> na escolha de dificuldade, cursor no HARD e aperte L3
  (F2 no teclado): aparece "RPG" ao lado do Hard. O jogo fica sempre no Hard, com inimigos mais fortes.
  Matar inimigos dá XP: o nível aparece acima do "EXP" (no Ko-op, um para cada jogador).
  Subiu de nível: pausa (Select) e R3 (ou F1), aba RPG, X gasta o ponto. No Ko-op, cada jogador
  abre a janela com o R3 do próprio controle e vê só o próprio RPG. "Redistribuir pontos" (X duas vezes) devolve todos.
  Na tela Load Game, os saves RPG mostram "RPG · Nível N". Um save RPG continua RPG; gravar um jogo normal
  por cima tira o RPG daquele slot. Os números (XP por nível, força dos atributos) estão no mksm.ini.

  Lutadores: no Versus todos vêm liberados, e Sub-Zero e Scorpion na seleção da história. Para jogar a história ou o Ko-op com outro
  lutador: num jogo novo, na escolha de dificuldade, L2/R2 no controle de cada jogador (Shift esq./dir. no teclado; segurado, passa
  sozinho). Fica no save. São 67, nesta ordem: monges e Versus (os únicos com fatality); com especiais (Shao Kahn, Goro, Kano,
  Shang Tsung, Inferno Scorpion, Ermac chefe, Kintaro); só golpes normais (Mileena, Jade, Jax, as versões
  de história do Versus como "Scorpion Boss", Shadow Priest, Orochi, inimigos...);
  golpes com animação bugada; e os que só andam (Noob Saibot, Smoke, Raiden, Kabal, Sonya...). Embaixo do nome aparece a situação.
  Fora dos monges e do Versus não há defesa de verdade: com L2 levam 20% do dano, mas trechos que pedem defender não avançam.
  No Ko-op, recomendado que um dos jogadores use um dos 4 clássicos (Liu Kang, Kung Lao, Sub-Zero ou Scorpion): alguns trechos
  da história pedem Brutality ou Multality, que só esses quatro fazem com certeza.

SAVES
  Ficam em jogo\mc0 (memory card 1) e jogo\mc1 (memory card 2). Copie essas pastas, e os arquivos rpg_mc0_slot*.ini se jogar no modo RPG, para fazer backup.
EOF
crlf > "$OUT/TESTAR.txt" <<'EOF'
Roteiro de teste no Windows (marque o que funcionou e conte o que não funcionou)

1. Abrir: dois cliques em Jogar.bat. Abre a janela, toca a abertura, chega ao menu.
2. Controle: o gamepad mexe no menu. F11 tela cheia. F1 abre o menu Gráfico.
3. Desempenho: menu Gráfico > Predefinição. "Alto" num PC bom, "Leve" ou "Mínimo" num fraco.
4. Modo RPG: Single Player > personagem > dificuldade: cursor no Hard, L3 (F2) mostra "RPG".
   Matar inimigos enche a barra de XP acima do EXP. Subiu de nível: aviso no meio da tela; Select, R3, aba RPG, gastar ponto.
5. Lutador novo: Sub-Zero e Scorpion já aparecem na seleção. Na tela de dificuldade, L2/R2 escolhe
   qualquer um dos 67 (segure para passar rápido; a situação aparece embaixo do nome).
   Na fase: golpes (quadrado, triângulo, bola), especiais (R1 + botão), lista em Start > Moves.
   Tente uma fatality quando a barra azul encher (veja os comandos em Start > Moves > Fatalities).
   Fatality só existe para os monges e os do Versus. Os outros: golpes (e especiais em alguns), sem defesa de verdade.
6. Ko-op: dois controles, Ko-op, cada um escolhe o lutador com L2/R2 no próprio controle (recomendado: um dos dois
   com Liu Kang, Kung Lao, Sub-Zero ou Scorpion, por causa dos trechos de Brutality/Multality).
   Controle 2 com botões trocados: R3 nele abre a aba Controles dele. Cima no controle 2: defesa contínua.
7. Versus: todos os 8 lutadores liberados.
8. Save: salvar num slot, fechar, abrir, Load Game. O slot RPG mostra "RPG · Nível N".

Se der erro ou fechar sozinho: mande print e diga em que passo foi.
EOF
echo "Pronto: $OUT ($(du -sh "$OUT" | cut -f1))"
