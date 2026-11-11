
```markdown
# 🗺️ ShaolinMonks: Plano de Investigação e Diário de Bordo

**Estado:** 03/11/2026 (após `run101`, VU1 recompilada em x86-64).  
Este é o documento técnico detalhado (diário de bordo e engenharia reversa). O `ESTADO.md` é o resumo técnico de status e o `README.md` a apresentação do projeto.  
*Convenção:* **[medido]** = visto em execução no log, **[lido]** = lido no código C++ gerado, **[memória]** = conhecimento arquitetural sobre o SDK do PS2.

---

## 1. Contexto e Objetivo Atual

Estamos portando Mortal Kombat: Shaolin Monks (PS2) usando recompilação estática via PS2Recomp. A CPU roda nativa em C++, mas o hardware auxiliar (GS, VU, DMA, IOP) ainda é emulado. 
O objetivo atual é **passar da tela de loading para o menu principal**. O loader de `GAMEDATA.WAD` funciona e, desde a run33, o jogo entra no filme CRI (`ApStart`, `0x26F918`) e roda 175 s sem falha. O próximo bloqueio é mostrar imagem: ver a seção 5A.

---

## 2. Estado das Fases Anteriores

| Fase | Status | Detalhe |
|---|:---:|---|
| Bootstrap & ELF | ✅ | ISO extraída, 13.651 arquivos C++ gerados a partir do executável. |
| Tela Magenta | ✅ | `PMODE` e `sceGsDispEnv` corrigidos no emulador do GS. |
| CDVD Bind | ✅ | 10 binds HLE (`mksm-cdvd`) substituem as chamadas de libcdvd do guest. |
| Threads | ✅ | `CreateThread` com prio 0 retornava -403. Fix em `EeScheduler.cpp:426` (`< 0` em vez de `< 1`). 5 threads ativas no guest. |
| Leitura de CD | ⚠️ | O backend HLE lê os setores (`FILELIST.DIR`, `.SFD`, `.WAD`) e o loader completa vários ciclos 2→3→0; o travamento final é o handle ADXF parado (`status=1`) com o loader esperando `status=3`. |

---

## 3. Dissecação da Máquina de Estados de Streaming

Para entender o travamento, mapeamos o coração do sistema de carregamento do jogo.

### 3.1 A Máquina Principal (`func_378A30`)
Localizada em `0x378A30`, gerencia o streaming assíncrono.
- Checa o bit 0 de `[0x755AB8]` (ocupado).
- Lê o estado atual em `[0x755ABC]`.
- Salta para o handler correspondente na tabela em `0x592790`.

**A Jump Table (medida nos logs):**
| Estado (`[0x755ABC]`) | Handler | O que faz |
|:---:|:---:|---|
| `0` | `0x378A90` | Init. Chama `func_211878` (requisição) e calcula tamanhos. |
| `1` | `0x378BF8` | **Poll**. Chama `func_2123C8`. Se `0` -> Vai pro Estado 2. Se `-1` -> Volta pro 0. Outro -> Fica em loop no 1. |
| `2` | `0x378C3C` | Dispara o `sceCdRead` (`func_467670`) no slot atual e vai pro Estado 3. |
| `3` | `0x378CA8` | Verifica callback em `[ptr+0x24]`. Se existir, executa e vai pro Estado 4. |
| `4` | `0x378CDC` | Finaliza e reseta de volta pro Estado 0. |

### 3.2 O Handle CRI ADXF (`0x54FB60`)
O despachante avalia as flags neste bloco de memória (**handle** do CRI ADXF, tamanho `0x44`):
- `handle[0x00]`: `1` = handle criado/ativo.
- `handle[0x01]`: **status** do ADXF — `1`=STOP, `2`=READING, `3`=READEND, `4`=READERR.
- `handle[0x03]`: **flag de processamento** (o "sinal verde" que o runtime injeta).
- `handle[0x08]`: ponteiro do callback pendente (`handle[8][4]()` no fim).
- `handle[0x0C]`: tamanho total em setores; `handle[0x14]`: setores já entregues.
- `handle[0x20]`: setores lidos da última transferência (`adxf_GetNumReadSct`).
- `handle[0x04]`: `stm` (decoder); `handle[0x2C]`: `512`.

A tabela de 16 slots **é** a tabela de handles (`0x54FB60 + i*0x44`), não uma estrutura paralela.

---

## 4. A Evolução do Bloqueio: run22 → run24 → run25

### 4.1 Run 22 — preso no estado 1
A máquina ficava eternamente presa no **Estado 1**. O HLE terminava a leitura (colocando `status = 1`), mas o poll `func_2123C8` nunca retornava sucesso (0) ou erro (-1).

### 4.2 Run 24 — injeção do sinal verde
Para forçar a saída do Estado 1, passamos a escrever `handle[3] = 1` no runtime quando `handle[0] == 1`, `status == 2` e `handle[3] == 0`.
* **Avanço:** a máquina destravou; o jogo leu `FILELIST.DIR`, setores do `GAMEDATA.WAD` e o catálogo dos `.SFD`.
* **Sintoma:** o cursor (`handle[0x14]`) chegava a ~36/39 e depois virava lixo (`75777`), e a thread principal ficava em `0x379220`.
* **Conclusão da run24 (incompleta):** "o `status` nunca chega a 3". Isso estava errado — ver 4.3.

### 4.3 Run 25 — o que realmente acontece

Foi adicionado ao runtime um observador (`[run:trans]`) que loga **toda transição** de `status`, `loopState`, `handle[3]` e `handle[8]`, com o `pc` do momento. O `[run:tick]` passou a amostrar a cada 30 frames.

**Descobertas:**

1. **O poll funciona.** A máquina completa ciclos inteiros, ~5 vezes nos primeiros 60 s:
   ```
   st=2 loopState=2 obj3=1 obj8=0x549190   (pedido aceito, lendo)
   st=3 loopState=2 obj3=1 obj8=0x0 cnt=29 (READEND, callback consumido)
   st=0 loopState=0 obj3=0 obj8=0x0        (handle fechado/zerado)
   ```
2. **`0x379220` não é o bug**: é o *delay slot* de `jal func_378A30` (`0x379218`). O PC pertence ao laço externo `0x379218–0x37925c`, que repete a máquina enquanto ela não terminar.
3. **O travamento final é outro estado**: `loopState = 2` com **`status = 1`** (STOP), `obj3 = 0`, `obj8 = 0`, `cnt = 0`, `stm = 0x53ce10`, `handle[0xC] = 0x2d047`, `handle[0x14] = 75777`. A transição que antecede é `st=2 → st=3 (pc=0x4180f0) → st=1 (pc=0x379220)`, e depois 50 s sem nenhuma mudança.
4. **`status = 1` é veneno para o loader**: `func_26F6E0` só retorna `1` quando `status == 3`; para 1 devolve `0` ("continua"), então o loader espera para sempre.
5. **`func_2123C8` ignora o retorno de `adxf_ReadNw`** e grava `loopState = 2` de qualquer jeito. Qualquer falha de início (ou parada prematura) vira deadlock silencioso em vez de erro.
6. Nota lateral: entre os ticks 30 e 540 (~9 s) o PC fica em `0x2130a0` (`loopState=0`) e **sai sozinho** — não é o bloqueio.

### 4.4 Protocolo ADXF decodificado [lido, dis]

O subsistema é o **CRI ADXF** (strings `adxf_GetStat@0x5A4798`, `adxf_ReadNw@0x5A45A8`, `adxf_Stop@0x5A4608`, `adxf_GetNumReadSct@0x5A4768`).

| Endereço | Nome | Comportamento |
|---|---|---|
| `func_413BE8` | `adxf_GetStat` | retorna `(int8)handle[1]` |
| `func_413B48` | `adxf_GetNumReadSct` | retorna `handle[0x20]` (`cnt`) |
| `func_413268` | `adxf_ReadNw` | `restante = handle[0xC] - handle[0x14]`; usa o menor entre pedido e restante; se der `0` → `status = 3` na hora; senão zera `cnt`, prepara `stm` e `status = 2` |
| `func_413690` | `adxf_Stop` | se `status` não for 1 nem 3: finaliza, `handle[0x20]` = bytes lidos, chama `func_413088` (despacha `handle[8][4]()`), grava `status = 1` |
| `func_413128` (via `func_4130F8`) | fecha/destrói handle | se `status == 2` chama `adxf_Stop`; libera `stm`; `memset(handle, 0, 0x44)` |
| `func_26F6E0` (via `func_370A98`) | wrapper do jogo p/ GetStat | `4` ou negativo → `-1`; senão calcula `[0x678444] = cnt` e `[0x678448] = cnt*100/[0x678440]`; retorna `1` **só se `status == 3`** |
| `func_26F640` (via `func_370A70`) | wrapper bytes→setores + `adxf_ReadNw` | arredonda `+0x7FF` e chama o read |
| `func_26F790` (via `func_370AE8`) | fecha o handle do loader | `func_4130F8` + zera `[0x678448]` |

Estrutura do poll (`func_2123C8`, estado interno em `[0x4EF7E0]`):
- `loopState == 0` → retorna `0`;
- `loopState == 1` → `adxf_ReadNw` e grava `loopState = 2` (**sem checar retorno**);
- `loopState == 2` → GetStat; `0` → retorna (espera); ≠ 0 → fecha handle e `loopState = 0`.

---

## 5. Evolução dos Testes: run25 → run26 (Vitória no Heap e Streaming)

### 5.1 Run 26 — Aplicação do Primeiro Passo e Desbloqueio do Heap
Na **run26** (01/11/2026), aplicamos as correções reveladas pela engenharia reversa do OGmidway:
1. **Syscalls `WakeupThread`, `iWakeupThread` e `ResumeThread`** (`Thread.cpp`): retornando o `id` da thread em caso de sucesso (`result >= 0 ? id : result`), em conformidade com o SDK Sony / Play! / PCSX2.
2. **Decodificação de 64-bit com Sinal no WAD (`GetAsyncFileSize`)**: em `0x211900` (`sub_00211878_0x211878.cpp` e `control_flow_emitter.cpp`), corrigido o `BGEZ` para testar `GPR_S64(ctx, 6) >= 0`.
3. **Constante `VF0.w = 1.0f`**: inicializada em todas as invocações de threads e callbacks do scheduler.

### 5.2 Resultados Medidos na Run 26 [medido, run26]
- **Fim da Corrupção de Heap**: O cursor `obj14` **nunca mais virou lixo (75777)**. O cursor agora avança legitimamente setor por setor (`h58 = obj14 = 358`).
- **Buffers de Carregamento Válidos**: A alocação em `Memory_AllocBlock` (`0x20F058`) para o loader assíncrono passou a entregar ponteiros de buffer reais e alinhados em `SYSTEM_MEMORY` (`0x1EDF180`, `0x1EDC140`, `0x1E4D940`, `0x1EDB180`), eliminando o erro de destino nulo em `0x4EF7EC`.
- **Fim do Deadlock no Estado 1**: O status do handle ADXF **não cai mais em STOP (`status=1`) em deadlock**. Ele realiza ciclos completos contínuos:
  `st=2 (lendo) → st=3 (READEND) → st=0 (reset) → st=2`.
- **Explosão no Volume de Dados Lidos**: O jogo realizou 16 leituras de grande escala no CDVD, incluindo um bloco contíguo de **292 setores (584 KB)** em `lbn=0x10003b` (`buf=0x1e4d940`), avançando muito além do `lbn=0x100038` onde antes parava.
- **Subsistemas em Atividade Contínua**: 5 threads ativas, VSync avançando (`vs=66`, `srvCalls=66`), DMA de SIF transferindo dados de áudio/IOP (`sceSifSetDma: dst=0x1252c0`, `dst=0x16f640`), sem nenhum travamento prematuro durante todo o período de 60 segundos.

---

## 5A. Run27 → Run33: do loader ao filme CRI (02/11/2026)

Runs 27 a 31 não foram registradas na hora; o que segue foi reconstruído de `run31_raw.log`, `ps2_log.txt` e do diagnóstico `[guest-branch:missing-context]` no `EeScheduler.cpp`.

### 5A.1 Salto para `0x55e25c` [medido, run31; lido]
A thread principal (id 1) saía do `0x379220`, chegava em `ApStart` e morria com `pc=ra=0x55e25c` (BSS), enquanto as outras threads seguiam rodando. Pista: `s0=0x441ab0`, um `ra` salvo lido como `s0`, ou seja, `$sp` estava `0x10` abaixo do esperado.

**Causa raiz:** bug do recompilador. `sub_0043FFE8` termina em `0x440000`, mas `0x43FFFC` é um `j func_419D60` cujo delay slot (`addiu $sp,$sp,0x10`) fica justamente em `0x440000`. O delay slot não era emitido, e `0x441AB0` restaurava `ra` = handle (`0x55e25c`).

**Correção:** `decodeFunction` (`ps2_recompiler.cpp`) agora decodifica o delay slot de um branch na última posição. Varredura do ELF achou 4 funções assim. Três mudam o código gerado: `0x43FFE8`, `0x450000` (`jal`), `0x48B0C0` (`jalr`). A quarta, `0x363228`, tem `nop` no delay slot e não muda.

Hipóteses descartadas: não é assert (`0x26EFE8` é a espera de frame; as strings `movie.cpp`/`ApStart` na pilha são tags do `malloc`).

### 5A.2 `sceDmaRecvN` [medido, run32]
Com a pilha certa, o jogo parou em `Unimplemented PS2 stub: sceDmaRecvN` (`0x43DCF8`, canal `0x1000D000`, destino `0x75F100`, QWC `0x400`). O stub agora programa MADR/QWC e dispara CHCR; a cópia é feita por `tryProcessScratchpadDma`. Outros canais continuam em TODO.

### 5A.3 Estado na run33 [medido]
175 s sem `guest-branch` nem exceção; encerrada pelo `timeout`. `vs=475`, `dma=1617`, `activeThreads=5`, `cb=0x272b50`, 17 `sceCdRead`. `gif` parado em 17 e nenhum `sceMpeg*` chamado. **Não verificado:** o que aparece na janela. O `gif` parado sugere que nada novo é desenhado.

### 5A.4 Pendências
1. **Fix S64 aplicado na run34.** Os 2.376 arquivos foram regenerados e recompilados. Resultado em 175 s, comparado à run33: `vs` 475 → 2931, `dma` 1617 → 11441, entrada no filme bem mais cedo (tick ~3600 em vez de ~9600), sem `guest-branch` nem exceção. `gif=17`, 17 `sceCdRead` e nenhum `sceMpeg*`, como antes.
2. **Filme/vídeo:** descobrir por que `sceMpeg*` não é chamado. Referência: `games/mk-shaolin-monks/runtime/mk-cri-video-ring.h` e `mk-mpeg-bridge.h` do OGmidway, mais o perfil IOP (`mk_cri.cpp`, RPC `0x90000200`). Atalho para testar o menu: flag `0x513a04` (seção 6.2).
3. **Limpeza:** pilha de IRQ da seção 6.2 já usa `SP=0` em `EeScheduler.cpp:1327` e `:1972` no código atual; `:1942` ainda usa `m_gsVSyncCallbackSp` (não revisado).

### 5A.5 Tela preta: causa e run35 [medido]
**O filme é a tela preta.** Na run34 o worker do filme (`0x43FAE8`) roda 10 vezes e `0x44CF60` (decode) 2 vezes; `sceMpegGetPicture` (`0x4688B0`) não aparece no rastreio porque é stub, então o estado real dele não foi medido. O callback de swap `0x272B50` roda 2860 vezes sem conteúdo. O anel de vídeo CRI nunca chega ao decoder.

Filme real exige, segundo o OGmidway: ponte `sceMpegGetPicture` → anel CRI → FFmpeg (`mk-mpeg-bridge.h`, `mk-cri-video-ring.h`), layout MPEG `c4/ec/f8/f0/f4`, e relógio de áudio CRI vindo do IOP (`mk_cri.cpp`, `mk_sfsv.cpp`, ~1.200 linhas). Sem o relógio só sai o primeiro quadro. FFmpeg já está ligado aqui (`PS2X_ENABLE_FFMPEG=ON`).

**Atalho:** `MKSM_SKIP_INTRO=1` usa o desvio nativo `0x513A04` em `0x1D32E0` (`game_overrides_mksm.cpp`). Na run35 (175 s): `gif` 17 → 663, `dma=9091`, sem falha, e **primeira imagem real do jogo**: painel azul com moldura e dragão ao fundo a partir do frame ~4800, estático até o fim. Hipótese: é o aviso de autosave, porque falta texto. Não confirmado.

`PS2X_SCREENSHOT_DIR=<pasta>` grava a janela a cada 600 frames.

**Próximos passos, em ordem:**
1. Painel: por que não há texto (fonte/textura ou pacote GIF). Ver `sceGsPutDrawEnv`, VIF DIRECT e SMODE2=3 na seção 6.4.
2. Pad: publicar os 32 bytes em `0x651F00` (seção 6.4); sem isso o diálogo não avança.
3. Filme: seguir a lista acima depois do menu.

### 5A.6 Texto e controle: run36 e run37 [medido]
**Causa do texto ausente:** `toDmaPhys` apagava o bit 31 dos ponteiros de scratchpad (`0x80000000 + offset`, `DmaAddr 0x3841D0`), e 37 envios ao GIF saíam com `tadr=0x0`. Corrigido em `Support.h`, junto com MADR bit 31 em `ps2_memory.cpp`. `sceGsPutDrawEnv` agora envia o pacote inteiro (`NLOOP+1`) por GIF DMA, como no OGmidway.

**Controle:** `scePadRead` publica os 32 bytes nos dois bancos da área DMA (`0x651F00`). A run37 logou `buttons=0xffbf` na porta 0 (Cross). Esses toques não vieram do código: vieram de teclado ou controle, sem confirmação de quem apertou.

**Resultado:** `gif` 663 → 4159 em menos tempo. A tela segue: aviso de autosave com texto → "Checking memory card" → "No MKSM save data" com menu No/Yes, setas e botões. Menu principal ainda não alcançado.

**Tela corrompida depois do Cross [medido, run37]:** na run37, após os toques em Cross, os últimos frames capturados são listras coloridas estáticas (verde, magenta, azul), sem nenhum painel. O jogo seguiu rodando (`vs=493`), mas a imagem some. Parece o sintoma de "bandas coloridas" do frontend no OGmidway, que eles atribuem a comandos MPG em tags REF do VIF e a pacotes GIF/VIF montados pelo HLE (`PROTOTYPE.md`). Causa aqui não verificada.

**Bugs abertos:**
0. Resolvido em 5A.7: as listras eram tela de carga.
1. Texto parcialmente quebrado: na tela do save, a linha 2 vira barras horizontais; na checagem do cartão somem letras como T, R, V. A posição X parece certa. Suspeitas: fronteira de pacote/scratchpad ou VIF DIRECT (seção 6.4). Não resolvido.
2. Velocidade instável: com carga alta na máquina, a mesma run em 120 s chegou a `vs=66` (run37a) contra `vs=440` (run36). Use 200 s.
3. Save em `mc0/` ainda não testado (escolher Yes).

### 5A.7 Menu principal: run38 [medido]
As listras **não eram bug de desenho**: são uma carga longa do frontend. Nela o jogo troca o display para `dispfb=0x11000` (FBP 0, 512×448, 16 bits) sobre a VRAM de texturas, e `gif` para enquanto `Async_DataLoad` lê (`obj14` vai a ~2700). Corrige a nota da 5A.6: a causa não era a do OGmidway. Na run38 (480 s, `MKSM_AUTO_PAD=x`) a carga terminou por volta do tick 10800 e o **menu principal apareceu**: logo, cenário e as 6 opções; o Cross automático entrou em Options e telas seguintes. `gif=135841`, `vs=1597`, sem falha.

Controle: o `USB Gamepad` genérico provavelmente manda o D-pad como eixo. `ps2_pad.cpp` agora trata eixo como direcional e mantém o teclado ativo com controle conectado. Não testado com o controle físico.

### 5A.8 Velocidade, proporção e texto: run39 a run48 [medido]
**Controle:** Weremir confirmou setas, X e triângulo funcionando e chegou à seleção de personagens.

**Velocidade:** o rastreador gravava e dava flush em cada chamada de função guest (~1,4 GB por minuto). Em vez de recompilar 13.651 arquivos (estimativa de ~5 h), `ps2_log_stream_override.cpp` define forte o `ps2_log::log_stream()`, que é fraco nos objetos, e manda tudo para um stream nulo. `PS2X_TRACE=1` religa o log. `ps2_runtime` passa a usar `-O3` com `-DPS2X_OPTIMIZE_RUNTIME_LIB=ON`. Resultado em 200 s: `vs` ~490 → 1556; menu em menos de 3 min, contra ~6 min antes. O inline do `log_entry` ainda custa um pouco; um build com `PS2X_ENABLE_AGRESSIVE_LOGS=OFF` e `-O2` tiraria o resto.

**Proporção:** com `SMODE2=3`, o frontend usa `DH=224` e o filme `DH=448`. `PresentFromLocalMemory` agora dobra a altura lógica quando DH ≤ 256 e lê metade das linhas, duplicando na saída. Imagem em proporção correta.

**Listras:** continuam na carga. São a VRAM exibida em FBP 0, 16 bits, enquanto `gif` está parado. Só ficaram mais curtas. Não sabemos o que o PS2 real mostra aqui; provavelmente falta um clear ou a imagem da tela de carga.

**Texto quebrado: causa ainda desconhecida.** Descartado com medição:
- atlas da fonte (`tbp=0x3de4`, T8 256×256, CLUT `0x3ee4`): dump da VRAM está perfeito;
- geometria/UV: 96.793 triângulos sem anomalia (altura 10, dV 320);
- sobrescrita: a fonte é enviada 1 vez; nenhum alvo de desenho ou transferência toca nela;
- cache de textura (`ReadVramUnlocked`) e CLUT direto da VRAM: sem efeito, revertidos;
- `0x3f70`/`0x3f84` são a moldura do painel, não glifos.
Letras afetadas: `f h i r s t v` em caixa-baixa pequena, e linhas inteiras em alguns diálogos.

**Magenta:** barras magenta nos diálogos depois do menu vêm do retorno padrão `0xFFFF00FF` de `SampleTexture` (PSM não tratado). `LookupCLUT` não disparou o log de CPSM inválido, então o suspeito é `tex.psm`. Próximo passo: logar `tex.psm` nesse retorno.

### 5A.9 Desempenho e apresentação: run49 a run63 [medido]
**Perfil** (`PS2X_PROFILE=<segundos>`, amostrador próprio em `ps2_sampling_profiler.cpp`, gera `ps2_profile.txt`): código do jogo só 1,7%; IOP 39% + 13% em `_Rb_tree_increment`; GS 32%. A `GameThread` estava a 99% de um núcleo, as demais ociosas. RAM não é gargalo e GPU exigiria renderizador novo.

**Otimizações:**
- IOP em lotes de 16384 ciclos EE em vez de a cada bloco.
- GS em thread própria: `Submit`, `LoadClut` e `TextureFlush` entram numa fila; o resto chama `WaitForQueue()`.
- Thread do IOP que gasta a fatia inteira paga 4× os ciclos. É `CRI_ADXI.IRX` (thread 10) em polling. Atalho marcado com `shortcut:`; rever se o áudio ADX falhar.
Resultado: `vs` de ~7/s para 16-28/s; `vs=2339` em 200 s; seleção de personagens em ~2 min. O maior custo restante é o rastreador, mesmo em stream nulo (~33%): só some com rebuild total usando `PS2X_ENABLE_AGRESSIVE_LOGS=OFF`, ~5 h.

**Piscar e bordas sumindo (mesma causa):** o host capturava a VRAM no meio do desenho. O jogo compõe em `fbp=46`, copia para `fbp=0` e põe texto e bordas por último. Agora a thread do GS copia a VRAM quando o alvo sai de `fbp=0`, e `Present` mostra essa cópia, voltando à VRAM viva após 30 apresentações sem cópia (telas de carga). `Present` nunca espera a fila: esperar deixava o host sem quadros durante os diálogos.

**Janela:** a imagem estica para preencher a janela (`dstRect` igual à janela).

**Listras:** o display de 16 bits é a superfície do filme. Sem decoder, vira preto (`shortcut:` em `PresentFromLocalMemory`). Remover quando o SFD tocar.

**Automação:** `MKSM_AUTO_PAD=xs` alterna Cross e Start e chega à seleção de personagens sozinho.

**Pendências:**
1. Barras magenta: só nas dicas de botão. `TEX0=0x0E800530` (`psm=40 tbp=0x530 tbw=0 tw=3`), `prim=4 fst=1 fbp=0`. Parece ponteiro/dado errado no lugar de TEX0. Rastrear quem escreve esse valor; pode ter a mesma causa do texto quebrado.
2. Texto quebrado (5A.8) segue aberto.
3. Gameplay: a run54 parou em `missing-target target=0x38c480` (JALR em `0x389b2c`). Função fora do `sm.toml`; o OGmidway tem listas em `config/pr244-entry-points.txt`.
4. Filme SFD.

### 5A.10 Magenta e texto quebrado: mesma causa [medido]
O pacote GIF de texto tinha 5312 bytes, mas a lista válida acabava com `eop=1` em 2656: **tamanho do DMA dobrado**. O resto era memória além do pacote, lida como comandos do GS (`TEX0=0x005e005500540053`, `PRIM` falso). Vinha da mistura de stubs HLE com rotinas originais de montagem de pacote. Removidos dos `stubs` do `sm.toml`: `sceGifPkCloseGifTag`, `sceGifPkAddGsAD`, `sceVif1PkOpenDirectCode`, `sceVif1PkCloseGifTag`, `sceVif1PkAddGsAD`. Run66: zero `TEX0` inválido, texto e dicas de botão corretos. `ps2_recompiled_stubs.h` ficou sem regenerar de propósito (só declara a mais) para evitar recompilar tudo.

### 5A.11 Lag dos menus: run64 a run74 [medido]
Na seleção de personagens o GS leva ~73% do tempo. A frame real fica em ~12 fps (o jogo alterna 2 buffers), com ~1M pixels estimados por quadro (estimativa por bounding box, sem scissor) e ~100 segmentos por quadro.

**Mudanças em `gs_cpu_backend.cpp`:**
- Rasterização em 4 bandas de 8 linhas (`rowInBand`). Só desenhos sem risco de ordem rodam juntos: uma leitura de textura que sobreponha alvo recém-desenhado fecha o segmento; leitura do próprio alvo roda em série.
- `BeginTransfer` e `UploadImage` entram na fila. Antes a `GameThread` gastava 38 s de 125 s em `WaitForQueue`.
- Quadro completo protegido por `m_frameMutex`: com a fila cheia o worker segura `m_mutex` o tempo todo e o host parava de receber imagem.
- Bandas giram (`_mm_pause`) entre segmentos em vez de dormir; bilinear no centro exato do texel usa 1 amostra; `lround` trocado; logs por pixel removidos do arquivo.

**Resultado:** menus entre 14 e 24 VSyncs/s, todas as threads do GS ativas (worker ~98%, bandas ~73%, `GameThread` ~67%). A automação não para sempre na mesma tela, então a comparação antes/depois na seleção de personagens é aproximada.

**Próximo ganho:** o custo por pixel segue alto (`WritePixel` + `SampleTexture` ~40%). Rebuild com `PS2X_ENABLE_AGRESSIVE_LOGS=OFF` (~5 h) tira ~33% da `GameThread`. Um salto maior exige fast paths por pixel ou renderizador por GPU.

### 5A.12 Rasterizador e entrada na fase: run75 a run86 [medido]
**Bancada:** `MKSM_AUTO_PAD=xsxsxsx.` para no menu principal (`.` no fim encerra o roteiro).

**Otimização do GS (12,5 → 19 fps no menu):** máscara em `rowInBand` no lugar de `%`; removido o cache de páginas de textura (copiava 8 KB por miss); bilinear inteira (`lerpRgba`); triângulo com pesos incrementais e saída antecipada da linha; descarte de primitiva chapada que o teste de alpha rejeita. Maior ganho: blocos 32×32 com `alpha=0x68/0x6a`, cuja equação de blend devolve `Cd`, gravam só o byte de alpha (`writesOnlyAlphaByte`). Atenção: `PixelIndexCT32` devolve índice de pixel; o byte é índice × 4.

**Funções ausentes:** `sm.toml` ganhou 1.586 `entry_points`: varredura de ponteiros no segmento de dados + mapa Ghidra do OGmidway. Elimina os callbacks ausentes (`0x38c480` e afins). Headers novos só com data preservada (`touch -r`) para não recompilar tudo; o rebuild completo corrige isso sozinho.

**Laço em `0x24e770`:** `BC2T/BC2F` testavam `vu0_status & 1` (flag Z). O certo é `vu0_vpu_stat & 0x100` (VU1 rodando). Corrigido em `control_flow_emitter.cpp`.

**Ponteiro nulo em `0x20a364` (alocador `0xb0a580` corrompido):** sumiu após portar `ps2_vu_sub.h` (VSUB preserva VF0 e atualiza MAC/STATUS) e `ps2_vu_clip.h` (ordem de bits do VCLIP) do OGmidway. Causa exata da corrupção não provada; o OGmidway também não fechou o autor.

**Resultado (run85):** fase 3D renderiza, terreno e iluminação visíveis, sem erro. Defeitos: branco estourado (provável bloom/AutoFlush, seção 6.4) e textura rasgada na horizontal.

**Desempenho na fase (run86):** 1,5 fps. 94% é `VU1Interpreter` na `GameThread` (`commitReadyPipelines` 28%); GS 2,7%. O próximo alvo de otimização é a VU1, não o GS.

### 5A.13 VU1: run88 e run89 [medido]
Na fase, 94% do tempo era `VU1Interpreter`. O EE gasta pouco, então pôr a VU1 em outra thread não adiantaria: o custo é a própria interpretação. Cortes em `src/lib/vu/`:
- VF, VI e ACC são aplicados na hora. Toda leitura deles trava em `calculatePairReadyCycle`, então nenhum programa vê a diferença, e o cálculo de ciclos segue igual. Flags, Q, P e stores não têm trava e continuam no pipeline. Filas `m_vfWritePipeline`, `m_viWritePipeline`, `m_accWritePipeline` removidas.
- `long double` → `double`; `normalizeOperands` em SSE; decodificado por referência.
- `commitReadyPipelines` rodava duas vezes por ciclo; registradores por bits (`ctz`) nas travas.
- Resultado FMAC normal dispensa `calculateFmacExactResult` (expoente fora de 0 e ≥ 0xFE).

**Resultado:** `gif`/`vs` na fase ~2,5× (de ~1,5 para ~3,5 fps), mesma imagem, sem erro. Perfil agora plano: sem ponto dominante, o interpretador está perto do limite.

**Testes:** suíte só de VU (`register_ps2_vu1_tests` + `register_ps2_vu_tests`): 83 passam, 1 falha igual à referência (`MSCAL ... GS VRAM`, lê a VRAM sem esperar a fila do GS). Removidos 6 asserts que espiavam registrador no meio do pipeline; valores finais seguem verificados. A suíte completa aborta em `EeScheduler::assertExecutor` e tem falhas de GS causadas pela fila assíncrona: pendente.

**Próximos passos:** recompilar microprogramas da VU1 (dinâmico ou por hash do MPG) é o que falta para taxa jogável. Rebuild completo sem rastreador ajuda menus, não a fase. Visual da fase: branco estourado (bloom/AutoFlush) e textura rasgada.

### 5A.14 Recompilador (JIT) da VU1: run90 a run101 [medido]
**Perfil dos microprogramas (run90):** 1,4 bilhão de pares, ~143 ns por par. Dominam MADD/MULA/MUL/MAX/ADD/SUB/ITOF/FTOI com MOVE, LQ/SQ, MTIR, IADDIU, IOR/IAND, IBNE. Só 3 de 2048 pares leem MAC/STATUS (FMAND), executados 0,01% das vezes. Metade do tempo em blocos de 12+ pares.

**Desenho (`src/lib/vu/ps2_vu1_jit.cpp`, só Linux x86-64):**
- Sequências de pares simples viram SSE, sobre o mesmo `VU1State`; o interpretador e o JIT alternam a cada par.
- Ficam no interpretador: desvios, CLIP/FCAND, DIV/EFU, XGKICK, flags, bits E/D/T/M, e pares até 16 antes de um leitor de MAC (`shortcut:` no código).
- Ciclos: calculados na compilação com as regras de `calculatePairReadyCycle`/`markPairWrites`; leituras de fora do bloco são checadas na entrada. Leitor de Q só no início de um bloco.
- Operandos Inf/NaN viram ±MAX; denormais por DAZ/FTZ no MXCSR.
- Lanes FMAC com resultado ou produto no expoente 0xFE/0xFF, ou soma MADD/MSUB zero/subnormal, chamam `normalizeFmacResult` do próprio interpretador: igualdade bit a bit.
- Blocos guardados por hash das palavras; tabela por geração do código VU1.
- `PS2X_VU_JIT=0` desliga; `PS2X_VU_JIT_CHECK=N` roda 1 bloco em N também no interpretador e compara VF/VI/ACC/memória/ciclos.

**Validação:** 140,5 milhões de blocos sem divergência na cena de entrada da fase. Na cena seguinte, com amostragem 1/16: 2 divergências em 16,8 milhões (`0x458`, `0x470`). Essa cena executa instruções VU inválidas (`reserved lower 0xfffffffb`) também sem JIT, e a run só com interpretador (run99) mostra a mesma tela verde/magenta e as telas roxas: são defeitos de desenho/emulação, não do JIT. Teste novo `VU1 recompiler matches the interpreter bit for bit` (bloco real do jogo + cobertura das operações): registradores, ACC, I e memória iguais; ciclos até 4 a menos no fim (o JIT não drena flags mortas). Suíte VU: 84 passam, 1 falha antiga (GS assíncrono).

**Resultado:** VU1 de 94% para ~4–7% do tempo da fase; contador de quadros do jogo ~1,9× mais rápido que na run89 e ~4,4× que na run85 (estimativa: ~6–7 fps). Gargalo volta ao GS (67%): `SampleTexture`, `DrawTriangle`, `ReadP4`/`LookupCLUT`.

**Bandas do GS:** 8 bandas (hyperthreads) mediram levemente pior que 4 na fase; fica uma por núcleo físico.

**Progresso no jogo (run99, só interpretador, 19 min):** após a primeira cena, o jogo carrega outra área (sala avermelhada e mapa de Outworld), passando por longas telas corrompidas roxas/magenta.

### 5A.15 Fluxo VIF1 desalinhado, bloom e sprites: run102 a run121 [medido]
**Causa raiz da fase corrompida (run112/run113):** um `DIRECT 1` do VIF1 traz só uma tag GIF IMAGE; os dados da imagem vêm nos `DIRECT` seguintes, com VIFcodes (NOP, `DIRECT 5`…) entre eles. O runtime consumia os qwords **seguintes do fluxo VIF** como imagem (até 8 KB), engolindo comandos. Dali em diante o VIF1 lia dados como comandos: MSCAL para endereços impossíveis (`0x37a18`), XGKICK de lixo (`reserved 0xfffffffb`, 577 por run), microprogramas presos em laço, pontos PRIM com coordenadas absurdas e geometria sumindo (céu, Liu Kang, HUD). Correção em `ps2_vif1_interpreter.cpp`: a continuação de IMAGE só sai do payload dos `DIRECT` seguintes (a mesma conclusão do OGmidway, seção 6.4). Run114: 0 comandos VIF desconhecidos, 0 XGKICK inválidos, 0 microprogramas presos. O jogo entra em **Goro's Lair** com HUD, menu de pausa e mapa; na run119 avança até 2,4% do jogo.

**VU1 até o fim do microprograma:** `MSCAL`/`MSCNT` davam 65.536 ciclos à VU1; um programa maior era cortado e o MSCAL seguinte reiniciava a VU por cima. No PS2 o VIF espera a VU. Agora roda até o fim (teto de 4M ciclos, ~1 quadro, só contra laço infinito), em `ps2_runtime.cpp`.

**Bloom (medido com médias por etapa, run106):** o pipeline usa a área do Z-buffer (página `0x8c`) como rascunho:
1. Limpa a área.
2. Copia o quadro reduzido.
3. Multiplica por ×0,25.
4. Aplica H (`+param/2`) e I (`-param`), o limiar.
5. Faz 17 passadas `Cs·0x17 + Cd` lendo o próprio buffer com 3–5 px de deslocamento.
6. Compõe `Cs·0x20 + Cd` sobre o quadro.

O `param` (`0x4f6e70+0x28`) vale 0 nesta fase, gravado pelo próprio jogo (`0x2cd6c0`), então o limiar está desligado de propósito. O construtor de blend (`0x278050`) monta `ALPHA = FIX<<32 | modo`, conferido. Mudanças:
- `DrawFeedbackPrimitive`: um primitivo que amostra o próprio alvo lê uma cópia das páginas tirada antes de desenhar (cache de textura do GS; o PCSX2 usa `autoFlush` neste jogo). Antes, cada pixel via os taps escritos na mesma linha.
- `TEX1` com `LCM=1`: LOD fixo `K`; `K <= 0` usa o filtro de magnificação. As passadas do bloom (`TEX1=0x24d`) são por ponto, não bilineares.

**Pendente:** o quadrante superior esquerdo (320×112, a área do bloom dentro do Z-buffer) ainda fica branco em algumas cenas. A cena testa Z (`GREATER`) contra o resto do bloom, e o Z da cena (~2M, `0x22xxxx`) perde para o brilho acumulado. Nenhum desenho limpa o Z. Falta referência de hardware para saber se o ganho real das 17 passadas é menor ou se o Z da cena é maior. Telas de transição com lixo verde ainda aparecem.

**Desempenho:** caminho rápido no `DrawSprite` para sprites CT32 sem Z, teste de alpha, máscara, FBA nem filtro (busca CT32, blend, escrita CT32; mesma aritmética do caminho geral). Teste novo `plain CT32 sprites blend with FIX like MKSM's bloom passes` passa com e sem o caminho rápido; as listas de pass/fail da suíte GS são idênticas. **Fase: 15,2 → 20,7 quadros completos/s (+35%)**, medido na mesma build (run120 × run121); menus inalterados (~9–10).

