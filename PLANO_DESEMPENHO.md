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
Esperado, exatamente: `gs: Failed: 2`, `vu1: Failed: 0`, `vu: Failed: 0`, `float: Failed: 0`. As 2 do `gs` são conhecidas: "texture page buffer hides local-memory writes until TEXFLUSH" (o backend lê a VRAM direto, sem imitar o cache de páginas de textura; o MKSM manda TEXFLUSH) e "sceGsSetDefDBuffDc seeds display envs…" (biblioteca que o MKSM não usa por esse caminho). Até 15/11 eram 33: 31 testes liam a VRAM antes de a fila do `GSCpuBackend` terminar; o `ps2x_tests` agora liga `PS2X_GS_CPU_SYNC=1`, e cada comando termina antes de voltar. Sem argumento, `ps2x_tests` roda todas as suítes: 460 passam, 3 falham (as 2 do `gs` acima e "SifInitRpc does not reset the running IOP", tempo do IOP em teste, sem relação com o jogo). A suíte `memory` abortava num assert do `EeScheduler` (escalonador sem thread executora quando um stub é chamado direto do teste); desde 15/11 passa (53/0).

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

**R4b. Replays do renderizador novo em 2x (com e sem a thread)**
```bash
cd /run/media/valk/linux_arquivos/projetos/ShaolinMonks && for t in 0 1; do for c in raiden fase4 menu golpe hud selecao pausa titulo; do echo -n "$c $(PS2X_GS_HW_THREAD=$t PS2X_GS_HW=1 PS2X_SCALE=2 PS2Recomp/out/build/ps2xTest/ps2x_gs_replay gsrec/$c.gsr 2>&1 | tail -1 | awk '{print $3}') "; done; echo; done
```
Esperado nas duas linhas: raiden `4ff24d7ae57ce969`, fase4 `eb01c45fec2ce0b7`, menu `ffd8380942bfb1a2`, golpe `9c482222a5541134`, hud `83a5306598c2c5f2`, selecao `b2d43403db02170d`, pausa `b172663b41a7f2d3`, titulo `2376a56c0f07db16`. Velocidade do lado que desenha: `PS2X_GS_HW=1 PS2X_SCALE=2 ps2x_gs_replay gsrec/bench_cutscene.gsr --repeat 2 --sync 0`, 5 rodadas, mediana do total (gravação de 1,27 GB fora do git; regravar com `PS2X_GS_RECORD_AT=76 PS2X_GS_RECORD_FRAMES=60`).

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
- [x] N03 Mipmap por pixel (trilinear) no renderizador padrão — feito e ligado (`PS2X_MIP_PIXEL=0` desliga); sem custo (gameplay 59,6 → 59,9/s); hashes antigos iguais. Na cena gravada, o fundo fica um pouco mais suave, mas o xadrez forte do print 2 não aparece nela → N03b
- [x] N03b Chão quadriculado — **corrigido** (14/11): o VIF agora anda na frente da VU1 (`PS2X_VIF_AHEAD`, ligado). O jogo manda as cores e o TEX0 de cada lote logo depois do MSCNT, direto no pacote que o programa envia; rodando o programa na hora, cada lote saía com as cores do lote de dois antes. Chão, paredes e fundo da cutscene agora batem com as referências do original
- [x] N04 Mipmap por pixel no renderizador novo — 15/11:
  - O frontend passa o nível 0 com a marca (bit 10 do `TEX1`) também para o renderizador novo.
  - O `resolveTextureSlow` decodifica os níveis 1..MXL (de `MIPTBP1/2`) num atlas abaixo do nível 0, na mesma imagem e no mesmo cache por conteúdo, com os blocos de todos os níveis.
  - O `gs_hw.frag` calcula o LOD por pixel como o `gs_tiles.comp`: log2(1/|Q|)·2^L + K, ou K com LCM; MMIN 2/4 pega um nível, 3/5 mistura dois; limites de região divididos por nível.
  - Armadilha: o MKSM deixa `MIPTBP` de níveis que nunca alcança apontando para alvos de desenho. Decodificar esses níveis trazia o alvo de volta da GPU a cada textura (cutscene 50 → 31 quadros/s). A cadeia agora para no primeiro nível que cai num alvo, e o custo ficou igual ao de um nível por primitivo (48,6 contra 49,1).
  - Gravações novas (as antigas guardaram a escolha de nível do frontend): cutscene novo × antigo 29,7 dB, gameplay 28,4/28,1. Nas cenas medidas a diferença visível é pequena; o efeito aparece no chão ao longe.
  - Hashes 2x, R4 e testes iguais (as gravações de referência não têm a marca).
- [x] N04b Liu Kang "girando" parado — **corrigido** (14/11): a instrução MMI `PABSW` era traduzida lendo o registrador errado (rs em vez de rt). O jogo usa `PABSW` para escolher o caminho mais curto entre dois quadros-chave de ângulo (`0x2AD378`); com o valor errado, na volta do laço da animação de espera os ossos davam uma volta de 360° em 4 quadros. Corrigidas também `PROT3W`, `PEXEW`, `PEXEH`, `PEXCH`, `PEXCW`, `PREVH`, `PINTH`, `PINTEH`, `PADDUH`, `PSUBUH` no recompilador. Nas gravações dos tempos em que o mortal sempre aparecia, ele fica parado. Resíduo a conferir: dois quadros soltos com pose de pulo na janela logo no começo da gameplay (~0,1 s, na entrada), que não aparecem na gravação do GS

**Fase 3: luzes**
- [x] N05 Raios: eram o mesmo defeito do chão (N03b). Os passes "opacos" dos raios tinham o alfa de vértice de outro lote; com o VIF na frente da VU, o `softenLightRay` não disparava mais (0 de 1,94 milhão de candidatos em 165 s; 290 mil sem a correção) e foi apagado junto com `PS2X_RAYS`. Raios da abertura da cutscene macios como no `goro_0_raios_teto.png`
- [x] N06 Raio arco-íris: provavelmente o mesmo defeito (cores de outro lote). Confirmar com o usuário (seção 10)
- [x] N07 Raios macios: idem; confirmar com o usuário (seção 10)

**Fase 4: engasgo na cutscene do Raiden**
- [x] N08 Quedas da cutscene medidas (14/11): VU1 + revezamento EE/VU1 na espera de todos os caminhos do jogo (`0x24E680`); trechos limitados pela GPU depois. Detalhes na seção 7
- [~] N09 Quedas — **câmera lenta corrigida** (14/11): o pulo de quadro do `checkpointDue` parava no lote de timers (≤ 1024 ciclos) e quase não pulava; agora para no próximo disparo de timer. Cutscene e combate com vsync 60 (velocidade certa); o jogo desenha 34–38 quadros/s nos dois trechos pesados da cutscene e ~52 em combate (antes ~46 em câmera lenta). Também corrigido: capturas prontas sobrescritas antes de chegar à janela. Correr na frente do GS (`VIF1_STAT` sem esperar + PATH3 na fila da VU1) testado de novo com isso: sem queda de imagens, mas ganho de ~1%, retirado. Falta: menos quadros perdidos (VU1 e GPU, seção 7)

**Fase 5: imagem nítida**
- [x] N10 Renderizador novo: menos custo por troca de estado — 15/11: o que sobrou foi para a N20 (atalho de estado, `BoundState`, CLUT por conteúdo)
- [x] N11 Renderizador novo: janela sem esperar a GPU, sem câmera lenta quando o GS atrasa, thread própria sem falha — 14/11, em 2x na cutscene: janela 60 fps estável (antes 12–48) com imagens novas = quadros do jogo; vsync ~60 (antes 15–35); thread própria sem falha em 3 rodadas de 150–160 s (era vazamento de memória de vídeo: 4 GB em 50 s, agora ~670 MB estáveis). O jogo desenha 16–24 quadros/s em 2x: isso é a N12. Barreiras estreitas passam para a N12 (decidir pelo perfil de GPU)
- [x] N12 Renderizador novo a 60/s em 2x (menos cópias de feedback e lotes, perfil de GPU por passo) — 15/11: 2x tão rápido quanto o renderizador antigo e padrão (N13); o resto (60 cravados) é a N20
- [x] N13 Renderizador novo como padrão, com escala 2x (o usuário já escolheu o 2x em 14/11) — 15/11: o `main.cpp` do runner põe `PS2X_GS_HW=1`, `PS2X_SCALE=2` e `PS2X_GS_HW_THREAD=1` (com 4+ threads de hardware) quando não vierem do ambiente/`mksm.ini`; `PS2X_GS_HW=0` volta ao antigo; testes e replay sem mudança. R5 sem variável nenhuma: cutscene 49,6 de média (antigo 50,2), gameplay 57,7 (mín 50; antigo 55,9 na mesma rodada de roteiro); abertura, avisos e título certos; 5,5 min de combate sem falha, memória da placa estável (~215 MiB) e do processo estável (~754 MB). Não é 60 fixo: os vales são do EE/VU1 nos dois renderizadores (N20)

**Fase 6: o resto do jogo (com o usuário)**
- [ ] N14 Avançar no jogo, apontando defeitos com F9 (antigas T15, T17)
- [ ] N15 Saves, filmes do fim e dois controles (antigas T16, T18, T19)
  - **15/11, relatos do usuário jogando a pasta portátil:**
    - **Tela rosa de ~1 s antes de alguns filmes:** quando a janela não pegava um quadro do jogo naquela volta (boot, troca para um filme), o `UploadFrame` subia uma textura magenta de depuração herdada do PS2Recomp. Agora fica a última imagem (preta antes da primeira). Capturada no quadro 5 da janela no boot; não aparece mais.
    - **Filme do menu (parado na tela de título) com eco e tocando errado:** o `OPENING` toca como demonstração e, se a pessoa continua parada, de novo. O `MPEG.cpp` só reabria o arquivo do filme quando o nome mudava: na segunda vez o vídeo seguia do meio e o som vinha do caminho emulado (com o esticamento do WSOLA, que soa como eco). Agora o `sceMpegDelete` fecha o arquivo. Medido: a segunda exibição abre o som do arquivo de novo e começa com os mesmos volumes da primeira.

**Fase 7: robustez e extras**
- [x] N16 Teste de uma hora — 14/11: 3660 s de roteiro com andar, golpes e combate contra vários inimigos (morre e recomeça), sem travar; memória estável em 428–430 MB do começo ao fim. Mostrou a câmera lenta em combate (~46 vsync/s), corrigida depois na N09
- [~] N17 Build do zero e guia do jogador — 14/11: um PS2Recomp limpo no commit fixado + `scripts/aplicar_patches.sh` compila o runtime e os testes sem rede (fontes do FetchContent reaproveitadas) e dá os mesmos resultados (`gs` 33 antigas, `vu1`/`vu`/`float` 0; hashes de `titulo`, `menu` e `fase` iguais): a lista de patches está completa. README (inglês e português) atualizado: `scripts/jogar.sh`, `mksm.ini`, F9/F10/F11, volumes, estado do chão e dos raios. Falta: compilar o código gerado do zero (horas) e testar o `instalar.sh` inteiro numa máquina limpa
- [ ] N18 Outras máquinas e testes antigos com falha (antigas T28, T29) — 15/11:
  - Intel HD 630 (`PS2X_GPU_DEVICE=Intel`): renderizador novo funciona. Em 2x a placa fica em 96–99% e o jogo em 35,2 de média na cutscene; com a predefinição "Leve" (1x, sem FXAA, bilinear), 48,9 e a placa em 58–73%, quase como na NVIDIA.
  - Sem Vulkan (`VK_ICD_FILENAMES` inexistente): volta sozinho ao renderizador antigo na CPU, com imagem certa e 20–40 quadros/s.
  - Testes `gs`: 33 → 2 falhas (modo síncrono `PS2X_GS_CPU_SYNC` nos testes).
  - Suíte `memory`: o assert antigo saiu (`assertExecutor` aceita o escalonador ainda sem thread executora), 53/0. Todas as suítes: 460/3.
  - Falta: build do zero só com o README
- [x] N19 Widescreen, controle configurável e latência, quadro cheio (antigas T21, T23, T24) — 14/11: FXAA de verdade (busca ao longo da borda) na resolução do PS2 antes da ampliação, ligado por padrão (`PS2X_FXAA=0` desliga; o HUD não borra); zona morta do analógico configurável (`PS2X_DEADZONE`, 25%); janela com ampliação bicúbica por padrão (`PS2X_UPSCALE=0` volta ao bilinear). Widescreen: a string "WideScreen" do ELF é só um rótulo de etapa de carregamento (`GameMain.cpp`), não uma opção nativa. **15/11: widescreen feito** pelo patch 16:9 do PCSX2 para esta versão (`pcsx2_patches`, `SLUS-21087_455DD546.pnach`). A parte de dado, o fator de proporção horizontal em `0x4F4B38` (1,0 no ELF → 0,75), é escrita a cada quadro pela janela, que então mostra a imagem em 16:9. Liga e desliga na hora: "Tela 4:3/16:9" no menu Gráfico, ou `PS2X_WIDESCREEN=1`. Durante filmes (`sceMpegCreate`…`sceMpegDelete`) a janela fica em 4:3. Cutscene e gameplay com a proporção certa e mais campo nas laterais; o 2D (avisos, menus, HUD) fica esticado, como no PCSX2. As partes de código do patch não entraram, porque numa recompilação estática não dá para trocar instrução escrevendo na RAM: a "borda preta" (`0x322F2C`: constante 88,0 → 1,0; não apareceu nas capturas) e a largura dos filmes (`0x272210`/`0x272498`: variável → 598, substituída pelo 4:3 nos filmes). Se a borda aparecer: patch de instrução no `sm.toml` (`[patches]`) trocando o `lui` por um `lw` de uma palavra fixa na memória baixa, que o runtime preenche conforme a opção (continua dando para ligar e desligar). Faltam controle configurável e latência
  - **15/11, fechada:**
    - **Controle configurável:** a janela do F1/R3 ganhou abas (Gráfico | Controles; L1/R1 ou Q/E). Na aba Controles, os 16 botões do PS2 mostram o botão do controle e as teclas; X numa linha espera o próximo botão ou tecla (Backspace cancela; F1 e F9–F12 ficam com a janela) e troca com quem já usava aquele botão. "Restaurar padrão" volta tudo. Grava no `mksm.ini` só o que saiu do padrão (`PS2X_PAD_<botão>=<botão do raylib>`, `PS2X_TECLA_<botão>=<tecla>[,<tecla>]`). Tabela em `ps2_pad.cpp` (`padBindings`, header `ps2_pad_bindings.h` fora do `ps2_runtime.h`). Testado com `MKSM_GFX_SCRIPT` + `xdotool key --window`: X → K gravado como `PS2X_TECLA_X=75,0`.
    - **Atraso:** `PS2X_PERF` mede o tempo do fim de cada campo até a janela mostrá-lo. Com a fila em ordem da N20 item 7, ele ficava em 20–29 ms e às vezes travava em ~54 ms (dois quadros sempre esperando: os relógios do jogo e da janela se afastaram um campo). Agora a janela pula uma vez quando há dois esperando por meio segundo seguido: menus 4–18 ms, gameplay 20–25 ms, sem os 54 ms, com 59–60 de 60 imagens. O resto do caminho (controle lido uma vez por quadro da janela, ~8 ms em média; o jogo lê no começo do quadro e desenha nele) é do próprio jogo e do raylib.
    - **Quadro cheio (T21): abandonado.** Com o renderizador novo em 2x, juntar os dois campos acrescentaria pouco detalhe e um quadro de atraso.
