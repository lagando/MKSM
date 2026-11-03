# Telas com lixo verde nas transições

## Resumo

O verde é uma **imagem de 16 bits (CT16) lida como CT32**. Nas telas de carregamento, o jogo desenha uma imagem CT16 de 512×448 na página 0 (a mesma página do framebuffer da cena) e troca o display para ela. Quando a tela volta para a cena, o display volta a ser CT32 640 de largura. Só que o quadro que a janela mostra nesse instante (a cópia de "quadro completo" do backend, ou a VRAM ao vivo) ainda tem a imagem de 16 bits na página 0. O preto CT16 com o bit de alfa (`0x8000`), lido em pares como CT32, dá `0x80008000`, ou seja, R=0, G=0x80, B=0: verde puro. Por cima aparecem as letras e logos da imagem, embaralhados.

A correção fica em `gs_cpu_backend.cpp/.h` (arquivos da outra sessão). Ela está descrita em [`telas_verdes_backend.diff`](telas_verdes_backend.diff), que aplica sobre a versão atual. Não foi aplicada na árvore.

## Evidência

Medido com diagnósticos temporários (já removidos) em `ps2_runtime.cpp` (`UploadFrame`) e `gs_frontend.cpp` (registro FRAME e TRXDIR):

| tick (vsync) | O que acontece |
|---|---|
| até 1016 | Cena CT32 normal: FRAME alterna as páginas `0x00` e `0x46` (fbw 10), `DISPFB1=0x1400` (FBP 0, 640, CT32), `DISPLAY1` com DH=224 |
| 1018–1032 | O jogo desenha com `FRAME fbp=0x0 fbw=8 psm=CT16` e `fbp=0x38 CT32`: a **tela legal/copyright** em 512×448×16 bits na página 0 |
| 1020 | `DISPFB1=0x11000` (FBP 0, 512, **CT16**), `DISPLAY1=0x1bf1ff00000000` (512×448) |
| 1032–1233 | Nada é desenhado (carregamento) |
| 1233 | Primeiro desenho CT32 depois da pausa vai para `0x46`. Como a página principal "mudou", o backend tira a cópia de quadro completo, e a página 0 ainda tem a imagem CT16 |
| 1234 | `DISPFB1=0x1400` de novo (CT32). A janela mostra essa cópia: **69% dos pixels verdes**, histograma dominado por `(0,128,0)` |

- **Boot (logo depois do intro):** reproduz em toda execução sem a correção (runs 2, 3, 5 e 6 deste estudo: verde no tick 1234).
- **Menu → fase:** é o mesmo mecanismo. O display vai para CT16 512×448 no tick 2856 (a tela "LOADING" com o logo e a barra de progresso) e volta para CT32 no tick 3328–3332. Se aparece verde depende de quando a cópia de quadro completo é tirada em relação ao primeiro quadro CT32 desenhado na página 0. A fase roda a 7–9 quadros/s, então um quadro errado fica ~150 ms na tela. Nas minhas execuções esse caso não disparou o detector, mas a sequência de registradores é a mesma do boot.
- **Trocas de área em Goro's Lair:** não chegaram a acontecer nas execuções (o `MKSM_AUTO_PAD=xs` não anda pela fase). Se usam a mesma tela "LOADING", o mecanismo é o mesmo.
- O conteúdo da página 0 durante o carregamento, decodificado como CT16 512×448, é a tela legal e a tela "LOADING" corretas. Ou seja, o desenho está certo; o erro está só na apresentação.

Hipóteses descartadas: o upload IMAGE (`UploadImageLocked`) e a cópia local→local estão corretos para CT16/CT24/T8; não há vídeo nem IPU nessas telas; o CLUT não participa.

## Correção (descrita; arquivos proibidos para esta sessão)

`telas_verdes_backend.diff`, sobre `ps2xRuntime/include/runtime/gs/gs_cpu_backend.h` e `ps2xRuntime/src/lib/gs/gs_cpu_backend.cpp`:

1. `m_pagePsm[512]`: o PSM da última escrita que começa em cada página de frame. É gravado pelo worker em cada `Draw` (`ctx.frame.fbp/psm`) e em cada transferência host→local ou local→local alinhada à página (`BeginTransferLocked`). `0xFF` = nunca escrita. A atualização usa `NotePagePsm`, que só pega o `m_frameMutex` quando o valor muda.
2. `m_completedPagePsm`: a cópia de `m_pagePsm` tirada junto com `m_completedFrameVram`.
3. `Present` passa ao `snapshotBackend` a tabela que corresponde à VRAM escolhida (a do quadro completo ou a ao vivo).
4. Em `PresentFromLocalMemory` (`copySource`), uma página cuja última escrita foi em outro formato sai preta (`& ~1u` deixa CT32 e CT24 equivalentes). Os displays CT16 continuam pretos, como já era (o atalho antigo foi mantido e documentado).

Por que não no frontend: o frontend (GameThread) está à frente da fila do backend. No tick 1234 ele já viu os desenhos CT32 na página 0, enquanto a cópia que a janela mostra é do tick 1233. Só o backend sabe o que estava na VRAM no momento da cópia.

## Como testar

Build própria `PS2Recomp/out/build-verde` (ver abaixo). Com a correção (compilada à parte, sem mexer nos arquivos compartilhados):

- `ps2x_tests gs`: 43 passam e 33 falham, **a mesma lista de 33** com e sem a correção (nesta árvore são 33, não 34).
- Jogo, 500 s, `MKSM_SKIP_INTRO=1 MKSM_AUTO_PAD=xs`: as duas voltas de CT16 para CT32 (ticks 1234 e 3332) acontecem com **0 quadros verdes** (detector: ≥10% dos pixels com G > R+24 e G > B+24). Sem a correção, o detector dispara no tick 1234 em toda execução. A fase entra normal, com HUD e mapa.

Para reproduzir o diagnóstico rápido: o caso do boot aparece ~25 s depois de abrir o jogo, sem precisar chegar na fase.

## Pendências (fora do escopo)

- **Mostrar as telas legal e "LOADING"** em vez de preto. Hoje todo display CT16 sai preto. Para mostrá-las: (a) trocar a condição para `writtenPsm == 0xFF ? psm == CT16 : diferente`, o que mantém preto o vídeo sem decodificador, e (b) em `PresentFromLocalMemory`, com INT+FFMD e DH > 256 (512×448), ler todas as linhas em vez de metade dobrada. Testei (a) sozinho: a tela aparece, mas só a metade de cima, esticada (o logo cortado embaixo, sem a barra), porque `doubleRows` lê `height/2` linhas. O `SMODE2` fica em `0x3` o tempo todo (o jogo não chama `SetGsCrt` para trocar para o modo frame).
- Trocas de área em Goro's Lair: confirmar com uma execução que ande pela fase (gravar com `PS2X_GS_RECORD` não ajuda aqui, porque a gravação não guarda PMODE/DISPFB).

## Build separada (o que funcionou)

- Copiar `out/build` para `out/build-verde` com `cp -a` (mantém os mtimes) e trocar o caminho no `CMakeCache.txt`.
- Configurar com `-DFETCHCONTENT_FULLY_DISCONNECTED=ON` e `-DFETCHCONTENT_SOURCE_DIR_{RAYLIB,RLIMGUI,IMGUI,VULKAN_HEADERS}=<out/build/_deps/...-src>`. Assim a linha de compilação dos 13 mil arquivos gerados fica idêntica à da build principal, e basta copiar os hashes de `src/runner/` do `.ninja_log` de `out/build`: a build sai em ~2 min em vez de ~2 h.
- Para testar um patch em arquivo proibido sem tocar nele: compilar os objetos que incluem `gs_cpu_backend.h` com `-iquote <cópia>/include`, restaurar o mtime original de cada `.o` (para o ninja não recompilar) e apagar os `.a` para forçar só o link.