**Testes:** memória 48/0 até o abort antigo do `EeScheduler`; VU1+VU 85/0 (a falha antiga do XGKICK era a fila assíncrona do GS: os testes agora leem `gs.ReadVram` antes da VRAM crua). Os dois testes que fixavam o comportamento antigo do DIRECT foram reescritos para o do hardware (continuação no próximo `DIRECT`, com `MARK` executando entre eles).


### 5A.16 Quadrante branco: o clear do `sceGsPutDrawEnv` saía vazio (run122 a run133) [medido]
**Sintoma:** em cenas da fase, o quadrante superior esquerdo (320×128 do buffer, a área do bloom) ficava branco e com buracos em blocos, e não se recuperava mais.

**Medido (run124 a run130):**
- A cena testa Z com `GREATER` contra a página `0x8c` e grava Z entre `0x04xxxx` e `0x1Axxxx` (quase tudo abaixo de `0x200000`).
- O bloom escreve o rascunho na mesma página (XYOFFSET 1888,1992, ou seja, linhas 0–127 do Z-buffer). As 720 passadas leem o próprio pixel deslocado de −3 a +5 px; cada passada soma 18% e, em 18 passadas, o rascunho cresce ~4,9× a cena reduzida. Medido no fim do quadro: média 100–170 no canal azul (o byte alto do Z24), com 40% dos pixels saturados.
- O limiar do bloom (`0x4f6e70+0x28`) vale 0xFF no ELF (bloom desligado). Na fase, `0x1e5858` copia 0x2C bytes do arquivo da fase para a estrutura, com limiar 0 e ganho `0x17`. São dados do jogo, não erro nosso (achado com página protegida + passo a passo no SIGSEGV).
- Nenhum desenho zerava o Z: era um ciclo. O quadrante ficava branco, o bloom seguinte copiava o branco e o Z continuava bloqueado.

