# Plano de execução: Shaolin Monks perfeito

> **Para o agente de IA que vai executar este plano.** Leia as seções 1 a 4 inteiras antes de mexer em qualquer coisa. Depois faça **uma tarefa por vez, na ordem da lista da seção 6**, seguindo os passos exatamente como estão escritos. Cada tarefa diz o que fazer, que comando rodar, o que você deve ver e quando ela está pronta. Não pule a verificação de nenhuma tarefa.
>
> **Trabalhe sozinho, o máximo possível, sem parar para perguntar.** O usuário não quer ser consultado a cada problema. Quando algo der errado: investigue, corrija e tente de novo, com pelo menos **três abordagens diferentes** antes de desistir. Se mesmo assim não resolver, marque a tarefa como `[!]` (bloqueada), escreva nela o que tentou e o que viu, e **siga para a próxima tarefa**. Coisas que só o usuário pode fazer (ouvir o som, jogar com controle, dizer se gostou) não param o trabalho: faça tudo o que dá sem ele e anote a pergunta na seção 10. Fale com o usuário **uma vez, no fim da sessão**, com um resumo curto do que foi feito e a lista da seção 10. Ao terminar uma tarefa, comece a próxima na hora.

O histórico detalhado (evidência de cada defeito antigo) está no apêndice no fim deste arquivo, no `PLANO.md` e no `ARQUITETURA.md`. Você não precisa ler o apêndice para executar as tarefas; consulte quando uma tarefa mandar.

---

## 1. Regras de ouro (nunca quebre)

1. **Commits:** só o usuário é autor. Nunca escreva `Co-Authored-By`, "Generated with Claude Code" nem qualquer menção a IA na mensagem de commit ou de PR. Mensagens em português, curtas.
2. **O que nunca vai para o git:** o ISO, o ELF, `extraido*/`, `output/`, binários, logs, `gsrec/` e imagens do jogo. Antes de cada commit, rode `git status --short` e confira a lista.
3. **`PS2Recomp/` não está no git deste projeto.** Você edita os arquivos dele no lugar (`/run/media/valk/linux_arquivos/projetos/ShaolinMonks/PS2Recomp/...`) e, antes de commitar, roda a rotina R10, que copia as mudanças para `patches/`. Arquivo novo dentro do `PS2Recomp` precisa de uma linha em `patches/arquivos.txt`.
4. **Nunca** rode `git checkout`, `git restore`, `git reset` ou `git stash` dentro do `PS2Recomp`. O trabalho só existe na árvore dele e no patch. Para poder desfazer uma edição, **antes de editar** copie o arquivo: `cp arquivo.cpp /tmp/arquivo.cpp.antes`. Para desfazer: `cp /tmp/arquivo.cpp.antes arquivo.cpp`.
5. **Diagnóstico temporário** (prints, contadores) sai antes do commit. Escreva sempre com a palavra `DIAG` no texto para achar depois: `grep -n DIAG arquivo.cpp` tem que dar vazio no commit.
6. **Toda mudança passa pelas rotinas R3 (testes) e R4 (replays)** antes do commit. Se um hash de replay mudar, ou você explica por que (a tarefa mandou mudar a imagem) e atualiza a tabela da R4 no mesmo commit, ou desfaz.
7. **`ARQUITETURA.md`** é atualizado no mesmo commit quando você muda threads, travas, o caminho de um quadro ou descobre uma armadilha nova.
8. **Não baixe nada da internet** sem pedir ao usuário (pacotes, bibliotecas, código).
9. **Responda o usuário em português**, curto e direto.
10. **Autonomia:** não pare para perguntar. Problema → investigue e tente de novo (três abordagens diferentes); ainda bloqueado → `[!]` na tarefa e próxima tarefa. Perguntas que só o usuário responde vão para a seção 10 e são mandadas todas juntas no resumo final da sessão.

## 2. Mapa do projeto

**Pastas** (caminhos absolutos):

| Nome | Caminho | O que tem |
|---|---|---|
| RAIZ | `/run/media/valk/linux_arquivos/projetos/ShaolinMonks` | checkout principal do git (branch `main`) |
| REPO | a pasta git onde você está trabalhando (a RAIZ ou uma worktree em `RAIZ/.claude/worktrees/...`) | docs, `scripts/`, `patches/` |
| PS2 | `RAIZ/PS2Recomp` | o recompilador e o runtime (código C++ que você edita) |
| BUILD | `RAIZ/PS2Recomp/out/build` | build do cmake |
| JOGO | `RAIZ/extraido_full/SLUS_210.87` | o executável do jogo (não vai para o git) |
| CENAS | `RAIZ/gsrec/*.gsr` | cenas gravadas do GS: `titulo menu dificuldade fase fase2 fase3 fase4 raiden filme selecao carregando hud pausa golpe` |

**Arquivos que as tarefas mencionam:**

| Arquivo (dentro de `PS2/ps2xRuntime/`) | O que é |
|---|---|
| `src/lib/gs/gs_hw_backend.cpp` | **renderizador novo** (`PS2X_GS_HW=1`), Vulkan próprio. Funções: `Renderer::draw`, `drawState`, `appendVertices`, `resolveTexture`, `resolveTextureSlow`, `loadClut`, `download`, `upload`, `present`, `GSHwBackend::Present`, `BeginTransfer` |
| `src/lib/gs/shaders/gs_hw.vert`, `gs_hw.frag` | shaders do renderizador novo (o `.frag` faz textura, paleta, TFX, teste de alfa, névoa) |
| `src/lib/gs/gs_cpu_backend.cpp` | renderizador atual (CPU) e espelho da memória de vídeo |
| `src/lib/gs/gs_gpu_accelerator.cpp`, `shaders/gs_tiles.comp` | renderizador atual na GPU (imita o chip pixel a pixel) |
| `src/lib/gs/gs_frontend.cpp` | recebe os comandos do jogo para o chip gráfico; `softenLightRay` (raios), `buildDrawBatch` (mipmap) |
| `src/lib/ps2_runtime.cpp` | janela: `UploadFrame` (mistura de campos, tamanho da textura), F11, captura de tela |
| `src/lib/Kernel/EeScheduler.cpp` | relógio do jogo, vsync, pulo de quadro |
| `src/lib/Kernel/Stubs/MPEG.cpp` | filmes (decodifica o `.SFD` com libav) |
| `../ps2xIOP/src/emulator/core/spu2_voices.cpp` | vozes de som e o reverb |
| `src/lib/ps2_spu2_stream.cpp` | saída de som (48 kHz) |

**Glossário mínimo:**
- **EE**: processador principal do PS2 (o código do jogo, já convertido para x86). **VU1**: processador que monta os modelos 3D. **GS**: chip que desenha. **IOP/SPU2**: processador e chip de som.
- **Campo**: o jogo desenha meia tela (640×224) por vez, alternando entre duas páginas de memória (`0x00` e `0x46`).
- **CLUT/paleta**: tabela de cores das texturas de 4 e 8 bits.
- **Alvo** (surface): imagem na placa de vídeo onde o renderizador novo desenha. **Espelho**: a cópia da memória de vídeo do PS2 no processador. "Cópia para o espelho" (download) = trazer um alvo da placa de volta; cada uma faz o processador **esperar a placa**, e é o que deixa o renderizador novo lento.
- **Replay**: rodar uma cena gravada do GS sem o jogo (`ps2x_gs_replay`). Rápido e repetível.

## 3. Rotinas (copie e cole)

Troque `<REPO>` pela sua pasta git. Use uma pasta temporária sua no lugar de `/tmp/mksm` se o seu ambiente der uma (scratchpad).

**R1. Antes de começar uma tarefa**
```bash
cd <REPO> && git status --short && git fetch origin && git log --oneline -1 origin/main
```
Esperado: `git status` vazio. Se você está numa worktree e o `origin/main` tem commits que você não tem: `git rebase origin/main`.

**R2. Compilar**
```bash
cmake --build /run/media/valk/linux_arquivos/projetos/ShaolinMonks/PS2Recomp/out/build --target ps2EntryRunner ps2x_gs_replay ps2x_tests 2>&1 | grep -E "error|Linking CXX exec"
```
Esperado: só linhas `Linking CXX executable`, nenhuma com `error`. Leva de segundos a poucos minutos. **Se passar de 10 minutos**, você mexeu num cabeçalho incluído pelo `ps2xRuntime/include/ps2_runtime.h` (recompila 13 mil arquivos). Evite: só mexa em `.cpp` e em cabeçalhos fora dessa cadeia (`grep -n include PS2/ps2xRuntime/include/ps2_runtime.h` mostra a cadeia).

**R3. Testes**
```bash
cd /run/media/valk/linux_arquivos/projetos/ShaolinMonks && for s in gs vu1 vu float; do echo "$s: $(PS2Recomp/out/build/ps2xTest/ps2x_tests $s 2>&1 | grep -iE 'failed' | tail -1)"; done
```
Esperado, exatamente: `gs: Failed: 33`, `vu1: Failed: 0`, `vu: Failed: 0`, `float: Failed: 0`. (As 33 falhas do `gs` são antigas. Não rode `ps2x_tests` sem argumento: a suíte `memory` aborta num assert antigo.)

**R4. Replays (imagem do renderizador atual)**
```bash
cd /run/media/valk/linux_arquivos/projetos/ShaolinMonks && for g in titulo menu dificuldade fase fase2 fase3; do echo "$g $(PS2Recomp/out/build/ps2xTest/ps2x_gs_replay gsrec/$g.gsr 2>&1 | tail -1)"; done
```
Esperado, exatamente:

| cena | hash |
|---|---|
| titulo | `832b159039f24acb` |
| menu | `d4d72ac4d224021a` |
| dificuldade | `53ed43d9645ded21` |
| fase | `380e36134058b593` |
| fase2 | `f7fe14a330aa7e0c` |
| fase3 | `0755c06223384f81` |

Mudanças só no renderizador novo (`gs_hw_backend.cpp`, `gs_hw.*`) não mudam esses hashes. Mudanças no `gs_frontend.cpp` também não aparecem aqui (as cenas gravam os comandos depois do frontend): teste essas no jogo (R5).

**R5. Rodar o jogo até a fase, medindo a velocidade**
```bash
D=/tmp/mksm; rm -rf $D; mkdir -p $D; cd $D; cp /run/media/valk/linux_arquivos/projetos/ShaolinMonks/PS2Recomp/imgui.ini .
PS2X_PERF=1 MKSM_SKIP_INTRO=1 MKSM_AUTO_PAD=xs MKSM_AUTO_PAD_UNTIL=40 MKSM_AUTO_HOLD="x@45-45.3,x@50-50.3,x@55-55.3,x@60-60.3,x@65-65.3,x@70-70.3,x@75-75.3,x@80-80.3,x@85-85.3,x@90-90.3,x@95-95.3,x@100-100.3,x@105-105.3,x@110-110.3,x@115-115.3,x@120-120.3" timeout 130 /run/media/valk/linux_arquivos/projetos/ShaolinMonks/PS2Recomp/out/build/ps2xRuntime/ps2EntryRunner /run/media/valk/linux_arquivos/projetos/ShaolinMonks/extraido_full/SLUS_210.87 > log.txt 2>&1
grep "perf\] janela" log.txt | awk -F'|' '{print NR*2"s", $3}' | tr '\n' ' '
```
Esperado: uma lista `2s vsync 60.0/s 4s ...`. **60/s = velocidade certa**; abaixo de 55 o jogo fica em câmera lenta. Linha do tempo: 0–40 s menus e seleção; ~48 s carregando a fase; 58–155 s a cutscene do Goro's Lair (o Raiden); depois a gameplay. Com o renderizador atual, as quedas conhecidas são em 64–68 s e 78–80 s (~33–36/s).
- Para o **renderizador novo**, ponha `PS2X_GS_HW=1` no começo da linha do jogo.
- A tela precisa estar destravada (com a tela travada a janela não abre).
- Para matar um jogo preso: `kill $(pgrep -x ps2EntryRunner)`. **Nunca** `pkill -f ps2EntryRunner` (mata o seu próprio shell).

**R6. Estatísticas do renderizador novo**
Rode a R5 com `PS2X_GS_HW=1 PS2X_GS_HW_STATS=1` no começo da linha do jogo e depois:
```bash
grep "gs:hw\]" /tmp/mksm/log.txt | tail -6
```
A cada ~2 s saem duas linhas. A que importa:
`[gs:hw] surface->mirror copies since last line: texread A, decode B, clut C, present D, transfer E, evict F, other G | mirror->surface uploads H`
Cada número é quantas vezes, nos últimos ~2 s, um alvo voltou para o espelho (cada uma é uma espera da placa). **Meta da Fase 2: todos perto de 0 durante a fase.** Hoje (13/11), na fase: `texread` ~30 e `decode` ~60 a cada 2 s.

**R7. Perfil (onde o tempo de uma thread vai)**
```bash
D=/tmp/mksm_perfil; rm -rf $D; mkdir -p $D; cd $D; cp /run/media/valk/linux_arquivos/projetos/ShaolinMonks/PS2Recomp/imgui.ini .
PS2X_PROFILE=65 PS2X_PROFILE_THREAD=GSFrontend MKSM_SKIP_INTRO=1 MKSM_AUTO_PAD=xs MKSM_AUTO_PAD_UNTIL=40 MKSM_AUTO_HOLD="x@45-45.3,x@50-50.3,x@55-55.3,x@60-60.3" timeout 110 /run/media/valk/linux_arquivos/projetos/ShaolinMonks/PS2Recomp/out/build/ps2xRuntime/ps2EntryRunner /run/media/valk/linux_arquivos/projetos/ShaolinMonks/extraido_full/SLUS_210.87 > /dev/null 2>&1
python3 <REPO>/scripts/perfil.py ps2_profile.txt /run/media/valk/linux_arquivos/projetos/ShaolinMonks/PS2Recomp/out/build/ps2xRuntime/ps2EntryRunner
```
`PS2X_PROFILE=65` começa a amostrar aos 65 s (dentro da cutscene). Threads: `GameThread`, `VU1Thread`, `GSFrontend` (onde roda o renderizador novo), `GSWorker` (renderizador atual). O `timeout` tem que ser bem maior que o número do `PS2X_PROFILE` (senão o arquivo não é gravado).

**R8. Captura de tela**
Na R5, acrescente `PS2X_SCREENSHOT_DIR=/tmp/mksm PS2X_SCREENSHOT_EVERY=600` (uma imagem a cada 600 quadros da janela, ~10 s). Abra as `frame_*.png` com a ferramenta de ler imagem. Prints do jogo original para comparar: `RAIZ/referencias_original/`.

**R9. Comparar os dois renderizadores nas cenas gravadas**
```bash
bash <REPO>/scripts/compara_gs.sh            # todas as cenas
bash <REPO>/scripts/compara_gs.sh raiden fase4
```
Esperado hoje (14/11): `menu ~47 dB`, `titulo ~51 dB`, `raiden ~29–30 dB`, `fase4 ~29 dB`, `pausa ~23 dB` (letras em posição subpixel; ver T09). Quanto maior, mais parecido (30+ é quase igual). As imagens ficam em `/tmp/compara_gs/`. Para gravar uma cena nova no jogo: na R5, acrescente `PS2X_GS_RECORD=/run/media/valk/linux_arquivos/projetos/ShaolinMonks/gsrec/<nome>.gsr PS2X_GS_RECORD_AT=<segundo> PS2X_GS_RECORD_FRAMES=2`.

**R10. Commit e envio**
```bash
cd <REPO>
scripts/atualizar_patches.sh /run/media/valk/linux_arquivos/projetos/ShaolinMonks/PS2Recomp   # só se mexeu no PS2Recomp
git add -A
git status --short        # confira: nada de ISO, ELF, extraido*, output/, binários, logs, gsrec/, imagens
git commit -m "<mensagem curta em português, sem Co-Authored-By>"
git push origin HEAD:main
```
Se o `atualizar_patches.sh` avisar `is new and not in patches/arquivos.txt`, acrescente no `patches/arquivos.txt` uma linha `<nome do arquivo>  <pasta dentro do PS2Recomp>` (veja as linhas que já existem) e rode de novo. Se o `git push` falhar por rede, espere e tente de novo; não force.

## 4. Armadilhas conhecidas (já custaram horas)

1. **Ferramentas de editar arquivo podem recusar caminhos do `PS2Recomp`** quando você está numa worktree. Edite com Python, trocando um trecho exato e conferindo que ele existe:
   ```bash
   python3 - <<'EOF'
   p='/run/media/valk/linux_arquivos/projetos/ShaolinMonks/PS2Recomp/ps2xRuntime/src/lib/arquivo.cpp'; s=open(p).read()
   old="texto exato que existe hoje"
   new="texto novo"
   assert old in s, "trecho não encontrado"; s=s.replace(old,new,1); open(p,'w').write(s)
   EOF
   ```
   Não apague linhas por número (`sed -i '16,17d'`): o número muda e o comando pode ser recusado.
2. **O shell padrão é zsh:** laços com listas de palavras não se dividem. Use `bash -c '...'` ou ponha o laço num script `.sh`.
3. **Pastas temporárias podem ser apagadas entre um dia e outro.** O que precisa durar vai para `scripts/` (no git) ou `gsrec/` (fora do git).
4. **Defeitos que aparecem às vezes** (o Kung Lao invisível foi assim): uma rodada não prova nada. Rode 3 a 4 vezes.
5. **Medidas de tempo variam** (temperatura, outros programas). Compare sempre o antes e o depois **um logo depois do outro**, 3 rodadas cada.
6. **Vulkan no renderizador novo:**
   - imagem só é liberada depois que o envio que a usa terminou (`retire` abre o bloco de comandos antes). Liberar cedo trava a placa (o kernel registra `Xid 31`; veja com `journalctl -k | grep -i xid`);
   - toda busca de textura no shader é limitada ao tamanho da imagem (`clamp` com `textureSize`); fora disso a placa pode travar;
   - memória da placa mapeada no processador: **só escrever, nunca ler** (ler é ~30× mais lento).
7. **Travamento do jogo sem erro:** quase sempre é espera de cerca da placa (GPU travada) ou deadlock entre a thread do GS e a janela. Veja o estado das threads com `for t in /proc/$(pgrep -x ps2EntryRunner)/task/*; do echo "$(cat $t/comm) $(cat $t/wchan)"; done | sort | uniq -c`.
8. **O replay não testa o `gs_frontend.cpp`** (raios, mipmap): teste no jogo.
9. **Gravações do GS (`gsrec/*.gsr`) guardam o que sai do frontend**, já com o nível de mipmap escolhido e os raios modificados. Mudou o `gs_frontend.cpp` (mipmap, raios)? **Grave a cena de novo** para testar; o replay das gravações antigas repete o comportamento antigo.
10. **Perfil de uma rodada só engana:** um perfil apontou 54% numa função que quase não roda (T07). Confira com dois perfis, ou com um contador temporário, antes de concluir.
11. **zsh conta arrays a partir de 1.** Em laço com array, use `bash -c '...'`.
12. **`rm -rf` dentro de `bash -c` é barrado pela checagem de segurança.** Use pastas novas (`mkdir -p` com outro nome) em vez de apagar.
13. **A tabela de filmes `0x4D3BA0` não segue a ordem dos nomes:** 0 MIDWAY, 1 BLITZ, 2 GAUNTLET, 3 OPENING, 4 END006, 5 END011, 6 FINAL, 7 ESRB. O jogo toca a trilha ADX 0 de cada `.SFD`.
14. **O som emulado estica o tempo (WSOLA) quando a emulação atrasa:** comparar áudio amostra a amostra com a referência só funciona no som nativo dos filmes. No emulado, compare espectros (T12, apêndice).

---

## 5. O que já foi feito (13–14/11, resumo)

Detalhes e números de cada item: apêndice, "Tarefas T01–T29".

- **Velocidade:** a cutscene do Raiden foi de 15–20 para 57–60 quadros/s no renderizador padrão, e a gameplay roda a 57–61/s (T07). A leitura do `CSR` não espera mais o GS desenhar tudo (`PS2X_GS_CSR_LAG`). Ainda há engasgos na cutscene (o usuário sente; medido 34–41/s em alguns trechos).
- **Filmes:** o som sai direto do `.SFD`, idêntico ao original amostra por amostra nos 8 filmes, e o chiado à direita sumiu (T12, T13; confirmado pelo usuário). Volumes geral, música e efeitos (T14).
- **Chiado na cutscene do Raiden:** o usuário diz que está suave agora (T14b tinha medido estalos no canal R; reabrir se voltar).
- **Tremida no menu:** o usuário confirma que acabou (T01–T03).
- **Renderizador novo (`PS2X_GS_HW=1`):**
  - imagem quase igual à do atual (menus 47–51 dB, fases 28–33 dB, T09), com DATE (T08);
  - 2x/3x corretos (T10);
  - lento: 20–32/s em 1x e 15–20/s em 2x (T07a/b, T10). É o único caminho para imagem mais nítida, e por isso ainda não é o padrão (T11).
- **Outros:** dois controles e vibração por porta (T16); menu principal inteiro abre e volta (T20); FXAA opcional (T22, o usuário achou que não resolve a qualidade); opções documentadas no `jogar.sh` (T25).
- **Ferramentas novas:** `MKSM_MOVIE=N` (toca o filme N primeiro); `MKSM_AUTO_HOLD` com `q t o` (quadrado, triângulo, círculo); `scripts/compara_gs.sh`; 14 cenas em `gsrec/`.

---

## 6. Lista de tarefas (faça na ordem)

Marque `[x]` com o resultado medido, ou `[!]` depois de três tentativas diferentes, e siga. As tarefas nascem do que o usuário viu jogando em 14/11 (prints no fim da conversa daquele dia):

- o chão sai **quadriculado**;
- as **luzes** ficam estranhas: raio com bordas em degrau e um raio com cores de arco-íris;
- ainda há **engasgo na cutscene do Raiden**;
- a imagem continua em **qualidade baixa**.

Referências do original em `RAIZ/referencias_original/`: `goro_1_kunglao_luta.webp` (chão liso) e `goro_0_raios_teto.png` (raios macios e translúcidos).

**Fase 1: ferramenta para pegar defeito na hora**
- [x] N01 Tecla F9 grava a cena do GS; F10 tira captura — `jogar.sh` arma por padrão (`gsrec/cena_<data>.gsr`, `capturas/`); testado: grava 2 quadros e o replay mostra a cena

**Fase 2: chão quadriculado (mipmap)**
- [x] N02 Confirmar a causa: um nível de mipmap por triângulo — confirmado: com o nível pelo maior Q do triângulo, o chão do fundo fica fino em vez de blocos
- [ ] N03 Mipmap por pixel (trilinear) no renderizador padrão
- [ ] N04 Mipmap por pixel no renderizador novo

**Fase 3: luzes**
- [ ] N05 Achar os desenhos dos dois raios dos prints
- [ ] N06 Corrigir as cores do raio arco-íris
- [ ] N07 Raios macios como no original (sem degraus)

**Fase 4: engasgo na cutscene do Raiden**
- [ ] N08 Medir as quedas: onde, quanto e qual thread segura
- [ ] N09 Tirar o gargalo das quedas (nenhuma linha abaixo de 58/s)

**Fase 5: imagem nítida**
- [ ] N10 Renderizador novo: menos custo por troca de estado
- [ ] N11 Renderizador novo: menos barreiras de GPU e thread própria de novo
- [ ] N12 Renderizador novo a 60/s em 2x na fase
- [ ] N13 Renderizador novo como padrão, com escala 2x (confirmar)

**Fase 6: o resto do jogo (com o usuário)**
- [ ] N14 Avançar no jogo, apontando defeitos com F9 (antigas T15, T17)
- [ ] N15 Saves, filmes do fim e dois controles (antigas T16, T18, T19)

**Fase 7: robustez e extras**
- [ ] N16 Teste de uma hora (antiga T26)
- [ ] N17 Build do zero e guia do jogador (antiga T27)
- [ ] N18 Outras máquinas e testes antigos com falha (antigas T28, T29)
- [ ] N19 Widescreen, controle configurável e latência, quadro cheio (antigas T21, T23, T24)

---

## 7. Tarefas em detalhe

### N01 F9 grava a cena; F10 tira captura
- **Por quê:** os defeitos aparecem em lugares a que o roteiro de botões não chega. O usuário aperta F9 onde vê o defeito, e a cena vira um `.gsr` reproduzível (R9).
- **Faça:**
  1. Em `ps2_runtime.cpp`, no laço da janela (onde trata `KEY_F11`), F9 liga um pedido atômico de gravação e F10 salva `TakeScreenshot` em `RAIZ/capturas/` com data e hora no nome.
  2. `GSRecordingBackend` (`gs/gs_recording_backend.cpp`) hoje só existe com `PS2X_GS_RECORD` e começa num segundo fixo. Faça ele existir também com `PS2X_GS_RECORD_DIR=<pasta>`: sem gravar até chegar o pedido do F9, aí grava 2 quadros em `<pasta>/cena_<data-hora>.gsr`, e pode gravar de novo no próximo F9. Antes, confira que repassar os comandos sem gravar não custa nada visível (R5 com e sem a variável).
  3. `scripts/jogar.sh` exporta `PS2X_GS_RECORD_DIR=$raiz/gsrec` por padrão, e o comentário explica F9/F10.
- **Pronto quando:** F9 no jogo gera um `.gsr` que o `ps2x_gs_replay ... --ppm` mostra igual à tela; F10 grava a imagem; vsync igual com e sem a gravação armada.
- **Seção 9:** "Quando vir um defeito, aperte F9 (grava a cena) e F10 (captura) e me diga o nome do arquivo."
- **Resultado (14/11):**
  - `gs_recording_backend.cpp`: com `PS2X_GS_RECORD_DIR`, o gravador fica armado sem gravar; `gsRecordScene()` (F9) faz ele gravar os 2 quadros seguintes em `cena_<data-hora>.gsr`, e ele pode gravar de novo depois. F10 salva a tela em `PS2X_CAPTURE_DIR`.
  - O `jogar.sh` arma os dois por padrão (`gsrec/`, `capturas/`, ambos fora do git).
  - Testado com uma chamada automática aos 70 s: saiu um `.gsr` de 24 MB que o `ps2x_gs_replay` reproduz (2 quadros).
  - O vsync da cutscene com o gravador armado (33–56/s) é o mesmo de sem ele.

