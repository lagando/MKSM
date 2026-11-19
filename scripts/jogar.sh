#!/usr/bin/env bash
# Roda o jogo. Configuração opcional em mksm.ini na raiz do projeto (linhas VAR=valor):
#   MKSM_SKIP_INTRO=1     pula os filmes da abertura
#   PS2X_REVERB=22        eco dos efeitos (0 desliga, até 100)
#   PS2X_VOL_MASTER=100   volume geral (0 a 100)
#   PS2X_VOL_MUSIC=100    volume da música e das falas
#   PS2X_VOL_SFX=100      volume dos efeitos
#   PS2X_VIF_AHEAD=0      ordem antiga VIF/VU1 (chão quadriculado; só para comparar)
#   PS2X_DEINTERLACE=1    mistura os campos (padrão: ligado no Windows, desligado no Linux)
#   PS2X_VSYNC=1          sincronia vertical: a imagem troca junto com o monitor (contra engasgo)
#   PS2X_EFFECTS=0        efeitos de luz simples: o brilho sem o desfoque (mais leve para placas de vídeo fracas)
#   PS2X_FXAA=1           suavização de bordas na resolução do PS2 (padrão: ligada no Windows, desligada no Linux)
#   PS2X_UPSCALE=0        amplia a imagem com filtro bilinear (o padrão é bicúbico, mais nítido)
#   PS2X_GPU_AUTO=0       não força a placa NVIDIA na janela
#   PS2X_RUMBLE=0         sem vibração no controle
#   PS2X_DEADZONE=25      zona morta do analógico em % (aumente se o personagem anda sozinho)
#   PS2X_PAD_X=7 / PS2X_TECLA_X=88,32   botão do controle e teclas de cada botão do PS2 (o menu, aba Controles, grava isto)
#   PS2X_MOVIE_AUDIO=0    som dos filmes pelo caminho emulado (o padrão toca direto do .SFD)
#   PS2X_GS_CSR_LAG=0     o jogo espera o GS terminar cada quadro (mais lento; só para comparar)
#   PS2X_GS_HW=0          renderizador antigo, só na resolução do PS2 (o padrão é o novo, por lotes, em 2x)
#   PS2X_SCALE=2          resolução interna do renderizador novo: 1 a 4 (padrão 2)
#   PS2X_GS_HW_THREAD=0   renderizador novo numa thread só (o padrão usa uma thread a mais com 4+ núcleos lógicos)
#   PS2X_WIDESCREEN=1     tela 16:9 (o 3D ganha campo nas laterais; o 2D estica; os filmes ficam 4:3)
#   MKSM_BLOOM=1         liga o brilho (bloom) do jogo; desligado por padrão: numa placa como a GTX 1050 ele derruba a luta do Evil Monastery para ~33 quadros/s
# Modo RPG (jogo novo: na escolha de dificuldade, cursor no Hard, L3 ou F2 liga). Números ajustáveis:
#   RPG_XP_PRIMEIRO=300   XP do nível 1 para o 2      RPG_XP_AUMENTO=100   quanto o 2º degrau custa a mais
#   RPG_XP_ACELERA=50     quanto cada aumento cresce   RPG_NIVEL_MAX=100    nível máximo
#   RPG_XP_EXTRA_1=100    a mais por degrau até o nível 10   RPG_XP_EXTRA_2=200   do 10 ao 29   RPG_XP_EXTRA_3=300   do 30 em diante
#   RPG_PONTOS_POR_NIVEL=1   pontos a cada nível       RPG_TETO_ATRIBUTO=20 pontos por atributo (0 = sem teto)
#   RPG_TETO_SABEDORIA=5     pontos na Sabedoria (0 = usa o teto geral)
#   RPG_PERDE_XP_AO_MORRER=1 morreu e continuou: perde o XP desde o checkpoint, sem cair de nível (0 desliga)
#   RPG_FORCA_POR_PONTO=5    % de dano causado         RPG_DEFESA_POR_PONTO=3   % de dano recebido a menos
#   RPG_VIDA_POR_PONTO=5     % de vida máxima          RPG_ROUBO_POR_PONTO=1    % do dano causado que vira vida
#   RPG_REGEN_POR_PONTO=0,1  % da vida por segundo     RPG_SABEDORIA_POR_PONTO=3 % de XP a mais para o nível
#   RPG_INIMIGO_DANO=30      % de dano a mais dos inimigos   RPG_INIMIGO_VIDA=30   % de vida a mais dos inimigos
#   RPG_INIMIGO_POR_NIVEL=0  % a mais nos dois por nível do jogador
# Cada slot RPG guarda o perfil em rpg_mc0_slot<n>.ini, ao lado deste mksm.ini (copie junto com o save).
# Menu "Gráfico": R3 nos menus, na pausa (Select) e no mapa, ou F1 a qualquer hora. As opções valem na
# hora (renderizador e thread ao reiniciar) e ficam gravadas neste mksm.ini. F12: painel de depuração.
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
export MKSM_INI="$raiz/mksm.ini" # o menu Gráfico grava aqui
mkdir -p "$PS2X_GS_RECORD_DIR" "$PS2X_CAPTURE_DIR"
cd "$raiz/PS2Recomp"
exec ./out/build/ps2xRuntime/ps2EntryRunner ../extraido_full/SLUS_210.87 "$@"
