# Plano: lutadores do Versus jogáveis na história

**Pedido do usuário (16/11):**
- Investigar tudo do modo Versus.
- Ver se dá para pôr todos os lutadores do Versus como jogáveis, e fazer o plano completo.
- No Versus, os lutadores liberam com o tempo de jogo: o usuário pediu para deixar **todos liberados** no repositório.

**Como usar este plano:** mesmo formato do `PLANO_RPG.md` e do `PLANO_DESEMPENHO.md`. Faça as tarefas da seção 5 na ordem, uma por vez (Faça / Verifique / Pronto quando). Ao terminar uma tarefa, marque `[x]` e anote os endereços e números medidos. Antes, leia o `ARQUITETURA.md` e o `MODDING.md` §5.

## 1. Regras

1. **Arquivos do jogo intactos:** nada muda no WAD, no ELF nem no memory card por fora. Tudo fica no runtime (trocas de função, escritas na RAM).
2. **Sem a opção ligada, o jogo é o original:** a escolha de um lutador do Versus na história é opcional. A liberação do Versus fica ligada por padrão, porque o usuário pediu; `MKSM_VERSUS_TODOS=0` desliga.
3. **Regras do projeto** (`CLAUDE.md`):
   - `PS2Recomp/` só por `scripts/atualizar_patches.sh`.
   - Nenhum header novo no `ps2_runtime.h`.
   - Diagnósticos temporários saem antes do commit.

## 2. O que já sabemos (investigação de 16/11)

| O quê | Onde / como |
|---|---|
| Lutadores do Versus | 9 na seleção. Visto liberado: Baraka, Kitana (ou Mileena), Sub-Zero, Kung Lao, Liu Kang, Scorpion, Johnny Cage e Reptile, mais um lugar a conferir. Num jogo novo, só Liu Kang e Kung Lao vêm liberados |
| Liberação | Lista de bits do Konten em `0x5D8280` (o save a guarda). `CharLocked(n)`, a consulta do menu (tratador em `0x1C1FE8`), testa o bit `n`. `unlockchar(n)` (`func_1C1A20`) liga o bit `n`. Outras categorias usam bases de bit: 92, 101, 166, 343 e 360 |
| Bits dos lutadores | Bits 80–87 (palavra `0x5D8288`, máscara `0x00FF0000`): Baraka, Scorpion e Reptile. Bits 88–91 (máscara `0x0F000000`): Kitana/Mileena, Sub-Zero e Johnny Cage |
| **Feito: todos liberados** | `ps2_runtime.cpp` liga `0x0FFF0000` em `0x5D8288` sempre que falta algum bit (`MKSM_VERSUS_TODOS=0` desliga). Testado: seleção com todos coloridos e luta Johnny Cage × Kung Lao jogável. Captura em `capturas/versus_todos.png` |
| Número do personagem | Halfword em `estrutura do jogador + 0x1E0E`. Jogador 1 em `0x5E6B40`, jogador 2 em `0x5E9F00` (`0x5E3780 + 0x33C0 × n`; `func_283B50(0x67C590, n)` devolve o ponteiro). Liu Kang = 0, Kung Lao = 1, Johnny Cage = 90: **o número é o mesmo bit que libera o lutador** |
| Seleção da história | 4 lugares: Liu Kang, Kung Lao, Sub-Zero e Scorpion. Os dois últimos não abrem com bit nenhum do Konten (testado com os bits 0–127): a história usa outra marca para eles |
| **Teste-chave:** Johnny Cage forçado na história | Escrevendo 90 em `0x5E6B40+0x1E0E` antes do Goro's Lair carregar, o HUD mostra "JOHNNY CAGE", mas o corpo é o do Liu Kang, que faz poses estranhas. Conclusão: o nome e parte do comportamento seguem o número, mas o **modelo e os dados do personagem são escolhidos antes e em outro lugar**. Captura em `capturas/personagem_jc_na_historia.png` |
| Outros fios | `unlockendofgame` (`func_1C21B8`) lê o personagem do jogador e liga o bit 82 (Liu) ou 88 (Kung Lao): o fim de jogo de cada monge libera um lutador. `unlockmk2` grava `[0x5D82B0] = 1`. Strings: `CHARACTER_MEMORY`, `CUSTOM_CHARACTER_MEMORY`, `Setup_MainCharacter`, `Process_Character_Data`, `New_Process_Character` |

## 2b. Avanço de 16/11, à noite (prova feita)