- [ ] N20 60 quadros/s fixos e ritmo regular (pedido do usuário em 15/11: "70 fps ou no mínimo 60, pra ficar fluido") — plano na seção 7
- [ ] N22 60 quadros/s, fase 2 (pedido do usuário em 17/11): engasgo no Windows (medida, threads fixas, vsync) e os vales que sobraram (PGO, JIT da VU1, IOP em thread) — plano na seção 7
- [x] N21 Opção "Gráfico" para PC fraco (pedido do usuário em 15/11) — feito: menu do runtime por cima do jogo (R3 fora da gameplay, F1 a qualquer hora), escolha do usuário; detalhes na seção 7

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
- **Resultado (14/11):**
  - **Marca:** o frontend deixa o `TEX0` no nível 0 e liga o **bit 10 do `TEX1`**. Os bits 10–18 não são usados pelo GS e o MKSM nunca os liga; os bits 44–63 ele liga, porque escreve o K estendido no sinal, e por isso o bit 63 não serviu.
  - **Registro:** o registro do primitivo do `gs_tiles.comp` cresceu de 64 para **72 palavras** (as 64 estavam cheias: 36–50 são cor/UV/STQ dos vértices, 52–57 o Z deles em `double`). O mipmap ocupa as palavras 64–68:
    - 64: MXL, MMIN, MMAG, L, LCM e o K (12 bits);
    - 65–66: `MIPTBP1` como foi escrito;
    - 67–68: `MIPTBP2`.

    Mudaram juntos: `kRecordWords` no shader e no acelerador, `GSGpuPrimitive::words`, `gs_raster.vert` e a cópia do acelerador.
  - **Shader:** `sampleTexture` calcula `lod = LCM ? K : log2(1/|q|)·2^L + K` por pixel e amostra com `sampleLevel`/`samplePointLevel` (endereço e largura do nível, tamanho dividido por 2^n). MMIN 4/5 filtram dentro do nível, 3/5 misturam dois níveis; com `lod <= 0` vale o MMAG.
  - **Sem mipmap por pixel:** o renderizador novo (`gsMipPerPixel()` falso enquanto ele desenha), os sprites do `gs_sprite.comp` e o rasterizador da CPU continuam com o nível 0 ou o antigo (atalhos anotados no código).
  - **Medido:**
    - hashes de `titulo menu dificuldade fase fase2 fase3` iguais aos de antes;
    - `ps2x_tests gs` 47/33 (como antes);
    - gameplay 59,6/s sem e 59,9/s com (60 s, roteiro determinístico).

    No mesmo instante gravado (165 s, `MKSM_PAD_CLOCK=reads`), com e sem: quase iguais. O fundo fica um pouco mais suave, que é o comportamento certo, mas o xadrez forte do print 2 do usuário (perto do fogo) não está nessa cena (N03b).

### N03b Achar o desenho do xadrez do print 2
- **Por quê:** no print 2 do usuário o chão tem quadrados claros e escuros em grade, perto do fogo. Na cena gravada da primeira sala (N03) não há esse padrão, e o mipmap por pixel não mudou o que se vê ali.
- **Suspeita principal:** uma passada de iluminação ou sombra do chão (textura pequena esticada) desenhada com filtro de ponto quando devia ser bilinear (`MMAG`, ou `linearFilter` decidido no `gs_frontend.cpp`), deixando cada texel como um quadrado.
- **Faça:**
  1. Peça ao usuário um F9 parado no lugar do print 2 (seção 10), ou chegue lá por roteiro.
  2. No replay, desligue um desenho de cada vez (ou pinte cada desenho de uma cor) até achar o que faz os quadrados. Anote `TEX0`, `TEX1` (`MMAG`, `MMIN`, `LCM`, `K`), o `linearFilter` que o frontend calculou, o blend e o tamanho da textura.
  3. Corrija o filtro ou a passada, e compare com `goro_1_kunglao_luta.webp`.
- **Pronto quando:** o lugar do print 2 sem quadrados, numa captura.
- **Achados (14/11), com as cenas que o usuário gravou com F9** (`gsrec/cena_20261114_1149*.gsr`; capturas em `capturas/`). O usuário: os quadrados existem desde a primeira vez que o jogo rodou, também na cutscene do Raiden; ele acha que falta suavização, textura ou upscale.
  1. **Os dois renderizadores mostram os mesmos quadrados** no replay da `cena_20261114_114950.gsr` (padrão e `PS2X_GS_HW=1`). Logo, o defeito já está no que chega ao GS, não no rasterizador nem no filtro de textura dos renderizadores.
  2. **Desenhos mais ampliados na cena:**

     | Textura | Formato | Ampliação | Blend | Triângulos | Filtro |
     |---|---|---|---|---|---|
     | `0x2400` | T4 512×256 | ~19× | `0x89` = `Cd·As`, TFX 2 | ~5 mil | bilinear |
     | `0x2A57` | T8 128×128 | ~23× | `0x68` = `Cs·FIX + Cd` | — | bilinear |
     | base do chão (`0x2931`, `0x2A00`) | T4 | 5–6× | — | — | trilinear, MMIN 5 |

     Tirar `0x2400` ou `0x2A57` no replay **não** tira os quadrados; tirar `0x2400` só clareia um pouco (aqui ela só escurece).
  3. **Triângulos de base do chão:** Gouraud ligado (IIP 1) em 100%, mas em ~50% deles os três vértices têm **a mesma cor** (ex.: 15, 21, 25 de 128, bem escuro). Triângulos vizinhos com cores chapadas diferentes = manchas de borda dura. Ou seja, a iluminação chega ao GS por triângulo/face, não suave por vértice.
  4. **Hipóteses, em ordem:**
     - **(a) Iluminação por vértice calculada errado antes do GS:** microprograma da VU1, ou EE/VU0 (conversões FTOI/ITOF, MINI/MAX, EFU). Num chão de malha grossa, o PS2 deve gerar cores diferentes em cada vértice compartilhado. Teste: comparar a cor do **mesmo vértice** (mesma posição) em triângulos vizinhos; se divergir, o cálculo está por face.
     - **(b) Mapa de luz:** o conteúdo ou a CLUT da textura `0x2400` estariam errados (a passada deveria clarear e suavizar). Teste: exportar a textura do replay como imagem e ver se é um gradiente suave.
     - **(c) Suavização/upscale ausente (hipótese do usuário):** os renderizadores não mudam o resultado (achado 1). Mas uma passada de pós-processamento do jogo (desfoque ou bloom em tela cheia) que não está sendo aplicada daria esse efeito. Teste: listar no replay os desenhos de tela cheia com textura = alvo (feedback) depois do chão, e ver se algum sai vazio ou é pulado (`fallback`, `feedback`).
     - `PS2X_VU_JIT=0` ficou lento demais (4–9 vsync/s) para chegar à gameplay em 290 s: inconclusivo.
  5. **Teste da hipótese (a), feito:** no mesmo quadro e na mesma textura de base, agrupando os vértices pela posição de tela e pelo Z, **268 de 540 posições compartilhadas têm cores diferentes conforme o triângulo**, com diferença de até 120 no R (128 = luz cheia). Ou seja, o mesmo vértice da malha sai claro num triângulo e escuro no vizinho. **É a causa do xadrez:** a iluminação que o jogo calcula (microprograma da VU1, com dados que vêm do EE pelo VIF) sai diferente por tira ou face.
  6. **Próximo:**
     1. Achar o microprograma da VU1 que desenha o chão (o `MSCAL`/`MSCNT` antes dos XGKICKs com `tbp0` `0x2931`/`0x2A00`).
     2. Gravar a memória da VU1 de entrada (dados do VIF: posições, normais, luzes) de duas tiras vizinhas.
     3. Ver se a mesma posição chega com a **mesma normal e as mesmas luzes** nas duas tiras:
        - se a entrada já difere, o erro está antes (EE/VU0 ou `UNPACK` do VIF com sinal ou máscara errada);
        - se a entrada é igual e a saída difere, é uma instrução da VU1 (suspeitas: `ERSQRT`/`ESQRT`/`EATAN` da EFU, `FTOI`/`ITOF`, `MINI`/`MAX` com zero negativo, `CLIP`, flags/`FMAND` usados para decidir quais luzes somar).
     4. Ferramenta útil: um modo que, para um `MSCAL` escolhido, salva memória e registradores da VU1 antes e depois.
     5. Antes de mexer em instrução, confira a semântica no manual da VU (`notas/` ou o `ARQUITETURA.md`) e crie um teste no `ps2x_tests vu1`.
  7. **Causa achada e corrigida (14/11)** com uma gravação temporária da VU1 (estado e memória antes de cada programa, pacotes do XGKICK, UNPACKs com os registradores do VIF) num quadro da gameplay (vsync 5400 do roteiro R5):
     - O chão de base (`0x2931`) é desenhado em lotes de ~76 vértices pelo programa em `pc 3800` (MSCNT, buffer duplo: TOPS 24/524, saída em 220/720). O brilho médio de cada lote variava sem padrão: 11, 17, 11, 36, 18, 38, 12, 34.
     - A sequência no VIF é `MSCNT` do lote N e, **logo depois**, `UNPACK V4-8` (endereço absoluto 225 ou 725, CL 3/WL 1, máscara só no ciclo 0) com **exatamente o número de vértices do lote N** (51, 75, 77, 76, 66...), direto nos campos RGBAQ do pacote de saída do lote N.
     - No console o VIF escreve as cores enquanto a VU transforma os vértices, e elas chegam antes do XGKICK. Aqui o programa rodava inteiro no MSCNT: o lote N saía com as cores do lote N−2 (último uso do mesmo buffer).
     - **Correção:** `ps2_vif1_interpreter.cpp`, o programa fica pendente até um comando que espere a VU (ver `ARQUITETURA.md` §3 e §5). `PS2X_VIF_AHEAD=0` volta ao jeito antigo.
     - **Conferido:** rodando cada programa dos dois jeitos por 170 s de jogo (2 milhões de MSCNTs), 57% dos lotes mudam só a cor (RGBAQ), 0,2% mudam também o TEX0 (o jogo manda a textura do lote do mesmo jeito) e **nenhum** muda posição ou ST. Nenhum MSCAL muda. Vértices com cor diferente na mesma posição: 550 → 128 (o resto são bordas entre objetos diferentes).
     - Testes: `vu1` 0 falhas (teste novo "VIF keeps unpacking after MSCAL..." falha com `PS2X_VIF_AHEAD=0`), `gs` 33 antigas, hashes iguais.
     - A hipótese (a) da iluminação calculada errado estava errada: a VU1 calcula certo, o problema era a ordem.
- **Seção 9:** "O chão ainda aparece quadriculado?"

### N04b Liu Kang "girando" parado
- **Por quê:** o usuário relatou (14/11) que o Liu Kang dá glitch de animação, "meio rodando quando tá parado", **toda vez que ele para, em qualquer lugar**. Pode ser o mesmo tipo de erro de conta da hipótese (a) da N03b (EE/VU0, ponto flutuante).
- **Faça:**
  1. Grave 30 quadros com ele parado (F9 várias vezes, ou `PS2X_GS_RECORD_FRAMES=30` no roteiro, depois que a gameplay começa) e compare os vértices do personagem quadro a quadro no replay. A pose de espera deve repetir suave; "girar" sugere salto de quadro de animação ou matriz recalculada com erro.
  2. Separe as causas, uma variável por vez, olhando a animação de espera:
     - `MKSM_NO_IDLE_SKIP=1` (pulo de relógio no laço de espera);
     - o pulo de quadro do `checkpointDue` (ARQUITETURA §4; pode faltar um interruptor);
     - `PS2X_VU_JIT=0`;
     - `PS2X_GS_CSR_LAG=0` (o jogo vê o GS de outro jeito).
  3. Se for tempo: a animação avança pelo contador de vsync em `0x67B79C` ou por relógio próprio. Confira o passo por quadro com despejo de RAM (`PS2X_DUMP_RAM_AT`).
