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

### Estado da Fase 0 (3/11)

Prontos:
- **`ARQUITETURA.md`:** o mapa do sistema.
- **Gravação/replay:** `GSRecordingBackend` e `ps2x_gs_replay` (uso em `ARQUITETURA.md` §6).
- **Suítes por nome:** `ps2x_tests gs vu1 ...`.

As gravações ficam em `gsrec/` (fora do git: contêm dados do jogo). Replay determinístico: o mesmo hash da VRAM em cada repetição.

**Linha de base no backend CPU** (`ps2x_gs_replay --repeat 2`, mediana por quadro; "quadro" = cada vez que o desenho sai do FBP 0, o que pode ser mais de uma vez por quadro do jogo):

| Gravação | Momento | Mediana | Pior |
|---|---|---|---|
| `menu.gsr` | menu principal, 50 s | 87,8 ms (11,4 fps) | 126 ms |
| `selecao.gsr` | seleção + dificuldade, 75 s | 99,3 ms (10,1 fps) | 190 ms |
| `fase.gsr` | Goro's Lair, 160 s | 19,0 ms (52,7 fps) | 95 ms |

**Leitura:** no menu e na seleção, o desenho sozinho não passa de ~11 fps (a GPU é o caminho). Na fase, nesta cena, o desenho não é o gargalo principal: os "0,5 fps" visuais vêm da janela presa no lock (item 1 abaixo) e da thread do jogo.

Faltam: o medidor permanente (`PS2X_PERF`) e o atalho direto para a fase.

## Fase 1: ganhos rápidos na CPU (~2–4 dias)

| # | Mudança | Ganho esperado | Estado |
|---|---|---|---|
| 1 | Janela sem bloquear: `try_lock` no `latchHostPresentationFrame` e, se ocupado, reaproveitar o último pedido. A imagem vem da cópia do quadro completo, que não depende do lock | Janela fluida (60 Hz) mesmo com o jogo lento; acaba com os "2 fps" visuais | Escrito em `patches/wip_menus_cache_texturas.diff`, **sem teste** |
| 2 | Validar o pulo do laço de espera (`idle_loops` no `sm.toml`, `eeIdleCheckpoint`) | Libera a thread do jogo na espera de quadro | Já está no código; falta medir com a Fase 0 |
| 3 | Cache de texturas decodificadas (RGBA linear na RAM, invalidado por número de série de página da VRAM) | Menos custo no bilinear T8/T4. Só vale se o menu não reenviar as texturas a cada quadro | No mesmo patch WIP; a única medição não mostrou ganho. **Manter só se o replay provar** |
| 4 | Upload de textura por linhas (`UploadImageLocked`, hoje pixel a pixel via ponteiro de função) | ~9% do worker no menu | A fazer |
| 5 | Sem logs em caminho quente (`sceSifSetDma`) | ~1–5% da thread do jogo | `AGRESSIVE_LOGS` já ficou desligado por padrão |

Meta da Fase 1: janela a 60 Hz; menu e seleção acima de 30 vsyncs/s.

## Fase 2: desenho na GPU (o ganho grande, ~3–6 semanas)

O código já tem a interface certa: `GSBackend` (`gs_backend.h`), com `Submit`, `UploadImage`, `ReadVram`, `Present` etc. Um `GSGpuBackend` entra no lugar do `GSCpuBackend` sem mexer no resto. A escolha vem de `PS2X_GS_BACKEND=gpu|cpu`, e o CPU continua como referência.

**Abordagem recomendada: rasterizador em compute shader sobre a memória local do GS**, não a rasterização "de hardware" do PCSX2.
- **Por quê:** o MKSM usa a mesma memória como Z e como cor. A página `0x8C` é o Z-buffer e também o rascunho do bloom. Ele também lê o próprio alvo (o bloom) e copia Z como textura (`0x12C`/`0x14C`). Com os 4 MB da VRAM do GS num buffer da GPU e o swizzle feito no shader, tudo isso continua exato e sem gambiarra. O PCSX2 precisa de vários hacks justamente por converter isso em texturas de GPU.
- **API:** Vulkan 1.2 com compute. É o melhor no Linux (Mesa) e portável. OpenGL 4.5 com compute também serve, se preferir reaproveitar o contexto do raylib.
- **Como desenhar:** em lotes, dividindo a tela em tiles (por exemplo 32×32 px). Cada workgroup processa os primitivos que tocam o seu tile, em ordem, o que mantém a ordem de desenho do GS. Os sprites de feedback (bloom) são uma barreira entre lotes.

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
