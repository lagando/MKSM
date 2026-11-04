# Arquitetura do ShaolinMonks (PS2Recomp + MKSM)

> **Para IAs:** leia este arquivo antes de mexer no runtime e atualize-o no mesmo commit quando mudar threads, locks, o caminho de um quadro ou interfaces (regras em `CLAUDE.md`).

Mapa do sistema para planejar mudanças grandes sem precisar redescobrir tudo. Ele diz **quem roda onde**, **quem espera quem** e **quais armadilhas este jogo tem**. O histórico de como cada coisa foi descoberta fica no `PLANO.md`. O plano de velocidade fica no `PLANO_DESEMPENHO.md`. Modificações do jogo ficam no `MODDING.md`.

Caminhos relativos a `PS2Recomp/ps2xRuntime/` quando não indicado.

---

## 1. Peças

| Peça | Onde | Papel |
|---|---|---|
| Recompilador | `PS2Recomp/ps2xRecomp`, configurado por `sm.toml` | Lê o ELF e gera `output/sub_XXXXXXXX_*.cpp`: uma função C++ por função MIPS, com o assembly original em comentário |
| Código gerado | `output/` → `src/runner/` | Compilado com `-O1` (13 mil arquivos; `-O3` deixaria a build longa demais) |
| Runtime | `src/lib`, `include` | Memória do PS2, DMA, VIF, VU0/VU1, GIF, GS, kernel (threads, semáforos, IRQs), stubs da SDK e janela (raylib); `-O3` e `-march=native` |
| IOP | `PS2Recomp/ps2xIOP` | Interpretador MIPS do IOP que roda os IRX reais (CRI ADXI, SNDF etc.) mais HLE de módulos |
| Overrides do jogo | `src/lib/game_overrides_mksm.cpp` | Ligações específicas do MKSM (libcdvd → HLE, pulo da intro) |
| Testes | `PS2Recomp/ps2xTest` (`ps2x_tests`) | Suítes por subsistema (GS, VU1, memória, kernel...), sem o jogo |

**Regra de rebuild:** headers incluídos por `include/ps2_runtime.h` (`ps2_memory.h`, `gs_frontend.h`, `gs_backend.h`, `ps2_vu1.h`...) entram no código gerado, e mexer neles recompila os 13 mil arquivos. Prefira mudar `.cpp` ou headers que só o runtime inclui (`gs_cpu_backend.h`, `ee_scheduler.h`).

---

## 2. Threads e quem espera quem

```
 thread principal (janela/raylib)          GameThread (EE + VU + IOP)                  worker do GS            bandas 1..3
 ─────────────────────────────            ──────────────────────────────              ────────────            ───────────
 latchHostPresentationFrame()  ──lock──▶  GS::m_stateMutex (por pacote GIF)  ──fila──▶  WorkerLoop  ──segmento──▶ BandLoop
   └─ Present(): copia o último           código do jogo, VIF1, VU1 (JIT),            (comandos em ordem)     (linhas y>>3 % 4)
      quadro completo                     IOP a cada lote de ciclos
```

- **GameThread** (`ps2_runtime.cpp`, `EeScheduler::run`): executa todo o EE. Isso inclui o código gerado, as interrupções (vblank, DMAC, timers), o VIF1 e a VU1 (microprogramas via MSCAL/MSCNT, rodando até o fim) e o IOP (`advanceIopEeCycles`, em lotes de 16384 ciclos de EE). Tudo em série nessa thread.
- **Worker do GS** (`GSCpuBackend::WorkerLoop`): consome a fila de comandos (`Draw`, `LoadClut`, `BeginTransfer`, `UploadImage`) em ordem e junta desenhos sem dependência em segmentos.
- **Bandas** (`BandLoop`, 3 threads mais o próprio worker): cada segmento é desenhado em paralelo por faixas de 8 linhas (`rowInBand`). Antes de um desenho que lê textura escrita por outro desenho ainda pendente, o segmento é esvaziado (*flush*).
- **Thread principal:** janela, entrada e apresentação, sempre a 60 Hz. `Present()` não espera a fila: usa `m_completedFrameVram`, uma cópia da VRAM tirada quando o jogo termina um campo. No MKSM isso é a troca do alvo entre as páginas `0x00` e `0x46` (§3). `latchHostPresentationFrame` só tenta pegar `GS::m_stateMutex` (`try_lock`); se a GameThread estiver com ele, reaproveita o último pedido de apresentação.

