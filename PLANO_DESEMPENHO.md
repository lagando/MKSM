# Plano de ação: desempenho (GPU, RAM e testes mais rápidos)

Plano separado do `PLANO.md`, só para velocidade. As medições vêm das runs 134 a 152 (seções 5A.17 do `PLANO.md`).

## Onde estamos

Visualmente: **~2 fps** no menu e na seleção de personagens, **~0,5 fps** na fase. A CPU sobe; a GPU e a RAM quase não são usadas.

O que as medições mostram:

| Tela | O jogo processa | O que limita |
|---|---|---|
| Menu / seleção | 17–22 vsyncs/s (~9 quadros do jogo/s) | Desenho em CPU: o worker do GS fica em 100% e o jogo passa ~50% do tempo esperando a fila |
| Fase | 14–20 vsyncs/s (~7–10 quadros/s) | Desenho em CPU (bloom + cenário) e a thread do jogo (IOP, VU1, laço de espera) |

**Por que a imagem parece ainda pior (2 fps / 0,5 fps):** a janela pega o mesmo lock (`m_stateMutex` do `GS`) que a thread do jogo segura durante um pacote GIF inteiro, inclusive enquanto espera a fila cheia do GS. Com o GS saturado, a janela fica presa junto e só atualiza de vez em quando. Esse é o problema visual número 1, e é barato de resolver.

**Por que a GPU fica parada:** todo o desenho do PS2 (o GS) é feito na CPU, pixel a pixel (`GSCpuBackend`). A GPU só recebe a imagem pronta para mostrar na janela.

**Por que a RAM não sobe:** não há nada que valha a pena guardar. Os 32 MB do PS2 e os 4 MB da VRAM do GS já cabem com folga. RAM extra só ajuda se usada como cache ou para gravar quadros, e é isso que este plano propõe.

## Fase 0: medir rápido (pré-requisito, ~1 dia)

Sem isso, cada teste leva 4–6 minutos de boot e as medições ficam ruidosas.

1. **Medidor permanente opcional** (`PS2X_PERF=1`), uma linha a cada 2 s:
   - vsyncs/s;
   - quadros do jogo/s;
   - quadros da janela/s;
   - ocupação do worker do GS;
   - % do tempo em que o jogo esperou a fila.

   Hoje esses números só existem como diagnóstico temporário.
2. **Gravar e reproduzir quadros (usa RAM e disco):**
   - **Gravar:** com `PS2X_GS_RECORD=arquivo`, gravar o fluxo GIF de N quadros (pacotes, uploads, CLUT) mais a VRAM inicial.
   - **Reproduzir:** `ps2x_gs_replay arquivo` desenha esses quadros sem bootar o jogo, em segundos.
   - Gravar um arquivo para cada caso: menu, seleção, Goro's Lair e bloom.
   - **Benchmark:** tempo por quadro de cada backend.
   - **Teste de regressão:** o hash da VRAM no fim tem de bater.
   - É a principal forma de deixar os testes rápidos.
3. **Rodar só uma suíte de testes:** `ps2x_tests gs` em vez de todas, para não editar o `main.cpp` toda vez.
4. **Atalho para a fase:** ativar o `Level Select`/`Warp Player` originais do jogo (ver `MODDING.md`) para cair direto em Goro's Lair, em vez de 2,5 min de menus com `MKSM_AUTO_PAD`.

### Estado da Fase 0 (03/11)

Prontos:
- **`ARQUITETURA.md`:** o mapa do sistema, mais `CLAUDE.md`/`AGENTS.md` mandando as IAs lerem o mapa primeiro.
- **Gravação/replay:** `GSRecordingBackend` e `ps2x_gs_replay` (uso em `ARQUITETURA.md` §6). O quadro é a troca entre os framebuffers `0x00`/`0x46`, um campo completo.
- **Suítes por nome:** `ps2x_tests gs vu1 ...`.
- **Medidor `PS2X_PERF`:** janela, imagens novas, campos do jogo, vsync, GS ocupado, jogo esperando o GS.