- **Pronto quando:** a causa está achada e corrigida, ou a pergunta "ainda gira?" está na seção 10 com a correção mais provável pronta atrás de uma variável.
- **Achados (14/11, depois da N03b):**
  - Reproduzido sem o usuário: com o roteiro R5 até a gameplay e **nenhum toque** depois do X de 95 s, capturas a cada 2–4 quadros (`PS2X_SCREENSHOT_FROM`/`UNTIL`, novos) mostram o Liu Kang parado na maioria dos quadros e, **soltos, um quadro de cada vez**, poses de outras animações (pulo com pernas dobradas, mortal, chute voador, encolhido no ar). Não é um salto de verdade: o quadro antes e o depois mostram ele parado. É isso que parece "girando".
  - Os quadros "parado" não são idênticos entre si (o quadro é desenhado de novo), então não é página velha do framebuffer: a pose do esqueleto sai errada por um quadro.
  - Não é a ordem VIF/VU1: na comparação da N03b, nenhuma posição de vértice muda entre os dois jeitos.
  - **Próximo:** `S/liu_run.sh <pasta> [VAR=valor]` (no scratchpad desta sessão; recriar: roteiro R5 sem toques após 95 s, capturas 5460–5700 a cada 2) e `liu_count.py` (conta quadros isolados que diferem dos dois vizinhos na área do personagem). Rodar com `PS2X_VIF_AHEAD=0`, `MKSM_NO_IDLE_SKIP=1`, `PS2X_GS_CSR_LAG=0`, `PS2X_VU1_THREAD=0`, 2 rodadas cada. A variável que zera os quadros soltos aponta a causa. Suspeitas: tempo de animação lido no meio da atualização (pulo de quadro do `checkpointDue`, ou VU0/VIF0 rodando síncrono como era a VU1), ou uma conversão float↔int da EE no índice do quadro da animação.
  - Roteiro: letras `R L U D` no `MKSM_AUTO_HOLD` empurram só o analógico (`r l u d` também apertam o direcional, que abre o mapa).
  - **Determinístico (14/11, mais tarde):** com a letra `g` no `MKSM_AUTO_HOLD` (grava a cena do GS uma vez por toque, `PS2X_GS_RECORD_DIR`, `PS2X_GS_RECORD_FRAMES=6`) em `g@97-98,g@100-101,...,g@124-125` e o replay com `--ppm-each <prefixo>` (as duas páginas depois de cada quadro): nas gravações de 97, 103, 109, 115 e 121, os quadros 2, 3 e 4 mostram o **mortal completo para a frente** (curvado → de cabeça para baixo → saindo do giro), e o quadro 5 já mostra ele parado. Nas gravações de 100, 106, 112... ele fica parado. Idêntico com `PS2X_UNCAPPED=1`: não é tempo de host.
  - A pose errada já está nos dados do GS: vem das matrizes do esqueleto (EE/VU0, antes da VU1).
  - **Descartados com medida:** `PS2X_VU1_THREAD=0`, `PS2X_GS_CSR_LAG=0`, `MKSM_NO_IDLE_SKIP=1`, `PS2X_VIF_AHEAD=0`, `PS2X_UNCAPPED=1`, arredondamento para zero na GameThread (teste temporário com `fesetround`), VU0 sem JIT (teste temporário), DMA do scratchpad (o jogo só usa o modo normal, já tratado), flags MAC da VU0 no modo macro (as 73 leituras de `CFC2 vi17` vêm todas depois de um `VSUB`, que já calcula as flags). O CVT.W.S já trunca e satura (inline no `fpu_translator.cpp`).
  - **Hipótese principal:** volta do laço da animação de espera (período de 300 leituras do pad). Ou o índice do quadro passa do fim por 3 quadros e lê a animação seguinte (o pulo com mortal do Liu Kang), ou a interpolação entre o último e o primeiro quadro-chave vai pelo caminho longo (quatérnio com o sinal trocado, giro de 360°).
  - **Causa e correção (14/11, mais tarde):**
    1. RAM despejada no mesmo ponto de cada quadro (primeira leitura do `VIF1_STAT`) em volta de um mortal: uma matriz de rotação de osso (`0xF84680`, Liu Kang) recebe, nos 4 quadros do mortal, rotações extras de 0°, ~86°, 180°, ~274° em torno do mesmo eixo, ou seja, uma volta completa.
    2. Vigia de escrita (`mprotect` + passo único) na matriz: quem escreve é `0x214E60` (matriz a partir de 3 ângulos, seno/cosseno por microprogramas da VU0), chamada de `0x2AD530`.
    3. Antes da chamada, `0x2AD31C`–`0x2AD440` interpola ângulos de 12 bits entre quadros-chave: calcula a diferença `d` e `d + 0x800`, embrulha em 12 bits (`psllw`/`psraw` 20), tira o valor absoluto com **`pabsw`**, soma as 3 componentes e fica com a menor.
    4. O código gerado tinha `PS2_PABSW(GPR_VEC(ctx, 0))`: o tradutor lia rs (sempre `$zero` aqui) em vez de rt, então as duas somas davam 0 e a escolha ficava errada em certas voltas do laço.
    5. Corrigido no `mmi_translation_helpers.cpp` (e as outras permutações de MMI com ordem errada); teste novo "MMI permutations use the R5900 element order" e `PABSW`/`PABSH` no teste de leitura de rt. Código regenerado: 51 arquivos mudaram (correções de MMI e o `VMAX`/`VMINI` da VU0 que nunca tinha sido regenerado); com o `sm.toml` do repositório, a geração nova é idêntica ao que foi compilado.
  - **Próximo (antigo):** achar a função que amostra a animação. Despejar a RAM (`PS2X_DUMP_RAM_AT`) no quadro antes e no quadro do mortal, procurar o estado da animação do Liu Kang (tempo/índice que dá a volta) e seguir quem o lê no `output/`. Comparar com a instrução da EE envolvida (suspeitas restantes: `MAX.S`/`MIN.S` com `std::max`, MMI de 128 bits, `DIV`/`MULT1`, conversões no VU0 macro `VFTOI`/`VITOF`).

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
- **Medido (14/11, R5 com o renderizador atual; quedas em 64–72 s e 78–84 s, 32–38 quadros/s):**
  - CPU nas quedas: VU1Thread 84–86%, GSFrontend ~80%, GSWorker ~65%, GameThread cai para ~41%.
  - **A GameThread espera ~1,15 s a cada 2 s** num `ps2_vu1_thread::sync()`, ~2 vezes por quadro. Vem da função do jogo `0x24E680` ("espera todos os caminhos": `D1_CHCR`, `D2_CHCR`, `VIF1_STAT & 0x1F000003`, `BC2T`, `GIF_STAT & 0xC00`); a leitura do `VIF1_STAT` sincroniza. EE e VU1 se revezam: o quadro custa EE + o resto da VU1.
  - Perfil da VU1Thread nas quedas: código do JIT 25%, `runJitBlock` (despacho) 21%, `processVIF1Data` 10%, `run`/`execLower` (interpretador) ~13%, `commitReadyPipelines` 4%, `prepareJitTable` 3%, `compileJitBlock` 3%.
  - O GSWorker fica "ocupado" 95–100% do tempo, mas ~30% disso é espera de fence (500–900 ms a cada 2 s), porque ele envia um lote à placa a cada pedaço de comandos (600–1900 envios a cada 2 s, só 3 slots). Agrupar os envios (≥ 3 ms) baixou o "GS ocupado" para ~70%, **sem mudar o fps**: não era o limite.
  - **Testado e revertido:** responder o `VIF1_STAT` sem esperar (como o CSR lag) e pôr o PATH3 do EE na fila da VU1Thread (sem esperar). O jogo desenhou mais quadros (média 51 → 54,7 de 56 a 100 s), mas a janela passou a receber só **12–14 imagens novas por segundo** em boa parte da cutscene (média 49,6 → 33,7): com o EE na frente, o GS e a placa acumulam atraso e a apresentação morre de fome. Pior para quem joga.
  - **Métrica certa:** nos `[perf] janela`, olhe "jogo N quadros/s" e "imagens novas N/s", não só o vsync (o vsync pode ficar em 60 com o jogo pulando quadros).
  - Depois das quedas (84–90 s) a placa (GTX 1050 Mobile) fica 98–100% ocupada a ~56 quadros/s: há trechos limitados pela GPU também.

### N09 Tirar o gargalo das quedas
- **Faça:** conforme a N08. Candidatos já levantados (apêndice E5):
  1. Dividir o `GSWorker` em duas threads: uma prepara os registros e as cargas de CLUT, a outra grava os comandos Vulkan (~40% do tempo em cada metade).
  2. Baratear a cópia do registro de 256 bytes no `QueuePrimitive`.
  3. Juntar cargas de CLUT repetidas.

  Medir antes e depois, lado a lado, três rodadas cada (armadilha 5).
- **Tentado (14/11), ver a N08:** os três candidatos acima não atacam o limite (o GSWorker não era o gargalo). O que segura as quedas é a VU1 somada ao EE na espera de todos os caminhos.
- **Feito depois (14/11):** pulo de quadro corrigido (vsync 60 nos trechos pesados) e capturas guardadas antes de reusar o slot (as 12–14 imagens/s sumiram). Com os dois, `VIF1_STAT` sem esperar + PATH3 na fila da VU1 deram só +1% de imagens (50,1 → 50,9 de média de 56 a 100 s) e saíram.
- **Próximos passos, em ordem:**
  1. ~~**Apresentação com o GS atrasado:**~~ (resolvido, ver acima) com o EE correndo na frente, a janela recebeu só 12–14 imagens novas/s, embora o GS processasse ~55 campos/s. Achar por que as capturas (`QueueCapture`/`publishCapture`/`TakeCapture`, 3 slots) não chegam à janela nesse caso. Corrigido isso, `VIF1_STAT` sem espera + PATH3 na fila da VU1 dão +3–4 quadros/s de média (o código está descrito na N08; refazer atrás de variável).
  2. **VU1 mais rápida:** no `runJitBlock`, ~28% é o laço das leituras com atraso (`block->reads`) e ~14% o das escritas (`m_vfReady[...] = entry + offset`), ou seja ~9% da VU1Thread. `processVIF1Data` 10%: caminho rápido para UNPACK V4-32/V4-16/V4-8 sem máscara e com CL=WL.
  3. **GPU:** depois das quedas a GTX 1050 Mobile fica em 98–100%; perfil do `gs_tiles.comp` por tipo de primitivo (sprites de bloom).
- **Pronto quando:** `PS2X_PERF` sem linha abaixo de 58/s na cutscene, em três rodadas; R3 e R4 iguais. Seção 9: "Ainda sente engasgo na cutscene?"

### N10 Renderizador novo: menos custo por troca de estado
- **Por quê:** é o caminho para imagem nítida (escala 2x/3x); hoje faz 20–32/s em 1x.
- **Feito (14/11):** as ~2 leituras de volta por quadro eram de um sprite que lê só um canto 32×32 da textura 1024×1024 em `0x2580` (página 0x12C, CT32, largura 10); a pegada nominal dela chega à superfície de Z em `0x2980` (escrita pela GPU), e o renderizador conferia a textura inteira. Agora, quando a textura inteira exigiria trazer dados de volta e o desenho fica numa região pequena e dentro dela (modo de borda simples), ele confere e decodifica só essa região (`sampledRegion`, `partialRead`; esses desenhos não entram no atalho de estado repetido). `texread` 56 → 0 a cada 2 s; PSNR das cenas igual (raiden 29,0/30,3, fase4 28,8/29,0, menu 46,9, pausa 23,1); replays do novo mais rápidos (raiden 173 → 157 ms, fase4 289 → 189 ms). Ao vivo em 2x na cutscene: 16 → 19 quadros/s de média.
- **O que ainda segura o 2x (14/11):** (1) a janela cai para 12–48 fps: o `Present` do caminho sem thread pega a trava do renderizador, que o frontend segura enquanto desenha; (2) com o GS mais lento que o tempo real, o EE fica parado esperando por ele e o vsync cai (câmera lenta: o pulo de quadro não age com o EE parado); (3) o GSFrontend gasta 59% em driver/libc (gravação de comandos): ~260–310 lotes e 36–39 cópias de feedback por quadro, cada cópia fecha o passo de render. CPU do frontend ~65–72% e GPU ~55–67%: ainda há espera entre os dois. Com `PS2X_GS_HW_THREAD=1`: 15–17 quadros/s e uma falha de segmentação intermitente (1 em 3 rodadas). O usuário testou o 2x: "fica muito melhor, mas muito lento, tudo piscando, câmera lenta".
- **Medido (14/11, cutscene do Raiden, 56–100 s):** renderizador atual média 51 quadros/s; novo em 1x 28; novo em 2x 16. Em 2x, nem CPU (GSFrontend 34–44%, o resto < 20%) nem GPU (62–68% no `nvidia-smi`) saturam: o limite são as esperas síncronas, ~2 `texread` por quadro (cada uma espera a GPU terminar e deixa a GPU parada no trabalho da CPU). Na seleção de personagem, o 2x some com a trama de quadradinhos do 1x (cabelo e pele lisos): é a melhora de imagem que o usuário pediu.
  - Medido (T07a/b, apêndice): ~750 mil primitivos/s numa thread só.
  - O atalho de estado acerta 94%, mas cada falha (`drawState`) custa ~6 µs, e são ~50 mil/s: metade de feedback, um terço porque `generation` sobe a cada carga de CLUT ou transferência.
- **Faça:**
  1. Faça `generation` invalidar só o que depende do que mudou. Uma carga de CLUT só afeta lotes com textura de paleta; uma transferência só afeta lotes cuja textura toca os blocos escritos.
  2. Perfil do `drawState` (R7 com contadores por parte: `surface()`, `upload`, `resolveTexture`, `pipeline`, `flushBatch`) e corte do que mais pesa.
  3. Feedback: desenhos seguidos que leem regiões já copiadas não precisam de nova cópia (já existe a lista de retângulos; veja se dá para não quebrar o lote).
- **Pronto quando:** fase com `PS2X_GS_HW=1` em 1x ≥ 45/s, PSNR igual.

### N11 Renderizador novo: janela sem esperar a GPU, sem câmera lenta, thread própria
- **Por quê:** é o "tudo piscando" e a "câmera lenta" que o usuário viu em 2x (14/11). Não dependem da GPU ser rápida: são esperas e travas.
- **Faça:**
  1. **Janela sem trava nem espera:** hoje o `Present` do caminho sem thread pega a trava do renderizador (que o frontend segura enquanto desenha) e o `finishPresent` espera a cerca da GPU; a janela cai para 12–48 fps. O frontend deve publicar a última imagem pronta num slot (como as capturas do renderizador atual: `QueueCapture`/`TakeCapture`, com `parked`), e a janela pega a mais nova cuja cerca já sinalizou, sem trava e sem esperar; se nenhuma estiver pronta, repete a anterior.
  2. **Sem câmera lenta com o GS atrasado:** quando o EE fica parado esperando o GS (CSR/FINISH, `VIF1_STAT`, fila cheia), o tempo do jogo não anda e o vsync cai abaixo de 60. A espera deve contar como tempo passando (vblanks e timers avançam, como no pulo de quadro do `checkpointDue`), para o jogo perder quadros em vez de ficar lento. Conferir na cutscene em 2x: `PS2X_PERF` com vsync 60.
  3. Troque o `fullBarrier` (ALL_COMMANDS→ALL_COMMANDS) por barreiras estreitas (cópia→fragmento, fragmento→cópia).
  4. **`GSHwWorker` (`PS2X_GS_HW_THREAD=1`):** corrigir a falha de segmentação intermitente (1 em 3 rodadas em 14/11; rodar com `gdb -batch -ex run -ex bt` até pegar) e medir de novo (15–17 quadros/s em 2x em 14/11, abaixo dos 19 sem thread).
