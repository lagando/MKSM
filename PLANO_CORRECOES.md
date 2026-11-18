# Plano de correções: save state, trava da Wu Shi, HUD e texturas (17/11)

Plano para os problemas que apareceram jogando a história em 17/11. Mesmo formato dos outros planos: faça as tarefas
**na ordem, uma por vez**; cada uma diz o que fazer, como verificar e quando está pronta. Marque `[x]` e anote o que
mediu. Leia antes o `ARQUITETURA.md` e as rotinas R3, R4 e R4b do `PLANO_DESEMPENHO.md` (§3).

O save state vem primeiro porque cada teste da trava custa uns 10 minutos de jogo até o lugar; com ele, a mesma cena se
repete em segundos e todas as fases seguintes andam mais rápido.

## 1. Regras

- **Commits e PRs:** só o usuário é autor; nada de `Co-Authored-By` ou atribuição a IA.
- **`PS2Recomp/`:** só por `scripts/atualizar_patches.sh` (arquivo novo entra em `patches/arquivos.txt`).
- **Toda mudança no runtime** passa por R3 (testes) e R4/R4b (hashes de replay) antes do commit.
- **Diagnóstico temporário** está marcado com `DIAG_TEMP` e sai antes de cada commit:
  `grep -rn DIAG_TEMP PS2Recomp/ps2xRuntime PS2Recomp/ps2xIOP` precisa sair vazio.
- **Save state é só para depuração:** desligado por padrão. Com ele desligado, nada muda no jogo.
- **Nada do jogo no git:** saves, savestates, cópias de RAM e gravações (`gsrec/`) ficam fora.

## 2. O que já sabemos (17/11)

**Trava na saída da Wu Shi Academy (história, depois do vídeo `END006.SFD`):**
- **Onde:** o personagem pula do muro da Wu Shi para o pátio. No jogo original ele cai no chão e começa uma cutscene. Aqui, a troca de mapa carrega e o jogo fica esperando.
- **É igual com os dois saves do usuário** (Liu Kang no slot 1, Johnny Cage no slot 2). **Não é o lutador extra.**
- **O que aparece na tela:**
  - **Liu Kang:** parado no ar sobre a passarela, sem o HUD (gravações F9 `cena_20261117_213310/213325.gsr`, com 15 s de diferença, iguais);
  - **Johnny Cage:** a mesma cena coberta por um quadrilátero preto de tela cheia com alfa 255 (`cena_20261117_184125.gsr`; a cena aparece tirando esse primitivo com o `gsr_semfade`).
- **O jogo não está travado:** roda a 55–60 quadros/s e a medição da GameThread mostra o quadro normal (desenho, VU1, DMA, IOP). É a lógica do jogo esperando.
- **Descartado:**
  - **filme:** nenhuma chamada `sceMpeg*` depois do `END006`;
  - **segundo circuito de vídeo:** o `PMODE` só tem o circuito 1;
  - **carregamento lento:** a transição caiu de 36–44 s para ~14 s e travou igual;
  - **funções do IOP faltando:** nenhum `[ps2xIOP:warning]`;
  - **busca de arquivo falhando:** nenhum `sceCdSearchFile failed`.
- **A pista (17/11, registro de RPC):**
  - na gameplay, o EE lê o disco a cada 2 s, chama o serviço de som do IOP `0x53465356` (função `0x8000`) uma vez por quadro e passa dados por SIF DMA (`sceSifSetDma` na medição); o IOP não lê o disco sozinho (o `sceCdRead` do IOP nunca é chamado);
  - na transição, o jogo chama o serviço `0x534E4446` ("SNDF", o `SNDFI.IRX`) com as funções `0x704`, `0x300`, `0x705` e `0x706`, e o serviço `0x90000200` (o ADX da CRI) com `0x410` e `0x411`;
  - depois disso, o EE **não lê mais o disco** e só segue a chamada `0x8000` de cada quadro.
  - **Hipótese:** o jogo pediu ao IOP os sons em stream da cutscene e confere a cada quadro se ficaram prontos; alguma resposta, sinal ou transferência entre o EE e esses módulos (que rodam como código real do IOP no emulador; os únicos serviços emulados por fora são `libsd` `0x80000701`, `mcserv` `0x80000400` e `dbcman`) não acontece como no PS2.
- **Antes do muro, outra trava (corrigida):** o `FRAME` inválido do §5 do `ARQUITETURA.md` fazia o jogo se arrastar a 1 quadro/s.

**Carregamentos lentos, com a janela congelada (corrigido e commitado em 17/11):**
- **Causa:**
  - a tela de carregamento desenha uma animação 3D numa página CT32 (`0x38`, na GPU) e a copia, com 224 sprites, para a tela CT16 de 512×448 na página 0, que vai pela CPU;
  - o `gs_cpu_backend.cpp` media a área de destino com páginas de 32 linhas, mas as de 16 bits têm 64;
  - a tela CT16 "invadia" a textura logo depois (`0x700`), e cada sprite ia pelo caminho de leitura do próprio alvo (`DrawFeedbackPrimitive`, que copia as páginas da textura e sincroniza as threads a cada primitivo).
- **Correção:** `spanBlocks` no `gs_cpu_backend.cpp` mede o alvo e a profundidade com a altura de página de cada formato.
- **Medido no carregamento do Goro's Lair:**

  | | antes | depois |
  |---|---|---|
  | janela (pior momento) | 3,5 fps | 37 fps |
  | GS ocupado | 83% | 10–15% |
  | vsync | 31/s | 60/s |

- **Testes:** R3 deu o esperado e os 22 hashes de R4/R4b ficaram iguais.
- **O que sobra:** a janela ainda cai para ~40 fps nas telas CT16, porque o `GSHwBackend::Present` espera a fila (`waitIdle`) quando a imagem mostrada não é uma superfície da GPU (fase 6).
- **Tentado e desfeito:** baixar só as superfícies que o primitivo usa no `Renderer::fallback` e não esperar a CPU a cada primitivo. Não mudou o tempo, porque o custo estava no `DrawFeedbackPrimitive`.

**HUD sumindo:** as 3 gravações F9 da rodada do Liu Kang (`cena_20261117_191016`, `_191032`, `_191059`, em `portatil/MKSM-linux/gsrec`) mostram só "EXP" e o nome. Faltam a medalha e as barras de vida e de energia. O usuário associa o sumiço a uma "luminosidade".