**Pontos de espera** (onde o desempenho se perde):

| Quem espera | O quê | Quando |
|---|---|---|
| GameThread | Fila do GS cheia (32768 comandos) | GS mais lento que o jogo: menus e fase (~50% do tempo no menu) |
| Thread principal | ~~`GS::m_stateMutex` em `latchHostPresentationFrame`~~ | **Corrigido (run174):** os dois pontos (o pedido de apresentação e o histórico de depuração) usam `try_lock`. Antes, a janela ficava 43% do tempo presa ali e caía para 20–35 Hz no menu. Não volte a pegar esse lock de forma bloqueante na thread da janela |
| GameThread | `WaitForQueue()` em `ReadVram`, `WriteVram`, `ClearFramebuffer`, `Sync(Finish/LocalToHost)` | Leituras de volta da VRAM pelo EE (raras no MKSM) |
| GameThread | Prazo do vblank | O vblank só dispara quando chegam **o prazo de ciclos do EE e o de tempo real** (`processDueDeadlines`). Se o EE emulado andar mais devagar que o PS2, o jogo inteiro fica mais lento |

---

## 3. Caminho de um quadro

1. **Jogo (EE):** monta listas DMA. A geometria vai pelo VIF1 para a VU1, e as telas 2D e os uploads vão pelo PATH3 (GIF DMA).
2. **VIF1** (`ps2_vif1_interpreter.cpp`): UNPACK para a memória da VU1, MSCAL/MSCNT, DIRECT/DIRECTHL (PATH2). Uma tag IMAGE que não cabe num DIRECT continua no **payload do DIRECT seguinte**; os VIFcodes entre eles executam normalmente.
3. **VU1** (`vu/`): microprogramas transformam vértices e fazem XGKICK (PATH1). JIT x86-64 com o interpretador como referência (`PS2X_VU_JIT_CHECK=1` confere cada bloco: VF, VI, ACC, Q/P/CLIP, memória e ciclos).
   - **Callouts:** `CLIP` e, no lado de baixo, `DIV`/`SQRT`/`RSQRT`, EFU, `MFP`, `FC*`, `ILW`/`ISW`/`ILWR`/`ISWR` entram no bloco como chamada ao código do interpretador, no ciclo certo (`m_cycle` = entrada + offset do par). Um bloco tem no máximo uma op de FDIV e uma de EFU; se a unidade estiver ocupada na entrada, o par fica com o interpretador. Cobertura na fase: ~84% dos pares no JIT.
   - **Diferença conhecida:** o MADD do JIT às vezes erra 1 ULP no w (≈10% dos blocos conferidos na fase, antes dos callouts também); um `DIV` depois disso herda a diferença no Q.
   - **Ciclos:** `m_nextPipelineCycle` guarda o próximo `readyCycle` dos pipelines (flags, FDIV, EFU, stores). `commitReadyPipelines` sai na hora antes dele, e `advanceTo` pula direto até ele, dando ao PATH1 os ciclos do meio de uma vez. Toda fila nova de pipeline precisa atualizar esse campo. O buffer de 64 KB do XGKICK não é mais zerado a cada XGKICK.
   - **Regenerar o código do jogo sem recompilar tudo:** o recompilador (`ps2xRecomp`) só reescreve arquivos cujo conteúdo mudou. Rode `./PS2Recomp/out/build/ps2xRecomp/ps2_recomp sm.toml` (escreve em `output/`, ~10 s) e copie para o runtime só os `.cpp` que mudaram: `rsync -rc --exclude='*.h' output/ PS2Recomp/ps2xRuntime/src/runner/`. Os dois headers de `output/` diferem dos do runtime e forçariam a recompilação completa. Uma correção de tradução assim leva ~1 min de build em vez de ~2 h. Para não depender de header novo, emita a semântica inline no texto gerado (como o `CVT.W.S`).
   - **Armadilha de build:** `runtime/ps2_vu1.h` entra no `ps2_runtime.h`: um membro novo na VU1 recompila os 13 mil arquivos (~25 mil s de CPU). Prefira estado local no `.cpp` (lambdas dentro de métodos acessam membros privados).
