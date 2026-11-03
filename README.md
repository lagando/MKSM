
```markdown
# ShaolinMonks: recompilação estática de PS2 com PS2Recomp

Estado em 03/11/2026 (run101). Documento feito para uma IA ou pessoa nova continuar sem contexto. Convenção: **[medido]** = visto em execução, **[lido]** = lido no código, **[memória]** = vem da memória da IA sobre o SDK do PS2, conferir antes de confiar. Este é o documento principal; `PLANO.md` tem o mesmo conteúdo com mais detalhes de investigação.

## Objetivo

Gerar uma versão nativa (Windows e Linux) do Mortal Kombat: Shaolin Monks (PS2, USA, SLUS-21087 v1.02) recompilando o ELF para C++ com [PS2Recomp](https://github.com/ran-j/PS2Recomp) (clone sem fork, commit `e563698`). O código da CPU vira C++, mas o runtime ainda emula GS, VU, DMA e IOP. Não é port com renderizador reescrito (isso é o OpenShao, que só roda a versão de Xbox). O PS2Recomp avisa que VU e GS têm desempenho ruim, então jogável não é garantido.

Usuário: Weremir (handle "Lagando"), tem a cópia legítima do jogo.

**Regra legal:** ISO, ELF, `extraido*/`, `output/` (C++ gerado), binários e logs **nunca** vão para o GitHub. Só patches, `sm.toml`, scripts e documentação. Não ajudar a obter ROMs.

**Repositório:** privado em `https://github.com/lagando/MKSM` (SSH: `git@github.com:lagando/MKSM.git`).

## Estado atual

| Etapa | Estado |
|---|---|
| Extrair ISO, compilar ferramentas, analisar ELF, gerar `sm.toml`, recompilar (13.651 arquivos, 236 MB), compilar runtime (~3h30, 97 MB) | Feito |
| Bind `libcdvd` (loop em `0x385D80`) | Resolvido: override `mksm-cdvd`, 10 binds guest→HLE |
| Tela magenta | Resolvida: `PMODE`/`SMODE2` e layout de `sceGsDispEnv` |
| **Threads do jogo** | **Resolvido**: `CreateThread` com prio 0 retornava -403 (`KE_ILLEGAL_PRIORITY`). Fix no `EeScheduler.cpp:426`: `params.priority < 0` em vez de `< 1`. Agora ids 2 a 5, `Start -> 0`, `activeThreads=5` [medido, run19] |
| Primeiros `sceCdRead` de dados | Ocorrem: `lbn=0x100000` (`FILELIST.DIR`, 2 setores), `0x100011` (cabeçalho do `GAMEDATA.WAD`, `0x1d` setores), `0x10002d` (1), `0x10002e` (7), `0x100038`, `0x100035`, `0x100036`, `0x1a0328`, `0x1a14c9`, `0x1a33ac`. São pseudo-LBNs dentro da faixa de `GAMEDATA.WAD` (`0x100011` + `0x308b7`) [medido, run22/run24] |
| **Streaming (estágio 5)** | **Avançou**: `func_413878` percorre 16 slots × 0x44 a partir de `0x54FB60`, chama `func_413788` nos ativos; o jogo leu `FILELIST.DIR`, setores do `GAMEDATA.WAD` e o catálogo dos `.SFD` [lido, dis; medido, run24] |
| **Subsistema identificado** | O objeto `0x54FB60` é o **handle CRI ADXF** (`adxf_GetStat`, `adxf_ReadNw`, `adxf_Stop`, `adxf_GetNumReadSct` — strings em `0x5A45xx`/`0x5A47xx`). A tabela de 16 slots **é** a tabela de handles (passo `0x44`) [lido, run25] |
| **Máquina do loader** | `func_2123C8` (poll) **funciona e completa ciclos** 2→3→0 (`status` 2 = lendo, 3 = fim, 1 = parado) continuamente sem travar o heap [medido, run26] |
| **Injeção do callback (HLE)** | `slot[3] = 1` quando `status == 2` e `slot[3] == 0` (`ps2_runtime.cpp`). Continua ativa [medido, run26] |
| **Desbloqueio do Heap / WAD 64-bit** | **Resolvido na run26**: Corrigido branch `BGEZ` de 64 bits em `GetAsyncFileSize` (`0x211900`) e retorno das syscalls `WakeupThread`/`ResumeThread`. Cursor `obj14` não vira mais lixo e buffers de heap (`0x1EDB180`, `0x1E4D940`) alocam perfeitamente [medido, run26] |
| Filme CRI | Roda sem falha, mas tela preta: falta ponte MPEG e relógio de áudio |
| Frontend | Diálogos de save com texto e controle, com `MKSM_SKIP_INTRO=1` (run37); parte do texto quebrada |
| Menu | **Alcançado** (run38, com `MKSM_SKIP_INTRO=1`) |