### N02 Confirmar a causa do chão quadriculado
- **Por quê:** no chão o jogo usa mipmap trilinear (`TEX1`: `MXL=2`, `MMIN=5` LINEAR_MIPMAP_LINEAR, `L=0`, `K` de −11 a −17,5; medido na `hud.gsr`). No PS2 o GS escolhe e mistura os níveis **por pixel**. O `gs_frontend.cpp` (perto de `mipLevel`) escolhe **um nível por triângulo**, pela média do Q dos vértices. Triângulos grandes do chão, que vão de perto a longe, caem em níveis diferentes dos vizinhos: uns nítidos, outros borrados, formando um xadrez.
- **Faça:**
  1. Contador temporário no `gs_frontend.cpp`, no ponto do `mipLevel`, rodando o jogo até a primeira sala (roteiro da T15, no apêndice). Anote quantos primitivos usam nível 0, 1 e 2 e, para uma amostra, o Q de cada vértice (min/máx) e o nível escolhido.
  2. Prova direta: troque temporariamente a média pelo **maior** Q do triângulo (o nível mais nítido) e tire uma captura no mesmo lugar (R8). Se o xadrez some ou muda, a causa está confirmada. **Não deixe esse atalho:** ele faz o fundo cintilar.
- **Pronto quando:** a contagem e as duas capturas (média e maior Q) estão anotadas aqui.
- **Resultado (14/11):** variante temporária (`DIAG_MIP_MAXQ`, já retirada) com o nível pelo **maior** Q do triângulo, contra a média. Mesmo roteiro determinístico (`MKSM_PAD_CLOCK=reads`), captura aos ~150 s de janela na primeira sala. Com a média, o chão do fundo à direita tem blocos grandes e lisos (o quadriculado do print); com o maior Q, o mesmo chão fica fino e uniforme, e as pedras perto da câmera ficam iguais. O raio de luz vertical também muda (mais fino e definido com o maior Q). Causa confirmada: a escolha de um nível de mipmap por triângulo. O maior Q não serve de correção (faz o fundo cintilar); a correção é a N03.

### N03 Mipmap por pixel (trilinear) no renderizador padrão
- **Por quê:** é a correção do xadrez no renderizador que o usuário usa. O caminho de GPU do renderizador padrão (`shaders/gs_tiles.comp`, `sampleTexture`) lê a textura direto da memória do GS, então dá para escolher o nível por pixel sem montar textura nenhuma.
- **Faça:**
  1. **Frontend:** quando o backend avisar que faz mipmap por pixel, não troque o `TEX0` pelo nível; mande o nível 0 e deixe `TEX1`, `MIPTBP1` e `MIPTBP2` no `GSDrawState` (já vão no `context`). Não mexa em `gs_types.h` (recompila 13 mil arquivos). Use uma consulta num header só do runtime, como `gs_hw_backend.h`, que o `gs_frontend.cpp` já inclui. Interruptor: `PS2X_MIP_PIXEL=0` volta ao nível por triângulo.
  2. **Registro do primitivo:** no `GSCpuBackend`, onde o registro de 64 palavras é montado para o `gs_tiles.comp` (procure quem escreve `rec[base + 58]`, a versão da CLUT), acrescente:
     - `MXL`, `MMIN`, `L`, `K` e `LCM`;
     - para cada nível de 1 a `MXL`, o `TBP` e o `TBW` (de `MIPTBP1`, níveis 1–3, e `MIPTBP2`, níveis 4–6, 14+6 bits cada).

     Confira antes quantas palavras livres o registro tem.
  3. **Shader** (`sampleTexture`):
     - por pixel: `lod = LCM ? K : log2(1/|q|) * 2^L + K`, com o mesmo `q` que já divide `s` e `t`;
     - `lod <= 0`: nível 0 com o filtro de ampliação (`MMAG`);
     - senão, pelo `MMIN`: 2 e 3 usam o nível `round(lod)`; 4 e 5 misturam `floor(lod)` e o seguinte, por `fract(lod)`. Os pares ímpares (3 e 5) filtram dentro do nível com bilinear; os pares (2 e 4), com ponto;
     - no nível `n` a largura é `texW >> n`, a altura `texH >> n`, e o `u` é dividido por `2^n`;
     - a CLUT é a mesma em todos os níveis.
  4. **Caminho da CPU** (o rasterizador por bandas, quando a GPU não pega o primitivo): implemente a mesma conta, ou mantenha o nível por triângulo e deixe isso escrito no código (`shortcut:`).
  5. **Verificação:**
     - grave a cena de novo (armadilha 9) e compare a captura do mesmo lugar com `goro_1_kunglao_luta.webp`;
     - R3 e R4: os hashes das cenas antigas não mudam, porque a gravação já traz o nível;
     - R5: vsync igual na fase;
     - R9 nas cenas novas.
- **Pronto quando:** o chão do print do usuário fica liso (captura antes/depois anotada aqui), sem cintilação nova ao longe, e o vsync da fase não cai mais que 1/s.
- **Commit:** "Mipmap por pixel (trilinear) no renderizador padrão: chão sem quadriculado".
- **Seção 9:** "O chão ainda aparece quadriculado?"

### N04 Mipmap por pixel no renderizador novo
- **Faça:** a antiga T10b (apêndice): o LOD pelo Q interpolado no `gs_hw.frag` e os níveis num atlas. Use a mesma consulta e o mesmo interruptor da N03. Antes, compare as capturas dos dois renderizadores na mesma cena gravada **depois** da N03.
- **Pronto quando:** PSNR igual ou melhor que antes nas cenas gravadas depois da N03, e chão liso no renderizador novo também.

### N05 Achar os desenhos dos dois raios dos prints
- **Por quê:** os defeitos aparecem **sempre nos mesmos lugares** (usuário, 14/11: "parece problema em interpretar as luzes do jogo"). Print 1, raio branco-amarelo grande com bordas em degrau; print 2, raio com faixas magenta, ciano e verde. No original (`goro_0_raios_teto.png`) os raios são faixas macias e translúcidas.
- **O que já se sabe:**
  - `softenLightRay` (`gs_frontend.cpp`, `PS2X_RAYS`) troca o blend dos raios do Goro's Lair para `Cs*As + Cd`. Ele reconhece a paleta `0x2F8B` e as texturas `0x2F5F`/`0x2F7F`, e o pilar `0x2728`.
  - Medido em 14/11: `0x2F5F` tem mipmap (`MXL=2`), com os níveis 1 e 2 em `0x2F7F` e `0x2F87` (`MIPTBP1`). Ou seja, a "outra textura" `0x2F7F` é o nível 1 da mesma.
- **Faça:**
  1. Grave as duas cenas (com F9 da N01, ou pelo roteiro; a `hud.gsr` e a `golpe.gsr` já têm raios brancos).
  2. No replay, ache os desenhos dos raios (`tbp0` e `cbp` acima) e anote, para cada um: blend (`ALPHA`), `TEST`, filtro, nível de mipmap usado, `TEX0.CSA`/`CPSM` e a cor dos vértices.
  3. O usuário testou com `PS2X_RAYS=0` em 14/11 (resposta na seção 10). Capture a mesma cena com `PS2X_RAYS=0` (o desenho original, sem a troca de blend) e com `PS2X_GS_HW=1`, para ver se o arco-íris depende da troca de blend, do renderizador ou do nível de mipmap.
- **Pronto quando:** a tabela "raio → registradores → captura com/sem `PS2X_RAYS`" está aqui, com a hipótese principal para cada defeito.

### N06 Corrigir as cores do raio arco-íris
- **Faça:** conforme a N05. Com o teste do usuário (`PS2X_RAYS=0` não muda nada, seção 10), o candidato 2 abaixo está descartado. Faça a N03 antes e capture os raios de novo: o mais provável é o mesmo defeito do chão. Candidatos, na ordem:
  1. **Nível de mipmap de textura 4 bits** lendo a CLUT ou os texels do lugar errado, por `CSA`/`CBP` diferente no nível, ou largura do nível errada. Com a N03 o nível passa a ser por pixel: confira de novo depois dela.
  2. **Soma do `Cs*As + Cd` do `softenLightRay`** estourando canais de forma diferente (cor de vértice > 0x80 multiplicada pela textura). Compare com o blend original.
  3. **Paleta trocada no meio do lote** (versão de CLUT do registro, `rec[base + 58]`).

  Uma cena gravada antes e depois, o PSNR do resto da cena igual e a captura do lugar do print.
- **Pronto quando:** o raio do print 2 fica branco-azulado como os outros, nas duas capturas (padrão e `PS2X_GS_HW=1`).

### N07 Raios macios como no original
- **Por quê:** no print 1 as bordas do raio são degraus duros e o raio parece um bloco sólido ("quadradas e sólidas", usuário; igual com `PS2X_RAYS=0`); no original são gradientes translúcidos.
- **Faça:**
  1. Veja na N05 se o raio usa nível de mipmap baixo com filtro de ponto (degraus = texels grandes ampliados) ou se a textura tem alfa em degraus.
  2. Depois da N03, capture de novo: o nível por pixel pode resolver.
  3. Se não resolver, uma melhoria com interruptor: filtro bilinear forçado nos desenhos reconhecidos como raio (pelo mesmo reconhecimento do `softenLightRay`) e alfa da borda suavizado.

  Compare com `goro_0_raios_teto.png`.
- **Pronto quando:** as bordas dos raios ficam macias na captura do lugar do print 1. Seção 9: "Os raios ficaram parecidos com os do original?"

### N08 Medir as quedas da cutscene do Raiden
- **Por quê:** o usuário sente engasgo na cutscene (14/11), **principalmente nos momentos sem fala do Raiden: quando ele chega e quando ele aponta** (provavelmente os trechos com o raio dele, bloom e partículas). Medido: o vsync cai para 34–41/s em alguns trechos (por volta de 100–120 s na R5), e a média da sessão dele foi 54/s, com 1/4 do tempo abaixo de 50.
- **Faça:**
  1. R5 com `PS2X_PERF=1` três vezes; anote os segundos de cada queda abaixo de 55/s.
  2. Nesses segundos, CPU por thread (script de `/proc/<pid>/task/*/stat`, igual ao da T07 no apêndice) e perfis (R7) da `GameThread`, `VU1Thread`, `GSFrontend` e `GSWorker`. Compare com um trecho bom.
  3. Contadores temporários das esperas: `ps2_vu1_thread::sync`, fila do `GSCpuBackend` cheia (`Enqueue`), `WaitForQueue` e `gsAsyncDrain`. Ver T07 e E5 no apêndice: antes, o gargalo era o `GSWorker` saturado (`QueuePrimitive`, `PrepareGpuPrimitive`, CLUT) com a `VU1Thread` esperando por ele.
- **Pronto quando:** cada queda tem a thread que satura e o que ela faz anotados aqui.

### N09 Tirar o gargalo das quedas
- **Faça:** conforme a N08. Candidatos já levantados (apêndice E5):
  1. Dividir o `GSWorker` em duas threads: uma prepara os registros e as cargas de CLUT, a outra grava os comandos Vulkan (~40% do tempo em cada metade).
  2. Baratear a cópia do registro de 256 bytes no `QueuePrimitive`.
  3. Juntar cargas de CLUT repetidas.

  Medir antes e depois, lado a lado, três rodadas cada (armadilha 5).
- **Pronto quando:** `PS2X_PERF` sem linha abaixo de 58/s na cutscene, em três rodadas; R3 e R4 iguais. Seção 9: "Ainda sente engasgo na cutscene?"

### N10 Renderizador novo: menos custo por troca de estado
- **Por quê:** é o caminho para imagem nítida (escala 2x/3x); hoje faz 20–32/s em 1x.
  - Medido (T07a/b, apêndice): ~750 mil primitivos/s numa thread só.
  - O atalho de estado acerta 94%, mas cada falha (`drawState`) custa ~6 µs, e são ~50 mil/s: metade de feedback, um terço porque `generation` sobe a cada carga de CLUT ou transferência.
- **Faça:**
  1. Faça `generation` invalidar só o que depende do que mudou. Uma carga de CLUT só afeta lotes com textura de paleta; uma transferência só afeta lotes cuja textura toca os blocos escritos.
  2. Perfil do `drawState` (R7 com contadores por parte: `surface()`, `upload`, `resolveTexture`, `pipeline`, `flushBatch`) e corte do que mais pesa.
  3. Feedback: desenhos seguidos que leem regiões já copiadas não precisam de nova cópia (já existe a lista de retângulos; veja se dá para não quebrar o lote).
- **Pronto quando:** fase com `PS2X_GS_HW=1` em 1x ≥ 45/s, PSNR igual.

### N11 Renderizador novo: menos barreiras de GPU e thread própria
- **Faça:**
  1. Troque o `fullBarrier` (ALL_COMMANDS→ALL_COMMANDS) por barreiras estreitas (cópia→fragmento, fragmento→cópia), e evite reabrir o passo de render depois de cada cópia quando a cópia pode vir antes.
  2. Teste de novo a `GSHwWorker` (`PS2X_GS_HW_THREAD=1`, apêndice T07b): ela ficou mais lenta porque a GPU virava o limite.
- **Pronto quando:** com a thread ligada, a fase em 1x fica mais rápida que sem ela, e a janela a 60 fps.

### N12 Renderizador novo a 60/s em 2x na fase
- **Faça:** R5 com `PS2X_GS_HW=1 PS2X_SCALE=2`. O que sobrar de custo de GPU em 2x (cópias, sombra, leituras de volta) vira item aqui.
- **Pronto quando:** fase em 2x ≥ 58/s, capturas nítidas e certas.

### N13 Renderizador novo como padrão, com escala 2x (confirmar)
- **Faça:** a antiga T11 (apêndice), com `PS2X_SCALE=2` como padrão quando a placa aguentar. Antes, rode o jogo para o usuário com as duas opções e pergunte qual ele prefere.
- **Pronto quando:** R5 sem variável nenhuma a 60/s em toda a fase, em 2x. Seção 9: "Jogou com o renderizador novo? Ficou mais nítido? Algum defeito novo?"

### N14 a N19
- **N14:** com o usuário jogando (F9/F10 da N01). Cada defeito vira uma tarefa nova aqui, com a cena gravada. Raios de outras fases: antiga T17.
- **N15:** antigas T16 (dois controles: já feito, falta o usuário testar), T18 (saves) e T19 (filmes do fim no fluxo real).
- **N16–N18:** antigas T26–T29 (apêndice).
- **N19:** antigas T21, T23 e T24 (apêndice).

---

## 8. Onde estamos (14/11, depois do teste do usuário)

| Área | Estado | Tarefas |
|---|---|---|
| Abertura e filmes | Os 8 `.SFD` tocam; som direto do arquivo, sem chiado (usuário confirmou) | N15 |
| Menus | 60/s; sem tremida (usuário confirmou); todas as opções abrem e voltam | — |
| Goro's Lair, velocidade | Gameplay 57–61/s; cutscene do Raiden melhorou muito, mas ainda engasga | N08, N09 |
| Goro's Lair, imagem | **Chão quadriculado** (mipmap por triângulo); **raios com degraus e um com arco-íris** | N02–N07 |
| Nitidez | 640×224 por campo; escala 2x/3x só no renderizador novo, que está lento | N10–N13 |
| Som | Bom (usuário); volumes no `mksm.ini`; eco 22 aprovado | — |
| Controle | Dois controles (falta testar); vibração por porta | N15 |
| Saves | Carregar funciona; autosave no meio da fase não testado | N15 |
| Resto do jogo | Não jogado além da primeira sala | N14 |

## 9. O que é "perfeito" (critério final)

O usuário decidiu (12/11): não precisa ser 100% fiel ao PS2; se ficar melhor, melhor (cada melhoria com um interruptor para voltar ao fiel). Está pronto quando:
1. joga do começo ao fim sem travar (abertura, todas as fases e chefes, filmes, créditos, extras, 2 jogadores);
2. 60 quadros/s estáveis em toda cena na máquina do usuário (i7-7700HQ + GTX 1050);
3. imagem igual ou melhor que a do PS2 (chão e raios como no original, nítida em 2x/3x, sem tremer);
4. som limpo e filmes sincronizados;
5. controle, saves e janela sem atrito;
6. uma hora de jogo sem travar nem vazar memória, e build do zero só com o README.

## 10. Para o usuário (juntar aqui, não parar)

Perguntas que só o usuário pode responder. **Não pare o trabalho por elas.** Mande a lista inteira no resumo final. Quando ele responder, anote a resposta e apague a pergunta.

- Respostas de 14/11 (2ª rodada): engasgo principalmente quando o Raiden chega e quando ele para de falar (sem fala); luzes estranhas sempre nos mesmos lugares.
- **Teste com `PS2X_RAYS=0` (14/11, 3ª rodada):** o raio arco-íris e os degraus **continuam todos**, nos mesmos lugares; as luzes ficam "quadradas e sólidas, muito feio", como antes; o chão continua quadriculado. **Conclusão: não é a troca de blend do `softenLightRay`**, é a forma como os raios são desenhados (textura/mipmap/filtro). Medido na mesma sessão: duas quedas de ~6 s a 31–36/s na cutscene (chegada do Raiden e fim da fala), gameplay 52–60/s.
- Respostas de 14/11: filmes sem chiado; cutscene do Raiden sem chiado ("suave"); som e eco bons; tremida do menu resolvida; FXAA não resolve a qualidade baixa; dois controles ainda não testados; não jogou além da primeira sala; ainda sente engasgo na cutscene; chão quadriculado e luzes estranhas "faz tempo".
- (N01, quando pronta) Ao ver um defeito, aperte F9 (grava a cena) e F10 (captura) e diga o nome do arquivo.
- (N03, quando pronta) O chão ainda aparece quadriculado?
- (N07, quando pronta) Os raios ficaram parecidos com os do original?
- (N09, quando pronta) Ainda sente engasgo na cutscene?

---

# Apêndice: histórico

## Tarefas T01–T29 (13–14/11): lista com resultados

Marque `[x]` quando terminar uma tarefa e escreva nela o resultado medido (números). Marque `[!]` quando ficar bloqueada depois de três tentativas diferentes, com o motivo, e siga para a próxima. **Nenhuma tarefa espera o usuário:** as marcadas **(confirmar)** têm uma parte que só ele pode conferir; faça todo o resto, deixe a correção mais provável pronta (se tiver dúvida, atrás de uma variável de ambiente) e escreva a pergunta na seção 9.

**Fase 1: defeitos que o usuário vê hoje**
- [x] T01 Tremida: gravar os quadros e achar o quadro errado
- [x] T02 Tremida: corrigir a causa achada na T01 (confirmar) — não aplicada: a medida mostrou que a mistura de campos reduz a cintilação
- [x] T03 Tremida: se não foi a mistura de campos, corrigir pelo quadro errado — sem quadro errado; resto vai para T08 (mipmap por pixel) e T10 (escala)

**Fase 2: renderizador novo rápido e certo** (meta: fase a 60/s com `PS2X_GS_HW=1`)
- [x] T04 Saída de vídeo sem copiar todos os alvos
- [x] T05 Ler alvos como textura de 8/4 bits na placa — `texread`/`decode`/`clut`/`transfer` na fase: ~100 → ~20 por segundo; PSNR igual; a fase não ficou mais rápida porque o gargalo era outro (T07)
- [x] T06 Paleta aplicada no shader para todas as texturas de paleta — não aplicada: medido, as decodificações já são raras (1–5 a cada 2 s na fase)
- [x] T07 Medir a fase e decidir o próximo corte — o regulador de quadro esperava o GS inteiro a cada leitura do `CSR`; corrigido (`PS2X_GS_CSR_LAG`): fase 15–20 → 35–60/s no renderizador atual, 17–30/s no novo
- [x] T07a Frontend do renderizador novo saturado na fase — 17–30 → 20–31/s: esperas de GPU e do espelho eliminadas; o resto é custo por primitivo numa thread só (T07b)
- [!] T07b Renderizador novo numa thread própria — feita, mas mais lenta (a GPU fica atrás); desligada por padrão (`PS2X_GS_HW_THREAD=1`). Ficou o reuso da cópia de feedback (225 → 55 por quadro). Fase 20–32/s
- [x] T08 Fidelidade do shader — DATE feito (fase 27,4 → 27,7 dB, raiden 23,7 → 24,1); PABE, COLCLAMP desligado e blends (1+F) não aparecem nas gravações; mipmap por pixel virou T10b
- [x] T09 Conferência cena a cena — 6 cenas novas; achado o meio pixel dos triângulos no novo: menus 26 → 47–51 dB, fases 24–27 → 28–33 dB; pausa 23 dB explicada
- [!] T10 Resolução interna 2x e 3x — imagem certa e nítida em 2x/3x (brilho, HUD, nada deslocado); fase 15–20/s em 2x, 7–11/s em 3x: esbarra no custo por primitivo (T07b)
- [ ] T10b Mipmap por pixel no renderizador novo (LOD pelo Q interpolado) — adiada: só aparece com a T11
- [!] T11 Renderizador novo como padrão — bloqueada: exige 60/s em toda a fase, e o novo faz 20–30/s (T07b)

**Fase 3: som**
- [x] T12 Chiado: medir todos os filmes sozinho — achado: no caminho emulado, acima de 15 kHz o canal R tem 1,9–5,3 dB mais agudos que o L em GAUNTLET, OPENING, END006, END011 e FINAL (referência equilibrada)
- [x] T13 Áudio dos filmes tocado direto do arquivo — idêntico bit a bit à trilha ADX 0 nos 8 filmes; agudos de R e L como na referência; OPENING termina com imagem e som juntos (±2 s)
- [x] T14 Volumes no `mksm.ini` — `PS2X_VOL_MASTER/MUSIC/SFX` medidos: 50 dá amplitude 0,500
- [!] T14b Chiado à direita fora dos filmes — achado na cutscene do Raiden (R +10 a +25 dB acima de 15 kHz); o PCM do jogo já chega com o R descontínuo a cada bloco de 256 amostras; depende do ritmo da emulação

**Fase 4: o jogo inteiro**
- [x] T15 Avançar no jogo o máximo possível com roteiros de controle e saves — roteiro cego anda pela primeira sala do Goro's Lair; ~4 min de gameplay a 57–61/s, sem tela preta nem travamento; passar portas precisa de alguém jogando (seção 9)
- [x] T16 Segundo controle (2 jogadores) — porta 1 lê o controle 1 do raylib (sem teclado); vibração por porta
- [ ] T17 Raios suavizados nas outras fases — espera alguém chegar a outra fase (T15)
- [ ] T18 Saves: autosave, cartão cheio, save do meio do jogo — parte do usuário; medido: o jogo lê o cartão sem erro e não grava na primeira sala
- [ ] T19 Filmes de meio e de fim no fluxo real — espera alguém chegar lá; os 8 filmes tocam com imagem e som certos pelo `MKSM_MOVIE` (T13)
- [x] T20 Todas as opções do menu principal — Single Player, Ko-op, Versus, Load Game, Options e Kontent abrem e voltam (triângulo), 60/s, sem travar

**Fase 5: melhor que o original**
- [ ] T21 Quadro cheio em vez de campo (com escala 2x)
- [x] T22 Suavização de bordas (FXAA) — filtro próprio na janela, `PS2X_FXAA=1` (desligado por padrão)
- [ ] T23 Widescreen 16:9
- [ ] T24 Controle configurável e latência de entrada
- [x] T25 Configuração completa no `mksm.ini` — todas as opções de uso no cabeçalho do `scripts/jogar.sh`

**Fase 6: robustez e entrega**
- [ ] T26 Teste de uma hora
- [ ] T27 Build do zero e guia do jogador
- [ ] T28 Outras máquinas (sem Vulkan, Intel)
- [ ] T29 Testes antigos com falha e limpeza de variáveis

---

## Tarefas T01–T29 em detalhe

Formato de cada tarefa: **Por quê** · **Faça** · **Pronto quando** · **Se der errado** · **Commit**.

### T01 Tremida: gravar os quadros e achar o quadro errado
- **Por quê:** o usuário vê a tela "dar uma flicada" ao escolher o personagem e texturas tremendo no jogo. Já descartado: meia linha de diferença entre os campos (medido, não ajuda). Suspeitos: (1) a mistura de campos da janela (média do campo atual com o anterior, `PS2X_DEINTERLACE`), que junta imagens de instantes diferentes; (2) o cache de texturas/paletas do renderizador atual na GPU trocando a paleta em alguns quadros; (3) a janela mostrando por um quadro a página que ainda está sendo desenhada.
- **Faça:**
  1. Faça uma cópia de `ps2_runtime.cpp` (regra 4). Ache o bloco da captura de tela (procure `PS2X_SCREENSHOT_EVERY`). Acrescente, só para diagnóstico, dois limites lidos de `DIAGSHOTFROM`/`DIAGSHOTTO` (variáveis de ambiente) e só grave quando `shotFrame` estiver entre eles.
  2. R2. Rode a R5 com `PS2X_SCREENSHOT_DIR=/tmp/mksm PS2X_SCREENSHOT_EVERY=1 DIAGSHOTFROM=<n1> DIAGSHOTTO=<n2>`, cobrindo o instante em que o X do roteiro escolhe o personagem. Comece com 1800 a 2600; se as imagens não mostrarem a tela de seleção mudando para a fase, ajuste o intervalo e rode de novo. Use `timeout 70` (não precisa chegar à fase).
  3. Ache os quadros que destoam dos vizinhos:
     ```bash
     python3 - <<'PY'
     import numpy as np, glob
     from PIL import Image
     fs=sorted(glob.glob('/tmp/mksm/frame_*.png')); prev=None; out=[]
     for f in fs:
         a=np.asarray(Image.open(f).convert('L').resize((320,224))).astype(float)
         if prev is not None: out.append((round(np.abs(a-prev).mean(),1), f))
         prev=a
     med=np.median([v for v,_ in out]); print('mediana', med)
     print([o for o in out if o[0] > 4*med+2])
     PY
     ```
     Um corte de câmera ou a troca de tela também dá um salto: só conta como "quadro errado" o que destoa do anterior **e** do seguinte (A, B, A).
  4. Repita os passos 2 e 3 com `PS2X_DEINTERLACE=0` (mistura desligada) e depois com `PS2X_GS_GPU=0` (renderizador atual na CPU; lento, use um intervalo menor).
  5. Anote aqui quantos quadros errados cada rodada teve e o que eles mostram (outra página? textura com cor errada? imagem pela metade?).
