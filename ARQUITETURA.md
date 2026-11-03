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
3. **VU1** (`vu/`): microprogramas transformam vértices e fazem XGKICK (PATH1). JIT x86-64 com o interpretador como referência (`PS2X_VU_JIT_CHECK`).
4. **Árbitro GIF** (`gs/ps2_gif_arbiter.cpp`): ordena PATH1/2/3 e entrega pacotes ao `GS` (frontend).
5. **Frontend do GS** (`gs/gs_frontend.cpp`): decodifica PACKED/REGLIST/IMAGE, mantém os registradores (dois contextos), monta `GSPrimitiveBatch` (até 3 vértices mais o estado completo) e chama o backend (`GSRasterBackend` em `gs_backend.h`).
6. **Backend** (`gs/gs_cpu_backend.cpp`): rasteriza em CPU sobre os 4 MB da VRAM com o layout real (swizzle por formato em `ps2_gs_memory.*`).
7. **Apresentação:** PMODE/DISPFB/DISPLAY escolhem o framebuffer, a cópia do campo completo vira textura RGBA e o raylib desenha na janela. É o **único** uso da GPU hoje.

**Campos:** o MKSM desenha um campo 640×224 em uma das páginas `0x00`/`0x46` e passa para a outra no campo seguinte. Bloom (`0x8C`), cópias de Z (`0x12C`/`0x14C`) e HUD acontecem antes da troca. Por isso "fim de quadro" = **troca entre `0x00` e `0x46`**, regra usada pela cópia da janela, pelo medidor e pela gravação. Na fase, o `PS2X_PERF` mostra essas trocas no mesmo ritmo do vsync (um campo por vsync). A espera em `0x274688` compara o contador de vsync com `último+2`; falta conferir como as duas coisas se encaixam.

**Interface trocável:** `GSRasterBackend` (`Submit`, `LoadClut`, `BeginTransfer`, `UploadImage`, `Flush`, `Sync`, `Present`, `ReadVram`, `WriteVram`, `ClearFramebuffer`, `SnapshotVram`...). Um backend de GPU entra aqui (`GS::setRasterBackend`). A gravação de quadros também (`GSRecordingBackend`, §6).

---

## 4. Tempo e sincronização

- **Relógio do EE:** `EeScheduler::accountCycles` soma ciclos por bloco gerado (32 por volta de laço), avança os timers em lote (até 1024 ciclos) e o IOP (1/8 dos ciclos, em lotes).
- **Vblank:** a cada 16,667 ms de tempo real **e** 4,9 M ciclos de EE. O handler do jogo incrementa o contador em `0x67B79C`.
- **Espera de quadro do MKSM:** `0x274688` gira até o contador de vsync chegar a `último+2` (ver a nota sobre campos no §3). As voltas do laço são `idle_loops` no `sm.toml`: o código gerado chama `eeIdleCheckpoint`, que pula o relógio até o próximo evento (`MKSM_NO_IDLE_SKIP=1` desliga).
- **IOP:** threads que só fazem polling (o CRI_ADXI) são cobradas 4× por fatia (atalho documentado em `iop_emulator.cpp`).

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

- **Perfil por amostragem:** `PS2X_PROFILE=<s>` grava `ps2_profile.txt`; os símbolos vêm do `nm`. Para perfilar só uma thread, filtre pelo nome dela no handler.
- **CPU por thread:** `/proc/<pid>/task/*/stat` (nomes: `GameThread`, `ps2EntryRunner` para o worker e as bandas).
- **Quem escreve um endereço:** proteger a página do host com `mprotect`, logar o RIP no `SIGSEGV`, liberar a página e dar passo único (flag TF); no `SIGTRAP`, proteger de novo; depois `addr2line`. Foi assim que se achou o arquivo da fase gravando o bloom.
- **Disassembly:** `llvm-objdump -d --triple=mipsel --mcpu=mips3 --start-address=A --stop-address=B extraido/SLUS_210.87`, ou o comentário no `output/sub_*.cpp`. Endereços constantes aparecem resolvidos (`FAST_WRITE32(0x4F6E98u, ...)`).
- **Capturas:** `PS2X_SCREENSHOT_DIR=<dir>` grava a janela a cada 600 quadros da janela.