Chance subjetiva (reavaliar a cada bloqueio): menu 45 a 60%, jogável 5 a 15%, jogo inteiro sem bugs graves menos de 5%. Maior risco: GS/VU numa Intel HD 630.

Para criar mods (personagens, fases, música, menus, trapaças), veja o [`MODDING.md`](MODDING.md).

## Bloqueio atual (run133)

**Goro's Lair jogável na tela:** a corrupção da fase era o fluxo VIF1 desalinhado. Um `DIRECT` com tag IMAGE engolia os VIFcodes seguintes como imagem, e daí vinham MSCAL com endereço absurdo, XGKICK de lixo, microprogramas presos e geometria sumindo. Corrigido, a fase renderiza com HUD, pausa e mapa, e o jogo chega a 2,4%. A VU1 agora roda cada microprograma até o fim, como o VIF real exige. A fase roda a ~20 quadros completos/s (caminho rápido para os sprites do bloom, +35%). O quadrante branco também foi resolvido (run133): o stub `sceGsSetDefClear` estava vazio, então o clear de cor e Z que o `sceGsPutDrawEnv` manda a cada quadro saía zerado, e o rascunho do bloom virava o Z do quadro seguinte. Falta: telas de transição com lixo verde. Detalhes nas seções 5A.15 e 5A.16 do `PLANO.md`.

### Antes (run101)

O jogo passa do loader, entra no filme CRI (`ApStart`, `0x26F918`) e roda 175 s sem falha (`vs=2931`, `dma=11441`). A tela preta é o filme: o decoder nunca recebe vídeo (seção 5A.5 do `PLANO.md`). Com `MKSM_SKIP_INTRO=1` o jogo pula a intro e mostra o primeiro painel real, agora com texto e controle: aviso de autosave, checagem do cartão e o menu No/Yes do save. **Menu principal alcançado na run38.** As listras coloridas são a tela de uma carga longa, não um bug (seção 5A.7). Seleção de personagens em ~2 min, sem piscar e com a imagem preenchendo a janela (seção 5A.9). Texto e magenta corrigidos (seção 5A.10). Desenho paralelo em 4 threads (seção 5A.11). **A primeira fase 3D já renderiza** (seção 5A.12), com branco estourado e textura rasgada. A VU1 agora é recompilada para x86-64 (seção 5A.14), e o limite voltou a ser o desenho. Depois da primeira cena o jogo carrega outra área, com muitas telas corrompidas no caminho. Falta também o filme. Correções da fase: delay slot no fim de função no recompilador (causa do salto para `0x55e25c`) e `sceDmaRecvN` para SPR. O fix `GPR_S64` agora está em todo o código gerado. Detalhes na seção 5A do `PLANO.md`.

## Histórico do bloqueio anterior (run25)

### Histórico curto: run22 → run24 → run25

- **run22**: a máquina ficava no **estado 1**; o HLE terminava a leitura (`status` 2→1) mas `func_2123C8` nunca retornava 0 nem -1.
- **run24**: a injeção de `slot[3] = 1` no runtime destravou o poll e o jogo passou a ler dados reais, mas o cursor `obj14` virava lixo (75777). A conclusão daquela run ("`status` nunca chega a 3") estava **incompleta**.
- **run25**: com um observador novo (`[run:trans]`, que loga **toda** mudança de `status`/`loopState`/`slot[3]`/`obj8`) ficou claro que a máquina **funciona e completa vários ciclos 2→3→0** antes de travar. O travamento final é outro: `status = 1` **com** `loopState = 2`.

### O que o run25 mediu [medido, run25]

Ciclo saudável (repetiu ~5 vezes nos primeiros 60 s):

```
[run:trans] st=2 loopState=2 obj3=1 obj8=0x549190   <- pedido de leitura aceito
[run:trans] st=3 loopState=2 obj3=1 obj8=0x0 cnt=29 <- READEND; callback consumido
[run:trans] st=0 loopState=0 obj3=0 obj8=0x0        <- handle fechado/zerado
```

Transição que antecede o travamento:

```
[run:trans] st=2 loopState=2 ... obj8=0x549190
[run:trans] st=3 loopState=2 ... obj8=0x0 cnt=1 pc=0x4180f0
[run:trans] st=1 loopState=2 obj3=0 obj8=0x0 cnt=0 pc=0x379220
```

E depois disso, **50 s sem nenhuma transição**: `loopState=2`, `loopCursor=0xc34a80`, `objw0=0x101` (`status=1`), `obj3=0`, `obj8=0`, `cnt=0`, `obj4` (`stm`) `=0x53ce10`, `objC=0x2d047`, `obj14=75777`, `pc=0x379220`, ~60 fps de janela mas `vs` quase parado.

Observação lateral: entre os ticks 30 e 540 (~9 s) o PC fica em `0x2130a0` com `loopState=0`; esse spin **se resolve sozinho** e não é o bloqueio.

### Por que `0x379220` não é o bug [lido, dis]

