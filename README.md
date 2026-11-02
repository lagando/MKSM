
```markdown
# ShaolinMonks: recompilação estática de PS2 com PS2Recomp

Estado em 01/11/2026 (noite, run24). Documento feito para uma IA ou pessoa nova continuar sem contexto. Convenção: **[medido]** = visto em execução, **[lido]** = lido no código, **[memória]** = vem da memória da IA sobre o SDK do PS2, conferir antes de confiar. Este é o documento principal; `PLANO.md` tem o mesmo conteúdo com mais detalhes de investigação.

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
| **Streaming (estágio 5)** | **Avançou**: `func_413878` percorre 16 slots × 0x44 a partir de `0x54FB60`, chama `func_413788` nos ativos; o poll saiu do estado 1 e o jogo leu `FILELIST.DIR`, setores do `GAMEDATA.WAD` e o catálogo dos `.SFD` [lido, dis; medido, run24] |
| **Injeção do callback (HLE)** | ⚠️ `slot[3] = 1` quando `status == 2` e `slot[3] == 0` (`ps2_runtime.cpp`). Destrava o poll, mas o cursor `obj14` vira lixo (75777) e a thread cai em spinloop [medido, run24] |
| **Telemetria estendida** | `obj3`, `obj8`, `obj14`, `jt0..jt4`, `busyPtr`, `busyCb`, `busyRes` no `[run:tick]` — compilado [medido, run24] |
| Menu | **Não ainda.** O guest fica em spinloop em `0x379220` (ver "Bloqueio atual") |

Chance subjetiva (reavaliar a cada bloqueio): menu 45 a 60%, jogável 5 a 15%, jogo inteiro sem bugs graves menos de 5%. Maior risco: GS/VU numa Intel HD 630.

## Bloqueio atual (run24, ticks 600 a 3480)

### O que travava antes (run22) [medido, run22]

A máquina `func_378A30` ficava presa no **estado 1** (`busySt=1`): a leitura HLE terminava e o `status` do slot ia de `2` para `1`, mas `func_2123C8` nunca retornava `0` nem `-1`. O `obj3` (`slot[3]`, flag "processar") ficava em `0`, o `obj8` (callback) era consumido e o `vs` congelava em 35.

### A injeção HLE do run24 [medido, run24]

O laço principal de `PS2Runtime::run` (`ps2_runtime.cpp`) varre os 16 slots de `0x54FB60` (passo `0x44`) e, **só** quando o slot está ativo, `status == 2` e `slot[3] == 0`, escreve `slot[3] = 1` (o "sinal verde" do despachante). Resultado:

**Antes do travamento (tick 600–1200, `status=2`)**:
- `pc=0x379220`, `caller=0x2f4e88`, `loopState=2`, `activeThreads=5`, ~60 fps.
- `obj=0x54fb60` com `status=2` (lendo), `obj3=1` (injeção ativa), `obj8=0x549190` (callback pendente), `obj14` avança 28→29→36→37→39 (cursor de escrita no WAD).
- `busySt=1` (estado "poll" da máquina `func_378A30`), `busyPtr` cicla entre 4 slots (`0x755ad0 → 0x755b04 → 0x755b38 → 0x755b6c`), `busyCb` muda (`0x2f6000`, `0x2f4ba8`, `0x31bc50`...), `busyRes` são valores pequenos (`200, 813, 3998, 12890`) = byte counts.
- `srvCalls` e `vs` avançam normalmente; `sceCdRead:ok` a cada ~2-3 VSync.
- Leituras reais que passaram a acontecer: `FILELIST.DIR` (`lbn=0x100000`, 2 setores), `GAMEDATA.WAD` (`lbn=0x100011` com `0x1d` setores, `0x10002d`, `0x10002e`, `0x100035`, `0x100036`, `0x100038`...) e o catálogo de `FRONT\MOVIES\*.SFD` (`sceCdSearchFile:ok`, sem leitura de setores ainda).

**Depois do travamento (tick 1440+, `status=1`)**:
- `status` virou **1** (`objw0=0x101`), `obj3` voltou a **0**, `obj8` virou **0x0** (callback consumido), `obj14` **congelou** em 75777 (lixo).
- `busySt=1` **ainda** (nunca saiu do estado "poll"), `busyPtr` congelou em `0x755b04`, `busyCb=0x3191f0`, `busyRes=0x803AA402` ← passou de byte count pra pointer/garbage.
- `pc=0x379220`, `caller=0x1d1548`; `vs` sobe muito devagar (43→44 entre os ticks 3360 e 3480, ~1 VSync por 120 ticks). Não congela, mas o guest está em spinloop e nunca chega ao menu. ~60 fps na janela é só o `UploadFrame` da main thread.

### [lido, `dis`]

**Despachante de streaming `func_413878`**:
- Percorre **16 slots** a partir de `0x54FB60` com passo `0x44`. Se `byte0 == 1` (ativo), chama `func_413788(slot)`.
- `func_413788` lê `status = slot[1]`:
  - `status == 2` → `func_417778(slot[4])`, atualiza `slot[1]`. Se novo status for 3/4, chama `func_413088` (dispara `slot[8][4]()` — o callback pendente).
  - `status == 1` e `slot[3] == 0` → **retorna imediatamente, não faz nada**.
  - `status == 1` e `slot[3] == 1` → `func_4176D0(slot[4])` (lock/unlock: `func_418628 → 42C7C0`, `func_417708 → 1` fixo, `func_418640 → 42C7C8`). Se retornar 1, processa: `func_417778`, atualiza `cnt`, chama `func_413088`, `slot[3]=0`.
- Ou seja: **quem deveria setar `slot[3] = 1` é outro código** (callback), não a `func_413788`. Como ele não rodava, o runtime passou a injetar o flag (ver acima).

**Máquina de estados `func_378A30`**:
- Entra se `[0x755AB8] & 1` (bit ocupado). Lê `state = [0x755ABC]`, salta pela tabela em `0x592790`.
- **Tabela de estados (endereços reais, run22)**:
  - `jt[0] = 0x378A90` — **state 0**: chama `func_211878` (requisição de read), `func_212380`, calcula end/tamanho.
  - `jt[1] = 0x378BF8` — **state 1**: `func_2123C8` (poll). Se `== 0` → state 2. Se `== -1` → state 0. Se outro → fica em 1 e tenta de novo. **É AQUI QUE A INJEÇÃO DO `slot[3]` ATUA.**
  - `jt[2] = 0x378C3C` — **state 2**: chama `func_467670` (`sceCdRead`) no slot `0x755AC8`, transiciona pra state 3.
  - `jt[3] = 0x378CA8` — **state 3**: `if ([ptr+0x24])` → chama o callback, transiciona pra state 4.
  - `jt[4] = 0x378CDC` — **state 4**: finaliza/reset, volta pra state 0 (ou 0x5AB8+4=0).

**Diagnóstico consolidado**: a injeção destravou o poll e o jogo leu dados reais do WAD, mas atropela o fluxo: o cursor `obj[0x14]` chega a 36 e depois vira lixo (75777), e a thread principal fica presa em `0x379220` esperando uma flag que não chega. O `slot[3]` provavelmente não é a única condição: falta o `obj8` (callback) atualizar cursor/contador antes, ou o sinal verde só vale depois que o HLE concluiu a escrita.

### Próximos passos, em ordem

1. **`dis 0x379220 100`** e **`dis 0x1D1548 100`** — ver qual registrador/memória o spinloop testa repetidamente e quem é o caller. É o item 1 do `PLANO.md`.
2. **`dis 0x2123C8 100`** — comparar o que o poll real espera com o que a injeção força. Se o campo "done" for outro (ex.: `[+0x30]`), `slot[3]` é o lugar errado.
3. **Adicionar ao `[run:tick]`** os 5 campos do slot `busyPtr`: `busyF4` (`[+0x4]`), `busyF8` (`[+0x8]`), `busyF1C` (`[+0x1C]`), `busyF20` (`[+0x20]`) e `busyF30` (`[+0x30]`). Comparar `busyF30` com `busyRes` (`[+0xC]`) — se iguais, o "done" está lá e o poll ignora.
4. **Trocar a injeção por um `sceCdCallback` fiel**: sinalizar o slot só depois que o HLE concluiu a escrita e apenas se `obj8` ainda estiver pendente. Ver `ps2xRuntime/src/lib/Kernel/Stubs/CD.cpp`.
5. Compilar + **run25** e comparar antes/depois.

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
timeout 60 script -qfc "./out/build/ps2xRuntime/ps2EntryRunner ../extraido_full/SLUS_210.87" runNN_raw.log
tr '\r' '\n' < runNN_raw.log | grep -a "run:tick" | tail -3 | cut -c1-1700
```

