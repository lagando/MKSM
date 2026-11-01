# ShaolinMonks: recompilação estática de PS2 com PS2Recomp

Estado em 01/11/2026 (tarde). Documento feito para uma IA ou pessoa nova continuar sem contexto. Detalhes de investigação ficam em `PLANO.md`. Convenção: **[medido]** = visto em execução, **[lido]** = lido no código (sem confirmar em execução), **[memória]** = vem da memória da IA sobre o SDK do PS2, conferir antes de confiar.

## Objetivo

Gerar uma versão nativa (Windows e Linux) do Mortal Kombat: Shaolin Monks (PS2, USA, SLUS-21087 v1.02) recompilando o ELF para C++ com [PS2Recomp](https://github.com/ran-j/PS2Recomp) (clone sem fork, commit `e563698`). O código da CPU vira C++, mas o runtime ainda emula GS, VU, DMA e IOP. Não é port com renderizador reescrito (isso é o OpenShao, que só roda a versão de Xbox). O PS2Recomp avisa que VU e GS têm desempenho ruim, então jogável não é garantido.

Usuário: Weremir (handle "Lagando"), tem a cópia legítima do jogo.

**Regra legal:** ISO, ELF, `extraido*/`, `output/` (C++ gerado), binários e logs **nunca** vão para o GitHub. Só patches, `sm.toml`, scripts e documentação. Não ajudar a obter ROMs.

## Estado atual

| Etapa | Estado |
|---|---|
| Extrair ISO, compilar ferramentas, analisar ELF, gerar `sm.toml`, recompilar (13.651 arquivos, 236 MB), compilar runtime (~3h30, 97 MB) | Feito |
| Bind `libcdvd` (loop em `0x385D80`) | Resolvido: override `mksm-cdvd`, 10 binds guest→HLE |
| Tela magenta | Resolvida: `PMODE`/`SMODE2` e layout de `sceGsDispEnv` |
| **Threads do jogo (bloqueio de 01/11)** | **Resolvido**: `CreateThread` com prio 0 retornava -403 (`KE_ILLEGAL_PRIORITY`). Fix no `EeScheduler.cpp:426`: `params.priority < 0` em vez de `< 1`. Agora ids 2 a 5, `Start -> 0`, `activeThreads=5` [medido, run19] |
| Primeiros `sceCdRead` de dados | Ocorrem: `lbn=0x10002d` (1 setor) e `0x10002e` (7 setores) [medido]. São pseudo-LBNs dentro da faixa de `GAMEDATA.WAD` (`0x100011` + `0x308b7`) [lido da tabela do `sceCdSearchFile`] |
| Menu | **Não ainda.** Jogo em novo loop (ver "Bloqueio atual") |

Chance subjetiva (reavaliar a cada bloqueio): menu 40 a 55%, jogável 5 a 15%, jogo inteiro sem bugs graves menos de 5%. Maior risco: GS/VU numa Intel HD 630.

## Bloqueio atual (run19, ticks 600 a 3480)

[medido]
- `pc=0x379220`/`0x379218`, `caller=0x1d1548`, `loopState=2`, `loopCursor` muda (`0x1edf180`, `0x1edc140`, `0xc34a80`), `activeThreads=5`, ~60 fps, sem crash.
- Objeto `*(0x4FADEC)=0x54fb60` passou de status 2 para **1** (`objw0=0x101`), `cnt=0`. O campo `total` (`0x678440`) virou lixo (4293920598): não usar mais como antes.
- Thread 2 (entry `0x4145a8`) é suspensa ~3200 vezes em 60 s (`SuspendThread id=2`), `ReferStatus` alterna 0x8 e 0x2. `iWakeupThread` em ids 3 e 4 a cada VSync.
- `sceSifSetDma` repetido (`src=0x54dd40 dst=0x1252c0 size=0x880` e `src=0x547e80 dst=0x16f640 size=0x880`, `ra=0x41ecf4`) a cada frame.

[lido, `dis` omite delay slots]
- `sub_001D1480` (init) chama `func_3791D8` em `0x1d1540`, que é o loop atual: chama `func_378A30` e, quando `[0x678450]` (contador de VSync) muda, chama `func_1DCB20` e `func_2C6878`/`func_378138`; CORRIGIDO (movz lido com atenção): `v1 = 1; movz v1, zero, v0` zera `v1` quando o bit é 0, então o loop **repete enquanto o bit 0 de `[0x755AB8]` for 1 (ocupado) e sai quando zera**. A versão anterior deste documento dizia o contrário. `func_378A30` é uma máquina de estados: se o bit 0 for 0 ela não faz nada; senão lê o estado em `[0x755ABC]` (0 a 4), salta por tabela em `0x592790` e o primeiro caso chama `func_211878` (a mesma função de requisição do loader antigo) e calcula tamanho alinhado em 0x40. Falta saber em que estado ela fica parada.
- Thread 2 (`0x4145a8`): laço lendo `[0x5341D8]` (64 bits) chamando `func_414590` até virar diferente de zero; então grava 1 em `[0x5341E0]` e chama `func_47FCA0` (provável `ExitThread`, [memória]). `func_414590` só incrementa um contador de 64 bits em `[0x534180]` (o `sd` do delay slot não aparece no `dis`, inferência): a thread 2 é provavelmente uma thread ociosa que conta ciclos livres, então o `Suspend` repetido talvez seja normal. `0x414410` decrementa `[0x534160]` e, se zerar, chama `0x414A58(thread 2)`, `GetThreadId` (`0x47FD50`) e `ChangeThreadPriority(self, [0x53BA50])` (par lock/unlock por prioridade, hipótese).
- Os 4 criadores (`0x414AB8`, `0x414B60`, `0x414BE8`, `0x414C70`) fazem `CreateThread` (prio vem de `[0x53BA58]`, que vale 0 no ELF [medido nos bytes]), `StartThread@0x480D48`, `0x47FCF0` (provável `ChangeThreadPriority`, [memória], valores finais de `[0x5341xx]` ainda não lidos) e o primeiro/último ainda chamam `0x414A58` (consulta status e chama `0x47FDD0`, syscall `0x37`, provável `SuspendThread` [memória]).

Próximos passos, em ordem:
1. Rebuild (os logs `[thread:Suspend]`, `Wakeup` e `ReferStatus` foram removidos de `Thread.cpp` com `sed`, **ainda não compilado**; pode dar warning de `id` sem uso). Manter só `Create`/`Start`.
2. Acrescentar ao `[run:tick]`: `[0x755AB8]` (bit 0 = ocupado, o loop espera zerar), `[0x755ABC]` (estado 0 a 4 da máquina em `0x378A30`), `[0x5341D8]`, `[0x5341E0]`. Ver quem escreve `[0x755AB8]`: `grep -l "0x755AB8\|0x755ab8" output/sub_*.cpp`.
3. `dis 0x414590 30`, `dis 0x378A30 40`, `dis 0x414410 40`.
4. Saber se os valores finais de prioridade (`0x47FCF0`) são aceitos pelo scheduler (`ChangeThreadPriority` em `EeScheduler.cpp` ~linha 717).
5. Só como triagem: forçar a saída do loop num game override.

## Ambiente do autor

CachyOS (Arch), KDE Wayland, shell **fish**, Konsole. i7-7700HQ, 15,5 GB RAM, 15 GB swap, GTX 1050 Mobile (runtime usa Intel HD 630 via Mesa). GCC 16.2.1, CMake, Ninja, 7zip, Python 3. `/` está ~80% cheio: tudo em `linux_arquivos` (ext4). Ghidra e JDK instalados mas sem uso ainda (Ghidra 12 pode exigir Java 21+; `env JAVA_HOME=/usr/lib/jvm/java-27-openjdk ghidra`).

### Armadilhas do fish (já causaram erro)
- Sem heredoc (`<< EOF`). Para criar arquivo use a ferramenta Write ou `nano`.
- Curinga sem match dá erro: use aspas simples (`--include='*.cpp'`).
- Não use `\n` em `sed` nem em Python dentro de aspas simples ao inserir C++ (vira quebra real, já quebrou o `Thread.cpp` duas vezes). Prefira apagar por número de linha (`sed -i 'N,Md'`) ou editar com a ferramenta Edit.
- O terminal quebra linhas longas coladas: comandos Python longos de uma linha podem falhar com `assert`.

## Estrutura

Raiz: `/run/media/valk/linux_arquivos/projetos/ShaolinMonks/`

```
Mortal Kombat - Shaolin Monks (USA).iso   original, não mexer
extraido_full/        ISO extraída (~3,5 GB). O runtime usa a pasta do ELF como cdrom0:
sm.toml               config do recompilador (stubs 24-181, entry_points 185-506, [mmio] 512, [jump_tables] 897)
output/               C++ gerado (não vai pro git); rodar `dis`/grep daqui, da raiz
patches/              backup local desatualizado (regerar antes de publicar)
PS2Recomp/            repo upstream
  out/build/ps2xRuntime/ps2EntryRunner     binário do runtime
  ps2xRuntime/src/lib/game_overrides_mksm.cpp   override libcdvd (no CMake via target_sources(ps2EntryRunner))
  ps2xRuntime/src/lib/ps2_runtime.cpp      telemetria [run:tick] (~linhas 2428-2470)
  ps2xRuntime/src/lib/Kernel/Syscalls/Thread.cpp   Create/Start/ReferStatus (+ logs [thread:], backup .bak)
  ps2xRuntime/src/lib/Kernel/EeScheduler.cpp       createThread:423, thread principal nasce prio 0 (:144)
  ps2xRuntime/src/lib/Kernel/Syscalls/Helpers/State.h   códigos KE_* (KE_ILLEGAL_PRIORITY=-403)
  ps2xRuntime/src/lib/Kernel/Stubs/{CD.cpp,GS.cpp,MPEG.cpp,IPU.cpp,Helpers/Support.h}
  ps2xIOP/src/                             emulador do IOP e serviços HLE
  run*_raw.log, ps2_log.txt (rastreador, ~265 MB por execução, recriado a cada run)
```

Fora do projeto: `~/mksm-backup/` (cartão de memória do site antigo, não confirmado como save), `~/.config/fish/functions/dis.fish`.

## Reproduzir do zero (resumo)

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

# 4. recompilar e copiar para o runtime (não rodar de novo se sm.toml/ELF não mudaram)
./PS2Recomp/out/build/ps2xRecomp/ps2_recomp sm.toml
cp output/*.cpp PS2Recomp/ps2xRuntime/src/runner/ && cp output/*.h PS2Recomp/ps2xRuntime/include/

# 5. configurar para GCC (SSE4.1 e sem LTO; teste antes: compilar um .cpp.o só)
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
tr '\r' '\n' < runNN_raw.log | grep -a "run:tick" | tail -2 | cut -c1-700
grep -a "\[thread:Create\]\|\[thread:Start\]" runNN_raw.log
```

`script` é necessário (sem pty o buffer de 4 KB se perde no `timeout`). Use `grep -a`. `timeout 60` e `tail` evitam a última linha truncada. Não deixe o jogo rodando com logs de thread ligados: enche o disco.

Campos do `[run:tick]`: `pc`, `caller` (lido de `[sp]`), `loopState` (`0x4EF7E0`), `loopCursor` (`0x4EF7EC`), `obj` (`*0x4FADEC`), `objw0`/`status` (byte `obj+1`), `cnt` (`obj+0x20`), `total` (`0x678440`, não confiável agora), `srvCalls/srvN/srvRuns/guard/usercb` (`0x533490/0x534124/0x5341B0/0x53423C/0x534240`), `cb` (`0x4FADF0`), `flag4ba0` (`0x4F4BA0`), `vs` (`0x678450`), `activeThreads`. O painel de registradores do depurador ImGui não é confiável com o jogo rodando.

## Diagnóstico: comandos úteis (da raiz do projeto)

```
dis 0x4145a8 40                       # função recompilada que contém o endereço (omite alguns delay slots; para completo: cut -c1-170 output/sub_XXXXXXXX_0x....cpp)
grep -l "0x44C808u" output/sub_*.cpp  # quem chama (só jal direto)
grep -n -m1 ">> sub_00415380" PS2Recomp/ps2_log.txt   # primeira aparição no rastreador (-m1: arquivo grande)
grep -c "PS2_FUNCTION_LOG_TRACKER" output/sub_XXXX.cpp   # o arquivo usa o rastreador? se 0, a contagem 0 não vale
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

## Publicação (quando chegar a hora)

Repositório sugerido: `README.md`, `.gitignore` (`*.iso`, `extraido/`, `extraido_full/`, `output/`, `out/`, `*.log`, `ps2_log.txt`), `sm.toml`, `scripts/` (extract/recomp/build, `.sh` e `.ps1`), `patches/`, `docs/`, `PS2Recomp/` como submódulo ou fork (ler a licença antes). Antes de publicar regenerar os patches: `git -C PS2Recomp diff -- . ':!ps2xRuntime/src/runner' > patches/runtime_atual.diff` e copiar à parte `game_overrides_mksm.cpp` (arquivo novo). Mudanças de runtime até agora: `game_overrides_mksm.cpp`, `CMakeLists.txt` (target_sources), `GS.cpp`, `Support.h`, `CD.cpp` (log), `ps2_runtime.cpp` (telemetria), `EeScheduler.cpp` (prioridade 0), `Thread.cpp` (logs temporários, remover antes de publicar). Windows deve ser mais direto (MSVC é o alvo principal do PS2Recomp). No Linux/GCC: SSE4.1 e sem LTO.

## Alternativas se não render

PCSX2 com a ISO. OpenShao (precisa de cópia de Xbox, não confirmada).