`0x379220` é o **delay slot** de `jal func_378A30` (0x379218). O PC amostrado pertence ao laço externo `0x379218–0x37925c`, que chama a máquina de estados e testa `[0x755AB8] & 1`. O spin é apenas sintoma: enquanto `func_2123C8` devolver "continua", o laço repete.

### Protocolo ADXF, agora sem ambiguidade [lido, dis]

O objeto em `[0x4FADEC]` é o handle **CRI ADXF** `0x54FB60` (o mesmo endereço da tabela de 16 slots × `0x44`): é um array de handles, e o slot 0 é o handle do loader. Strings que confirmam: `adxf_GetStat` (`0x5A4798`), `adxf_ReadNw` (`0x5A45A8`), `adxf_Stop` (`0x5A4608`), `adxf_GetNumReadSct` (`0x5A4768`).

Campos do handle (`0x54FB60` + `i*0x44`):

| Offset | Nome | Papel |
|---|---|---|
| `+0x00` | active | `1` = handle criado |
| `+0x01` | **stat** | `1`=STOP, `2`=READING, `3`=READEND, `4`=READERR |
| `+0x03` | cb flag | flag "processar" que a injeção HLE escreve |
| `+0x04` | stm | ponteiro do decoder (ADXT-like) |
| `+0x08` | sj/callback | objeto de callback pendente (`[+8][4]()` é chamado no fim) |
| `+0x0C` | total | tamanho total em setores |
| `+0x14` | cursor | setores já entregues |
| `+0x20` | cnt | **setores lidos da última transferência** (`adxf_GetNumReadSct`) |
| `+0x2C` | bufsize | `512` |

Funções:

- `func_26F6E0` (via `func_370A98`) = wrapper do jogo para `adxf_GetStat`:
  `s = adxf_GetStat()`; se `s == 4` ou negativo → `-1`; senão `cnt = adxf_GetNumReadSct()` → `[0x678444]`, `[0x678448] = cnt*100/[0x678440]`, e retorna `1` **somente se `s == 3`** (senão `0`).
- `func_413268` = `adxf_ReadNw`: `restante = adxf[0xC] - adxf[0x14]`; se o pedido for menor, usa o pedido; se der `0` → grava `adxf[1] = 3` e retorna; senão zera `adxf[0x20]`, prepara o stm e grava `adxf[1] = 2`.
- `func_413690` = `adxf_Stop`: se `adxf[1]` não for 1 nem 3, finaliza, grava `adxf[0x20]` = bytes lidos, chama `func_413088` (despacha `adxf[8][4]()`) e grava **`adxf[1] = 1`**.
- `func_413128` (via `func_4130F8`) = fecha/destrói o handle: se `adxf[1] == 2` chama `adxf_Stop`; libera `stm` (`adxf[4]`), `memset(adxf, 0, 0x44)`.
- `func_2123C8` (o poll):
  - `loopState == 0` → retorna `0` (nada a fazer);
  - `loopState == 1` → `func_370A70` (`adxf_ReadNw`) e grava `loopState = 2` **sem olhar o retorno**;
  - `loopState == 2` → `func_370A98` (GetStat); se `== 0` retorna (continua esperando); senão `func_370AE8` (fecha handle) e `loopState = 0`.

### Onde está o impasse

O loader só sai do estado 2 quando `status == 3` (ou 4/erro). Se o handle ficar em **`status == 1`** (STOP), `func_26F6E0` devolve `0` para sempre → espera eterna. É exatamente o estado final do run25.

Como `func_2123C8` ignora o retorno de `adxf_ReadNw`, qualquer falha de início (ou parada prematura) vira **deadlock silencioso** em vez de erro. Hipóteses ainda em aberto, ambas compatíveis com o log:

1. O wrapper `func_26F640` (bytes→setores) está pedindo **0 setores** ou um tamanho inconsistente com o cursor (`adxf[0xC]=0x2d047` < `adxf[0x14]=75777`), e `adxf_ReadNw` sai por READEND/erro — e alguém chama `adxf_Stop` em seguida.
2. O callback de conclusão (`adxf[8][4]()`, despachado por `func_413088`/`func_413690`) **para/fecha** o handle antes de o poll observar o `3` (o log mostra `obj8` sendo consumido na mesma transição em que `status` cai para `1`).

### Próximos passos, em ordem

