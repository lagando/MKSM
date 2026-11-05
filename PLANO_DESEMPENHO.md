# Plano de desempenho

Plano só de velocidade, separado do `PLANO.md`. A arquitetura (threads, filas, pontos de espera) está no `ARQUITETURA.md`; o histórico de medições, no fim deste arquivo.

## Onde estamos (05/11)

| Tela | Quadros/s | Real do PS2 | O que limita |
|---|---|---|---|
| Menus | **60** | 60 | nada (teto) |
| Goro's Lair, cutscene | 14–37 | 60 | GPU e thread do jogo, alternando |
| Goro's Lair, gameplay | **25–29** (~45%) | 60 | a GPU executando o `gs_tiles.comp`; logo atrás, a thread do jogo |

Medido com GPU ligada (padrão desde 05/11; com `PS2X_GS_GPU=0`, a fase cai para ~7,5). A última mudança (CLUT sem esperar a GPU, replay `fase2` 51 → 44 ms) ainda não foi medida ao vivo.

**Uso de CPU por thread na gameplay** (`/proc/<pid>/task`, 05/11): GameThread 85%, GSWorker 29% (o resto esperando a GPU), GSFrontend 21%, GSBand 18%.

**Para dobrar a gameplay é preciso as duas coisas:** GPU mais rápida no desenho **e** tirar trabalho da thread do jogo. Uma sem a outra dá no máximo ~31–32 quadros/s.

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
- ⬜ **Atalho direto para a fase** (`Level Select`/`Warp Player` do jogo, `MODDING.md`): cortaria ~4 min de cada teste ao vivo e acabaria com a falha do script na seleção de personagem.
- ⬜ **Gravação da gameplay** (`fase3.gsr`): a `fase2` é da cutscene; a gameplay tem outro perfil de desenho.

## Fase 1: ganhos rápidos na CPU

- ✅ Janela sem bloquear (`try_lock`) e cópia do campo na troca `0x00`↔`0x46`: janela a 60 Hz.
- ✅ Logs fora do caminho quente (`AGRESSIVE_LOGS` desligado).
- ⬜ Validar o pulo do laço de espera (`idle_loops`, `sub_00274688` aparece com ~2–5% da GameThread).
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
- ⬜ Os 800 sprites de feedback por campo da `fase2` (cópia de Z24 sobre área sobreposta, 33×33 px) ainda vão para a CPU e esperam a GPU (~40 esperas por campo). Levar para a GPU com uma cópia do retângulo de origem antes.
- ⬜ Linhas e pontos ainda na CPU (não aparecem na `fase2`).
- ⬜ Apresentação direto da GPU (hoje a janela recebe uma cópia da VRAM).

## Fase 3: thread do jogo, sem mudar a arquitetura

- ✅ IOP: fila de DMA só quando vence, imports só em `jr $ra`, lotes de 16384 ciclos, AutoDMA no ritmo real, `step()` enxuto (~30% → ~8% da GameThread).
- ✅ VU1 JIT: callouts para `CLIP`/`DIV`/EFU/`FC*`/`ILW`/`ISW`, leitores de Q no meio do bloco (com variante sincronizada), `XGKICK`/`XTOP` no JIT, desvios, tabelas por microprograma, `readyMax`; pares interpretados 62 → 6 milhões em 3 min. Conferidor `PS2X_VU_JIT_CHECK` sem diferença.
- ✅ Frontend do GS na thread `GSFrontend` (tirou ~21% da GameThread; ganho em quadros/s só depois que a GPU deixar de ser o limite).
- ⬜ **Custo fixo de entrada/saída dos blocos do JIT** (`runJitBlock` ~10%, código do JIT ~8,5%): blocos maiores, menos conferências por bloco.
- ⬜ **IOP: HLE das funções do LIBSD que o CRI chama em polling** (`sceSdBlockTransStatus` e companhia): corta boa parte dos ~8%.
- ⬜ **libc na GameThread** (mutex/memcpy/malloc ~8,7%): o mutex por primitivo saiu com o `GSFrontend`; medir de novo o que sobrou.

## Fase 4: GPU mais rápida (o próximo ganho grande)

Hoje o `GSWorker` passa ~70% do tempo esperando a GTX 1050 terminar o `gs_tiles.comp`. Mais threads na CPU não ajudam: a fila já está cheia, quem demora é a GPU.

- ✅ **Onde vai o tempo na `fase2`** (replay, 44,8 ms/campo, medido trocando partes do shader): o desenho do shader é ~24,5 ms; o resto (~20 ms) é decodificar, montar lotes e despachar (com o shader vazio: 20,3 ms). Dentro do shader, a leitura de textura (bilinear: 4 leituras na VRAM com swizzle e 4 da CLUT) custa ~10 ms e a escrita do pixel ~5 ms. O Z em `double` (temia-se o fp64 lento da GTX 1050) e a janela de colunas por pixel quase não pesam (0,5 e 2 ms); `restrict` nos buffers não muda nada.
- ✅ **Tiles de 8×8** em vez de 16×16: 44,8 → 42,8 ms, bit a bit igual.
- ⬜ **Cache de textura decodificada na GPU:** decodificar a textura (swizzle + CLUT) uma vez por upload/CLUT num buffer linear RGBA e amostrar dele; invalidar por página escrita. É o maior pedaço que sobra no shader (~10 ms dos ~22).
- ⬜ **Triângulos pelo rasterizador da GPU** (vertex/fragment), com o pixel do GS em aritmética inteira no fragment shader e a VRAM continuando como buffer. É o ganho maior e o risco maior: a cobertura de pixel e a interpolação têm de bater bit a bit com a CPU. Validar com os cinco replays.
- ⬜ **Sobrepor CPU e GPU de verdade:** com menos esperas, o `GSWorker` pode montar o lote seguinte enquanto a GPU desenha o atual (três slots em vez de dois).

Meta: GS fora do caminho na gameplay (jogo esperando o GS ~0%).

## Fase 5: paralelismo da thread do jogo

Depois da Fase 4, a GameThread (EE + VU1 + IOP em série) é o teto. **Regra para todas as mudanças desta fase:** o resultado tem de ser determinístico, igual a cada execução com as mesmas entradas. Isso é obrigatório para o online P2P (coop e versus, em lockstep ou rollback): um IOP ou VU1 que termina "quando der" muda o estado do jogo entre as máquinas. Paralelizar só com pontos de sincronização fixos, como já é feito no `GSFrontend`.

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
- **CLUT sem esperar a GPU (05/11):** 339 de 379 esperas vinham de cargas de CLUT presas a uploads; intervalo exato dos uploads e espelho no host: 379 → 139 esperas, `fase2` 51 → 44 ms. O tempo total de espera quase não caiu (686 → 550 ms em 888): o resto é a GPU executando.