- **Pronto quando:** em 2x na cutscene do Raiden, janela a 60 fps sem piscar, vsync 60 (velocidade certa) e três rodadas com a thread ligada sem falha.
- **Feito (14/11):**
  1. Janela: cópia do campo completo na troca `0x00`↔`0x46`, gravada pela thread do GS num anel de `kSlots + 2` cópias; o `Present` pega a mais nova já terminada, sem trava e sem esperar (`record`/`noteDraw`/`takeNewest`). Com uma só cópia publicada, a GPU um ou dois campos atrás deixava a janela sem imagem nova (contadores: cópias publicadas, nenhuma pronta). Janela 60 fps; imagens novas = quadros do jogo.
  2. Câmera lenta: o relógio de vsync recomeçava depois de 2 campos de atraso; o EE espera o GS ~30–130 ms por quadro em 2x (medido: ~1,2 s a cada 2 s em `gsAsyncWaitLastVblank`, uma espera por quadro). Agora recomeça só depois de 30 campos (meio segundo). Testados 8/15/30: média de vsync 47/54/58 na cutscene. Renderizador atual sem mudança (vsync 60, carga igual).
  3. Thread própria: a falha era `vkAllocateMemory` sem memória (-2) e o driver caindo no `createImage`: o `contentCache` só tinha teto de 3000 entradas, e leituras parciais de texturas 1024×1024 (4 MB) mudam a cada quadro. Teto de 512 MB no `contentCache` e 128 MB no `recycled`. Memória de vídeo ~670 MB estável (antes subia até 3,9 GB e caía). Com a thread: janela 60 fps, 17–23 quadros/s, parecido com sem thread (16–24).
  4. Item 3 (barreiras estreitas) fica para a N12, decidido pelo perfil de GPU.

### N12 Renderizador novo a 60/s em 2x
- **Por quê:** com N11 feita, o que sobra é custo real: o GSFrontend gasta 59% em driver/libc gravando comandos (~260–310 lotes e 36–39 cópias de feedback por quadro, cada cópia fecha o passo de render) e a GPU fica em 55–67%.
- **Faça:**
  1. **Perfil de GPU por passo:** timestamps (`VkQueryPool`) em volta de cada passo de render, cópia de feedback, cópia de sombra e leitura de volta; um resumo por segundo atrás de `PS2X_PERF`. Sem isso, os itens abaixo são chute.
  2. **Menos cópias de feedback:** reaproveitar a cópia quando a região lida não foi desenhada desde a última cópia (a lista de retângulos já existe); não copiar quando o desenho lê só fora do que foi desenhado no passo.
  3. **Menos lotes:** juntar lotes seguidos com o mesmo estado (o atalho de estado acerta 94%; ver por que os lotes não se juntam) e invalidar por `generation` só o que mudou (item 1 da N10).
  4. **Menos banda em 2x:** RGBA8 em vez de RGBA16F nas superfícies onde o resultado é exato (CT32/CT24 sem blend com sobra), conferindo PSNR.
  5. **Gravação de comandos mais barata:** perfil (`perf`) do frontend em 2x; push constants/descritores reaproveitados no lugar do que aparecer no topo.
- **Pronto quando:** cutscene e fase em 2x ≥ 58/s em três rodadas, PSNR das cenas igual, capturas nítidas e certas.
- **Feito (15/11):**
  1. Perfil de GPU por tipo (`PS2X_PERF`: `[perf] GPU novo`). Primeira medida em 2x: envio 400–700 ms/s, desenho 100–230, sombra 30–77; GPU 60–100%.
  2. O envio era quase todo o `refreshPending` (blocos de transferências sobre superfícies): uma cópia + barreira completa + ampliação **por bloco** em 2x, e todos os blocos pendentes de todas as superfícies a cada transferência (o `download` com lista vazia chamava o reenvio completo). 70–145 mil blocos/s, quase todos em `0x2980` (CT32 e Z24, 640×128, regravada em ~30 transferências por campo). Agora: em lote (uma cópia e uma ampliação por superfície, rascunho próprio, sem repetir bloco) e sob demanda (só quando a superfície é usada). Média do jogo na cutscene em 2x (60–108 s): **19 → 25,5 quadros/s**; GPU ~33% (desenho ~150 ms/s, sombra ~50). A GPU deixou de ser o limite: agora é a CPU do GSFrontend.
  3. Perfil de CPU em 2x: sem thread, o GSFrontend fica ~98% ocupado (limite). Corrigido: a lista de blocos pendentes virou uma por superfície (a Z24 de `0x2980`, quase nunca usada, acumulava dezenas de milhares de entradas varridas a cada uso de superfície); o registro de depuração de cada primitivo e GIF tag pedia o `GetTransferSnapshot` (duas travas, uma delas a do renderizador, que a `GSHwWorker` segura) e agora só os eventos de transferência pedem; a paleta convertida (256 `clutColor` por consulta de textura, 14% da thread do renderizador) fica guardada até a CLUT ou o modo de leitura mudar. Medida alinhada pelo fim da carga (48 s de cutscene, `media.sh` na pasta de rascunho): sem thread **19,0 → ~28 quadros/s**, estável entre rodadas.
  4. Com `PS2X_GS_HW_THREAD=1` o resultado é bimodal (18–25 ou ~33 quadros/s na mesma build); fixar a `GSHwWorker` no núcleo da `GSWorker` não mudou isso (desfeito). Nesse modo o GSFrontend fica ~51% ocupado e a `GSHwWorker` ~92%: decodificar texturas na CPU (`resolveTextureSlow` + leituras por pixel) ~25% e aplicar transferências no espelho (`UploadImageLocked` + `WriteP8`/`WriteCT32`) ~19%.
  5. Transferências de host para VRAM por corridas de linha (`GSMem::WriteRow`, tipo do pixel resolvido na compilação; antes, escolha de formato, chamada e divisão por pixel): hashes R4 e PSNR iguais; replays do novo mais rápidos (menu 434 → 172 ms, seleção 67 → 39, raiden 178 → 144). **2x na cutscene: ~28 → ~38 quadros/s** (duas rodadas, 38,1 e 38,4). Renderizador atual sem mudança (50,8; o limite dele é a VU1/EE).
  6. Cargas de CLUT não vão mais para a fila do backend da CPU (trava, aviso e a `GSWorker` acordando a cada carga, só para os desenhos de reserva): o renderizador entrega a própria CLUT na hora de um desenho de reserva (`SetClutTable`). Imagens iguais; 2x ~38,5 → ~39. Com a CPU mais rápida, a GPU volta a pesar: com o jogo a 44–46 quadros/s ela fica ~80% ocupada (desenho ~430 ms/s, sombra ~165, envio ~200), ~18 ms por quadro.
  7. Blocos pendentes juntados em retângulos da grade de blocos de cada superfície (uma região de cópia e de ampliação por retângulo; a `0x2980` inteira vira um retângulo em vez de ~1300 regiões): envio na GPU ~200 → ~10 ms/s; GPU 40–70% ocupada em 2x. Imagens iguais (hashes 2x iguais). 2x na cutscene: 36–41 quadros/s (variação entre rodadas).
  8. Medida estável para o GS: `gsrec/bench_cutscene.gsr` (60 quadros da cutscene aos 76 s, 1,27 GB, fora do git; gravar com `PS2X_GS_RECORD=... PS2X_GS_RECORD_AT=76 PS2X_GS_RECORD_FRAMES=60`) e `PS2X_GS_HW=1 PS2X_SCALE=2 ps2x_gs_replay gsrec/bench_cutscene.gsr --repeat 2 --sync 0`, 5 rodadas, mediana do total. O jogo ao vivo varia ±4 quadros/s entre rodadas, o replay ±3%. Testado com ele: trocar a trava recursiva do renderizador por uma leve não muda nada (o "sem trava 19% mais rápido" de um experimento era agendamento, não a trava).
  9. Paleta convertida com cache de 4 entradas, compartilhado com o `mappedRead` (que fazia 256 `clutColor` por leitura); CLUT e texturas lidas por linha (`GSMem::ReadRow`), decodificadas em memória comum (ler de volta a área de envio, combinada para escrita, deixou os replays 2–4× mais lentos na primeira tentativa). Replay de referência 2980 → 2840 ms; imagens iguais.
  10. O histórico de depuração do GS (uma entrada por escrita de registrador, GIF tag e primitivo) só enche enquanto o painel de depuração o lê (estático no `gs_frontend.cpp`, sem mexer no `.h` da cadeia do `ps2_runtime.h`).
  11. `rdtsc` por etapa: montar o lote ~130 ciclos por primitivo, `Submit` ~1300. O atalho de estado falhava ~5% das vezes e cada falha (`drawState`) custa ~4 µs (alvos 2–4 mil ciclos, textura 5–7 mil, DATE ~1 mil, resto 2–4 mil): ~24% da thread. 60–70% das falhas eram a `generation`, e quase toda subida dela vinha de cargas de CLUT. Agora a CLUT tem só o `clutSerial`, conferido apenas por desenhos e texturas paletados. Hashes 2x iguais em 8 cenas; replays do novo mais rápidos (raiden 143 → 125 ms, golpe 103 → 91); referência 2840 → 2790 ms. Ao vivo ~40 quadros/s (variação ±3).
  12. Gameplay (164–220 s, roteiro andando e batendo): 2x **49 quadros/s** (mín 42), vsync 60, janela 60; renderizador atual 60. A thread do GS segue no limite (~98%) nos dois trechos; GPU 42–63%. O replay de referência acompanha o jogo ao vivo (23 ms por quadro ≈ 43/s; ao vivo ~40 na cutscene): para 60 ele precisa cair a ~1950 ms (−30%).
  13. Modo com thread (`PS2X_GS_HW_THREAD=1`) no replay: 2,2 a 5,1 s com a mesma entrada; restrito a um CPU por núcleo físico, 3,2 a 5,6 s (sem thread, igual: ~2,95). Depende de threads de hardware extras e de acordar a outra thread na hora; a fila tem uma condição só para os dois sentidos e a `GSHwWorker` segura a trava do renderizador em lotes de até 65 536 comandos. Fica para depois.
  14. Backend de gravação (F9, armado pelo `jogar.sh`): sem gravação aberta e sem troca de página, o `Submit` não pega mais a trava (~1,5 milhão de travas/s, ~4% da thread do GS no jogo do usuário). F9 testado: grava e o replay roda.
  15. Próximo: ver o plano abaixo.