1. **Trace de chamadas no runtime** (dá para pendurar em `dispatchGuestBranch`): logar `0x413268` (`adxf_ReadNw`), `0x413690` (`adxf_Stop`), `0x413128` (fecha handle) e `0x412BA0`/`0x413208` com `a0..a3`, valor de retorno (`$v0`) e `adxf[1]` depois. Objetivo: ver **quem** chama `adxf_Stop` no ciclo ruim e com que argumentos.
2. Logar também `func_26F640` (wrapper bytes→setores) para ver o tamanho pedido e o LBN de cada pedido.
3. Conferir `[0x678440]` (total de setores): no run24 era lixo (`4293920598`), o que estraga `pct` e pode confundir o próprio jogo.
4. **Fix A (fiel ao SDK)**: quando `adxf_ReadNw` falhar/for parado, deixar `adxf[1] = 4` (READERR) em vez de `1`; o loader sai com `-1` e o jogo segue o fluxo de erro em vez de travar.
5. **Fix B (injeção mais restrita)**: só escrever `slot[3]` quando o handle tiver `stm` válido, `status == 2` e callback pendente — hoje a injeção dispara o despachante mesmo em handle parado.

### Despachante de streaming e máquina do loader (referência) [lido, dis]

**Despachante `func_413878`** (e o servidor `func_413208`/`func_413128`):
- Percorre **16 handles** a partir de `0x54FB60` com passo `0x44`. Se `byte0 == 1` (ativo), chama `func_413788(handle)`.
- `func_413788` lê `status = handle[1]`:
  - `status == 2` → `func_417778(handle[4])`, atualiza `handle[1]`. Se o novo status for 3/4, chama `func_413088` (dispara `handle[8][4]()` — o callback pendente).
  - `status == 1` e `handle[3] == 0` → **retorna imediatamente, não faz nada**.
  - `status == 1` e `handle[3] == 1` → `func_4176D0(handle[4])`; se retornar 1, processa (`func_417778`, atualiza `cnt`, chama `func_413088`, `handle[3] = 0`).
- Quem deveria setar `handle[3] = 1` é o callback do CDVD; como ele não rodava, o runtime passou a injetar o flag.

**Máquina de estados `func_378A30`** (bit ocupado em `[0x755AB8]`, estado em `[0x755ABC]`, tabela em `0x592790`):

| Estado | Handler | O que faz |
|---|---|---|
| 0 | `0x378A90` | Init: `func_211878` (requisição), `func_212380`, calcula fim/tamanho |
| 1 | `0x378BF8` | Poll: `func_2123C8`. `0` → estado 2; `-1` → estado 0; outro → repete o estado 1 |
| 2 | `0x378C3C` | `func_467670` (`sceCdRead`) no slot `0x755AC8` → estado 3 |
| 3 | `0x378CA8` | Se `[ptr+0x24]`, chama o callback → estado 4 |
| 4 | `0x378CDC` | Finaliza/reset → estado 0 |

O estado do loader que trava (`loopState`, em `[0x4EF7E0]`) é interno à `func_2123C8` (0 = ocioso, 1 = inicia `adxf_ReadNw`, 2 = espera `status == 3`), e não o `[0x755ABC]` da tabela acima.

## Ambiente do autor

CachyOS (Arch), KDE Wayland, shell **fish**, Konsole. i7-7700HQ, 15,5 GB RAM, 15 GB swap, GTX 1050 Mobile (runtime usa Intel HD 630 via Mesa). GCC 16.2.1, CMake, Ninja, 7zip, Python 3. `/` está ~80% cheio: tudo em `linux_arquivos` (ext4). Ghidra e JDK instalados mas sem uso ainda (Ghidra 12 pode exigir Java 21+; `env JAVA_HOME=/usr/lib/jvm/java-27-openjdk ghidra`).

### Armadilhas do fish (já causaram erro)
- Sem heredoc (`<< EOF`). Para criar arquivo use a ferramenta Write ou `nano`.
- Curinga sem match dá erro: use aspas simples (`--include='*.cpp'`).
- Não use `\n` em `sed` nem em Python dentro de aspas simples ao inserir C++ (vira quebra real, já quebrou o `Thread.cpp` duas vezes). Prefira apagar por número de linha (`sed -i 'N,Md'`) ou editar com a ferramenta Edit.
- O terminal quebra linhas longas coladas: comandos Python longos de uma linha podem falhar com `assert`.

### Armadilhas do `PS2_IF_AGRESSIVE_LOGS`

Macro em `ps2_log.h:189`:
```c
#define PS2_IF_AGRESSIVE_LOGS(code) \
    /* if(AGRESSIVE_LOGS) { code; } */
```
Aceita **1 argumento** (`code`). Por isso:
- **Nunca** colocar `{a, b, c}` com vírgulas dentro — o pré-processador conta como N argumentos e falha (`passou 5 argumentos, mas usa apenas 1`).
- Para zero-init de vetor use `uint32_t jt[5]{};` (sem vírgula), não `{0, 0, 0, 0, 0}`.
- `for (int i = 0; i < 5; ++i)` é OK (`;` não conta).
- Toda declaração (`uint32_t x = ...;`), `for`, `if` deve ficar **antes** do `RUNTIME_LOG(...)`, nunca dentro da cadeia de `<<`.

## Estrutura

Raiz: `/run/media/valk/linux_arquivos/projetos/ShaolinMonks/`

