# Plano de desempenho

Plano só de velocidade, separado do `PLANO.md`. A arquitetura (threads, filas, pontos de espera) está no `ARQUITETURA.md`; o histórico de medições, no fim deste arquivo.

## Onde estamos (09/11)

| Tela | Quadros/s | Real do PS2 | O que limita |
|---|---|---|---|
| Menus | **60** | 60 | nada (teto) |
| Goro's Lair, cutscene | 20–33 | 60 | o GS (ocupado ~100%, jogo esperando o GS até 50%) |
| Goro's Lair, gameplay | **35–40** (média 38, ~64%) | 60 | o GS (ocupado ~97%); a thread do jogo logo atrás |

Medido com GPU ligada (padrão desde 05/11; com `PS2X_GS_GPU=0`, a fase cai para ~7,5). **Em 09/11, com o cache de texturas:** gameplay 27,5–30 quadros/s e o jogo esperando o GS 0%. A GPU já não limita: o próximo ganho é a thread do jogo (Fase 5). **Depois do JIT nativo de desvios/`ILW`/`ISW` e do Z reduzido na GPU (09/11):** gameplay 31,5–35 quadros/s, GS ocupado 89–99%: GPU e thread do jogo voltaram a empatar.

**Uso de CPU por thread na gameplay** (`/proc/<pid>/task`, 05/11): GameThread 85%, GSWorker 29% (o resto esperando a GPU), GSFrontend 21%, GSBand 18%.

**Para dobrar a gameplay é preciso as duas coisas:** GPU mais rápida no desenho **e** tirar trabalho da thread do jogo. Uma sem a outra dá no máximo ~31–32 quadros/s.

## Plano para a gameplay (10/11)

**Por que o PCSX2 é mais rápido na mesma máquina:** (1) desenha triângulos com o rasterizador da GPU (pipeline gráfico), não com um compute shader por tile; (2) gasta pouco CPU por primitivo; (3) roda a VU1 numa thread própria (MTVU). Nós fazemos o contrário nos três. E como o vsync é preso por ciclos, cada quadro lento vira câmera lenta: a 38 quadros/s o jogo anda a ~63% da velocidade.

**Hoje dois limites empatam (~38 quadros/s):** o GS (ocupado ~97%) e a GameThread logo atrás. Por campo pesado (replay da `fase2`, ~31 ms): ~12 ms esperando a GPU e ~19 ms de CPU no `GSWorker` (`QueuePrimitive` ~23%, `PrepareGpuPrimitive` ~19%, driver ~12%). Na GameThread: VU1 ~30–45%, IOP ~12%, laço do vsync ~7%. Só chega a 60 atacando os dois.

Ordem, do maior ganho por esforço ao maior risco:

1. ✅ **Medir a gameplay de verdade (10/11).** `gsrec/fase3.gsr`: 20 campos de gameplay gravados aos 270 s (Liu Kang no Goro's Lair), 182 MB; replay 27,2 ms/campo pela GPU (36,8/s, igual ao vivo), 91,5 ms pela CPU, hash `65173649eaffbb7a` nos dois. Descobertas:
   - **A GPU é o limite principal.** Medido com timestamps do Vulkan (`[perf] GPU ocupada`): ao vivo na gameplay 72–84% ocupada, ~21 ms de GPU por quadro a 38/s; no replay 81–84%. Para 60/s a GPU precisa caber em 16,6 ms.
   - O `GSWorker` gasta só ~8,6 ms de CPU por campo no replay (32% de uma thread): `QueuePrimitive` 28%, `PrepareGpuPrimitive` 22%, driver 20%, `WorkerLoop` 9%. Os ~20% em que a GPU fica parada são esses buracos: cortar CPU por primitivo sozinho dá no máximo ~38/0,8 ≈ 47/s.
   - Esperas ao vivo em 290 s (contadas por ponto de `Flush`): cópia do campo completo 10651× / 65,6 s, carga de CLUT 9089× / 21,8 s, o resto desprezível. A cópia do campo agora é assíncrona (`QueueCapture`, ARQUITETURA §3): sozinha não mudou a gameplay (36,8/s, 34–39), porque o worker passa a esperar ao reusar o slot, com a GPU ocupada; mas é pré-requisito do passo 2.
   - **Nova ordem:** o ganho grande agora é **desenho mais barato na GPU** (passo 4, ou cortar o custo do `gs_tiles.comp`); o passo 2 vem junto para fechar os buracos. A espera da CLUT (21,8 s) entra no passo 2.
2. **GS: CPU por primitivo 5–8× menor.** Hoje cada triângulo vira um registro de 256 bytes montado e copiado para a memória da GPU, com varreduras de conflito por primitivo.
   - Estado do desenho (TEX0, ALPHA, TEST, FRAME...) num bloco à parte, deduplicado; o triângulo só guarda vértices (~32 bytes) e o índice do estado.
   - Tiras e leques de triângulos como um registro só (vértices compartilhados), em vez de um por triângulo.
   - Conferência de conflitos por mudança de estado, não por primitivo.
   - Três slots de envio: a CPU monta o lote seguinte enquanto a GPU desenha.
   - Meta: CPU do worker de ~19 para ~5 ms por campo pesado; continua bit a bit igual nos replays.
3. **VU1 numa thread própria (como o MTVU do PCSX2).** O VIF1 entrega o microprograma e a GameThread segue; ela só espera onde lê o resultado (memória/registradores da VU1, `VIF1_STAT`, `FLUSH*`, `CHCR` do canal 1). Determinístico: a ordem dos pacotes ao GIF continua a do fluxo VIF1. Tira ~30–45% da GameThread.
4. **GPU: triângulos pelo rasterizador da GPU.** Vertex/fragment shader para os casos comuns (triângulo texturizado com blend e Z), o compute atual só para o resto. É o maior ganho de GPU (o shader hoje leva ~12 ms por campo na GTX 1050) e o maior risco: cobertura de pixel e interpolação precisam bater com a CPU; se não baterem bit a bit, decidir uma tolerância medida nos replays.
5. **IOP mais barato:** HLE das funções de som que o CRI consulta em laço (`sceSdBlockTransStatus` e companhia) ou IOP em thread própria com fatias de tempo emulado. ~12% da GameThread.

Expectativa: 1+2 tiram o GS do caminho (~45 quadros/s, limitado pela GameThread); com 3, ~55–60; 4 e 5 dão a folga para os trechos pesados e para a cutscene.

## Como medir (antes de qualquer mudança)

- `PS2X_PERF=1`: uma linha a cada 2 s (janela, imagens novas, quadros do jogo, vsync, GS ocupado, jogo esperando o GS). "GS ocupado" conta tempo de parede, inclusive esperando a GPU.
- `PS2X_PROFILE=<s> PS2X_PROFILE_THREAD=<nome>`: perfil por amostragem só de CPU (esperas não aparecem). Threads: `GameThread`, `GSFrontend`, `GSWorker`, `GSBand`.
- Replay: `ps2x_gs_replay gsrec/<x>.gsr --repeat 3` (tempo por campo) e `--compare` contra a VRAM da CPU (`PS2X_GS_GPU=0 ... --dump-vram`). Toda mudança no GS precisa de **0 bytes de diferença** em `titulo`, `menu`, `dificuldade`, `fase` e `fase2` (hash conhecido de `fase2`: `edef4ac4b8f25a78`).
- Ao vivo até a gameplay: `MKSM_SKIP_INTRO=1 MKSM_AUTO_PAD=xs MKSM_AUTO_PAD_UNTIL=40 MKSM_AUTO_HOLD="x@45-45.3,x@50-50.3,...,x@120-120.3"`; a gameplay começa ~95 s depois da fase carregar (~250 s de execução). A seleção de personagem às vezes demora e o script perde os toques.
- Máquina livre: conferir `pgrep -a ps2EntryRunner` e `top` antes (outro processo pesado já falseou várias medições). Com a tela travada a janela não abre.

## Fase 0: medir rápido

- ✅ `ARQUITETURA.md` e `CLAUDE.md`/`AGENTS.md` com o mapa do sistema.
- ✅ Gravação e replay do GS (`PS2X_GS_RECORD`, `ps2x_gs_replay`), cinco gravações em `gsrec/` (fora do git).
- ✅ Suítes de teste por nome (`ps2x_tests gs vu1 ...`).
- ✅ Medidor `PS2X_PERF` e perfil por thread com threads nomeadas.
- ⬜ **Pular a cutscene de abertura do Goro's Lair.** Medido em 09/11: o jogo já entra no nível aos ~46 s (`func_1BB8A8(0x60)`) e o 3D começa aos ~56 s; o que pesa no teste é a cutscene, até ~120–145 s. Ela não sai com Start. O `Level Select` do jogo (`MODDING.md`) só pula os menus, que já são rápidos. Falta achar o que controla a cutscene (fim forçado ou flag de "já vista").
- ⬜ **Gravação da gameplay** (`fase3.gsr`): a `fase2` é da cutscene; a gameplay tem outro perfil de desenho.

## Fase 1: ganhos rápidos na CPU

- ✅ Janela sem bloquear (`try_lock`) e cópia do campo na troca `0x00`↔`0x46`: janela a 60 Hz.
- ✅ Logs fora do caminho quente (`AGRESSIVE_LOGS` desligado).
- ✅ Pulo do laço de espera (09/11): os dois endereços antigos nunca rodavam na gameplay; o laço quente era o poll do FIELD do GS CSR em `0x2747a4`. Com ele em `idle_loops` e o pulo limitado ao próximo evento dos timers da EE: gameplay média 36,25 → 38,2 quadros/s (35–40), GS ocupado ~97%: o GS volta a ser o limite. O vsync na gameplay é preso por ciclos (EE a ~60% da velocidade, câmera lenta), então quadros/s depende do tempo de host por ciclo emulado.
- ⬜ Cache de texturas decodificadas (`patches/wip_cache_texturas.diff`): sem ganho medido; só fica se o replay provar.
- ⬜ Upload por linhas no `UploadImageLocked`: perdeu importância (uploads de textura vão para a GPU); só os pequenos ficam na CPU.

## Fase 2: desenho na GPU (Vulkan compute)

- ✅ VRAM do GS num buffer da GPU, swizzle no shader, CPU como plano B.
- ✅ Sprites (inclusive bloom que lê o próprio alvo), triângulos com o pixel completo, todos os formatos de textura, CLUT com versões, bilinear, Z, blend, fog.
- ✅ VRAM na memória da GPU sincronizada por faixas; envio sem espera em dois slots; uploads e cópias local→local na GPU.
- ✅ Teste `gs` que compara CPU e GPU byte a byte em cada regra de espera.
- ✅ GPU ligada por padrão (`PS2X_GS_GPU=0` desliga; sem Vulkan cai na CPU).
- ✅ Esperas por CLUT: intervalo exato de blocos dos uploads e upload pequeno espelhado no host (379 → 139 esperas por campo na `fase2`).
- ⬜ Medir ao vivo a mudança da CLUT.
- ✅ Os 800 sprites de feedback por campo da `fase2`: já vão para a GPU (conferido em 09/11 com contador: nenhum primitivo da `fase2` cai na CPU).
- ✅ CPU do `GSWorker` no replay (09/11): por campo da `fase2`, ~12 ms dos ~33 são espera pela GPU (3 flushes); o resto é CPU do worker, que é o gargalo do replay. `QueuePrimitive` varria o mapa do cache de texturas duas vezes por primitivo e refazia o hash da CLUT de 16 em 16 bits: com o limite da faixa e o hash por 64 bits, `fase2` ~33 → ~30,7 ms, bit a bit igual. Sobram `QueuePrimitive` ~23% (divisão do `unordered_map`, cópia do registro para a memória mapeada), `PrepareGpuPrimitive` ~19% (faltas de cache lendo o `batch` que outra thread escreveu) e driver ~12%. Tentados sem ganho e desfeitos: agrupar 64 draws por lock na fila do `GSCpuBackend` (o mutex custava ~9% da thread produtora, mas ela não é o gargalo) e prefetch dos comandos no worker (medida inconclusiva com a máquina carregada).
- ✅ Redução do Z da fase na GPU (09/11): o intervalo da textura do sprite ia até a altura do `TEX0` e parecia ler o próprio alvo; `fase2` 38,3 → 35,5 ms, nenhum primitivo da `fase2` na CPU.
- ✅ Intervalo de blocos pelo retângulo e formato (09/11): uploads, cópias e a carga de CLUT (CSM1) usam os blocos que as células ocupam, não a página inteira nem `CBP..+8`. Esperas por CLUT na `fase2` 540 → 120 em 60 campos (sobra 1 por campo: upload duplo real de uma CLUT CT16 em `0x2850`), `fase2` 35,3 → 30,0 ms, cinco replays bit a bit iguais. Ao vivo a gameplay não muda (~27–34 quadros/s nas duas versões, média 31), porque quem limita é a thread do jogo; "GS ocupado" cai de ~91% para ~82%.
- ⬜ Espera do fim de campo (`flushAll` antes de copiar a VRAM completa): ~9,5 ms por campo na `fase2`, é a GPU executando.
- ⬜ Linhas e pontos ainda na CPU (não aparecem na `fase2`). Na gameplay sobram ~20 sprites/s CT16 que leem o próprio alvo.
- ⬜ Apresentação direto da GPU (hoje a janela recebe uma cópia da VRAM).

## Fase 3: thread do jogo, sem mudar a arquitetura

- ✅ IOP: fila de DMA só quando vence, imports só em `jr $ra`, lotes de 16384 ciclos, AutoDMA no ritmo real, `step()` enxuto (~30% → ~8% da GameThread).
- ✅ VU1 JIT: callouts para `CLIP`/`DIV`/EFU/`FC*`/`ILW`/`ISW`, leitores de Q no meio do bloco (com variante sincronizada), `XGKICK`/`XTOP` no JIT, desvios, tabelas por microprograma, `readyMax`; pares interpretados 62 → 6 milhões em 3 min. Conferidor `PS2X_VU_JIT_CHECK` sem diferença.
- ✅ VU1 JIT, callouts de dentro dos blocos (09/11): por 10 s de gameplay eram ~14 M de correções exatas de MADD, ~12 M de desvios, ~5,5 M de pares que gravam o VI antigo e ~3,4 M de `ILW`/`ISW`. Agora: correção 18, gravação 0, desvios 4 M (os do primeiro par do bloco), `ILW`/`ISW` 0. Sobram `DIV` 7,3 M, `FCAND` 3,7 M, `CLIP` 3,7 M. Conferidor: 0 diferenças em 17,8 M de blocos.
- ⬜ `IBNE` no primeiro par do bloco (3,9 M/10 s) e `DIV` (7,3 M/10 s) nativos.
- ✅ Frontend do GS na thread `GSFrontend` (tirou ~21% da GameThread; ganho em quadros/s só depois que a GPU deixar de ser o limite).
- ⬜ **Custo fixo de entrada/saída dos blocos do JIT** (`runJitBlock` ~10%, código do JIT ~8,5%): blocos maiores, menos conferências por bloco.
- ⬜ **IOP: HLE das funções do LIBSD que o CRI chama em polling** (`sceSdBlockTransStatus` e companhia): corta boa parte dos ~8%.
- ⬜ **libc na GameThread** (mutex/memcpy/malloc ~8,7%): o mutex por primitivo saiu com o `GSFrontend`; medir de novo o que sobrou.

## Fase 4: GPU mais rápida (o próximo ganho grande)

Hoje o `GSWorker` passa ~70% do tempo esperando a GTX 1050 terminar o `gs_tiles.comp`. Mais threads na CPU não ajudam: a fila já está cheia, quem demora é a GPU.

- ✅ **Onde vai o tempo na `fase2`** (replay, 44,8 ms/campo, medido trocando partes do shader): o desenho do shader é ~24,5 ms; o resto (~20 ms) é decodificar, montar lotes e despachar (com o shader vazio: 20,3 ms). Dentro do shader, a leitura de textura (bilinear: 4 leituras na VRAM com swizzle e 4 da CLUT) custa ~10 ms e a escrita do pixel ~5 ms. O Z em `double` (temia-se o fp64 lento da GTX 1050) e a janela de colunas por pixel quase não pesam (0,5 e 2 ms); `restrict` nos buffers não muda nada.
- ✅ **Tiles de 8×8** em vez de 16×16: 44,8 → 42,8 ms, bit a bit igual.
- ✅ **Cache de textura decodificada na GPU** (09/11): `fase2` 42,5 → ~37 ms, os cinco replays bit a bit iguais à CPU. Ao vivo, a gameplay foi de 25–29 para 27,5–30 quadros/s, com "jogo esperando o GS" em 0%: agora quem limita é a thread do jogo. O que custava era a corrente de leituras dependentes (tabela → VRAM → CLUT), não o número delas (CLUT em 32 bits sozinha: 0 ganho). Na `fase2`, chaveando pelo conteúdo da CLUT: ~1100 decodificações em 20 campos (73 M texels), contra 245 M leituras de textura. Pela versão da CLUT seriam 30 mil (1,7 G texels). Detalhes no `ARQUITETURA.md` §3.
- ⬜ **Triângulos pelo rasterizador da GPU** (vertex/fragment), com o pixel do GS em aritmética inteira no fragment shader e a VRAM continuando como buffer. É o ganho maior e o risco maior: a cobertura de pixel e a interpolação têm de bater bit a bit com a CPU. Validar com os cinco replays.
- ⬜ **Sobrepor CPU e GPU de verdade:** com menos esperas, o `GSWorker` pode montar o lote seguinte enquanto a GPU desenha o atual (três slots em vez de dois).

Meta: GS fora do caminho na gameplay (jogo esperando o GS ~0%).

## Fase 5: paralelismo da thread do jogo

Depois da Fase 4, a GameThread (EE + VU1 + IOP em série) é o teto. **Regra para todas as mudanças desta fase:** o resultado tem de ser determinístico, igual a cada execução com as mesmas entradas. Isso é obrigatório para o online P2P (coop e versus, em lockstep ou rollback): um IOP ou VU1 que termina "quando der" muda o estado do jogo entre as máquinas. Paralelizar só com pontos de sincronização fixos, como já é feito no `GSFrontend`.

- **Perfil da GameThread na gameplay (09/11, com o cache de texturas, `PS2X_PROFILE=200`):** VU1 ~50% (código do JIT 10%, `runJitBlock` 8%, pares no interpretador ~15%: `execLower`, `calculateFmacExactResult`, `normalizeFmacResult`, `execUpper`, decodificação; `progressXgkick` 4%), código do jogo 13% (dos quais 4,8% no laço de espera do vsync `0x274688`), IOP 8%, VIF1 3%, e ~11% na `libc`. Na `libc`, a maior parte cai logo depois de um `syscall` de futex no meio de `pthread_cond_signal`, mas um `LD_PRELOAD` que contava as chamadas viu zero vindas da GameThread: a origem continua aberta (o `addr2line` na `libc` sem símbolos dá o exportado mais próximo). Tentado sem ganho medível e desfeito: acordar o `GSFrontend` só quando ele dorme e tirar o `notify_one` do `scheduleEvent`.
- ✅ **Fila do `GSFrontend` sem lock (09/11):** a origem da `libc` na GameThread era o `gsAsyncSubmit` (achado com a varredura de pilha nova do perfil: `gsAsyncSubmit ← GifArbiter::drain ← progressXgkick`, ~7% em `futex`/mutex). Trocada a fila com mutex e `notify_one` por um anel de bytes com espera ativa curta no frontend. Gameplay ao vivo: média 31 → **36 quadros/s** (30,5–39), `libc` 12,8% → 6,6% da GameThread. O GS voltou a ficar perto do limite (ocupado 88–99%).
- **Perfil da GameThread depois disso (09/11):** VU1 ~30% (`runJitBlock` 8,4%, código do JIT 7,7%, `progressXgkick` 4,1%, `execLower` 2,1%, `run` 2,1%, `commitReadyPipelines` 1,8%), IOP ~11,6% (`executeInstruction` 7,7%, `runCycles` 3,9%), laço de espera do vsync `0x274688` 6,6%, `libc` 6,6% (sobram `memcpy` do pacote e `writeIORegister`), VIF1 4,5%, `read64` 3,3%, `checkpointDue` 2,9%.
- ⬜ **Laço de espera do vsync** (`sub_00274688`, 6,6%) e **IOP** (11,6%) são os próximos alvos baratos da GameThread; a GPU precisa cair junto (espera de fim de campo, Fase 4).
- ⬜ **VU1 numa thread própria** (~45% da GameThread com o JIT): o VIF1 entrega o microprograma e o EE só espera onde lê o resultado (VU1 memória/registradores, `VIF1_STAT`, o `CHCR` do canal 1). Medido: o jogo espera o canal 1 ~1,7 ms depois de cada envio de ~8 ms, então sobrepõe pouco sozinho (~15%); vale junto com a próxima.
- ⬜ **IOP numa thread própria** (~8%): ele avança em fatias de tempo emulado combinadas com o EE (por exemplo a cada vsync ou a cada RPC), nunca pelo relógio real.
- ⬜ **Executar o lote seguinte do EE enquanto a VU1 termina o anterior**, quando o jogo não lê nada da VU1 no meio.

Meta: gameplay a 60 quadros/s.

## Fase 6: rumo ao 100% nativo

Trocar o que hoje é emulado por código nativo, sem perder o comportamento do jogo. Cada troca precisa de um teste que compare com o emulado.

- ⬜ **Som:** o CRI ADX/AIX hoje roda dentro do IOP emulado (decodificação no EE, mistura no IOP, saída por AutoDMA). Um decodificador e mixer nativos no host eliminariam o IOP do caminho do som. Atenção às armadilhas já achadas (`notas/audio.md`): ordem L/R, criptografia do ADX, `CpuSuspendIntr`.
- ⬜ **IOP inteiro em HLE:** cartão de memória, controle e CD já são HLE em parte. Com o som nativo, o IOP restante pode virar funções do host.
- ⬜ **VU1:** os microprogramas do jogo são poucos (3 imagens trocadas ~170 vezes/s). Recompilá-los estaticamente, como o EE, tiraria o interpretador e o JIT.
- ⬜ **GS:** com a Fase 4 feita, o desenho já é nativo na GPU; falta só a apresentação direto.

## Histórico das medições

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
