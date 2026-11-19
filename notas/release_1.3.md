# ShaolinMonks RPG 1.3

## História: até Reptile's Lair

Testadas do início ao fim: menu inicial, Goro's Lair, Wu Shi Academy, The Portal, Evil Monastery, Living Forest e Reptile's Lair.

## Correções

- **Muro da Wu Shi:** a transição para a cutscene não trava mais (personagem parado no ar, controle travado ou tela preta). O jogo agora tem a mesma memória do PS2: o heap ia só até `0x1F00000`, e a cutscene não conseguia reservar o buffer de leitura.
- **Memória igual à do PS2:** o heap do jogo vai até `0x1FF0000`, como no console. A memória do próprio emulador foi para a área do kernel, que o jogo nunca usa.
- **Sons das áreas:** os bancos de som deixam de falhar depois de algumas trocas de sala (a memória do processador de som era liberada e reaproveitada errado).
- **Carregamentos:** as telas de LOADING não congelam mais a janela (no carregamento da fase, de 39 para 50 quadros/s).
- **Muros da Wu Shi lentos:** o jogo não se arrasta mais a 1 quadro/s perto dos muros.

## Outros lutadores na história

- **Portões de fatality:** quando nenhum jogador tem fatality (nenhum monge nem lutador do Versus na partida), os portões "faça uma fatality" abrem sozinhos.
- **Portões de Multality e Brutality:** quem não tem Multality ou Brutality (Johnny Cage, Kitana, Reptile, Baraka e outros) passa por esses portões digitando as setas que aparecem. O jogo cancela o golpe, mas o portão abre. Liu Kang, Kung Lao, Sub-Zero e Scorpion fazem os de verdade.
- **Shao Kahn:** na história, causa 2,5× de dano e leva 0,7×. Ele defende de verdade com o L2.
- **Defesa do L2:** saiu a defesa por software (L2 reduzia o dano para 20% nos lutadores fora do Versus). Cada lutador usa a própria defesa.
- **Johnny Cage** chegou ao Reptile's Lair.

## Novidades

- **Bloom desligado por padrão:** o brilho do jogo derrubava a luta no telhado do Evil Monastery de 60 para ~33 quadros/s numa GTX 1050. Liga de volta no menu Gráfico (F1 ou R3, "Brilho (bloom)") a qualquer hora, ou com `MKSM_BLOOM=1` no `mksm.ini`.

- **Save state de depuração:** `MKSM_SAVESTATE=1` liga F5 (salvar) e F6 (carregar). Feito para investigar bugs; não substitui o save do jogo.
- **README:** tabela de testes por lutador (quem zera a história).

## Problemas conhecidos

- HUD da Wu Shi às vezes aparece só com o texto durante a luta.
- Alguns vídeos e cutscenes tocam com o som errado ou sem som; o vídeo do Shang Tsung e algumas cutscenes ficam um pouco lentos.
- Shao Kahn: a sala da fatality fica lenta; parado, ele se contorce; para para bater; não passa do portão do arremesso (a sala seguinte à fatality).
- A loja dos lutadores do Versus mostra os golpes do Liu Kang; só o especial do R2 funciona neles.

## Windows

- Executável novo com as correções acima.
- Instalador: corrigido o erro ao tirar o "somente leitura" dos arquivos copiados da ISO (contribuição de ashtonrd, PR #1).

## Conferir antes de publicar

- [x] Usuário passa pelo muro da Wu Shi (Linux, 18/11).
- [x] Portões de fatality, Multality e Brutality com Shao Kahn e Johnny Cage (Linux, 18 e 19/11).
- [x] Recompilar o Windows com as mudanças de 18 e 19/11.
- [ ] Windows: abrir, carregar o save e jogar uns minutos.
- [x] Montar os pacotes (`dist/ShaolinMonksRPG-Linux.zip`, 24 MB; `dist/ShaolinMonksRPG-Windows.zip`, 334 MB; sem nada do jogo).