```
Mortal Kombat - Shaolin Monks (USA).iso   original, não mexer
extraido/              ELF + SYSTEM.CNF (7z parcial)
extraido_full/         ISO extraída (~3,5 GB). O runtime usa a pasta do ELF como cdrom0
output/                C++ gerado (não vai pro git); rodar `dis`/grep daqui, da raiz
patches/               snapshot do PS2Recomp (regenerar antes de publicar)
scripts/               extract.sh, recomp.sh, build.sh, run.sh
sm.toml                config do recompilador
PS2Recomp/             repo upstream (não commitado no MKSM; submódulo virá)
  out/build/ps2xRuntime/ps2EntryRunner     binário do runtime
  ps2xRuntime/src/lib/game_overrides_mksm.cpp   override libcdvd (entra via target_sources(ps2EntryRunner))
  ps2xRuntime/src/lib/ps2_runtime.cpp      telemetria [run:tick] (~linhas 2400-2600)
  ps2xRuntime/src/lib/Kernel/Syscalls/Thread.cpp   Create/Start (+ logs [thread:], backup .bak)
  ps2xRuntime/src/lib/Kernel/EeScheduler.cpp       createThread:423, thread principal nasce prio 0 (:144)
  ps2xRuntime/src/lib/Kernel/Syscalls/Helpers/State.h   códigos KE_* (KE_ILLEGAL_PRIORITY=-403)
  ps2xRuntime/src/lib/Kernel/Stubs/{CD.cpp,GS.cpp,MPEG.cpp,IPU.cpp,Helpers/Support.h}
  ps2xIOP/src/                             emulador do IOP e serviços HLE
  run*_raw.log, ps2_log.txt                logs (recriados a cada run)
```

Fora do projeto: `~/mksm-backup/` (cartão de memória antigo, não confirmado como save), `~/.config/fish/functions/dis.fish`.

## Reproduzir do zero (resumo)

Os scripts em `scripts/` fazem isto. Manualmente:

```
# 1. extrair
mkdir -p extraido && 7z x "Mortal Kombat - Shaolin Monks (USA).iso" SYSTEM.CNF SLUS_210.87 -oextraido
7z x "Mortal Kombat - Shaolin Monks (USA).iso" -oextraido_full

# 2. ferramentas (config ~21 min por downloads)
git clone --recurse-submodules https://github.com/ran-j/PS2Recomp.git
cd PS2Recomp && cmake -S . -B out/build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build out/build -j4 --target ps2_analyzer ps2_recomp && cd ..

# 3. analisar, depois editar sm.toml e pôr low_memory_mode = true em [general]
./PS2Recomp/out/build/ps2xAnalyzer/ps2_analyzer extraido/SLUS_210.87 sm.toml

# 4. recompilar e copiar para o runtime
./PS2Recomp/out/build/ps2xRecomp/ps2_recomp sm.toml
cp output/*.cpp PS2Recomp/ps2xRuntime/src/runner/ && cp output/*.h PS2Recomp/ps2xRuntime/include/

# 5. configurar para GCC (SSE4.1 e sem LTO)
cd PS2Recomp
cmake -S . -B out/build -DCMAKE_BUILD_TYPE=None -DCMAKE_CXX_FLAGS="-msse4.1 -O1 -g0" -DPS2X_ENABLE_RUNNER_UNITY_BUILD=OFF -DPS2X_ENABLE_RUNNER_PCH=OFF

# 6. build completo (horas; na tomada, sem suspender)
systemd-run --user --scope -p MemoryMax=12G -p MemorySwapMax=4G cmake --build out/build -j2 --target ps2EntryRunner 2>&1 | tee build.log
```

Notas: LTO vem de `ps2xRuntime/cmake/ReleaseMode.cmake` (só em Release/RelWithDebInfo), por isso build type `None`. Se aparecer `Killed`/`signal 9` é falta de memória. Edição de um arquivo do runtime = build incremental de minutos.

## Rodar e ler logs

Sempre a partir de `PS2Recomp/`, com o ELF da pasta da ISO extraída (o runtime só aceita o ELF em `argv[1]`, não lê env):

```
cd PS2Recomp
cmake --build out/build -j2 --target ps2EntryRunner 2>&1 | tail -n 5
timeout 200 script -qfc "./out/build/ps2xRuntime/ps2EntryRunner ../extraido_full/SLUS_210.87" runNN_raw.log
tr '\r' '\n' < runNN_raw.log | grep -a "run:tick" | tail -3 | cut -c1-1700
```

`script` é necessário (sem pty o buffer de 4 KB se perde no `timeout`). Use `grep -a`. `timeout 200` e `tail` evitam a última linha truncada. Não deixe o jogo rodando com logs de thread ligados: enche o disco.

Campos do `[run:tick]`:

| Campo | Onde | O que é |
|---|---|---|
| `pc`, `ra`, `sp`, `gp` | registradores debug | PC/Ra/Sp/Gp do guest |
| `dispfb1`, `display1`, `dispfb2`, `display2`, `pmode`, `smode2` | `gs.*` | estado do GS |
| `caller` | `[sp]` | endereço que chamou o leaf atual |
| `loopState`, `loopCursor` | `0x4EF7E0`, `0x4EF7EC` | máquina de estados do loader |
| `obj`, `objw0` | `*0x4FADEC` | ponteiro + word0 do handle ADXF |
| `status` | `obj[1]` (byte) | `1`=STOP, `2`=READING, `3`=READEND, `4`=READERR |
| `obj3` | `obj[3]` (byte) | flag "processar" (não setado = travado) |
| `obj4`, `h8..h5C` | `obj[4]` e `obj[4]+offsets` | `stm` (decoder) e campos internos dele |
| `obj8`, `obj8b`, `objC`, `obj14`, `obj1C` | `obj[+0x8/0xC/0x14/0x1C]` | callback pendente, total (setores), cursor (setores), fim |
| `obj24`, `obj28`, `obj2C`, `obj30` | `obj[+0x24..0x30]` | contadores/parâmetros do handle |
| `jt0..jt4` | `0x592790 + i*4` | tabela de estados da `func_378A30` |
| `busyPtr`, `busyCb`, `busyRes` | `0x755AC8`, `+0x24`, `+0xC` | slot do poll, callback armado, byte count |
| `cnt` | `obj[0x20]` | contador de progresso (0 = travado) |
| `srvCalls`, `srvN`, `srvRuns` | `0x533490/0x534124/0x5341B0` | contadores do servidor de VSync |
| `guard`, `usercb` | `0x53423C/0x534240` | guarda e callback do servidor |
| `cb` | `0x4FADF0` | callback do loop principal |
| `flag4ba0`, `vs` | `0x4F4BA0`, `0x678450` | flags do loop e contador de VSync (congela = travado) |
| `busy`, `busySt` | `0x755AB8`, `0x755ABC` | máquina `func_378A30` (bit ocupado + estado) |
| `idleFlag` | `0x5341D8` | thread 2 idle |
| `lk`, `lkSt`, `l8`, `lC`, `hook` | `0x53D504`... | área de lock/servidor |
| `r8`, `rC`, `r10`, `r14`, `guard5dc` | `0x5504C8`... | registradores do servidor 0x5343xx |
| `pct` | `0x678448` | progresso |
| `activeThreads` | scheduler | threads ativas (5 = OK) |
| `dma`, `gif`, `gsw`, `vif` | contadores | I/O |

O painel de registradores do depurador ImGui não é confiável com o jogo rodando.

`[run:trans]` (novo no run25) loga **uma linha por mudança** de `status`, `loopState`, `slot[3]`/`slotSt` ou `obj8`, com `pc` no momento. É a forma mais barata de ver a ordem real dos eventos do ADXF (o `[run:tick]` só amostra a cada 30 frames). Para desligar, remova o bloco `PS2_IF_AGRESSIVE_LOGS` que começa com "HACK MKSM (diagnóstico run25)" em `ps2_runtime.cpp`.

Variáveis úteis: `MKSM_SKIP_INTRO=1` pula a intro; `MKSM_AUTO_PAD=xs` aperta Cross e Start em ciclo (`d` = baixo); `PS2X_SCREENSHOT_DIR=<pasta>` grava capturas; `PS2X_TRACE=1` liga o rastreador em `ps2_log.txt` (bem lento); `PS2X_PROFILE=<s>` grava `ps2_profile.txt`.

## Diagnóstico: comandos úteis (da raiz do projeto)

```
dis 0x4145a8 40                       # função recompilada que contém o endereço (omite alguns delay slots)
dis 0x413878 100                      # despachante de streaming (16 slots × 0x44 em 0x54FB60)
dis 0x413788 100                      # trata o slot (status 1/2)
dis 0x378A30 150                      # máquina de estados do loader
dis 0x378BF8 80                       # state 1 (poll) — o que trava
dis 0x2123C8 100                      # função do poll (loopState interno)
dis 0x26F6E0 120                      # wrapper do jogo p/ adxf_GetStat (3=done, 4=-1)
dis 0x413BE8 40                       # adxf_GetStat (retorna handle[1])
dis 0x413268 120                      # adxf_ReadNw (grava status 2 ou 3)
dis 0x413690 140                      # adxf_Stop (grava status 1 e despacha callback)
dis 0x413128 120                      # fecha/destrói handle (memset 0x44)
dis 0x4130F8 40                       # wrapper com lock do fecha-handle
grep -l "0x44C808u" output/sub_*.cpp  # quem chama (só jal direto)
grep -n -m1 ">> sub_00415380" PS2Recomp/ps2_log.txt   # primeira aparição no rastreador
grep -c "PS2_FUNCTION_LOG_TRACKER" output/sub_XXXX.cpp   # usa rastreador? (0 não vale)
```