**Causa:** o jogo chama `sceGsPutDrawEnv` a cada quadro, e o pacote termina com 6 registros de clear (TEST ALWAYS, SPRITE 640×224 com cor 0 e Z 0, TEST de volta) montados por `sceGsSetDefClear` (`0x383560`). Nosso stub dessa função não fazia nada: os 6 registros saíam zerados, o clear não acontecia e o Z do quadro seguinte era o resto do bloom. O OGmidway registrou o mesmo sintoma ("stale depth and white accumulation", STATUS.md) e corrigiu o `PutDrawEnv`, que nós já tínhamos portado. Faltava esta outra metade.

**Correção:** `sceGsSetDefClear` em `Kernel/Stubs/GS.cpp` agora espelha a rotina original: TEST(ZTE, ALWAYS), PRIM SPRITE, RGBAQ com Q=1,0, dois XYZ2 com o `z` pedido (b, a e z vêm da pilha EABI) e o TEST de volta com o `ztest` pedido, retornando 6. Teste novo `sceGsSetDefClear writes the retail clear sprite packet` compara as 12 palavras com as da rotina original. Suíte GS: 41/34 (as mesmas 34 falhas antigas, de antes da mudança).

**Run133:** sem quadrante branco em nenhum dos 25 quadros capturados da fase. O mapa e a pausa também ficaram limpos. Telas de transição com lixo verde continuam pendentes.