- **Plano para chegar a 60 em 2x (15/11, pedido do usuário: "faz um plano pra fazer acontecer").**
  - **De onde partimos:** a gravação de referência guarda as chamadas ao renderizador (lotes já decodificados), então o replay mede só o lado que desenha: 18,9 ms de mediana por quadro, 20,7 de média (`--repeat 24`). Ao vivo, a mesma thread ainda decodifica os pacotes do GS (~10–15% dela): ~25 ms por quadro, ~40/s. Perfil do replay (processo inteiro, 19 mil amostras): paletas ~16% (`loadClut` 6,3, `paletteFor` 6,0, cópia/comparação da CLUT no `loadClutNow` ~3,3), vértices ~12% (`appendVertices` 9,9 + `memcpy` 2,0), driver ~14% (`flushBatch` ~6,4, `ioctl` 6,1, `beginPass` 1,0), transferências no espelho 5,0% (`WriteRow`), decodificar texturas ~8% (`resolveTextureSlow` 4,3, `ReadRow` 2,9, `PixelAddress` 1,2), blocos pendentes 3,6%. ~20% são do próprio replay (ler 19 MB de lotes de 312 bytes por quadro). Por quadro: ~62 mil primitivos, ~2300 lotes, ~180 cópias de feedback.
  - **Meta:** decodificar e desenhar em threads separadas (o quadro custa o maior dos dois, não a soma) e o lado que desenha ≤ ~15 ms por quadro no replay (folga para o EE e a GPU).
  - **Fase 1 — duas threads de verdade (`PS2X_GS_HW_THREAD=1`):** a fila de hoje pega uma trava por comando, acorda a `GSHwWorker` a cada primeiro comando e usa uma condição só para os dois sentidos; a `GSHwWorker` segura a trava do renderizador em lotes de até 65 536 comandos. Fazer: o produtor junta comandos num bloco local e entrega o bloco inteiro (uma trava e um aviso por bloco); condições separadas para "tem trabalho" e "tem espaço/terminou"; a `GSHwWorker` solta a trava do renderizador entre blocos. **Pronto quando:** replay com thread com variação < 10% em 10 rodadas; ao vivo em 2x, cutscene com thread ≥ 15% acima de sem thread em 3 rodadas; sem falha; hashes iguais.
  - **Fase 1 feita (15/11):** a variação não era a fila: o lote entrava nela por cópia membro a membro, com lixo nos bytes de padding de `GSPrimitiveBatch`, e o atalho de estado do `draw` compara o `GSDrawState` com `memcmp`. O atalho falhava ao acaso (260 mil → 350–660 mil caminhos lentos de textura) e, numa grade de 28 sprites de 32×32 que lê a textura 1024×1024 em `0x2580` quadro sim, quadro não, o caminho certo forçava leituras de volta da GPU (até 200 por replay, cada uma invalidando texturas). Cópia byte a byte: o replay com thread fica determinístico, com os mesmos hashes 2x de sem thread, em 2,0–2,4 s (mediana ~2,0–2,3; sem thread ~2,8). Acordar a `GSHwWorker` em blocos com condições separadas foi testado e ficou pior (mediana 2,35 contra 2,02); desfeito. Ao vivo, cutscene em 2x: com thread 45,1 / 45,9 / 42,2, sem thread 38,8 / 37,8; renderizador atual 50,2 (as médias de 45,1 e 42,2 incluem o fim do carregamento). Gameplay: com thread 48,4 / 51,8, sem thread 46,9. Três rodadas com thread, sem falha. Na gameplay, cinco threads ocupadas: GSFrontend 86%, VU1Thread 84%, GSHwWorker 82%, thread principal (janela) 81%, GameThread 58% (4 núcleos, 8 threads de hardware).
    - Achado de passagem: depois de uma leitura parcial (`partialRead`), o primitivo seguinte com o mesmo estado reaproveita a decodificação pelo atalho, mesmo amostrando outra região. Nessa grade, os blocos 2–28 leem zeros. Os campos finais saem idênticos com e sem a correção, e a correção custa ~4 leituras de volta por quadro (replay 2,9 → 6,3 s): fica anotado no código (`shortcut:` no `draw`).
  - **Fase 1b — CPU livre para as threads do GS:** a thread principal (janela) gasta ~81% de um núcleo na gameplay, e o GSFrontend 86% só decodificando e enfileirando. Perfil das duas (`PS2X_PROFILE_THREAD=ps2EntryRunner` e `GSFrontend`) e corte do que pesa. **Pronto quando:** as duas abaixo de ~50%.
    - **Janela feita (15/11):** 64% da thread principal era a média dos campos byte a byte sobre 4,6 MB por quadro (2x), o resto cópias inteiras da imagem (`s_otherField = s_previous`, `s_previous = s_uploadBuffer`, buffer zerado e copiado linha a linha). Agora a imagem do tamanho da textura sobe direto, a média sai com `_mm_avg_epu8` (mesmo resultado, conferido) e os buffers trocam de dono. Thread principal 81% → 28%. Em 2x com thread: gameplay 48–52 → **54,9 / 55,2** quadros/s; cutscene 42–46 → **52,4 / 51,4 / 53,0 / 52,9** (o renderizador atual fez 50,2 na mesma medida). Capturas da janela normais nos dois renderizadores.
    - Fixar a `GSHwWorker` no núcleo da GSWorker (quase parada neste renderizador): cutscene 53–54 com e sem; desfeito.
    - GSFrontend: os 82% são quase todos espera ativa. Depois de cada pacote ele gira até 100 µs (`kSpinTime` no `gs_async.cpp`) antes de dormir: 54% das amostras no próprio `runFrontend`, contra ~22% de decodificação (`buildDrawBatch` 8%, `writeRegisterPacked` 6%, `vertexKick` 4%, `processGIFPacket` 3%) e ~16% enfileirando (`Submit` com trava por comando). Próximo: giro mais curto quando o renderizador tem thread, e fila sem trava por comando.
  - **Fase 2 feita (15/11):** medido no replay (60 quadros): 151 mil cargas de CLUT (~2500 por quadro), 79% mudando a CLUT, e o `paletteFor` errando nas 119 mil chamadas (o cache era pelo `clutSerial`, que só cresce: voltar a uma CLUT já vista contava como nova). ~500 milhões de ciclos, ~15% do replay. Agora: (1) cada carga é reconhecida pela impressão digital dos blocos de origem no espelho (`hashBytes` dos blocos de 256 bytes da CLUT, depois de o espelho estar em dia) junto com CBP/CPSM/CSM/CSA; 256 cargas guardadas com as entradas já na ordem da CLUT, aplicadas com `memcmp`/`memcpy` quando a faixa é contínua. Acerto de 99,6% (150 762 contra 655), conferido entrada a entrada contra o caminho antigo: 0 diferenças. (2) a paleta convertida é guardada pelo hash do conteúdo da CLUT (64 entradas). Hashes 2x iguais nas 8 cenas, com e sem thread; testes `gs` iguais (33). Replay de referência: sem thread 2,88 → 2,24–2,47 s; **com thread 2,31 → 1,92 s** (abaixo da meta de ~1,95). Ao vivo em 2x com thread: cutscene ~52–53, gameplay 54–55 quadros/s, iguais a antes: a `GSHwWorker` caiu de 88% para 77–79% e o limite passou a ser outro.
    - **O 2x com thread anda junto com o renderizador atual:** na mesma gameplay o atual faz 55,9 (2x: 54,3–55,5) e na cutscene ~50 (2x: ~52–53), com os mesmos vales nos mesmos lugares: cutscene 64–70 s e 78–82 s, ~34–40; gameplay 210–222 s, ~42–50. Esses vales são do EE e da VU1 (N09), não do GS. A N13 fica liberada.
  - **Fase 2 — paletas (~16%):** carregar a CLUT só quando a região dela na VRAM foi escrita desde a última carga com o mesmo CBP/CSA/CPSM (hoje lê e compara 256 entradas a cada carga); a paleta convertida guardada pelo conteúdo (hash calculado uma vez na carga), não pelo `clutSerial`. **Pronto quando:** `loadClut` + `paletteFor` < 4% no perfil; hashes 2x iguais nas 8 cenas.
  - **Fase 3 — vértices e lotes (~12% + driver ~14%):** perfil linha a linha do `appendVertices` (conversão de cada vértice, `memcpy`); menos lotes/passos (por que ~2300 lotes para 62 mil primitivos: ver o que quebra o lote); menos chamadas ao driver por lote (push constants/descritores repetidos). **Pronto quando:** replay de referência ≤ ~2200 ms (`bench.sh`), imagens iguais.
  - **Fase 4 — resto do lado que desenha:** decodificação de texturas (`resolveTextureSlow`, `mappedRead`), `refreshPending`, `WriteRow`. Se a GPU passar a ser o limite a 60/s (hoje 42–63% a 49/s), cópias de sombra e barreiras estreitas (item 3 da N11). **Pronto quando:** replay ≤ ~1800 ms.
  - **Fase 5 — ao vivo e N13:** medir cutscene e gameplay em 2x com thread (métrica alinhada, 3 rodadas); se ≥ 58/s, fazer a N13 (`PS2X_GS_HW=1`, `PS2X_SCALE=2` e a thread como padrão, só com ≥ 4 threads de hardware; o atual atrás de `PS2X_GS_HW=0`).
  - Cada fase: testes `gs` (2 falhas conhecidas desde 15/11; antes 33), hashes R4, `compara_gs.sh`, hashes 2x das 8 cenas (seção 3), commit com `scripts/atualizar_patches.sh`.

### N13 Renderizador novo como padrão, com escala 2x
- **Escolha do usuário (14/11):** "o 2x realmente fica muito melhor"; ele quer a imagem 2x no jogo normal. Não precisa perguntar de novo.
- **Faça:** a antiga T11 (apêndice): `PS2X_GS_HW=1` e `PS2X_SCALE=2` como padrão no `jogar.sh`/runtime quando a placa aguentar 60/s; o renderizador atual fica atrás de `PS2X_GS_HW=0`. README e `jogar.sh` atualizados.
- **Pronto quando:** R5 sem variável nenhuma a 60/s em toda a fase, em 2x. Seção 9: "Jogou com o renderizador novo? Ficou mais nítido? Algum defeito novo?"

### N14 a N19
- **N14:** com o usuário jogando (F9/F10 da N01). Cada defeito vira uma tarefa nova aqui, com a cena gravada. Raios de outras fases: antiga T17.
- **N15:** antigas T16 (dois controles: já feito, falta o usuário testar), T18 (saves) e T19 (filmes do fim no fluxo real).
- **N16–N18:** antigas T26–T29 (apêndice).
- **N19:** antigas T21, T23 e T24 (apêndice).

---

### N20 60 quadros/s fixos e ritmo regular
- **Pedido (15/11):** "70 fps ou no mínimo 60, pra ficar fluido".
- **70 fps não é o alvo, por três motivos:**
  1. A tela do notebook é de 60 Hz (`kscreen-doctor`: 1920×1080 a 60,01 Hz); acima de 60 a janela descartaria quadros.
  2. O MKSM anda um passo de lógica por vsync de 60 Hz. Mais de 60 quadros diferentes por segundo só existem inventando quadros (interpolação: borra o movimento rápido e o HUD e atrasa o controle) ou mudando o relógio da lógica do jogo (arriscado, sem garantia; com `PS2X_UNCAPPED=1` ele simplesmente corre acelerado).
  3. O que dá a sensação de fluido é cada um dos 60 quadros aparecer uma vez, no tempo certo: sem vales e sem quadros repetidos.
  - **Alvo:** 60 fixos com ritmo regular.
- **Onde estamos (15/11, padrão novo, sem variáveis):**
  - **Cutscene** (média 49,6): vales em 64–70 s (35–42), 78–82 s (32–40) e trechos de 53–57 entre 86 e 104 s.
  - **Gameplay** (média 57,7, mín 50): vale em 210–222 s (42–50).
  - Os mesmos vales aparecem no renderizador antigo.
  - **CPU nos vales:** GSFrontend 84–88%, VU1Thread 83–87%, GSHwWorker 81–84%, GameThread 66–71%, janela ~27%. São ~4 núcleos de trabalho num i7-7700HQ de 4 núcleos (8 threads de hardware): as threads irmãs de hyperthreading disputam o mesmo núcleo.
  - Além disso, o quadro é em série: o EE espera a VU1 duas vezes por quadro na função `0x24E680` (N08).
  - **O que falta:** para ir de 32 a 60 no pior trecho, o caminho crítico do quadro precisa ficar ~45% mais curto; os trechos de 50–57, ~10–15%.
- **Faça, nesta ordem (medir antes e depois, três rodadas, `medequeda.sh`/`gprss.sh` da pasta de rascunho ou R5):**
  1. **Medir o caminho crítico de cada quadro nos vales:** por quadro do jogo, tempo do EE rodando, do EE esperando a VU1 (`ps2_vu1_thread::sync`), do EE esperando o GS (`gsAsyncWaitLastVblank`/CSR), da VU1 ocupada e da `GSHwWorker` ocupada. Uma linha `[perf] quadro` a cada 2 s atrás de `PS2X_PERF`. **Pronto quando:** cada vale tem anotado quem está no caminho crítico.
  2. **Devolver CPU desperdiçada:**
     - O GSFrontend gira até 100 µs a cada pacote antes de dormir; na gameplay, 54% do tempo dele era esse giro, que ocupa a thread irmã do mesmo núcleo. Fazer o giro só quando o produtor está ativo, ou com `sched_yield`/`pause` mais espaçado.
     - O produtor pega uma trava da fila da `GSHwWorker` por comando (~5% de cada lado); juntar os comandos num bloco local e entregar o bloco.
     - Procurar giros parecidos na VU1Thread e na GameThread.
     - **Pronto quando:** as threads do GS somadas caem ≥ 0,5 núcleo nos vales.
  3. **VU1 mais rápida (N09, item 2):**
     - Interpretador ~13% da VU1Thread nos vales (`run`/`execLower`): cobrir os blocos que caem nele no JIT.
     - Despacho do `runJitBlock` (~9%): laços de leituras com atraso e de escritas.
     - `processVIF1Data` (~10%): caminho rápido para UNPACK V4-32/V4-16/V4-8 sem máscara e com CL=WL.
     - **Pronto quando:** VU1Thread ≥ 25% mais leve nos vales; testes `vu1`/`vu` iguais; R4 igual.
  4. **EE e VU1 em paralelo:** refazer, atrás de variável, `VIF1_STAT` sem espera + PATH3 na fila da VU1. Em 14/11 deu +3–4 quadros/s antes de a apresentação ser corrigida e +1% depois; com o renderizador novo, a apresentação já não depende do GS em dia. Ver também o que o EE faz depois da espera da `0x24E680`: se for a sincronização de quadro do próprio jogo, o paralelismo para aí.
  5. **`GSHwWorker` mais leve:**
     - Driver ~25% da thread: ~180 cópias de feedback por quadro fecham o passo de render com barreira completa. Reaproveitar cópias, usar barreiras estreitas, juntar passos.
     - `appendVertices` ~10%.
     - Decodificar texturas ~13% (`resolveTextureSlow`, `mappedRead` por texel).
     - `refreshPending` ~6%.
     - **Pronto quando:** replay de referência com thread ≤ 1,5 s (hoje 1,92).
  6. **GameThread (código do jogo recompilado):** perfil nos vales e corte das funções quentes do jogo (animação/esqueleto, física, partículas). Pode precisar de melhorias no recompilador (ponto flutuante, acesso à memória).
  7. **Ritmo dos quadros:** com o jogo a 60, cada imagem nova vai à tela uma vez, no vsync da janela. Medir o histograma dos intervalos entre imagens novas (contador temporário). **Pronto quando:** < 1% fora de 16,7 ± 2 ms nos trechos a 60.