Função fish `dis` (em `~/.config/fish/functions/dis.fish`, `funcsave dis`):

```
function dis
    python3 -c 'import glob,re,sys; t=int(sys.argv[1],16); fs=[(int(re.search(r"sub_([0-9A-Fa-f]+)_",f).group(1),16),f) for f in glob.glob("/run/media/valk/linux_arquivos/projetos/ShaolinMonks/output/sub_*.cpp")]; s=max(x for x in fs if x[0]<=t); L=[l.rstrip()[:100] for l in open(s[1]) if l.startswith("    // 0x")]; print(s[1]); print("\n".join(L[:int(sys.argv[2])]))' $argv
end
```

Regras de leitura do rastreador: `grep -c` conta **linhas** (cada chamada gera `>>` e `<<`, dividir por 2). Wrappers gerados com `#ifdef _DEBUG` (ex.: `sub_00480BE0`, `sub_00480D48`) **não** aparecem no rastro. Preferir `fprintf` temporário no runtime a depender do rastreador.

## Fatos do ELF e runtime (úteis para qualquer próxima investigação)

- ELF stripped, entrada `0x11c070`, segmento `0x100000-0x780898`. Endereços valem só para este ELF.
- Dois conjuntos de funções de thread: stubs de syscall puro (`0x47FC60` CreateThread, `0x47FD70` iReferThreadStatus, `0x47FDD0` syscall `0x37`...; passo 0x10 por número de syscall, [memória]) e a biblioteca do SDK (`InitThread@0x480AF0`, `iWakeupThread@0x480BE0`, `StartThread@0x480D48` em `stubs` do `sm.toml`, trocadas por HLE). O jogo usa `StartThread@0x480D48` e `iWakeupThread@0x480BE0`.
- Códigos de erro: `KE_ILLEGAL_PRIORITY=-403`, `KE_UNKNOWN_THID` e outros em `Syscalls/Helpers/State.h`. -407 é o retorno de `SuspendThread` em id inválido.
- Pseudo-LBN: `kCdPseudoLbnStart=0x00100000` (`Support.h:7`). A ISO tem `0x1A0B10` setores, então haveria colisão se `cdImage` fosse configurado; nesse caso subir para `0x00800000`. Hoje `cdImage` nunca é configurado e só pseudo-LBNs de `sceCdSearchFile` resolvem.
- `FILELIST.DIR` é texto (286 bytes). 16 arquivos catalogados: `FILELIST.DIR`, `WADCRC.BIN`, `GAMEDATA.WAD`..`WAJ`, 7 `.SFD` em `FRONT\MOVIES`.
- A tela magenta era o fallback do runtime (`UploadFrame`), não desenho do jogo.
- `ps2_runtime` é `STATIC` sem `WHOLE_ARCHIVE`: overrides precisam entrar em `target_sources(ps2EntryRunner PRIVATE ...)`.
- O jogo tem módulo de FMV próprio (`sceMpeg*` chamados só de `0x446xxx`-`0x44Axxx`) que **nunca** foi chamado até agora. O objeto `0x4FADEC` **não** é player de vídeo: é o handle do **CRI ADXF** em `0x54FB60` (confirmado pelas strings `adxf_*` e pelo layout de `0x44` bytes).
- Servidor de VSync `0x4153A0` (dentro do arquivo `sub_00415380`): roda a cada VSync, percorre 3 slots de handles (`~0x5341BC/C0/C4`, flags `~0x534168/6C/70`, endereços aproximados), chama `0x414938(handle)` (consulta `iReferThreadStatus`, se status for 4 ou 0xC [memória] chama `iWakeupThread`). Os handles agora são ids válidos de thread.
- Duas tabelas de slots de streaming: `0x54FB60` (16 slots × 0x44, despachante `func_413878`, estágio 5 — a do nosso objeto) e `0x53C504` (0x28 entradas, `func_418178`, estágios 3/7). `func_418220` só salta para `func_41D5C8` (outro subsistema). [lido, dis]
- Estado da máquina `func_378A30` em `[0x755ABC]`, bit ocupado em `[0x755AB8]`, ponteiro de slot em `[0x755AC8]`. Tabela de handlers em `0x592790` (5 entradas). [medido, run22]

## Referência Externa: OGmidway/mksm-recomp

