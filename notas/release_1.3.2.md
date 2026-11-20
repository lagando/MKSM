# ShaolinMonks RPG 1.3.2

## Windows mais rápido

- **Começo do Goro's Lair perto de 60 quadros/s** (antes, 35 a 45 na mesma máquina em que o Linux roda a 60). Duas correções que só pesavam no Windows:
  - uma função da VU1 chamada milhares de vezes por quadro reservava 68 KB de pilha, e o Windows conferia página por página a cada chamada;
  - a trava principal do desenhista agora usa a trava nativa do Windows, mais leve.
- **Ainda falta:** com muitos inimigos na tela, o Windows cai para ~40 quadros/s e continua atrás do Linux. A investigação segue.

## O resto

Tudo da 1.3.1 (bloom desligado por padrão, com opção no menu Gráfico) e da [1.3](https://github.com/lagando/MKSM/releases/tag/v1.3).