- **Item 1 feito (15/11):** `PS2X_PERF` imprime a cada 2 s uma linha `[perf] quadro` com o tempo de cada parte em ms por quadro do jogo: EE esperando a VU1 (`ps2_vu1_thread::sync`), EE e VU1 esperando o frontend do GS (`waitForHead`), VU1 ocupada, frontend decodificando (sem o giro) e `GSHwWorker` desenhando. Cutscene, padrão novo:

  | trecho | jogo/s | VU1 | GSHwWorker | GS decodificando | EE esperando VU1 |
  |---|---|---|---|---|---|
  | bom (56 s, 96–100 s) | 60 | 9,5–11 | 9–11 | 4,5–5 | 2–4 |
  | médio (72–76 s, 86–94 s) | 54–58 | 14–15 | 14–15 | 6,5–7 | 2–4 |
  | vales (66–68 s, 78–80 s) | 34–37 | **25–27** | **19–23** | 12–14 | 10–14 |

  O EE nunca espera o GS (0,0). Nos vales, a VU1 sozinha passa do orçamento de 16,7 ms, e a `GSHwWorker` também; o EE fica parado esperando a VU1. **A VU1 é o caminho crítico, a `GSHwWorker` vem logo atrás**: o item 3 sobe para antes do item 2.
  - Perfil da VU1Thread em 64–84 s: código do JIT 26%, despacho `runJitBlock` 20%, `processVIF1Data` 10,5%, interpretador ~14% (`run` 6,9, `execLower` 5,7, `execUpper` 1,4), `commitReadyPipelines` ~4%, compilar/preparar a tabela ~5,7%.
  - Contadores do JIT (até 110 s): 218 milhões de chamadas do `runJitBlock`, 89% executadas (~12 pares por bloco). As recusas são todas "bloco sem função" (nenhuma por orçamento, FDIV, EFU ou XGKICK). 32 milhões de pares no interpretador (~1,3% dos pares e ~14% do tempo da VU1). Por motivo: **"perto de leitor de flag" 22,6 milhões (~80%)**, bit E 2,8 milhões, instrução de baixo sem suporte 0,9 milhão. Os 16 pares (`kFlagReaderWindow`) antes de cada FSAND/FSOR/FSEQ/FMAND/FMOR/FMEQ ficam fora do JIT porque o código gerado não calcula as flags MAC/status; o laço quente do MKSM (pc 0x90–0xC0, 0x110…) cai nisso a cada vértice.
  - **Tentado e desfeito (15/11):** compilar esses pares como uma chamada ao interpretador dentro do bloco (`pairCall`: o par inteiro no ciclo certo, com o pipeline de flags exato). Correto (testes `vu1`/`vu` iguais; `PS2X_VU_JIT_CHECK=1` com 7,3 milhões de blocos conferidos e 0 divergências), mas a VU1 nos vales foi de 25–27 para 23–27 ms por quadro, dentro do ruído: o custo está em executar os pares pelas funções do interpretador, não em sair do JIT. Para ganhar aqui, o código gerado precisa calcular ele mesmo as flags MAC/status das operações FMAC e pô-las no pipeline (aí os 16 pares antes de cada leitor viram código nativo). Ganho máximo ~14% da VU1, e o necessário nos vales é ~40%.
  - **Item 5, primeiro corte (15/11):** o `flushBatch` repetia a cada lote (~2300 por quadro) viewport, scissor, constantes de mistura, buffer de vértices, conjuntos de descritores, pipeline e push constants. Agora lembra o que já está ligado no buffer de comandos aberto (`BoundState`, zerado quando um buffer novo começa) e pula as chamadas repetidas. Hashes 2x iguais nas 8 cenas com e sem thread; testes `gs` iguais. Replay de referência com thread ~1,80 → ~1,71–1,76 s (3–5%).
  - **Item 3, despacho da VU1 (15/11):** perfil por linha do `runJitBlock` (com `-g` só no `ps2_vu1_jit.cpp`): ~33% no laço das leituras com atraso, ~14% no das escritas. Cada par que lia o mesmo canal (as linhas de matriz no laço de vértices) acrescentava outra leitura ao bloco, embora só a de menor deslocamento possa causar espera. Agora as repetidas saem na compilação (mesmo resultado: testes `vu1`/`vu` iguais, `PS2X_VU_JIT_CHECK=1` com 6,3 milhões de blocos e 0 divergências, inclusive de ciclos). VU1 por quadro: vales 25,2–26,5 → 24,2–24,4 ms, trechos médios 15,0–15,2 → 14,4–14,5 (−4–5%); quadros/s dentro do ruído.
  - **UNPACK do VIF (15/11):** caminho direto para UNPACK sem máscara, modo 0 e CL ≥ WL (decodifica e grava o vetor inteiro), conferido byte a byte contra o laço geral em 3,1 milhões de UNPACKs (0 diferenças). VU1 por quadro só 1–2% menor (o custo está nos acessos à memória, não na lógica da máscara): desfeito, não paga as ~50 linhas.
  - **`appendVertices` (15/11, perfil por linha no replay com thread):** era o maior custo da `GSHwWorker` (17,4% + 3% de `memcpy`). Calculava o Z de cada vértice de triângulo duas vezes (a primeira descartada) e o do sprite quatro vezes, e montava cada vértice numa variável para depois copiar. Agora cada vértice é escrito uma vez no lugar, com o Z calculado uma vez. Hashes 2x iguais nas 8 cenas com e sem thread; o replay variou mais que o efeito (1,71/1,90 s contra 1,79 antes). Mantido por fazer estritamente menos trabalho com o mesmo tamanho de código.
    - Resto da `GSHwWorker` no replay: `WriteRow` 7,8%, `resolveTextureSlow` 7,3%, `refreshPending` 5,6%, `draw` 4,5% (+5% fora, provavelmente o `memcmp` do estado), `upload` ~4,3% em `memcpy`.
  - **Item 2, giro do GSFrontend (15/11):** cutscene com giro de 0 µs contra 100 µs (padrão): média 54,8 nas duas; nos vales, VU1 26,5 contra 25,5 ms por quadro e `GSHwWorker` 23,6 contra 20,1 (ruído). O giro não rouba CPU de ninguém; fica como está. As threads estão de fato fazendo esse trabalho (não é disputa de núcleo).
  - **Flags da VU1 (15/11, pedido do usuário):**
    - Calcular as flags MAC/status no código gerado pelo JIT não entrou. O interpretador tira as flags do resultado antes de normalizá-lo (underflow e zero exato só se distinguem ali), então seria reescrever a geração das operações FMAC. O verificador do JIT (`PS2X_VU_JIT_CHECK`) não confere flags, e não haveria como validar.
    - **Entrou o equivalente seguro:** para os leitores de flag MAC (FMAND/FMEQ/FMOR), a janela de pares no interpretador vai do leitor até o primeiro par que escreve flag (Add/Sub/Mul/Madd/Msub) a 4 ou mais pares de distância. A escrita leva 4 ciclos, cada par leva pelo menos 1, e essa escrita sobrepõe todas as mais antigas. Antes eram sempre 16 pares. Os leitores de status (FSAND/FSOR/FSEQ) ficam com os 16, porque os bits "pegajosos" vêm de todas as operações anteriores. Laços que voltam para perto do leitor ficam como antes (a janela continua por endereço).
    - Resultado: testes `vu1`/`vu` iguais; cutscene com geometria inteira nas capturas; VU1 por quadro nos vales 24,8–25,3 → 23,6–24,5 ms, nos trechos médios 14,5–15,0 → 14,3–14,7 (−2 a −5%); quadros/s dentro do ruído.
  - **Conclusão do item 3 até aqui:** nos piores vales, a VU1 (preciso por ciclo) e a `GSHwWorker` precisam ficar ~40% e ~30% mais rápidas. Nenhum corte isolado medido chega perto disso. O mais provável é que esses dois trechos não fiquem em 60 cravados nesta máquina sem uma VU1 bem mais rápida (flags no JIT, despacho por bloco mais barato, UNPACK rápido, somados). Seguir pelos itens 2 e 5, que ajudam os trechos de 50–57, e voltar à VU1 com as flags no JIT.
  - Uma vez em ~20 rodadas (cp1, 15/11), o vsync caiu para 12/s de 90 s em diante, com o EE sem esperar VU1 nem GS. Não se repetiu na rodada seguinte; se voltar, perfil da GameThread (`PS2X_PROFILE_THREAD=GameThread`) no trecho.
- **15/11, depois do usuário jogar a pasta portátil ("dá umas lagadas"; "mudei o gráfico e não ficou mais rápido"):**
    - **Item 7 feito:** a janela mostrava 55–58 imagens novas com o jogo a 60 (pegava sempre a cópia mais nova; um campo atrasado aparecia duas vezes e o seguinte sumia). Agora mostra em ordem, uma por volta, pulando só com mais de duas esperando: 60/60 nos trechos a 60 (R5 sem capturas).
    - **Limite de 30 quadros/s testado e descartado:** com o bit FIELD virando a cada 2 vblanks o jogo vai a 30 cravados (pior intervalo 34–35 ms, sem tranco), mas a lógica anda por quadro: a cutscene levou ~48 s contra ~27 s. Câmera lenta, não serve.
    - **Medida na gameplay (R5 até 310 s):** VU1 13–14 ms por quadro de 16,7 (vales da cutscene 24–28). É a VU1 que segura o ritmo nesta máquina; o gráfico mais leve só alivia a GPU. Novo: `PS2X_PERF` mostra o pior intervalo entre quadros a cada 2 s.
    - **Próximo:** medir onde o usuário está (save dele em `portatil/backup_save_mc0_15nov`), que é mais adiante e mais pesado que o Goro's Lair, e voltar ao item 3 (VU1).
- **15/11, noite (usuário: "pode fazer até finalizar a N20"):**
    - **Medida comum:** script de R5 por 120 s, sem toques depois da fase carregar (cutscene inteira), com resumo por trecho. Antes / depois do corte abaixo:

      | trecho | fps méd (mín) | VU1 ms/quadro (máx) | GSHwWorker | EE esperando VU1 |
      |---|---|---|---|---|
      | cutscene, antes | 50,6 (25,5) | 15,8 (25,9) | 16,4 | 6,4 |
      | cutscene, depois | 51,6 (21,0) | 15,3 (25,2) | 16,8 | 5,8 |
      | gameplay, antes | 59,6 (57,5) | 12,3 (14,9) | 10,5 | 5,1 |
      | gameplay, depois | 59,8 (58,5) | 11,9 (13,1) | 10,3 | 4,9 |

    - **Perfil da VU1Thread nos vales (64–84 s):** código do JIT 26%, despacho `runJitBlock` 20,6%, `processVIF1Data` 12,6%, interpretador ~12,5% (`execLower` 6,1, `run` 5,0, `execUpper` 1,4), pipelines/ciclos ~6,5%, callouts do JIT 3,1%, `prepareJitTable` 1,8% (hash do código quando o jogo reenvia o programa; as tabelas já ficam por conteúdo), envio ao GS (XGKICK, GIF) ~6%.
    - **Despacho medido:** 36 leituras por bloco, ~9 conferidas por execução (horizonte médio de 2 ciclos), e só 0,2% dos blocos esperam de fato; ~1,5 milhão de blocos/s. **Feito:** leituras e escritas do bloco viram ponteiros diretos para os contadores de pronto (`ReadyRef`), sem escolher a tabela VF/VI/ACC a cada entrada: VU1 −3% (tabela acima). `PS2X_VU_JIT_CHECK=1`: 7,3 milhões de blocos, 0 diferenças; testes `vu1`/`vu` 87/0.
    - **Código do JIT:** 200–290 bytes de x86 por par; os 4 blocos mais quentes (laço de vértices, pc 0x0d98, 0x1120, 0x0a18, 0x0318) somam ~16% da VU1Thread. Cada FMAC carrega e limita os operandos, confere as lanes de expoente extremo (`emitExactFmacFixup`) e grava com mistura por lane: é o preço da exatidão bit a bit.
    - **Perfil da GameThread nos vales:** IOP ~14% (JIT e laço do IOP rodam na mesma thread), VU0 ~10% (boa parte no interpretador: o fim de todo programa da VU0 conta como leitor de flag, e **o jogo lê mesmo** MAC/CLIP com `CFC2 $vi17/$vi18` em 44 funções, então a janela fica), `writeIORegister` + `unordered_map` dos registradores ~7%, salto/`lookupFunction` ~5%, funções do jogo espalhadas (0,5–2% cada: `-O2` no código gerado renderia pouco). Os ~4% do escalonador "fora do executável" são a espera pelo horário real do próximo vblank (folga, não custo).
    - **Conclusão desta rodada:** nos vales, **três** partes passam de 16,7 ms ao mesmo tempo: VU1 (~25), `GSHwWorker` (16–20) e o EE (~17 de trabalho próprio, fora a espera). Nenhum corte isolado chega aos 30–40% que cada uma precisaria. Na gameplay da primeira sala o jogo já fica em 59,6–59,8 de média (mín 57,5–58,5), com as imagens iguais aos quadros (item 7) e atraso de 20–25 ms (N19).
    - **Próximos cortes, em ordem de ganho por esforço:**
      1. **Numa madrugada (recompila os 13 mil arquivos, ~2 h):** tabela simples no lugar do `unordered_map` dos registradores de I/O (`ps2_memory.h`), e os outros cortes que mexem em headers do `ps2_runtime.h`. EE −3 a −5%.
      2. **JIT da VU1, geração de código (várias sessões):** manter os VF de um bloco em registradores XMM e conferir as lanes extremas uma vez por resultado. Até ~10% da VU1.
      3. **Flags MAC/status no JIT:** os pares perto de leitores de flag deixam o interpretador. Até ~12% da VU1.
      4. **IOP numa thread própria:** −14% do EE, mas mexe no tempo do som (ver as armadilhas do IOP no ARQUITETURA); só com medida de áudio antes/depois.
      5. **Medir onde o usuário está** (save em `portatil/backup_save_mc0_15nov`): áreas mais pesadas que o Goro's Lair podem ter outro gargalo.
- **15/11, madrugada (depois da sessão medida do usuário):**
    - **Sessão do usuário (pasta portátil, `PS2X_PERF` + F8):** na luta pesada do fim, 50 s seguidos a 33–43 quadros/s (lentidão de verdade, o jogo conta o tempo em quadros) com **VU1 19–26 ms** por quadro, `GSHwWorker` 14–16, EE esperando a VU1 7–14. Perfis do trecho iguais aos dos vales da cutscene: é a VU1.
    - **Cortes desta rodada (todos conferidos com `PS2X_VU_JIT_CHECK=1`: 7,3 M blocos da VU1 e 3,1 M da VU0, 0 diferenças, inclusive de ciclos; testes 460/3):**
      - **MPG idêntico não muda o código:** o MKSM reenvia os mesmos programas várias vezes por quadro; agora o VIF compara antes de copiar e só então incrementa a geração do código. O JIT e o cache decodificado deixam de refazer o hash de 16 KB (`prepareJitTable` era 2,5% da VU1 na luta).
      - **Geração do código conferida no despacho sem chamar `prepareJitTable`** a cada bloco.
      - **JIT:** a constante do teste de resultado exato fica em `xmm8` (era `movabs` + `movups` em cada FMAC), e os dois operandos de um FMAC são conferidos juntos (um desvio em vez de dois). O montador ganhou REX para `xmm8–15`.
      - **VU0, fim de programa:** o jogo lê MAC (`$vi17`) e CLIP (`$vi18`) depois da VU0, nunca STATUS. A janela no interpretador agora vai só do último par que escreve MAC até o fim (era 16 pares), quando nenhum desvio fica no trecho, nenhum desvio relativo cai dentro dele e não há JR/JALR no código. Pares da VU0 no interpretador: 22 M → 4,7 M na R5 (−79%).
      - **WAITQ abre bloco:** o par com WAITQ (divisão de perspectiva) ia sempre ao interpretador; agora pode ser o primeiro par de um bloco, e o despacho espera o FDIV antes de entrar (o mesmo atraso do interpretador).
    - **Resultado (R5, duas rodadas):** cutscene 50,6 → 54,2–54,4 de média, mínimo 25,5 → 37,5–39, VU1 15,8 → 14,6–15,0 ms (pico 25,9 → 23,1–24,1); gameplay 59,6 → 59,9–60,0 (mín 57,5 → 59,0–59,5), VU1 12,3 → 11,4–11,5.
    - **DIV no código do JIT:** 73% das chamadas de volta dos blocos eram DIV (divisão de perspectiva, ~1,25 M/s; 22% FCAND, o resto XTOP/MFP/FCSET/EFU). Com o FDIV vazio (o caso comum depois do WAITQ), o bloco faz a DIV e o `queueQ` no lugar; com resultado pendente, segue pela chamada de volta. O verificador do JIT agora compara também o FDIV pendente (valor, ciclo, flags D/I): 8,4 M blocos, 0 diferenças. Cutscene 54,2–54,4 → 55,1–55,2 de média, VU1 14,6–15,0 → 14,1–14,3 ms.
    - **Recompilação completa (16/11, ~1h50 com `-j6`):** os registradores de I/O saem do `unordered_map` para uma tabela simples da página de I/O do EE (`IoRegisterFile` em `ps2_memory.h`, no heap) e `microAddressMask` fica inline (`ps2_vu1.h`). VU1 na cutscene 14,1–14,3 → 13,7–13,9 ms, na gameplay 11,4 → 10,9–11,0; EE esperando a VU1 6,1–6,4 → 5,6–5,7. Testes 460/3.
    - **Somando a noite inteira (R5, duas rodadas):** cutscene 50,6 → 55,0–55,2 de média, pior trecho 25,5 → 39; VU1 15,8 → 13,7–13,9 ms (−12%); gameplay 59,6 → 59,3–60,0, VU1 12,3 → 10,9–11,0 (−11%); VU0 no interpretador −79%.
    - **O que sobra no interpretador da VU1:** pares de fim de programa (bit E e o par seguinte), XGKICK isolado (bloco de 1 par fica abaixo de `kMinBlockPairs`) e pares em slot de atraso depois de desvio interpretado.