**Descartado:** o `TEST.ZTE=0` (o runtime ignora o bit) não muda nada nesta fase; ficou de fora.


### 5A.17 Velocidade da fase: onde o tempo vai de verdade (run134 a run147) [medido]
**A medida certa é vsync/s, não o contador de composições.** O `[gs:rate]` antigo contava ~3 trocas de framebuffer por quadro do jogo. O jogo desenha 1 quadro a cada 2 vsyncs (30 fps no PS2) e espera o resto em `0x274688`, girando sobre o contador de vsync em `0x67B79C`. O vblank do scheduler só dispara quando **os dois** prazos chegam: o de tempo real (16,7 ms) e o de ciclos do EE emulado (4,9 M ciclos). Na fase, o EE andava a ~70–90 MHz (o PS2 roda a 294,9), ou seja, 14–19 vsyncs/s, ~7–9 quadros reais do jogo por segundo.

**Perfil por thread (CPU de cada thread e um perfil só da `GameThread`):**
- `GameThread` 94–99%, worker do GS 90–100%, 3 bandas a 44%.
- Na thread do jogo: `advanceEeTimers` 15,8% (rodava após cada bloco do jogo), IOP interpretado ~30% (compilado com `-O1`), VU1 ~13%, logs ~5% e o código do jogo em si só ~9%.
- O EE ficava 23–30% do tempo bloqueado com a fila do GS cheia.
- No worker do GS, os ~620 sprites de feedback do bloom por quadro (32×32, lendo o próprio alvo) ocupavam 30–42% do tempo, em série.

**Mudanças:**
1. **Feedback do bloom em paralelo** (`DrawFeedbackPrimitive`): cada pixel lê a cópia tirada antes do primitivo, então as linhas vão para as 4 bandas sem mudar o resultado. Worker: 36% → 22% do tempo nesses sprites.
2. **Spans de triângulo calculados** (`DrawTriangle`): cada linha resolve as três arestas e testa só o trecho coberto, com 2 colunas de folga. O teste por pixel continua decidindo, então a saída é idêntica. Triângulos de guard-band têm bbox de até 4000 px. Depois disso, o EE deixou de esperar a fila cheia do GS (23–30% → ~0% nas mesmas cenas).
3. **Timers do EE em lote** (`EeScheduler::accountCycles`): acumula até 1024 ciclos ou até o próximo disparo de timer. Some do perfil (era 15,8%). *Atalho:* a contagem lida pelo jogo atrasa até 1024 ciclos, como diz o comentário no código.
4. **IOP com `-O3`** (`ps2xIOP/CMakeLists.txt`, mesma opção `PS2X_OPTIMIZE_RUNTIME_LIB` do runtime).

**A/B no mesmo binário (chaves temporárias, 2 execuções de 230 s de cada lado):** vsyncs totais 4806/4357 → 5302/4976 (**+10–14%**); média na fase (segunda metade) ~16,6 → ~20,6 vsyncs/s (**+24%**), com bastante ruído entre execuções. Telas iguais.