4. **Árbitro GIF** (`gs/ps2_gif_arbiter.cpp`): ordena PATH1/2/3 e entrega pacotes ao `GS` (frontend).
5. **Frontend do GS** (`gs/gs_frontend.cpp`): decodifica PACKED/REGLIST/IMAGE, mantém os registradores (dois contextos), monta `GSPrimitiveBatch` (até 3 vértices mais o estado completo) e chama o backend (`GSRasterBackend` em `gs_backend.h`).
6. **Backend** (`gs/gs_cpu_backend.cpp`): rasteriza em CPU sobre os 4 MB da VRAM com o layout real (swizzle por formato em `ps2_gs_memory.*`).
7. **Apresentação:** PMODE/DISPFB/DISPLAY escolhem o framebuffer, a cópia do campo completo vira textura RGBA e o raylib desenha na janela. É o **único** uso da GPU hoje.

**Campos:** o MKSM desenha um campo 640×224 em uma das páginas `0x00`/`0x46` e passa para a outra no campo seguinte. Bloom (`0x8C`), cópias de Z (`0x12C`/`0x14C`) e HUD acontecem antes da troca. Por isso "fim de quadro" = **troca entre `0x00` e `0x46`**, regra usada pela cópia da janela, pelo medidor e pela gravação. Na fase, o `PS2X_PERF` mostra essas trocas no mesmo ritmo do vsync (um campo por vsync). A espera em `0x274688` compara o contador de vsync com `último+2`; falta conferir como as duas coisas se encaixam.

**Acelerador GPU** (`PS2X_GS_GPU=1`, `gs/gs_gpu_accelerator.*`, Vulkan compute):
- **Memória:** com ele ligado, a memória local do GS (4 MB) passa a ser um buffer Vulkan mapeado na CPU (`vram()`). O `GSCpuBackend` continua desenhando nele o que a GPU ainda não faz.
- **Cópia na GPU dedicada:** numa GPU dedicada esse buffer fica na RAM do sistema, e a GPU acessaria cada pixel pelo PCIe (lento). Por isso ela trabalha numa cópia na memória dela (`deviceVram`), sincronizada por faixas de blocos:
  - **CPU → GPU:** toda escrita da CPU na VRAM avisa `HostWrote(início, fim)`: segmentos de CPU (frame e Z), desenhos de feedback, uploads, cópias local→local, `ClearFramebuffer`, `WriteVram`, `LoadState` e a carga inicial. Sem lote aberto, a faixa entra numa lista e sobe no início do próximo; com lote aberto, a cópia é gravada na hora. **Escrita nova na VRAM pela CPU sem `HostWrote` = imagem errada só com GPU.**
  - **GPU → CPU:** no `Flush()`, as faixas que o lote escreveu voltam para a RAM antes da fence.
  - Na Intel integrada, a memória já é comum e não há cópia (`mirror = false`).