- **Pronto quando:** R5 sem variáveis, três rodadas: cutscene e gameplay sem intervalo de 2 s abaixo de 58 quadros/s, e "imagens novas" igual a "jogo".
- **Estimativa honesta:**
  - Os trechos de 50–57 devem chegar a 60 com os itens 2 e 5.
  - Os dois piores vales (32–42) precisam dos itens 3 e 4 (talvez do 6) e podem não chegar a 60 cravados neste processador. Medir depois de cada item e anotar aqui.

### N21 Opção "Gráfico" para PC fraco
- **Pedido (15/11):** uma opção "Gráfico" no menu de opções do jogo (no menu inicial e na pausa, pelo Select) para baixar os gráficos e rodar bem em PC fraco. "Veja se é possível primeiro."
- **Dentro do menu do próprio jogo:** possível em tese, mas caro e arriscado.
  - Os menus são animações Flash da Paradox (`AnimMenuGUI`/`AnimMenuMan`) carregadas do WAD, e os textos vêm por ID/CRC (MODDING.md §7).
  - Um item novo exige editar o layout e a navegação dentro dos arquivos do WAD (ainda sem empacotador; há o `WADCRC.BIN`) e entender o código que trata cada item.
- **Recomendado:** menu "Gráfico" do próprio runtime, em sobreposição, aberto por um botão quando o menu de opções ou a pausa do jogo está na tela. Quando o menu de opções está aberto, mostrar uma dica "R3: Gráfico".
  - O runtime já tem ImGui (o "Runtime Debugger").
  - A pausa pode ser detectada por `gMenuSystem` `0x5D1DE0` / `ActiveWindowId` `0x5D2728`.
  - Enquanto o menu estiver aberto, o controle não vai para o jogo.
  - Grava no `mksm.ini`.
- **Opções e quando valem:**

  | Opção | Valores | Quando vale |
  |---|---|---|
  | Resolução interna | 1x / 2x / 3x | na hora: `downloadAll` + `InvalidateAll` + nova `scale`; a confirmar |
  | Suavização FXAA | sim / não | na hora (hoje `static const` no `ps2_runtime.cpp`, vira variável) |
  | Filtro de ampliação | bicúbico / bilinear | na hora (idem) |
  | Mistura de campos | sim / não | na hora (idem) |
  | Renderizador | novo / antigo | ao reiniciar |
  | Thread do desenho | sim / não | ao reiniciar |

  - Predefinições: "Alto" (2x, FXAA, bicúbico) e "Leve" (1x, sem FXAA, bilinear).
- **Feito (15/11), no formato recomendado (o usuário escolheu):**
  - **Quando abre:** R3 abre o menu "Gráfico" quando o jogo não está em gameplay; F1 abre a qualquer hora. O painel de depuração passou do F1 para o F12.
  - **Detecção do estado do jogo:** comparando a RAM jogando, no mapa (Start), na pausa (Select) e no menu inicial. O `0x5D2724`/`0x5D2728` do MODDING.md ficou sempre em 0.

    | endereço | vale 1 em |
    |---|---|
    | `0x4F4B68` | gameplay e cutscenes (0 no menu inicial, carregamento, mapa e pausa; 0x3C no boot) |
    | `0x6BEE2C` | pausa do Select ("Continue / Options / Exit Game") |
    | `0x4C49E8` | qualquer menu dentro do jogo (mapa e pausa) |

  - **A dica "R3: Gráfico"** aparece na pausa e nas telas do menu inicial, nunca durante um filme (`g_moviePlaying`, 15/11: aparecia nos filmes da abertura e da história, que rodam com `0x4F4B68` = 0). O painel de depuração (F12) começa fechado.
  - **Controle:** enquanto o menu está aberto, e até soltar os botões que o fecharam, o jogo não recebe nada (`g_gfxBlockInput` no `ps2_pad.cpp`).
  - **Opções e gravação:** Predefinição Alto/Leve, Resolução 1x–4x (na hora: `GSHwBackend::SetScale`), FXAA, filtro, mistura de campos (na hora), renderizador e thread (ao reiniciar, marcados com *). Cada mudança grava as chaves no `MKSM_INI` (o `jogar.sh` exporta `$raiz/mksm.ini`), mantendo as outras linhas.
  - **Restaurar recomendado (15/11, pedido do usuário):** linha no fim do menu que volta tudo aos padrões de uma instalação nova (2x, FXAA, bicúbico, mistura de campos, 4:3, renderizador novo, thread com 4+ núcleos lógicos), só com X/Enter. Testado com `MKSM_GFX_SCRIPT` a partir de um `mksm.ini` com 1x, sem FXAA, bilinear e 16:9: tudo voltou e foi gravado.
  - **Predefinição Mínimo e "Efeitos de luz" (15/11, pedido do usuário, para PCs com placa fraca):** Mínimo = 1x, sem FXAA, bilinear, sem mistura de campos e efeitos de luz simples (o brilho sem as ~760 passadas de desfoque, com a soma final compensada para a mesma luz; ver ARQUITETURA). Replay raiden: 1x 1737–1761 → 1459–1554 ms (−12 a −16%), 2x 1952–2038 → 1787–1811 ms (−8 a −11%). Luz média das cenas 52,7 → 50,2 (hud), 34,3 → 32,1 (raiden); menus, título, pausa e seleção iguais bit a bit. Com os efeitos completos (padrão), nada muda.
  - **Teste sem controle:** `MKSM_GFX_SCRIPT="190o,193d,194l,198c"` (abre, desce, esquerda, fecha). Testado na pausa: menu e dica certos (acentos inclusive), 2x → 1x ao vivo, `mksm.ini` atualizado, jogo seguiu.
  - **Achado no caminho:** com a thread ligada, `InvalidateAll` chamado com a trava do renderizador pega travava tudo (ele espera a fila, e a `GSHwWorker` precisa da trava para esvaziá-la). Era o caso do `SetScale`, e também de `Initialize`/`Reset`, que só rodam no boot. Agora usam `invalidateAllLocked`.
- **Limite honesto:** essas opções aliviam a placa de vídeo (1x desenha ~4× menos pixels que 2x). Num PC com processador fraco, o peso maior é emular o EE e a VU1 (N20), e isso não muda com gráfico mais baixo.

### N22 60 quadros/s, fase 2: Windows e os vales que sobraram
- **Pedido (17/11):** "vê se tem algum jeito de resolver o 2 dos 60fps, monte um plano". O "2" era: os vales da cutscene do Raiden, o engasgo leve que o usuário sente no Windows e a ideia de passar de 60.
- **Onde estamos (fim da N20):**
  - Cutscene: 55 de média, pior trecho de 2 s a ~39. Gameplay da primeira sala: 59,3–60.
  - Luta pesada do save do usuário: 33–43 por 50 s, com a VU1 em 19–26 ms por quadro.
  - Nos vales, três partes passam de 16,7 ms ao mesmo tempo: VU1 (~24 ms), `GSHwWorker` (16–20) e o EE (~17 de trabalho próprio).
  - Para o pior vale chegar a 60, a VU1 precisa ficar ~35–40% mais rápida, e as outras duas ~15–30%.
- **Passar de 60 não é alvo** (mesmos motivos da N20: a lógica do jogo anda um passo por vsync de 60 Hz). Em monitor de 120/144 Hz, o que ajuda é VRR (G-Sync/FreeSync) ou o monitor a 60/120 Hz.
- **Achado de 17/11 (lendo o código):**
  - A janela apresenta por um relógio próprio de 60 Hz (`SetTargetFPS(60)`), sem vsync (sem `FLAG_VSYNC_HINT`).
  - O raylib já liga o relógio de 1 ms no Windows (`timeBeginPeriod(1)`, `SUPPORT_WINMM_HIGHRES_TIMER`).
  - A fixação das threads pesadas em núcleos físicos (`ps2_perf::pinHeavyThread`) só existe no Linux: no Windows, duas delas podem cair no mesmo núcleo (no Linux isso custava ~5 quadros/s).

**Fase A: engasgo no Windows** (independe da VU1; rápida)
- **A1 Medir no Windows.**
  - **Faça:** a pasta portátil ganha um `Jogar-medindo.bat` (`PS2X_PERF=1`, saída em `medida.txt`). O usuário joga uns 5 minutos onde engasga e manda o arquivo.
  - **Pronto quando:** dá para dizer se o engasgo vem do jogo abaixo de 60 (VU1/EE, como no Linux) ou do jogo a 60 com imagens em ritmo irregular (apresentação).
- **A2 Threads fixas também no Windows.**
  - **Faça:** `pinHeavyThread` com `GetLogicalProcessorInformationEx` (um processador lógico por núcleo físico) e `SetThreadAffinityMask`; `PS2X_PIN=0` desliga, igual ao Linux.
  - **Pronto quando:** medida do usuário antes e depois.
- **A3 Sincronia vertical opcional.**
  - **Por quê:** sem vsync, o relógio de 60 Hz da janela e o do monitor (o DWM compõe a 60,0x Hz) se cruzam aos poucos, e de tempos em tempos uma imagem fica dois refreshes e a seguinte some. É o "engasgo" com o jogo a 60.
  - **Faça:** `PS2X_VSYNC` e a linha "Sincronia vertical" no menu Gráfico. Com ela ligada, a janela troca de imagem junto com o monitor. Em monitor de 144 Hz, o laço da janela passa a girar a 144: conferir que a fila em ordem do item 7 da N20 continua mostrando cada quadro do jogo uma vez.
  - **Pronto quando:** histograma dos intervalos entre imagens novas com < 1% fora de 16,7 ± 2 ms no Windows, com o jogo a 60.
- **A4 Energia (sem código).**
  - **Faça:** o LEIA-ME do Windows recomenda o plano de energia "Alto desempenho" e o carregador ligado (no "Equilibrado", o notebook baixa o clock).

**Fase B: os vales** (cada item medido antes e depois com a R5, três rodadas; hashes da R4 iguais)
- **B1 PGO do runtime.**
  - **Faça:** compilar só o runtime (não os 13 mil arquivos gerados) com `-fprofile-generate`, rodar a cutscene (R5) e o replay de referência, e recompilar com `-fprofile-use`, mais LTO no runtime. O despacho do JIT da VU1, o VIF, o GS e o IOP são o tipo de código que mais ganha com PGO (5–15% é comum). O resultado é o mesmo; muda só a ordem e o encaixe do código de máquina.
  - **Atenção:** no Windows o perfil precisa sair do mesmo compilador (MinGW). Fazer primeiro no Linux.
  - **Pronto quando:** VU1 e `GSHwWorker` por quadro anotados antes e depois.
- **B2 JIT da VU1: VF em registradores** (N20, próximo corte 2).
  - **Faça:** os VF usados num bloco ficam em registradores XMM do começo ao fim do bloco, e as lanes de expoente extremo são conferidas uma vez por resultado.
  - **Pronto quando:** até ~10% da VU1; `PS2X_VU_JIT_CHECK=1` sem diferenças.
- **B3 Flags MAC/status no JIT** (N20, corte 3).
  - **Faça:** antes, estender o `PS2X_VU_JIT_CHECK` para comparar as flags; depois, o JIT calcula as flags das operações FMAC, e os pares perto de leitores de flag deixam o interpretador.
  - **Pronto quando:** até ~12% da VU1, verificador sem diferenças.
- **B4 IOP numa thread própria** (N20, corte 4).
  - **Por quê:** o IOP é ~14% da GameThread.
  - **Faça:** medir o som antes e depois (estalos, ritmo do ADX) e seguir as armadilhas do IOP no `ARQUITETURA.md` (interrupções suspensas, DMA).
- **B5 "VU1 rápida", opcional e desligada por padrão.**
  - **Por quê:** cada par da VU1 vira 200–290 bytes de x86 por causa das correções de exatidão por lane (`emitExactFmacFixup`, limites dos operandos).
  - **Faça:** no menu Gráfico, uma opção que pula essas correções, como os modos de "clamping" leves de outros emuladores. Medir o ganho e a diferença de imagem (PSNR nos replays e capturas da cutscene).
  - **Pronto quando:** só fica se ganhar ≥ 15% da VU1 sem defeito visível; se não, desfazer e anotar.
- **B6 `GSHwWorker`: cópias de feedback.**
  - **Por quê:** ~180 por quadro, cada uma fechando o passo de render com barreira completa (o driver é ~25% da thread).
  - **Faça:** reaproveitar cópias da mesma região e usar barreiras estreitas.
  - **Pronto quando:** replay de referência com thread ≤ 1,5 s.
- **B7 (pesquisa, só se B1–B6 não bastarem) VU1 em paralelo.**
  - **Ideia:** dois executores da VU1 em lotes independentes (um `MSCAL` que só lê o que os próprios UNPACKs escreveram e as constantes), com a saída do GIF na ordem original.
  - **Risco:** ganho potencial grande; complexidade alta (detectar a dependência entre lotes).
