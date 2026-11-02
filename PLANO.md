
```markdown
# 🗺️ ShaolinMonks: Plano de Investigação e Diário de Bordo

**Estado:** 01/11/2026 (Noite, após `run25`).  
Este é o documento técnico detalhado (diário de bordo e engenharia reversa). O `README.md` é o resumo de status para o repositório.  
*Convenção:* **[medido]** = visto em execução no log, **[lido]** = lido no código C++ gerado, **[memória]** = conhecimento arquitetural sobre o SDK do PS2.

---

## 1. Contexto e Objetivo Atual

Estamos portando Mortal Kombat: Shaolin Monks (PS2) usando recompilação estática via PS2Recomp. A CPU roda nativa em C++, mas o hardware auxiliar (GS, VU, DMA, IOP) ainda é emulado. 
O objetivo atual é **passar da tela de loading para o menu principal**. O jogo estava travando num *polling* de CD; após a intervenção no HLE (run24) passou a fazer leituras reais e a completar vários ciclos de leitura (run25), mas ainda termina num *spinloop* em `0x379220` quando o handle do CRI ADXF fica em `status = 1` (STOP) enquanto o loader espera `status = 3` (READEND).

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