**Testado e descartado:**
- Cobrar mais caro o polling do CRI_ADXI no IOP (×16 em vez de ×4) liberou a thread do jogo (99% → 83%), mas não mudou o vsync/s, e mexe com o áudio.
- O FINISH não força espera no EE (nenhuma chamada de `Sync(Finish)` na fase).

**Próximos ganhos (proposta, em ordem):**
1. **Pular o laço de espera do quadro** (`0x274688`): adiantar o relógio do EE até o próximo vblank em vez de girar 32 ciclos por volta. É a técnica de *idle loop* do PCSX2, e libera a thread do jogo para o resto.
2. **Renderer do GS na GPU:** o rasterizador em CPU gasta ~100+ ns por pixel e é o teto do worker. Uma GPU resolveria o desenho e o bloom de uma vez.
3. Parar de logar `sceSifSetDma` (2 linhas por chamada, com `AGRESSIVE_LOGS` ligado por padrão). Exige recompilar o código gerado.


### 5A.18 Janela travada e cópia no meio do campo: os "2 fps / 0,5 fps" visuais (run170 a run174) [medido]
- **Sintoma:** visualmente ~2 fps no menu e ~0,5 fps na fase.
- **Medidor novo** (`PS2X_PERF=1`, ver `ARQUITETURA.md` §6): o jogo fazia ~18 campos/s no menu e ~15 na fase, mas só 4–5 imagens novas/s chegavam à janela no menu e 8–12 na fase. No menu, a própria janela caía para 20–35 Hz.
- **Causa 1:** a amostragem de pilha por tempo de relógio da thread da janela (timer dirigido à thread + `backtrace`) mostrou 43% do tempo em `pthread_mutex_lock` dentro de `GS::latchHostPresentationFrame`. Era o `m_stateMutex`, pego de forma bloqueante para gravar um evento de **depuração**, enquanto a GameThread o segurava esperando a fila cheia do GS.
- **Causa 2:** a cópia da VRAM que a janela mostra era tirada ao sair do FBP 0. No MKSM isso acontece no meio de um campo (antes de bloom/HUD) e só nos campos desenhados na página `0x00`.
- **Correção:**
  - os dois pontos de `latchHostPresentationFrame` usam `try_lock`; se ocupado, reaproveitam o último pedido ou pulam o evento de depuração;
  - a cópia passa a ser tirada na troca entre as páginas `0x00` e `0x46`, quando o campo anterior está completo.
- **Resultado:** janela a 60 Hz no menu; imagens novas 6–9/s no menu e 13–15/s na fase, igual ao ritmo do jogo. O teste antigo `latched host presentation frame stays stable until the next latch` passou a passar (GS 43/33). O limite agora é o desenho em CPU (`PLANO_DESEMPENHO.md`).


### 5A.19 GS na GPU, bloco 1: sprites do caminho rápido em Vulkan (03/11) [medido]
`PS2X_GS_GPU=1` liga o `GSGpuAccelerator`: a VRAM vira um buffer Vulkan mapeado, e os sprites do caminho rápido da CPU (bloom, cópias, sprites lisos com blend) são desenhados por um compute shader (`gs_sprite.comp`) com a mesma aritmética inteira da CPU.

Para sair bit a bit igual:
- **Swizzle:** a GPU usa a tabela CT32 gerada pelo próprio `GSMem::PixelIndexCT32`.
- **UV:** os texels por coluna e linha vêm da CPU, porque a divisão em float da GPU não arredonda igual.
- **Feedback:** o sprite copia antes as mesmas páginas que a CPU copia (`FeedbackPages`).

O replay das três gravações deu 0 bytes de diferença contra a CPU. Mais detalhes e próximos blocos em `PLANO_DESEMPENHO.md` e `ARQUITETURA.md`.


### 5A.20 GS na GPU, bloco 2: triângulos e sprites gerais em tiles (03/11) [medido]
`gs_tiles.comp`: um workgroup por tile de 16×16; cada thread é dona de um pixel e percorre os primitivos do lote, na ordem do GS. Isso preserva blend e Z sem disputa entre threads. Os formatos de 8/16 bits, em que dois pixels dividem uma palavra, usam `atomicAnd`/`atomicOr`.

Para sair igual à CPU:
- **Tabelas de página:** a GPU usa a mesma posição dentro da página de cada layout (`GSMem::PixelAddress`).
- **Float:** o backend da CPU agora compila com `-ffp-contract=off`, porque com `-march=native` o GCC fundia `a*b+c` em FMA onde quisesse. O shader usa `precise`.
- **Z:** interpolado em `double` nos dois.
- **Recíproco de Q:** refinado com `fma` na GPU.

Resultado: título e menu bit a bit iguais; fase com 1 pixel diferente por 1 unidade em 20 campos. A mudança de float tirou da lista uma falha antiga (`GS T4 CSM1 preserves CSA bit 4 for CT16 CLUTs`; GS 44/32).


### 5A.21 Telas verdes nas transições: imagem CT16 lida como CT32 (03/11) [medido]
Investigado por uma sessão paralela; os detalhes e a evidência estão em `notas/telas_verdes.md`.
- **Causa:** as telas de aviso legal e de "LOADING" são uma imagem CT16 de 512×448 desenhada na página 0, a mesma página do framebuffer da cena. Quando o display volta para CT32, a cópia de quadro completo que a janela mostra ainda tem essa imagem. O preto CT16 com bit de alfa (`0x8000`), lido como CT32, vira `0x80008000`: verde puro.
- **Correção** (`gs_cpu_backend.*`): o backend guarda o PSM da última escrita (desenho ou upload) de cada página de frame, junto com a cópia de quadro completo. Na apresentação, uma página escrita em outro formato sai preta.
- **Resultado:** 0 quadros verdes nas duas transições medidas (boot e menu→fase); GS sem falhas novas; replay sem mudança na VRAM.

### 5A.22 Câmera da fase olhando para a parede (05/11) [corrigido]
- **Causa:** o recompilador traduzia `SQRT.S fd, ft` lendo **fs**. No R5900 o operando é **ft** (bits 20–16); `0x460C0004` é `sqrt.s $f0, $f12`. Toda raiz quadrada do jogo (~90 lugares em 58 arquivos) tirava a raiz do que estivesse em `$f0`.
- **Como chegou na câmera:** a câmera (struct em `0x6cba50`: alvo `+0x70`, olho `+0x80`, ângulos `+0x90`/`+0x94` em unidades de 4096 = 360°) calcula pitch = `atan2(dy, isqrt(dx²+dz²))` e yaw = `atan2(dx, dz) − 2048` em `func_2EA2A8`. O `isqrt` (`func_215DA0`) é `cvt.s.w` + `sqrt.s` + `cvt.w.s`; com a raiz errada, a distância horizontal saía 0 e o pitch ±90°. A base da câmera (`0x511340`–`0x511360`, montada em `func_2EBC20`/`func_2EB4D0`) e os planos do frustum (`0x77f920`) vinham desses ângulos, e o jogador ficava fora do frustum.
- **Pistas que levaram até lá:** RAM em disco (`PS2X_DUMP_RAM_AT`), a memória da VU1 com a matriz de projeção, o struct da câmera e os 6 planos do frustum. O `atan2f` do jogo (`0x2084B8`), testado sozinho, estava certo, então o erro vinha das entradas. A instrumentação temporária de `func_2EA2A8` mostrou o pitch preso em ±1024.
- **Resultado:** aos 80 s o Liu Kang aparece em guarda no Goro's Lair. A fase ficou mais lenta (14–20 campos/s), porque agora desenha os personagens e a cena de verdade.
- **Histórico da investigação** (mantido abaixo):
- **Sintoma** (relatado jogando): na fase a câmera mostra a parede/rocha de perto, não os personagens.
- **Não é regressão de 04/11:** a gravação `gsrec/fase.gsr`, feita em 03/11, já mostra o mesmo quadro (rocha escura em primeiro plano, HUD do Liu Kang, nenhum personagem). Ver com `ps2x_gs_replay gsrec/fase.gsr --ppm <prefixo>`.
- **Corrigido no caminho (não resolveu a câmera, 05/11):**
  - `CVT.W.S` arredondava para o mais próximo (`nearbyintf` no modo do host) e estourava fora de int32. O R5900 trunca e satura em ±(2³¹−1). Agora a conversão sai inline do recompilador (298 arquivos, 1.224 usos).
  - Macros de VU0: `VRSQRT` calculava `1/sqrt(ft)` ignorando `fs` (a instrução é `fs/sqrt(|ft|)`); `VDIV` dava 0 com divisor zero (o certo é ±máximo); `VSQRT` zerava negativos (o certo é `sqrt(|ft|)`). Agora usam `ps2_fdiv`/`ps2_fsqrt`/`ps2_frsqrt`.
  - Mesmo quadro depois das duas correções, em 72 s e 80 s.
- **Evidência nova (05/11):**
  - Segurar direita/cima a partir de 63 s muda um pouco o quadro e a barra de vida: o jogo responde e a câmera se move, mas segue colada no cenário e o personagem não aparece.
  - **Armadilha que já me pegou:** `VOPMSUB` usa a **mesma** permutação do `VOPMULA` (fd.x = ACC.x − fs.y·ft.z), e o programa troca os operandos (`OPMULA ACC, a, b` + `OPMSUB d, b, a`). O código gerado e o interpretador da VU1 estão certos, e o teste `VU0 OPMULA and OPMSUB use cross-product lane permutations` garante isso. "Corrigir" para outra permutação quebrou os produtos vetoriais. Isso mudou a câmera para uma vista de cima do chão, o que mostra que ela depende deles. Revertido.
- **Fase e estado (RAM em disco, 05/11):** é o tutorial do **Goro's Lair** (Raiden pede "L2: lock-on, block"). Com o auto-pad parado antes da fase, o relógio do jogo anda e os inimigos tiram vida de um Liu Kang que não aparece. As capturas antigas pegavam o jogo quase todo em pausa (o auto-pad aperta Start em ciclo).
  - **Jogador existe e se move:** raiz em ~`0x5e6c20`/`0x9edd20`, de (−3421, −930, 950) a (−3390, −930, 607). O esqueleto (matrizes dos ossos) fica em ~`0xf807d0`, com y de −1400 a −1850. **y negativo é para cima**: os pés estão em −930.
  - **Câmera fixa candidata:** `0x16ce100` (dado do nível, valores redondos): rotação ortonormal com translação (−4197, −393, −184). Lida como câmera→mundo, o eixo Z dela aponta para o jogador (cos 0,98). A ~540 unidades **abaixo** do chão, ela veria rocha por dentro. Há outra matriz com a mesma rotação em `0x16ce140` (posição −758, −1321, 673), o que sugere luz ou projetor em vez de câmera. **Não confirmado:** falta achar a matriz que a VU1 usa de fato no quadro.
  - **Próximo passo:** capturar a memória da VU1 junto com a RAM, achar a matriz mundo→tela usada pelo cenário, ver onde o jogador cai na tela e de que nó de câmera ela vem.
