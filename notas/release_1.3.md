# ShaolinMonks RPG 1.3 (rascunho, sai depois do teste do usuário no muro da Wu Shi)

## Correções

- **Muro da Wu Shi:** a transição para a cutscene não trava mais (personagem parado no ar, controle travado ou tela preta). O jogo agora tem a mesma memória do PS2: o heap ia só até `0x1F00000`, e a cutscene não conseguia reservar o buffer de leitura.
- **Memória igual à do PS2:** o heap do jogo vai até `0x1FF0000`, como no console. A memória do próprio emulador foi para a área do kernel, que o jogo nunca usa.

## Problemas conhecidos

- HUD da Wu Shi às vezes aparece só com o texto durante a luta.
- Uma cutscene depois do muro roda sem som; o vídeo do Shang Tsung e algumas cutscenes ficam um pouco lentos.
- **Sons das áreas:** os bancos de som deixam de falhar depois de algumas trocas de sala (a memória do processador de som era liberada e reaproveitada errado).
- **Carregamentos:** as telas de LOADING não congelam mais a janela (no carregamento da fase, de 39 para 50 quadros/s).
- **Muros da Wu Shi lentos:** o jogo não se arrasta mais a 1 quadro/s perto dos muros.

## Novidades

- **Save state de depuração:** `MKSM_SAVESTATE=1` liga F5 (salvar) e F6 (carregar). Feito para investigar bugs; não substitui o save do jogo.

## Windows

- Executável novo com PGO e todas as correções acima.

## Conferir antes de publicar

- [x] Usuário passa pelo muro da Wu Shi (Linux, 18/11): passou, terminou a Wu Shi, chegou ao mapa seguinte e salvou no slot 4.
- [ ] HUD da Wu Shi: não mudou com a correção de memória (problema conhecido).
- [ ] Windows: abrir, carregar o save e jogar uns minutos.
- [ ] Juntar a branch `claude/wizardly-jones-acf5a6` na `main` e montar os pacotes.