- **O personagem da fase vem do save:** `save+0xCC` (jogador 1) e `save+0xD0` (jogador 2), lidos por `func_1CEE00` ao montar a sala, que chama `func_2F6BA0(número, jogador)`. Esta grava `+0x1E0E` e `+0x2AAC` da estrutura do jogador e as listas `0x6F5D98`/`0x6F5DD0`. Trocar só o `+0x1E0E` muda o nome no HUD, mas não o corpo.
- **Prova:** com `save+0xCC = 90`, o Johnny Cage entra no Goro's Lair com o próprio modelo, anda e tem o nome no HUD.
- **Números (cada um carregado no Goro's Lair):**

| Número | Personagem | Número | Personagem |
|---|---|---|---|
| 0 | Liu Kang | 85 | Ermac (não está no Versus) |
| 1 | Kung Lao | 88 | Sub-Zero |
| 82 | Scorpion | 89 | Kitana |
| 83 | Baraka | 90 | Johnny Cage |
| 84 | Reptile | 81 | gigante de quatro braços (Goro/Kintaro) |
| 2, 3, 80, 86, 87 | inimigos genéricos ou sem nome claro | 91 | trava no carregamento |

- **Varredura de 16/11 (números 0–30 e 80–110 carregados no Goro's Lair, apertando quadrado, triângulo, bola e X):**
  - **Respondem com os próprios golpes:** Sub-Zero (88), Scorpion (82), Johnny Cage (90), Reptile (84), Baraka (83) e Kitana (89); também o gigante de quatro braços (81), o encapuzado Shadow Priest (27, magias rosa e verde) e o demônio de lava (30, fogo e explosão).
  - **Carregam, mas ficam parados de braços abertos (sem golpes de jogador):** Ermac (85), um Liu Kang alternativo (5), um homem de terno (6) e uma mulher de roupa rosa (23).
  - **Inimigos genéricos (7–22, 24–26, 28, 29):** monges, ninjas, esqueletos, demônios vermelhos, macaco branco. Não testados no controle.
  - **4:** tela preta. **91 e 92–110:** travam no carregamento.
  - **Especiais (L1 + botão)** não testados: o roteiro não aperta L1.
  - **Personagem secreto do Versus:** andando o cursor pela seleção, só aparecem os 8 conhecidos.
- **Golpes e especiais (16/11, roteiro com os ombros: `1` L1, `2` R1, `3` L2, `4` R2 no `MKSM_AUTO_HOLD`):** na história, os especiais são R1 + botão. Testados com o próprio efeito: Johnny Cage (bola verde, chute de sombra vermelho), Sub-Zero (gelo no chão, deslize), Scorpion (fogo, golpe que explode no chão).
- **Lista de golpes (Start > Moves):** para os lutadores do Versus, ela abre com nomes genéricos ("Special 1–5") e descrições dos monges. A loja e a lista da história são tabelas dos monges: comprar golpe com um lutador do Versus é a maior incógnita.
- **Fatalities:** não dá para testar por roteiro (precisa da barra cheia, de um inimigo e do comando certo). Ficam no roteiro de teste do usuário (`TESTAR.txt` da pasta do Windows).
- **Experimentais usando golpes de outro (pedido do usuário):** exige achar onde fica a tabela de golpes e animações de cada personagem e apontá-la para outra. Ermac, 5, 6 e 23 não têm golpes de jogador; Goro, Shang Tsung e o demônio já atacam com os próprios golpes. Próxima tarefa: **C4**.
- **Verificação de 16/11, à tarde:**

| Personagem | Carrega | Golpes | Especiais (R1 + botão) | Situação |
|---|---|---|---|---|
| Liu Kang, Kung Lao | sim | sim | sim | originais (o usuário confirmou jogando) |
| Sub-Zero (88), Scorpion (82) | sim | sim | sim (gelo, deslize; fogo) | o usuário confirmou jogando |
| Johnny Cage (90) | sim | sim | sim (bola verde, chute de sombra) | lista de golpes genérica |
| Reptile (84), Baraka (83), Kitana (89) | sim | sim | não testados um a um | provável igual ao Johnny Cage |
| Goro (81), Shadow Priest (27), demônio de lava (30) | sim | sim (próprios) | — | experimentais: grandes, câmera e colisão estranhas |
| Ermac (85), "NULL (ARMOR LION)" (5), "NULL (NINJA)", o homem de terno (6), "NULL (SHIVA)" (23) | sim | **não** | — | sem golpes de jogador; 6 e 23 ficam de braços abertos (sem animação). Com um "parente" no Ko-op (Scorpion, Sub-Zero, Kitana, Johnny Cage, Liu Kang) continuam iguais |

  - **Luta na história:** a luta do Goro's Lair dispara no Ko-op com dois pulsos para a direita (64 s e 76 s) e X para a janela do Raiden. Os inimigos atacam e o Desafio RPG vale no dano recebido. O roteiro não mira, então quase nunca acerta; os golpes que acertam chegam com dano 0 porque cada tipo de golpe passa pela armadura do inimigo. Matar e ganhar XP por roteiro não deu; fica para o teste do usuário.
  - **Dano do combate:** `func_1CBF48(atacante, vítima, base)` faz base × 1,3 no co-op (`+0x280`), desconta a armadura da vítima por tipo de golpe (tabela de `func_2B92C8`, índice de `jogador+0x27D4`), multiplica pela força do atacante (`obj+0x3C0` + `+0x1C8`, sobre 100) e tira a defesa (`+0x3C2`...). Depois chama `func_284FE0(vítima, dano, atacante)`, que é onde o RPG atua. Desde esta data, a Força e o Roubo de vida no co-op são de quem bateu.
  - **Na lista desde 16/11, à noite (o usuário quer vê-los no jogo):** Ermac e os cortados entram na escolha do jogo novo como "(sem golpes)". Carregam e andam pela sala (o Ninja e a Shiva deslizam de braços abertos), mas não atacam.
  - **Golpes emprestados (C4):** o conjunto de golpes e animações vem do pacote de dados de cada personagem, carregado inteiro pelo número. Trocar o `+0x2AAC` (só vira o tipo do objeto) ou pôr um parente na fase não ajuda. O caminho é achar o carregador do pacote (`New_Process_Character`, `CHARACTER_MEMORY`) e fazer o Ermac carregar as animações e golpes de outro. É trabalho de investigação longa, sem garantia de que o esqueleto aceite.
- **17/11, depois do teste do usuário:** nomes certos: o 81 é o **Kintaro**, o 27 é o **Shadow Priest** e o 6 é o **homem de terno** e o 30 é o **Orochi**. A defesa (L2) funciona nos lutadores do Versus, mas não nos experimentais: ela vem do pacote de golpes de cada personagem, e os chefes não têm a defesa de jogador. Com L2 segurado, o dano cai para 20% (`hookDamage`, só nos experimentais), mas o jogo não conta como defesa, e trechos que pedem defender não avançam. **18/11:** o usuário viu o Shao Kahn defendendo de verdade (e passando trechos), então a defesa por software (L2 → 20%) saiu: ela se somava à defesa real de quem tem. Pista para a defesa de verdade: com o Liu Kang segurando L2, `jogador+0x264/0x268/0x26C/0x278` viram 1 (parece o registro do botão; a defesa só entra com um golpe chegando).
- **Feito: escolha só no jogo novo** (o usuário pediu, para não bagunçar golpes comprados e fatalities; a aba do meio do jogo saiu). Na tela de dificuldade de um jogo novo, L1/R1 no controle de cada jogador (Q/E no teclado) trocam o lutador: Liu Kang, Kung Lao, Sub-Zero, Scorpion, Baraka, Reptile, Kitana, Johnny Cage, e Goro, Shang Tsung e o demônio de lava (experimentais; Ermac saiu: não obedece ao controle). "O da seleção" é o original. A troca de `func_1CEE00` grava `save+0xCC`/`0xD0` quando a primeira sala é montada (antes disso o jogo ainda reescreve). Depois, o lutador fica no save do jogo. Testado: Ko-op novo com Sub-Zero e Baraka no Goro's Lair. Roteiro de teste: `MKSM_TEST_LUTADOR=88,83`.
- **A seleção da história continua com 4 lugares**, e Sub-Zero e Scorpion seguem bloqueados nela: não abrem com nenhum bit do Konten (bits 0–511 testados). A aba Personagens resolve sem isso.
- **Falta (Fase C):** jogar cada lutador numa fase inteira (golpes, armas, loja de golpes, chefe, cutscenes). Só o andar e o modelo foram vistos.

## 2c. Todos os números, testados (17/11)

- **Nomes:** o jogo tem uma tabela de nomes por número, 0x80 bytes cada (só o texto), em `0xC6E500` na RAM durante a fase (cópia da RAM `fe3`, Goro's Lair; carregada com a fase, então o endereço pode mudar). O 64 é **Shao Kahn**, 47 e 57 **Goro**, 54 Kano, 66 Mileena, 67 Jade, 74 Jax, 15 Oni, 36 Oni Warlord, 53 Noob Saibot, 70 Smoke, 72 Raiden, 73 Kabal, 75 Sonya, 62/63 Shang Tsung, 55 Inferno Scorpion, 58/81 Kintaro, 68 Ermac (chefe). Entre 52 e 77 estão as versões de história dos lutadores do Versus (52 Scorpion, 56 Baraka, 59 Reptile, 65 Kitana, 69 Sub-Zero, 71 Johnny Cage): iguais aos 82–90 no teste do usuário; entram na lista como "Scorpion Boss" etc., depois do Jax. 91 em diante trava.
- **Fatality:** só existem animações para Liu Kang (`liu1_fat`, `liu2_fat`), Kung Lao (`kun1..5_fat`), Sub-Zero (`sub1..3`), Scorpion (`sco1..2`), Reptile (`rep1..3`), Kitana (`kit1..2`), Johnny Cage (`cag1..3`) e Baraka (`bar1..2`), nos `GAMEDATA.WA?`. Os chefes só têm as cenas em que matam os monges (`s_ermac_boss_fatality_lk/kl`, `_baraka_boss_fatality`). O comando de roteiro `setfatalitylevel` (registrado em `func_1280B8`, tratado em `func_128308`) soma ao nível de fatality de cada jogador: inteiro em `save+0x4BE0+(j−1)×0x60` (`func_1D9EA0`/`func_1D9EE8`) e um float em `save+0x4BB8+(j−1)×0x60` (`func_1D9E08`/`func_1D9E50`).
- **Teste:** primeiro um roteiro que repete uma gravação do usuário (pega a esfera azul à direita do começo do Goro's Lair, Start pula a cena e a luta começa) e conta os acertos do jogador no `func_284FE0`; depois o próprio usuário, um por um, contando golpes normais e especiais. Com dois jogos abertos ao mesmo tempo, ~1 em 4 travou no carregamento (`guest-branch:missing-target` com o EE pulando para dados, `0x1DFAF80`/`0x1DAD60C`); com um jogo só, quase nunca.

| Grupo | Lutadores (número: golpes + especiais) |
|---|---|
| Com especiais | Shao Kahn 64 (3+3), Goro 47 e 57 (3+3; parado de braços abertos: o pacote não tem animação parada de jogador), Kano 54 (3+3), Shang Tsung 62 (3+4; se some no teleporte, não volta), Inferno Scorpion 55 (3+3), Ermac chefe 68 (3+1), Kintaro 81 (4+3) e 58 (2+2) |
| Só golpes | Mileena 66 (4, movimentação perfeita), Jade 67 (4, idem), Jax 74 (3), Shadow Priest 27 (4), Orochi Hellbeast 30 (4), Oni Warlord 36 (3), Banshee 2 (2, cortada), Red Dragon Merc 16 (4), Mask Guard 26 (4), Black Dragon Merc 9 (4), Shokan 31 (4, cortado), Demon Captain/General/Archer/Handler 10–13 (3), Tarkata 14 (3), Stone Johnny Cage 86 (3), Bloody Skeleton 8 (3), Putrid Corpse 18 e Undead Corpse 29 (3, explodem e somem), Elemental Knight 19 (3, desmancha e demora a voltar), Brother of Shadow 17 (2, não pula), Green Tarkata 28 (2, cortado), Living Tree Arms 43 (2), Living Tree Mouth 42 (1) |
| Golpes, bugados | Oni 15 (4, falha ao bater e correr), Female Ninja 7 (3, cortada), Ninja Wraith 20, Chan Chan 24, Nagiri Swamp 34 (anda torto), "Oni de fogo" 33, "Oni de chifre" 37 (cortados, 3 golpes cada) |
| Sem golpes | Noob Saibot 53, Smoke 70, Raiden 72 (aparecem dois no mapa), Kabal 73, Sonya 75, Shang Tsung 63, Ermac 85, Burning Monk 76 e Order of Light Monk 77 (meio bugados), Armor Lion 5, homem de terno 6, Shiva 23 |
| **Fora da lista** (bugados) | Cannon Demon 3, Clay Soldier 4 (tela preta), Plains Snake 21, Turban Guard 25, "Nagiri azul" 35, "Liu Kang prateado" 38, Spirit Mask 39, Snake Boss Rider 49 (não fica de pé), Ice Boss 60, Tarkata Grunt 87, o 32 (tela preta) e os bichos e objetos, que nem foram testados: Hanging Corpse 22, Giant Wings 40, Crow 41, Bat 44, Snake 45 (câmera colada), Skull 46, Tentacle 48, Acid Snake 50, Tiger Boss 51 e Wind Boss 61 (câmera colada), Tortured Person 78, Dismembered Body 79, Pterodactyl 80 |

- **Ko-op (pedido do usuário, 17/11):** a documentação recomenda que um dos jogadores use um dos 4 clássicos (Liu Kang, Kung Lao, Sub-Zero, Scorpion), porque trechos da história pedem Brutality ou Multality.
- **Brutality e Multality (investigação começada em 17/11):** a história tem portões "You Need to perform a multality to progress", "You need to perform a Brutality to progress" e "You MUST perform a block to progress" (textos em ~`0xC3D100`/`0xC3D280`/`0xC43700` na RAM da fase). As duas são habilidades que a história dá ("Multality Obtained! ... instantly kill multiple enemies"; a Brutality mata vários "for a short time", um estado com prazo) e saem do modo de fatality (Start > Moves, seção de fatalities). Comandos de roteiro em `func_1280B8`: `setfatalitylevel` (`func_128308`), `setfatalityAmount` (`func_1284E0`), `fatalitytutor` (grava `0x4AD58C`), `fatalityoff` (`0x4AD590`), `brutalityoff` (`func_128760`: para cada estrutura de personagem ativa, `func_325DD8`, chama `func_1266A8(0x5BB2B0, estrutura)`). A `func_122CC8` monta as tabelas de golpes finais (42 chamadas a `func_126900`, com a tabela de nomes `kun1_fat`... `cag3_fat` em `0x572A20`). **Ideia do usuário (17/11):** só para os lutadores do Versus, que já fazem a fatality no L1: usar o mesmo esquema dos 4 clássicos para Multality e Brutality. **Falta:** saber se um lutador do Versus faz a Multality/Brutality (ele faz a própria fatality: Johnny Cage confirmado) e, se não, achar o teste do portão e dar a ele a entrada de um dos 4 clássicos ou marcar o portão quando ele fizer a própria fatality.
- **Na escolha (`kFighters`, 67 nomes)**, nessa ordem: monges e Versus, com especiais, só golpes (Mileena, Jade, Jax e as versões de história do Versus primeiro, depois mais golpes primeiro), bugados, sem golpes. Embaixo do nome, a situação.

## 3. Como seria (o desenho)

O usuário escolhe, para a história (Single Player ou Ko-op), um dos lutadores do Versus. O jogo carrega o modelo, os golpes e os sons dele, e ele joga o Goro's Lair e o resto como se fosse um dos monges.

- **Onde escolher:** na própria seleção da história, com os lutadores do Versus a mais. Se a lista do jogo não aceitar mais lugares, uma linha nova na janela de opções (R3/F1), "Personagem da história", que vale no próximo jogo novo ou carregado.
- **O que precisa casar:**
  1. **Carga do personagem:** o caminho que a seleção usa para escolher modelo, texturas, golpes e banco de som. É ele que precisa receber o lutador do Versus, e não só o número em `+0x1E0E` (teste-chave).
  2. **Memória:** a área `CHARACTER_MEMORY` tem tamanho fixo. Um lutador do Versus pode ser maior que um monge.
  3. **Golpes:** os do Versus são feitos para luta 1 × 1; a história tem vários inimigos, pegar armas, quebrar objetos, brutalidades e fatalities por fase. O que faltar no lutador pode travar ou deixar um botão sem efeito.
  4. **Loja de golpes e XP:** a loja do mapa lista golpes do personagem. Para um lutador do Versus pode vir vazia ou errada.
  5. **Roteiro das fases:** cutscenes e eventos que conferem se o jogador é o personagem 0 ou 1 (como o `unlockendofgame`). Com outro número, podem não disparar ou mostrar o monge.
  6. **HUD e retrato:** o nome já segue o número (visto). Falta o retrato no círculo do HUD.
  7. **Ko-op:** dois lutadores do Versus ao mesmo tempo, ou um monge e um lutador.
- **Modo RPG:** funciona igual, porque o RPG lê XP, dano e vida, não o personagem.

## 4. Riscos

- **O lutador do Versus pode não ter tudo que a história usa** (item 3). Pior caso: só dá para jogar até certo ponto, ou só nas arenas. Mitigação: começar só pela troca de modelo e golpes e testar fase por fase.
- **Memória:** se o lutador não couber, ele fica de fora, ou é preciso achar como o Versus aumenta a área.
- **Cutscenes com o monge:** aceitável (o usuário decide); o personagem jogável volta depois da cena.
- **Saves:** o personagem escolhido precisa ir junto com o save, como o RPG (`*_slot<n>.ini`), sem mudar o arquivo do cartão.

## 5. Tarefas

### Fase A: investigação

- [x] **A0 Versus com todos liberados** (feito 16/11; ver a seção 2).
- [x] **A1 Número de cada lutador** (16/11: tabela na seção 2b)
  - **Faça:** no Versus, escolha cada lutador e leia `0x5E6B40+0x1E0E` (cópia da RAM na luta). Confira o nono lugar.
  - **Pronto quando:** tabela nome → número → bit anotada aqui e no `MODDING.md`.
- [x] **A2 O que a seleção grava** (16/11: `save+0xCC`/`0xD0`, `func_1CEE00` → `func_2F6BA0`)
  - **Faça:**
    - Compare a RAM logo depois de aceitar Liu Kang e logo depois de aceitar Kung Lao na história, e o mesmo no Versus com um lutador. Procure, além de `+0x1E0E`, o valor que o carregamento lê (global, save ou outra estrutura).
    - Ache quem chama `Setup_MainCharacter`/`New_Process_Character` e com que argumento.
  - **Verifique:** trocar esse valor antes do Goro's Lair carregar faz o modelo do Kung Lao aparecer numa história iniciada com o Liu Kang.
  - **Pronto quando:** endereço e função anotados.
- [x] **A3 Lutador do Versus na história (prova)** (16/11: Johnny Cage no Goro's Lair; golpes e luta ainda sem teste)
  - **Faça:** com o caminho da A2, carregue o Johnny Cage no Goro's Lair.
  - **Verifique:** modelo, golpes básicos (soco, chute, pulo, agarrão), dano nos inimigos, morte e volta.
  - **Pronto quando:** capturas e a lista do que funciona e do que não funciona anotadas.
- [ ] **A4 Memória e dados por lutador:** tamanho dos dados de cada lutador contra a área `CHARACTER_MEMORY` (onde fica, quanto cabe, como o Versus faz). **Pronto quando:** tabela de quem cabe.
- [x] **A5 Seleção da história** (17/11: Sub-Zero e Scorpion abrem com as marcas de fim de jogo `0x5D82B4 = 1` (zerou com Kung Lao, libera o Sub-Zero) e `0x5D82B8 = 1` (zerou com Liu Kang, libera o Scorpion), gravadas por `func_1D9908`/`func_1D98D8`; o runtime liga as duas com `MKSM_VERSUS_TODOS`): como ela monta os 4 lugares (onde fica a lista, o que trava Sub-Zero e Scorpion) e se aceita mais. **Pronto quando:** decidido entre a lista do jogo e a linha na janela de opções (3.).

### Fase B: escolha e carga

- [x] **B1 Escolher o lutador** (16/11: L2/R2 na tela de dificuldade do jogo novo; L1/R1 trocam as abas da janela R3, gravando no save do jogo): na seleção da história (A5) ou na janela de opções, com o número salvo por slot (`personagem_mc0_slot<n>.ini` ou dentro do perfil do RPG).
- [x] **B2 Carregar o lutador** (o próprio jogo carrega pelo save): troca de função no caminho da A2, só quando a opção está ligada. Sem ela, o jogo é o original (regra 2).
- [x] **B3 Ko-op** (jogador 2 na mesma aba): o jogador 2 também escolhe.

### Fase C: compatibilidade, fase por fase

- [ ] **C1 Goro's Lair inteiro com cada lutador:** golpes, armas, objetos, chefe, cutscenes, loja de golpes. Anote por lutador o que falha.
- [ ] **C2 Correções:** para cada falha da C1, a correção no runtime ou um aviso ("este lutador não tem X"). Golpe faltando pode usar um do monge.
- [ ] **C4 Golpes emprestados:** achar a tabela de golpes e animações por personagem (a que `func_2F6BA0` monta a partir do número) e dar ao Ermac (e aos outros sem golpes) os de um personagem de corpo parecido. A animação parada do Goro (47/57, de braços abertos) fica como está: decisão do usuário, 17/11.
- [ ] **C3 Demais fases:** depende de saves do usuário mais adiante (`portatil/backup_save_mc0_15nov` e o atual).

### Fase D: acabamento

- [x] **D1 Sem a opção = original:** (17/11: testes 460/3 e os 16 hashes dos replays iguais; sem escolha, o jogo usa o lutador da seleção) testes e replays (R3/R4 do `PLANO_DESEMPENHO.md`) iguais.
- [x] **D2 Documentação e pastas portáteis:** (17/11: README nas duas línguas, LEIA-ME e TESTAR, `MODDING.md`, `ARQUITETURA.md`, pastas e zips do release) README (os dois), LEIA-ME, `MODDING.md`, `ARQUITETURA.md`.

## 6. Decisões

| Decisão | Escolha | Origem |
|---|---|---|
| Lutadores do Versus | Todos liberados, ligados por padrão (`MKSM_VERSUS_TODOS=0` desliga) | Usuário, 16/11 |
| Lutadores na história | A investigar (Fase A); opcional | Usuário pediu o plano, 16/11 |
| 4 jogadores | Fora por enquanto (exige mudar a lógica do jogo para mais de dois jogadores) | Usuário, 16/11 ("esquece isso por enquanto") |

## Golpes emprestados: o que se descobriu (18/11, branch `personagens-golpes`)

**Onde ficam as coisas (SLUS_210.87):**
- **Pacote do personagem (modelo):** um recurso de tipo 2 por personagem (Noob `0x20039`, Sub-Zero `0x2005c`), montado por `sub_0025CA68` (`Renderer/Character/ps2Setup.cpp`) e processado por `0x2556b0` ("New_Process_Character"). Descomprimido, começa com `2EMP` e traz o **modelo e o esqueleto**. No Sub-Zero aparecem partes reais do modelo; no Noob, `dummy_01`: o modelo dele é um boneco de teste, com uma animação só (`0x575`).
- **Pacote de golpes:** pedido em `0x3195f8` com `a1=3` (chamado de `0x1ceaf8`). Os ids vêm da tabela de descrições no executável, em `0x4d6908` + 32 bytes por entrada, palavras 4, 5 e 6. Sub-Zero (88, entrada `0x4d6928`): `0x1a95 0x1a96 0x1b24`. Noob (53, entrada `0x4d64c8`): `-1 -1 -1` (não tem).
- **Tabela de golpes:** `0x62EEA8` + número × `0x370`, montada na carga da fase. Cada palavra é um número de animação para uma ação (Sub-Zero: padrão `0x9d3`; Noob: `0x575` em tudo).
- **Golpe começa em** `0x2ae650(objeto, animação)`; o bloco é escolhido pelo número de personagem do objeto de luta (`+0x1e0e`), lido na hora do golpe (`0x302700`/`0x302724`).
- **Escolha de lutador do RPG:** troca o pacote carregado; o objeto do jogador nasce com o número escolhido (53 no Noob).

**Receita da prova (sem travar):** antes da carga, copiar os 3 ids do pacote de golpes do dono para a entrada do personagem; depois que ele nasce, gravar o número do dono em `+0x1e0e` do objeto de luta. O jogo passa a executar os golpes do dono.

**Resultado no Noob com golpes do Sub-Zero:** os golpes saem, mas o corpo do Noob afunda no chão (confirmado pelo usuário): o esqueleto do boneco não aceita animações de lutador. **Limite:** só funciona entre personagens com o mesmo esqueleto.

**Opções:**
1. Validar entre corpos compatíveis (Scorpion, Sub-Zero, Reptile: ninjas jogáveis completos), por exemplo o L1 de um no outro. Se funcionar, dá para emprestar golpe a golpe.
2. Noob jogável: corpo do Sub-Zero com as cores do Noob, com golpes e fatality do Sub-Zero.
3. Ermac, Smoke e outros sem golpes: conferir se cada um tem esqueleto de lutador ou é boneco antes de tentar.

**Falta:** o mapa botão → posição do bloco (para emprestar 1 ou 2 golpes ou só o L1) e transformar a receita em código no runtime, aplicado quando o personagem nasce.

**Ferramentas** (diagnóstico fora do git: `portatil/ps2_savestate_diag.cpp`, `portatil/diag_muro.diff`, mais `PS2X_TRACE_CALLS` no `dispatchGuestBranch`): save states antes da carga do Goro's Lair e na luta (Noob e Sub-Zero), `copy@`, `poke@`, `calls@`, `watch@`, `ram@`.

## Shao Kahn na história (18/11)

- **Dano:** na história, o Shao Kahn (64) causa 2,5× e leva 0,7× (`hookDamage`, com ou sem RPG): os golpes dele foram feitos para bater no jogador.
- **Defesa:** ele defende de verdade; a defesa por software do L2 saiu.
- **Barra de fatality:** float em `save+0x4BB8` (+0x60 por jogador). O HUD a copia em `0x5cffe8`. Com o Liu Kang, sobe 20 a 30 por soco e para em 130 (nível 1, `save+0x4BE0`). Os golpes do Shao Kahn não a enchem. **Armadilha:** encher a barra do Shao Kahn por fora (testado, +25 por acerto) trava a sala da fatality: os inimigos só se mexem quando ele anda. O jogo deve procurar a fatality dele, que não existe. Se um save gravar a barra acima de zero, ele fica travado também. **Portão resolvido (18/11, testado pelo usuário):** os comandos de roteiro `isfatalityfull` (`0x128678`), `fatality` (`0x128630`) e `fatalityover` (`0x1285C8`) respondem 1 quando nenhum jogador ativo é monge nem do Versus (`semFatality` em `mksm_rpg.cpp`); a barra fica intocada.
- **Lag:** mesmo sem a barra, a sala da fatality fica lenta só com ele. Ainda não investigado.
- **Pose parada (testado, não resolveu):** a animação na tela fica em `objeto_de_luta+0x24` (objeto do jogador j em `0x5E6B40 + j*0x33C0`; `+0x20` é o estado, 11 parado e 7 andando). Parado, a rotina de postura (`0x2BA9E0`, chamada a `func_2AE650` em `0x2BAAC4`) pede a 7 e a de espera (`func_3025D0`) pede a `0x688` do bloco de golpes (que é quase todo `0x688`; as de verdade são 4, 7, `0xB`, `0x22`, `0x3C`, `0xB9`, `0x68B`). Andando, `0x68B`. Trocar as duas por `0x68B` (ou forçar qualquer uma das outras) deixou a tela igual: a contorção não vem da escolha da animação. Suspeita: animações do lutador jogável num esqueleto de chefe, como o Noob afundando.
- **Próximo portão:** a sala seguinte pede arremessar um inimigo numa parede; o empurrão do Shao Kahn não conta.
- **Para depois:** golpes andando (ele para para bater).
- **Save states:** `capturas/savestates/fatality_liukang_inimigo.bin` e `fatality_shaokahn_inimigo.bin`.

## Loja dos lutadores do Versus (18/11)

- A loja (Start, R1) mostra o nome do lutador, mas a lista é a do monge do save (Liu Kang): combos e especiais dele. Comprar destrava, mas o Johnny Cage não tem esses golpes. Só o especial do R2, comum a todos, funciona (testado pelo usuário).
- O XP fica em `save+0x4BA4`. Cada item tem um nível em bytes: `func_1DA528() + jogador*0x1C + item + 0xE0` (o Combo 1 do Johnny caiu em `save+0x1D1`). O nível máximo de cada item fica em `0x4CC448+item`. A compra passa por `func_1BB328(jogador, item)`, que chama `func_1B85A8(jogador, item, nível)`. A pilha da compra passa pelo menu em `0x19DB74`/`0x19DD64`.
- O bloco de animações de movimento do Johnny (`0x62EEA8 + 90*0x370`) tem 36 entradas reais, como os monges. O do Shao Kahn tem 7.
- **Falta:** esconder ou recusar na loja os itens que o lutador do Versus não tem, e ver se ele tem golpes do Versus sem botão na história.
- **Save states:** `capturas/savestates/johnny_parado.bin` e `johnny_loja_5000xp.bin` (5730 de XP, na fase).

## Multality do Johnny Cage (18/11)

- Com a moeda e as barras cheias, o L1 abre o "FINISH HIM" com 5 setas para a esquerda. As setas acendem quando apertadas, mas nenhuma animação começa: o Johnny não tem entrada de Multality. Nos outros, o comando é 4 setas e triângulo (Liu Kang: ↑↓↑↓△ e ←→↑↑△; Kung Lao: ↑→↓←△ e ←→→←△; Scorpion: ↓↓↓↑△; Sub-Zero: ↑↑↓↑△).
- **Tabela de golpes finais da fase:** gerente em `0x5BDD28`. O buffer do que foi apertado fica em `0x5BDD34`, com o contador em `0x5BDD50` (`func_126818` grava; `func_126B58` lê o controle e compara). As entradas ficam em `0x5BDD58 + i*0x3C`, com a quantidade em `0x5BE334`. Cada entrada tem: `+4` o golpe/animação, `+0x10`, `+0x14` a categoria (comparada com `0x4AD58C`, que vale 2 na sala da Multality; as fatalities do Johnny são 4), as setas em `+0x1C..+0x28` como bits do controle (`0x1000` cima, `0x2000` direita, `0x4000` baixo, `0x8000` esquerda) e o botão final em `+0x2C` (`0x10` triângulo, `0x20` bola, `0x80` quadrado). Quem monta é `func_126900` (chamada por `func_122CC8`).
- O Johnny tem 14 entradas: 11 fatalities (categoria 4) e 3 de categoria 0/1, nenhuma de categoria 2. Ideia: criar uma entrada de categoria 2 com o golpe e o comando de uma Multality de monge.
- **Portão resolvido (18/11, testado pelo usuário):** na sala que pede Multality (`0x4AD58C == 2`), se o lutador não tem entrada de categoria 2, a primeira fatality vira categoria 2 e, acertadas 4 setas (contador `0x5BDD50`), `fatalityover(2)` responde 1 (`multalityLocked` em `mksm_rpg.cpp`). O jogo cancela a animação e o inimigo não morre, mas o portão abre. No teste, as setas da fatality não apareceram; 5 setas para a direita bastaram.
- **Brutality (19/11, testado pelo usuário):** a mesma coisa com a categoria 3 (`0x4AD58C == 3`, `fatalityover(3)`); o Johnny passou e chegou ao Reptile's Lair.
- **Co-op (23/11):** há 8 gerentes de golpes finais, um por personagem (`0x5BB2B8 + k*0x610`, contador de setas em `+0x28`); cada jogador usa o seu. O truque agora vale para todos (antes só o gerente 7). Uma entrada da categoria pedida com `+0xC = 0x29A` é fatality convertida (não conta como Multality de verdade), o que também resolve o save state feito depois da conversão. Testado em `capturas/savestates/multality_kintaro_kitana.bin`: o L1 do controle 2 (Kitana) abre o FINISH HIM com as setas da fatality dela.
- **Para depois:** Multality de verdade, copiando a entrada de um monge (golpe em `+4`/`+0x10`).
- **Save states:** `capturas/savestates/johnny_antes_moeda_multality.bin` e `johnny_multality_cheia.bin` (o L1 já abre o "FINISH HIM").

## Chefe jogável trava a fatality (23/11, investigação começada)

- **Sintoma (usuário):** com Shao Kahn, Kintaro, Goro e outros chefes na partida, o L1 da fatality fica bloqueado, como no jogo original quando há um chefe na luta. Em Ko-op (Liu Kang + Shao Kahn) na sala da fatality, aparece embaixo a barra de vida de chefe "SHAO KAHN": o jogo trata o jogador 2 como chefe. Save state: `capturas/savestates/sala_fatality_2111.bin`.
- **Caminho do L1:** `func_16B950` lê os botões e, com o bit do L1, chama `func_125AF0`, que chama `func_121B28`. Esta confere a trava geral (`0x4AD590`, o `fatalityoff` do roteiro) e procura o bloco de golpes do lutador (`+0x1E0E`) na lista de golpes finais.
- **Pistas:** a variável de roteiro `inbossbattle` é registrada por `func_1D9060` (`func_362CE0`, estrutura em `0x5E2980`, que aponta para `0x73CC50`); ninguém escreveu nela em 8 s do save state. Na tabela de personagens (`0x4D6908`), os chefes têm um formato próprio (`0002xxxx 00000001 000000xx ...`), mas o número do lutador não bate um a um com a linha.
- **Resolvido (23/11):** dentro de `func_121B28`, a `func_127C08` varre os personagens da sala (`func_283B50(0x67C590, i)`, i de 1 a 13) e, para cada ativo (`func_325DD8`), pergunta à `func_127E30(gerente, bloco)` se o bloco de golpes é de chefe; se algum for, todos os alvos são recusados. O jogador Shao Kahn caía nessa varredura. Agora o `hookBossBlock` (`mksm_rpg.cpp`) faz a `func_127E30`, só quando chamada de dentro da `func_127C08`, responder 0 para o bloco de um jogador ativo. Testado em `capturas/savestates/2111_travado.bin` (Liu Kang + Shao Kahn, barra cheia): o L1 abre o FINISH HIM. A barra de vida de chefe embaixo continua aparecendo (só visual).
- **Dano dos chefes jogáveis:** Shao Kahn, Goro (47, 57) e Kintaro (81, 58) causam 1,8× e levam 0,6× (`kBossTuning`). Só vale para o lutador escolhido por um jogador (`fighterId` do save); os chefes inimigos não mudam. Desde 23/11 o bônus vale para todo dano em quem não é jogador (projéteis, ondas e armas acertam por outro objeto, e antes passavam sem bônus).

## Golpes correndo nos lutadores fora do Versus (resolvido 24/11)

- **Sintoma (usuário):** os 8 do Versus têm um golpe diferente para quadrado, triângulo e bola quando estão correndo há um segundo. Os outros lutadores não têm: correndo, esses botões não fazem nada. Meta: correndo, sair o golpe normal.
- **Medido (`func_2AE650`, objeto de luta `0x5E6B40`):** Johnny parado pede `0xB2B` (quadrado), `0xB68` (triângulo), `0xB4E` (bola); correndo, `0xB77`, `0xB53`. Shao Kahn parado pede `0x68E` e `0x69C`; correndo, nenhuma chamada. Os golpes não vêm do bloco de animações (`0x62EEA8`), e sim do pacote de golpes: sem golpe "correndo" para o botão, o jogo não faz nada.
- **Estado de movimento:** `objeto+0x3360` vale -1 parado, 105 começando a andar e 118 correndo. Gravar -1 ou 105 ali no instante do quadrado não fez o golpe sair: a escolha usa outra coisa.
- **Causa:** a lista de golpes que o comparador de botões percorre (`func_16CE20`, teste `func_16C788`, fila `func_16C968` em `0x62EC78`) vem da tabela `0x5CCF88 + bloco*0x6C`, um campo por estado (`func_16A750` grava em `objeto+0x24C`). Parado é o `+0x00`; correndo, o `+0x38`. Johnny (bloco 90) tem lista correndo em `+0x38`; Shao Kahn (bloco 64) só tem `+0x00..+0x0C` e o resto em 0: correndo, a lista fica vazia e nenhum botão é testado.
- **Correção:** o `hookFrame` (`mksm_rpg.cpp`) copia o `+0x00` para o `+0x38` vazio do bloco de cada jogador. Os do Versus, que já têm `+0x38`, não mudam. Testado em `capturas/savestates/shaokahn_parado.bin`: correndo, o quadrado pede `0x68E` e o triângulo `0x69B` (os golpes parados), e a captura mostra o golpe saindo da corrida.
- **Limite:** um inimigo com o mesmo bloco de um jogador também passa a ter a lista correndo.

## Golpes emprestados para a Sonya (25/11, tentado)

- **Sonya (75):** tem modelo próprio, de lutador, e anda normal no Goro's Lair. Na tabela de descrições (`0x4D6928 + (n-88)×32`; a fórmula `0x4d6908 + n×32` antiga estava errada), as palavras 4 a 6 (pacotes de golpes) valem -1. Save state com ela: `capturas/savestates/sonya_goro.bin` (pacote da Kitana 65 copiado na carga).
- **Teste** (`portatil/diag_sonya_rpg.diff`, `MKSM_DIAG_SONYA=<doador>`): na montagem da sala, copia as palavras 4 a 6 do doador para a Sonya e põe 75 no `save+0xCC`. Com a Kitana de história (65) ou a do Versus (89): a sala carrega, mas a lista de golpes do bloco 75 (`0x5CCF88 + 75×0x6C`) fica só com o nó vazio de sempre (`anim -1`, botão `0x100`), e nenhum botão casa. Os pacotes trazem animações, mas a lista de golpes vem de outro lugar.
- **Trocar o bloco** (`+0x1E0E` = 65): o HUD vira "KITANA" e o lutador aparece como um boneco branco em T: o bloco escolhe também o modelo e as animações (a tabela `0x62EEA8` do 65 aponta animações que não estão carregadas).
- **Falta:** achar de onde vem a lista de golpes do bloco (quem preenche `0x5CCF88 + bloco×0x6C` na carga) e copiar a do doador para o bloco 75, mantendo modelo e esqueleto da Sonya. Antes disso, conferir se as animações do doador servem no esqueleto dela.

## Smoke e Raiden aceitam golpes de lutador (25/11)

- **Teste** (`portatil/diag_emprestar_golpes.diff`: `MKSM_DIAG_ALVO=<lutador> MKSM_DIAG_SONYA=88 MKSM_DIAG_SONYA_BLOCO=1`, jogo novo automático até o Goro's Lair): na montagem da sala, o pacote de golpes do Sub-Zero (88) vai para o lutador, e o objeto de luta passa a usar o bloco 88. Quadrado apertado várias vezes.
- **Smoke (70):** faz os golpes do Sub-Zero (chutes, deslize) com o próprio corpo, em pé no chão. Não afunda, não fica em T.
- **Raiden (72):** também faz os golpes, sem deformar; segura o cajado do modelo dele durante os golpes do Sub-Zero.
- **Diferença para a Sonya:** com a Sonya, o doador foi a Kitana (65/89) e o modelo virou um boneco em T. O doador e o bloco importam: o Sub-Zero (88) tem o bloco montado com animações carregadas. Testar a Sonya com o 88 (ou com a Kitana do Versus como bloco) antes de concluir que o corpo dela não aceita.
- **Pedido do usuário (25/11):** Raiden com todos os golpes do Kung Lao (1) e Smoke com todos os do Sub-Zero (88). Problemas relatados por ele com o teste: (1) o boneco às vezes "quebra e vira uma bola"; (2) com o Raiden, a primeira cena (que tem o Raiden de história, também 72) não carrega direito e aparecem dois Raidens. A causa provável do (2): o teste copia o pacote do doador na entrada 72 da tabela de descrições, e o Raiden da cena lê a mesma entrada.
- **Tentado e desfeito (`portatil/tentativa_modelo_doador.diff`):** o jogador entra como o doador (save = 1 ou 88) e a tabela de modelos (`0x4D7A08 + (n-88)×48`, o id do recurso de modelo: Kung Lao `0x20005`, Sub-Zero `0x2005C`, Smoke `0x2004A`, Raiden `0x2004C`, Sonya `0x2004F`, Noob `0x20039`) do doador recebe o modelo do escolhido. A cena ficou certa (um Raiden só), mas o jogador nasceu com o modelo do doador (Kung Lao, Sub-Zero): o modelo é lido de outro lugar ou antes da montagem da sala.
- **Próximo:** voltar à receita que deixa o modelo certo (pacote do doador na entrada do escolhido + bloco do doador) e limitar a cópia do pacote ao jogador. Opções: copiar só durante o pedido do pacote do jogador (`0x3195F8` com a1 = 3, chamado de `0x1CEAF8`) e restaurar logo depois; ou achar quem lê o id do modelo e trocar só para o jogador. A "bola" ainda não foi reproduzida: pedir ao usuário onde acontece (golpe, especial, agarrão, cena).
- **Carga do personagem (25/11):** `func_1CE6E8(jogador, lutador, roupa, ...)`; jogador = 1 ou 2 para os jogadores. Modelo: palavras da tabela `0x4D6988 + n×0x30 + roupa×0x24` (0 = id do recurso, `+0xC..+0x1C` arquivos) e palavras 1–3 da tabela de descrições `0x4D5E28 + n×32`; golpes: palavras 4–6 (pedido `func_3195F8`, a1 = 3 para jogador). **O bloco de golpes pertence ao número**: só é montado quando um personagem com esse número carrega o pacote dele; trocar só o pacote (a1 = 3) deixa o bloco do escolhido com o nó vazio.
- **Tentativa A (`portatil/tentativa_pacote_jogador.diff`):** pacote do doador só no pedido do jogador + bloco do doador. Raiden no Goro's Lair: cena de abertura toca, um Raiden só depois, modelo certo, golpes do Kung Lao, HUD "KUNG LAO". Mas só funcionou porque o Kung Lao da cena monta o bloco 1; o Smoke (bloco 88 ausente) ficou sem golpes. Não serve em geral.
- **Tentativa B (`portatil/tentativa_carga_doador.diff`):** na chamada do jogador, carregar o doador com as palavras de modelo do escolhido (tabelas devolvidas depois). A cena toca, mas na luta o Raiden some (câmera presa numa parede) e o Smoke vira um pedaço solto (só a arma): misturar as palavras de modelo quebra o modelo. Provável que as palavras 1–3 da descrição sejam animações/esqueleto e não modelo.
- **Tentativa B por partes (Smoke):** só o id 0 ou só as palavras 1–3 da descrição: corpo do Sub-Zero em T. Id 0 + `+0xC..+0x2C`: corpo do Smoke na cena, quebrado na luta. Carregar com a identidade do doador não serve.
- **Tentativa C (`portatil/tentativa_pacote_na_carga.diff`):** pacote do doador na entrada do escolhido só durante a `func_1CE6E8` do jogador. Cenas certas nos dois (Raiden: os dois Raidens na cena e um só depois), modelos certos. Mas o bloco do doador não é montado (Smoke sem golpes, HUD "SMOKE"): a lista de golpes é montada depois, pelo retorno do pedido, relendo a tabela pelo número. Com a tabela trocada o tempo todo (o teste do Smoke de antes) funciona, mas aí o Raiden da cena também lê.
- **Smoke com a tabela trocada o tempo todo + bloco 88 (25/11, desfeito a pedido do usuário):** nos testes automáticos o Smoke parecia chutar, mas o usuário jogou e nenhum golpe saiu; o modelo ficou bugado como os personagens ficavam no começo do recomp. Não confiar nas capturas automáticas para golpes: o teste de verdade é o usuário jogando. Commit `690e31c`, revertido em `d845daf`.
- **Conclusão:** um bloco por número. O Raiden jogador e o da cena (72) não podem ter golpes diferentes enquanto tiverem o mesmo número. O Smoke pode usar a tabela trocada o tempo todo enquanto nenhum Smoke de história (missões do Smoke) estiver na sala.
- **Próximo:** separar quais palavras são modelo e quais são animação (trocar uma por vez na tentativa B), ou fazer o bloco do doador ser montado também quando só o pacote dele é carregado com outro número.
- **Para virar recurso:** fazer isso no runtime quando o lutador escolhido não tem golpes (pacote do doador antes da carga, `+0x1E0E` depois), com o HUD mostrando o nome do doador como efeito colateral (o nome vem do bloco).

## Agarrão e arremesso (R2) nos lutadores fora do Versus (24/11, a fazer)

**Meta (usuário):** os monges e os lutadores do Versus agarram e arremessam inimigos comuns com o R2. Nos outros, o R2 não faz nada. Quem já tem habilidade no R2 fica com ela, e o agarrão vai para outro botão. Contra chefes, nada muda. Bônus: o portão "arremesse um inimigo na parede", que trava o Shao Kahn (seção do Shao Kahn), passaria a abrir de verdade.

**O que já se sabe e serve aqui:**
- Os golpes vêm da lista por estado em `0x5CCF88 + bloco×0x6C`. Cada golpe tem o botão em `+0x14` (`0x80` quadrado, `0x10` triângulo, `0x20` bola) e o próximo golpe do combo em `+0x20`. Quem compara é a `func_16CE20` (MODDING.md, "Listas de golpes por estado").
- Já copiamos lista de um campo para outro (golpe correndo): copiar ponteiros não custa desempenho.
- **Risco principal:** o arremesso tem duas animações, a de quem agarra e a de quem é agarrado. A do agarrado pode vir do pacote do atacante e só servir em esqueleto de lutador (o mesmo limite do Noob afundando no chão).

**Tarefas (na ordem):**

**Medido (24/11):**
- **R2 = `0x2`** na máscara de botões (`+0x14` do golpe; com R1 o jogo usa `0x20xx`, `0x40xx`... e golpes com `+0x1A` de condição).
- **Johnny (bloco 90):** o agarrão é o golpe `fce728` na lista parada (`+0x00`): animação `0xB58`, botão `0x2`, `+0x10` = 5000, `+0x12` = `0x14`. O combo dele (`+0x20`) é uma lista de 14 arremessos, um por tipo de inimigo: animação `0x23` (a última, `0xB5D`), `+0x02..05` = `00 01 01 11`, `+0x10` = `0x12`, `+0x12` = `0x64`, `+0x28` = 1 e `+0x2C` = a chave do inimigo (`0x179A`, `0x17B3`... `0x18AE`).
- **Shao Kahn (bloco 64):** nenhum golpe com o R2. O botão está livre para ele.
- **Enxerto testado** (`PS2X_GRAB_TEST`, código em `portatil/diag_agarrao_rpg.diff`): os 15 nós montados em `0xB8000` (faixa livre entre os pools do runtime e as pilhas, ARQUITETURA §5 "Fim do heap") e postos na frente da lista do Shao Kahn. O R2 passa a casar e o golpe entra na fila. Com a `0xB58` do Johnny, nada toca (a animação não existe no pacote dele); com `0x68E`, `0x21`, `0x22` ou `0x23`, a animação toca. O arremesso (o combo) entra na fila, mas **nunca toca**, nem perto do inimigo (`fatality_shaokahn_inimigo.bin`, depois de dois X): o Johnny também enfileira o combo quando erra, e o arremesso só sai quando a animação do agarrão pega o inimigo. Nenhuma animação do pacote do Shao Kahn testada até agora agarra.
- **Como o arremesso é escolhido (24/11, noite):** o consumidor da fila (`func_16B138`) chama `func_16F1F8(objeto, &golpe)`, que testa cada arremesso com `func_2E7F30(objeto, chave +0x2C)` (um seletor enorme, um caso por chave) e fica com o primeiro que passa. Johnny agarrando: entrou na fila o `fce760` e tocou o `fcea38` (chave `0x18AE`). A chave `0x18AE` passa quando o jogador tem um **alvo agarrado**: `objeto_de_luta+0x22FA` = número do inimigo (slot no array `0x5E6B40`, -1 sem alvo; lido por `func_3237C8(0x6FB490, objeto)`), com o alvo em `+0x2308` ≠ 1, `+0x2BB4` = 0 e `+0x3234` = 0.
- **Quem cria o vínculo:** `func_2FEDB8` (processa cada acerto; chamada de `0x3024C0` com a0 = atacante, a1 = vítima, a2 = tipo do acerto) grava `atacante+0x22FA` = vítima em `0x2FF474`, `vítima+0x2308`, e mexe em `+0x27E4`, `+0x2344..0x2348`, `+0x2660`, `+0x2320` e chama `func_325CC8`. O ramo depende de valores do acerto (pilha `+0x430`/`+0x444`), que vêm do dado da animação. Também grava `+0x22FA` em `0x302EEC` (`func_302DF8`). Nos testes, o agarrão do Johnny pegou só uma vez em 6, então o rastreio de `func_2FEDB8` não pegou um acerto de agarrão para comparar os argumentos.
- **Caminhos possíveis:** (a) achar o campo da animação que faz o acerto virar agarrão e reproduzi-lo no golpe enxertado; (b) quando o golpe enxertado acertar um inimigo comum, rodar o mesmo preparo da `func_2FEDB8` (muitos campos, risco de estado incompleto); (c) chamar `func_302DF8`, a outra que grava o vínculo, se ela for o "agarrar" de alto nível. Ver (c) primeiro.
- **(c) descartado (25/11):** a `func_302DF8` não agarra: é a volta ao estado normal do lutador. Ela grava `+0x22FA` = -1 (solta o alvo) e zera dezenas de campos. Sobra (b) ou (a). Para (b), o preparo começa em `0x2FF3FC`: `atacante+0x27E4` = 1, `vítima+0x2344` = 1, `vítima+0x2660/0x2662` = 0, `vítima+0x234A` = `atacante+0x2348`, `atacante+0x2320` = `atacante+0x1C` se vazio, `atacante+0x22FA` = vítima, `vítima+0x2308` = (valor de `$s7`, a descobrir), `func_325CC8(vítima, atacante)` e o que vem depois de `0x2FF4A0`. Para comparar com o jogo, falta um acerto de agarrão de verdade rastreado (save state com o Johnny colado num inimigo comum ajuda).
- **Save state (25/11, usuário):** `capturas/savestates/johnny_r2_inimigo.bin`: Johnny logo à frente de um inimigo comum; carregar e apertar R2 já agarra e arremessa. É a base para rastrear um acerto de agarrão de verdade (R2.1).
- **Save state (25/11, usuário):** `capturas/savestates/kitana_golpes_inimigo.bin`: Kitana na frente de um inimigo; dá para testar qualquer golpe, inclusive o agarrão dela.
- **Save states da cabeça (25/11, usuário):** `capturas/savestates/liukang_cabeca_r2.bin` e `jade_cabeca_r2.bin`: o personagem em cima de uma cabeça no chão. No Liu Kang, R2 segura a cabeça e quadrado a joga; na Jade, nada.
- **Falta:** achar a função que, ao pegar o inimigo, escolhe o arremesso do combo (confere `+0x28`/`+0x2C`) e o que nela marca a animação como agarrão (dado da animação, ou um campo do golpe). Com isso: ou forçar o "pegou" quando o inimigo está no alcance, ou achar uma animação de agarrão no pacote de cada lutador. Depois, conferir se a animação de quem arremessa (`0x23`) e a do inimigo funcionam no corpo do Shao Kahn.

- [ ] **R2.1 Medir o agarrão de verdade:** com o Johnny (`johnny_parado.bin`), apertar R2 perto de um inimigo e rastrear `func_16C788`/`func_16C968` (`PS2X_TRACE_CALLS`). Achar o nó do R2: em que campo da tabela fica, o valor do botão em `+0x14` e o que vem em `+0x20` (agarrou → arremesso). Ver se há teste de alcance ou de alvo antes.
  - **Pronto quando:** se conhece o nó do agarrão e a cadeia agarrar → segurar → arremessar.
- [ ] **R2.2 Medir o R2 nos outros:** Shao Kahn (`shaokahn_parado.bin`) e um lutador com habilidade no R2. Se o R2 não casa com nenhum golpe, é onde enxertar; se casa, anotar a habilidade. **Botão (decisão do usuário, 24/11):** o L1 fica livre (reservado para o futuro). Ordem: R2; se ocupado, o primeiro livre entre R1+quadrado, R1+triângulo e R1+bola; se os três tiverem golpe, R1+R2.
- [ ] **R2.3 Animação do agarrado:** descobrir de onde ela vem (pacote do atacante ou do inimigo). Teste mais simples: apontar o nó do agarrão do Johnny na lista de um lutador de fora (cópia de ponteiro) e ver as duas animações. Se o corpo do atacante deformar, tentar só a reação de arremesso do inimigo com um golpe próprio do lutador.
  - **Pronto quando:** existe uma forma que agarra e arremessa sem deformar, ou se sabe que não dá para aquele esqueleto.
- [ ] **R2.4 Botão e alvos:** agarrão no R2 de quem não tem nada, e no botão combinado com o usuário para quem tem habilidade. Só contra inimigos comuns: conferir o bloco do alvo (`func_127E30`, a mesma que separa chefe) e recusar chefes.
- [ ] **R2.5 Testar:** Shao Kahn, Kintaro, Goro, Kitana, Baraka, e o Johnny (não pode mudar). Agarrar, arremessar, arremessar na parede (o portão depois da sala da fatality), dano. Medir quadros/s antes e depois.

## Mileena e Jade: agarrão e itens do chão com o R2 (25/11, plano)

**Meta (usuário):** Mileena (66) e Jade (67) fazem o agarrão/arremesso em inimigos comuns e pegam com o R2 os itens do chão (cabeça, etc.); quadrado joga o item segurado. Contra chefes, nada muda. Regra de botão da seção acima (L1 livre).

**Por que elas primeiro:** têm movimentação perfeita e golpes (tabela "Só golpes"), e o R2 delas não faz nada, então não há habilidade a mudar de botão.

**O que já se sabe:** o agarrão do monge/Versus é um nó com botão `0x2` na lista parada (`+0x00`) e um combo de arremessos escolhidos pela chave do inimigo (`+0x2C`, via `func_2E7F30`); o arremesso só sai com o vínculo `+0x22FA` gravado pela `func_2FEDB8` num acerto de agarrão. Um bloco de golpes por número: mexer no bloco 66/67 mexe também na Mileena/Jade inimigas.

**Riscos:** (1) animações: segurar, carregar e jogar o item, e o agarrão, podem não existir no pacote delas (o mesmo limite que travou o Smoke e a Sonya: golpe de doador não toca no corpo de outro); (2) a Mileena/Jade inimigas ganhariam os nós; (3) o item pode ser pego por código fora da lista de golpes (teste de proximidade do objeto), e aí o enxerto de nó não basta.

- [x] **I1 Medir a pegada do item (Liu Kang, `liukang_cabeca_r2.bin`):** rastrear `func_16C788`/`func_16C968`/`func_16B138` ao apertar R2 e depois quadrado. Achar o nó (botão, chave de condição `+0x2C`, animação), o que muda no objeto do lutador e no da cabeça (campo de "item segurado") e qual nó o quadrado usa com o item na mão.
  - **Pronto quando:** se conhece a cadeia R2 → segurar → quadrado → jogar, com endereços.
  - **Medido (25/11):** é tudo lista de golpes. Pegar = nó na lista parada (`+0x00`) com animação `0x98`, `+0x08` = `0x800000` (marca "só em cima de item", que faz aparecer o "R2 Pick up"), botão `0x2`, `+0x10` = `0x141388`, `+0x1C` = `0x40`. Segurando, o jogo troca para a lista da posição 17 da tabela (`entrada+0x44`): no Liu Kang, 57 nós, todos com a animação `0x9A` (jogar), um por botão (quadrado `0x80`, triângulo `0x10`, bola `0x20`, R2 `0x2`, e as variantes com R1/direções `0x20xx`, `0x40xx`, `0x80xx`, `0x10xx`), mais um nó `0xFFFF` com botão `0x100`.
- [x] **I2 Medir a Jade:** o R2 não casa com nada (a marca `0x800000` não existe na lista dela) e a posição 17 (`+0x44`) é 0. O "R2 Pick up" aparece mesmo assim (o aviso é do item, não do lutador).
  - **Enxerto testado (`PS2X_ITEM_TEST`, `mksm_rpg.cpp`, DIAG):** nó de pegar inserido **depois** do primeiro nó da lista parada (na frente não funciona: o objeto guarda a lista em `+0x24C` e começa do primeiro nó já conhecido) e uma lista "segurando" de 4 nós (`0x9A` com quadrado, triângulo, bola, R2) na `+0x44`, em `0xB9000`. Resultado na captura: o R2 casa (nó `b9000`), o aviso some e a cabeça vai para a Jade; o quadrado casa o nó de jogar (`b9038`). Mas a pose não é a do Liu Kang: a `0x98` do pacote dela parece levantar o bastão, com a cabeça na ponta. Teste do usuário: `portatil/MKSM-linux/jogar-teste-item.sh`.
- [ ] **I2 Medir a Jade (`jade_cabeca_r2.bin`):** o R2 casa com algum nó? Que chaves de condição a lista dela tem? Existem no pacote dela animações com os números usados em I1?
  - **Pronto quando:** se sabe se falta só o nó ou também a animação.
  - **Usuário (25/11):** funcionou jogando: a Jade pega a cabeça e joga.
- **Save state:** `capturas/savestates/jade_inimigo_r2.bin` (Jade na frente de Oni; feito com o enxerto de pegar já na RAM).
- **Outras marcas do R2 no Liu Kang** (lista parada): `0x89` com `0x100000`, `0xA9` com `0x200000`, `0x9E` com `0x1000000` (outras interações do cenário, a identificar).
- [ ] **I3 Medir o agarrão de verdade** (R2.1 acima, com `johnny_r2_inimigo.bin` e `kitana_golpes_inimigo.bin`): argumentos da `func_2FEDB8` num acerto de agarrão, comparando os dois.
- [x] **I4 (pegar item, 25/11):** recurso fixo em `hookFrame` (`mksm_rpg.cpp`): em todo bloco de jogador com a lista 17 vazia, nó `0x98`/`0x800000` depois da raiz e a lista 17 com `0x9A`, em `0xB9000 + j*0x800`. Testado pelo usuário na Jade. Limite: a tabela é por bloco, então um inimigo do mesmo lutador também ganha (a IA não aperta R2 em item). Plano original: **Enxertar na Jade só para o jogador:** nós do item (e depois do agarrão) na frente da lista do bloco 67, na faixa livre `0xB1000–0xC0000`, como no teste do Shao Kahn; o casador só aceita esses nós quando o objeto é de um jogador (evita o risco 2). Usar animações do pacote dela que sirvam (ou as do monge, se tocarem).
  - **Verifique (usuário jogando, não captura automática):** R2 em cima da cabeça segura, quadrado joga, o golpe acerta; soco e chute normais continuam; Jade inimiga igual.
- **Agarrão medido (25/11):** igual em todos: nó com botão `0x2` e animação própria (Liu Kang `0x10A`, Kitana `0xADA`, Johnny `0xB58`), combo com 14 arremessos (13 com `0x23`, chaves `0x179A`...`0x18AE`; o último com animação própria: `0x10E`, `0xADE`, `0xB5D`). Kitana agarrando: `func_2FEDB8(0, vítima, 2)` (acerto do tipo 2) e depois toca o arremesso da chave `0x18AE`. A Jade não tem agarrão (golpes `0x706`–`0x715`). Enxerto do nó + 14 arremessos na Jade (depois do nó de pegar, que casa primeiro só em cima de item): o R2 casa e a animação toca, mas com `0x10A`, `0xADA` ou `0xB58`, em 8 apertos cada, a Jade não acerta nada (nenhuma `func_2FEDB8` com atacante 0). O número da animação é por pacote; essas não existem no dela.
- **Agarrão na Jade funciona no rastreio (25/11):** o tipo do acerto não separa agarrão (soco comum da Jade também chega com `a2 = 2`). O que importa é o golpe enxertado acertar: com a animação `0x70a` dela no nó do R2, o acerto grava o vínculo (`jogador+0x22FA` = alvo, `+0x27E4` = 1) e o combo toca o arremesso `0x23`, igual ao Johnny (`0xB58` acerta, vínculo, `0xB5D`). `0x706`, `0x70c`, `0x70e` também acertam; `0x708`, `0x710`, `0x712`, `0x714` não. Teste no jogo: `portatil/MKSM-linux/jogar-teste-item.sh` (`GRAB=0x70a` padrão). Falta ver se o inimigo voa de verdade e achar a animação da Mileena.
- **Agarrão de verdade (25/11, aprovado pelo usuário na Mileena):** o arremesso em par sai da `func_16D710(_, atacante, animação)`: ela lê o registro da animação do atacante (`func_2B1920(0x6B3000, anim)`: tabela em `rd32(0x6B3008) + anim*4`) e só faz o arremesso com o campo `+0x2A` > 0, que é a animação do agarrado (Johnny `0xB5D` → `0xB5F`). Depois `func_16F760` → `func_16D370` → `func_306078(vítima, 0xB5F, 0, 0xB5D)` põe os dois nas animações. A `0x23` (arremesso comum) tem `+0x2A` = 0: por isso o inimigo só tomava o golpe. Mileena e Jade não têm animação em par; as carregadas na fase são do Kung Lao (`0x1C6..0x219`) e do Oni (`0x32C`, `0x332`). Recurso: no R2 (depois do nó de pegar item), o agarrão do Kung Lao `0x1C2` e o combo de 14 arremessos com o último (chave `0x18AE`) em `0x1C6` → `0x1C7`: o inimigo gira para cima, cai e levanta. Testado com `mileena_agarrao.bin` (save do usuário). Antes, com o golpe próprio delas no agarrão (`0x6E6`, `0x6EC`...), o vínculo era gravado mas o inimigo só tomava o golpe.
- **Botões da Mileena/Jade (25/11):** os R1+botão do Versus são nós da mesma lista com `+0x08` bit `0x10000` (Johnny: `0x10010000` + `+0x0C` = `0x201..0x210`), postos antes do botão sozinho. Enxertados: R1+triângulo = cópia do rolamento (triângulo+bola, `0x6EC` na Mileena, que fica) e R1+bola = cópia do golpe do L1 (`0x6EA`). A lista é a mesma das inimigas: `hookMatch` (`func_16C968(objeto, nó)` → 1 se casa) nega os nós enxertados a quem não é jogador e o L1 original ao jogador (L1 livre). Inimigas ficam como o original.
- **Par escolhido na hora (25/11):** o pacote do Kung Lao só está carregado porque ele é o outro monge; num co-op sem ele pode faltar. A cada quadro o `hookFrame` usa o primeiro par carregado (registro do arremesso com `+0x2A` > 0) entre Kung Lao `0x1C2/0x1C6`, Liu Kang `0x10A/0x10E`, Kitana `0xADA/0xADE` e Johnny `0xB58/0xB5D`; sem nenhum, tira o agarrão da lista (o R2 só pega item). Não testado num co-op sem o Kung Lao.
- **Co-op testado (25/11, automático até a fase):** Mileena + Johnny → usa o par do Johnny (`0xB58/0xB5D`); Mileena + Jade → nenhum par carregado, o agarrão sai das duas (R2 só pega item). Pedido do usuário: registrar o agarrão do Kung Lao nelas mesmo sem ele na partida, e depois fazer o mesmo para outros.
- **Registrar animação de outro lutador (investigação, 25/11):** três camadas, medidas com o save da Mileena (Kung Lao carregado):
  - **Registro do golpe:** `rd32(0x6B3008) + anim*4` → registro de `0x70` bytes (`func_2B1920`, quantidade em `0x6B3000` = `0xBC2`). Guarda `+0x00` id, `+0x2A` a animação do agarrado; nenhum ponteiro para o movimento. Os do Kung Lao ficam em `0x12EDxxx` (ex. `0x1C2` em `0x12EDEE4`), os da Mileena em `0xFDCxxx`. Sem o lutador carregado, a entrada é 0.
  - **Índice no banco:** `func_2E6098(anim)` = `rd32(0x6C8A28 + anim*4)`: troca o número da animação pela posição no banco carregado (`0x1C2` → `0x1DB`, `0x6E6` → `0xDB`, `0x23` → `0xB13`; `0x10A`, do Liu Kang fora da partida, fica igual a si mesmo).
  - **Falta:** achar o banco (posição → quadros do movimento) e quem o monta ao carregar um lutador (o carregador do pacote, `New_Process_Character`). Plano: (1) rastrear quem lê o índice `0x1DB` ao tocar o agarrão (`func_2AECB0`, `func_34D420`, `func_2E9A80`); (2) achar o carregador e pedir o pacote do Kung Lao (só as animações) junto com o da Mileena/Jade; ou guardar uma cópia do banco e dos registros do Kung Lao e instalar quando faltarem (precisa saber o tamanho e os ponteiros internos). Ver os riscos de memória (ARQUITETURA §5) antes de alocar.
- **Banco e carga (25/11, continuação):** banco = `rd32(0x6B3004) + posição*8` (`func_2B1948`; `func_2AECB0` lê o `+2`). O índice `0x6C8A28` é preenchido pelo tratador do recurso tipo `0x45` (`func_1D1068`, tabela de tratadores em `0x4D856C`; `0x1D0F90`/`0x1D0FD8` são os vizinhos `0x42`/`0x43`): lê o cabeçalho do recurso (`+0` quantidade, `+0x10` o lutador), pega a faixa do lutador (`func_2B19C0(0x6B3000, lutador, &primeira, &última)`) e numera as posições a partir de `0x6A98F0`; depois `func_25EF68` carrega os quadros. A fase carrega cada jogador com `func_1CE6E8(jogador, lutador, roupa, …, t1)` chamada de `func_1CEE00` (co-op Mileena + Jade: jogador 1 com t1 = `0x21`, jogador 2 com `0x22`), que pede o pacote de golpes com `func_3195F8(0x6FA678, 3, palavras 4–6 da descrição, t1, 0)`.
- **Tentativa (não deu certo ainda):** depois da carga do jogador 2, pedir o pacote do Kung Lao (`func_3195F8(0x6FA678, 3, 0x172D, 0x172E, 0x1AED, t1 = 0x23, 0)`). O jogo não travou, mas o par dele não apareceu (registro de `0x1C6` continuou vazio em 110 s). Código em `portatil/diag_carga_kunglao.diff` (fora do git). Próximos: entender o t1 (grupo de memória? `0x21`/`0x22` por jogador) e o a1 (3 ou 4, os dois ramos do `func_1CE6E8`); ver se o pedido é atendido só numa fase de carga anterior (pedir antes, no `hookRoom`); medir a memória livre antes de carregar um terceiro pacote.
- **Falta:** conferir o agarrão em outras fases e na Jade (mesmo código, bloco 67, não visto em jogo); recusar chefes; Ermac (R2 ocupado) com agarrão e item em outro botão.
- [x] **I5 Agarrão na Jade:** com I3, o nó do agarrão + combo de arremessos; se nenhuma animação dela pega, forçar o vínculo (caminho (b) da seção acima) quando o golpe acertar inimigo comum.
- [ ] **I6 Repetir na Mileena (66)** e anotar desempenho (o enxerto só roda no casador, custo desprezível).

## Animações guardadas nos lutadores sem golpes (25/11)

- **Faixas por lutador:** a tabela `0x62EEA8 + n×0x370` começa com a primeira animação própria de cada lutador, e as faixas são contíguas: o lutador n tem de `base(n)` até `base(n+1) − 1`. Confere com o medido: Jade 67 = `0x6F6..0x716` (golpes `0x706–0x715`), Johnny 90 começa em `0xB0F` (agarrão `0xB58`), Liu Kang 0 = `0xC2..0x176` (agarrão `0x10A`). Abaixo de `0xC2` ficam as animações comuns a todos (reações, `0x23` arremesso, `0x98`/`0x9A` item).
- **Sem golpes (faixa, total; usadas pela tabela):** Ermac 85 `0x98B..0x9AE` (36; só `0x98B`), Shang Tsung 63 `0x66F..0x687` (25; só `0x66F`), Shiva 23 `0x3B3..0x3C8` (22; só `0x3B3`), Burning Monk 76 `0x79E..0x7A7` (10), Order of Light 77 `0x7A8..0x7B1` (10), Smoke 70 `0x765..0x76D` (9; só `0x765`), Noob 53, Raiden 72, Kabal 73, Sonya 75 (2 cada), Armor Lion 5 e homem de terno 6 (1 cada).
- **Falta:** tocar as animações sobrando no quadrado (enxerto de nó, como na Jade) e ver quais existem de verdade no pacote e quais acertam. Ermac, Shang Tsung e Shiva primeiro. Precisa de save state de cada um.
