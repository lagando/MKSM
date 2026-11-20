# ShaolinMonks RPG

### [💬 Discord oficial](https://discord.gg/YsuSVxjhrq)

**[⬇️ Baixar](#baixar)** · **[Estado](#estado)** · **[Testes das fases](#testes-das-fases)** · **[Requisitos](#requisitos)** · **[Compilar e rodar](#compilar-e-rodar)** · **[Outros lutadores](#outros-lutadores)** · **[Modo RPG](#modo-rpg-opcional)** · **[Organização](#organizacao)** · **[Contribuir](#contribuir)** · **[Licença](#licenca)** · **[Créditos](#creditos)**

*Mortal Kombat: Shaolin Monks* (PS2) recompilado para PC, com modo RPG e lutadores extras.

### [⬇️ Baixar agora (Windows e Linux)](#baixar)

[English](README.md) · **Português (Brasil)**

<a id="baixar"></a>

## ⬇️ Baixar

| Sistema | Download | Tamanho |
|---|---|---|
| **Windows 10/11** | [**ShaolinMonksRPG-Windows.zip**](https://github.com/lagando/MKSM/releases/latest/download/ShaolinMonksRPG-Windows.zip) | 334 MB (traz o kit de compilação) |
| **Linux** | [**ShaolinMonksRPG-Linux.zip**](https://github.com/lagando/MKSM/releases/latest/download/ShaolinMonksRPG-Linux.zip) | 23 MB |

**Descompacte → ponha a ISO do seu jogo em `coloque_o_jogo_aqui` → dois cliques em `Instalar.bat` (Windows) ou `Instalar.sh` (Linux).** A primeira vez compila por algumas horas; depois é só abrir `portatil/…/Jogar.bat` ou `jogar.sh`. Detalhes em [Compilar e rodar](#compilar-e-rodar); todas as versões na [página de Releases](https://github.com/lagando/MKSM/releases).

> O zip não traz o jogo nem um executável pronto: ele compila o jogo no seu PC a partir da sua própria ISO.

O **ShaolinMonks RPG** é uma **recompilação estática** do *Mortal Kombat: Shaolin Monks* de **PlayStation 2** (USA, `SLUS-210.87`) para código nativo de PC, feita com o [**PS2Recomp**](https://github.com/ran-j/PS2Recomp).

Isto **não é um emulador** e **não é um remaster**. O código MIPS do jogo (o ELF) é traduzido para C++ e compilado como programa nativo. O hardware que não é CPU (GS, VU, DMA, IOP, SPU2) continua emulado pelo runtime do PS2Recomp, com as nossas mudanças por cima.

> **Este repositório não contém dados do jogo.** Nenhum ISO, ELF, código gerado, gravação ou arquivo do jogo, e ele não ajuda a obtê-los. Você precisa da sua própria cópia.

## O que o ShaolinMonks RPG traz

- **Modo RPG opcional:** o XP dá níveis e cada nível dá um ponto de atributo (Força, Defesa, Vida, Roubo de vida, Regeneração, Sabedoria). No Ko-op, cada jogador tem o próprio nível. O nível e a barra de XP aparecem no HUD, no estilo do jogo. [Detalhes](#modo-rpg-opcional).
- **67 lutadores na história e no Ko-op:** além dos monges e do Versus, Shao Kahn, Goro, Kano, Shang Tsung, Kintaro, Mileena, Jade, Jax, as versões Boss do Versus, inimigos da história e personagens cortados. Escolha com L2/R2 na tela de dificuldade de um jogo novo. [Detalhes](#outros-lutadores).
- **Todos os lutadores do Versus liberados**, e Sub-Zero e Scorpion já na seleção da história.
- **Ko-op com dois controles:** cada jogador remapeia os próprios botões (R3 no próprio controle), e o direcional para cima do controle 2 liga a defesa contínua.
- **Gráfico de PC:** resolução interna até 4x, tela 16:9, FXAA, tela cheia e um menu com predefinições para PC fraco.
- **Janela própria:** título "Mortal Kombat Shaolin Monks RPG" e o dragão do ícone do save do jogo.
- **Linux e Windows**, com pastas portáteis para levar a outro computador.

## Estado

| Área | Estado |
|---|---|
| Recompilar o ELF (13.651 arquivos C++) e compilar o runtime | ✅ Feito |
| Boot, threads, leitura de disco (CRI ADXF / `GAMEDATA.WAD`) | ✅ Feito |
| Telas de save, menu principal, seleção de personagem | ✅ Feito |
| Primeira fase 3D (Goro's Lair) com HUD, pausa e mapa | ✅ Feito |
| Música e falas (ADX) | ✅ Funcionando |
| GS na GPU (Vulkan) | ✅ Renderizador novo por lotes em 2x por padrão (mipmap por pixel); o antigo, exato, fica em `PS2X_GS_HW=0` |
| VU1 numa thread própria, JIT x86-64 | ✅ Funcionando |
| Velocidade cheia (60 campos/s) | 🟡 Velocidade certa em tudo (sem câmera lenta); numa GTX 1050 a gameplay roda a ~55–60 quadros/s, e nas cenas pesadas o jogo pula quadros (`PLANO_DESEMPENHO.md`, N20; próximos passos na N22) |
| Filmes CRI (FMV) | ✅ Abertura e os 8 filmes tocam; Start pula |
| Memory card (salvar e carregar) | ✅ Funcionando (arquivos em `extraido_full/mc0/`) |
| Som na gameplay | ✅ 48 kHz sem distorção; eco (reverb) do SPU2 aproximado (`PS2X_REVERB`) |
| Som dos filmes | ✅ Tocado direto dos arquivos `.SFD` (sem chiado) |
| Kung Lao como parceiro | ✅ Aparece (era invisível) |
| Resto das fases testadas do início ao fim | 🟡 as 6 primeiras, até Reptile's Lair, 100%; as demais ainda não (ver [Testes das fases](#testes-das-fases)) |
| Build para Windows | 🟡 Compila no próprio Windows com o kit do zip (`Instalar.bat`) ou a partir do Linux (`scripts/build_windows.sh`); já jogado num Windows de verdade, com pequenos engasgos (plano N22 no [`PLANO_DESEMPENHO.md`](PLANO_DESEMPENHO.md)); abre também sem Vulkan (desenho pela CPU) |
| Chão, paredes e raios de luz no Goro's Lair | ✅ Como no original (o VIF agora anda na frente da VU1: o jogo manda as cores de cada lote logo depois de começá-lo) |
| Liu Kang girando parado (uma instrução MMI do R5900 lia o registrador errado) | ✅ Corrigido |
| Momentos pesados da cutscene (chegada do Raiden) | 🟡 Caem para ~35 quadros/s por alguns segundos, nos dois renderizadores (limite do EE/VU1, N20) |
| Lutadores | ✅ Os 8 liberados no Versus, e Sub-Zero e Scorpion na seleção da história (`MKSM_VERSUS_TODOS=0` volta às liberações originais); 67 lutadores (Versus, chefes como Shao Kahn, Goro e Shang Tsung, inimigos e personagens cortados) podem ser escolhidos para um jogo novo de história ou Ko-op com **L2/R2** na tela de dificuldade; a compatibilidade na história ainda está em teste ([`PLANO_PERSONAGENS.md`](PLANO_PERSONAGENS.md)) |
| Tela cheia | ✅ F11 |
| Janela | ✅ Título "Mortal Kombat Shaolin Monks RPG" e o dragão do ícone do save do jogo como ícone |
| Ko-op com dois controles | ✅ Cada jogador remapeia os próprios botões (R3 no próprio controle); defesa contínua no direcional para cima do controle 2 |
| Resolução interna maior | ✅ Renderizador novo em 2x por padrão, tão rápido quanto o antigo; `PS2X_GS_HW=0` volta ao antigo, `PS2X_SCALE=1..4` escolhe a escala |
| Menu de gráfico para PC fraco | ✅ R3 nos menus/pausa (ou F1): resolução interna, 16:9, FXAA, filtro, mistura de campos, renderizador; grava no `mksm.ini` |
| Widescreen | ✅ 16:9 no menu Gráfico ou `PS2X_WIDESCREEN=1` (o 3D ganha campo nas laterais; o 2D/HUD estica, como no PCSX2; filmes ficam 4:3) |
| Modo RPG (níveis e pontos de atributo) | 🟡 Implementado, Single Player e Ko-op, com nível e barra de XP no HUD no estilo do jogo (ver [Modo RPG](#modo-rpg-opcional)); equilíbrio ainda em teste |

Estado detalhado: [`ESTADO.md`](ESTADO.md). Plano de desempenho com medições: [`PLANO_DESEMPENHO.md`](PLANO_DESEMPENHO.md).

## Testes das fases

Quanto de cada fase já foi jogado do início ao fim na recompilação (situação em 19/11/2026). Legenda: ✅ 100% testada, 🟡 testada em parte, ⬜ 0% ainda não testada.

Ordem completa da campanha, com o menu inicial antes dela:

| # | Fase | Progresso |
|---|---|---|
| — | Menu inicial — seleção de personagem e telas de save | ✅ 100% |
| 1 | Goro's Lair — Covil de Goro | ✅ 100% |
| 2 | Wu Shi Academy — Academia Shaolin | ✅ 100% (bug conhecido: em algumas lutas o HUD mostra só o texto) |
| 3 | The Portal — Portal entre os reinos (área central) | ✅ 100% |
| 4 | Evil Monastery — Monastério do Mal | ✅ 100% (bug conhecido: alguns vídeos com o som todo errado) |
| 5 | Living Forest — Floresta Viva | ✅ 100% |
| 6 | Reptile's Lair — Covil de Reptile | ✅ 100% |
| 7 | The Portal — Retorno ao portal central | ⬜ 0% |
| 8 | Soul Tombs — Tumbas das Almas | ⬜ 0% |
| 9 | The Iron Maiden Room — Sala da Donzela de Ferro | ⬜ 0% |
| 10 | The Spiked Ceiling Room — Sala do teto de espinhos | ⬜ 0% |
| 11 | The Flaming Floor — Sala do chão em chamas | ⬜ 0% |
| 12 | The Spinning Fan Room — Sala do ventilador giratório | ⬜ 0% |
| 13 | The Soul Collector Room — Sala do coletor de almas | ⬜ 0% |
| 14 | Wastelands — Terras Desoladas | ⬜ 0% |
| 15 | Sub-Zero's Boat — Embarcação de Sub-Zero | ⬜ 0% |
| 16 | Goro's Arena — Arena do confronto com Goro | ⬜ 0% |
| 17 | The Portal — Retorno ao portal | ⬜ 0% |
| 18 | The Dead Pool — Poço da Morte | ⬜ 0% |
| 19 | Netherrealm — Submundo | ⬜ 0% |
| 20 | Scorpion's Lair — Covil de Scorpion | ⬜ 0% |
| 21 | The Foundry — Fundição | ⬜ 0% |
| 22 | The Prison — Prisão onde estão Kitana e Johnny Cage | ⬜ 0% |
| 23 | Kano's Secret Room — Sala do confronto secreto com Kano | ⬜ 0% |
| 24 | Shao Kahn's Arena — Arena dos chefes finais | ⬜ 0% |
| 25 | Shang Tsung — Primeiro chefe final | ⬜ 0% |
| 26 | Kintaro — Segundo chefe final | ⬜ 0% |
| 27 | Shao Kahn — Chefe final | ⬜ 0% |
| 28 | Ending — Encerramento | ⬜ 0% |

Áreas extras e opcionais (visitadas durante a exploração; nenhuma testada ainda):

- The Warrior Shrine — ⬜ 0%
- The Armory — ⬜ 0%
- The Pit — ⬜ 0%
- The Rooftops — ⬜ 0%
- The Tower — ⬜ 0%
- Ice Falls — ⬜ 0%
- Kombat Tomb — ⬜ 0%
- Wu Shi Shrine — ⬜ 0%

## Testes dos lutadores

Quais lutadores conseguem zerar a história do início ao fim. A meta é deixar todos em 100%.

| Lutador | Grupo | Zera a história? |
|---|---|---|
| Liu Kang | Monge / história | ✅ 100% confirmado |
| Kung Lao | Monge / história | ✅ 100% confirmado |
| Sub-Zero | Monge / história | ✅ 100% confirmado |
| Scorpion | Monge / história | ✅ 100% confirmado |
| Johnny Cage | Versus | 🟡 em teste (chegou ao Reptile's Lair, com os portões de fatality, Multality e Brutality abertos) |
| Kitana | Versus | ⬜ ainda não testado |
| Reptile | Versus | ⬜ ainda não testado |
| Baraka | Versus | ⬜ ainda não testado |
| Shao Kahn | Outros | 🟡 em teste (até a sala depois da fatality; trava no portão do arremesso) |
| Goro | Outros | ⬜ ainda não testado |
| Goro (outro) | Outros | ⬜ ainda não testado |
| Kano | Outros | ⬜ ainda não testado |
| Shang Tsung | Outros | ⬜ ainda não testado |
| Inferno Scorpion | Outros | ⬜ ainda não testado |
| Ermac | Outros | ⬜ ainda não testado |
| Kintaro | Outros | ⬜ ainda não testado |
| Kintaro (outro) | Outros | ⬜ ainda não testado |
| Mileena | Outros | ⬜ ainda não testado |
| Jade | Outros | ⬜ ainda não testado |
| Jax | Outros | ⬜ ainda não testado |
| Scorpion Boss | Outros | ⬜ ainda não testado |
| Baraka Boss | Outros | ⬜ ainda não testado |
| Reptile Boss | Outros | ⬜ ainda não testado |
| Kitana Boss | Outros | ⬜ ainda não testado |
| Sub-Zero Boss | Outros | ⬜ ainda não testado |
| Johnny Cage Boss | Outros | ⬜ ainda não testado |
| Shadow Priest | Outros | ⬜ ainda não testado |
| Orochi Hellbeast | Outros | ⬜ ainda não testado |
| Red Dragon Merc | Outros | ⬜ ainda não testado |
| Mask Guard | Outros | ⬜ ainda não testado |
| Black Dragon Merc | Outros | ⬜ ainda não testado |
| Shokan | Outros | ⬜ ainda não testado |
| Oni Warlord | Outros | ⬜ ainda não testado |
| Demon Captain | Outros | ⬜ ainda não testado |
| Demon General | Outros | ⬜ ainda não testado |
| Demon Archer | Outros | ⬜ ainda não testado |
| Demon Handler | Outros | ⬜ ainda não testado |
| Tarkata | Outros | ⬜ ainda não testado |
| Stone Johnny Cage | Outros | ⬜ ainda não testado |
| Bloody Skeleton | Outros | ⬜ ainda não testado |
| Putrid Corpse | Outros | ⬜ ainda não testado |
| Undead Corpse | Outros | ⬜ ainda não testado |
| Elemental Knight | Outros | ⬜ ainda não testado |
| Brother of Shadow | Outros | ⬜ ainda não testado |
| Green Tarkata | Outros | ⬜ ainda não testado |
| Living Tree Arms | Outros | ⬜ ainda não testado |
| Banshee | Outros | ⬜ ainda não testado |
| Living Tree Mouth | Outros | ⬜ ainda não testado |
| Oni | Outros | ⬜ ainda não testado |
| Female Ninja | Outros | ⬜ ainda não testado |
| Ninja Wraith | Outros | ⬜ ainda não testado |
| Chan Chan | Outros | ⬜ ainda não testado |
| Nagiri Swamp | Outros | ⬜ ainda não testado |
| Oni de Fogo | Outros | ⬜ ainda não testado |
| Oni de Chifre | Outros | ⬜ ainda não testado |
| Noob Saibot | Outros | ⬜ ainda não testado |
| Smoke | Outros | ⬜ ainda não testado |
| Raiden | Outros | ⬜ ainda não testado |
| Kabal | Outros | ⬜ ainda não testado |
| Sonya | Outros | ⬜ ainda não testado |
| Shang Tsung (outro) | Outros | ⬜ ainda não testado |
| Ermac (Versus) | Outros | ⬜ ainda não testado |
| Burning Monk | Outros | ⬜ ainda não testado |
| Order of Light Monk | Outros | ⬜ ainda não testado |
| Armor Lion | Outros | ⬜ ainda não testado |
| Homem de terno | Outros | ⬜ ainda não testado |
| Shiva | Outros | ⬜ ainda não testado |

<a id="requisitos"></a>

## O que você precisa

- Sua própria cópia do jogo (PS2, USA, `SLUS-210.87`).
- **Windows 10 ou 11 (64 bits):** nada a instalar. O zip do Windows traz o kit de compilação (GCC, CMake e Ninja).
- **Linux 64 bits:** o compilador e as bibliotecas de desenvolvimento do sistema (`g++`, `cmake`, `ninja`, `7z`, `glslc`, Vulkan, FFmpeg, X11). O `Instalar.sh` instala o que faltar no Arch, Ubuntu/Debian ou Fedora, pedindo a senha uma vez.
- **Processador:** x86-64 de 64 bits com SSE4.1 e 4 núcleos ou mais (com menos funciona, mas fica abaixo de 60 nas cenas pesadas). Não precisa de AVX/AVX2.
- **Placa de vídeo com Vulkan 1.1:** NVIDIA desde a GTX 600, AMD desde a HD 7000, Intel HD 620 em diante (no Linux, a Intel HD 4000 tem um Vulkan incompleto que erra o desenho). Sem Vulkan, o jogo abre e desenha pela CPU, bem mais devagar.
- **Para compilar:** 8 GB de RAM, ~20 GB livres no disco e algumas horas na primeira vez (o código gerado são 13 mil arquivos). Para jogar, ~1,5 GB de RAM bastam.

> **Não há executável pronto para baixar, e não vai haver.** O executável contém o código do jogo traduzido, então distribuí-lo seria distribuir o jogo. Cada pessoa compila a partir da própria cópia com o script abaixo.

## Compilar e rodar

**Do jeito mais simples:**

1. Baixe o zip do seu sistema na [página de **Releases**](https://github.com/lagando/MKSM/releases/latest) (`ShaolinMonksRPG-Linux.zip` ou `ShaolinMonksRPG-Windows.zip`: trazem tudo para compilar sem internet) e descompacte. No Linux, o repositório puro também serve (`git clone`; aí o instalador baixa o que faltar); no Windows, use o zip, que traz o kit de compilação.
2. Ponha a ISO do seu jogo (PS2, USA, `SLUS-210.87`) na pasta `coloque_o_jogo_aqui`.
3. Dê dois cliques no instalador:
   - **Linux:** `Instalar.sh`. Ele instala o que faltar (pede a senha uma vez), compila e monta `portatil/MKSM-linux`.
   - **Windows:** `Instalar.bat`. Compila no próprio Windows com o kit que vem no zip do Windows (GCC, CMake e Ninja, na pasta `ferramentas`), **sem instalar nada, sem administrador e sem internet**, monta `portatil\MKSM-windows` e, no fim, apaga o kit e os arquivos intermediários.
4. Jogue: dois cliques em `jogar.sh` (Linux) ou `Jogar.bat` (Windows) dentro da pasta `portatil`.

A primeira compilação leva algumas horas. Se parar no meio, rode o instalador de novo: as etapas prontas são puladas. A pasta `portatil` leva o seu jogo: é só para você.

**Passo a passo manual** (o que os instaladores fazem por baixo):

Um comando extrai a ISO, baixa o PS2Recomp no commit fixado, aplica os nossos patches, recompila o ELF e compila tudo:

```bash
scripts/instalar.sh "caminho/da/sua.iso"
```

Use `JOBS=1` se faltar memória. Se parar no meio, rode de novo: as etapas prontas são puladas.

Opcional, no Linux: `scripts/pgo_runtime.sh` treina o runtime com o seu jogo (~15 min, abre a janela sozinho) e recompila com o perfil. A imagem fica idêntica, e a VU1 e o desenho ficam ~10% mais leves.

Para jogar:

```bash
scripts/jogar.sh
```

A janela se chama "Mortal Kombat Shaolin Monks RPG" e usa como ícone o dragão do ícone do save do jogo (`mc0/BASLUS-21087/game.icn`, criado no primeiro save; sem ele, fica o ícone padrão). Ele lê configurações opcionais do `mksm.ini` na raiz do projeto (uma `VARIAVEL=valor` por linha; a lista está no começo do [`scripts/jogar.sh`](scripts/jogar.sh)). Teclas: **F11** tela cheia, **F10** captura de tela (`capturas/`), **F9** grava a cena atual para relatar defeito (`gsrec/`; são dados do jogo, não publique). **Menu Gráfico:** **R3** no controle nos menus, na pausa do Select e no mapa, ou **F1** a qualquer hora (resolução interna, 16:9, FXAA, filtro de ampliação, mistura de campos, renderizador; grava no `mksm.ini`). **F12** abre o painel de depuração.

**Pastas portáteis:** depois do build, `scripts/empacotar_linux.sh` monta `portatil/MKSM-linux` (roda em outro Linux sem instalar nada) e `scripts/build_windows.sh` monta `portatil/MKSM-windows` (dois cliques no `Jogar.bat`). Elas levam os arquivos do seu jogo: são só para você.

**Notebook com duas placas de vídeo:** sem isso a janela pode abrir na placa integrada. Rode com `prime-run` na frente (ou `__NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia`).

**Controle:** um controle comum (USB/Bluetooth) funciona; o analógico tem zona morta de 25% (`PS2X_DEADZONE` no `mksm.ini`). Cada jogador remapeia os próprios botões na aba **Controles**: o jogador 1 com F1/R3, o jogador 2 apertando o **R3 do segundo controle** (útil para controles genéricos, que mapeiam os botões diferente; fica como `PS2X_PAD2_*`). No segundo controle, o **direcional para cima** liga e desliga a defesa contínua (como segurar L2), para uma pessoa jogar o Ko-op com um controle só.

Variáveis de ambiente úteis (todas no [`ARQUITETURA.md`](ARQUITETURA.md) §6):

| Variável | Efeito |
|---|---|
| `MKSM_SKIP_INTRO=1` | Pula os vídeos de abertura |
| `PS2X_VOL_MASTER`, `PS2X_VOL_MUSIC`, `PS2X_VOL_SFX` | Volumes, de 0 a 100 |
| `PS2X_REVERB=22` | Eco nos efeitos sonoros (0 desliga) |
| `PS2X_FXAA=1` | Suavização de bordas (ligada por padrão no Windows; no Linux vem desligada, que rende mais quadros) |
| `PS2X_GS_HW=0` | Renderizador antigo (resolução do PS2, exato) no lugar do novo |
| `PS2X_SCALE=1` | Resolução interna do renderizador novo, 1 a 4 (padrão 2; 1 para placa fraca) |
| `PS2X_WIDESCREEN=1` | Tela 16:9 (3D mais largo; o 2D/HUD estica; filmes ficam 4:3) |
| `PS2X_RUMBLE=0` | Sem vibração no controle |
| `PS2X_DEINTERLACE=1` | Mistura os dois campos (ligada por padrão no Windows; no Linux vem desligada, que alivia o processador) |
| `PS2X_VSYNC=1` | Sincronia vertical: a imagem troca junto com o monitor (contra engasgo com o jogo a 60; também no menu Gráfico) |
| `PS2X_UNCAPPED=1` | Sem trava de tempo real (medir vazão; o jogo acelera) |
| `PS2X_PERF=1` | Mostra fps, carga do GS e da GPU e para onde vai o tempo de cada quadro, a cada 2 s |
| `PS2X_PIN=0` | Não fixa as quatro threads pesadas em núcleos físicos separados (Linux e Windows) |
| `PS2X_GS_GPU=0` | Com `PS2X_GS_HW=0`: o renderizador antigo só na CPU |
| `PS2X_GS_RASTER=0` | Tiles em compute na GPU em vez do rasterizador |
| `PS2X_VU1_THREAD=0` | VU1 na thread do jogo |

## Outros lutadores

- **Liberados:** os 8 lutadores do Versus (Liu Kang, Kung Lao, Sub-Zero, Scorpion, Baraka, Reptile, Kitana, Johnny Cage), e Sub-Zero e Scorpion na seleção da história (o jogo normalmente os libera depois de zerar com cada monge). `MKSM_VERSUS_TODOS=0` no `mksm.ini` volta às liberações originais.
- **História e Ko-op com outro lutador:** num **jogo novo**, na tela *Select Difficulty*, cada jogador aperta **L2/R2** no próprio controle (**Shift esquerdo/direito** no teclado, jogador 1; segurado, passa sozinho) para escolher um dos 67 lutadores, na ordem da tabela abaixo. A escolha fica no save e não muda depois, para os golpes comprados nunca se misturarem entre personagens. Na história, os especiais são **R1 + um botão**.

| Grupo (na ordem da lista) | Lutadores | Situação |
|---|---|---|
| Monges e Versus | Liu Kang, Kung Lao, Sub-Zero, Scorpion, Kitana, Johnny Cage, Baraka, Reptile | Golpes, especiais e fatalities (são os únicos com animação de fatality nos dados do jogo) |
| Com especiais | Shao Kahn, Goro (dois), Kano, Shang Tsung, Inferno Scorpion, Ermac (o chefe), Kintaro (dois) | Golpes e especiais próprios; o Goro fica de braços abertos quando parado; o Shang Tsung não volta se sumir |
| Só golpes normais | Mileena, Jade, Jax, as versões de história do Versus (Scorpion Boss, Baraka Boss, Reptile Boss, Kitana Boss, Sub-Zero Boss, Johnny Cage Boss), Shadow Priest, Orochi Hellbeast, os inimigos da história (Mercs, Mask Guard, Oni Warlord, Demons, Tarkata, Corpses, Elemental Knight, Living Trees...) e alguns cortados (Shokan, Green Tarkata, Banshee) | Atacam, sem especial; Mileena e Jade se movem perfeitamente; os Corpses explodem e somem |
| Golpes, mas bugados | Oni (falha ao bater e correr), Female Ninja, Ninja Wraith, Chan Chan, Nagiri Swamp, Oni de Fogo, Oni de Chifre | Atacam, com animação quebrada |
| Sem golpes | Noob Saibot, Smoke, Raiden, Kabal, Sonya, o outro Shang Tsung, Ermac (do Versus), Burning Monk, Order of Light Monk, Armor Lion, homem de terno, Shiva | Só andam |

- **Ko-op: recomendado que um dos jogadores use um dos 4 clássicos** (Liu Kang, Kung Lao, Sub-Zero ou Scorpion). Alguns trechos da história pedem Brutality ou Multality, que só esses quatro fazem com certeza.
- **Portões de Multality e Brutality:** quem não tem Multality ou Brutality (Johnny Cage e outros) passa pelos portões "faça uma Multality/Brutality" digitando as setas; o jogo cancela o golpe, mas o portão abre.
- **Portões de fatality:** quando nenhum jogador tem fatality (nenhum monge nem lutador do Versus na partida), os portões "faça uma fatality" da história abrem sozinhos. O Shao Kahn também causa 2,5× de dano e leva 0,7× na história.
- **Defesa (L2):** é a do próprio lutador. O Shao Kahn defende normalmente; quem não tem defesa nos dados de golpes simplesmente não defende.
- **Fora da lista:** quem quebra a câmera, não fica de pé ou trava (bichos, árvores, chefes gigantes e alguns cortados). Ficam anotados no [`PLANO_PERSONAGENS.md`](PLANO_PERSONAGENS.md).

- **Ainda não é perfeito:** os lutadores do Versus foram feitos para luta 1 contra 1. A loja de golpes, as fatalities da história e as cutscenes (que continuam mostrando os monges) podem não casar. Dar golpes a quem não tem (e consertar a animação parada do Goro) exige misturar os pacotes de dados de dois personagens, o que ainda não foi descoberto. Situação, números dos personagens e a fórmula de dano do jogo: [`PLANO_PERSONAGENS.md`](PLANO_PERSONAGENS.md).
- **Teste:** a pasta portátil do Windows traz o `TESTAR.txt`, um roteiro passo a passo (RPG, lutadores, Ko-op, Versus, saves).

## Modo RPG (opcional)

Um modo opcional, escolhido só ao começar um jogo novo, em que a experiência dá **níveis** e cada nível dá um **ponto de atributo**. A economia do jogo não muda: os inimigos dão o mesmo XP e os golpes custam o mesmo.

- **Ligar:** Single Player ou Ko-op → personagem → na *Select Difficulty*, cursor no **Hard** e aperte **L3** (**F2** no teclado). Aparece "RPG" dourado ao lado do Hard. O RPG é sempre no Hard, com inimigos mais fortes no começo (+30% de dano e +30% de vida): o começo é mais duro que o Hard e vai ficando mais fácil conforme você evolui.
- **Níveis:** o nível sai de todo o XP ganho (comprar golpe não tira nível). Do 1 para o 2 custa 400 de XP, depois 500, 650, 850... (cada degrau custa +100 até o nível 10, +200 do 10 ao 29 e +300 do 30 em diante), até o nível 100. O nível, uma barra do XP até o próximo e o número (`120/550`) aparecem acima do "EXP", no estilo do HUD do jogo; um "+N" verde pisca quando há pontos para distribuir. No Ko-op, um para cada jogador.
- **Morrer:** ao morrer e continuar, você perde o XP ganho desde o último checkpoint (o mesmo que o jogo tira do "EXP"), mas **nunca perde nível**: a perda para no começo do nível atual. Voltar a uma fase e ganhar XP de novo dos mesmos inimigos continua valendo. Carregar um save também volta o RPG para o que estava salvo. `RPG_PERDE_XP_AO_MORRER=0` desliga a perda.
- **Atributos** (1 ponto por nível, até 20 em cada e 5 na Sabedoria): Força (+5% de dano causado), Defesa (−3% de dano recebido), Vida (+5% de vida máxima), Roubo de vida (cura 1% do dano causado), Regeneração (0,1% da vida máxima por segundo), Sabedoria (+3% de XP para o nível). A velocidade nunca muda: golpes, combos e colisões ficam os originais.
- **Distribuir:** pausa (Select) → **R3** (ou **F1**) → aba **RPG** (L1/R1 trocam de aba). X gasta um ponto; "Redistribuir pontos" (X duas vezes) devolve todos. No Ko-op, cada jogador vê só o próprio nível e pontos: o jogador 2 abre a janela com o R3 do segundo controle.
- **Saves:** um save feito no modo RPG continua RPG. Na tela *Load Game* ele aparece com a tag "RPG · Nível N". Os dados do RPG ficam ao lado do `mksm.ini`, em `rpg_mc0_slot<n>.ini` (um por slot); o save do memory card não muda e abre sem o modo. No backup, copie esses arquivos junto com o `mc0/`.
- **Ajustes:** todos os números (curva de XP, efeito por ponto, tetos, força dos inimigos) são chaves `RPG_*` do `mksm.ini` (lista no começo do [`scripts/jogar.sh`](scripts/jogar.sh)). Com o modo desligado, o jogo roda igual ao original.
- Desenho, endereços e o que já foi testado: [`PLANO_RPG.md`](PLANO_RPG.md).

<a id="organizacao"></a>

## Como o repositório é organizado

O PS2Recomp **não é copiado para cá**. O `scripts/aplicar_patches.sh` clona-o no commit de [`patches/UPSTREAM_COMMIT`](patches/UPSTREAM_COMMIT) e aplica o [`patches/runtime_atual.diff`](patches/runtime_atual.diff) mais os arquivos novos listados em [`patches/arquivos.txt`](patches/arquivos.txt). A pasta `PS2Recomp/` resultante é ignorada pelo git: você trabalha nela e roda `scripts/atualizar_patches.sh` para trazer as mudanças de volta para `patches/`.

| Caminho | O que é |
|---|---|
| `sm.toml` | Configuração do PS2Recomp para este jogo (stubs, overrides, laços ociosos) |
| `patches/` | Nossas mudanças no runtime, IOP, recompilador e testes do PS2Recomp |
| `scripts/` | Compilação, patches e testes |
| [`ARQUITETURA.md`](ARQUITETURA.md) | Arquitetura: threads, locks, o caminho de um quadro, tempo, armadilhas do jogo, testes sem bootar o jogo. **Comece por aqui.** |
| [`PLANO_DESEMPENHO.md`](PLANO_DESEMPENHO.md) | O plano ativo: desempenho, imagem, tarefas em aberto, medições |
| [`PLANO_RPG.md`](PLANO_RPG.md) | O modo RPG opcional: desenho, endereços do jogo, o que foi testado |
| [`PLANO_PERSONAGENS.md`](PLANO_PERSONAGENS.md) | Lutadores extras: liberação, números dos personagens, fórmula de dano, o que funciona na história |
| [`PLANO.md`](PLANO.md) | Histórico: o diário de investigação até 13/11, com a evidência de cada correção |
| [`MODDING.md`](MODDING.md) | Endereços e formatos do jogo (level select, dados das fases, bloom...) |
| [`ESTADO.md`](ESTADO.md) | Estado técnico e passo a passo manual |
| `notas/` | Notas de investigação |

## Contribuir

Veja o [`CONTRIBUTING.md`](CONTRIBUTING.md). Em resumo: nunca envie dados do jogo, mantenha o renderizador bit a bit exato (há testes de replay para isso) e mantenha a documentação em dia com o código.

<a id="licenca"></a>

## Licença

[GPL-3.0](LICENSE), a mesma do PS2Recomp, de onde `patches/` deriva.

<a id="creditos"></a>

## Créditos

- [PS2Recomp](https://github.com/ran-j/PS2Recomp), de ran-j e colaboradores: o recompilador e o runtime sobre os quais este projeto é feito.
- *Mortal Kombat: Shaolin Monks* © Midway / Warner Bros. Interactive Entertainment. Projeto de fãs, sem fins lucrativos e sem afiliação com os detentores dos direitos.
