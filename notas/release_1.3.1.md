# ShaolinMonks RPG 1.3.1

## Desempenho

- **Bloom desligado por padrão:** o brilho do jogo fazia o renderizador novo copiar a imagem várias vezes por quadro. Numa GTX 1050, a luta no telhado do Evil Monastery caía de 60 para ~33 quadros/s; sem o bloom, volta a 60 e a placa fica em ~20%. A imagem quase não muda (céu e luzes um pouco menos estourados).
- **Opção no menu Gráfico:** "Brilho (bloom)" liga e desliga a qualquer hora (F1, ou R3 na pausa). A escolha fica salva no `mksm.ini` como `MKSM_BLOOM`.

## O resto

Tudo da 1.3: história testada até Reptile's Lair, correção do muro da Wu Shi, portões de fatality, Multality e Brutality para os outros lutadores, Shao Kahn mais forte e o instalador do Windows corrigido. Notas completas na [1.3](https://github.com/lagando/MKSM/releases/tag/v1.3).

## Problemas conhecidos

- HUD da Wu Shi às vezes aparece só com o texto durante a luta.
- Alguns vídeos e cutscenes tocam com o som errado ou sem som.
- Com o bloom ligado, cenas como a luta do Evil Monastery ficam lentas em placas como a GTX 1050.