**Textura de fundo parada:** relato do usuário na mesma rodada, ainda sem descrição exata.

**Escurecimento quando algo passa na frente da câmera:** relato da primeira rodada de 17/11, sem captura.

## 3. Ferramentas

- **Versão de diagnóstico** (`DIAG_TEMP` na árvore do `PS2Recomp`, sem commit):
  - **Registro:** `[diag] sala` (lutador em `save+0xCC/0xD0` a cada `func_1CEE00`) e `[diag] filme` (cada `.SFD` lido).
  - **Teclas:** F7 salva a RAM do EE inteira (32 MB) em `capturas/`. F8 mede a GameThread por 15 s, com `PS2X_PROFILE=tecla PS2X_PROFILE_THREAD=GameThread`.
  - **Captura automática** (até 4 por sessão), disparada por janela parada mais de 20 s, janela sem uma volta rápida por 20 s, ou imagem toda preta por 12 s com o jogo rodando. Ela salva a RAM, mede a GameThread e imprime as pilhas de todas as threads (`[pilha]`, por sinal `SIGUSR2`, porque o `ptrace` está bloqueado).
  - **`PS2X_DIAG_FALLBACK_SLEEP_US=N`:** atrasa cada primitivo que vai para a CPU, para simular um carregamento longo.
- **Atalhos na pasta portátil** (fora do git):
  - `jogar-diagnostico.sh`: usa o executável de diagnóstico e grava `diagnostico.txt`;
  - `jogar-teste-liukang.sh`: carrega um save com o Liu Kang no lugar do lutador gravado (`MKSM_TEST_LUTADOR=0`) e devolve o memory card no fim.
- **Ferramentas desta sessão** (entram no repositório na tarefa 0.2):
  - `gsr_stats`: primitivos por alvo e envios por quadro de uma `.gsr`;
  - `gsr_seq`: sequência de desenho de um quadro, agrupada por estado;
  - `gsr_semfade`: copia a `.gsr` sem o quadrilátero preto de tela cheia, para ver o que há atrás;
  - `pilhas.py`: traduz os blocos `[pilha]` para nomes de função com `addr2line`.
- **Replay:** `ps2x_gs_replay arquivo.gsr --ppm-each prefixo` grava cada quadro; com `PS2X_GS_HW=0 PS2X_GS_GPU=0`, desenha pela CPU, para comparar com a GPU.

## 4. Tarefas (faça na ordem)

### Fase 0: guardar o que está pronto (~30 min)

