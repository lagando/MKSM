#!/usr/bin/env bash
# Roda o jogo. Configuração opcional em mksm.ini na raiz do projeto (linhas VAR=valor):
#   MKSM_SKIP_INTRO=1     pula os filmes da abertura
#   PS2X_REVERB=22        eco dos efeitos (0 desliga, até 100)
#   PS2X_VOL_MASTER=100   volume geral (0 a 100)
#   PS2X_VOL_MUSIC=100    volume da música e das falas
#   PS2X_VOL_SFX=100      volume dos efeitos
#   PS2X_RAYS=0           raios de luz como no original (opacos)
#   PS2X_DEINTERLACE=0    não mistura os campos
#   PS2X_FXAA=1           suaviza as bordas na janela (pode amaciar o texto do HUD)
#   PS2X_GPU_AUTO=0       não força a placa NVIDIA na janela
#   PS2X_RUMBLE=0         sem vibração no controle
# F11 alterna tela cheia.
set -euo pipefail
raiz="$(cd "$(dirname "$0")/.." && pwd)"
if [[ -f "$raiz/mksm.ini" ]]; then
    while IFS='=' read -r chave valor; do
        [[ -z "$chave" || "$chave" == \#* ]] && continue
        export "$chave=$valor"
    done < "$raiz/mksm.ini"
fi
cd "$raiz/PS2Recomp"
exec ./out/build/ps2xRuntime/ps2EntryRunner ../extraido_full/SLUS_210.87 "$@"