- **Já descartado:** leituras de flags MAC/STATUS (todas logo depois de `vsub`, que já é exato via `ps2VuSub`); instruções não implementadas (nenhuma no código gerado); `VFTOI`/`VITOF` (truncam como a VU).
- **Onde procurar:** a câmera é calculada no EE (matemática vetorial em VU0, modo macro e microprogramas). Suspeitos: instruções VU0 do código gerado (`ps2_vu_clip.h`, `ps2_vu_sub.h`), arredondamento de float, o teste de colisão da câmera contra o cenário (que a empurra para dentro da parede) e o PATH de entrada (o auto-pad não mexe na câmera). Os símbolos `SelectFreeCam` (`0x1AA558`) e `SelectFreezeCam` (`0x1AA5B8`) da seção 6.4 podem ajudar a isolar.
- **Pendente:** as telas CT16 continuam pretas. Mostrá-las exige ler as 448 linhas em vez da metade dobrada pelo FFMD.

---

### 5A.23 Triângulos escuros no chão do Goro's Lair (09/11) [medido, em aberto]

Triângulos escuros de borda dura no chão e nas paredes da fase (cutscene e gameplay).
- CPU e GPU dão o mesmo resultado: não é o rasterizador da GPU.
- Não é o teste de Z GEQUAL (tolerância de até 4096 não muda nada), nem a neblina (desligada, continua), nem as passadas de multiplicação `0x89`/`0x58`.
- **É a cor dos vértices:** com RGB dos vértices forçado a `0x80`, o chão fica uniforme. A camada base do chão (texturas T4, Z GREATER, neblina) já sai com os triângulos.
- A cor vem de um pacote PATH1 da VU1: `XGKICK` no PC `0xEC0`, buffer em `0xDC0`, 3424 bytes, segunda tag no deslocamento 48 (`NLOOP=70`, `NREG=3`, `ST, RGBAQ, XYZF2`). Em ~0,4% dos canais de cor, a vaga do `RGBAQ` traz um XYZ (X/Y ≈ `0x7Exx`, Z de 24 bits, `F` no último campo); o byte de baixo vira uma cor qualquer.
- A memória da VU1 ainda tem o XYZ na vaga da cor quando o `XGKICK` termina: não é corrida de leitura do PATH1, o microprograma grava ali. JIT e interpretador dão o mesmo número (5130, 14055, 41061 nos três primeiros relatórios): é semântica comum aos dois.
- Cores normais chegam como `0x4B0000xx` (float + 2^23, truque do jogo para converter em inteiro).
- **Próximo passo:** desmontar o microprograma em volta de `0xEC0` e achar quem grava a vaga `RGBAQ` (provável: cálculo de endereço com VI, atraso de VI/desvio, ou `ISW`/`SQI` com incremento).

### 5A.24 Losangos brancos nos raios de luz (09/11) [medido, em aberto]

Os "losangos" são os raios de luz que descem do teto (e a coluna de luz em volta do pilar): saem como barras/cunhas brancas sólidas de borda dura em vez de feixes translúcidos.
- A textura do raio (T4 em `0x2F5F`, paleta CT32 em `0x2F8B`, 128×128) tem listras em degradê de cor e **alfa `0x80` em toda ela**: a transparência vem só do alfa dos vértices (modulação, blend `0x44` normal).
- No pixel de uma barra branca, o vértice chega `7F7F7F80` (cinza 127, alfa cheio) e cobre o fundo; outros desenhos do mesmo raio chegam com alfa `0x30` (translúcido, certo). A coluna do pilar (T4 em `0x2728`) tem o mesmo padrão: alfa de vértice `0x00`/`0x13`/`0x25`/`0x6D` na maioria, mas 828 vértices em `0x80` e 40 em `0xFF` (num quadro da `fase2`).
- `0xFF` no truque de conversão do jogo (float + 2^23) é um valor negativo que não foi limitado; `7F7F7F80` tem cara de cor padrão. Suspeita: o microprograma da VU1 que gera esses vértices toma o caminho errado (desvio por flag/`CLIP`/`FCAND`) ou calcula errado a cor/alfa.
- `MAX`/`MINI` da VU no interpretador comparam com float do host; a VU compara como inteiro com sinal e módulo. Com a comparação exata, vértices do raio com alfa > `0x80` caem 3695 → 2229 (o JIT dá o mesmo que o interpretador sem a correção: a diferença é o sinal do zero, `MAX(+0,-0)`), mas as barras brancas continuam. Não aplicado: precisa ir para o JIT junto e não resolve sozinho.
- Os triângulos escuros do chão (5A.23) são outro defeito.
- **10/11, seguindo o raio (TEX0 `0x2F5F`) do VU1 até o pixel, tudo confere com o que o jogo manda:**
  - Os envios saem do buffer `0x2D00` da VU1 (`XGKICK` em vários PCs: `0xB10`, `0x1280`, `0xEC0`, `0x760`). A cor `7F7F7F80` é dado de entrada (VIF V4-8 com sinal: `0x7F,0x7F,0x7F,0xFFFFFF80`, passo `0x30` em `0xE10`), repassada pelo microprograma: não é conta errada da VU1.
  - Geometria: tira de 42 vértices; os fora da tela vêm com ADC=1 (o frontend respeita, `vertexKick(!adk)`). Sobram as faixas 2–7, 22–27 e 30–35, que usam só U de 0,87 a 0,98 da textura (uma listra larga e lisa): daí o cinza uniforme das barras.
  - Paleta CT32 16×2 em `0x2F8B`: 13 entradas cinza com alfa `0x80` e 3 transparentes, nunca usadas pela textura. Textura subida como CT32 64×32 e lida como T4 128×128: índices em listras limpas, 0–12 (layout certo; o CSM1 8×2 também está certo, `swizzleClutIndexCSM1` troca os bits 3 e 4).
  - Registradores no desenho: PRIM `0x5C` (tristrip, Gouraud, textura, blend), TEX1 com mipmap (`MMIN=5`, `MXL=2`, K ≈ −12,5), TEST `0x7380B` (alfa GEQUAL `0x80`, AFAIL=RGB_ONLY, Z GREATER), CLAMP 1, ALPHA herdado `0x44` (FIX `0x80`), FBA=1, COLCLAMP=1. Pelo protocolo, o raio sai opaco também no PS2.
- Sem imagem de referência do jogo real não dá para apontar a divergência. O mipmap não é emulado (amostra só o nível 0); fora isso, o GS segue os comandos.
- **10/11, com print do jogo real (cena do Raiden):** no real não há barras brancas (só um brilho verde fraco no canto superior direito), e a cena toda é mais clara e esverdeada, com halo forte.
  - A textura do raio aparece com dois blends: `ALPHA 0x48` (aditivo, classes 24/86, deve ser o brilho fraco do real) e `ALPHA 0x44` (normal, classes 32/93 e 37/98: as barras opacas). O raio não escreve `ALPHA`: herda o último.
  - Ordem de envio ao GIF (registrada na submissão, antes do árbitro): em cada quadro, um `DIRECT` PATH2 de 112 bytes põe `ALPHA=0x44` logo antes dos 5 pacotes PATH1 do raio. `MSCAL` roda o programa da VU1 até o fim na hora, e PATH1 também despacha na hora: é a ordem do próprio fluxo VIF1, não uma inversão nossa.
  - O VIF aplica STMOD/ROW certo; o alfa `0xFFFFFF80` vem do dado (V4-8 com sinal, modo 0, máscara só de preenchimento).
  - Conclusão: com os comandos que temos, o GS desenha o raio opaco; no hardware ele não aparece, então ou falha no Z lá ou a VU1 real gera outra geometria/ADC. Sem trace de referência do GS não dá para separar.
  - Cadeia de brilho conferida: Z como textura → `0x12C`/`0x14C`; quadro → `0x8C` (memória do Z reaproveitada como cor) com desfoque de 1440 sprites; soma ao quadro com FIX `0x20`. O buffer `0x8C` sai certo (cena desfocada, ¼ de resolução) e a soma sobe o brilho médio de ~27 para ~43. O halo do real parece mais forte.
- **11/11, duas hipóteses descartadas com medição (replay `fase2`):**
  - *Mipmap:* o raio usa TEX1 com `MMIN=5`, `MXL=2`, `L=0`, `K=-12,5`, e nós só amostramos o nível 0. Mas o LOD calculado com o Q dos vértices fica entre −2 e −1 em todos os 280 triângulos do raio (`0x2F5F`): o GS real também usaria o nível 0. A coluna do pilar (`0x2728`) nem usa mipmap (`MMIN=1`, `MXL=0`).
  - *Z:* Z buffer `PSMZ24`, Z dos vértices entre ~0,95 e 1,7 milhão (cabe em 24 bits, sem limitação), `ZMSK=0`, teste GREATER. Nada fora da faixa.
  - Sobra o que já estava anotado: o alfa `0x80` vem do dado montado pela EE (pacote VIF). O próximo suspeito é o código da EE que calcula o alfa de fade do raio (truque float + 2^23).
- **Próximo passo:** um dump de GS do PCSX2 (`.gs`) desta cena, para comparar registrador a registrador com o nosso fluxo (PCSX2 não está instalado nesta máquina), ou achar a função da EE que escreve as cores do raio no pacote.

## 6. Descobertas do Repositório OGmidway/mksm-recomp