- **Pronto quando:** a causa está anotada aqui: "some com `PS2X_DEINTERLACE=0`" → T02; "some com `PS2X_GS_GPU=0`" → T03 (cache da GPU); "aparece nos três" → T03 (página errada na janela). Se nenhuma rodada mostrar quadro errado, aumente o intervalo, tente também durante a gameplay (~160 s) e, depois de três tentativas sem achar, faça a T02 mesmo assim (a mistura é o suspeito mais provável) e marque a pergunta na seção 9.
- **Ao terminar:** restaure o `ps2_runtime.cpp` da cópia (o diagnóstico não vai para o commit).
- **Resultado (13/11):** três gravações da janela, com captura reduzida (`ImageResize`, diagnóstico) para poder gravar todo quadro:
  - quadros 1650–2250 (seleção do Liu Kang, caixa de dificuldade, tela preta, entrada do Goro's Lair, cutscene): 348 quadros, **0 quadros errados** (A, B, A), e diferença exatamente 0 entre quadros na tela de seleção parada (nada pisca);
  - quadros 2200–2650: 295 quadros, 0 quadros errados;
  - quadros 2400–2520 em 640×448 durante a câmera da cutscene, contando pixels que sobem e descem mais de 24 níveis em 3 quadros: **com a mistura de campos (padrão) 0,000% por quadro (mediana); com `PS2X_DEINTERLACE=0`, 1,399%**. Os picos de ~57% são cortes de câmera e acontecem nos dois.
  - Conclusão: a mistura de campos **tira** a cintilação entre campos; desligá-la (T02) pioraria. Não há quadro errado nem página errada na janela. O que sobra do relato ("algumas texturas tremem durante o jogo") deve ser serrilhado de textura em movimento (amostragem por ponto e mipmap de um nível por primitivo): vai para T08 (mipmap por pixel) e T10 (resolução interna 2x/3x). Pergunta na seção 9.

### T02 Tremida: desligar a mistura de campos por padrão (confirmar)
- **Faça:**
  1. Em `PS2/ps2xRuntime/src/lib/ps2_runtime.cpp`, ache a linha com `PS2X_DEINTERLACE` (dentro de `UploadFrame`). Hoje ela liga a mistura quando a variável **não** é `0`. Troque para ligar só quando a variável for `1`: a expressão `return !(v && *v == '0');` vira `return v && *v == '1';`. Ajuste o comentário logo acima ("PS2X_DEINTERLACE=1 turns it on").
  2. R2, R3, R4 (os hashes não mudam). Repita a medição da T01 sem variável nenhuma: nenhum quadro errado.
  3. Atualize `ARQUITETURA.md` (parágrafo "Janela (host)") e o comentário de opções em `scripts/jogar.sh`: agora é `PS2X_DEINTERLACE=1` que liga.
  4. Seção 9: "A tremida na escolha do personagem sumiu?".
- **Pronto quando:** compila, testes e hashes iguais, e a medição da T01 não acha quadro errado.
- **Commit:** "Janela: mistura de campos desligada por padrão (tremia); PS2X_DEINTERLACE=1 liga".

### T03 Tremida: corrigir pelo quadro errado
- **Faça:** conforme a T01:
  - **Some com `PS2X_GS_GPU=0`** (cache da GPU): grave a cena (R9) no instante do quadro errado e rode `ps2x_gs_replay` com e sem `PS2X_GS_GPU=0`. Se os hashes diferem, o renderizador na GPU erra: compare as imagens (`--ppm`), ache a textura com cor errada e olhe a chave do cache de texturas/CLUT em `gs_gpu_accelerator.cpp` (procure `clutHash` e `shortcut: CLUT matched by a 64-bit hash`). A correção provável é pôr as 512 entradas da CLUT na chave em vez do hash.
  - **Aparece nos três** (página errada na janela): em `gs_frontend.cpp`, `latchHostPresentationFrame` e a lógica de "preferred display source" escolhem a página mostrada; anote qual página foi mostrada no quadro errado (`displayFbp`) e faça a janela só trocar de página quando o jogo trocar o `DISPFB`.
  - R2, R3, R4, e a medição da T01 sem quadro errado.
- **Pronto quando:** a medição da T01 não acha quadro errado. Se depois de três abordagens não resolver, marque `[!]` com o que tentou e siga.

### T04 Saída de vídeo sem copiar todos os alvos
- **Por quê:** quando o renderizador novo não acha o alvo da tela (menus de 512 de largura, filmes), `GSHwBackend::Present` chama `downloadAll()`, que copia **todos** os alvos para o espelho a cada quadro da janela (60 vezes por segundo).
- **Faça:**
  1. Em `gs_hw_backend.cpp`, em `GSHwBackend::Present`, no trecho com o comentário "No display surface", troque `m_renderer->downloadAll();` por uma cópia só dos alvos que a tela lê:
     - calcule o bloco base e a largura da tela a partir do `request` do mesmo jeito que `Renderer::present` faz no começo (`dispfb`, `fbp`, `fbw`, `w`, `h`);
     - `uint32_t b0, b1; blockRange(GSInternal::framePageBaseToBlock(fbp), fbw, GS_PSM_CT32, w, h, b0, b1);`
     - `m_renderer->flushBatch(); m_renderer->downloadRange(b0, b1);`
  2. R2. Rode a R6 parando no menu (`timeout 45` em vez de 130): na linha `copies`, `present` deve cair para perto de 0. Depois a R5 completa com `PS2X_GS_HW=1`: os filmes e os menus continuam aparecendo (R8).
- **Pronto quando:** `present` ≈ 0 nos menus e a imagem dos menus e filmes continua certa.
- **Commit:** "Renderizador novo: saída sem alvo casado copia só a área da tela".
- **Resultado (13/11):** a cópia sem alvo casado agora traz só os alvos que cobrem a área lida por cada circuito ligado, e o quadro é montado direto do espelho (o `GSCpuBackend` só monta quadro dos desenhos que ele mesmo fez, e no modo novo ele não desenha nada). Na abertura e nos menus, `present` caiu para 0–2 a cada ~2 s. **Defeito achado e corrigido no caminho:** com `PS2X_GS_HW=1` o filme MIDWAY saía magenta. O `GS::copyLatchedHostPresentationFrame` (`gs_frontend.cpp`) supunha linhas de 640 pixels, e o renderizador novo entrega as linhas compactadas (512 no filme, 1280+ na escala 2x); agora deduz pelo tamanho do buffer. Filme e aviso de autosave conferidos nas capturas. R3 e R4 iguais.

### T05 Ler alvos como textura de 8/4 bits na placa (o maior ganho)
- **Por quê:** na fase, o efeito de desfoque (`0x2980`) e as cópias de Z (`0x2580`) leem pixels que a placa desenhou **como textura de paleta de 8 bits** (truque do PS2 para separar um canal de cor). Hoje cada leitura traz o alvo inteiro para o espelho (`texread` e `decode` na R6). A solução é a placa achar sozinha, para cada texel da textura, qual pixel do alvo e qual byte dele contém o índice.
- **Tamanho:** grande. Faça em sub-passos e compile/teste a cada um.
- **Faça:**
  1. **Confirme a convenção de endereço** (10 minutos, evita horas de erro). `GSMem::PixelAddress(psm, bp, bw, x, y)` devolve o índice do pixel "em unidades da largura de armazenamento do formato": para PSMT8 é o byte, para PSMT4 é o meio byte (nibble), para PSMCT32 é a palavra de 32 bits. Escreva um teste pequeno em `PS2/ps2xTest/src/ps2_gs_tests.cpp` (copie o formato dos testes que já estão lá): grave com `GSMem::WriteP8` um valor num texel T8, leia a memória com `GSMem::ReadCT32` no pixel de 32 bits calculado pela sua fórmula (palavra = byte / 4; byte dentro da palavra = byte % 4) e confira o valor. Faça o mesmo para T4 (nibble) e para T8H (o índice fica no byte mais alto de uma palavra CT32). Só siga quando o teste passar.
  2. **Tabela de mapeamento** (no processador, uma vez por par textura/alvo): para cada texel (u, v) da textura, calcule o endereço com `PixelAddress(tex.psm, tex.tbp0, tex.tbw, u, v)`, converta para (palavra, byte, nibble) e ache o pixel (x, y) do alvo que guarda essa palavra. Para achar o pixel, monte antes um mapa inverso do alvo: para cada pixel (x, y) do alvo, `palavra = PixelAddress(GS_PSM_CT32, alvo.block, alvo.fbw, x, y)` e guarde (x, y) num vetor indexado por `palavra - alvo.block * 64`. Guarde o resultado numa imagem Vulkan `VK_FORMAT_R16G16_UINT` do tamanho da textura: canal R = `x | (byte << 12) | (nibble << 14)`, canal G = `y`. Guarde num cache (chave: tbp, tbw, psm, tw, th, bloco e fbw do alvo).
  3. **Paleta como imagem:** uma imagem 256×1 `VK_FORMAT_R8G8B8A8_UNORM` com as cores de `clutColor(state, i)` para i = 0..255 (ou 0..15 para 4 bits). Cache pela soma (hash) das cores.
  4. **Descritores:** hoje cada imagem tem um descriptor set com uma ligação (binding 0). Acrescente ao `setLayout` (em `Renderer::init`) as ligações 1 (mapa, `usampler2D`) e 2 (paleta, `sampler2D`). Crie uma função que devolve um descriptor set para o trio (alvo, mapa, paleta), com cache por esse trio; nos sets que já existem, ligue o `dummy` em 1 e 2. Ao liberar uma imagem (`freeImage`), apague do cache os sets que a usam.
  5. **Shader** (`gs_hw.frag`): declare `layout(set=0,binding=1) uniform usampler2D mapTex;` e `layout(set=0,binding=2) uniform sampler2D palTex;`. Novo bit em `flags`: `1024` = leitura mapeada. Em `fetchTexel`, quando o bit está ligado: leia `uvec2 m = texelFetch(mapTex, c, 0).rg` (com `c` já ajustado pelo `wrapCoord` e limitado ao tamanho), extraia `x = m.r & 0xFFF`, `byte = (m.r >> 12) & 3`, `nib = (m.r >> 14) & 1`, `y = m.g`; leia o pixel do alvo em `(x, y)` na escala (`ivec2((vec2(x,y)+0.5)*pc.scale)`, limitado ao tamanho), converta para bytes (r,g,b × 255, a × 128, arredondando), pegue o byte (`0`=r, `1`=g, `2`=b, `3`=a), para 4 bits pegue o nibble, e devolva `texelFetch(palTex, ivec2(indice + base_da_paleta, 0), 0) * 255.0`. Para texturas de 32 bits lidas com outra largura (a cópia de Z de largura 5 lida com largura 4), a mesma tabela serve: devolva o pixel inteiro sem tirar byte.
  6. **Ligar:** em `resolveTextureSlow`, no ramo `if (!layoutOk)`, quando o alvo é de cor (`!s->depth`) e a textura é de paleta (T8, T4, T8H, T4HL, T4HH) ou de 32 bits com outra largura, use o caminho mapeado em vez de `download`.
  7. R2, R3, R4 (hashes do atual iguais). R9: o PSNR do `raiden` e da `fase4` não pode cair (hoje ~23,5 e ~25). R6 na fase: `texread` e `decode` perto de 0. R5 com `PS2X_GS_HW=1`: anote o vsync na fase.
- **Pronto quando:** `texread` ≈ 0 e `decode` ≈ 0 na fase, PSNR igual ou melhor, e a fase mais rápida que os ~20/s de hoje (anote o número aqui).
- **Se der errado:** imagem com blocos trocados = convenção de endereço errada (volte ao passo 1). Placa trava (`Xid` no `journalctl`) = busca fora da imagem no shader (limite com `clamp`).
- **Commit:** "Renderizador novo: alvos lidos como textura de 8/4 bits na placa (sem trazer o alvo de volta)". Atualize o `ARQUITETURA.md` (parágrafo do renderizador comum).
- **Resultado (13/11):** feito como descrito, com três diferenças do roteiro: (1) o mapa e a paleta ficam no descriptor set 1 (não nas ligações 1 e 2 do set 0), com um set padrão de imagens vazias; (2) o byte 3 de uma palavra Z24/CT24 não está na superfície de 24 bits: texel nesse byte conta como fora, e leituras do byte alto não usam superfícies de 24 bits (sem isso a `fase4` caía de 25,7 para 22,3 dB); (3) as texturas são nominalmente 1024×1024 e cobrem mais que o alvo, então a conferência é por desenho, só na região amostrada (soma prefixada 2D dos texels fora). Também entrou o retângulo sujo por superfície (o espelho vale fora do que a placa desenhou) e a transferência sobre alvo refaz só os blocos escritos. Cópias da placa para o espelho na fase, a cada 2 s: antes ~100 `texread` + ~50 `transfer`/`other`; depois ~45 `texread` (a cópia de Z de `0x2580`, 1024×1024 de largura 10, amostrada fora da superfície Z24 usada como cor: recusa da região) e 0 no resto. R3 (46 passam, 33 falhas antigas), R4 (hashes iguais), R9 iguais (menu 26,8/26,3, raiden 23,3/23,7, fase4 25,7/24,8). A fase não andou mais rápido: o GS ficava ~17% ocupado; o limite era a espera do `CSR` (T07).

### T06 Paleta aplicada no shader para todas as texturas de paleta
- **Por quê:** hoje cada combinação textura+paleta é decodificada em RGBA no processador. O jogo troca a paleta de uma mesma textura muitas vezes, e cada troca decodifica tudo de novo.
- **Faça:** use a imagem de paleta e a ligação 2 da T05. Para texturas de paleta vindas do espelho, decodifique **só os índices** numa imagem `VK_FORMAT_R8_UINT` (cache pelo conteúdo dos blocos, como já é feito em `resolveTextureSlow`, mas sem a paleta na chave) e aplique a paleta no shader (novo bit de `flags`, `2048`). R2, R3, R4, R9 (PSNR igual), R5 com `PS2X_GS_HW=1` (menus continuam a 60/s).
- **Pronto quando:** PSNR igual, menus a 60/s, e menos decodificações (conte com um contador `DIAG` temporário antes e depois).
- **Commit:** "Renderizador novo: texturas de paleta decodificadas só como índices; paleta no shader".
- **Resultado (14/11):** feito (índices no canal R da imagem RGBA8, paleta no set 1, flag 2048) e desfeito: PSNR idêntico, mas as decodificações ao vivo não mudaram (95 na entrada da fase, depois 1–5 a cada 2 s, com e sem a paleta embutida). O cache por conteúdo já cobre o caso, porque nesta cena cada textura usa sempre a mesma paleta. Vale refazer só se outra fase mostrar `decode` alto (contador `DIAG` antes da decodificação em `resolveTextureSlow`).

### T07 Medir a fase e decidir o próximo corte
- **Faça:** R5 com `PS2X_GS_HW=1`, R6 e R7 (`PS2X_PROFILE_THREAD=GSFrontend` e depois `GameThread`). Anote aqui: vsync na fase, números da linha `copies`, as 5 primeiras linhas de cada perfil.
- **Decida:**
  - vsync ≥ 58 em toda a fase → siga para T08.
  - Ainda há cópias (`copies` com números > 5 a cada 2 s) → descubra o motivo pela palavra da linha (`texread`, `clut`, `evict`...), procure no `gs_hw_backend.cpp` a linha que usa esse motivo (`Why...`) e escreva aqui uma tarefa nova para eliminar aquele caso.
  - Sem cópias, mas a `GSFrontend` passa de 80% no perfil → o custo é por primitivo: a próxima tarefa é juntar lotes com array de texturas (descritores indexados); 98% das quebras de lote são troca de textura (medido no F1, apêndice).
  - Sem cópias, `GSFrontend` folgada, mas a `GameThread` passa > 30% em `waitForHead` ou `ps2_vu1_thread::sync` → o limite é a VU1 (veja "Trilha A" no apêndice).
- **Pronto quando:** os números estão anotados e a próxima tarefa está escrita.
- **Resultado (13/11):**
  - Fase, renderizador novo, antes: vsync 12–19/s; GS ocupado ~17%; jogo esperando o GS 0%; cópias só `transfer`/`other` ~26 cada a cada 2 s.
  - CPU por thread (10 s em `/proc/<pid>/task/*/stat`): GSFrontend 45%, GameThread 23%, VU1Thread 21%, GSWorker 17%. **Ninguém saturado:** as threads se revezavam.
  - Perfil da GameThread (R7): 35% das amostras dentro do `waitForHead` do anel do GS. Um contador temporário no `gsAsyncDrain` mostrou um só chamador, `PS2Memory::read64` (leitura do `CSR`), ~64 mil vezes/s, somando ~1,5 s de espera a cada 2 s. É o regulador de quadro girando no bit FIELD (`0x274790–0x2747a4`). Cada leitura esperava o GS esvaziar a fila, e o jogo só começava o quadro seguinte depois do GS terminar o anterior.
  - Correção (`PS2X_GS_CSR_LAG`, ligada; `=0` volta): a leitura do `CSR` espera só o que foi enviado até o vblank anterior. Renderizador atual na fase: **35–60 vsync/s** (antes 15–20), com as quatro threads a 75–80%. Renderizador novo: 17–30/s. Cada leitura do `CSR` lá espera ~54 ms, porque o frontend novo está saturado: 52% de CPU, ~670 ms a cada 2 s em `submit(true)` e ~220 ms em `syncInner`.
  - **Armadilha da T05 achada aqui:** ler da placa só os blocos tocados por uma transferência parcial (`downloadBlocks`) virou ~130 esperas de GPU por segundo (texturas T4 16×16, meio bloco). Voltou para o atalho (o resto do bloco vem do espelho; `PS2X_GS_HW_EXACT_TRANSFER=1` lê da placa), PSNR igual.
  - Um perfil anterior apontou 54% em `sub_004869C0`, o agendador de alarmes do Timer 2 que percorre uma lista ligada. Um contador mostrou que ela quase não roda: foi um perfil fora do comum, não repetiu. Confira com dois perfis antes de concluir.
  - Robustez: com a correção, uma rodada (de 11) do renderizador novo travou na entrada da fase (tela preta, EE sem thread pronta, vsync seguindo a 60/s). Oito rodadas seguidas depois (4 de cada renderizador) passaram (atual 57–60/s, novo 22–23/s no fim do trecho). Não se sabe se o travamento é anterior. Se voltar a acontecer, rode com `PS2X_GS_CSR_LAG=0` para separar (T26–T29).
  - Próxima tarefa: T07a.

### T07a Frontend do renderizador novo saturado na fase
- **Por quê:** com a T07, o renderizador atual faz a fase a 35–60/s, e o novo fica em 17–30/s porque a thread `GSFrontend` não dá conta. Medido a cada 2 s: 52% de CPU, ~280 `submit(true)` somando ~670 ms, e ~1400 `syncInner` somando ~220 ms.
- **Faça:**
  1. Conte os `submit(true)` por chamador (contador `DIAG` com `__builtin_return_address(0)` menos `&__executable_start`, resolvido com `addr2line -Cfie`). Os ~45 `texread` a cada 2 s são parte deles.
  2. `texread` restante: a cópia de Z de `0x2580` (CT32, largura 10, 1024×1024) amostra fora da superfície Z24 usada como cor (recusa 4 no `mappedRead`). Opções: deixar o mapa apontar para mais de uma superfície (uma imagem por região), ou trazer de volta só os blocos que a região amostrada toca **sem** `submit(true)` por desenho (juntar num envio só por quadro).
  3. `syncInner` chama `cpu.Sync` do espelho a cada vez que há transferência pendente: veja se dá para sincronizar só quando o bloco lido foi escrito (`pendingWrite` já marca os blocos).
  4. Perfil da `GSFrontend` (R7 com `PS2X_PROFILE_THREAD=GSFrontend`) para os 52% de CPU.
- **Pronto quando:** fase com `PS2X_GS_HW=1` igual ou melhor que o renderizador atual (35–60/s), PSNR igual.
- **Resultado (14/11):** o frontend ficava ocupado 89% do tempo (1,78 s a cada 2 s). Corrigido:
  - `submit(true)`: as ~280 esperas de GPU a cada 2 s eram as leituras por bloco das transferências parciais da T05 (`downloadBlocks`). Voltaram para o atalho; sobram ~36 esperas e 33 ms a cada 2 s.
  - `refreshPending` (~15% da CPU do frontend) lia o espelho pixel a pixel com o endereço completo e convertia cada canal para half. Agora usa uma tabela de 64 posições (a ordem dos pixels dentro de um bloco de 256 bytes é fixa: teste "keep one order" no `ps2x_tests gs`) e tabelas byte→half. Caiu para ~2%.
  - `syncInner` (~1400 esperas pelo espelho a cada 2 s, ~380 ms): as transferências de host para VRAM agora entram no espelho na própria thread do frontend (`GSCpuBackend::BeginTransferNow`/`UploadImageNow`; `PS2X_GS_HW_INLINE_MIRROR=0` volta), sem a ida e volta da GSWorker.
  - Medido depois: ~750 mil primitivos/s; `draw` ~0,5 µs por primitivo, e a decodificação do GS mais ou menos o mesmo. O atalho de estado acerta 94%; cada falha (`drawState`) custa ~6 µs, e são ~50 mil/s (metade de feedback, um terço de `generation`, que sobe a cada carga de CLUT ou transferência). Para 60/s a thread precisaria de ~1,8 milhão de primitivos/s.
  - Tentado e desfeito: reaproveitar a cópia de feedback por blocos de 32×32 ainda não redesenhados. Nesta cena o bloom lê o que acabou de desenhar, então quase toda leitura pede cópia: sem ganho, e copiando a imagem inteira ficou pior (16–26/s).
  - Fase com `PS2X_GS_HW=1`: 20–31/s (renderizador atual: 57–60/s). PSNR e hashes iguais.

### T07b Renderizador novo numa thread própria
- **Por quê:** depois da T07a, o frontend do renderizador novo gasta metade do tempo decodificando pacotes do GS (`processGIFPacket`, `writeRegisterPacked`, `vertexKick`, `buildDrawBatch`) e metade desenhando (`GSHwBackend::Submit`). O renderizador atual separa as duas coisas: o frontend decodifica e enfileira, e a `GSWorker` desenha. Com o novo na mesma thread, o teto é ~750 mil primitivos/s (~25 quadros/s nesta cena).
- **Faça:**
  1. Dê ao `GSHwBackend` uma fila de comandos igual à do `GSCpuBackend` (`Enqueue`, `WaitForQueue`, um laço numa thread `GSHwWorker`): `Submit`, `LoadClut`, `BeginTransfer`, `UploadImage` entram na fila, em ordem.
  2. Tudo que lê estado de volta (`ReadVram`, `Sync`, `ConsumeLocalToHostBytes`, `SnapshotVram`, `Present` sem alvo casado) espera a fila antes.
  3. `Present` com alvo casado já espera só a cópia de apresentação: confira que a janela não espera a fila.
  4. Cuidado com o espelho: as transferências inline da T07a passam a rodar na thread nova (continuam fora da GSWorker).
  5. R3, R4, R9 (PSNR igual), R5 com `PS2X_GS_HW=1` na fase.
- **Pronto quando:** fase com `PS2X_GS_HW=1` ≥ 45/s, PSNR igual, menus a 60/s.
- **Resultado (14/11), bloqueada depois de três tentativas:**
  1. **Thread `GSHwWorker`** (feita como descrito; o `Present` é um marcador na fila, a thread grava a cópia na ordem do fluxo e a janela mostra a última publicada, com anel de 3 cópias). Correta: PSNR e hash de VRAM iguais com e sem a thread. Mas mais lenta, 16–28/s contra 20–31/s, e a janela caiu para 32–52 fps. Medido dentro dela: ~460 ms a cada 2 s esperando a GPU liberar um bloco de comandos (`slotwait`), e quem produz fica ~2 s somados parado com a fila cheia. A GPU vira o limite assim que a CPU se divide.
  2. **Fixar a thread** no núcleo da `GSWorker` (ociosa no modo novo): sem mudança.
  3. **Menos trabalho na GPU:** as ~225 cópias de feedback por quadro (cada uma fecha o passo, copia e põe uma barreira total) eram o grosso. O bloom da fase são sprites que leem um bloco de 32×32 e desenham nele mesmo, em blocos disjuntos. Agora a sombra guarda a imagem inteira e a lista dos retângulos exatos desenhados desde a cópia; uma leitura que não cruza nenhum reaproveita a cópia e fica no lote. Cópias de feedback: 225 → 55 por quadro; replays do novo 10–15% mais rápidos; PSNR idêntico. Ao vivo, uma thread: 20–32/s; com a thread: ainda pior.
  - A thread ficou no código, **desligada por padrão** (`PS2X_GS_HW_THREAD=1` liga). Vale testar de novo quando a GPU tiver folga (menos barreiras totais e menos passos reabertos).
  - Próximo passo possível: trocar o `fullBarrier` (ALL_COMMANDS→ALL_COMMANDS) por barreiras estreitas, e juntar os passos de render reabertos depois de cada cópia. Fica para a T10/T11, porque a escala 2x/3x aumenta o custo de GPU de qualquer jeito.

### T08 Fidelidade do shader
- **Faça**, um item por vez, cada um com um replay que o exercite e o PSNR (R9) antes e depois:
  1. **DATE** (teste do alfa de destino, bit 14 do `TEST`): hoje ignorado. Implementar com stencil ou lendo o alfa de destino.
  2. **PABE** (só mistura quando o alfa do pixel tem o bit 7): hoje ignorado.
  3. **COLCLAMP desligado** (a cor dá a volta em vez de saturar): hoje sempre satura.
  4. **Blends com termo (1+F)** (procure `shortcut: (1 + F)` em `mapBlend`): hoje aproximados.
  5. **Mipmap por pixel** no shader (hoje um nível por primitivo, no `gs_frontend.cpp`).
- **Pronto quando:** cada item feito tem a anotação do PSNR antes/depois aqui.
- **Resultado (14/11):** contador temporário no `drawState` em todas as gravações (estados de desenho): PABE 0, COLCLAMP desligado 0, blends (1+F) 0, dithering 0; DATE em 38 estados na `fase`/`fase3` e 2 no `raiden`.
  1. **DATE feito:** o desenho amostra a sombra do alvo (a mesma da leitura de feedback: cópia inteira mais os retângulos desenhados desde ela) no próprio pixel (`gl_FragCoord`) e descarta se o bit 7 do alfa não for igual ao DATM. Set 1 ganhou a ligação 2 (`dstTex`); flags 8192 (DATE) e 16384 (DATM). Junto com leitura mapeada (raro), continua ignorado. `PS2X_GS_HW_DATE=0` desliga. PSNR: `fase` 27,4/27,5 → 27,7/27,8; `raiden` 23,3/23,7 → 23,6/24,1; o resto igual.
  2–4. Sem uso nestas cenas: nada a corrigir agora. Se outra fase usar, o mesmo contador mostra.
  5. **Mipmap por pixel:** 20–50% dos primitivos com textura nas fases usam mipmap (`fase` 44 613 de 69 060; `fase3` 151 598 de 473 580). Virou a T10b, porque só aparece para o usuário quando o renderizador novo virar padrão (T11).

### T10b Mipmap por pixel no renderizador novo
- **Por quê:** o frontend escolhe um nível de mipmap por primitivo (`gs_frontend.cpp`, perto de `mipLevel`). Triângulos grandes que vão de perto a longe ficam com um nível só: tremem ao longe ou borram de perto (resto da tremida da T01).
- **Faça:**
  1. No GS o LOD não usa derivadas de tela: `LOD = (LCM ? K : log2(1/|Q|) << L) + K` (TEX1). O shader calcula por pixel a partir do Q interpolado (`vStq.z`).
  2. O backend recebe hoje só o nível escolhido. Para não mexer em `gs_types.h` (recompila 13 mil arquivos), dê ao frontend uma consulta em `gs_hw_backend.h` (header só do runtime; o `gs_frontend.cpp` já o inclui) que diga "o backend faz mipmap por pixel"; aí o frontend não troca o `TEX0` pelo nível.
  3. No backend, para estados com `MXL > 0` e `MMIN` de 2 a 5: decodifique os níveis (`MIPTBP1`/`MIPTBP2`, largura e altura divididas por 2 a cada nível) num atlas (nível 0 em (0,0), os outros ao lado), passe o deslocamento de cada nível no push constant e escolha/misture os níveis no shader conforme `MMIN`.
  4. R3, R4, R9 (PSNR igual ou melhor nas fases), capturas comparadas com `referencias_original/`.
- **Pronto quando:** PSNR igual ou melhor e menos cintilação ao longe (contagem de pixels que mudam entre quadros parados, como na T01).

### T09 Conferência cena a cena
- **Faça:** grave (R9, `PS2X_GS_RECORD...`) as cenas que faltam: `selecao` (seleção de personagem, ~35 s), `carregando` (~48 s), `hud` (gameplay com a barra de vida, ~160 s), `golpe` (um golpe com sangue/brilho), `pausa` (Start na gameplay), `filme` (sem `MKSM_SKIP_INTRO`, ~5 s). Rode `scripts/compara_gs.sh`.
- **Pronto quando:** todas com PSNR ≥ 30, ou a diferença conferida a olho e explicada aqui (exemplo aceito: os raios suavizados).
- **Resultado (14/11):**
  - Gravadas com o renderizador atual (`/tmp` não guarda; o roteiro está descrito aqui):
    - `filme` (sem `MKSM_SKIP_INTRO`, 5 s) e `carregando` (48 s): nos dois quadros não há desenho (o filme é envio direto à VRAM; a tela de carga fica parada), então dão "igual".
    - `selecao` (35 s), `hud` (160 s), `pausa` (Start em 157 s, gravada em 160 s).
    - `golpe`: quadrado/triângulo/círculo entre 161 e 164 s, gravada em 164 s. O `MKSM_AUTO_HOLD` ganhou `q` (quadrado), `t` (triângulo) e `o` (círculo).
  - **Achado:** os triângulos do renderizador novo tinham +0,5 pixel na posição (para amostrar "no canto do pixel"), mas o atual avalia no centro. Sem o +0,5 (`appendVertices`), o PSNR subiu em todas as cenas:

    | Cena | Antes | Depois |
    |---|---|---|
    | menu | 26,8 | 46,9 |
    | titulo | 27,3 | 51,4 |
    | dificuldade | 26,8 | 47,8 |
    | selecao | 24,9 | 47,4 |
    | fase | 27,7 | 33,1 |
    | fase2 | 24,5 | 29,6/30,8 |
    | fase3 | 25,5 | 28,6 |
    | fase4 | 25,7 | 28,8 |
    | raiden | 23,6 | 29,0/30,3 |
    | hud | 25,2 | 28,0 |
    | golpe | 24,0 | 27,6 |
    | pausa | 17,6 | 23,1 |

    O mesmo nos sprites (sem o `floor`) piorou um pouco e foi desfeito. Somar +0,5 também aos sprites piorou (0,4–0,6 dB).
  - **Pausa (23 dB), conferida a olho:** o erro está todo nas letras das estatísticas, feitas de tiras de triângulos com vértices em x .25/.75 e filtro bilinear: cada renderizador põe as bordas das letras numa fração de pixel diferente. A 1× as duas imagens são iguais a olho.
  - **Fases (28–33 dB):** o resto vem dos raios suavizados (melhoria aceita), do mipmap por primitivo (T10b) e das diferenças de borda dos triângulos.

### T10 Resolução interna 2x e 3x
- **Faça:** R5 com `PS2X_GS_HW=1 PS2X_SCALE=2` e depois `3`, com R8. Confira nas capturas: imagem nítida, brilho/desfoque no lugar, HUD certo, nada deslocado. Anote o vsync na fase.
- **Se algo sair deslocado ou faltando:** procure no `gs_hw_backend.cpp` contas com `scale` (cópias de feedback, leitura de alvo, saída) e confira se cada coordenada do PS2 é multiplicada pela escala.
- **Pronto quando:** capturas nítidas e certas em 2x, e vsync ≥ 55 na fase em 2x.
- **Resultado (14/11), bloqueada na velocidade:**
  - Capturas em 2x e 3x (menus, cutscene do Raiden, fase): geometria bem mais nítida, brilho e raios no lugar, nada deslocado. Uma captura preta em 2x caiu numa transição (o instante muda porque a janela roda abaixo de 60).
  - Fase, primeira medida: 2x 10–18/s, 3x 7–11/s (1x: 20–32/s), e a janela caía para 28–50 fps.
  - Causa achada: cada cópia de feedback/DATE (`refreshShadow`) copiava a imagem inteira, ~9 MB em 2x, ~55 vezes por quadro. Agora, com a sombra inteira, ela copia só os retângulos desenhados desde a última cópia (várias regiões num `vkCmdCopyImage`). Replays do novo 10–20% mais rápidos, PSNR igual.
  - Depois: 2x 15–20/s, janela 59 fps; 1x 19–29/s.
  - Falta o mesmo de antes (T07b): a thread do frontend decodifica e desenha ~750 mil primitivos/s. Em 2x soma-se o custo de GPU. Volte aqui depois que o renderizador novo passar de ~45/s em 1x.

### T11 Renderizador novo como padrão
- **Faça:** em `GSHwBackend::WrapFromEnvironment`, sem a variável `PS2X_GS_HW` usar o modo `Render`; `PS2X_GS_HW=0` volta ao atual. Atualize `ARQUITETURA.md`, `scripts/jogar.sh` (comentário de opções) e a tabela "Onde estamos" do apêndice.
- **Pronto quando:** R3, R4 (as cenas usam o atual direto, os hashes não mudam), R5 sem variável nenhuma a 60/s em toda a fase, e T09 feita. Seção 9: "Jogou com o renderizador novo (padrão agora)? Algum defeito novo?"

### T12 Chiado: medir todos os filmes sozinho
- **Por quê:** o usuário ouviu chiado à direita nos filmes. No MIDWAY, medido, os dois canais têm os mesmos agudos do áudio original (apêndice, item E3); faltam os outros filmes.
- **Faça:** para cada filme (tabela `0x4D3BA0`, índices 0 a 7; o método de trocar a entrada 0 da tabela pelo índice N está no apêndice, item D5), toque o filme com `PS2X_SPU2_DUMP=/tmp/mksm/filme_N.raw`, decodifique o áudio do mesmo `.SFD` com `ffmpeg -i RAIZ/extraido_full/FRONT/MOVIES/<NOME>.SFD -map 0:a:0 -ac 2 -ar 48000 -f s16le ref_N.raw` e compare, por canal, a energia acima de 10 kHz e o erro depois de alinhar (o script está no apêndice, item E3). Anote aqui o filme com diferença entre L e R.
- **Pronto quando:** a tabela "filme → agudos L, agudos R, erro L, erro R" está aqui. Com ou sem chiado achado, siga para a T13 (ela resolve dos dois jeitos).
- **Resultado (14/11):**
  - `MKSM_MOVIE=N` (novo, `game_overrides_mksm.cpp`) troca a entrada 0 da tabela `0x4D3BA0` pela N, então o filme N toca primeiro.
  - **Ordem real da tabela** (não é a dos nomes; confirmada casando o áudio): 0 MIDWAY, 1 BLITZ, 2 GAUNTLET, 3 OPENING, 4 END006, 5 END011, 6 FINAL, 7 ESRB.
  - O jogo toca a trilha ADX 0. OPENING, END006, END011 e FINAL têm mais 4 trilhas ADX e 5 AC3 5.1.
  - Dump com `PS2X_SPU2_DUMP`; referência com `ffmpeg -map 0:a:0`. O caminho emulado estica o tempo (WSOLA) quando a emulação atrasa e o alinhamento por amostra falha. Por isso a comparação é de espectro, sem alinhar: energia acima de 10 e 15 kHz relativa à banda toda, em janelas de 1 s, no trecho do filme.

  | Filme | >10k L/R dump | R−L | >10k L/R ref | R−L | >15k R−L dump | >15k R−L ref |
  |---|---|---|---|---|---|---|
  | MIDWAY | −23,9 / −23,3 | +0,6 | −25,5 / −25,1 | +0,4 | +0,3 | +0,0 |
  | BLITZ | −26,7 / −26,2 | +0,5 | −27,1 / −27,1 | +0,0 | +0,2 | −0,4 |
  | GAUNTLET | −26,1 / −25,0 | +1,1 | −25,1 / −25,7 | −0,5 | **+1,9** | −0,5 |
  | OPENING | −24,3 / −22,8 | +1,4 | −24,0 / −24,0 | +0,0 | **+2,7** | +0,2 |
  | END006 | −25,2 / −24,5 | +0,7 | −22,1 / −22,5 | −0,5 | **+3,6** | −0,4 |
  | END011 | −25,3 / −24,3 | +1,0 | −24,4 / −23,8 | +0,6 | **+5,3** | +0,9 |
  | FINAL | −21,9 / −20,7 | +1,2 | −23,0 / −23,0 | −0,0 | **+2,5** | +0,1 |
  | ESRB | −27,6 / −27,5 | +0,1 | −25,5 / −25,5 | +0,0 | −0,1 | +0,0 |

  O canal direito do caminho emulado tem agudos a mais nos filmes longos, e a referência não: bate com o "chiado à direita" relatado. A causa no caminho emulado (ADX pelo IOP → AutoDMA do SPU2) não foi achada. A T13 contorna nos filmes. A música da gameplay passa pelo mesmo caminho: veja a T14b.

### T13 Áudio dos filmes tocado direto do arquivo
- **Por quê:** o vídeo do filme já é decodificado direto do `.SFD` pelo libav (`MPEG.cpp`). Fazer o mesmo com o áudio tira o caminho emulado (que pode ser a origem do chiado) e permite sincronizar o vídeo pelo relógio do áudio.
- **Faça:**
  1. Em `MPEG.cpp`, ache onde o arquivo do filme é aberto com `avformat_open_input`. Abra também o fluxo de áudio (`av_find_best_stream` com `AVMEDIA_TYPE_AUDIO`), decodifique com o libavcodec e converta para 48 kHz estéreo 16 bits (libswresample; confira se ele já está ligado no `CMakeLists.txt` do `ps2xRuntime` antes de usar).
  2. Toque esse áudio num `AudioStream` próprio do raylib e, enquanto o filme toca, descarte o som do filme que chega pelo caminho emulado (o fluxo AutoDMA do SPU2, em `ps2_spu2_stream.cpp`; acrescente uma chave "filme nativo ativo").
  3. Grave com `PS2X_SPU2_DUMP` e compare com o áudio tirado do `.SFD` pelo ffmpeg (o método está no apêndice, item E3).
- **Pronto quando:** o áudio gravado do filme bate com o do `.SFD` nos dois canais (mesma medida da T12, erro parecido em L e R) e o filme longo (OPENING) termina com som e imagem juntos. Seção 9: "O chiado dos filmes sumiu?"
- **Resultado (14/11):**
  - `MovieAudio` (`MPEG.cpp`) abre o mesmo `.SFD` quando o filme começa (primeiro `sceMpegGetPicture` de um arquivo novo). Pega a **primeira** trilha de áudio, porque o `av_find_best_stream` escolhia a AC3 5.1. Decodifica, converte para 48 kHz estéreo 16 bits (libswresample) e entrega ao mixer por `ps2_movie_audio::setSource`.
  - Enquanto há filme, o mixer (`ps2_spu2_stream.cpp`) toca esse som. O fluxo emulado continua sendo consumido, mas fica fora da mistura.
  - `sceMpegDelete` (fim do filme ou Start) desliga. `PS2X_MOVIE_AUDIO=0` volta ao som emulado.
  - Medido: o dump bate **bit a bit** com a trilha ADX 0 em todos os 8 filmes (100% das amostras; no END006 os últimos 7 s ficaram fora porque a rodada acabou). Agudos acima de 15 kHz R−L: −1,2 a +0,8 dB, como a referência.
  - Sincronia do OPENING: o vídeo do `.SFD` escurece aos 223 s e a captura da janela fica preta aos ~220–224 s, com o som indo até 232 s junto com o filme. Juntos dentro de ~2 s, que é a precisão das capturas.

### T14 Volumes no `mksm.ini` (confirmar o nível do eco)
- **Faça:** acrescente volume geral, de música/fala (fluxo AutoDMA, `ps2_spu2_stream.cpp`) e de efeitos (vozes, `spu2_voices.cpp`), lidos de variáveis `PS2X_VOL_MASTER`, `PS2X_VOL_MUSIC`, `PS2X_VOL_SFX` (0 a 100, padrão 100), documentados em `scripts/jogar.sh`. Grave com `PS2X_SPU2_DUMP` com volume 100 e 50 e confira que a amplitude cai à metade. Seção 9: "O eco (`PS2X_REVERB`, padrão 22) está bom?".
- **Pronto quando:** as três variáveis funcionam (medido no dump).
- **Commit:** "Som: volumes geral, música e efeitos".
- **Resultado (14/11):** no `fillStream`, o fluxo AutoDMA (música, falas e o som nativo dos filmes) é multiplicado por `PS2X_VOL_MUSIC`, as vozes do SPU2 (renderizadas agora num buffer separado) por `PS2X_VOL_SFX`, e os dois por `PS2X_VOL_MASTER` (0 a 100, padrão 100; documentados em `scripts/jogar.sh`, valem no `mksm.ini`). Medido no dump, com 50: MASTER 0,500, MUSIC 0,500, SFX 1,000 no filme (sem efeitos); no menu só com efeitos (música 0), SFX 0,500.

### T14b Chiado à direita fora dos filmes
- **Por quê:** a T12 achou agudos a mais no canal R do caminho emulado (ADX pelo IOP → AutoDMA do SPU2). Os filmes agora tocam do arquivo (T13), mas a música da gameplay e dos menus passa pelo mesmo caminho.
- **Faça:** ache um arquivo ADX de música no disco (`extraido_full/`, extensão `.ADX` ou dentro dos `.AFS`) que toque no menu ou na fase. Decodifique-o com o ffmpeg como referência, grave o dump (`PS2X_SPU2_DUMP`, `PS2X_VOL_SFX=0` para tirar os efeitos) e meça como na T12 (`agudos > 15 kHz`, R−L). Se o R tiver agudos a mais, compare o PCM que o IOP manda (`finishSpuDma`) com a referência antes e depois do WSOLA, para achar onde o canal direito ganha ruído.
- **Pronto quando:** R−L acima de 15 kHz igual ao da referência (±0,5 dB) na música da gameplay.
- **Resultado (14/11), bloqueada depois de três abordagens:**
  - A música fica dentro do `GAMEDATA.WA*`: ~250 ADX estéreo longos, achados pela assinatura `(c)CRI`. Não deu para casar o dump da fase com uma trilha só: a fase parece misturar mais de uma.
  - Sem referência, medi o próprio dump só com música (`PS2X_VOL_SFX=0`), R−L acima de 15 kHz em janelas de 5 s:
    - menus (20–100 s) e fase (155–165 s): ±2 dB, como as referências;
    - **cutscene do Raiden (105–150 s): +10 a +25 dB**.
  - Nesse trecho o R tem um salto a cada 256 amostras, numa fase fixa, de 6 a 12× o salto típico; o L não. Isso é um estalo por bloco de AutoDMA (o KB tem 256 amostras de L e 256 de R), e centenas por segundo soam como chiado.
  - O que já foi descartado:
    1. **Host:** o dado bruto que o IOP entrega (`finishSpuDma` → `onAutoDmaBlock`, núcleo 0; o núcleo 1 fica mudo) já tem os saltos no R. Os blocos chegam inteiros (2 KB, 4 buffers) e o R não é cópia velha do mesmo buffer (hash por buffer).
    2. **JITs:** com `PS2X_IOP_JIT=0` o defeito continua; com `PS2X_VU_JIT=0` some. Mas com `PS2X_VU_JIT_CHECK=1` (cada bloco do JIT comparado com o interpretador) deu zero divergências em 12,5 milhões de blocos, e nessa rodada, mais lenta, o defeito apareceu noutro momento (40 s, +26 dB) e a cutscene ficou limpa.
    3. **Conclusão:** não é conta errada. É corrida de tempo no caminho decodificador ADX do jogo → buffer de AutoDMA, que só estraga o canal direito quando a emulação aperta.
  - **Próximo:** achar quem escreve as metades R do buffer de AutoDMA. Ponto de observação na escrita da RAM do IOP no endereço do buffer (o `madr` do `IopMemory` no AutoDMA), registrando quem escreve e quando, em relação ao início e ao fim do período. Conferir se L e R chegam por transferências SIF separadas do EE e se a do R atrasa ou cai quando a fila está cheia.

### T15 Avançar no jogo o máximo possível
- **Faça:** use `MKSM_AUTO_PAD`/`MKSM_AUTO_HOLD` (roteiro de botões por tempo; `MKSM_PAD_CLOCK=reads` deixa determinístico; formatos em `ps2_pad.cpp`, procure `MKSM_AUTO_HOLD`) e o save em `RAIZ/extraido_full/mc0` para passar do Goro's Lair: andar, atacar, passar pelas portas. A cada fase nova alcançada: R5 (velocidade), R8 (capturas a cada 300 quadros) e compare com o que se espera (sem buracos, sem tela preta, sem câmera lenta). Cada defeito vira uma tarefa nova aqui (formato da seção 6), com uma cena gravada (R9) ou despejo de RAM (`PS2X_DUMP_RAM_AT=<segundos>`). Copie a pasta `mc0` antes de salvar por cima, para não perder o save do usuário.
- **Pronto quando:** você chegou o mais longe que os roteiros permitem, com as fases visitadas e os defeitos anotados aqui. Seção 9: "Jogou mais adiante? Viu algum defeito (fase e momento)?".
- **Resultado (14/11):**
  - Roteiro por tempo depois da cutscene (de 158 a 400 s, ciclos de 12 s: frente 3 s, quadrado/triângulo, direita 1,5 s, frente 3 s, círculo/quadrado, esquerda 1 s, X). O `mc0` do usuário foi copiado antes, e nada foi salvo.
  - Resultado: Liu Kang anda e ataca pela primeira sala do Goro's Lair; aparecem o mapa e o aviso "R2 Pick up" perto dos crânios. Vsync de 57 a 61/s em toda a gameplay (o renderizador padrão, depois da T07). Nenhuma tela preta, travamento ou câmera lenta em ~4 min.
  - Roteiro cego por tempo não passa portas nem acha saídas: avançar exige alguém jogando, ou um roteiro guiado pela imagem (captura, decide, aperta), que fica fora do alcance de um roteiro por tempo.

### T16 Segundo controle
- **Faça:** em `PS2/ps2xRuntime/src/lib/ps2_pad.cpp`, `PSPadBackend::readState(int port, ...)` lê sempre o mesmo controle (`kGamepad`) e o teclado, qualquer que seja a porta. Faça a porta 1 ler o segundo controle do raylib (índice 1: `IsGamepadAvailable(1)`, `GetGamepadAxisMovement(1, ...)`, `IsGamepadButtonDown(1, ...)`) com o mesmo mapeamento, e sem teclado. Os roteiros `MKSM_AUTO_*` continuam só na porta 0. A vibração (`scePadSetActDirect` em `Kernel/Stubs/Pad.cpp`) hoje só trata a porta 0: com dois controles, cada porta vibra o seu (o kernel dá um `/dev/input/event*` por controle).
- **Pronto quando:** compila e, sem segundo controle ligado, a porta 1 devolve "nada apertado" (o jogo continua igual); com o roteiro de teste da porta 0, nada muda. Seção 9: "Testou com dois controles?"
- **Resultado (14/11):** `readState` usa o controle de índice igual à porta (0 ou 1), e o teclado só na porta 0; os roteiros já eram só da porta 0. Vibração: `Rumble g_rumble[2]`. A porta N usa o N-ésimo dispositivo com vibração de `/dev/input/event*`, em ordem numérica (atalho anotado no código: costuma bater com a ordem do raylib). Compila; o trajeto de teste pela porta 0 chegou à fase como antes. Sem segundo controle aqui, a porta 1 fica "nada apertado".

### T17 Raios suavizados nas outras fases
- **Por quê:** `softenLightRay` (`gs_frontend.cpp`) reconhece os raios do Goro's Lair pelo endereço da paleta (`0x2F8B`) e da textura (`0x2728`). Outras fases têm outros endereços.
- **Faça:** quando o usuário apontar barras de luz sólidas numa fase (T15), grave a cena, ache o desenho (blend `0x44`, `TEST 0x7380B`, alfa de vértice ≥ 0x80) e acrescente o endereço, ou troque o reconhecimento por uma assinatura do desenho que não dependa da fase.

### T18 Saves
- **Faça:** com o usuário: autosave no meio de uma fase, sair e carregar; dois perfis; cartão cheio (copie a pasta `RAIZ/extraido_full/mc0` antes, para não perder o save dele).
- **Medido (14/11):** na entrada, o jogo acha a pasta `BASLUS-21087` (5 entradas) e lê 198 876 bytes sem erro. Em ~4 min na primeira sala (T15) não houve escrita (nenhum comando 6 do cartão): o autosave deve ser em ponto de controle mais adiante. O resto precisa de alguém jogando (seção 9, T15).

### T19 Filmes de meio e de fim
- **Faça:** confirme que `END006`, `END011` e `FINAL` tocam quando o jogo chama (não só pelo teste da tabela, apêndice D5). Depende de o usuário chegar lá (T15) ou de um save perto do fim.
- **Estado (14/11):** pelo `MKSM_MOVIE` (T12/T13) os 8 tocam com imagem e com som idêntico ao `.SFD`. Falta vê-los no fluxo real do jogo; Kontent → Movies (T20) talvez toque os já liberados.

### T20 Menu principal inteiro
- **Faça:** com `MKSM_AUTO_PAD` ou com o usuário, abrir cada opção do menu principal e voltar. Nenhuma trava.
- **Resultado (14/11):** roteiro `MKSM_AUTO_HOLD`: X no aviso de autosave (8 s), Start na tela de título (20, 23, 26 s). Depois, para cada opção: baixo, X, triângulo duas vezes (o "Cancel" das telas é o triângulo, não o círculo). Capturas a cada 150 quadros. Abriram e voltaram:
  - Ko-op: seleção com Liu Kang e Kung Lao;
  - Versus Mode;
  - Load Game: checa o cartão e mostra a lista;
  - Options;
  - Kontent: Arenas, Concept Art, Movies, Photos...

  Tudo a 60/s, sem travar. O Single Player é o trajeto de sempre (seleção → dificuldade → fase).

### T21 a T25 (melhorias)
- **T21 Quadro cheio:** com `PS2X_SCALE=2`, mostrar o campo atual inteiro (448 linhas desenhadas) em vez de dobrar linhas.
- **T22 FXAA:** um shader de suavização de bordas na janela (`ps2_runtime.cpp`, ao desenhar a textura do quadro), com chave no `mksm.ini`.
  - **Resultado (14/11):** shader próprio no `DrawTexturePro` da janela. Onde a luminância salta contra os 4 vizinhos (contraste ≥ 0,05 e ≥ 15% do máximo), mistura 4 amostras ao longo da borda (perpendicular ao gradiente), na proporção do contraste; fora das bordas não mexe. `PS2X_FXAA=1` liga (documentado no `jogar.sh`). Compila e roda; nas capturas, as bordas do chapéu e dos braços ficam menos serrilhadas. Desligado por padrão porque pode amaciar o texto do HUD (pergunta na seção 9).
- **T23 Widescreen:** achar no jogo a matriz de projeção (campo de visão) e alargar na RAM; HUD fica 4:3. Risco: objetos cortados nas laterais.
- **T24 Controle e latência:** mapeamento e zona morta no `mksm.ini`; medir o atraso botão → imagem; o renderizador novo mostra um quadro atrasado (por desenho, para não esperar a placa): oferecer opção sem esse atraso.
- **T25 Configuração:** todas as opções que ficarem no `mksm.ini`, com valor padrão e explicação em `scripts/jogar.sh`.
  - **Resultado (14/11):** o cabeçalho do `scripts/jogar.sh` lista as opções de uso, com padrão e explicação: pular abertura, eco, os três volumes, raios, mistura de campos, FXAA, placa NVIDIA, vibração, som dos filmes, espera do CSR, renderizador novo e escala, e uma nota sobre dois controles. As de diagnóstico (`PS2X_PERF`, `PS2X_PROFILE`, `PS2X_GS_HW_STATS`...) ficam na seção 3 deste plano e no `ARQUITETURA.md`.

### T26 a T29 (robustez)
- **T26 Uma hora:** rodar uma hora (o usuário jogando ou o roteiro em laço) e medir a memória (`ps -o rss= -p $(pgrep -x ps2EntryRunner)` a cada minuto): não pode crescer sem parar.
- **T27 Build do zero:** numa pasta nova, seguir só o `README.md` com `scripts/instalar.sh`, `extract.sh`, `recomp.sh`, `build.sh`; corrigir o que faltar no README. Escrever um guia curto do jogador.
- **T28 Outras máquinas:** rodar com `PS2X_GS_GPU=0` e `PS2X_GPU_DEVICE=Intel`: pode ficar lento, não pode travar.
- **T29 Testes e limpeza:** as 33 falhas antigas da suíte `gs` e o assert da suíte `memory` (consertar ou marcar como esperadas, com o motivo); tirar as variáveis de ambiente que só serviam para depurar.

---

## Onde estávamos antes do teste do usuário (14/11)

| Área | Estado | Tarefas |
|---|---|---|
| Abertura e filmes | Os 8 `.SFD` tocam; som direto do arquivo, idêntico ao original (chiado à direita resolvido nos filmes) | T19 |
| Menus | 60/s; todas as opções do menu principal abrem e voltam | — |
| Goro's Lair | Cutscene do Raiden 57–60/s (era 15–20, T07); gameplay 57–61/s | — |
| Som | 48 kHz; volumes geral/música/efeitos; chiado no canal direito na cutscene do Raiden ainda aberto | T14b |
| Imagem | Raios suavizados; FXAA opcional; falta nitidez (escala só no renderizador novo) | T10, T21 |
| Renderizador novo (`PS2X_GS_HW=1`) | Quase igual ao atual (menus 47–51 dB, fases 28–33); 2x/3x corretos; lento: 20–32/s em 1x, 15–20/s em 2x | T07b, T10, T11 |
| Controle | Dois controles (porta 1 = segundo controle); vibração por porta | T24 |
| Saves | Carregar funciona; autosave no meio da fase não testado | T18 |
| Resto do jogo | Só a primeira sala do Goro's Lair por roteiro | T15, T17 |

## (antigo) Critério final

O usuário decidiu (12/11): não precisa ser 100% fiel ao PS2; se ficar melhor, melhor (cada melhoria com um interruptor para voltar ao fiel). Está pronto quando:
1. joga do começo ao fim sem travar (abertura, todas as fases e chefes, filmes, créditos, extras, 2 jogadores);
2. 60 quadros/s estáveis em toda cena na máquina do usuário (i7-7700HQ + GTX 1050);
3. imagem igual ou melhor que a do PS2 (sem defeitos, nítida em 2x/3x, sem tremer);
4. som limpo e filmes sincronizados;
5. controle, saves e janela sem atrito;
6. uma hora de jogo sem travar nem vazar memória, e build do zero só com o README.

## (antigo) Perguntas ao usuário de 13–14/11

Perguntas que só o usuário pode responder. **Não pare o trabalho por elas.** Acrescente aqui ao longo da sessão e mande a lista inteira, de uma vez, no resumo final. Quando o usuário responder, anote a resposta e apague a pergunta.

- (T13) O chiado dos filmes sumiu? (agora o som dos filmes sai direto do arquivo; `PS2X_MOVIE_AUDIO=0` volta ao antigo para comparar)
- (T14b) Na cutscene do Raiden (logo depois da seleção), ainda ouve chiado do lado direito? Foi medido lá e não está corrigido.
- (T14) O eco (`PS2X_REVERB`, padrão 22) e os volumes novos (`PS2X_VOL_MASTER`, `PS2X_VOL_MUSIC`, `PS2X_VOL_SFX` no `mksm.ini`) estão bons?
- (T15) Jogou mais adiante? Viu algum defeito (fase e momento)?
- (T16) Testou com dois controles?
- (T22) Com `PS2X_FXAA=1` no `mksm.ini`, a imagem fica melhor? Se sim, ele vira o padrão.
- (T01) A gravação quadro a quadro não achou quadro errado nem pisca na seleção. A tremida é a tela inteira dando um pulo (em que momento exato?), ou texturas finas cintilando quando a câmera ou o personagem se mexem? Se puder, grave um vídeo curto do celular.

---

---

# Apêndice: histórico até 13/11

O texto abaixo é a evidência de cada trilha até 13/11 (o que foi medido, descartado e feito). Os itens ainda abertos viraram tarefas na seção 5.

### Onde estamos (11/11)

| Item | Estado |
|---|---|
| Gameplay (Goro's Lair) | **60 quadros/s**, o teto do vsync do PS2 |
| Vazão real da gameplay (`PS2X_UNCAPPED=1`) | **~60–65**: sem folga. GS ~90% ocupado, GPU (GTX 1050) 81–84% |
| Vazão real dos menus | 200–250 |
| Kung Lao como parceiro | invisível na cena de entrada (só o chapéu aparece) |
| Raios de luz do teto e coluna do pilar | barras sólidas cinza-claras em vez de feixes translúcidos |
| Filmes de abertura (CRI/Sofdec) | **abertura inteira**: MIDWAY, autosave, título, OPENING como demonstração; Start pula |

### Trilha A: desempenho (folga e, no futuro, 90 quadros/s)

**Estado (12/11):** a gameplay roda nos 60 quadros/s do vsync do PS2 o tempo todo; nada abaixo aumenta o fps visível, só dá folga ou prepara os 90. Sem trava (`PS2X_UNCAPPED=1`) a vazão é ~60–62 e **nenhuma thread chega a 100%** (GSFrontend 83% com espera ativa, VU1Thread 78%, GSWorker 64%, GameThread 57%): o teto vem das esperas entre elas, não do custo de uma.

**Ordem do que falta:**
1. ✅ **A9a – passo de tempo do jogo (12/11): não existe um global.** 1/60 (`0x3C888889`) aparece em ~20 pontos do código, sempre montado com `lui`/`ori` e passado em `$f12` para funções diferentes (`0x394F50`, `0x102D28`, `0x102940`, `0x1BB218`...; nenhuma dominante), e 3 cópias nos dados (`0x50A5D8`, `0x50A5F0`, `0x58E1B8`) sem referência direta; 1/30 não aparece. Com a lógica medida andando um passo por vsync, o MKSM é travado por quadro: **90 fps de verdade só com interpolação de quadros**, inviável sem conhecer a engine. A trilha fica como folga, não como 90.
2. **Sobreposição entre threads** (o único ganho real de vazão): reduzir as esperas da GameThread na leitura do `VIF1_STAT`, da fila do GS e dos slots da GPU; executar o lote seguinte do EE enquanto a VU1 termina o anterior quando o jogo não lê nada da VU1 no meio.
   - 🔶 Medido (12/11, gameplay sem trava): a GameThread espera a VU1Thread **45% do tempo** (`ps2_vu1_thread::sync`, ~175 vezes/s, ~2,6 ms cada, quase tudo na leitura do `VIF1_STAT`). A espera é exigida pelo determinismo (o valor só existe depois que a VU1 termina o que recebeu), então o ganho vem de **VU1Thread mais rápida**. Perfil dela sem trava: código do JIT 24%, `runJitBlock` 22% (modelo de ciclos), `libc` 9,6%, `processVIF1Data` 8,3%, `run`+`execLower` ~12% (pares interpretados), compilação/tabelas ~5,5%. A arena de código do JIT não enche (<1 MB) e as tabelas por código nunca são descartadas.
   - Pares da VU1 ainda interpretados (histograma por PC, 12/11): (a) o fim de cada programa, `XGKICK` + NOP com E-bit + slot de atraso (`0x1280`–`0x1290`, `0xEC0`–`0xED0`), três pares por execução; (b) contas FMAC/`ILW`/`ISW` dentro da janela de 16 pares antes de um leitor de flags (`0x6C0`–`0x6D0`, `0xE28`, `0x798`/`0x7A0`). Próximo ganho: o JIT calcular MAC/STATUS nessas janelas e compilar o final com XGKICK (o E-bit fica fora). Estimativa ~6% da VU1Thread; não muda o fps visível (60).
   - Decisão (12/11): o par com `XGKICK` do fim é compilável, mas o bloco recusa a entrada enquanto o `XGKICK` anterior ainda transfere (regra de correção do PATH1). Calcular flags no JIT é uma mudança grande no gerador x86, com risco para a VU1, por ≤6% de uma thread e nenhum fps visível: adiado até haver uma meta acima de 60.
3. **A4 restante:** apresentar direto da VRAM da GPU (sem cópia para o host) e a espera de fim de campo; ~1 ms de GPU.
4. **A5 restante:** registro no formato final já no frontend (copiar uma vez), menos trabalho de CLUT por primitivo; custo espalhado, só com reestruturação.
5. **A6/A7** (VU1, IOP): só quando a medida mostrar que viraram o limite.
6. **A8** (som nativo, IOP em HLE): semanas, risco de quebrar o som, não dá fps.

Histórico da regra antiga: o limite parecia ser o desenho (worker do GS ~90%, GPU 81–84%); medido depois, a GPU caiu para ~70% e o worker sozinho não explica o teto.

1. ✅ **Linha de base com vazão (11/11):** gameplay ~60–63 quadros/s com `PS2X_UNCAPPED=1`, GS ~90% ocupado, GPU 81–84%. Refazer a cada passo: Três rodadas de `PS2X_UNCAPPED=1` na gameplay, anotando média de quadros/s, GS ocupado, GPU ocupada e CPU por thread (`threads.sh`). Repetir a cada passo abaixo.
2. ✅ **Onde vai o tempo da GPU (11/11, replay `fase3`, `--repeat 3`, pulando um tipo de envio por vez com uma variável temporária):** campo inteiro **16,0 ms** (~62/s: é o teto ao vivo). Sem cada tipo: triângulos pelo rasterizador **−3,5 ms**, sprites −2,3, decodificação de textura −1,7, tiles (compute) −1,0, uploads −1,1, cópia do campo para a janela −1,2. Para 90/s o campo precisa caber em ~11 ms: triângulos e sprites são onde está a maior parte.
3. ❌ **GPU: variantes do fragment shader (11/11, medido, não feito):** tirar do fragment shader o caminho de sprites (uma variante "só triângulos") deu 15,0 contra 15,2 ms por campo na `fase3`, dentro do ruído (±0,4). Tirar o interlock (imagem errada, só para medir) deu 14,4 ms: ele custa ~0,6 ms. O custo dos triângulos é o trabalho por pixel (amostra de textura, blend, ler e escrever a VRAM no buffer), não o tamanho do shader. O ganho grande seria usar a cor e o blend fixos da GPU (render target) quando o estado do GS permite (blend comum, sem DATE, sem leitura do próprio alvo), uma reescrita do caminho de desenho: só vale com a meta de 90.
4. 🔶 **GPU: menos trabalho por campo.** ✅ (12/11) A cópia do campo para a janela copiava a VRAM inteira (4 MB) na GPU e de novo na CPU (`TakeCapture`); agora só as páginas 0–139 (os dois quadros do MKSM, `0x00` e `0x46`, 1,1 MB): `fase3` ~15,2 → ~14,0 ms por campo, seis hashes iguais, imagem certa ao vivo em todas as telas. Ao vivo sem trava a vazão não mudou (60,4): o limite ao vivo não é só a GPU. Falta: apresentar direto da VRAM da GPU e a espera de fim de campo.
5. 🔶 **Worker do GS: CPU por primitivo (12/11, medido).** Ao vivo sem trava o worker é o limite (GS 96–98% ocupado, GPU 70%). Perfil do `GSWorker` (o profiler agora escolhe, entre threads de mesmo nome, a que mais usou CPU; o `Present` tem um `GSWorker` ocioso): `QueuePrimitive` 24,7%, `PrepareGpuPrimitive` 20,4%, driver 10,6%, CLUT (`SetClut`, `LoadClutUnlocked`, `QueueClutLoad`) ~15%. Com `rdtsc` por trecho no replay `fase3`: ~405 ciclos por primitivo no `QueuePrimitive`, espalhados (25–78 por trecho: corte de lote, cópia, chave de textura/CLUT, notePending, tiles). Sem ponto quente isolado; o ganho pede escrever o registro no formato final já no frontend (copiar uma vez) e cortar o trabalho de CLUT por primitivo.
   - ✅ CLUT recarregada igual (12/11): 43% das cargas pela CPU na `fase3` deixam a paleta como estava (~220 por campo); agora não publicam versão nova (sem `SetClut`, hash de CLUT nem chave de textura nova). Seis hashes iguais, `gs` 44/33; vazão ao vivo 60,4 → 61,7 (dentro do ruído), worker ainda 98%.
   - ❌ Preparar o primitivo na thread do frontend (12/11): exato (seis hashes iguais), mas vazão 59,4 (sem ganho); desfeito. CPU por thread na gameplay sem trava: GSFrontend 83% (boa parte espera ativa), VU1Thread 78%, GSWorker 64%, GameThread 57%: nenhuma em 100%. O teto ~60–62 vem das esperas entre as threads (VIF1_STAT, anel do GS, slots da GPU), não de uma thread; o próximo ganho é sobreposição, não corte de CPU isolado.
6. ⬜ **VU1Thread**, quando o passo 1 mostrar que ela virou o limite. Já descartado com medida: recompilação estática (o custo é o modelo de ciclos, não o despacho) e tirar leituras repetidas. Candidatos: `processVIF1Data` (UNPACK em SIMD por formato), o modelo de ciclos só nos limites de bloco quando o bloco não tem FDIV/EFU/XGKICK.
7. ⬜ **Thread do jogo**, quando for o limite: JIT da IOP conferindo blocos por geração de escrita das páginas em vez de `memcmp` (exige marcar toda escrita na RAM da IOP, inclusive DMA e `writeGuest` da EE); MULT/DIV e entrada com load pendente no JIT da IOP.
8. ⬜ **Fase 6 (longo prazo, não dá fps):** som do CRI nativo e IOP em HLE. Só depois do resto, com teste comparando com o emulado e cuidado com as armadilhas de `notas/audio.md`.
9. ⬜ **90 quadros/s de verdade.** Soltar o vsync só acelera o jogo, porque a lógica anda um passo por vsync. Caminhos, do mais barato ao mais caro:
   - Procurar no executável uma variável de passo de tempo ou velocidade do jogo (engine da Paradox; procurar floats de 1/60 e 1/30 usados na física e na animação). Se existir, vblank a 90 Hz com o passo em 2/3.
   - Se não existir: interpolação de imagem na apresentação (gera quadros intermediários; tem artefatos e latência).
   - Pré-requisito em qualquer caso: vazão de ~95 no passo 1.

### Trilha B: Kung Lao parceiro invisível

**O que já se sabe (PLANO 5A.25):**
- Escolhendo o Liu Kang, o Kung Lao parceiro aparece só com o chapéu. Escolhendo o Kung Lao, o parceiro Liu Kang aparece inteiro.
- O corpo nunca chega ao GS: replay GPU e software dão o mesmo hash e a mesma imagem sem ele.
- Os dados dele são lidos: com o Liu Kang escolhido o CD tem só leituras a mais (222 setores em LBN 1594609 e alguns blocos pequenos).
- Descartados com medida: JIT da VU, VU1 em thread, leituras de CD com erro, instruções VU inválidas, instruções EE sem tradução, comparações `C.cond.S` da EE e `VMAX`/`VMINI` da VU0.

**Passos:**
1. ✅ **Leituras de CD (11/11):** as leituras a mais do caso Liu Kang vêm no fim, com destino em áreas de buffer (`0x1DEE900`, `0xA35800`...), com cara de som e vozes. O modelo é carregado igual nos dois casos.
2. ✅ **Texturas (11/11, replay de 3 quadros aos 69 s com hash do conteúdo de cada textura desenhada):** 8 texturas T8 só aparecem com o Kung Lao jogador (o corpo dele). No caso Liu Kang, uma delas está na VRAM no mesmo endereço (`0x35D0`) e nenhum triângulo a usa: a textura chega, a geometria não.
3. ✅ **Vértices (11/11, contador temporário no `vertexKick`, 66–72 s):** com o Kung Lao parceiro somem ~1,1 M de vértices desenhados (11,3 M contra 12,4 M) e os pulados por ADC quase não mudam (6,25 M contra 6,46 M). O corpo não é gerado: o corte é antes do XGKICK (EE ou microprograma da VU1), não no GS nem no ADC. Suspeita: o corpo usa esqueleto e o chapéu não; matrizes de osso erradas (animação do parceiro) fariam a VU1 descartar o objeto no teste de recorte.
- ✅ **VIF1 (11/11, contador temporário em `processVIF1Data`, 66–72 s):** com o Kung Lao parceiro a EE manda **mais** dados ao VIF1 (733 MB contra 664 MB). O corpo é enviado; quem não gera os vértices é a VU1. B4 (corte na EE) fica improvável; seguir pelo B5.
4. ⬜ **Corte na EE:** seguir o caminho de desenho do objeto (lista de desenho, teste de visibilidade/frustum, LOD por distância) até o desvio que pula o corpo. Suspeitos: comparações de float com valores especiais, conversões float↔int (o PS2 trunca), comandos de cena `hideobj`/`bgcharhide`/`killandhidecharacter` e a variável de debug `OtherPlayerInvisible` (`0x5756B0`).
- 🔶 **Execuções da VU1 por entrada (11/11, contador temporário em `execute`/`startXgkick`, 66–72 s):** nos microprogramas de personagem (entradas `0x6F0`, `0x000` e `0xE50`, chaveadas pelo código), as execuções que terminam **sem nenhum XGKICK** são 3–5× mais comuns com o Kung Lao parceiro: 1209 contra 368, 722 contra 196 e 270 contra 53. A memória de dados nessas execuções vazias não tem valores com expoente 255 nem enormes (as execuções que enviam têm mais expoentes 255 no caso Liu Kang, 146 contra 93 por execução, que podem ser máscaras legítimas).
- ⚠️ **O defeito é intermitente (11/11).** Uma rodada com `PS2X_VU_JIT=0` mostrou o Kung Lao inteiro, e a busca binária dos blocos do JIT da VU0 parecia apontar para a combinação dos blocos `0x8` e `0x60`–`0x78`. Mas duas rodadas seguidas com o JIT todo desligado voltaram a mostrar só o chapéu, e uma rodada sem nenhum bloco da VU0 no JIT também. **Não é o JIT**; os resultados da busca binária foram ruído. Lição: neste defeito, uma rodada só não prova nada; cada teste precisa de pelo menos 3 rodadas.
- Achado à parte, verdadeiro mas não é a causa: o conferidor do JIT não compara `mac`/`status`, e na VU0 eles diferem do interpretador em quase todo bloco (o JIT só calcula flags perto de quem lê; é por desenho).
- ✅ **Leituras extras não decidem (11/11, 3 rodadas com hora de cada `sceCdRead`):** nas rodadas 2 e 3 os 222 setores (LBN 1594609) e os blocos pequenos chegam aos 86–92 s, depois da cena com os personagens (~70–83 s), nas duas. Rodada 3 com o Kung Lao inteiro, rodada 2 sem. Frequência medida até agora: ~1 em 3–4 rodadas mostra o corpo.
- ✅ **Frequência depende da sessão (11/11, 4 rodadas por configuração, mesmo binário):** padrão 4 de 4 com o corpo; `PS2X_VU1_THREAD=0` 3 de 4. Horas antes, com o mesmo código de jogo, quase todas as rodadas saíam sem o corpo. É uma corrida de tempo de host (carga da máquina, ordem das threads), não um erro de cálculo fixo.
5. ⬜ **Corrida (próximo):** o que varia entre rodadas é o tempo de host (threads de GS, VU1, IOP, áudio, vsync de tempo real). Testar com `PS2X_VU1_THREAD=0`, `PS2X_GS_ASYNC=0` e `PS2X_UNCAPPED=1`, 4 rodadas cada, contando quantas mostram o corpo; a variável que leva a 0 ou a 4 de 4 aponta a corrida. Dentro do jogo, olhar o que lê o relógio real (contadores de tempo, `Async_DataLoad`) no caminho do parceiro. O corpo do parceiro chega à VU1 mas às vezes não gera vértices; e o caso Liu Kang tem leituras de CD a mais no fim (222 setores em LBN 1594609 e blocos pequenos), que podem ser o modelo do parceiro chegando depois de a cena começar. Passos: registrar o instante de cada leitura (temporário, em `sceCdRead`) e o quadro em que o corpo aparece/some, em 5 rodadas; ver se o corpo aparece só quando essas leituras terminam antes da cena. Se for isso, conferir o tempo das leituras HLE contra o do PS2 (o jogo pode contar com uma ordem entre a carga e o roteiro da cena) e o ponto em que o jogo espera a carga (`Async_DataLoad`, `0x378A30`).
- Pronto quando: o Kung Lao aparece inteiro na cena com o Liu Kang escolhido, sem mudar os seis hashes de replay.

**Passos para fechar (12/11):**
- ✅ **B6 – contagem por configuração (12/11):** `scripts/kl_corpo.py` (pixels da calça do Kung Lao nos planos abertos, sem os closes do Raiden) bate com o olho nas 12 rodadas antigas. script que roda N=4 vezes e conta pela captura aos ~75 s se o corpo aparece (comparar a área do sprite do parceiro com uma referência). Sem isso cada teste custa 4×5 min de olho.
- ✅ **B7 (12/11):** com o roteiro do pad em segundos reais: padrão 4/4, `PS2X_GS_ASYNC=0` 4/4, `PS2X_VU1_THREAD=0` 2/4, as duas 3/4. Com `MKSM_PAD_CLOCK=reads` (toques nos mesmos quadros do jogo): `PS2X_VU1_THREAD=0` 4/4, padrão 4/4, toques deslocados de 1–4 s 4/4. **12 de 12 com entrada determinística:** a variação vinha do horário dos toques de X do harness (thread do jogo mais lenta → X em outro ponto da cena), não de corrida entre threads do runtime. Não reproduzido de forma fixa; se voltar a aparecer, gravar o horário exato dos toques. Texto original: com o B6, medir `PS2X_VU1_THREAD=0`, `PS2X_GS_ASYNC=0`, `PS2X_UNCAPPED=1` e as três juntas. A que leva a 0/4 ou 4/4 aponta a corrida.
- ⬜ **B8 – ordem fixa entre threads (só depuração):** variável que serializa EE → VU1 → GS a cada comando (sem sobreposição). Se o defeito some sempre, a corrida está numa sincronização frouxa entre elas; bisseccionar ligando a sobreposição de uma de cada vez.
- ⬜ **B9 – fontes de tempo real que o jogo lê:** registrar (temporário) toda leitura de contador de tempo pela EE (`COUNT`, timers `0x10000000`–`0x10001800`, `GetTimerCount`/alarmes) e a ordem das respostas SIF/`Async_DataLoad` (`0x378A30`) no caminho do parceiro, em uma rodada boa e uma ruim; a primeira divergência é a corrida.
- ⬜ **B10 – corrigir:** tornar determinístico o valor divergente (ex.: resposta de carga só no vsync seguinte, como no PS2, ou timer derivado do relógio do EE e não do host). Conferir 8/8 rodadas com o corpo e os seis hashes iguais.

### Trilha C: raios de luz sólidos ("losangos")

**O que já se sabe (PLANO 5A.24):**
- A textura do raio (T4 `0x2F5F`, paleta CT32 `0x2F8B`) tem alfa `0x80` em tudo; o alfa dos vértices vem do pacote VIF montado pela EE: `0x30` em parte dos desenhos (certo) e `0x80` nas barras (cor `7F7F7F80`).
- Blend herdado `ALPHA 0x44` (normal), posto por um `DIRECT` do próprio fluxo logo antes do raio; outros desenhos do raio usam `0x48` (aditivo).
- Descartados com medida: mipmap, faixa de Z, ordem com o bloom, MAX/MINI da VU, JIT da VU1, comparações de float da EE (as barras continuam depois da correção de 11/11).
- No jogo real não há barras.

**Pista nova (11/11):** o `TEST` do raio é `0x7380B`: alfa GEQUAL `0x80` com AFAIL=RGB_ONLY **e DATE=1, DATM=0** (teste de alfa de destino: só desenha onde o bit 7 do alfa do pixel de destino é 0). O DATE já é emulado (CPU e GPU). Se no PS2 real os pixels atrás do raio têm esse bit em 1, o raio é rejeitado ali, e é exatamente o que o print real mostra. Então o suspeito vira **quem escreveu o alfa do quadro antes do raio** (cena com `FBA`, `FBMSK`, alfa de textura, a passada que limpa o quadro).

**Passos:**
1. ✅ **Medir o alfa de destino nos pixels das barras (11/11, replay `fase2`, diagnóstico temporário no `WritePixel`):** 0 pixels do raio recusados pelo DATE; o alfa de destino sob o raio é `0x00` em 99,9% (24 546 contra 30 em `0x80`).
2. ✅ **Quem escreveu esse alfa:** a cena (`2c78`, `2590`) escreve alfa `0x80`–`0x83`; logo antes do raio, sprites de 32×32 com `TEST 0x31001` (alfa NEVER, AFAIL=FB_ONLY), `ALPHA 0x6a` (resultado = Cd) e vértice com alfa 0 zeram só o alfa, junto com os brilhos `1180`/`2580` (e a limpeza `3288` no começo do campo). É o próprio fluxo do jogo limpando o alfa antes do raio, então o DATE deixa o raio passar também no protocolo. **Pista do DATE descartada:** nossa emulação segue o fluxo.
3. ➖ Corrigir no GS: nada a corrigir pelo DATE.
4. ⬜ **Se o alfa de destino estiver certo:** voltar à hipótese da EE: achar a função que escreve as cores do raio no pacote (`xref.py` no endereço de origem do UNPACK em `0xE10`) e conferir a conversão float + 2^23 do alfa de fade.
5. ⬜ **Último recurso:** dump de GS do PCSX2 (`.gs`) desta cena, para comparar registrador a registrador. O PCSX2 não está instalado nesta máquina; precisa do usuário.
- Pronto quando: na cena do Raiden e na gameplay os raios saem como feixes translúcidos, sem barras.

**Passos para fechar (12/11):**
- 🔶 **C6 (12/11):** despejo da RAM na gameplay com as barras na tela (140 s): `7F7F7F80` aparece 3643 vezes (é a cor neutra padrão dos vértices, não serve de assinatura) e o alfa `0x30` não aparece em nenhuma forma (bytes, inteiros, float 48). O alfa certo não está pronto na RAM da EE: deve ser montado na VU1 (registrador de material × cor do vértice). Próximo: no replay, achar o XGKICK do raio e o programa/endereço da VU1 que monta o RGBA. Texto original: no replay `fase2`, guardar o endereço de origem (MADR do DMA VIF1) do UNPACK que leva cor `7F7F7F80` e do que leva alfa `0x30`.
- ⬜ **C7 – achar quem escreve:** `PS2X_DUMP_RAM_AT` no mesmo instante e um watchpoint temporário de escrita da EE nesse endereço (log de PC), depois `xref.py` para subir até a função do raio.
- ⬜ **C8 – entender o alfa:** ler a função: o alfa vem de um fade (float → int com o truque `+2^23`, `CVT.W.S`, saturação em 0x80)? Conferir as instruções usadas contra o comportamento do PS2 (truncamento, sem NaN/inf, `MADD`/`MSUB` com acumulador, `CLAMP`). A barra sólida é o fade preso no máximo.
- ⬜ **C9 – se o código parecer certo:** o valor de entrada do fade pode vir de um timer ou de outra entidade (mesma família da corrida do Kung Lao); registrar o valor ao longo dos quadros.
- ⬜ **C10 – comparação com o PCSX2 (precisa do usuário):** dump `.gs` da cena do Raiden pelo PCSX2, para ver o alfa de vértice certo; com ele, C7 vira só procurar onde o nosso diverge.
- ⬜ **C11 – corrigir** no recompilador ou no runtime, conferindo os seis hashes (as gravações antigas mudam só nos raios; regravar uma de referência).

### Trilha D: filmes de abertura

**Estado (12/11, PLANO 5A.26):** ✅ D1–D4 e D6 feitos: o travamento depois do MIDWAY era o `sceMpegDelete` (`0x4688A0`) sem função gerada, alcançado por um `j` no destroy do Sofdec; ligado a um wrapper HLE. A abertura vai até o título e o `OPENING.SFD` toca como demonstração. Antes: `MIDWAY.SFD` tocava inteiro (30 quadros/s, imagem certa, som). O `GetPicture` decodifica o `.SFD` do disco (arquivo achado pela faixa de LBN no `sceCdRead`), anda o DMA toIPU na medida do vídeo de cada quadro, chama o callback "nodata" no fim e devolve -1. Depois disso a tela fica preta: o laço principal roda (dois envios SIF por quadro, `ra 0x41ECF4`), mas o `OPENING.SFD` nunca é lido. `0x1D32E0` roda uma vez só no boot (só dispara a abertura).

**Passos:**
- ✅ **D1 – o que a thread do jogo faz na tela preta:** perfil da `GameThread` (`PS2X_PROFILE` dos 25 s em diante) e histograma de PCs da EE (só código do jogo, sem a IOP) para achar o laço que espera.
- ✅ **D2 – quem cria o player:** `xref.py` nos chamadores de `0x447728`/`0x44C928` e nas referências às strings `MIDWAY`/`OPENING` do FILELIST; achar a lista de filmes da abertura e o índice que deveria avançar.
- ✅ **D3 – a condição de fim:** registrar (temporário) o estado do player Sofdec (struct em `0x105C3C0` e o handle do player) quadro a quadro no fim do MIDWAY: que campo a rotina da abertura testa e que valor ela espera. Candidatos: `sceMpegIsEnd` (não está ligado no HLE), estado de fim da stream de áudio do CRI (IOP/SIF), contagem de quadros contra o cabeçalho do SFD, o campo em `mpegAddr+0x40` que o modo arquivo hoje pula.
- ✅ **D4 – corrigir o fim:** ligar a função ou escrever o campo que falta (no HLE do MPEG ou no fim do toIPU). Pronto quando o `OPENING.SFD` aparece no log do `sceCdRead` e começa a tocar.
- ✅ **D5 – os outros filmes (12/11):** trocando a entrada 0 da tabela `0x4D3BA0` pelo índice N antes do `0x1D32E0` (override temporário), os 8 tocam com imagem certa: MIDWAY, OPENING, BLITZ, GAUNTLET, ESRB, END006, END011, FINAL; o ESRB curto termina e o jogo segue para o aviso de autosave. abertura inteira até o título; conferir também filmes de meio de jogo (cenas pré-renderizadas, `FINAL.SFD`) com o mesmo caminho.
- ✅ **D6 – pular com botão:** Start durante o filme deve encerrar (o jogo testa o pad e pede fim ao player); conferir que o HLE para limpo.
- ⬜ **D7 – sincronia:** imagem pelo relógio de áudio do CRI (hoje pelos 30/s do vsync); conferir que não desliza num filme longo.
- ✅ **D8 – limpeza (12/11):** `MKSM_SKIP_INTRO` agora é só atalho. `MKSM_SKIP_INTRO` vira só atalho; atualizar `ARQUITETURA.md` e o `PLANO.md` 5A.26; seis hashes iguais.

### Som na gameplay (12/11) ✅

Nos menus perfeito, na gameplay ondulado. Medido: o AutoDMA entregava ~50 200 amostras por segundo real (48 000 é o certo), e o WSOLA comprimia 4–10% o tempo todo. Duas causas: (1) o `runCycles` do IOP não descontava os ciclos que passavam do alvo (4% a mais, menus inclusive); (2) depois de um carregamento o vsync recuperava o atraso correndo, e a IOP chegava a 37–38 MHz por minutos. Correções: dívida de ciclos no IOP, vsync reancorado quando passa de 2 campos de atraso e andamento 1,0 exato com a fila numa faixa folgada. Depois: 48 002/s nos menus, 47 858/s na gameplay, andamento 1,0 em 77 de 79 s.

### Trilha E: melhor que o original (12/11)

O usuário decidiu: **não precisa ser 100% fiel; se ficar melhor que o PS2, melhor**. Então, onde a fidelidade custa caro e uma melhoria resolve, vale a melhoria. Cada melhoria tem um interruptor (variável de ambiente) para comparar com o comportamento fiel.

**Estado depois do teste do usuário (12/11):**

| Item | Estado |
|---|---|
| Som na gameplay | corrigido por medida (48 kHz, sem compressão); falta o ouvido do usuário |
| Câmera lenta nas cenas pesadas | pulo de quadro: 55–60/s; sobram 2 quedas de ~4 s a 36/s (VU1) |
| Imagem | filtro bilinear, campos misturados, mipmap; ainda com tracinhos escuros e sem nitidez |
| Raios de luz | placas chapadas (trilha C) |
| Filmes | tocam; chiado no canal direito; sincronia não conferida (D7) |

#### E-A. Corrigir o que está errado (antes de melhorar)

- ✅ **E1. Tracinhos escuros no chão e nas paredes.** Causa: a cor/alfa Gouraud era truncada, então 0x80 nos três vértices dava 127,99 → 0x7F em pixels espalhados. Chão e paredes usam `TEST 0x7380B` (alfa GEQUAL 0x80, AFAIL=RGB_ONLY): esses pixels falhavam, não gravavam Z, e a geometria de trás pintava por cima. Arredondar no `DrawTriangle` (CPU) e no `gs_tiles.comp` (GPU) resolveu; teste novo na suíte `gs`. Os raios usam o mesmo TEST, mas continuam chapados (E2).
- ✅ **E2. Raios de luz (melhoria, não fidelidade).** Os dados do jogo mandam o raio opaco (PLANO 5A.24); o console mostra feixes suaves. `softenLightRay` no `gs_frontend.cpp`: desenho T4 com `TEST 0x7380B`, blend `0x44`, alfa de vértice ≥ 0x80 e a paleta dos raios (`0x2F8B`, texturas `0x2F5F`/`0x2F7F`) ou a coluna do pilar (`0x2728`) vira aditivo (`0x48`) com alfa 0x18. As barras brancas da cena do Raiden e as placas do teto viraram brilho translúcido. `PS2X_RAYS=0` volta ao original. *Limite:* casado por endereço de VRAM do Goro's Lair; outras fases precisam dos seus endereços.
- **E3. Chiado no canal direito dos filmes.** `PS2X_SPU2_DUMP` durante o MIDWAY, separar L e R e ver se o R tem blocos trocados, amostras de outro fluxo ou ordem errada no intercalado do ADX do SFD. *Pronto quando:* L e R limpos no dump. **Medido (12/11):** no MIDWAY, L e R têm a mesma energia de agudos que o áudio decodificado direto do `.SFD` pelo ffmpeg (L −18,5/−18,1 dB, R −19,2/−18,6 dB acima de 10 kHz). O erro maior do R contra a referência é de baixa frequência, vindo do alinhamento entre as gravações, não de chiado. O OPENING não tocou em 110 s de abertura, então não foi medido. **Falta:** o usuário dizer em qual filme e em que momento ouve o chiado.
- ✅ **E4. Reverberação (melhoria).** Medido: o jogo liga o efeito (`ATTR` bit 7, `0xFF81`), manda as vozes 1–23 para ele (`VMIXEL/VMIXER` `0xFFFE/0x00FF`), mas o `sceSdSetEffectAttr` falha e o preset fica zerado com `EVOL` 0, ou seja, sem eco. `spu2_voices.cpp`: reverb Schroeder próprio (4 combs, 2 allpass por canal) nas vozes enviadas ao efeito, só com o `ATTR` ligado, com cauda de 3 s. `PS2X_REVERB=0..100` (padrão 22; 0 desliga). Música e falas (AutoDMA) não passam pelo reverb. Falta o ouvido do usuário para ajustar o nível.
- **E5. Quedas que sobraram na cutscene.** **Medido (12/11):** nas quedas (64–68 s e 78–80 s, ~32 quadros/s), a GameThread passa **57–60% do tempo no `ps2_vu1_thread::sync()`** (fora delas, ~26%) e o **worker do GS fica 99% ocupado**. A cadeia é worker do GS saturado → VU1Thread parada na fila do GS → jogo parado esperando a VU1. A GPU fica em 14–28%. Descartado: aceitar atraso de até 6 campos antes de re-ancorar o vsync (sem efeito, desfeito). **Próximo:** baixar o custo de CPU por primitivo no worker (perfil: `QueuePrimitive` 23%, `PrepareGpuPrimitive` 19%, bibliotecas 17%, `SetClut` + `LoadClutUnlocked` 14%, `ReadCT32` 5%), ou dividir o worker em duas threads (preparar os registros num núcleo, gravar os comandos Vulkan em outro). *Pronto quando:* `PS2X_PERF` sem linha abaixo de 55/s na cutscene. **Mais medidas (12/11):** nem o `FINISH` nem as esperas de GPU seguram: o `Sync` com motivo diferente de apresentação não é chamado na fase, e o `GSGpuAccelerator::Flush` roda ~560 vezes/s com 0–2% de espera. O gargalo é CPU pura no worker. No perfil, o ponto mais quente do `QueuePrimitive` é a cópia do registro de 256 bytes (`rep movsb` para a memória mapeada), seguido das bibliotecas chamadas por `QueueUpload`, `beginIfNeeded`, `flushSprites` e `TakeCapture`. **Referência para medir:** gravar a cena do Raiden (`PS2X_GS_RECORD_AT=80`, 2 quadros) e rodar `ps2x_gs_replay raiden.gsr --repeat 200`: 28,5 ms por quadro com `--sync 1`, 16,9 ms com `--sync 0` (GPU 62%). **Feito (12/11):** cópias para os anéis mapeados na memória da GPU (registros, UV, descritores de sprite) com escritas não-temporais AVX (`streamCopy`) e `_mm_sfence` antes do `vkQueueSubmit`: replay do Raiden 15,9 s → 13,7 s (−14%, A/B lado a lado), mesmos hashes. Descartados: montar o registro direto na memória mapeada (27× mais lento, porque ler essa memória de volta é caríssimo) e copiar as versões de CLUT em bloco (sem ganho). **No jogo as quedas continuam** (~33–36/s em 64–68 s e 78–80 s): falta ~2× de vazão no worker. **Próximo:** dividir o worker em duas threads (uma faz `PrepareGpuPrimitive` e as cargas de CLUT, a outra grava os registros e os comandos Vulkan), já que ~40% do tempo está em cada metade.
- **E6. D7:** sincronia de som e imagem dos filmes até o fim.
- **E7. Cores diferentes do original:** depois de E1 e E2, comparar de novo com os prints; o que sobrar vira item próprio.

#### E-B. Imagem melhor que a do PS2

- **E8. Resolução interna maior (2x, 3x).** O maior ganho de imagem. O jogo desenha campos de 640×224; desenhar a VRAM da GPU em escala N (triângulos pelo rasterizador, que já existe como caminho `raster`; sprites e cópias escalados; leituras do jogo de volta reduzidas). Resolve de uma vez o "pixelado" e o serrilhado. Começar por 2x só nos alvos de cena (`0x00`/`0x46` e Z), deixando o rascunho do bloom em 1x. `PS2X_SCALE=N`. *Pronto quando:* captura da janela em 1280×896 nítida, replays em 1x com os mesmos hashes.
- **E9. Quadro cheio em vez de campo.** Com E8 em altura 2x, cada campo já tem 448 linhas: dá para mostrar o campo atual inteiro, sem misturar com o anterior (sem o leve rastro do `PS2X_DEINTERLACE`). Conferir o deslocamento de meia linha entre campos.
- **E10. Suavização de bordas.** FXAA (um shader na janela, barato) ou MSAA no rasterizador. Fazer depois de E8.
- **E11. Filtro de textura melhor.** Bilinear forçado onde o jogo usa ponto em textura de cena (não em HUD nem nos passes de bloom) e anisotrópico no chão. Interruptor, porque pode borrar o HUD.
- ✅ **E12. Tela cheia e proporção certa.** F11/Alt+Enter, barras laterais mantendo 4:3, escala inteira opcional. Hoje a janela abre em 640×448. **Feito (12/11):** F11 alterna tela cheia sem bordas; a imagem fica em 4:3 centralizada em qualquer formato de janela; a janela abre em 1280×896 quando o monitor tem 1080 linhas.
- **E13. Widescreen 16:9.** Achar a matriz de projeção/o campo de visão do jogo e alargar a visão horizontal (patch na RAM, como os de `MODDING.md`), mantendo o HUD em 4:3. *Risco:* objetos cortados nas laterais pelo culling do jogo.

#### E-C. Jogar melhor que no PS2

- ✅ **E14. Controle.** Vibração (o jogo usa o motor do DualShock), reconexão a quente, mapeamento e zona morta configuráveis, teclado completo. **Vibração feita (12/11):** `scePadSetActDirect` (já ligado ao jogo em `0x206EF0`) manda os motores para o primeiro `/dev/input/event*` com `FF_RUMBLE` (force feedback do kernel; o raylib/GLFW não tem vibração). Procura de novo a cada 3 s se o controle não estiver ligado. `PS2X_RUMBLE=0` desliga. **Não testado:** não havia controle ligado nesta máquina. Falta: mapeamento e zona morta configuráveis.
- ✅ **E15. Arquivo de configuração.** Um `mksm.ini` no lugar das variáveis de ambiente (escala, tela cheia, filtros, zona morta, pular abertura) e um script `scripts/run.sh` que já usa a NVIDIA (`prime-run`) quando existe. **Feito (12/11):** `scripts/jogar.sh` lê `mksm.ini` na raiz (linhas `VAR=valor`, fora do git) e roda o jogo.
- ✅ **E16. Janela usando a placa certa sozinha.** O log mostra a janela na Intel e o GS na NVIDIA sem `prime-run`: pedir a GPU dedicada pelo próprio programa (variáveis `__NV_PRIME_RENDER_OFFLOAD`/`DRI_PRIME` antes do `InitWindow`). **Feito (12/11):** com `/proc/driver/nvidia` presente, o runtime define `__NV_PRIME_RENDER_OFFLOAD=1` e `__GLX_VENDOR_LIBRARY_NAME=nvidia` antes do `InitWindow`; conferido no log (`Renderer: NVIDIA GeForce GTX 1050`) sem `prime-run`. `PS2X_GPU_AUTO=0` desliga.
- **E17. Carregamentos.** Medir o tempo de cada carregamento; o disco é HLE, então o que sobra é espera do jogo por vsync/timers. Encurtar só se passar de poucos segundos.
- **E18. Folga de desempenho (trilha A).** Com E8 a GPU passa a ser o limite na GTX 1050: medir 2x antes de prometer 3x. O perfil do GSWorker (12/11) mostra `QueuePrimitive` 23% e `PrepareGpuPrimitive` 19%: são o alvo se o worker limitar.

### Trilha F: renderizador de GPU "comum" (decidido em 12/11)

**Por quê:** o GS atual na GPU imita o chip pixel a pixel (shader com interlock ordenado), e por isso o processador prepara cada triângulo um por um. Esse é o gargalo das quedas (E5). Um renderizador como o modo hardware do PCSX2 junta milhares de triângulos num desenho comum de GPU com blend fixo e tira esse custo. A resolução interna maior (E8) vem junto: é só o tamanho dos alvos. O renderizador atual fica como reserva e como referência de imagem (`PS2X_GS_HW=0`).

**Medido antes de começar (replay do Raiden, 12/11):** 97% dos desenhos são tiras de triângulos texturizadas, sem ler o próprio alvo:
| Fatia | Blend (A,B,C,D) | Testes | Na GPU |
|---|---|---|---|
| 27% + 3% | `0x44` (Cs−Cd)·As+Cd, normal | alfa GEQUAL 0x80 RGB_ONLY, Z GREATER | blend fixo (As×2) + passada de alfa |
| 22% | `0x89` Cd·As (escurece: sombra/luz) | Z GEQUAL | `dst·SRC_ALPHA` |
| 22% | `0x58` Cs·Ad+Cd (aditivo pelo alfa de destino) | alfa | `src·DST_ALPHA + dst` |
| 21% | `0x64` (Cs−Cd)·FIX+Cd | alfa | cor constante de blend |
| 1,4% | `0x68` Cs·FIX+Cd, **lê o próprio alvo** (bloom) | — | cópia do alvo antes (feedback) |
Texturas quase todas T4/T8 com paleta (CLUT); alvo CT32; Z em Z24.

**Desenho:**
- **Backend novo** `GSHwBackend` atrás da interface `GSRasterBackend` (`gs_backend.h`), escolhido no `GS::init`. Vulkan, reaproveitando o device do `GSGpuAccelerator`.
- **Espelho da VRAM na CPU** continua sendo a verdade para uploads (IMAGE), CLUT e leituras do jogo.
- **Cache de alvos:** cada `FRAME` (fbp, fbw, psm) vira uma textura de cor em escala N; cada `ZBUF`, uma de profundidade. Upload do espelho quando o alvo é criado ou a VRAM foi escrita por cima; download (reduzido para 1x) só quando o jogo lê (local→host, CLUT dentro de alvo, textura que é alvo).
- **Cache de texturas:** decodifica (tbp, tbw, psm, tw, th, hash da CLUT) do espelho para RGBA8; invalidada por escrita na faixa da VRAM.
- **Lotes:** vértices num buffer grande; o lote fecha ao mudar estado (textura, blend, testes, alvo). Shader de fragmento com TFX (modulate/decal/highlight), TCC, alfa de vértice ×2, fog, teste de alfa com AFAIL (FB_ONLY/ZB_ONLY/RGB_ONLY por máscaras de escrita ou duas passadas), DATE pela via do alfa de destino.
- **Feedback** (textura = alvo atual, o bloom): copiar a região do alvo para uma textura temporária antes do desenho.
- **Apresentação:** a janela mostra direto o alvo de exibição (sem passar pela VRAM), na escala N.

**Passos (cada um com o replay do Raiden e da `fase4` como prova, comparando com o renderizador atual):**
- ✅ **F1.** Esqueleto: `GSHwBackend` (`gs_hw_backend.cpp`) repassa tudo ao backend atual e conta o que faria. `PS2X_GS_HW=count` no jogo ou no `ps2x_gs_replay`. **Por campo:** Raiden ~62 500 primitivos em ~2 330 lotes (~27 por lote), `fase4` ~25 000 em ~880, `fase3` ~25 000 em ~750. **Quase todas as quebras de lote são troca de textura** (98%); alvo, blend e testes quase não mudam. Feedback (lê o próprio alvo): ~940 primitivos por campo no Raiden. **Conclusão para o F3:** usar um atlas/array de texturas (ou descritores sem limite) para juntar lotes entre texturas, senão são ~2 300 draws por campo (aceitável no Vulkan, mas é o que vai custar CPU).
- ✅ **F2–F4 (13/11, `PS2X_GS_HW=1`, desligado por padrão).** `GSHwBackend` modo render (`gs_hw_backend.cpp`, shaders `gs_hw.vert/frag`): alvos RGBA16F (r,g,b/255, a/128) e D32 em escala N, lotes com blend fixo (As pela segunda saída do fragment shader, dual-source), teste de alfa com passada AFAIL, fog, TFX/TCC, texturas decodificadas do espelho com cache por conteúdo (o jogo reenvia cada textura para o mesmo lugar antes de desenhar), feedback (bloom) por cópia da região lida, saída de vídeo direto do alvo com um quadro de atraso, e o `GSCpuBackend` sem GPU como espelho para transferências, leituras e paletas. Replay do Raiden: imagem quase igual à atual (PSNR 23 contra ela); menus a 60/s no jogo. **Ainda lento na fase (12–20/s):** as paletas lidas de dentro do buffer do desfoque (dependência real do jogo) obrigam a ler a placa a cada carga. Armadilhas achadas: liberação adiada de imagem precisa de bloco de comandos já aberto (senão Xid 31, falha de página na GPU); busca de textura fora da imagem precisa de limite; alvos de cor e Z na mesma memória coexistem; transferências para dentro de um alvo atualizam só os blocos 8×8 escritos.
- **F4b em andamento (13/11).** Feito: cache de texturas por conteúdo e imagens recicladas (menus 28→60/s); transferências para dentro de alvos atualizam só os blocos 8×8 escritos; `syncInner` só quando os blocos lidos têm escrita pendente na thread do espelho; texturas e paletas lidas de dentro de um alvo não trazem o alvo de volta quando o espelho já tem os blocos (`blockTransferSeq` > `writeSeq`); alvos com a mesma base e largura diferente são descartados em vez de copiados. Fase: 12 → ~20/s. **O que sobra (medido):** ~45 cópias de alvo inteiro por segundo, todas leituras de verdade de pixels da placa: o desfoque (`0x2980`) e a cadeia de Z (`0x2580`, largura 5) lidos como textura de 8 bits com paleta (truque de separar canal). **Próximo:** reinterpretação na GPU (tabela por textura que leva cada texel T8/T4 ao pixel e byte do alvo CT32, calculada uma vez no processador com `GSMem::PixelAddress`) e paleta montada na placa quando a fonte da CLUT é um alvo. Bug corrigido no caminho: laço que esvaziava o mapa de texturas enquanto o percorria.
- **F4b (objetivo).** Paleta na GPU: decodificar só os índices (R8) e aplicar a paleta no shader, com a paleta montada na própria placa quando a fonte é um alvo (o buffer do desfoque). Tira a leitura da placa por carga de paleta e a decodificação por paleta. *Pronto quando:* fase ≥ 55/s com `PS2X_GS_HW=1`.
- **F5.** Feedback (bloom), cópias de Z como textura (`0x12C`/`0x14C`), CLUTs dentro de alvos, local→host. *Pronto quando:* bloom e HUD certos.
- **F6.** Escala N (`PS2X_SCALE=2/3`). *Pronto quando:* captura nítida em 2x e cutscene ≥ 55/s.
- **F7.** Ligar por padrão, filmes e menus conferidos; o atual fica com `PS2X_GS_HW=0`.

### Ordem sugerida (12/11, revista)

1. **E1** (tracinhos) e **E2** (raios): são os defeitos mais visíveis e talvez a mesma causa.
2. **E5** (quedas na cutscene) e **E3** (chiado nos filmes).
3. **Trilha F** (renderizador comum de GPU): resolve E5 e E8 juntos; depois **E9**, **E10**.
4. **E4** (eco), **E14–E16** (controle, configuração, placa certa).
5. **E13** (widescreen), **E11**, **E17**, **E6**, **E7**.
6. Trilha B (Kung Lao: 12/12 com entrada determinística; reabrir só se o usuário vir de novo) e trilha A (folga).

---

### Registro detalhado (até 11/11)

#### Onde estamos (09/11)

| Tela | Quadros/s | Real do PS2 | O que limita |
|---|---|---|---|
| Menus | **60** | 60 | nada (teto) |
| Goro's Lair, cutscene | 20–33 | 60 | o GS (ocupado ~100%, jogo esperando o GS até 50%) |
| Goro's Lair, gameplay | **35–40** (média 38, ~64%) | 60 | o GS (ocupado ~97%); a thread do jogo logo atrás |

Medido com GPU ligada (padrão desde 05/11; com `PS2X_GS_GPU=0`, a fase cai para ~7,5). **Em 09/11, com o cache de texturas:** gameplay 27,5–30 quadros/s e o jogo esperando o GS 0%. A GPU já não limita: o próximo ganho é a thread do jogo (Fase 5). **Depois do JIT nativo de desvios/`ILW`/`ISW` e do Z reduzido na GPU (09/11):** gameplay 31,5–35 quadros/s, GS ocupado 89–99%: GPU e thread do jogo voltaram a empatar.

**Uso de CPU por thread na gameplay** (`/proc/<pid>/task`, 05/11): GameThread 85%, GSWorker 29% (o resto esperando a GPU), GSFrontend 21%, GSBand 18%.

**Para dobrar a gameplay é preciso as duas coisas:** GPU mais rápida no desenho **e** tirar trabalho da thread do jogo. Uma sem a outra dá no máximo ~31–32 quadros/s.

#### Plano para a gameplay (10/11)

**Por que o PCSX2 é mais rápido na mesma máquina:** (1) desenha triângulos com o rasterizador da GPU (pipeline gráfico), não com um compute shader por tile; (2) gasta pouco CPU por primitivo; (3) roda a VU1 numa thread própria (MTVU). Nós fazemos o contrário nos três. E como o vsync é preso por ciclos, cada quadro lento vira câmera lenta: a 38 quadros/s o jogo anda a ~63% da velocidade.

**Hoje dois limites empatam (~38 quadros/s):** o GS (ocupado ~97%) e a GameThread logo atrás. Por campo pesado (replay da `fase2`, ~31 ms): ~12 ms esperando a GPU e ~19 ms de CPU no `GSWorker` (`QueuePrimitive` ~23%, `PrepareGpuPrimitive` ~19%, driver ~12%). Na GameThread: VU1 ~30–45%, IOP ~12%, laço do vsync ~7%. Só chega a 60 atacando os dois.

Ordem, do maior ganho por esforço ao maior risco:

1. ✅ **Medir a gameplay de verdade (10/11).** `gsrec/fase3.gsr`: 20 campos de gameplay gravados aos 270 s (Liu Kang no Goro's Lair), 182 MB; replay 27,2 ms/campo pela GPU (36,8/s, igual ao vivo), 91,5 ms pela CPU, hash `65173649eaffbb7a` nos dois. Descobertas:
   - **A GPU é o limite principal.** Medido com timestamps do Vulkan (`[perf] GPU ocupada`): ao vivo na gameplay 72–84% ocupada, ~21 ms de GPU por quadro a 38/s; no replay 81–84%. Para 60/s a GPU precisa caber em 16,6 ms.
   - O `GSWorker` gasta só ~8,6 ms de CPU por campo no replay (32% de uma thread): `QueuePrimitive` 28%, `PrepareGpuPrimitive` 22%, driver 20%, `WorkerLoop` 9%. Os ~20% em que a GPU fica parada são esses buracos: cortar CPU por primitivo sozinho dá no máximo ~38/0,8 ≈ 47/s.
   - Esperas ao vivo em 290 s (contadas por ponto de `Flush`): cópia do campo completo 10651× / 65,6 s, carga de CLUT 9089× / 21,8 s, o resto desprezível. A cópia do campo agora é assíncrona (`QueueCapture`, ARQUITETURA §3): sozinha não mudou a gameplay (36,8/s, 34–39), porque o worker passa a esperar ao reusar o slot, com a GPU ocupada; mas é pré-requisito do passo 2.
   - **Onde vai a GPU na gameplay** (timestamps por tipo de dispatch, replay da `fase3`): triângulos em tiles 39%, **sprites 21,6% em ~1200 dispatches por campo**, **cópias de snapshot 7,9% (~800 por campo)**, decodificação de textura 2,2%, sincronização host↔GPU 2,2%, upload 1,2%, captura 1,1%. Os sprites eram os passes de bloom/desfoque (ladrilhos 32×32 lendo e somando no próprio `0x8C`): custo fixo de dispatch e barreira, não de pixel.
   - ✅ **Sprites em lote** (ARQUITETURA §3): `fase3` 27,2 → **21,5 ms** (36,8 → 46,5/s), `fase2` ~31 → 26,8 ms, seis replays bit a bit iguais, `gs` 44/33 (as mesmas falhas sem GPU). Sprites na GPU 21,6% → 7,9% e snapshot 7,9% → 3,5%; ainda sobram ~300 dispatches de sprite por campo (lotes cortados por risco). **Ao vivo na gameplay: 36,8 → 40,3/s (34,5–43,5), GS ocupado 99% → 80%, jogo esperando o GS 0%:** a GameThread volta a ser o limite, com a GPU logo atrás (82–86% ocupada).
   - **Nova ordem:** o ganho grande agora é **desenho mais barato na GPU** (passo 4, ou cortar o custo do `gs_tiles.comp`); o passo 2 vem junto para fechar os buracos. A espera da CLUT (21,8 s) entra no passo 2.
2. 🔶 **GS: CPU do worker (10/11, em andamento).** Com a VU1 em thread, o worker voltou a limitar (100% ocupado, GPU 73–76%). Medido no replay da `fase3`:
   - Espera da carga de CLUT: ~1–2 por campo em ~670 cargas, e os conflitos são reais (CLUT em `0x2850` recém-enviada à GPU; CLUT em `0x25D0` dentro de uma faixa de texturas sendo escrita), não fusão grosseira de faixas. Tirar exige carregar a CLUT na GPU (modo novo no shader, CSM1/CSM2, CT16/CT32, CSA) e recarregar a cópia da CPU só quando um desenho de CPU precisar.
   - Enviar à GPU a cada N primitivos piora: N=1500 → 24,9 ms, N=600 → 33,1 ms (contra ~22): cada envio tem custo fixo na GPU (o cache de texturas é por slot e recomeça; sincronização host↔GPU; barreiras). A granularidade atual fica.
   - O que sobra é CPU por primitivo (`QueuePrimitive` 28%, `PrepareGpuPrimitive` 22%, driver 20%), o item abaixo.
   - **Rodada de 10/11** (replay da `fase3`, seis replays bit a bit iguais, `gs` 44/33, `vu1` 42/0, `vu` 44/0):
     - Achado de medida: o replay esperava a GPU a cada campo e media latência (CPU + GPU somadas). `--sync 0` mede vazão como no jogo; números abaixo com ele (tempo total de 12 repetições).
     - `memset` de 256 bytes no começo de `PrepareGpuPrimitive` aparecia como 70% dela (a loja esperava a fila de escritas do registro anterior). Agora só zera o que cada caminho deixa sem escrever: 25% → 21% do worker.
     - Triângulos (`gs_tiles.comp`) = ~46% da GPU, e **só percorrer as listas sem desenhar já custava ~4 ms por campo**: poucas warps (shader grande) e duas leituras dependentes por entrada. Caixas da lista em memória compartilhada, 64 por vez: 21,1 → 19,8 ms por campo (com espera).
     - Corte de tiles que o triângulo não alcança: pares tile×primitivo −30% (10,9 M → 7,6 M em 2 s), tempo igual (a GPU deixou de ser o único limite).
     - O Z por pixel em `double` (1/32 da velocidade na GTX 1050) **não** pesa: trocado por float só para medir, mesmo tempo.
     - Escrever os registros na RAM em vez do BAR: CPU igual e GPU 45% mais lenta. O custo de CPU não vem de escrever no BAR.
     - **Carga de CLUT na GPU** (ARQUITETURA §3): a espera que sobrava (~0,8 por campo, ~3 ms cada, ~1/3 do tempo do worker parado) é real. As CLUTs moram dentro do buffer do desfoque (§5). Esperar só o envio anterior não pega nada (o conflito é no próprio lote). Com a carga na fila da GPU: **5,18 → 4,54 s** (−12%), esperas 120 → 5 a cada 2 s. Tentados e tirados: espelhar no host todo upload grande sem conflito, e faixas de escrita pelas linhas do triângulo em vez da tesoura (as duas pioram junto com a CLUT na GPU: 4,79 s).
     - **Ao vivo na gameplay: ~46 → ~49 quadros/s (46,5–51)**, GPU 77–84%, GS ocupado ~100%.
     - Limite que sobra no replay: a GPU de novo (`wait ring` ~0,5 s a cada 2 s, GPU ~82% ocupada), com ~2,3 M texels de textura decodificados por campo.
   Texto original: **GS: CPU por primitivo 5–8× menor.** Hoje cada triângulo vira um registro de 256 bytes montado e copiado para a memória da GPU, com varreduras de conflito por primitivo.
   - Estado do desenho (TEX0, ALPHA, TEST, FRAME...) num bloco à parte, deduplicado; o triângulo só guarda vértices (~32 bytes) e o índice do estado.
   - Tiras e leques de triângulos como um registro só (vértices compartilhados), em vez de um por triângulo.
   - Conferência de conflitos por mudança de estado, não por primitivo.
   - Três slots de envio: a CPU monta o lote seguinte enquanto a GPU desenha.
   - Meta: CPU do worker de ~19 para ~5 ms por campo pesado; continua bit a bit igual nos replays.
3. ✅ **VU1 numa thread própria (10/11, ARQUITETURA §2):** gameplay ao vivo **40,3 → 45,8–46,1/s** (43,5–48), imagem certa, `gs` 44/33 (as mesmas falhas), `vu1` 42/0, `vu` 44/0. Agora o GS está 100% ocupado e a GPU 73–76%: a CPU do `GSWorker` voltou a ser o limite (passo 2). Detalhe que pegou: o canal do GIF enfileira PATH3 sem drenar e conta com o drain do fim do `processPendingTransfers`, onde em linha os PATH1/PATH2 do VIF1 saem antes (o árbitro ordena por prioridade); a VU1Thread drena no primeiro envio e no fim de cada lote para manter essa ordem. Texto original do plano: **VU1 numa thread própria (como o MTVU do PCSX2).** O VIF1 entrega o microprograma e a GameThread segue; ela só espera onde lê o resultado (memória/registradores da VU1, `VIF1_STAT`, `FLUSH*`, `CHCR` do canal 1). Determinístico: a ordem dos pacotes ao GIF continua a do fluxo VIF1. Tira ~30–45% da GameThread.
4. **Avaliação de 10/11 (replay da `fase3`, `--sync 0`, ~4,3 s em 12 repetições; GPU ~16,5 ms reais por campo, ao vivo ~49/s):**
   - GPU por tipo de trabalho (tirando cada dispatch, só para medir): triângulos em tiles ~40%, decodificação de textura ~12%, sprites ~8%, cópias de snapshot ~6%, uploads/cópia do campo/sincronização ~3% cada.
   - Dentro dos tiles: o laço das listas já não custa nada; amostrar textura ~0,9 s, cobertura e montagem ~0,35 s, o resto escrita.
   - Tentados sem ganho e desfeitos: `texCache` num binding só de leitura (cache L1 da NVIDIA); cache de texturas persistente entre envios (o jogo reenvia as texturas a cada campo: decodificações iguais, e o cache cheio força espera); decodificar só depois de área suficiente (ler da VRAM custa bem mais que decodificar: proporção 1 → 6,9 s, sempre decodificar → 4,3 s); tabelas do `pixelAddress` empacotadas em bits; unificar triângulo e sprite numa só chamada de `sampleTexture`/`writePixel` (zera a pilha de 1 KB por invocação que o driver mostra, mas fica ~3% mais lento).
   - Estatísticas do driver (`VK_KHR_pipeline_executable_properties`): o shader de tiles usa 40 registros (ocupação boa) e 202 KB de binário. A pilha de 1 KB vem das chamadas reais de `sampleTexture`/`writePixel` (duas chamadas para cada uma) e não pesa.
   - Uso útil das invocações nos tiles: ~42% (área dos triângulos ÷ 64 × pares tile×primitivo). É o que o rasterizador recupera: estimativa de −15 a −20% da GPU.
   - A GTX 1050 tem `VK_EXT_fragment_shader_interlock` (pixel) e `VK_EXT_conservative_rasterization`.
5. ✅ **Rasterizador exato (10/11, ARQUITETURA §3):** `fase3` 4,45 → **3,37 s (−24%)**, seis replays bit a bit iguais, `gs` 44/33 com e sem. Ao vivo a GPU caiu de 77–84% para **60–68%** ocupada, mas os quadros ficaram em 42–48/s, com "jogo esperando o GS" em 0%: o limite agora é a GameThread (IOP ~20% dela, passo 6). Achados: rasterização conservadora + interlock ~2× mais lenta nesta placa (dilatação no vertex shader resolve); a VRAM precisa de `coherent`; o retângulo em dois triângulos desenhava a diagonal duas vezes. Plano original: **GPU: triângulos pelo rasterizador da GPU, exato.** Desenho dos registros atuais por `vkCmdDraw` (vértices tirados do registro no vertex shader; posições x0,y0,x1,y1 nas palavras 60–63), rasterização conservadora, e o fragment shader roda o mesmo `trianglePixel`/`sampleTexture`/`writePixel` entre `beginInvocationInterlockARB`/`end`: a ordem por pixel continua a do GS, a cobertura exata continua a do teste de arestas, e as regras de corte de lote ficam. Começar só por triângulos, ligado por variável e comparado bit a bit nos seis replays. Texto antigo: **GPU: triângulos pelo rasterizador da GPU.** Vertex/fragment shader para os casos comuns (triângulo texturizado com blend e Z), o compute atual só para o resto. É o maior ganho de GPU (o shader hoje leva ~12 ms por campo na GTX 1050) e o maior risco: cobertura de pixel e interpolação precisam bater com a CPU; se não baterem bit a bit, decidir uma tolerância medida nos replays.
6. 🔶 **Thread do jogo (10/11).** Perfil da GameThread na gameplay com o rasterizador: IOP ~20% (interpretador, ~4,7 M instr/s a ~40 ns cada, custo espalhado; as faixas mais quentes são as travas de interrupção e o laço dos 64 canais do driver de som do CRI, trabalho real, não espera), **microprogramas da VU0 ~12% no interpretador**, `libc` ~10%.
   - ✅ **JIT na VU0** (ARQUITETURA §2): o mesmo JIT da VU1, com a geração de código da VU0 e os 16 pares antes do fim do programa interpretados (a EE lê MAC/STATUS depois do `VCALLMS`). `PS2X_VU_JIT_CHECK=20` ao vivo: 12,5 M blocos conferidos, 0 diferenças. Gameplay **~44 → ~51 quadros/s (46,5–54)**; a VU0 interpretada caiu à metade.
   - IOP em thread própria descartada por ora: a IOP lê e escreve a memória da EE no meio da fatia (`readGuest`/`writeGuest`), então rodar em paralelo quebra o determinismo do lockstep.
   - Cache por stub no `IopImportRegistry::decode` (antes varria até 64 KB para trás a cada chamada de import): correto, sem ganho medido (~51 quadros/s igual); fica para evitar o pior caso.
   - O que a IOP executa (histograma de PC ao vivo, 1,4 G instruções): trabalho real do driver de som do CRI, não espera. ~16% são cópias byte a byte desenroladas de campos por canal (`0x72380–0x73300`), ~9% as travas aninhadas em volta de `CpuSuspendIntr` (`0x58340–0x58400`), um laço sobre os 64 canais (`0x57d80`). Não há laço ocioso para pular.
   - **Medida rápida da IOP (10/11):** ns por instrução dentro do `runCycles` em 70 s de boot e menus, mais um hash da RAM da IOP a cada 2^26 ciclos para provar que o comportamento não mudou. Linha de base 21,8 ns.
     - Busca única da instrução (`executeInstruction(cpu, instruction)`; o `step` já lia a palavra para conferir stub de import): **21,8 → 18,8 ns**, hashes iguais.
     - Escritas na RAM em linha (`store8/16/32`): exatas, mas sem ganho fora do ruído; desfeito.
     - `EeScheduler::processPendingEvents` construía um `std::deque` local a cada chamada (a libstdc++ aloca mapa e bloco até vazio): agora reaproveita `m_eventsScratch`.
     - Ao vivo: IOP 21% → 18% e `libc` 10% → 8,7% da GameThread; quadros/s 47–54,5, dentro do ruído (~50).
   - ✅ **JIT da IOP** (`iop_jit.cpp`, ARQUITETURA §2): blocos de ALU, loads/stores na RAM e desvio + delay slot. **21,8 → 11,6 ns por instrução** nos menus, 84% das instruções no código gerado, hash da RAM da IOP idêntico ao interpretador. Na gameplay a IOP caiu de ~18% para ~11% da GameThread (7% disso é achar e conferir o bloco: `unordered_map` + `memcmp` das palavras). Quadros/s 46–56,5 (média ~50,5): **o GS voltou a 96–97% ocupado**, ele é o limite de novo.
   - Para depois: conferir blocos por geração de escrita das páginas em vez de `memcmp`; MULT/DIV e entrada com load pendente (10% das tentativas) no JIT.
   - Histórico: recompilar a IOP (o interpretador já está perto do que um `switch` dá; os ~19 ns restantes estão espalhados). O custo (~40 ns no começo) está espalhado pelo `step`/`runCpu`/`executeInstruction` (busca dupla da instrução, checagens de sentinela, interrupção e DMA por instrução). Caminho: blocos básicos decodificados em cache, com as checagens por bloco; depois, se preciso, recompilar a IOP.
7. 🔶 **Pipeline do GS ao vivo (10/11).**
   - ✅ **Três slots de envio** (eram dois): no replay o worker esperava ~35% do tempo o slot de duas levas atrás, com a GPU a 79%. Com três: GPU 96%, `fase3` 5,29 → 4,53 s (12 repetições, `--sync 0`), seis replays iguais.
   - ✅ Memo da última textura no `QueuePrimitive` (a divisão do `unordered_map` aparecia no perfil): CPU do worker −5%.
   - Ao vivo quase igual (43,5–55,5, média ~51): CPU por thread na gameplay GSFrontend 83%, VU1Thread 80%, GameThread 67%, GSWorker 66%, num i7-7700HQ (4 núcleos, 8 threads). 69% do GSFrontend é a espera ativa do anel; encurtá-la de 100 para 10 µs não mudou nada (49,8 contra 51,7, dentro do ruído).
   - **Quem espera quem ao vivo (10/11, cronômetros temporários em cada espera, gameplay a ~52/s):** GameThread **~45% esperando a VU1Thread** (`ps2_vu1_thread::sync`), 0% esperando o frontend, 0% na cadência; GSFrontend ~25% trabalhando e ~75% esperando dados; GSWorker ~30% esperando a GPU e ~5% a fila. **Todas** as esperas da GameThread vêm de um só ponto: leitura do **`VIF1_STAT`** (`readIORegister`, 0x10003C00), ~160 por segundo (≈3 por campo), ~3 ms cada. O jogo consulta o VIF1 esperando a VU1 terminar o que recebeu; para o valor não depender do tempo do host, a leitura espera todo o trabalho pendente. A espera é legítima (no PS2 a EE também espera ali): **o elo é a velocidade da VU1Thread.** Por isso o JIT da IOP e a cópia do frontend ficaram de lado: aceleram threads que esperam, não a que segura.
   - ✅ **Cache das tabelas decodificadas da VU1 por conteúdo (11/11):** perfil ao vivo da VU1Thread com ~44% em decodificação (`decodeLowerUsage`, `getDecodedInstructionPairForPc`, `decodeInstructionPair`, `decodeUpperUsage`): cada troca de programa subia a geração do código e o `rebuildDecodedCodeCache` decodificava os 2048 pares de novo, para os mesmos 4 programas. Agora as tabelas ficam guardadas por interpretador (mapa `thread_local` no `.cpp`, sem mexer no header) com uma cópia do código, e são reaproveitadas quando o código bate byte a byte. `vu1` 42/0, `vu` 44/0. Gameplay **~52 → ~55 quadros/s** (48,5–58).
   - Perfil da VU1Thread depois do cache (11/11): código do JIT 21,5%, `runJitBlock` 21% (os laços do modelo de ciclos sobre as leituras e escritas do bloco; o `readyMax` já é deslizante, é trabalho real), `progressXgkick` 11%, `processVIF1Data` 10%, compilação/tabelas do JIT ~5%.
   - ✅ `progressXgkick`: o `% m_activeVuDataSize` por qword copiado era uma divisão de verdade (43% da função); a memória de dados é 4 ou 16 KB, então virou máscara. Ao vivo 56,3 de média (53–60,5), dentro do ruído da rodada anterior (55,0).
   - ✅ `processVIF1Data` (UNPACK): `writeIndex % wl` e `/ wl` por vetor eram divisões de verdade (o ponto mais quente da função); viraram contadores. Função 10,2% → 8,7% da VU1Thread.
   - ✅ `prepareJitTable`: o hash de 16 KB por troca de programa era uma cadeia serial de multiplicações; quatro faixas independentes. Saiu do topo do perfil (1,9%).
   - ❌ Leituras repetidas no `runJitBlock` (11/11): a conferência de prontidão na entrada é ~metade do `runJitBlock` (~10% da VU1Thread). Tirar as leituras repetidas do mesmo registrador (só a primeira pode travar) é exato (conferidor: 7,3 milhões de blocos, 0 diferenças, 0 de ciclos), mas o `runJitBlock` foi só de 21,3% para 20,8%: quase não havia repetição. Revertido. Os arrays de prontidão (~1,2 KB) cabem no L1; o custo é o laço com desvios e loads dependentes, não falta de cache, então compactar o layout (mexe no `ps2_vu1.h` e recompila 13 mil arquivos) não deve render. Não feito.
   - ❌ Recompilação estática dos microprogramas: não feita. O custo de `runJitBlock` (21%) são as leituras e escritas dos arrays de prontidão do modelo de ciclos, não o despacho; código estático pagaria o mesmo. Ao vivo 55,4–55,7 (ruído ±4).
   - ✅ **Uma thread pesada por núcleo físico (11/11):** com `PS2X_PERF=1` o GS aparecia 96% ocupado e a CPU por thread somava ~3,2 núcleos num processador de 4 núcleos e 8 threads. O agendador do Linux punha duas das quatro threads pesadas (GameThread, VU1Thread, GSFrontend, GSWorker) nos dois hyperthreads do mesmo núcleo, e as duas andavam devagar. Fixar cada uma num núcleo físico (`ps2_perf::pinHeavyThread`, lido de `/sys/.../thread_siblings_list`): **54,2 → 59,3–59,6 quadros/s** na gameplay (três rodadas fixadas contra uma de controle na sequência), quase sempre entre 59 e 60. `PS2X_PIN=0` desliga; com menos de 4 núcleos não faz nada. Por isso os ganhos da VU1 dos passos anteriores não apareciam no fps: o elo era a disputa de núcleo.
   - ✅ JIT da VU1 sem `clampOperand` nos operandos que o próprio bloco produziu com FMAC/ITOF/ABS (nunca são Inf/NaN): conferidor com 12,6 milhões de blocos, 0 diferenças. Ganho não mensurável ao vivo (abaixo do ruído).
   - ✅ Worker do GS (11/11, perfil no replay `fase3`): `noteSourceWrite` varria as 32 origens de CLUT vigiadas a cada escrita de quadro e Z de todo primitivo (~1/4 do `QueuePrimitive`). Limites `watchLo`/`watchHi` pulam a varredura quando a escrita não chega perto de nenhuma: `QueuePrimitive` 33,6% → 24,7%, CPU do worker −12%; hashes dos seis replays iguais. Ao vivo, com as threads fixadas, o GS fica ~94% ocupado e as imagens novas a 52–57/s com o jogo a 59–60: o worker e a GPU são a folga que resta.
   - ❌ Imagens novas na janela (11/11): com o jogo a 59,5–60 quadros/s, a janela mostra ~54–55 imagens novas por segundo. Uma fila de um quadro entre o worker e o `Present` (o quadro que termina antes do anterior ser mostrado espera em vez de sobrescrever) deu 55,0: a perda não é de dois quadros dentro de um refresh. Revertido. Suspeita seguinte: a cadência irregular de término dos quadros no worker (GS ~94% ocupado).
   - ✅ **Causa achada (11/11):** contando o salto do número do quadro a cada `Present`, ele só era chamado ~55 vezes por segundo: a janela pegava quadro novo só quando o contador de vsync do convidado mudava, e com os dois a ~60 Hz alguns refreshes não viam mudança. Latch a cada quadro da janela: **imagens novas 54,5 → 58,5/s** com o jogo a 59,9 (capturas de tela conferidas).
   - ✅ `kSlots = 2` derrubava o jogo no boot: o `Initialize` criava `capture[0]`, `[1]` e `[2]` à mão, e com dois slots `capture[2]` escrevia além do array, por cima dos vetores do lote de sprites. Agora é um laço sobre `kSlots`. Com dois slots o jogo roda a 60,0, mas a janela mostra 48,7 imagens novas por segundo contra ~54,5 com três: fica três.
   - Próximo: VU1Thread mais rápida (perfil de 10/11: `runJitBlock` 20%, código do JIT 22%, `progressXgkick` 11%, `processVIF1Data` 10%; recompilação estática dos 4 microprogramas desenhada no item da Fase 6).
   - Histórico: achar o elo ao vivo. Candidatos: o trabalho real do GSFrontend (`GSCpuBackend::Submit` copiando o lote, ~14%), a VU1Thread (80%, JIT da VU1) e a disputa de núcleos com hyperthreading.
8. **IOP mais barato:** HLE das funções de som que o CRI consulta em laço (`sceSdBlockTransStatus` e companhia) ou IOP em thread própria com fatias de tempo emulado. ~12% da GameThread.

Expectativa: 1+2 tiram o GS do caminho (~45 quadros/s, limitado pela GameThread); com 3, ~55–60; 4 e 5 dão a folga para os trechos pesados e para a cutscene.


- `PS2X_PERF=1`: uma linha a cada 2 s (janela, imagens novas, quadros do jogo, vsync, GS ocupado, jogo esperando o GS). "GS ocupado" conta tempo de parede, inclusive esperando a GPU.
- `PS2X_PROFILE=<s> PS2X_PROFILE_THREAD=<nome>`: perfil por amostragem só de CPU (esperas não aparecem). Threads: `GameThread`, `GSFrontend`, `GSWorker`, `GSBand`.
- Replay: `ps2x_gs_replay gsrec/<x>.gsr --repeat 3` (tempo por campo) e `--compare` contra a VRAM da CPU (`PS2X_GS_GPU=0 ... --dump-vram`). Toda mudança no GS precisa de **0 bytes de diferença** em `titulo`, `menu`, `dificuldade`, `fase` e `fase2` (hash conhecido de `fase2`: `edef4ac4b8f25a78`).
- Ao vivo até a gameplay: `MKSM_SKIP_INTRO=1 MKSM_AUTO_PAD=xs MKSM_AUTO_PAD_UNTIL=40 MKSM_AUTO_HOLD="x@45-45.3,x@50-50.3,...,x@120-120.3"`; a gameplay começa ~95 s depois da fase carregar (~250 s de execução). A seleção de personagem às vezes demora e o script perde os toques.
- Máquina livre: conferir `pgrep -a ps2EntryRunner` e `top` antes (outro processo pesado já falseou várias medições). Com a tela travada a janela não abre.

#### Fase 0: medir rápido

- ✅ `ARQUITETURA.md` e `CLAUDE.md`/`AGENTS.md` com o mapa do sistema.
- ✅ Gravação e replay do GS (`PS2X_GS_RECORD`, `ps2x_gs_replay`), cinco gravações em `gsrec/` (fora do git).
- ✅ Suítes de teste por nome (`ps2x_tests gs vu1 ...`).
- ✅ Medidor `PS2X_PERF` e perfil por thread com threads nomeadas.
- ⬜ **Pular a cutscene de abertura do Goro's Lair.** Medido em 09/11: o jogo já entra no nível aos ~46 s (`func_1BB8A8(0x60)`) e o 3D começa aos ~56 s; o que pesa no teste é a cutscene, até ~120–145 s. Ela não sai com Start. O `Level Select` do jogo (`MODDING.md`) só pula os menus, que já são rápidos. Falta achar o que controla a cutscene (fim forçado ou flag de "já vista").
- ⬜ **Gravação da gameplay** (`fase3.gsr`): a `fase2` é da cutscene; a gameplay tem outro perfil de desenho.

#### Fase 1: ganhos rápidos na CPU

- ✅ Janela sem bloquear (`try_lock`) e cópia do campo na troca `0x00`↔`0x46`: janela a 60 Hz.
- ✅ Logs fora do caminho quente (`AGRESSIVE_LOGS` desligado).
- ✅ Pulo do laço de espera (09/11): os dois endereços antigos nunca rodavam na gameplay; o laço quente era o poll do FIELD do GS CSR em `0x2747a4`. Com ele em `idle_loops` e o pulo limitado ao próximo evento dos timers da EE: gameplay média 36,25 → 38,2 quadros/s (35–40), GS ocupado ~97%: o GS volta a ser o limite. O vsync na gameplay é preso por ciclos (EE a ~60% da velocidade, câmera lenta), então quadros/s depende do tempo de host por ciclo emulado.
- ⬜ Cache de texturas decodificadas (`patches/wip_cache_texturas.diff`): sem ganho medido; só fica se o replay provar.
- ⬜ Upload por linhas no `UploadImageLocked`: perdeu importância (uploads de textura vão para a GPU); só os pequenos ficam na CPU.

#### Fase 2: desenho na GPU (Vulkan compute)

- ✅ VRAM do GS num buffer da GPU, swizzle no shader, CPU como plano B.
- ✅ Sprites (inclusive bloom que lê o próprio alvo), triângulos com o pixel completo, todos os formatos de textura, CLUT com versões, bilinear, Z, blend, fog.
- ✅ VRAM na memória da GPU sincronizada por faixas; envio sem espera em dois slots; uploads e cópias local→local na GPU.
- ✅ Teste `gs` que compara CPU e GPU byte a byte em cada regra de espera.
- ✅ GPU ligada por padrão (`PS2X_GS_GPU=0` desliga; sem Vulkan cai na CPU).
- ✅ Esperas por CLUT: intervalo exato de blocos dos uploads e upload pequeno espelhado no host (379 → 139 esperas por campo na `fase2`).
- ✅ Carga de CLUT na GPU quando a origem tem escrita pendente (10/11, ver passo 2 do plano da gameplay).
- ✅ Os 800 sprites de feedback por campo da `fase2`: já vão para a GPU (conferido em 09/11 com contador: nenhum primitivo da `fase2` cai na CPU).
- ✅ CPU do `GSWorker` no replay (09/11): por campo da `fase2`, ~12 ms dos ~33 são espera pela GPU (3 flushes); o resto é CPU do worker, que é o gargalo do replay. `QueuePrimitive` varria o mapa do cache de texturas duas vezes por primitivo e refazia o hash da CLUT de 16 em 16 bits: com o limite da faixa e o hash por 64 bits, `fase2` ~33 → ~30,7 ms, bit a bit igual. Sobram `QueuePrimitive` ~23% (divisão do `unordered_map`, cópia do registro para a memória mapeada), `PrepareGpuPrimitive` ~19% (faltas de cache lendo o `batch` que outra thread escreveu) e driver ~12%. Tentados sem ganho e desfeitos: agrupar 64 draws por lock na fila do `GSCpuBackend` (o mutex custava ~9% da thread produtora, mas ela não é o gargalo) e prefetch dos comandos no worker (medida inconclusiva com a máquina carregada).
- ✅ Redução do Z da fase na GPU (09/11): o intervalo da textura do sprite ia até a altura do `TEX0` e parecia ler o próprio alvo; `fase2` 38,3 → 35,5 ms, nenhum primitivo da `fase2` na CPU.
- ✅ Intervalo de blocos pelo retângulo e formato (09/11): uploads, cópias e a carga de CLUT (CSM1) usam os blocos que as células ocupam, não a página inteira nem `CBP..+8`. Esperas por CLUT na `fase2` 540 → 120 em 60 campos (sobra 1 por campo: upload duplo real de uma CLUT CT16 em `0x2850`), `fase2` 35,3 → 30,0 ms, cinco replays bit a bit iguais. Ao vivo a gameplay não muda (~27–34 quadros/s nas duas versões, média 31), porque quem limita é a thread do jogo; "GS ocupado" cai de ~91% para ~82%.
- ⬜ Espera do fim de campo (`flushAll` antes de copiar a VRAM completa): ~9,5 ms por campo na `fase2`, é a GPU executando.
- ⬜ Linhas e pontos ainda na CPU (não aparecem na `fase2`). Na gameplay sobram ~20 sprites/s CT16 que leem o próprio alvo.
- ⬜ Apresentação direto da GPU (hoje a janela recebe uma cópia da VRAM).

#### Fase 3: thread do jogo, sem mudar a arquitetura

- ✅ IOP: fila de DMA só quando vence, imports só em `jr $ra`, lotes de 16384 ciclos, AutoDMA no ritmo real, `step()` enxuto (~30% → ~8% da GameThread).
- ✅ VU1 JIT: callouts para `CLIP`/`DIV`/EFU/`FC*`/`ILW`/`ISW`, leitores de Q no meio do bloco (com variante sincronizada), `XGKICK`/`XTOP` no JIT, desvios, tabelas por microprograma, `readyMax`; pares interpretados 62 → 6 milhões em 3 min. Conferidor `PS2X_VU_JIT_CHECK` sem diferença.
- ✅ VU1 JIT, callouts de dentro dos blocos (09/11): por 10 s de gameplay eram ~14 M de correções exatas de MADD, ~12 M de desvios, ~5,5 M de pares que gravam o VI antigo e ~3,4 M de `ILW`/`ISW`. Agora: correção 18, gravação 0, desvios 4 M (os do primeiro par do bloco), `ILW`/`ISW` 0. Sobram `DIV` 7,3 M, `FCAND` 3,7 M, `CLIP` 3,7 M. Conferidor: 0 diferenças em 17,8 M de blocos.
- ⬜ `IBNE` no primeiro par do bloco (3,9 M/10 s) e `DIV` (7,3 M/10 s) nativos.
- ✅ Frontend do GS na thread `GSFrontend` (tirou ~21% da GameThread; ganho em quadros/s só depois que a GPU deixar de ser o limite).
- ⬜ **Custo fixo de entrada/saída dos blocos do JIT** (`runJitBlock` ~10%, código do JIT ~8,5%): blocos maiores, menos conferências por bloco.
- ⬜ **IOP: HLE das funções do LIBSD que o CRI chama em polling** (`sceSdBlockTransStatus` e companhia): corta boa parte dos ~8%.
- ⬜ **libc na GameThread** (mutex/memcpy/malloc ~8,7%): o mutex por primitivo saiu com o `GSFrontend`; medir de novo o que sobrou.

#### Fase 4: GPU mais rápida (o próximo ganho grande)

Hoje o `GSWorker` passa ~70% do tempo esperando a GTX 1050 terminar o `gs_tiles.comp`. Mais threads na CPU não ajudam: a fila já está cheia, quem demora é a GPU.

- ✅ **Onde vai o tempo na `fase2`** (replay, 44,8 ms/campo, medido trocando partes do shader): o desenho do shader é ~24,5 ms; o resto (~20 ms) é decodificar, montar lotes e despachar (com o shader vazio: 20,3 ms). Dentro do shader, a leitura de textura (bilinear: 4 leituras na VRAM com swizzle e 4 da CLUT) custa ~10 ms e a escrita do pixel ~5 ms. O Z em `double` (temia-se o fp64 lento da GTX 1050) e a janela de colunas por pixel quase não pesam (0,5 e 2 ms); `restrict` nos buffers não muda nada.
- ✅ **Tiles de 8×8** em vez de 16×16: 44,8 → 42,8 ms, bit a bit igual.
- ✅ **Cache de textura decodificada na GPU** (09/11): `fase2` 42,5 → ~37 ms, os cinco replays bit a bit iguais à CPU. Ao vivo, a gameplay foi de 25–29 para 27,5–30 quadros/s, com "jogo esperando o GS" em 0%: agora quem limita é a thread do jogo. O que custava era a corrente de leituras dependentes (tabela → VRAM → CLUT), não o número delas (CLUT em 32 bits sozinha: 0 ganho). Na `fase2`, chaveando pelo conteúdo da CLUT: ~1100 decodificações em 20 campos (73 M texels), contra 245 M leituras de textura. Pela versão da CLUT seriam 30 mil (1,7 G texels). Detalhes no `ARQUITETURA.md` §3.
- ⬜ **Triângulos pelo rasterizador da GPU** (vertex/fragment), com o pixel do GS em aritmética inteira no fragment shader e a VRAM continuando como buffer. É o ganho maior e o risco maior: a cobertura de pixel e a interpolação têm de bater bit a bit com a CPU. Validar com os cinco replays.
- ⬜ **Sobrepor CPU e GPU de verdade:** com menos esperas, o `GSWorker` pode montar o lote seguinte enquanto a GPU desenha o atual (três slots em vez de dois).

Meta: GS fora do caminho na gameplay (jogo esperando o GS ~0%).

#### Fase 5: paralelismo da thread do jogo

Depois da Fase 4, a GameThread (EE + VU1 + IOP em série) é o teto. **Regra para todas as mudanças desta fase:** o resultado tem de ser determinístico, igual a cada execução com as mesmas entradas. Isso é obrigatório para o online P2P (coop e versus, em lockstep ou rollback): um IOP ou VU1 que termina "quando der" muda o estado do jogo entre as máquinas. Paralelizar só com pontos de sincronização fixos, como já é feito no `GSFrontend`.

- **Perfil da GameThread na gameplay (09/11, com o cache de texturas, `PS2X_PROFILE=200`):** VU1 ~50% (código do JIT 10%, `runJitBlock` 8%, pares no interpretador ~15%: `execLower`, `calculateFmacExactResult`, `normalizeFmacResult`, `execUpper`, decodificação; `progressXgkick` 4%), código do jogo 13% (dos quais 4,8% no laço de espera do vsync `0x274688`), IOP 8%, VIF1 3%, e ~11% na `libc`. Na `libc`, a maior parte cai logo depois de um `syscall` de futex no meio de `pthread_cond_signal`, mas um `LD_PRELOAD` que contava as chamadas viu zero vindas da GameThread: a origem continua aberta (o `addr2line` na `libc` sem símbolos dá o exportado mais próximo). Tentado sem ganho medível e desfeito: acordar o `GSFrontend` só quando ele dorme e tirar o `notify_one` do `scheduleEvent`.
- ✅ **Fila do `GSFrontend` sem lock (09/11):** a origem da `libc` na GameThread era o `gsAsyncSubmit` (achado com a varredura de pilha nova do perfil: `gsAsyncSubmit ← GifArbiter::drain ← progressXgkick`, ~7% em `futex`/mutex). Trocada a fila com mutex e `notify_one` por um anel de bytes com espera ativa curta no frontend. Gameplay ao vivo: média 31 → **36 quadros/s** (30,5–39), `libc` 12,8% → 6,6% da GameThread. O GS voltou a ficar perto do limite (ocupado 88–99%).
- **Perfil da GameThread depois disso (09/11):** VU1 ~30% (`runJitBlock` 8,4%, código do JIT 7,7%, `progressXgkick` 4,1%, `execLower` 2,1%, `run` 2,1%, `commitReadyPipelines` 1,8%), IOP ~11,6% (`executeInstruction` 7,7%, `runCycles` 3,9%), laço de espera do vsync `0x274688` 6,6%, `libc` 6,6% (sobram `memcpy` do pacote e `writeIORegister`), VIF1 4,5%, `read64` 3,3%, `checkpointDue` 2,9%.
- ⬜ **Laço de espera do vsync** (`sub_00274688`, 6,6%) e **IOP** (11,6%) são os próximos alvos baratos da GameThread; a GPU precisa cair junto (espera de fim de campo, Fase 4).
- ⬜ **VU1 numa thread própria** (~45% da GameThread com o JIT): o VIF1 entrega o microprograma e o EE só espera onde lê o resultado (VU1 memória/registradores, `VIF1_STAT`, o `CHCR` do canal 1). Medido: o jogo espera o canal 1 ~1,7 ms depois de cada envio de ~8 ms, então sobrepõe pouco sozinho (~15%); vale junto com a próxima.
- ⬜ **IOP numa thread própria** (~8%): ele avança em fatias de tempo emulado combinadas com o EE (por exemplo a cada vsync ou a cada RPC), nunca pelo relógio real.
- ⬜ **Executar o lote seguinte do EE enquanto a VU1 termina o anterior**, quando o jogo não lê nada da VU1 no meio.

Meta: gameplay a 60 quadros/s.

**Vazão sem a trava de tempo real (11/11, `PS2X_UNCAPPED=1`):** menus a 200–250 campos/s, gameplay só **~60–65**, com GS ~90% ocupado e GPU (GTX 1050) 81–84%. O teto atual da gameplay é quase exatamente 60: não há folga. Para 90 seria preciso ~1,5× mais vazão no worker do GS **e** na GPU; a GameThread (IOP, VU0) não é o limite, então JIT da IOP e som nativo não aproximam dos 90. Além disso, a lógica do MKSM anda um passo por vsync: mais vsyncs por segundo aceleram o jogo. Um modo de 90 fps de verdade precisaria separar lógica e imagem (interpolar quadros ou corrigir o passo de tempo do jogo), fora do alcance atual.

#### Fase 6: rumo ao 100% nativo

Trocar o que hoje é emulado por código nativo, sem perder o comportamento do jogo. Cada troca precisa de um teste que compare com o emulado.

- ⬜ **Som:** o CRI ADX/AIX hoje roda dentro do IOP emulado (decodificação no EE, mistura no IOP, saída por AutoDMA). Um decodificador e mixer nativos no host eliminariam o IOP do caminho do som. Atenção às armadilhas já achadas (`notas/audio.md`): ordem L/R, criptografia do ADX, `CpuSuspendIntr`.
- ⬜ **IOP inteiro em HLE:** cartão de memória, controle e CD já são HLE em parte. Com o som nativo, o IOP restante pode virar funções do host.
- 🔶 **VU1 recompilada estaticamente** (engenharia reversa feita em 10/11, implementação não começada):
  - **Os programas:** o jogo usa **4 microprogramas de 16 KB**, sempre enviados como 8 `MPG` de 2 KB (destinos `0x0000`–`0x3800`), mais um stub de 8 bytes em `0x0000`. Em ~300 s: A 47 254 envios, B 34 824, C 12 410, D 2 403, ou seja ~320 trocas de programa por segundo (~5 MB/s de microcódigo). Só 14% dos `MPG` regravam o que já estava lá. Os 55 estados de memória de código vistos são combinações desses pedaços; o JIT já guarda tabelas por hash do código (`tablesByCode`). Os programas são código do jogo: ficam fora do git (despejo local, como o `gsrec/`).
  - **Onde vai a VU1Thread** (perfil por thread, gameplay, com a VU1 já em thread): código do JIT 21,9%, **`runJitBlock` 20,3%**, `progressXgkick` 10,6%, `processVIF1Data` (UNPACK) 9,5%, `execLower`/`execUpper` ~7%, **`compileJitBlock` + `prepareJitTable` 3,6%**, `commitReadyPipelines` 3,2%.
  - **Por que `runJitBlock` custa tanto:** a cada entrada de bloco ele aplica o modelo de ciclos da VU1 (quando cada VF/VI/ACC fica pronto, trava de FDIV/EFU, variante com Q sincronizado, `XGKICK` em andamento) e grava os ciclos de saída. É isso que mantém Q, P e o envio ao GIF iguais ao hardware.
  - **Desenho:** um tradutor (script, como o `ps2xRecomp` do EE) que gera uma função C++ por programa, com os pares de instrução em ordem e `goto` nos desvios. Os quatro são reconhecidos pelo hash no `MSCAL`; programa desconhecido cai no JIT/interpretador. O modelo de ciclos precisa ir junto, estático: como o programa é fixo, o tradutor calcula em tempo de geração as latências entre pares do mesmo bloco, e só os limites de bloco consultam o estado de ciclos. Conferência: rodar o gerado e o interpretador lado a lado (como o `PS2X_VU_JIT_CHECK`) até 0 diferenças.
  - **Ganho esperado:** ~20–25% da VU1Thread (entrada/saída de bloco e compilação). Hoje a VU1Thread usa ~73% de um núcleo e não é o único limite: a linha jogo → VU1 → frontend do GS → worker → GPU está equilibrada (GSFrontend 72%, GameThread 60%, GSWorker 54% + esperas da GPU). Por isso o ganho em quadros/s vem só junto com o worker do GS (passo 2).
- **Medição por thread (10/11, gameplay a ~46/s):** VU1Thread 73%, GSFrontend 72%, GameThread 60%, GSWorker 54% (o resto esperando a GPU).
- ⬜ **GS:** com a Fase 4 feita, o desenho já é nativo na GPU; falta só a apresentação direto.

#### Histórico das medições

Resumo, em ordem. Os detalhes de cada correção estão no `PLANO.md` (seção 5A) e no `git log`.

- **Início (runs 134–152):** ~2 fps visuais no menu e ~0,5 na fase, tudo desenhado na CPU. Os números visuais eram piores que o ritmo do jogo por causa da janela presa no lock do GS e da cópia tirada no meio do campo; corrigido no item 1 da Fase 1 (janela 20–35 → 60 fps).
- **Fase 2, blocos 1–2 (03/11):** sprites e triângulos na GPU, bit a bit iguais. Com a máquina livre a Intel HD 630 perdia da CPU; a GTX 1050 ganhava pouco (fase 36,8 ms na CPU contra 44,7 ms na GPU).
- **Bloco 3 (04/11):** esperas só em conflito real e CLUT com versões: fase 36,8 → 27,7 ms; menus a 47 campos/s ao vivo.
- **Bloco 4 (04/11):** VRAM na memória da GPU (antes a GPU lia pelo PCIe): fase 27,7 → 20,8 ms. A fase passou a ser limitada pela thread do jogo.
- **Bloco 5 (04/11):** IOP sem custos por instrução, JIT da VU1 de 68% para 84% dos pares, ciclos da VU1 pulando até o próximo evento, sem zerar 64 KB por XGKICK: menus a 60, fase 17–22 → 26–37.
- **Bloco 6 (04–05/11):** pipeline da VU1 em cache, envio da GPU sem espera, uploads de textura na GPU: fase 31–39 ao vivo. AutoDMA no ritmo real: 34–41.
- **Raiz quadrada corrigida (05/11):** a fase passou a desenhar a cena certa e caiu para 14–20. As medidas anteriores eram de uma câmera olhando para uma parede.
- **Fase real, rodadas 1–3 (05/11):** gravação `fase2.gsr` (13 MB de comandos por campo); flushes por campo ~75 → ~19, replay 60 → 49 ms; JIT da VU1 até 93,7% dos pares, depois pares interpretados 62 → 6 milhões; IOP 12,6% → ~8%. Descoberta: o que se media era a cutscene de ~95 s; a gameplay roda a 24–28 quadros/s. VIF1 assíncrono sozinho sobreporia só ~15%.
- **GPU ligada por padrão (05/11):** sem a variável, a fase era desenhada na CPU: 7,5 → 25–27,5 quadros/s na cutscene. Cinco replays com 0 bytes de diferença.
- **Frontend do GS em thread própria (05/11):** −21% de CPU na GameThread, quadros/s iguais (A/B com `PS2X_GS_ASYNC=0`): o limite já era a GPU.
- **Shader da GPU (05/11):** partição do tempo da `fase2` (acima, Fase 4) e tiles de 8×8: 44,8 → 42,8 ms.
- **Fila do `GSFrontend` sem lock (09/11):** gameplay 31 → 36 quadros/s de média; `libc` na GameThread 12,8% → 6,6%.
- **Blocos exatos de uploads e CLUT (09/11):** `fase2` 35,3 → 30,0 ms; ao vivo igual (limite na thread do jogo), GS ocupado 91% → 82%.
- **CLUT sem esperar a GPU (05/11):** 339 de 379 esperas vinham de cargas de CLUT presas a uploads; intervalo exato dos uploads e espelho no host: 379 → 139 esperas, `fase2` 51 → 44 ms. O tempo total de espera quase não caiu (686 → 550 ms em 888): o resto é a GPU executando.
- **XGKICK em blocos (11/11):** dentro dos dados de um GIFtag, `progressXgkick` copia de uma vez todos os qwords que o crédito de ciclos paga (todas as leituras de uma chamada veem a mesma memória da VU). Perfil da `VU1Thread`: `progressXgkick` 11,1% → 8,6%, mas a `libc` (memcpy) sobe 6,8% → 10,3%; ganho pequeno, porque a função roda a cada ciclo e quase sempre copia um qword só. Conferidor do JIT: 12,6 M blocos, 0 diferenças; gameplay 58,3 quadros/s (perto do teto de 60).
- **XGKICK adiado (11/11):** dentro dos dados de um GIFtag, longe do último qword, o PATH1 só deve leituras; `advanceOneCycle` guarda o ciclo como crédito e as leituras acontecem antes do próximo commit de store na memória da VU, no fim da tag e no fim de cada `run` (o VIF pode escrever entre execuções). Mesmos bytes, muito menos chamadas. `progressXgkick` sai do topo do perfil da `VU1Thread` (8,6% → <3,7%), `libc` 10,3% → 8,3%; gameplay 59,8 quadros/s; conferidor do JIT 13,6 M blocos, 0 diferenças; teste "XGKICK observes stores committed before a future PATH1 qword" passa.

### Teste do usuário (12/11): seis problemas

1. **Imagem pixelada.** ✅ (filtro) A janela ampliava o quadro de 640x448 sem filtro. Filtro bilinear ligado (`ps2_runtime.cpp`). Falta: renderizar em resolução interna maior (upscale no GS da GPU).
2. **Chiado no canal direito nos filmes.** Ver o áudio do Sofdec (ADX/PCM intercalado do SFD): ordem L/R ou um bloco errado no canal direito.
3. **Ataques com atraso.** Medir a latência do pad até a imagem: fila do GS assíncrono, buffer de vsync e leitura do pad a cada quadro.
4. **Texturas faltando, dá para ver através das paredes.** Gravar um `.gsr` no lugar e achar a primitiva descartada (teste de Z, clipping ou textura não enviada).
5. **Cores diferentes do original.** Comparar com os prints: modulação de cor da textura (TFX/TCC), alfa e níveis de cinza no shader da GPU.
6. **Cutscene do Raiden em câmera lenta.** Medido: o vsync caía para 36–55/s porque o relógio do EE só anda com o trabalho da CPU. Pulo de quadro no `EeScheduler` (ver ARQUITETURA §tempo): agora 55–60/s, e a janela parou de oscilar entre 60 e 7 fps. Sobram duas quedas de ~4 s a 36/s, presas na VU1Thread (o JIT da VU1 e o VIF1 dominam o perfil). Próximo: perfilar a VU1Thread nessas quedas. O item 3 (golpes lentos) provavelmente era isso; conferir com o usuário.

#### Comparação com prints do jogo original (12/11)

Prints do YouTube guardados em `../referencias_original/` (fora do git: imagens do jogo), contra `PS2X_SCREENSHOT` e captura da janela da mesma cena (Goro's Lair, Raiden):
- **Conferido no replay:** CPU e GPU dão o mesmo hash na `fase3`, e a mistura dos dois circuitos (PMODE) já está no `Present`. A diferença não vem da GPU nem da saída de vídeo.
- ✅ **Campos misturados na janela** (`PS2X_DEINTERLACE=0` desliga): média do campo atual com o último da outra página, como a TV. Tira o tremido entre campos.
- ✅ **Mipmap, um nível por primitivo** (`GS::buildDrawBatch`): LOD = (log2(1/Q) << L) + K com o Q médio dos vértices e TEX0 apontado para o nível de `MIPTBP1/2`. ~40% dos desenhos com textura na fase usam `MMIN=5`, `MXL=2`. Na captura, a melhora é pequena: **o mipmap não era a causa principal**. Os `.gsr` gravam lotes já montados, então o replay não testa essa mudança (hashes iguais).
- **Causa real do "ruído" (achada no zoom do framebuffer):** tracinhos verticais escuros de 1 px (2 linhas na janela) espalhados pelo chão e pelas paredes. Parecem pixels em que a segunda passada (luz/detalhe por cima da mesma geometria) não desenhou: teste de Z da passada 2 falhando por diferença mínima de Z entre as passadas, ou regra de cobertura nas bordas dos triângulos. **Próximo passo:** achar num `.gsr` da fase uma passada dupla sobre a mesma geometria, contar os pixels que a passada 2 rejeita por Z e comparar o Z interpolado das duas.
- **Falta o desfoque/brilho de tela cheia.** O original é todo suave, com brilho em volta das luzes e dos personagens. O nosso sai nítido e duro. É a mesma suspeita da trilha C: um passe que reduz o quadro, desfoca e mistura de volta não está saindo (ou sai com alfa 0). Isso também explica a reclamação de "pixelado" e parte das "cores diferentes". Próximo: num `.gsr` da fase, listar os desenhos que leem o próprio framebuffer (página `0x00`/`0x46` como textura) e conferir o alfa e o resultado de cada um.
- **Raios de luz:** no original, feixes suaves em degradê; no nosso, placas claras chapadas no alto da tela.
- **Chão:** no nosso, riscos pretos serrilhados nas rachaduras; no original, sombras suaves. Provavelmente é o mesmo passe de mistura.