- **Buffers auxiliares** (registros, tiles, UV, CLUT, tabelas, snapshot) ficam na memória da GPU visível à CPU (BAR): a CPU só escreve neles.
- **Lotes:** no `WorkerLoop`, um desenho aceito por `PrepareGpuSprite` entra num command buffer; desenhos consecutivos da GPU vão juntos, com uma barreira entre cada um.
- **Envio sem espera:** dois slots de envio (command buffer, fence e metade dos anéis de UV, registros, tiles e CLUT cada). No fim de cada leva de comandos o worker só chama `Submit()`, e a GPU roda enquanto a CPU monta o próximo. `Flush()` envia e espera tudo. Ele roda quando a CPU vai mexer na VRAM e o `Touches` (que também olha o que está em voo) acusa conflito, antes de segmentos de CPU, na cópia do campo completo e em `WaitForQueue` (leitores de fora: `ReadVram`, `SnapshotVram`, `SaveState`...). Uma barreira no começo de cada command buffer mantém a ordem do GS entre os envios. Fora de um envio, `SetClut` só guarda a paleta; ela é escrita na primeira versão do slot quando ele começa.
- **Uploads na GPU:** uploads de 4 KB ou mais (texturas) viram um dispatch do `gs_tiles.comp` em modo upload (constante de especialização `kMode = 1`): um pixel por invocação, bytes no anel de UV, escrita por `writeVram`. Assim entram na fila em ordem e nunca esperam a GPU. Ficam na CPU os pequenos (CLUTs, que a carga de CLUT lê da RAM), os que dão a volta na largura do buffer e os formatos fora da lista. `PS2X_GS_GPU_UPLOAD=0` desliga.
- **Exatidão:** a GPU usa a mesma tabela de swizzle CT32 da CPU (`GSMem::PixelIndexCT32`) e recebe da CPU as coordenadas de texel por coluna e linha. Sprites que leem o próprio alvo copiam antes as páginas da textura (`FeedbackPages`, igual à CPU).
- **Dispositivo:** por padrão, a GPU dedicada (NVIDIA aqui), que mediu mais rápido; a Intel integrada travou uma vez (`i915 GPU HANG`). Escolha outra com `PS2X_GPU_DEVICE=<parte do nome>`.
- **Quando esperar a GPU:** o acelerador guarda as faixas de VRAM que o trabalho pendente **lê** e **escreve**.
  - **CLUT e cópias local→local** (leem a VRAM): só esperam se houver escrita pendente na faixa.
  - **Uploads** (escrevem): esperam se houver leitura ou escrita pendente.
  - **Leituras contadas:** textura, frame e Z escritos, e também o Z que o teste de profundidade lê com `ZMSK=1` (`depthRead*`). A carga de CLUT confere `CBP..+8` no CSM1 e a linha `(COU*16, COV)` de largura `CBW` no CSM2.
  - **Troca de CLUT:** vira uma nova versão num buffer circular, e cada primitivo guarda a sua (`words[58]`). Assim nunca força espera.
- **Ordem dentro de um lote de tiles:** além de ler depois de escrever, o lote também é cortado quando um primitivo **escreve onde outro do lote leu**, porque o pixel dono da escrita pode rodar antes do pixel que leu.
- **Hoje cobre:**
  - **Sprites do caminho rápido** (`gs_sprite.comp`, um dispatch por sprite): bloom, cópias, sprites lisos com blend.
  - **Triângulos e sprites gerais** (`gs_tiles.comp`): rasterizador em tiles de 16×16, em que cada thread é dona de um pixel e percorre, na ordem do GS, os primitivos do lote que tocam o seu tile. Inclui o `WritePixel` completo, texturas de todos os formatos via tabelas de página geradas por `GSMem::PixelAddress`, CLUT, bilinear e fog.
- **Fica na CPU:** linhas, pontos e primitivos que leem o próprio alvo (fora os sprites rápidos). Frames em formato Z (o jogo limpa o Z desenhando sprites nele) já vão para a GPU.
- **Lotes de tiles:** um lote é cortado (novo dispatch, com barreira) quando um primitivo lê textura de memória que outro do lote escreveu, ou escreve a mesma memória por outro layout (frame×Z, largura ou formato diferentes).
- **Regra de exatidão:** `gs_cpu_backend.cpp` é compilado com `-ffp-contract=off` e o shader marca as contas com `precise`. Assim CPU e GPU fazem as mesmas operações IEEE, e o Z é interpolado em `double` nos dois. Toda conta em float nova precisa existir igual nos dois lados, na mesma ordem. Divisões por pixel na GPU usam recíproco refinado (`fma`) ou vêm prontas da CPU.
- **Validação:** replay das quatro gravações bit a bit igual à CPU (NVIDIA). O teste `GPU accelerator draws the same VRAM as the CPU through every wait rule` (suíte `gs`) roda um cenário sintético nos dois caminhos e compara a VRAM inteira: upload sobre textura pendente, 300 trocas de CLUT (passa das 256 versões), Z lido com ZMSK, CLUT CSM2, linha da CPU sob trabalho da GPU e cópia local→local. Sem Vulkan ele compara CPU com CPU e passa. Quebrar o `Touches` faz ele falhar.

