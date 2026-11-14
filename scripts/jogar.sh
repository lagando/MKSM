#!/usr/bin/env bash
# Roda o jogo. Configuração opcional em mksm.ini na raiz do projeto (linhas VAR=valor):
#   MKSM_SKIP_INTRO=1     pula os filmes da abertura
#   PS2X_REVERB=22        eco dos efeitos (0 desliga, até 100)
#   PS2X_VOL_MASTER=100   volume geral (0 a 100)
#   PS2X_VOL_MUSIC=100    volume da música e das falas
#   PS2X_VOL_SFX=100      volume dos efeitos
#   PS2X_VIF_AHEAD=0      ordem antiga VIF/VU1 (chão quadriculado; só para comparar)
#   PS2X_DEINTERLACE=0    não mistura os campos
#   PS2X_FXAA=1           suaviza as bordas na janela (pode amaciar o texto do HUD)
#   PS2X_GPU_AUTO=0       não força a placa NVIDIA na janela
#   PS2X_RUMBLE=0         sem vibração no controle
#   PS2X_MOVIE_AUDIO=0    som dos filmes pelo caminho emulado (o padrão toca direto do .SFD)
#   PS2X_GS_CSR_LAG=0     o jogo espera o GS terminar cada quadro (mais lento; só para comparar)
#   PS2X_GS_HW=1          renderizador novo (experimental: mais fiel, mais lento na fase)
#   PS2X_SCALE=2          resolução interna 2x/3x/4x (só com PS2X_GS_HW=1)
# Dois controles: o segundo joga na porta 2; o teclado só no primeiro jogador.
# F11 alterna tela cheia. F9 grava a cena (2 quadros do GS) em gsrec/cena_<data>.gsr; F10 salva a tela em capturas/.
set -euo pipefail
raiz="$(cd "$(dirname "$0")/.." && pwd)"
if [[ -f "$raiz/mksm.ini" ]]; then
    while IFS='=' read -r chave valor; do
        [[ -z "$chave" || "$chave" == \#* ]] && continue
        export "$chave=$valor"
    done < "$raiz/mksm.ini"
fi
export PS2X_GS_RECORD_DIR="${PS2X_GS_RECORD_DIR:-$raiz/gsrec}" PS2X_CAPTURE_DIR="${PS2X_CAPTURE_DIR:-$raiz/capturas}"
mkdir -p "$PS2X_GS_RECORD_DIR" "$PS2X_CAPTURE_DIR"
cd "$raiz/PS2Recomp"
exec ./out/build/ps2xRuntime/ps2EntryRunner ../extraido_full/SLUS_210.87 "$@"