Uma pesquisa aprofundada no repositório [OGmidway/mksm-recomp](https://github.com/OGmidway/mksm-recomp) (outubro/2026), que realiza a recompilação estática para PC do mesmo jogo e versão (Mortal Kombat: Shaolin Monks, SLUS_210.87 NTSC-U) usando a mesma base (PS2Recomp), trouxe revelações fundamentais sobre o bloqueio em `0x379220`, a arquitetura do CRI/ADXF, e o progresso até o menu e gameplay:

### 6.1 O que o projeto OGmidway já alcançou
- **Boot limpo, vídeo de introdução Midway, aviso de autosave, menu principal, seleção de personagens (Liu Kang / Kung Lao) e diálogo de dificuldade**.
- Reprodução completa de cutscene/vídeo com áudio (`opening.sfd` com mais de 6.700 quadros decodificados via FFmpeg bridge conectada ao anel de vídeo CRI).
- Entrada na fase **Goro's Lair** após o loading (geometria 3D texturizada renderizada no GS).
- Suporte a leitura de controle (Cross/Start e analógicos) no guest via DMA de pad.

### 6.2 O Bloqueio em `0x379220` e o "Deadlock de Heap/Loading" desvendados
O projeto do OGmidway passou pelo **exato mesmo travamento em 0x379218 / 0x379220** com o cursor em `0x4EF7EC`. Eles isolaram as causas raízes e implementaram correções cruciais:

1. **Corrupção de Heap pela Pilha de IRQ (`EeScheduler.cpp`)**:
   - **Causa**: No `EeScheduler::dispatchIrq`, o tratador de IRQ recebia o `handler.sp` (`0x1FFFF90`) registrado no guest. Esse endereço colidia com a pilha usada pelo `malloc` suspenso (`0x1FFFEC0`), sobrescrevendo registradores salvos (`s1` de `0x179BFB8` virava `0xF82`).
   - **Efeito**: Isso deformava o cálculo do fim do heap em `SYSTEM_MEMORY` (`alloc_blocks` em `0x64E998`), fazendo a alocação assíncrona do buffer em `0x20F058` falhar e retornar ponteiro nulo para o endereço `0x4EF7EC`.
   - **Correção**: Em `EeScheduler.cpp`, zerar o SP na invocação de IRQ (`SET_GPR_U32(&invocation.context, 29, 0u);`), forçando o scheduler a alocar uma pilha de invocação isolada.

2. **Bug de Branch com Sinal de 64-bit no PS2Recomp (`control_flow_emitter.cpp`)**:
   - **Causa**: O EE R5900 possui registradores de 128-bit e instruções como `BLEZ`, `BGTZ`, `BLTZ`, `BGEZ` comparam os 64 bits inferiores com sinal. O recompiler emitia `GPR_S32(ctx, rs) >= 0`.
   - **Efeito**: No arquivo 35 do `GAMEDATA.WAD`, a entrada compactada possui bit 63 setado e bit 31 limpo (`0x803aa0026d400028`). O `BGEZ` em `0x211900` tomava o ramo errado, gerando uma requisição de tamanho gigantesco que estourava o heap do jogo.
   - **Correção**: Emitir `GPR_S64(ctx, rs)` em todos os 12 formatos de branch com sinal no recompiler.

3. **Retorno de `WakeupThread` e `ResumeThread`**:
   - **Causa**: No PS2Recomp upstream, `WakeupThread` e `ResumeThread` retornavam 0 em sucesso.
   - **Efeito**: O despachante do CRI e rotinas de sincronização do MKSM (`0x4154f8` e `0x415580`) comparam explicitamente o retorno da syscall com o ID da thread alvo para limpar a flag de espera (`0x53417c`). Retornar 0 fazia o jogo achar que a chamada falhou, congelando o servidor de streaming.
   - **Correção**: Retornar o ID da thread em vez de 0 nas syscalls de thread acordada.

4. **Preservação de VF0.w = 1.0f constante de hardware**:
   - Em callbacks e novas threads (`GuestInvocation` e `startThread`), `vu0_vf[0]` precisa ser sempre inicializado como `(0, 0, 0, 1.0f)`. Falhas nisso geram NaNs no motor de física Havok (`0x3A0380`).

5. **Skip de Vídeo / Intro Nativo para Testes Rápidos**:
   - O jogo possui uma flag nativa em `0x513a04`: setar temporariamente para `1` durante a execução de `0x1d32e0` ativa o ramo de bypass da introdução direto para a tela de título/autosave.

6. **Módulo IOP Dedicado para CRI e SNDF**:
   - O OGmidway implementou em `ps2xIOP/src/modules/mk_cri.cpp` e `mk_sfsv.cpp` o perfil `mk-shaolin-monks-us` para responder aos RPCs `0x90000200` (CRI ADX DTX) e `0x53465356` / `0x534E4446` (SNDF áudio). A devolução correta de blocos de descritores no bridge SJX evita que workers de áudio entrem em espera infinita por espaço na fila (16 KiB room queue).

### 6.3 Mapa de Símbolos Reais (Ghidra / Protótipo)
O OGmidway identificou os nomes originais dos subsistemas que estávamos analisando:
- `0x378A30`: `Async_DataLoad` (a máquina de estados principal)
- `0x2123C8`: `AsyncLoadStatus` (o poll da máquina de streaming)
- `0x211878`: `GetAsyncFileSize`
- `0x20ECD0`: `Memory_Initialise`
- `0x20F058`: `Memory_AllocBlock`
- `0x2123A0`: `StartAsyncLoadFile`
- `0x370A70`: `cPxFileInterface_StartFileLoading`
- `0x370A98`: `cPxFileInterface_FileLoadStatus`
- `0x370AE8`: `cPxFileInterface_CloseFileHandle`
- `0x4EF7E0`: `async_load_info`
- `0x64E998`: `alloc_blocks` (tabela de heaps do jogo)
- `0x755AB8`: `async_info` (bit de ocupado e estado da máquina `Async_DataLoad`)

---

### 6.4 Roteiro de Armadilhas Futuras Descobertas pelo OGmidway (Para Quando Passarmos do Menu)

O histórico de engenharia reversa do OGmidway documenta exatamente os problemas que enfrentaremos assim que a tela de loading for superada e a renderização 3D começar:

#### 1. Gráficos & GS (A Corrupção da Caverna de Goro)
- **Acúmulo de Branco / Tela Branca por Falta de Clear (`sceGsPutDrawEnv`)**:
  - `sceGsPutDrawEnv` (em `0x383668`) recebe um ponteiro para um GIF-tag seguido de quadwords com comandos de clear de cor e profundidade. O HLE original do PS2Recomp tratava apenas 8 pares de registradores e descartava o clear.
  - *Solução do OGmidway*: Submeter o pacote completo `NLOOP+1` via GIF DMA direto (`mem.writeIORegister(0x1000A000u, 0x101u)`).
- **VIF DIRECT corrompendo Geometria**:
  - Dados de textura IMAGE pendentes no GIF eram engolidos no topo do loop de comandos VIF, desalinhando o fluxo e fazendo o runtime interpretar bytes de textura como comandos de instrução VIF (disparando programas VU1 inexistentes com `XGKICK` explodindo).
  - *Solução*: Consumir bytes de IMAGE pendentes apenas dentro de payloads `DIRECT/DIRECTHL`.
- **Tela Duplicada Verticalmente no Menu / Autosave**:
  - O modo de vídeo `SMODE2=3` (interlaçado + frame mode) exige que o backend do GS leia apenas `ceil(altura/2)` (224 linhas) e dobre na apresentação do host (bob deinterlacing). Ler 448 linhas puras pegava o frame buffer adjacente e duplicava a imagem verticalmente.
- **AutoFlush de Textura e Bloom Excessivo**:
  - O jogo reaproveita o mesmo espaço de memória VRAM para framebuffer scratch (`140`) e leitura de textura (`4480`). No PCSX2, o hack específico para MKSM é `autoFlush = 2`. Sem controle de página/flush, o efeito de bloom satura os canais RGB rapidamente até o branco puro.

#### 2. Entradas e Controle (`libpad`)
- **Controle Travado / Botões Pressionados Direto**:
  - O HLE do `scePadRead` devolvia os botões corretos na syscall, mas não atualizava o buffer de DMA bruto registrado em `0x651F00`.
  - Como o wrapper do jogo (`0x212590`) lê os bytes diretamente desse endereço de DMA em vez de confiar apenas no retorno da syscall, o jogo via `0xFFFF` (todos os botões pressionados simultaneamente).
  - *Solução*: Publicar a estrutura de 32 bytes sincronizada nos dois bancos de 128 bytes do DMA de pad.

#### 3. Física, Culling e VU0/VU1 (Havok)
- **Culling Invertido no `VCLIP`**:
  - A ordem dos bits dos planos de clipping no helper `VCLIP` do PS2Recomp estava invertida, fazendo esferas visíveis serem descartadas e esferas fora da câmera serem aceitas.
- **Macro `VSUB` e Flags de Hardware**:
  - O macro `VSUB` de VU0/VU1 não atualizava corretamente as flags `MAC`/`STATUS` e podia poluir `VF0`. No Havok, loops de broadphase (`0x3A0380`) que usavam comparações com sinal travavam com valores negativos de registradores (`s7 = -57555`).

#### 4. Scratchpad DMA (SPR)
- **Bits de Endereçamento do Scratchpad**:
  - O SDK codifica ponteiros de Scratchpad como bit 31 setado (`0x80000000 + offset`) via `DmaAddr` (`0x3841D0`).
  - O helper `toDmaPhys` do PS2Recomp limpava o bit 31 e mandava o DMA ler da RAM principal (`0x00000000`), corrompendo geradores de partículas e efeitos de tela.
  - *Solução*: Mapear a faixa `0x80000000` para a janela canônica `PS2_SCRATCHPAD_BASE` (`0x70000000`).

#### 5. Cheats & Recursos de Debug Originais Mapeados
- O executável de varejo mantém internamente as funções do menu de desenvolvimento da Midway:
  - `SelectFreeCam` (`0x1AA558`): ativa câmera livre
  - `SelectFreezeCam` (`0x1AA5B8`): congela a câmera
  - `GodModeToggle` (`0x1AAAD8`): invencibilidade
  - `DebugMenuSetup` (`0x1AAE80`) e `DebugMenuUpdate` (`0x1A88D8`): menu de depuração completo

### 6.5 Detalhes Exatos de Código dos Overrides do OGmidway

Para quando formos aplicar as correções no runtime, aqui estão os trechos exatos de overrides descobertos pelo OGmidway em `loading-trace.cpp` e `mk-mpeg-bridge.h`:

#### 1. Redirecionamento de `sceCdLayerSearchFile` (`0x385CE0`)
O MKSM usa `sceCdLayerSearchFile` para consultar arquivos em camada de disco. Como usamos disco extraído de camada única:
```cpp
void mkLayerSearch(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    if (GPR_U32(ctx, 6) != 0) {
        sub_00385CE0_0x385ce0(rdram, ctx, runtime);
        return;
    }
    const uint32_t result = GPR_U32(ctx, 4);
    ps2_stubs::sceCdSearchFile(rdram, ctx, runtime);
    // Se encontrou o arquivo, zera o campo de flags da camada (offset +32)
    if (GPR_U32(ctx, 2)) WRITE32(result + 32u, 0u);
}
```

#### 2. Layout do Picture Buffer MPEG no MKSM
O SDK da Sony mudou o layout interno da estrutura `sceMpeg` ao longo dos anos. O PS2Recomp upstream usa os offsets padrão de outro jogo (`0xb0, 0xd8, 0xe4, 0xdc, 0xe0`). No MKSM NTSC-U, os offsets corretos são:
```cpp
constexpr ps2_stubs::MpegPictureGuestLayout mkMpegLayout{0xc4, 0xec, 0xf8, 0xf0, 0xf4};
```
Ao chamar `sceMpegCreate` (`0x467D48`) ou `sceMpegReset` (`0x468950`), é necessário inicializar o estado com esse layout:
```cpp
ps2_stubs::initializeMpegPlaybackState(getRegU32(ctx, 4), mkMpegLayout);
```

#### 3. Sincronização do Anel de Vídeo CRI com Registradores IPU_TO
Para que a rotina de stop/restore de vídeo do CRI (`0x44C2D8`) saiba quantos bytes o decoder aceitou sem crashar o DMA:
```cpp
auto& mem = runtime->memory();
mem.writeIORegister(0x1000b400, 5);
mem.writeIORegister(0x1000b410, (read(0) & 0x0fffffffu) + head * 2048u);
mem.writeIORegister(0x1000b420, 0);
mem.writeIORegister(0x1000b430, (read(4) & 0x0fffffffu) + head * 16u);
```

#### 4. Endereços das Rotinas do Pipeline de Bloom (Pós-Processamento)
Mapeados no Ghidra para quando formos corrigir o efeito de saturação da imagem:
- Blend Builder: `0x278050`
- XYOFFSET_2 Builder: `0x278520`
- Feedback Helper: `0x279F08`
- Setup de Pacotes: `0x276CA0` e `0x276F00`
- Composite Final: `0x27CF38`

---

## 7. Fatos do ELF e Runtime (Cheat Sheet)

* **Pseudo-LBN:** `kCdPseudoLbnStart=0x00100000`. LBNs lidos (ex: `0x100038`) caem dentro de `GAMEDATA.WAD` (base `0x100011`).
* **Tabelas de Streaming:**
* `0x54FB60`: Tabela do Estágio 5 (nosso alvo). 16 slots × `0x44`. Despachante em `func_413878`.
* `0x53C504`: Tabela dos Estágios 3/7. 0x28 entradas. Despachante em `func_418178`.

* **Handle CRI ADXF:** `0x54FB60` (+`i*0x44`). Status: `1`=STOP, `2`=READING, `3`=READEND, `4`=READERR. `[+0x20]` = setores da última transferência, `[+0xC]` = total, `[+0x14]` = cursor, `[+0x04]` = `stm`, `[+0x08]` = callback pendente.
* **Funções ADXF:** `413BE8` GetStat, `413B48` GetNumReadSct, `413268` ReadNw, `413690` Stop, `413128`/`4130F8` fecha handle. Wrappers do jogo: `26F640` (read), `26F6E0` (stat + pct de progresso), `26F790` (close).
* **Endereços de Thread:** `InitThread@0x480AF0`, `iWakeupThread@0x480BE0`, `StartThread@0x480D48`.
* **Códigos de Erro:** `KE_ILLEGAL_PRIORITY=-403`, `KE_UNKNOWN_THID`, etc. (Ver `Syscalls/Helpers/State.h`).

---

## 8. Armadilhas Conhecidas do Ambiente e Código

### 8.1 A Macro `PS2_IF_AGRESSIVE_LOGS`

Macro localizada em `ps2_log.h:189`:

```c
#define PS2_IF_AGRESSIVE_LOGS(code) /* if(AGRESSIVE_LOGS) { code; } */

```

**ATENÇÃO:** Esta macro aceita apenas **1 argumento**.

* NUNCA coloque arrays inicializados com vírgulas: `uint32_t jt[5] = {0, 0, 0, 0, 0};` quebra o build (o pré-processador interpreta as vírgulas como 5 argumentos).
* Use o construtor vazio: `uint32_t jt[5]{};`.
* Declarações de variáveis, blocos `for` e `if` devem ficar **antes** da chamada principal do `RUNTIME_LOG(...)`, nunca embutidos nela.

### 8.1.1 Duração das runs
A falha da run31 vem perto do tick 3750, e o filme só começa depois disso: use `timeout 175`, não 60. Com `PS2_FUNCTION_LOG_TRACKER` ligado, `ps2_log.txt` passa de 1,4 GB. A função `dis` pula delay slots: para confirmar, leia o `.cpp` gerado.

### 8.2 Fish Shell Tricks

* Não use heredocs (`<< EOF`) para passar blocos de código.
* Não use `\n` em `sed` ou Python inline via terminal (pode quebrar strings em multilinhas indesejadas no C++).
* O terminal do KDE quebra linhas longas ao colar; scripts de uma linha em Python podem falhar.

---

## 9. Rotina de Build e Teste

**Build Incremental (Compilando o Runtime Editado):**

```fish
cd /run/media/valk/linux_arquivos/projetos/ShaolinMonks/PS2Recomp
cmake --build out/build -j2 --target ps2EntryRunner

```

**Execução e Captura Segura de Log:**

```fish
timeout 60 script -qfc "./out/build/ps2xRuntime/ps2EntryRunner ../extraido_full/SLUS_210.87" runNN_raw.log
tr '\r' '\n' < runNN_raw.log | grep -a "run:tick" | tail -3 | cut -c1-2000
tr '\r' '\n' < runNN_raw.log | grep -a "run:trans"          # ordem real dos eventos do ADXF
```

*Sempre usar `script` e `grep -a` para não perder o buffer do terminal com saídas longas.*

**Função `dis` (Adicione no ~/.config/fish/functions/dis.fish):**

```fish
function dis
    python3 -c 'import glob,re,sys; t=int(sys.argv[1],16); fs=[(int(re.search(r"sub_([0-9A-Fa-f]+)_",f).group(1),16),f) for f in glob.glob("/run/media/valk/linux_arquivos/projetos/ShaolinMonks/output/sub_*.cpp")]; s=max(x for x in fs if x[0]<=t); L=[l.rstrip()[:100] for l in open(s[1]) if l.startswith("    // 0x")]; print(s[1]); print("\n".join(L[:int(sys.argv[2])]))' $argv
end

```

```

## 10. Rumo ao 100% nativo e fps alto [futuro]

Objetivo: tirar a emulação que sobrou (GS, VU, DMA/VIF, IOP) e rodar acima de 30 fps (60/90/144) sem mudar a jogabilidade. Nada daqui começa antes do **pré-requisito**: o jogo inteiro correto a 30 fps no caminho atual. O caminho emulado é a referência para comparar cada passo (gravações `gsrec/` + `ps2x_gs_replay`, ver `ARQUITETURA.md` §6).

Convenção desta seção: **[lido]**/**[medido]** = já confirmado no projeto; **[hipótese]** = conferir antes de confiar.

### 10.1 Por que ser nativo não basta para fps alto [lido]
- **Ser nativo resolve a velocidade:** a lógica do EE já é C++ nativo, e um PC moderno a roda centenas de vezes por segundo. O custo hoje está no desenho emulado (GS/VU1), não na lógica.
- **Mas a lógica é presa ao quadro:** a espera de quadro em `0x274688` gira até o contador de vsync chegar a `último+2` (`ARQUITETURA.md` §4), ou seja, um tick de lógica a cada 2 vsyncs = 30 Hz. Física, animação, IA, timers e câmera avançam um passo fixo por tick.
- **Consequência:** destravar essa espera deixa o jogo 3x mais rápido a 90 fps (golpes, tempo, áudio fora de sincronia). Fps alto exige separar **tick de lógica** (fica a 30 Hz) de **quadro desenhado** (livre).

### 10.2 Opções avaliadas para fps alto
- **Interpolação entre ticks (escolhida):** a lógica segue a 30 Hz. O renderizador guarda o estado dos dois últimos ticks (matrizes de modelos, ossos e câmera) e desenha quadros intermediários interpolando com `alpha = tempo desde o tick / 33,3 ms`. É o método dos recomps de N64. Não mexe em física nem em combos.
- **Patch de delta-time (rejeitada):** achar cada lugar que soma "1 quadro" e trocar pelo tempo real. São centenas de pontos, com bugs sutis de colisão e física, e cada um precisa de teste próprio.
- **Lógica a 90 Hz (rejeitada):** rodar 3 ticks por quadro e dividir constantes. Muda janelas de combo e timings de luta.

### 10.3 Ordem dos passos
Cada passo só começa com o anterior medido. Cada um tem um **critério de pronto** comparável com o caminho emulado.

#### Passo 1 — Renderizador nativo (Vulkan) no lugar do GS emulado
- **Hoje [medido]:** o GS é emulado registrador por registrador; partes já rodam na GPU (`gs_gpu_accelerator.*`, seções 5A.15–5A.20), mas a semântica segue a do chip (páginas, PSM, CLUT, Z).
- **Alvo:** receber a geometria já transformada (saída do XGKICK da VU1: primitivas, texturas, estados) e desenhar direto em Vulkan, com texturas convertidas e cacheadas uma vez, resolução e proporção livres.
- **Como:** começar por um modo híbrido. Os pacotes GIF do XGKICK continuam sendo a interface, mas viram chamadas de desenho em lote em vez de escritas na VRAM emulada. DMA/VIF viram passagem de dados.
- **Riscos [lido]:** efeitos que dependem de truques exatos do GS: o bloom (`0x8C`, sprites de feedback), as cópias de Z (`0x12C`/`0x14C`), imagens CT16 lidas como CT32 (5A.21) e o clear do `sceGsSetDefClear` (5A.12/run133). Cada um precisa de um caminho dedicado.
- **Pronto quando:** as gravações `gsrec/menu.gsr` e `gsrec/fase.gsr` saem visualmente iguais ao backend atual (diferença por pixel abaixo de um limiar), sem efeito faltando.

#### Passo 2 — VU1 recompilada de antemão
- **Hoje [medido]:** JIT x86-64 em tempo de execução (5A.14), com cache do pipeline e desvios rápidos.
- **Alvo:** o jogo envia um conjunto finito de microprogramas (via `MPG`). Coletar todos, recompilar para C++ no build (como o ELF) e despachar por hash do microprograma. O VU0 em modo micro segue a mesma ideia.
- **Como:** um modo de coleta que grava cada microprograma novo (hash + código) jogando todas as fases; um gerador que os converte para C++; o JIT fica como reserva para hash desconhecido, com aviso no log.
- **Riscos:** microprogramas montados ou modificados em tempo de execução [hipótese]; semântica de pipeline e flags (MAC/STATUS/clip) e as armadilhas já vistas (`VOPMSUB`, 5A.22).
- **Pronto quando:** nenhum microprograma cai no JIT nas fases testadas, e os testes da suíte VU continuam passando.

#### Passo 3 — IOP, SPU2 e CD em HLE
- **Hoje [medido]:** o IOP é emulado; o CD (`mksm-cdvd`, 10 binds) e o ADX (música e falas, `ps2_spu2_stream.cpp`) já passam por atalhos no host.
- **Alvo:** trocar os módulos do IOP por código nativo: arquivos lidos direto do disco, áudio mixado no host (SDL), sem SPU2 emulado.
- **Riscos:** o jogo espera respostas RPC no tempo certo; sons VAG e efeitos que hoje passam pelo SPU2.
- **Pronto quando:** boot até a fase com o IOP desligado, áudio sem buracos e tempos de carga iguais ou menores.

#### Passo 4 — Interpolação entre ticks (60/90/144 fps)
- **Pré-requisito:** passo 1 (o renderizador precisa receber matrizes, não pixels prontos).
- **Mapeado até agora [medido, 5A.22]:** struct da câmera em `0x6cba50` (alvo `+0x70`, olho `+0x80`, ângulos `+0x90`/`+0x94`, 4096 = 360°); jogador com raiz em ~`0x5e6c20`; matrizes dos ossos em ~`0xf807d0`; base da câmera montada em `func_2EBC20`/`func_2EB4D0`.
- **Como:**
  1. Marcar o fim do tick no laço de `0x274688` (o jogo fica no tick; o host fica livre).
  2. Guardar o estado de cada objeto desenhado por tick, identificado de forma estável (endereço do objeto, não posição no buffer).
  3. Desenhar N quadros por tick, interpolando posição e rotação (slerp para rotação, ângulos com volta em 4096).
  4. Não interpolar o que muda de forma discreta: cortes de câmera, teleportes, HUD, partículas novas. Quando a diferença entre ticks for grande demais, usar o tick novo direto.
- **Riscos:** objetos que somem e aparecem entre ticks; skinning feito na VU1 a partir de matrizes que não estão no EE [hipótese]; latência de entrada maior (um tick de atraso).
- **Pronto quando:** 60/90/144 fps estáveis na fase, com o tempo do jogo igual ao de 30 fps (comparar o relógio da luta em 60 s reais) e sem tremor visível nos cortes de câmera.

### 10.4 Primeiro levantamento (pode começar já, sem mexer no runtime)
- Achar a matriz mundo→tela que a VU1 recebe de fato a cada quadro e de onde ela vem no EE (pendência herdada de 5A.22).
- Listar os microprogramas da VU1 de uma fase inteira (contagem, tamanhos, quantos se repetem) para medir o tamanho do passo 2.
- Confirmar quantos vsyncs por tick o jogo usa em cada modo (menu, fase, filme): `0x274688` com `último+2` pode não ser o único laço de espera.
- Registrar os achados nesta seção, com as tags **[medido]**/**[lido]**, como no resto do plano.