**Interface trocável:** `GSRasterBackend` (`Submit`, `LoadClut`, `BeginTransfer`, `UploadImage`, `Flush`, `Sync`, `Present`, `ReadVram`, `WriteVram`, `ClearFramebuffer`, `SnapshotVram`...). Um backend de GPU entra aqui (`GS::setRasterBackend`). A gravação de quadros também (`GSRecordingBackend`, §6).

---

## 4. Tempo e sincronização

- **Relógio do EE:** `EeScheduler::accountCycles` soma ciclos por bloco gerado (32 por volta de laço), avança os timers em lote (até 1024 ciclos) e o IOP (1/8 dos ciclos, em lotes).
- **Vblank:** a cada 16,667 ms de tempo real **e** 4,9 M ciclos de EE. O handler do jogo incrementa o contador em `0x67B79C`.
- **Espera de quadro do MKSM:** `0x274688` gira até o contador de vsync chegar a `último+2` (ver a nota sobre campos no §3). As voltas do laço são `idle_loops` no `sm.toml`: o código gerado chama `eeIdleCheckpoint`, que pula o relógio até o próximo evento (`MKSM_NO_IDLE_SKIP=1` desliga).
- **SPU2:** não é emulado (só registradores e o tempo das DMAs; ver `notas/audio.md`). O PCM que o ADX manda por AutoDMA vai para o host: `finishSpuDma` (IOP) → `ps2_spu2_stream.cpp` → `AudioStream` do raylib, com velocidade de leitura que segue a fila (sem buracos abaixo de 60 campos/s). Efeitos sonoros (vozes do SPU2) ainda não tocam. Uma DMA para o SPU2 com AutoDMA ligado (`ADMAS`) termina no ritmo real do som, 768 ciclos do IOP por quadro estéreo. Terminar cedo fazia o ADX decodificar áudio ~384× mais rápido que o tocar.
- **IOP:** nenhuma thread é cobrada a mais por fatia. Um atalho de 4×/16× para threads que não cedem deixava a decodificadora do ADX faminta e fazia o zumbido no som (`notas/audio.md`). O laço do IOP só varre a fila de DMA quando alguma vence (`nextDmaDue`) e só procura a tabela de imports quando a instrução é `jr $ra`.

---

## 5. Armadilhas deste jogo (invariantes que qualquer reescrita do GS precisa manter)

| O quê | Detalhe | Onde está documentado |
|---|---|---|
| Z-buffer e bloom na mesma página | Página `0x8C` = Z24 da cena **e** rascunho 320×128 do bloom. O Z é zerado pelo clear do `sceGsPutDrawEnv`, montado por `sceGsSetDefClear` (`0x383560`) | `PLANO.md` 5A.15–5A.16 |
| Desenho que lê o próprio alvo | ~720 sprites de bloom por quadro leem `0x8C` e escrevem em `0x8C`. Cada primitivo precisa ver a VRAM **de antes dele** (cache de textura do GS; `autoFlush: 2` no PCSX2). `DrawFeedbackPrimitive` copia as páginas antes | 5A.15, 5A.17 |
| Z como textura | O jogo copia o Z (`0x8C`, lido como Z24) para `0x12C`/`0x14C` e usa como textura/Z de efeitos | 5A.15 |
| Bloom configurado pela fase | `0x4F6E70` (+0x1C ganho, +0x28 limiar; 0xFF = desligado), copiado do arquivo da fase por `0x1E5858` | 5A.16, `MODDING.md` |
| TEX1 com LCM=1 | O LOD é fixo (K); K ≤ 0 usa o filtro de magnificação. As passadas do bloom são por ponto | 5A.15 |
| Sprites "só alfa" | Blend que devolve Cd (`0x6A`, `0x68` com FIX 0): só o byte de alfa muda (caminho rápido `writesOnlyAlphaByte`) | código |
| Resolução | 640×224 por campo (entrelaçado), XYOFFSET da cena 1728,1936, framebuffers nas páginas `0x00` e `0x46` | `MODDING.md` §3 |
| Telas CT16 na página 0 | Aviso legal e "LOADING" (512×448 CT16) usam a página do framebuffer; o backend lembra o PSM de cada página para não apresentar CT16 como CT32 (era o verde) | `PLANO.md` 5A.21, `notas/telas_verdes.md` |