- **Ordem:** A1, A2, A3 e A4 (Windows; A1 depende do usuário), B1, medir, B2 e B3, B4, B5, B6, B7.
- **18/11, medida do Windows (A1) analisada** (`medida.txt` do usuário, .exe de 17/11 17:31, sem as correções do `a633afd`; mesmo notebook, i7-7700HQ + GTX 1050):
  - Jogo rodando: média de 39,5 quadros/s, abaixo de 55 em 150 de 167 trechos. A GPU fica só 35–39% ocupada: o limite é a CPU.
  - Mesma área e mesmo save no Linux (teste de 18/11): 52,2 quadros/s. Por quadro, Windows contra Linux: VU1 18,4 contra 13,9 ms (+32%), GS decodificando 9,5 contra 8,2 (+16%), `GSHwWorker` 20,6 contra 13,3 (+55%), EE esperando a VU1 10,0 contra 6,9.
  - O arrasto de 2–3 quadros/s em t=518–523 tem a forma do arrasto dos muros da Wu Shi (`FRAME` com formato indefinido), corrigido no `a633afd`, que esse .exe não tinha.
  - Parte da diferença do `GSHwWorker` é essa versão; o resto (VU1 +32%) é do build (`-msse4.1` sem `-march=native`, MinGW) ou do Windows (plano de energia, A4).
  - .exe novo (com PGO, a correção do muro e a janela sem congelar) no Wine, cena do Goro's Lair: 34,4 quadros/s contra 34,9 do .exe antigo (igual, uma rodada cada).
  - **Próximo:** o usuário mede de novo com o .exe novo na mesma área. Se a VU1 continuar ~30% acima do Linux, testar um .exe com `-mavx2` (o i7-7700HQ tem), conferindo antes que o pacote não vai para PCs sem AVX2.
- **17/11, feito (A1–A4, e o padrão do Linux pedido pelo usuário):**
  - **Padrão do Linux:** o usuário trocou na pasta portátil o FXAA e a mistura de campos para desligados e disse que os quadros "ficaram perfeitos". Agora é o padrão no Linux (`kRecFxaa`/`kRecBlend` no `ps2_runtime.cpp`, também no "Restaurar recomendado" e na predefinição "Alto"); no Windows, como antes.
  - **A1:** `Jogar-medindo.bat` (Windows) e `jogar-medindo.sh` (Linux) nas pastas portáteis, gravando `medida.txt`.
  - **A2:** `pinHeavyThread` no Windows (`GetLogicalProcessorInformationEx`, núcleos de desempenho primeiro, `SetThreadAffinityMask`; `PS2X_PIN=0` desliga). Compilado com MinGW; o efeito só se mede no Windows do usuário.
  - **A3:** "Sincronia vertical" no menu Gráfico (`PS2X_VSYNC`, desligada por padrão): `FLAG_VSYNC_HINT` e teto de 240/s na janela.
  - **A4:** o LEIA-ME do Windows recomenda o plano "Alto desempenho" e o carregador ligado.
  - **Medida (R5 longa, uma rodada cada, Linux, i7-7700HQ + GTX 1050):**

    | configuração | cutscene: jogo méd (mín) | imagens novas | pior intervalo | trechos de 2 s com intervalo > 20 ms | gameplay: jogo méd (mín) |
    |---|---|---|---|---|---|
    | padrão antigo (FXAA e mistura ligados) | 56,0 (36) | 55,3 | 61 ms | 44/45 | 58,7 (50,5) |
    | padrão novo do Linux | 57,4 (41) | 57,0 | 62 ms | 35/45 | 59,4 (56) |
    | novo + sincronia vertical | 58,0 (40) | 57,9 | 33 ms | 16/45 | 59,2 (53) |

  - **B1 (PGO) feito:** `scripts/pgo_runtime.sh` (opção `PS2X_PGO` no CMake do runtime): compila `ps2_runtime` e `ps2_iop` com contadores, treina com a cutscene inteira mais uma luta depois da esfera (cópia do jogo com o memory card copiado) e recompila com o perfil. Precisou de `-fprofile-correction` (o perfil é gravado com as outras threads rodando, e alguns contadores ficam desencontrados por 1–2) e `-Wno-coverage-mismatch` (o gatilho `PS2X_PGO_DUMP_AT` só existe no build de treino). R4b: os 16 hashes iguais; testes 460/3. Medida (padrão novo do Linux, sem vsync):

    | | cutscene: jogo méd (mín) | VU1 ms/quadro | GSHwWorker | pior intervalo | trechos > 20 ms | gameplay: jogo (mín) | VU1 | GSHwWorker |
    |---|---|---|---|---|---|---|---|---|
    | sem PGO | 57,4 (41) | 12,9 | 12,0 | 62 ms | 35/45 | 59,4 (56) | 11,6 | 10,5 |
    | PGO, 1ª | 58,0 (45) | 11,5 | 10,7 | 32 ms | 19/45 | 59,5 (57) | 10,4 | 9,6 |
    | PGO, 2ª | 57,6 (42) | 11,7 | 10,8 | 32 ms | 21/45 | 59,1 (55) | 10,6 | 9,6 |

    VU1 e `GSHwWorker` ~10% mais leves, com a mesma imagem. O `out/build` fica em `PS2X_PGO=usar`; depois de mudanças grandes no runtime, rodar o script de novo (funções que mudaram compilam sem perfil).
  - **PGO do Windows (17/11, feito):** `scripts/pgo_windows.sh` treina o `.exe` do MinGW no Wine. A `libgcov` do `mingw-w64-gcc` do Arch é reduzida: registra os contadores mas não grava arquivo (só `__gcov_merge_add`, `__gcov_dump`, `__gcov_init`, `__gcov_info_to_gcda`...). O build de treino do Windows tem um `__gcov_init` próprio que guarda o `gcov_info` de cada arquivo e grava cada perfil pelo `__gcov_info_to_gcda` (a API do GCC para quem grava sozinho), com o `fopen` do runtime; cada treino vai para uma pasta (`PS2X_PGO_OUT`) e o `gcov-tool merge` junta. Só a contagem de caminhos (`-fno-profile-values -fno-profile-reorder-functions`; a biblioteca reduzida não tem o perfil de valores). Tentado e descartado: a `libgcov` do WinLibs (completa, mas UCRT; o `.exe` MSVCRT de treino quebrava na abertura). Medida no Wine (R5 longa): cutscene 41,2 → 42,8 quadros, gameplay 46,1 → 48,1; VU1 19,9 → 19,5 ms, `GSHwWorker` 16,1 → 15,5. Sem Vulkan continua abrindo. O zip do release compila sem PGO (levar o perfil exige padronizar os caminhos dos arquivos de perfil; fica para a v1.2).
  - **Leitura:** o padrão novo ganha 1–1,5 quadro/s e sobe o mínimo; a sincronia vertical quase divide por dois o pior intervalo e os trechos com engasgo. Causa provável: sem vsync, o `SetTargetFPS(60)` do raylib espera a parte final de cada quadro girando (`SUPPORT_PARTIALBUSY_WAIT_LOOP`), e esse giro tira um núcleo das threads pesadas; com vsync a janela dorme na troca do buffer. **Próximo:** se a medida do Windows confirmar, ligar a sincronia vertical por padrão (ou trocar a espera da janela por sono sem giro).
- **Pronto quando:**
  - Windows do usuário sem engasgo com o jogo a 60.
  - R5 com a cutscene sem trecho de 2 s abaixo de 58.
  - A luta pesada do save do usuário com ≥ 55.
- **Estimativa honesta:**
  - A2–A4: uma sessão. B1: meio dia.
  - B2 e B3: várias sessões, juntas até ~20% da VU1. B4: uma sessão e medida de som.
  - O pior vale a 60 cravados provavelmente só com B1–B4 somados (talvez B5 ou B7). Os trechos de 50–57 devem chegar antes.

## 8. Onde estamos

**17/11:** N22 aberta (60 quadros/s, fase 2: Windows e os vales que sobraram; plano na seção 7). Fora do desempenho: modo RPG, 67 lutadores e o nome ShaolinMonks RPG (`ESTADO.md`).

**15/11 (resumo; a tabela abaixo é de 14/11):**
- 2x com thread é o padrão (N13), com mipmap por pixel (N04).
- Menu Gráfico com 16:9 (N21, N19).
- Velocidade certa em tudo: gameplay ~55–60 quadros/s; dois vales da cutscene a ~35, limite da VU1 e do EE (N20).
- Testes 460/3 (R3).
- Intel HD 630 e sem Vulkan testados (N18).
- Em aberto:
  - para mim: N20 (60 cravados nos vales: VU1 bem mais rápida), N19 (controle configurável e latência), N18 (build do zero pelo README), modo RPG (`PLANO_RPG.md`);
  - para o usuário: N14 (jogar adiante com F9/F10), N15 (saves, filmes do fim, dois controles), testar o menu Gráfico e o 16:9 com controle de verdade.

**14/11, fim do dia:**

| Área | Estado | Tarefas |
|---|---|---|
| Abertura e filmes | Os 8 `.SFD` tocam; som direto do arquivo, sem chiado (usuário confirmou) | N15 |
| Menus | 60/s; sem tremida (usuário confirmou); todas as opções abrem e voltam | — |
| Goro's Lair, velocidade | Sem câmera lenta: vsync 60 na cutscene e em combate; o jogo pula quadros nos trechos pesados (34–38 quadros/s em dois momentos da cutscene, ~52 em combate); uma hora seguida sem travar, memória estável em ~430 MB | N09 |
| Goro's Lair, imagem | **Chão, paredes e raios corrigidos** (VIF na frente da VU1); usuário aprovou os prints | N03b, N05–N07 |
| Liu Kang | Mortal parado corrigido (instrução MMI `PABSW` lia o registrador errado) | N04b |
| Nitidez | Renderizador novo em 2x por padrão (15/11), tão rápido quanto o antigo; janela com ampliação bicúbica | N04, N19 |
| Som | Bom (usuário); volumes no `mksm.ini`; eco 22 aprovado | — |
| Controle | Dois controles (falta testar); vibração por porta | N15 |
| Saves | Carregar funciona; autosave no meio da fase não testado | N15 |
| Resto do jogo | Não jogado além da primeira sala | N14 |
| Robustez | Árvore limpa (commit fixado + patches) compila e passa nos testes; uma trava no boot em 10 boots (com outra carga na máquina), não repetida em 9 boots seguidos | N16, N17 |

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

- (N22, 17/11) No Windows: quando o `Jogar-medindo.bat` existir, jogar uns 5 minutos onde engasga e mandar o `medida.txt`. Dizer também o processador e a placa de vídeo desse PC e a taxa do monitor (60, 120 ou 144 Hz).
- (N20, 15/11) Resposta ao pedido de 70 fps: a tela é de 60 Hz e o jogo anda um passo por vsync de 60 Hz; o alvo é 60 fixos com ritmo regular (plano na N20).
- Respostas de 14/11 (2ª rodada): engasgo principalmente quando o Raiden chega e quando ele para de falar (sem fala); luzes estranhas sempre nos mesmos lugares.
- **Teste com `PS2X_RAYS=0` (14/11, 3ª rodada):** o raio arco-íris e os degraus **continuam todos**, nos mesmos lugares; as luzes ficam "quadradas e sólidas, muito feio", como antes; o chão continua quadriculado. **Conclusão: não é a troca de blend do `softenLightRay`**, é a forma como os raios são desenhados (textura/mipmap/filtro). Medido na mesma sessão: duas quedas de ~6 s a 31–36/s na cutscene (chegada do Raiden e fim da fala), gameplay 52–60/s.
- Respostas de 14/11: filmes sem chiado; cutscene do Raiden sem chiado ("suave"); som e eco bons; tremida do menu resolvida; FXAA não resolve a qualidade baixa; dois controles ainda não testados; não jogou além da primeira sala; ainda sente engasgo na cutscene; chão quadriculado e luzes estranhas "faz tempo".
- (N01, quando pronta) Ao ver um defeito, aperte F9 (grava a cena) e F10 (captura) e diga o nome do arquivo.
- (N03b) Pare no lugar do print 2 (chão quadriculado perto do fogo) e aperte F9 e F10; diga os nomes dos arquivos em `gsrec/` e `capturas/`.
- (N04b, respondido 14/11) Gira toda vez que para, em qualquer lugar.
- (N03b, respondido 14/11) Os quadrados existem desde sempre, inclusive na cutscene do Raiden; o usuário acha que falta suavização/textura/upscale. Depois de resolver, seguir o plano inteiro.
- (14/11, respondido: "tá bom, mas dá pra ver os quadradinhos em volta do personagem"; seleção de personagem com qualidade baixa) Agora há um FXAA completo antes da ampliação. Os quadradinhos no cabelo e na pele vêm da resolução do PS2 (640×224 por campo) e só somem com a resolução 2x do renderizador novo, que ainda é lento: na cutscene, 28 quadros/s em 1x e 16 em 2x, contra 51 no atual (N10–N12). Ficou melhor com o FXAA? (Respondido 14/11: no jogo ficou como a imagem da esquerda; o usuário quer a da direita, ou seja, o 2x por padrão: prioridade N10–N13.)
- (N04b) Respondido 14/11: o Liu Kang não gira mais. Resolvido.
- (N03b/N05–N07, 14/11) O chão ainda aparece quadriculado em algum lugar? O raio arco-íris e os raios com degraus sumiram? (A cutscene e a primeira sala ficaram como as referências do original nas capturas.)
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
- [-] T21 Quadro cheio em vez de campo (com escala 2x) — abandonada na N19 (pouco detalhe a mais em 2x, um quadro de atraso)
- [x] T22 Suavização de bordas (FXAA) — filtro próprio na janela, `PS2X_FXAA=1` (desligado por padrão)
- [x] T23 Widescreen 16:9 — feito na N19 (patch 16:9 do PCSX2, menu Gráfico)
- [x] T24 Controle configurável e latência de entrada — feito na N19 (aba Controles, dois controles, atraso medido)
- [x] T25 Configuração completa no `mksm.ini` — todas as opções de uso no cabeçalho do `scripts/jogar.sh`

**Fase 6: robustez e entrega**
- [ ] T26 Teste de uma hora
- [x] T27 Build do zero e guia do jogador — 17/11: instaladores de dois cliques e zips do release; o do Linux testado do zero, sem internet
- [x] T28 Outras máquinas (sem Vulkan, Intel) — feito na N18; 17/11: Windows abre também sem Vulkan e o Linux roda em CPU sem AVX2
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