- [x] **0.1 Commit da correção dos carregamentos** (17/11: junto foram o `fallback` mais leve, o descarte do `FRAME` inválido e a mistura dos dois circuitos no `GSHwBackend`; R3 `gs: Failed: 2`, os 22 hashes de R4/R4b iguais, Goro's Lair com a janela ≥ 48 fps e vsync 60 no carregamento)
  - **Faça:**
    - Salve o diagnóstico fora do git: `git -C PS2Recomp diff > /tmp/diag.diff`.
    - Tire da árvore os trechos `DIAG_TEMP`, deixando só o `spanBlocks` no `gs_cpu_backend.cpp`.
    - Rode `scripts/atualizar_patches.sh`.
    - Anote o caso no `ARQUITETURA.md` (armadilha: páginas de 16 bits têm 64 linhas) e no `PLANO_DESEMPENHO.md`.
  - **Verifique:** R3, R4 e R4b iguais aos valores esperados. Carregamento do Goro's Lair (R5) com a janela ≥ 35 fps e vsync 60/s.
  - **Pronto quando:** commit feito, o `grep DIAG_TEMP` vazio, e o diagnóstico reaplicado na árvore para as fases seguintes.
- [ ] **0.2 Ferramentas de gravação no repositório**
  - **Faça:** ponha `gsr_stats`, `gsr_seq` e `gsr_semfade` como alvos em `ps2xTest/tools` (ao lado do `ps2x_gs_replay`) e o `pilhas.py` em `scripts/`. Descreva-os em uma linha cada no `ARQUITETURA.md` §6.
  - **Verifique:** compilam com `cmake --build PS2Recomp/out/build`; `gsr_stats gsrec/menu.gsr` lista os quadros.
  - **Pronto quando:** commit feito, com os arquivos novos em `patches/arquivos.txt`.

### Fase 1: save state de depuração, 1 slot (estimativa: 1 a 2 dias)

**Feito em 18/11 (1.1 a 1.8):** `ps2_savestate.cpp`, com acesso por `friend struct SaveStateAccess` em vez de métodos em cada classe (`ARQUITETURA.md` §9). `ps2x_tests` 462/3 (teste novo de ida e volta passa). Cenário menu: `ram=` igual ao salvar e carregar, 60 quadros/s depois. Cenário luta: dois carregamentos seguidos, `ram=` igual, 60 quadros/s, sem `guest-branch`. Falta a 1.9 (com o usuário, no muro). Atalhos: a fila de áudio não é esvaziada; arquivos de host do IOP e a árvore da ISO não vão no arquivo (carregar depois de reabrir o jogo ainda não testado).

Esta fase está escrita para ser aplicada sem decisões novas. **Siga as decisões fixas abaixo.** Se algo não bater com
o código (um membro com outro nome, um arquivo que mudou), anote na tarefa o que achou e o que fez.

#### 1.0 Decisões fixas

1. **Liga com `MKSM_SAVESTATE=1`.** Desligado, não há tecla, não há arquivo, e o único código no caminho quente é a
   leitura de um `std::atomic<int>` por volta do escalonador. O `jogar-diagnostico.sh` liga; o `jogar.sh` não.
2. **Teclas:**
   - F5 salva e F6 carrega, lidas no laço da janela em `ps2_runtime.cpp`, junto do bloco de F8/F9/F10 (procure `KEY_F9`).
   - Elas só fazem o pedido (`ps2_savestate::request(Save|Load)`); quem atende é o escalonador, no ponto quieto.
   - F7 a F12 já têm uso.
3. **Arquivo:**
   - `savestate.bin`, na pasta do `MKSM_INI` (a pasta portátil), ou na pasta atual se não houver `MKSM_INI`.
   - Grave em `savestate.bin.tmp` e só depois renomeie, para um save state bom não se perder se o jogo fechar no meio.
4. **Formato:**
   - **Cabeçalho:** `char magic[8] = "MKSMSS01"`, `uint32_t formato = 1`, `uint64_t idExecutavel`.
   - **`idExecutavel`:** FNV-1a de 64 bits do próprio executável (`/proc/self/exe` no Linux; `GetModuleFileNameA` no Windows), calculado uma vez, no primeiro uso.
   - **Seções:** `char nome[8]`, `uint64_t tamanho`, bytes.
   - **Carregar é tudo ou nada.** Leia o arquivo inteiro para a memória e confira magic, formato, id e a presença de todas as seções **antes** de aplicar qualquer uma. Qualquer falha recusa o carregamento sem mexer em nada.
5. **Como escrever cada coisa:**
   - **Struct trivialmente copiável:** vai crua, com `memcpy`. Ponha `static_assert(std::is_trivially_copyable_v<T>)` em cada uso; o id do executável garante o mesmo layout.
   - **Contêineres:** quantidade e depois os elementos.
   - **`unordered_map`/`map`:** quantidade e depois os pares (chave, valor); refaça inserindo.
   - **`std::string`:** tamanho e depois os bytes.
   - **`std::variant`:** `index()` e depois a alternativa.
   - **Nunca grave:** ponteiros (grave um id e refaça o ponteiro ao carregar), `std::function`, mutex, `condition_variable`, `std::thread`, atomics de estatística ou log, contadores de log.
6. **Utilitário de escrita e leitura:** um arquivo só de header, `PS2Recomp/ps2xIOP/include/ps2x/iop/savestate_io.h`, com `namespace ps2x::savestate { class Writer; class Reader; }`.
   - **`Writer`:** `pod(const T&)`, `bytes(const void*, size_t)`, `str(const std::string&)`, `u64(uint64_t)`, `beginSection(const char nome[8])` e `endSection()`. Esta última preenche o tamanho da seção.
   - **`Reader`:** os mesmos métodos para ler, mais `bool ok()`. Leitura além do fim marca erro e devolve zeros, sem ler fora do buffer.
   - **Onde fica:** no IOP, porque o IOP e o runtime usam o mesmo arquivo e o runtime já inclui `ps2x/iop/`.
7. **Ponto quieto:** o topo do laço `while` de `EeScheduler::run()` (`ps2xRuntime/src/lib/Kernel/EeScheduler.cpp`, logo depois de `processPendingEvents()`).
   - **Por que ali:** nenhuma função recompilada está na pilha do PC, e o estado de cada thread do jogo está em `m_threads`. O código gerado sai até esse ponto sempre que um checkpoint vence, inclusive no laço de espera de quadro `0x274688` (`eeIdleCheckpoint`). O IOP roda na mesma thread (`accountCycles` → `advanceIopEeCycles`), então também está parado.
   - **O que fazer ali:** `if (ps2_savestate::pending()) ps2_savestate::service(m_runtime);`. Se o carregamento aplicou um estado, `continue` no laço: o escalonador despacha a thread atual salva, no `pc` salvo.
8. **Condições para salvar (todas).** Se alguma falhar, tente de novo na próxima volta. Depois de 5 s sem conseguir, desista e mostre o motivo da primeira que falhou.
   - **Threads do jogo:** em toda `GuestThread` de `m_threads`:
     - `invocations.empty()`;
     - `!resumeCompletion`;
     - `!wait.completion`;
     - `wait.reason` é `None`, `Sleep`, `Semaphore`, `EventFlag` ou `VSync`.
   - **Escalonador:** `m_pendingInvocations.empty()` e `!m_insideInterrupt`.
   - **Filme:** `g_moviePlaying == false` (`MPEG.cpp`).
   - **Memory card:** sem comando pendente (`g_mcCommandPending == false`, `MemoryCard.cpp`).
   - **IOP:** `callDepth == 0`, `activeCpu == nullptr`, `!servicingDmaInterrupts`, `!servicingGuestCallbacks` (`IopEmulator::Impl`, `iop_emulator.cpp`); `m_activeContext == nullptr` (`PS2IopHostAdapter`).
   - **GIF:** a fila do `GifArbiter` (`m_queue`) vazia depois do passo 9.
9. **Parar os outros trabalhadores, nesta ordem, antes de ler ou escrever estado:**
   1. `ps2_vu1_thread::sync()`;
   2. `gsAsyncDrain()`;
   3. no backend do GS, `Flush()` e depois `Sync(GSSyncReason::Finish)`;
   4. trave o mutex da fila de áudio do `ps2_spu2_stream.cpp`.
10. **Carregar** acontece no mesmo ponto quieto. Recuse durante filme e com comando de memory card pendente. Depois:
    1. leia e valide o arquivo inteiro;
    2. pare os trabalhadores (passo 9);
    3. feche os arquivos abertos do estado atual (memory card, arquivos de host do IOP e do EE);
    4. aplique as seções na ordem do arquivo;
    5. faça os ajustes de 1.0.12;
    6. `continue` no laço do escalonador.
11. **Aviso na tela por 2 s:** "Save state salvo", "Save state carregado" ou "Save state recusado: \<motivo\>". Desenhe com o `hudText` do `ps2_runtime.cpp`, no mesmo lugar dos outros avisos do overlay. Escreva no log também, com o prefixo `[savestate]`.
12. **Ajustes depois de carregar:**
    - **EE:** `m_checkpointPending = true`; `updateNextDeadline()`; prazos de tempo real refeitos a partir de agora (salve "ns que faltavam", não o instante); `renewTimeSlice()`; `publishSnapshot()`.
    - **VU:** `markVU0CodeModified()` e `markVU1CodeModified()`, chamados no fim de `PS2Memory::loadState` (são privados), e `invalidateCaches()` (novo) nas duas `VU1Interpreter`.
    - **IOP:** `jit.clear()`.
    - **GS:** `GSCpuBackend::LoadState(vram, estado)` no backend da CPU. Para chegar até ele, desembrulhe: `GSRecordingBackend::inner()` e `GSHwBackend::inner()` (acessor novo para `m_inner`). Depois, se houver `GSHwBackend`, `InvalidateAll()`.
    - **Áudio:** esvazie a fila do `ps2_spu2_stream.cpp` (função nova `spu2StreamClear()`).
    - **Modo RPG:** o estado salvo volta junto (seção `RPG`), para o XP não contar duas vezes.
13. **Teste sem ninguém no teclado:** `MKSM_SAVESTATE_AUTO="save@25,load@35"` faz os pedidos nos segundos dados, contados desde a abertura do jogo e cada um uma vez só. O log mostra o hash FNV-1a da RDRAM na hora de salvar e logo depois de carregar: `[savestate] salvo ... ram=<hash>` e `[savestate] carregado ... ram=<hash>`.
    - O hash do carregado tem de ser igual ao do salvo.
    - Os cenários de teste ficam em `scripts/teste_savestate.sh` (tarefa 1.2).

**Arquivos novos** (entram em `patches/arquivos.txt`):
- `PS2Recomp/ps2xIOP/include/ps2x/iop/savestate_io.h`;
- `PS2Recomp/ps2xRuntime/src/lib/ps2_savestate.cpp` (ponha na lista do `ps2xRuntime/CMakeLists.txt`, ao lado de `src/lib/mksm_rpg.cpp`);
- `PS2Recomp/ps2xRuntime/src/lib/ps2_savestate.h`, incluído só por `.cpp`, nunca pelo `ps2_runtime.h`;
- `scripts/teste_savestate.sh` (no repositório).

**Recompilação:** os headers incluídos pelo `ps2_runtime.h` forçam recompilar os ~13 mil arquivos gerados, o que pode passar de 1 hora. São eles: `ps2_memory.h`, `gs_frontend.h`, `ps2_vu1.h`, `ps2_gif_arbiter.h`, `ps2_audio.h`, `ps2_pad.h` e o próprio `ps2_runtime.h`. Por isso **toda** mudança nesses headers está na tarefa 1.1, num lote só. Depois dela, só se mexe em `.cpp` e em headers que o código gerado não inclui. Rode o build em segundo plano e confira o fim do log.

#### Tarefas

- [ ] **1.1 Headers, num lote só (uma recompilação longa)**
  - **Faça:**
    - No topo de cada header abaixo, ponha `namespace ps2x::savestate { class Writer; class Reader; }`.
    - Nas classes, acrescente as declarações públicas `void saveState(ps2x::savestate::Writer &w) const;` e `void loadState(ps2x::savestate::Reader &r);`. Os corpos vêm nas tarefas seguintes; por enquanto, deixe corpos vazios no `.cpp` de cada classe, para ligar.
    - Classes e extras:
      - `ps2xRuntime/include/runtime/ps2_memory.h`: em `PS2Memory` e em `PS2Memory::IoRegisterFile` (a classe aninhada tem membros privados próprios).
      - `ps2xRuntime/include/runtime/gs/gs_frontend.h`: em `GS`.
      - `ps2xRuntime/include/runtime/gs/ps2_gif_arbiter.h`: em `GifArbiter`. Acrescente também `bool empty() const { return m_queue.empty(); }`.
      - `ps2xRuntime/include/runtime/ps2_vu1.h`: em `VU1Interpreter`. Acrescente também `void invalidateCaches();`.
      - `ps2xRuntime/include/ps2_runtime.h`: em `PS2Runtime`, para os membros do próprio runtime.
      - `ps2xRuntime/include/runtime/ee_scheduler.h`: em `EeScheduler`. Acrescente também `bool saveStateBlocker(std::string &why) const;` e `void afterStateLoad();`. Este header não é incluído pelo `ps2_runtime.h`, mas fica no mesmo lote para ser um build só.
      - `ps2xRuntime/src/lib/gs/gs_hw_backend.h`: só `GSRasterBackend &inner() { return m_inner; }` (público).
      - `ps2xRuntime/src/lib/ps2_iop_host.h`: em `PS2IopHostAdapter`. Acrescente também `bool saveStateBlocker(std::string &why) const;`.
      - `ps2xIOP/include/ps2x/iop/iop_subsystem.h`: em `IopSubsystem`, com assinatura própria: `bool saveState(ps2x::savestate::Writer &w, std::string &why) const;` (a falha diz o motivo) e `bool loadState(ps2x::savestate::Reader &r, std::string &why);`. Acrescente também `bool saveStateBlocker(std::string &why) const;`.
  - **Verifique:**
    - o build completo termina;
    - R3, R4 e R4b dão exatamente os valores esperados (nada mudou de comportamento);
    - o jogo abre e chega à fase (R5).
  - **Pronto quando:** compila, R3/R4/R4b batem, e nenhum header da lista precisa mudar de novo nesta fase.
- [ ] **1.2 Utilitário, arquivo, teclas, aviso e teste automático (só RDRAM e scratchpad)**
  - **Faça:**
    - O `savestate_io.h` (1.0.6) e o `ps2_savestate.h/.cpp`, com: `request()`, `pending()`, `service(PS2Runtime&)`, cabeçalho, id do executável, tmp + renomear, validação tudo-ou-nada, aviso de 2 s e `MKSM_SAVESTATE_AUTO`.
    - O gancho no `EeScheduler::run()` (1.0.7).
    - F5 e F6 no laço da janela (1.0.2).
    - Primeira seção, `EE.RAM`: a RDRAM (`PS2_RAM_SIZE` bytes a partir de `m_memory.getRDRAM()`) e a scratchpad (16 KB). Nesta tarefa, carregar só copia essas duas; ainda não é um carregamento de verdade.
    - Um teste de ida e volta do `Writer`/`Reader` numa suíte nova do `ps2x_tests` (`savestate`): POD, string, vetor, mapa, variant, leitura além do fim marca erro.
    - O `scripts/teste_savestate.sh` com dois cenários, que imprime as linhas `[savestate]` e as de `[perf]` depois do carregamento:
      - **menu:** `MKSM_SKIP_INTRO=1 MKSM_AUTO_PAD=xs MKSM_AUTO_PAD_UNTIL=20 MKSM_SAVESTATE=1 MKSM_SAVESTATE_AUTO="save@25,load@32" PS2X_PERF=1 timeout 60`;
      - **luta:** `MKSM_PAD_CLOCK=reads MKSM_SKIP_INTRO=1 MKSM_AUTO_PAD=xs MKSM_AUTO_PAD_UNTIL=24.5 MKSM_AUTO_HOLD="x@36-36.2,s@50-50.2,R@53.5-55.5,s@61.8-62"` (o roteiro que chega à luta do Goro's Lair) com `MKSM_SAVESTATE_AUTO="save@80,load@95,load@115"` e `timeout 150`.
  - **Verifique:**
    - `ps2x_tests savestate` passa;
    - no cenário menu, `ram=` do carregado é igual ao do salvo;
    - com `MKSM_SAVESTATE` desligado, R5 igual a antes.
  - **Pronto quando:** as três coisas acima.
- [ ] **1.3 Escalonador do EE e membros do runtime**
  - **Faça:**
    - **`EeScheduler::saveState`/`loadState`.** Grave todos os membros de dados, **menos:**
      - `m_runtime`, `m_rdram`;
      - `m_eventMutex`, `m_eventCv`, `m_snapshotMutex`, `m_snapshot`;
      - `m_executorThread`, `m_running`, `m_guestExecuting`, `m_stopRequested`, `m_checkpointPending`;
      - `m_nextDeadlineCycle`, `m_nextDeadlineHostNs` (recalculados);
      - `m_eventsScratch`.
    - **Cada `GuestThread`:** todos os campos, menos `wait.completion`, `resumeCompletion` e `invocations` (garantidos vazios). Ao carregar, deixe esses três vazios.
    - **`EeWaitState.payload`:** é `std::variant` (1.0.5).
    - **`ScheduledEvent`:** `deadlineCycle`, `event`, `sequence`, e `hostDeadline` como "ns que faltavam" (1.0.12).
    - **`EeAlarm` e `EeIrqHandler`:** confira que são trivialmente copiáveis; se tiverem `std::function`, campo a campo.
    - **`EeScheduler::saveStateBlocker`:** devolve as condições de 1.0.8 que são dele. **`afterStateLoad`:** os ajustes do EE em 1.0.12.
    - **`PS2Runtime`** (seção `EE.RUNT`, no `ps2_savestate.cpp`): `m_cpuContext`, `m_eeExitHandlers`, `m_eeSyscallOverrides`, `m_eeSyscallMirrorAddresses`, `m_guestHeapBlocks`, `m_guestHeapBase/End/Limit/SuggestedBase`, `m_guestHeapConfigured`, `m_asyncCallbackStackFloor/Top`, `m_loadedModules`.
    - **Não grave do runtime:** backends de áudio, controle, ROM e VFS; interface de depuração; atomics `m_debug*`; `m_boundRdram`, `m_boundGSVram`.
  - **Verifique:** cenário menu, com `load` 7 s depois do `save`. Depois de carregar, o menu volta ao estado do save (veja pela captura F10 ou pelo `[perf]` sem erro) e o jogo segue 20 s sem `guest-branch`/`missing-target` no log.
  - **Pronto quando:** cinco rodadas do cenário menu sem travar.
- [ ] **1.4 Memória, DMA, VIF, timers e GIF (`PS2Memory`, `GifArbiter`)**
  - **Faça:**
    - **Seção `EE.MEM`, grave:**
      - a memória das VU: VU0 código e dados de 4 KB cada, VU1 código e dados de 16 KB cada (`m_vu0Code`, `m_vu0Data`, `m_vu1Code`, `m_vu1Data`);
      - `IoRegisterFile` (`m_flat`, `m_present`, `m_other`), `gs_regs`, `vif0_regs`, `vif1_regs`, `dma_regs[10]`;
      - `m_tlbEntries`, `m_seenGifCopy`;
      - `m_path3Masked`, `m_vif1PendingPath2ImageQwc`, `m_vif1PendingPath2DirectHl`, `m_path3MaskedFifo`;
      - `m_pendingGifTransfers`, `m_pendingVif0Transfers`, `m_pendingVif1Transfers` (campo a campo, porque `chainData` é vetor);
      - `m_completedDmacCauses` (com `m_completedDmacMutex`), `m_codeRegions`, `m_eeTimers`.
    - **Não grave:** `m_rdram`, `m_scratchpad` (já na `EE.RAM`), `m_gsVRAM` (vai com o GS), callbacks, `m_gifArbiter`, contadores atomics. Os `m_vu*CodeGeneration` só sobem no fim do `loadState`, pelos `markVU0CodeModified()`/`markVU1CodeModified()`.
    - **`IoRegisterFile`** é uma classe aninhada privada (linha ~384 do `ps2_memory.h`), com o próprio `saveState`/`loadState` da tarefa 1.1. O `PS2Memory` chama os dois.
    - **`GifArbiter`:** recuse salvar se `!empty()`. Não há mais nada a gravar.
  - **Verifique:** cenário luta. Depois de carregar, a luta continua com a imagem certa por 30 s, com `jogo` ≥ 50 quadros/s no `[perf]`.
  - **Pronto quando:** cenário luta sem travar e sem lixo na imagem (confira um F9 depois do carregamento, reproduzido com o `ps2x_gs_replay --ppm-each`).
- [ ] **1.5 VU0 e VU1 (`VU1Interpreter`, as duas instâncias)**
  - **Faça:**
    - **Grave:** `m_state` (confira com `static_assert` que é trivialmente copiável; se não for, campo a campo), `m_flagPipeline`, `m_fdiv`, `m_efu`, `m_storePipeline`, `m_xgkick` (se tiver vetor, campo a campo), `m_vfReady`, `m_viReady`, `m_accReady`, `m_cycle`, `m_efuResourceReady`, `m_nextPipelineCycle`, `m_workingClip`, `m_currentUpperInstruction`, `m_viBranchBackupValue/Reg/Valid`, `m_stopRequested`, `m_pendingHaltD`, `m_pendingHaltT`.
    - **Não grave:** `m_decodedCodeCache`, `m_cached*`, `m_uncachedPair`, `m_jit`, `m_jitEnabled`, `m_active*`.
    - **Recuse salvar** se algum `m_active*` não for nulo depois do `ps2_vu1_thread::sync()`.
    - **`invalidateCaches()`:** `m_decodedCodeCacheValid = false; m_cachedVuCode = nullptr; m_cachedMemory = nullptr; m_cachedCodeGeneration = 0;`.
  - **Verifique:** cenário luta com três carregamentos seguidos. Os personagens e o cenário 3D aparecem inteiros depois de cada um.
  - **Pronto quando:** sem polígonos faltando nem tremendo depois de carregar.
- [ ] **1.6 GS**
  - **Faça:**
    - **Seção `GS`, do `GS`, grave:** `m_ctx`, `m_prim`, `m_primRegister`, `m_prmodeRegister`, `m_curR/G/B/A`, `m_curQ`, `m_curS/T`, `m_curU/V`, `m_curFog`, `m_fogR/G/B`, `m_prmodecont`, `m_pabe`, `m_scanmsk`, `m_dimx`, `m_dthe`, `m_colclamp`, `m_texa`, `m_texclut`, `m_bitbltbuf`, `m_trxpos`, `m_trxreg`, `m_trxdir`, `m_vtxQueue`, `m_vtxCount`, `m_vtxIndex`, `m_preferredDisplaySourceFrame`, `m_preferredDisplayDestFbp`, `m_hasPreferredDisplaySource`.
    - **Não grave:** apresentação, depuração, `m_displaySnapshot`, mutexes, `m_localMemoryStorage`, `m_privRegs` (ponteiro; os registradores privilegiados já vão em `gs_regs`, na `EE.MEM`: confira para onde ele aponta).
    - **Backend:** ache o `GSCpuBackend` (1.0.12) e grave `SnapshotVram(vram)` (4 MB) e `SaveState(estado)`. Ao carregar, `LoadState(vram, estado)` e, se houver `GSHwBackend`, `InvalidateAll()`.
    - Faça tudo com o `m_stateMutex` do `GS` travado.
  - **Verifique:** salve na gameplay, ande, carregue. O primeiro quadro depois do carregamento mostra o lugar do save, sem tela verde, preta ou com lixo (F10 logo depois).
  - **Pronto quando:** dez carregamentos na gameplay, todos com a imagem certa.
- [ ] **1.7 IOP**
  - **Faça (tudo dentro do `ps2xIOP`):**
    - Cada classe recebe `saveState(Writer&) const` e `loadState(Reader&)`; os headers internos do IOP não forçam recompilar o código gerado.
    - **`IopEmulator::Impl`** (`iop_emulator.cpp`):
      - **grave** `memory`, `sysmem`, `kernel`, `cdvd`, `vblank`, `rpc`, `sysclib`, `stdio`, `heaplib`, `intrman`, `timrman`, `ioman`, `cpuCore` e `loadcore` (cada um pelo seu método), `modules`, `pendingDmaInterrupts`, `nextDmaDue`, `pendingGuestCallbacks`, `nextModuleId`, `moduleCursor`, `totalCycles`, `totalInstructions`, `eeCycleCarry` e os três `GuestCallback` (confira que são trivialmente copiáveis);
      - **não grave** `host`, `jit` (limpo depois de carregar), `imports` (tabela fixa), `activeCpu`, `lastError` e `callDepth`.
    - **`IopMemory`:** a RAM de 2 MB e os registradores do IOP que ela guarda (leia o `iop_memory.h`).
    - **`IopKernel`:** `m_threads`, `m_semaphores`, `m_eventFlags`, os próximos ids, e `m_currentThread` gravado como id (0 = nenhum), refeito com a busca em `m_threads`.
    - **Os outros módulos** (`IopCdvd`, `IopVblank`, `IopRpcBridge`, `IopSysclib`, `IopStdio`, `IopHeaplib`, `IopIntrman`, `IopTimrman`, `IopIoman`, `IopCpuCore`, `IopLoadcore`, `IopSysmem`): grave todos os membros que não sejam referência, ponteiro, mutex ou função.
    - **Arquivos abertos** (`IopIoman` e `PS2IopHostAdapter::m_hostFiles`): grave caminho, modo e posição, e reabra ao carregar. O jogo provavelmente deixa os `.WAD` abertos o tempo todo, então recusar não serve.
    - **`IopSubsystem::Impl`** (`iop_subsystem.cpp`): o emulador e os serviços de `coreServices` (memory card, `dbcman`, `libsd` e os outros). Na interface base dos serviços, acrescente `virtual void saveState(Writer&) const {}` e `virtual void loadState(Reader&) {}`, e implemente nos que têm estado. O `libsd` e o `spu2_voices.cpp` levam a RAM do SPU2 (2 MB), as vozes e o stream do ADX.
    - **`PS2IopHostAdapter`** (seção `IOP.HOST`, `ps2_iop_host.cpp`): `m_nextToken`, `m_nextHostFileHandle` e os arquivos, como acima.
  - **Verifique:** no cenário luta com música:
    - depois de carregar, a música continua;
    - um carregamento antes de entrar numa sala nova não impede a sala de carregar (leitura de disco);
    - um carregamento no meio da luta funciona.
  - **Pronto quando:** som e carregamentos de fase funcionam depois do save state.
- [ ] **1.8 Stubs do kernel e o resto**
  - **Faça:** em cada `.cpp` abaixo, duas funções `void saveX(ps2x::savestate::Writer&)` e `void loadX(ps2x::savestate::Reader&)`, declaradas no `ps2_savestate.h`, cada uma com o mutex do arquivo travado.
    - `Kernel/Stubs/SIF.cpp`: `g_nextSifDmaTransferId`, `g_sifRegs`, `g_sifSregs`, `g_sifCmdHandlers` (confira o tipo), `g_sifCmdBuffer`, `g_sifSysCmdBuffer`, `g_sifCmdInitialized`.
    - `Kernel/Stubs/Pad.cpp`: `g_padPorts`, `g_rumble`. Não `g_padOverride*`, que é de teste.
    - `Kernel/Stubs/MemoryCard.cpp`: `g_mcNextFd`, `g_mcLastCmd`, `g_mcLastResult`, `g_mcPorts`, `g_cvMcFileCursor`. Os arquivos de `g_mcFiles` vão como os arquivos do IOP (caminho, modo, posição).
    - `Kernel/Stubs/CD.cpp`: `g_cdStreamTiming`, `g_movieRanges` e todo estado de leitura em stream que estiver no arquivo. Procure também variáveis `static` dentro das funções.
    - `Kernel/Stubs/DMA.cpp`: `g_dmaCurrentEnv`.
    - `Kernel/Stubs/Audio.cpp`: `g_audio_stub_state`.
    - `Kernel/Stubs/MPEG.cpp`: `g_mpeg_stub_state`, se for trivialmente copiável. Se não for, recuse salvar com um MPEG criado. O filme já é recusado em 1.0.8.
    - `Kernel/Syscalls/RPC.cpp`: o estado de clientes e servidores de RPC do lado do EE, que está num namespace anônimo. Liste-o aqui ao implementar. Chamada em andamento recusa o salvamento.
    - `Kernel/Stubs/FileIO.cpp` e `Kernel/Syscalls/FileIO.cpp`: tabelas de arquivos abertos, como as do IOP.
    - `mksm_rpg.cpp`: `g_state`, `g_fighter`, `g_fighterPending` e o que mais for estado do jogo (não os de tela).
    - `game_overrides_mksm.cpp`: as variáveis globais do arquivo.
    - **Não grave** (e anote o motivo): contadores de log (`*LogCount`, `s_unknownCounts`), `ps2_pad.cpp` (entrada do PC), `ps2_vu1_thread.cpp` (fila vazia depois do `sync`), `ps2_spu2_stream.cpp` (só esvaziar), `Deci2.cpp` (depuração).
  - **Verifique:** salvar e carregar no menu de pausa, na gameplay e logo depois de um save do jogo no memory card. Durante um filme, a recusa aparece com o motivo.
  - **Pronto quando:** os quatro casos funcionam.
- [ ] **1.9 Teste de resistência e documentação**
  - **Faça:** com o usuário, um save state antes do santuário da Wu Shi, uma vez com cada save (slot 1, Liu Kang; slot 2, Johnny Cage).
  - **Verifique:**
    - dez carregamentos seguidos, entrando na transição a cada vez: o comportamento se repete;
    - carregar depois de fechar e abrir o jogo (mesmo executável);
    - vinte save e load seguidos na gameplay, sem travar;
    - um executável recompilado recusa o arquivo antigo, com o aviso.
  - **Pronto quando:**
    - 10 de 10 repetem a cena;
    - o `ARQUITETURA.md` ganha uma seção "Save state de depuração", com o ponto quieto, as condições, as seções, os ajustes depois de carregar e o que é recusado;
    - o `CLAUDE.md` lista `MKSM_SAVESTATE` e F5/F6 nos comandos.

**Se travar no caminho:** se um carregamento travar o jogo, compare a seção do subsistema da tarefa em que está. Faça o teste "salvar, carregar e salvar de novo" e compare os bytes das duas gravações por seção (um script em Python): a primeira seção diferente aponta o que não foi salvo ou não foi restaurado.

### Fase 2: trava na saída da Wu Shi (estimativa: meio dia a 2 dias, com o save state)

- [x] **2.1 O que o EE e os módulos de som trocam na transição** (diagnóstico temporário). **Feito (18/11):** depois do END006, cada banco novo recebe CRI `0x411`/`0x410` sem o `0xA0n` nem os `0x80D0` no meio; no muro, os `0x70n` respondem `0xfffffffe` (banco não carregado). Mapa completo no `ARQUITETURA.md` §8.
  - **Faça:** registre, para os serviços `0x534E4446` e `0x90000000`, e para a função `0x8000` do `0x53465356`, em cada chamada:
    - os primeiros 32 bytes do buffer enviado;
    - os primeiros 32 bytes da resposta, depois que a chamada termina;
    - o modo (síncrona ou assíncrona) e se a função de fim (`endFunction`) foi chamada.
    Registre também as transferências SIF DMA do EE para o IOP (endereço, tamanho) e os comandos SIF do IOP para o EE.
  - **Verifique:** compare a gameplay normal antes do muro com a hora da transição e com o estado parado.
  - **Pronto quando:** está anotado aqui o que o jogo pede na transição e o que a resposta da função `0x8000` traz a cada quadro no estado parado (o campo que ele confere).
- [x] **2.2 O lado do IOP**. **Feito (18/11):** nenhuma thread do IOP fica presa; quem desiste é o EE. Nos despejos de RAM, os bancos 4 a 6 têm o buffer do IOP zerado: a reserva de 32 KB (`sceSifAllocIopHeap`) falhou.
  - **Faça:** no emulador do IOP, registre o que as threads do `SNDFI.IRX` e do ADX fazem depois dos pedidos da transição: em que semáforo ou event flag cada uma fica esperando (`IopKernel`), que importações chamam e se alguma leitura ou transferência que elas pedem nunca termina.
  - **Verifique:** a thread que deveria preparar o som da cutscene e o ponto onde ela para.
  - **Pronto quando:** a espera que nunca acaba está identificada (quem espera, o quê, e quem deveria sinalizar).
- [x] **2.3 Achar a diferença para o PS2**. **Feito (18/11):** o `sceSifFreeIopHeap` do jogo nunca liberava (a flag `0x564084` fica em -1 porque o `Init` é stub) e o `IopMemory::allocate` não reaproveitava espaço; o heap do IOP acabava depois de ~8 bancos. Anotado no `ARQUITETURA.md` §5 ("Heap do IOP") e §8.
  - **Faça:** com o que 2.1 e 2.2 mostrarem, confira no código do runtime o pedaço envolvido (SIF DMA, RPC, `IopCdvd`, `iop_kernel`, o stream do SPU2) contra o comportamento documentado do PS2 (pcsx2, ps2sdk).
  - **Verifique:** escrevendo a resposta certa na mão (diagnóstico) a transição segue.
  - **Pronto quando:** a causa está anotada aqui e no `MODDING.md`.
- [ ] **2.4 Corrigir**. **Testado em 18/11: a trava continua.** Os bancos de som agora carregam (depois do END006 e no muro, todos com resposta 0) e o heap do IOP é reaproveitado; essa falha era real, mas não era a causa. No muro: a tela de LOADING (CT16, página 0, pelo caminho da CPU) fica ~8 s a 1–9 quadros/s, as leituras do disco terminam, e o jogo volta a 58–60 quadros/s com `0x4F4B68` = 1 e a cena desenhada (VU1 ~16 ms e GPU 39% por quadro). Mesmo assim o personagem não cai e a cutscene não começa: a lógica espera algo que os registros não mostram. Descartados: banco de som, resposta do `SFSV` (zerada é normal: o jogo nunca liga os avisos, funções `0x1700`/`0x1b00` do SNDF) e sala vazia. **Próximo:** o save state da fase 1, gravado logo antes do muro, para repetir a transição sem o usuário; no estado travado, RAM (F7), gravação (F9) e perfil (F8). Correção anterior, que fica: `game_overrides_mksm.cpp` liga 0x485540 ao stub que libera, e o `allocate` procura o primeiro espaço livre. No jogo, até o Goro's Lair, os buffers voltam sempre para `0x1a4f80` e os bancos carregam; R3 com 461/3.
  - **Faça:** a correção vai no subsistema achado em 2.3 (stub, IOP, CD, filme), não no jogo. Se for mesmo coisa do jogo, use um override no `game_overrides_mksm.cpp`.
  - **Verifique:**
    - a partir do save state, a transição passa 10 de 10 vezes, com os dois lutadores;
    - jogar 10 minutos na área seguinte;
    - R3, R4 e R4b.
  - **Pronto quando:** o usuário passa da Wu Shi jogando normalmente.
- [ ] **2.5 Jogo se arrastando a 1 quadro/s (visto com o Liu Kang)**
  - **Faça:** se o arrasto voltar depois de 2.4, as pilhas automáticas mostram que thread segura as outras. Corrija.
  - **Verifique:** três passagens pela transição sem arrasto.
  - **Pronto quando:** sem arrasto, ou com a causa anotada se não for reproduzível.

### Fase 3: HUD sumindo (barras de vida e energia)

- [ ] **3.1 O jogo desenhou as barras?** **Achado (18/11):** nas 3 gravações F9 da floresta da Wu Shi, o jogo só manda o texto do HUD; a moldura e as barras não estão no fluxo do GS (CPU e GPU iguais), caso (a). Num dos casos, porém, a janela perdeu até o texto que a gravação tinha, possível caso (c), que a reprodução não pega porque começa com o cache da GPU vazio. Teste de 18/11: F6 (descarta os caches da GPU) não mudou nada, então não é o cache: o HUD e o fogo faltam já no que chega ao GS, e a causa está antes (EE ou VU1). **Atenção:** o F6 de diagnóstico ocupa a tecla prevista para carregar o save state (fase 1); tire-o antes de implementar a fase 1. Os diagnósticos dessa investigação ficaram fora do git, em `portatil/diag_temp.diff` (aplique na pasta `PS2Recomp` com `patch -p1`).
  - **Faça:**
    - nas 3 gravações F9 do Liu Kang, procure com o `gsr_seq` os sprites do HUD (canto superior esquerdo, desenhados depois da cena) e compare com uma gravação em que as barras aparecem (`gsrec/hud.gsr`);
    - reproduza as gravações pela CPU e pela GPU.
  - **Verifique:** imagens das duas reproduções lado a lado.
  - **Pronto quando:** está decidido entre:
    - (a) o jogo não desenhou as barras;
    - (b) desenhou, mas elas ficaram escondidas (ordem, alfa ou brilho por cima);
    - (c) só a GPU erra.
- [ ] **3.2 O jogo original esconde as barras?**
  - **Faça:** na gameplay, veja se as barras voltam ao bater num inimigo ou ao levar dano. Teste também com "Efeitos" desligado no menu Gráfico.
  - **Verifique:** com o save state, a mesma cena com e sem "Efeitos".
  - **Pronto quando:** está anotado se é comportamento do jogo (nada a corrigir) ou erro.
- [ ] **3.3 Corrigir, se for erro**
  - **Faça:** a correção no renderizador ou no runtime.
  - **Verifique:** as 3 gravações mostram as barras. Acrescente uma gravação do caso à R4b, com o hash novo.
  - **Pronto quando:** o HUD não some mais na gameplay.

### Fase 4: textura de fundo parada

- [ ] **4.1 Descrição exata**
  - **Faça:** com o usuário, anote:
    - qual objeto fica parado (céu, fundo distante, água…);
    - se ele acompanha a câmera;
    - em que lugar do mapa acontece.
    Peça F9 e F10 no momento e um save state ali.
  - **Pronto quando:** a descrição e as capturas estão aqui.
- [ ] **4.2 CPU ou GPU**
  - **Faça:**
    - reproduza a gravação pela CPU e pela GPU e compare as imagens;
    - com o `gsr_seq`, ache a textura do objeto (`tbp`) e veja se ela é atualizada a cada quadro (transferência ou desenho na própria textura).
  - **Verifique:** se só a GPU erra, é a invalidação do cache de texturas do `GSHwBackend`. Se as duas erram, o problema vem antes (EE/VU).
  - **Pronto quando:** a causa está anotada.
- [ ] **4.3 Corrigir**
  - **Verifique:** a gravação reproduz certo, R4b igual (ou com o hash novo justificado) e, no jogo, o fundo se move no lugar.
  - **Pronto quando:** o usuário confirma no lugar.

### Fase 5: escurecimento quando algo passa na frente da câmera

- [ ] **5.1 Capturar e separar**
  - **Faça:** F9 e F10 no momento e um save state ali. Reproduza pela CPU e pela GPU. Teste com "Efeitos" desligado (o brilho lê a profundidade e a imagem do quadro).
  - **Pronto quando:** está anotado se é efeito do jogo, erro do brilho ou erro da GPU.
- [ ] **5.2 Corrigir**
  - **Verifique:** reprodução e jogo certos, R4b.
  - **Pronto quando:** o usuário confirma.

### Fase 6: janela travada nas telas de carregamento (o que sobrou da fase 0)

- [ ] **6.1 Não esperar a fila para mostrar a imagem** **Feito em parte (18/11):** o `Present` mostra a última imagem do espelho enquanto o GS está ocupado (até 250 ms). R5: janela no carregamento da fase de 39 para 50,5 quadros/s; quedas aos 2 s (54) e aos 42 s (53,5) sumiram; jogo na cena 52,5 → 52,8 quadros/s (igual); END006 com 60 imagens novas/s antes e depois; R4b igual. Falta a queda da virada para a fase (50,5), que não passa por essa espera.
  - **Faça:** no `GSHwBackend::Present`, no caminho sem superfície da GPU (`presentFailed`), mostre a última imagem quando a fila estiver ocupada, em vez de esperar com `waitIdle`. Mas espere se já tiverem passado 250 ms, para os filmes não congelarem.
  - **Verifique:**
    - janela ≥ 55 fps no carregamento do Goro's Lair (R5);
    - os filmes `MIDWAY`, `OPENING` e `END006` sem travar a imagem;
    - R4b igual.
  - **Pronto quando:** a janela não cai abaixo de 55 fps em nenhum carregamento.

**No fim de cada fase:** tire os `DIAG_TEMP` que não servem mais, atualize o `ARQUITETURA.md` e faça o commit da fase.