---

## 6. Medir e testar sem bootar o jogo

- **Medidor:** com `PS2X_PERF=1`, uma linha a cada 2 s: janela (fps e imagens novas/s), campos do jogo/s, vsync/s, % do tempo com o worker do GS ocupado e % do tempo em que o jogo esperou a fila do GS. Código em `runtime/ps2_perf.h`; os contadores ficam no scheduler, na janela e no `GSCpuBackend`.
- **Ruído:** mede com a máquina ociosa (outra build ou outro jogo rodando distorce tudo).

- **Gravar quadros** (`GSRecordingBackend`): `PS2X_GS_RECORD=arquivo.gsr`, `PS2X_GS_RECORD_AT=<segundos>`, `PS2X_GS_RECORD_FRAMES=<n>`.
  - **Estado inicial:** no primeiro fim de quadro (troca `0x00`↔`0x46`) depois de `AT` segundos, grava o estado do backend (VRAM + CLUT + transferência).
  - **Comandos:** depois disso, grava todos os comandos do backend, em ordem, por `n` quadros.
  - **Quando cada tela aparece** varia entre execuções. Confira a imagem com `--ppm` e dê ao arquivo o nome do que ele contém.
- **Reproduzir** (`ps2x_gs_replay arquivo.gsr`): desenha os quadros num backend novo, mede o tempo por quadro e imprime o hash da VRAM.
  - `--repeat N`: repete o arquivo, para medir com menos ruído.
  - `--dump-vram arquivo`: salva a VRAM final.
  - `--compare arquivo`: compara com uma VRAM salva e diz quantos bytes e quais páginas diferem.
  - `--ppm prefixo`: salva os framebuffers `0x00` e `0x46` como imagem.
- **Uso:** gravar uma vez cada caso (menu, seleção, fase, bloom). Toda mudança no GS (e o futuro backend de GPU) é medida e comparada em segundos contra o backend CPU.

---

## 7. Ferramentas de investigação que já funcionaram

- **Perfil por amostragem:** `PS2X_PROFILE=<s>` grava `ps2_profile.txt`; os símbolos vêm do `nm`. `PS2X_PROFILE_THREAD=GameThread` guarda só as amostras dessa thread. Cada linha tem `rip` e `[rsp]`: numa função folha como `memcpy`, o segundo é quem chamou. `ptrace` está bloqueado (`ptrace_scope=1`), então `gdb`/`eu-stack` não anexam ao jogo.
- **CPU por thread:** `/proc/<pid>/task/*/stat` (nomes: `GameThread`, `ps2EntryRunner` para o worker e as bandas).
- **Quem escreve um endereço:** proteger a página do host com `mprotect`, logar o RIP no `SIGSEGV`, liberar a página e dar passo único (flag TF); no `SIGTRAP`, proteger de novo; depois `addr2line`. Foi assim que se achou o arquivo da fase gravando o bloom.
- **Disassembly:** `llvm-objdump -d --triple=mipsel --mcpu=mips3 --start-address=A --stop-address=B extraido/SLUS_210.87`, ou o comentário no `output/sub_*.cpp`. Endereços constantes aparecem resolvidos (`FAST_WRITE32(0x4F6E98u, ...)`).
- **Capturas:** `PS2X_SCREENSHOT_DIR=<dir>` grava a janela a cada 600 quadros da janela.