O repositório [OGmidway/mksm-recomp](https://github.com/OGmidway/mksm-recomp) (outubro/2026) é um projeto irmão dedicado à recompilação do mesmo executável (`SLUS_210.87`) via PS2Recomp. Ele já alcançou o **boot completo, vídeo de intro Midway, tela de autosave, menu principal, seleção de personagens (Liu Kang / Kung Lao), diálogo de dificuldade e entrada na fase da caverna (Goro's Lair)**.

### Principais Lições e Soluções para os Nossos Bloqueios:

1. **Deadlock do Heap / Loading em 0x379220**:
   - **Pilha de IRQ (`EeScheduler.cpp`)**: O tratador de IRQ usava o SP do guest (`0x1FFFF90`) em vez de deixar o scheduler alocar pilha isolada (SP=0). Isso sobrescrevia dados da pilha do `malloc` (`s1` em `0x1FFFEC0`), corrompendo o cálculo de fim de heap (`alloc_blocks` em `0x64E998`), fazendo a alocação do loader assíncrono em `0x20F058` falhar e retornar buffer nulo em `0x4EF7EC`.
   - **Branches 64-bit com Sinal no Recompiler**: Branches como `BGEZ/BLEZ/BLTZ/BGTZ` geravam `GPR_S32(ctx, rs)` em vez de `GPR_S64`. Na entrada 35 do `GAMEDATA.WAD` (bit 63 setado, bit 31 limpo), isso fazia o decodificador tomar o ramo errado e requisitar um tamanho inválido (`0x803aa002`), travando o loader.
   - **Retorno de Syscalls `WakeupThread` e `ResumeThread`**: O código do MKSM exige que essas chamadas retornem o **ID da thread acordada** (não 0). Retornar 0 mantinha flags de sincronização ativas eternamente.

2. **CRI ADX e Vídeo SFD**:
   - O projeto possui um perfil IOP dedicado (`ps2xIOP/src/modules/mk_cri.cpp` e `mk_sfsv.cpp`) gerenciando as filas de descritores de áudio do CRI DTX (`0x90000200`) e comandos de som do driver SNDF (`0x53465356`).
   - A reprodução de vídeo SFD (`opening.sfd`) é ligada ao decodificador MPEG via ponte no anel de blocos de 2048 bytes do CRI (`mkConsumeCriVideoBlocks`).

3. **Nomes de Símbolos Reais (Ghidra/Protótipo)**:
   - Identificados os símbolos originais: `0x378A30` = `Async_DataLoad`, `0x2123C8` = `AsyncLoadStatus`, `0x211878` = `GetAsyncFileSize`, `0x64E998` = `alloc_blocks`, `0x4EF7E0` = `async_load_info`, `0x755AB8` = `async_info`.
   - Flag de bypass do intro nativa do jogo: escrever `1` temporariamente em `0x513a04` durante `0x1d32e0` pula os vídeos direto para o menu.

4. **Desafios Futuros para Gameplay e Renderização 3D**:
   - **Clear do GS (`sceGsPutDrawEnv`)**: Enviar pacotes `NLOOP+1` via GIF DMA para evitar acúmulo de branco e sobreposição de frames anteriores.
   - **VIF DIRECT**: Impedir que comandos VIF consumam dados de textura como código executável.
   - **Pad DMA (`0x651F00`)**: O jogo lê botões diretamente da área DMA de trabalho; `scePadRead` precisa replicar o estado nos dois bancos para o controle responder.
   - **Scratchpad (`toDmaPhys`)**: Endereços `0x80000000` representam o Scratchpad (`0x70000000`), essencial para os emissores de partículas e efeitos.
   - **Culling no Havok (`VCLIP` / `VSUB`)**: Correções de ordem de bits em planos de corte e preservação de `VF0.w = 1.0f`.
   - **Funções de Debug Nativas**: O binário retail retém a câmera livre (`0x1AA558`), congelamento (`0x1AA5B8`) e god mode (`0x1AAAD8`).

---

## Publicação (quando chegar a hora)

Repositório privado: `https://github.com/lagando/MKSM` (SSH `git@github.com:lagando/MKSM.git`), já publicado com `README.md`, `PLANO.md`, `sm.toml`, `scripts/`, `patches/` e `.gitignore` (`*.iso`, `extraido/`, `extraido_full/`, `output/`, `out/`, `*.log`, `ps2_log.txt`, `/PS2Recomp/`, `/ps2_runtime.cpp`). O `PS2Recomp/` fica fora do git (clone local + patch); no futuro, submódulo ou fork (ler a licença antes). O snapshot atual do runtime está em `patches/runtime_atual.diff`, regenerado no estado run25 (inclui a injeção do `slot[3]`, a telemetria de `[run:tick]` e o observador `[run:trans]`).

```
git -C PS2Recomp diff -- . ':!ps2xRuntime/src/runner' > patches/runtime_atual.diff
cp PS2Recomp/ps2xRuntime/src/lib/game_overrides_mksm.cpp patches/game_overrides_mksm.cpp
```

Mudanças de runtime até agora: `game_overrides_mksm.cpp`, `CMakeLists.txt` (target_sources), `GS.cpp`, `Support.h`, `CD.cpp` (log), `ps2_runtime.cpp` (injeção do `slot[3]` + telemetria), `EeScheduler.cpp` (prioridade 0), `Thread.cpp` (logs temporários, remover antes de publicar). Windows deve ser mais direto (MSVC é o alvo principal do PS2Recomp). No Linux/GCC: SSE4.1 e sem LTO.