`script` é necessário (sem pty o buffer de 4 KB se perde no `timeout`). Use `grep -a`. `timeout 60` e `tail` evitam a última linha truncada. Não deixe o jogo rodando com logs de thread ligados: enche o disco.

Campos do `[run:tick]`:

| Campo | Onde | O que é |
|---|---|---|
| `pc`, `ra`, `sp`, `gp` | registradores debug | PC/Ra/Sp/Gp do guest |
| `dispfb1`, `display1`, `dispfb2`, `display2`, `pmode`, `smode2` | `gs.*` | estado do GS |
| `caller` | `[sp]` | endereço que chamou o leaf atual |
| `loopState`, `loopCursor` | `0x4EF7E0`, `0x4EF7EC` | máquina de estados do loader |
| `obj`, `objw0` | `*0x4FADEC` | ponteiro + word0 |
| `status` | `obj[1]` (byte) | 0=vazio, 1=done, 2=lendo, 3/4=callback |
| `obj3` | `obj[3]` (byte) | flag "processar" (não setado = travado) |
| `obj8` | `obj[8]` | callback pendente (`slot[8][4]()`) |
| `obj14` | `obj[0x14]` | cursor de escrita no WAD |
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

## Diagnóstico: comandos úteis (da raiz do projeto)

```
dis 0x4145a8 40                       # função recompilada que contém o endereço (omite alguns delay slots)
dis 0x413878 100                      # despachante de streaming (16 slots × 0x44 em 0x54FB60)
dis 0x413788 100                      # trata o slot (status 1/2)
dis 0x378A30 150                      # máquina de estados do loader
dis 0x378BF8 80                       # state 1 (poll) — o que trava
dis 0x2123C8 100                      # função do poll
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
- O jogo tem módulo de FMV próprio (`sceMpeg*` chamados só de `0x446xxx`-`0x44Axxx`) que **nunca** foi chamado até agora; a hipótese de o objeto `0x4FADEC` ser player de vídeo ficou sem apoio. O objeto talvez seja um streamer/loader (CRI) [hipótese].
- Servidor de VSync `0x4153A0` (dentro do arquivo `sub_00415380`): roda a cada VSync, percorre 3 slots de handles (`~0x5341BC/C0/C4`, flags `~0x534168/6C/70`, endereços aproximados), chama `0x414938(handle)` (consulta `iReferThreadStatus`, se status for 4 ou 0xC [memória] chama `iWakeupThread`). Os handles agora são ids válidos de thread.
- Duas tabelas de slots de streaming: `0x54FB60` (16 slots × 0x44, despachante `func_413878`, estágio 5 — a do nosso objeto) e `0x53C504` (0x28 entradas, `func_418178`, estágios 3/7). `func_418220` só salta para `func_41D5C8` (outro subsistema). [lido, dis]
- Estado da máquina `func_378A30` em `[0x755ABC]`, bit ocupado em `[0x755AB8]`, ponteiro de slot em `[0x755AC8]`. Tabela de handlers em `0x592790` (5 entradas). [medido, run22]

## Publicação (quando chegar a hora)

Repositório privado: `https://github.com/lagando/MKSM` (SSH `git@github.com:lagando/MKSM.git`), já publicado com `README.md`, `PLANO.md`, `sm.toml`, `scripts/`, `patches/` e `.gitignore` (`*.iso`, `extraido/`, `extraido_full/`, `output/`, `out/`, `*.log`, `ps2_log.txt`, `/PS2Recomp/`, `/ps2_runtime.cpp`). O `PS2Recomp/` fica fora do git (clone local + patch); no futuro, submódulo ou fork (ler a licença antes). O snapshot atual do runtime está em `patches/runtime_atual.diff`, regenerado no estado run24 (inclui a injeção do `slot[3]`).

```
git -C PS2Recomp diff -- . ':!ps2xRuntime/src/runner' > patches/runtime_atual.diff
cp PS2Recomp/ps2xRuntime/src/lib/game_overrides_mksm.cpp patches/game_overrides_mksm.cpp
```

Mudanças de runtime até agora: `game_overrides_mksm.cpp`, `CMakeLists.txt` (target_sources), `GS.cpp`, `Support.h`, `CD.cpp` (log), `ps2_runtime.cpp` (injeção do `slot[3]` + telemetria), `EeScheduler.cpp` (prioridade 0), `Thread.cpp` (logs temporários, remover antes de publicar). Windows deve ser mais direto (MSVC é o alvo principal do PS2Recomp). No Linux/GCC: SSE4.1 e sem LTO.