Faltam:
- **Atalho direto para a fase.**
- **Linha de base do replay com a máquina ociosa:** a última medição saiu com outra build compilando em paralelo; os números abaixo da primeira versão usavam outro recorte de quadro.

As gravações ficam em `gsrec/` (fora do git: contêm dados do jogo). Replay determinístico: o mesmo hash da VRAM em cada repetição. A tela gravada em cada momento varia entre execuções; os arquivos atuais são `menu.gsr` (menu principal), `titulo.gsr` ("Press START") e `fase.gsr` (Goro's Lair).

**Medido no jogo com `PS2X_PERF` (run172–174):**

| Tela | Janela | Imagens novas | Campos do jogo | vsync | GS ocupado | Jogo esperando o GS |
|---|---|---|---|---|---|---|
| Menu (antes) | 20–35 fps | 4–5/s | ~18/s | ~17/s | ~95% | ~55% |
| Menu (depois do item 1 e da cópia por campo) | **60 fps** | 6–9/s | ~18/s | ~17/s | ~100% | ~55% |
| Fase (antes) | 60 fps | 8–12/s | ~15/s | ~15/s | ~90% | 0–20% |
| Fase (depois) | 60 fps | **13–15/s** | ~15/s | ~15/s | ~90% | 0–20% |

**Leitura (corrigida):** os "2 fps" e "0,5 fps" visuais eram dois problemas de apresentação:
- **Janela travada:** ela ficava presa no lock do GS.
- **Cópia no meio do campo:** a cópia que ela mostra era tirada no meio de um campo e só em metade deles.

Os dois estão corrigidos: a janela mostra cada campo completo a 60 Hz. O que sobra é o ritmo do próprio jogo: ~15–18 campos/s, contra 60 no PS2. O desenho em CPU está saturado nas duas telas (GS ~90–100%). A primeira versão desta seção dizia que, na fase, o desenho não era gargalo; estava errado, porque o recorte antigo contava 3 "quadros" por campo.

## Fase 1: ganhos rápidos na CPU (~2–4 dias)

| # | Mudança | Ganho esperado | Estado |
|---|---|---|---|
| 1 | Janela sem bloquear: `try_lock` nos dois pontos de `latchHostPresentationFrame`; cópia do campo tirada na troca `0x00`↔`0x46` | Janela a 60 Hz; todo campo pronto chega à tela | **Feito (run174)**: janela 20–35 → 60 fps no menu; imagens novas 4–5 → 6–9/s no menu e 8–12 → 13–15/s na fase |
| 2 | Validar o pulo do laço de espera (`idle_loops` no `sm.toml`, `eeIdleCheckpoint`) | Libera a thread do jogo na espera de quadro | Já está no código; falta medir com a Fase 0 |
| 3 | Cache de texturas decodificadas (RGBA linear na RAM, invalidado por número de série de página da VRAM) | Menos custo no bilinear T8/T4. Só vale se o menu não reenviar as texturas a cada quadro | Em `patches/wip_cache_texturas.diff`; a única medição não mostrou ganho. **Manter só se o replay provar** |
| 4 | Upload de textura por linhas (`UploadImageLocked`, hoje pixel a pixel via ponteiro de função) | ~9% do worker no menu | A fazer |
| 5 | Sem logs em caminho quente (`sceSifSetDma`) | ~1–5% da thread do jogo | `AGRESSIVE_LOGS` já ficou desligado por padrão |

Meta da Fase 1: janela a 60 Hz; menu e seleção acima de 30 vsyncs/s.

## Fase 2: desenho na GPU (o ganho grande, ~3–6 semanas)

O código já tem a interface certa: `GSBackend` (`gs_backend.h`), com `Submit`, `UploadImage`, `ReadVram`, `Present` etc. Um `GSGpuBackend` entra no lugar do `GSCpuBackend` sem mexer no resto. A escolha vem de `PS2X_GS_BACKEND=gpu|cpu`, e o CPU continua como referência.

**Abordagem recomendada: rasterizador em compute shader sobre a memória local do GS**, não a rasterização "de hardware" do PCSX2.
- **Por quê:** o MKSM usa a mesma memória como Z e como cor. A página `0x8C` é o Z-buffer e também o rascunho do bloom. Ele também lê o próprio alvo (o bloom) e copia Z como textura (`0x12C`/`0x14C`). Com os 4 MB da VRAM do GS num buffer da GPU e o swizzle feito no shader, tudo isso continua exato e sem gambiarra. O PCSX2 precisa de vários hacks justamente por converter isso em texturas de GPU.
- **API:** Vulkan 1.2 com compute. É o melhor no Linux (Mesa) e portável. OpenGL 4.5 com compute também serve, se preferir reaproveitar o contexto do raylib.
- **Como desenhar:** em lotes, dividindo a tela em tiles (por exemplo 32×32 px). Cada workgroup processa os primitivos que tocam o seu tile, em ordem, o que mantém a ordem de desenho do GS. Os sprites de feedback (bloom) são uma barreira entre lotes.

### Andamento da Fase 2

**Bloco 1 feito (03/11):**
- **Vulkan:** headers da Khronos via `FetchContent`, loader do sistema e shader compilado com `glslc` na build.
- **VRAM compartilhada:** a VRAM é um buffer mapeado, com a CPU como plano B para o que a GPU ainda não faz.
- **Sprites do caminho rápido na GPU**, incluindo os que leem o próprio alvo (bloom).
- **Validação:** `ps2x_gs_replay` com e sem `PS2X_GS_GPU=1` deu **0 bytes de diferença** em `titulo.gsr`, `menu.gsr` e `fase.gsr`, e o jogo roda ao vivo com a GPU ligada.
- **Tempos ainda sem valor:** a máquina estava carregada (outra sessão com dois jogos e uma build rodando). Indicação sob a mesma carga: na fase, 1158 → 299 ms por campo no replay (o bloom foi para a GPU); no título ficou mais lento (35 → 66 ms), porque sprites e triângulos se alternam e cada troca espera a GPU.

**Bloco 2 feito (03/11):** triângulos e sprites gerais na GPU (`gs_tiles.comp`), com o pixel completo, todos os formatos de textura, CLUT, bilinear, Z, blend e fog.
- **Validação:** `titulo.gsr` e `menu.gsr` bit a bit iguais à CPU; `fase.gsr` com 1 pixel diferente por 1 unidade em 20 campos.
- **Replay** (carga ~8–10 por causa de outra sessão; refazer com a máquina livre):

| Gravação | CPU | GPU (Intel HD 630) |
|---|---|---|
| `titulo.gsr` | 35 ms/campo | 15 ms |
| `menu.gsr` | 197 ms | 114 ms |
| `fase.gsr` | 377 ms | 227 ms |

- **Ao vivo** com `PS2X_GS_GPU=1`: telas corretas até a seleção de dificuldade. A velocidade ao vivo ainda não foi medida com a máquina livre.

**Medição com a máquina livre (03/11, carga ~0):** os números acima, tirados sob carga, **enganavam**. A carga estrangulava as 4 threads de desenho da CPU, e por isso a GPU parecia mais rápida.

| Gravação | CPU (4 threads) | GPU Intel HD 630 | GPU NVIDIA GTX 1050 |
|---|---|---|---|
| `titulo.gsr` | 8,7 ms/campo | 11,4 ms | **4,6 ms** |
| `menu.gsr` | 33,8 ms | 78,7 ms | **24,4 ms** |
| `fase.gsr` | **36,8 ms** | 104,6 ms | 44,7 ms |

- **Exatidão:** a NVIDIA é bit a bit igual à CPU nas três gravações.
- **Ao vivo** (`PS2X_PERF`, 190 s): CPU com 14–18 campos/s no menu e na fase; NVIDIA (`PS2X_GS_GPU=1 PS2X_GPU_DEVICE=NVIDIA`) com 17–23 campos/s, chegando mais cedo a cada tela.
- **Conclusão:** a Intel integrada é mais lenta que a CPU; a NVIDIA ganha pouco e ainda perde na fase. O shader de tiles está longe do ideal: cada pixel percorre todos os primitivos do tile, e há uma espera de fence a cada troca CPU↔GPU.
- **Padrão:** a GPU continua opcional; a CPU segue como padrão.

**Bloco 3, menos esperas pela GPU (04/11):** antes, quase todo lote terminava numa espera completa: 62 por campo no menu e 115 na fase (12 ms e 32 ms por campo só esperando). A maioria vinha de cargas de CLUT e uploads. Agora:
- só se espera a GPU quando há conflito de verdade entre o que ela tem pendente e o que a CPU vai ler ou escrever;
- o CLUT tem versões, então trocar de paleta nunca força espera;
- o padrão passou a ser a GPU dedicada.

Replay com a máquina livre, bit a bit igual à CPU nas três gravações:

| Gravação | CPU | GPU NVIDIA |
|---|---|---|
| `titulo.gsr` | 8,7 ms | **4,3 ms** |
| `menu.gsr` | 33,8 ms | **16,6 ms** |
| `fase.gsr` | 36,8 ms | **27,7 ms** |

**Ao vivo:** 47 campos/s nos menus com GPU (CPU: 14–19) e 15–20 na fase.

**Lentidão da CPU na dificuldade: não era regressão.** Com a máquina livre, `dificuldade.gsr` custa 35–47 ms por campo na CPU (antes: 70–230 ms com uma build Rust rodando). Tirar o `-ffp-contract=off` não muda nada além do ruído. O replay na CPU varia ±15% entre execuções seguidas (aquecimento/clock).

**Bloco 4, VRAM na memória da GPU (04/11):** perfilando a fase, 75% do tempo era espera da GPU, e a VRAM estava na RAM do sistema (a GPU lia cada pixel pelo PCIe). Mudanças:
- cópia da VRAM na memória da GPU, sincronizada por faixas (`HostWrote` na subida, faixas escritas na volta);
- buffers auxiliares na memória da GPU (BAR);
- sprites com frame em formato Z (limpeza do Z na fase) agora vão para a GPU: eram ~2.700 primitivos por 20 campos ainda na CPU.

| Gravação | Antes (bloco 3) | Agora |
|---|---|---|
| `titulo.gsr` | 4,3 ms | **3,9 ms** |
| `menu.gsr` | 16,6 ms | **14,0 ms** |
| `fase.gsr` | 27,7 ms | **20,8 ms** |
| `dificuldade.gsr` | 16,6 ms | **14,4 ms** |

Bit a bit igual à CPU nas quatro. Os testes `gs` com GPU falham nos mesmos 46 com e sem a cópia (testes que leem o buffer original; com a CPU são os 33 de sempre).

**Ao vivo:** menus a ~47 campos/s, fase a 16–21. Na fase, o GS fica ocupado só ~65% do tempo e o jogo não espera o GS: **o limite da fase agora é a thread do jogo (EE/VU1)**, não o GS.

**Bloco 5, a thread do jogo (04/11):** na fase a GameThread fica a 100% e o GS não é mais o limite. Perfil só dessa thread (`PS2X_PROFILE_THREAD`), na ordem em que foi atacado:
- **IOP** (~30%): o thread do CRI_ADXI faz polling o tempo todo. A fila de DMA era varrida (e um vetor alocado) a cada instrução; a busca de imports lia a memória duas vezes por instrução; o polling passou a custar 16× por fatia.
- **VU1 JIT:** cobria 68% dos pares. Com callouts para `CLIP`, `DIV`, EFU, `FC*` e `ILW`/`ISW`, cobre 84%. Sozinho não mudou a fase.
- **Ciclos da VU1** (~19%): o JIT avançava ciclo a ciclo quando havia XGKICK ativo. Agora pula até o próximo evento.
- **`memcpy`** (~12%): era o `m_xgkick = {}` zerando o buffer de 64 KB a cada XGKICK e a cada microprograma.

| Ao vivo, GPU ligada | Antes | Agora |
|---|---|---|
| Menus | 47–56 campos/s | **60** |
| Fase | 17–22 | **26–37** |
| Carregamento até a fase | ~14 s | ~10 s |

Testes `vu1`/`vu`/`kernel`/`interrupt` 127/0; `PS2X_VU_JIT_CHECK=1` sem diferença de ciclos.

**Teste da GPU (04/11):** a suíte `gs` ganhou um teste que desenha o mesmo cenário na CPU e na GPU e compara a VRAM byte a byte, cobrindo cada regra de espera e a cópia na memória da GPU. Ele não depende das gravações em `gsrec/`.

**GPU, o que ficou de lado:** com os menus no teto de 60 e a fase limitada pela GameThread (GS ocupado 65–70%, jogo esperando o GS 0%), sobrepor CPU e GPU ou mandar os uploads para a GPU só reduz uso de CPU, sem ganhar campos/s. Voltar a eles quando o GS virar o limite de novo.

**Bloco 6 (04–05/11):**
- **Fase:** próximo ciclo de pipeline da VU1 em cache (`m_nextPipelineCycle`, com recompilação completa de ~2 h); `commitReadyPipelines` caiu de 7,9% para 2,1% da GameThread. Leituras de RAM do IOP inline. Fase ao vivo: **31–39 campos/s** (antes 26–37).
- **GPU:** envio sem espera (dois slots) e uploads de textura na GPU. Replay, todos bit a bit iguais:

| Gravação | Antes | Agora |
|---|---|---|
| `titulo.gsr` | 3,9 ms | **2,8 ms** |
| `menu.gsr` | 15,0 ms | **9,6 ms** |
| `fase.gsr` | 20,3 ms | 20,6 ms |
| `dificuldade.gsr` | 14,0 ms | **8,8 ms** |

Ao vivo, o worker do GS nos menus caiu de ~95% para ~55% ocupado; na fase segue ~65%.

**Próximo:**
- **Fase:** ainda é a GameThread: IOP (~26%), JIT da VU1 e `sub_00274688` (~5%, a espera de vsync do próprio jogo).
- **IOP mais rápido:** cache de decodificação ou HLE das funções do LIBSD que o ADX chama em polling.
- **Tirar da CPU o que sobrou no GS:** linhas, pontos, feedback.
- **Apresentação direto da GPU.**

Ordem de implementação (cada passo validado contra o backend CPU com os arquivos de replay da Fase 0, comparando a VRAM):
1. Infraestrutura: buffer da VRAM na GPU, fila de comandos, `Present` direto da GPU (sem cópia para a CPU).
2. Uploads (host→local), cópias local→local, CLUT.
3. Sprites sem textura e com textura CT32: limpeza de tela, alfa e bloom. Isso cobre o pós-processamento.
4. Triângulos: Z (GEQUAL/GREATER, Z24/Z32), blend, teste de alfa e de destino, fog.
5. Formatos de textura T8/T4/CT16 e filtro bilinear.
6. Leituras de volta para o EE (local→host, `ReadVram`) com sincronização sob demanda.

Meta da Fase 2: GS deixa de ser o limite (worker de CPU ~0%). Menu a 60 vsyncs/s e fase limitada só pela thread do jogo.

**Risco:** as diferenças de arredondamento entre GPU e CPU precisam ficar em zero (aritmética inteira no shader, igual à do backend CPU). O replay diferencial existe exatamente para isso.

## Fase 3: thread do jogo (depois da GPU)

Com o GS fora do caminho, o limite passa a ser a thread do jogo:
- **IOP:** o polling do CRI_ADXI custa caro. Detectar laços de polling no IOP, como já foi feito no EE.
- **VU1:** fazer o JIT cobrir mais microprogramas.
- **Thread separada para o IOP:** ele rodaria em paralelo ao EE, sincronizando pelo SIF.

## Ordem sugerida

**Fase 0 → item 1 da Fase 1 → itens 2 a 5 → Fase 2 → Fase 3.**

A Fase 0 e o item 1 resolvem a sensação de travamento e aceleram todos os testes seguintes. A Fase 2 é a que usa a GPU de verdade.
