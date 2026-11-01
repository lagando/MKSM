# Projeto ShaolinMonks (recompilação estática de PS2 com PS2Recomp)

Estado em 01/11/2026. Este documento permite reproduzir tudo o que foi feito e continuar de onde parou (outra pessoa, outra conversa ou outra IA).

## Objetivo

Estudar a recompilação estática do Mortal Kombat: Shaolin Monks (versão de PS2) usando o [PS2Recomp](https://github.com/ran-j/PS2Recomp), partindo da ISO que o usuário já possui. Não há garantia de sucesso: o README do PS2Recomp diz que o projeto é experimental e que o desempenho de VU e GS é muito ruim.

Contexto: o usuário (Weremir, handle "Lagando") queria contribuir com o OpenShao (port nativo da versão de Xbox, repo OAleex/OpenShao) e decidiu tentar a versão de PS2. O OpenShao só roda a versão de Xbox, então não serve para esta ISO.

## Objetivo final

Gerar uma versão nativa do jogo para **Windows e Linux** a partir do ELF de PS2, e manter o trabalho público no GitHub. Em termos técnicos, o resultado seria o código do jogo traduzido (recompilação estática) rodando sobre o runtime do PS2Recomp, que ainda emula o hardware (GS, VU, DMA, IOP). **Não** é um port com renderizador reescrito, como o OpenShao. O PS2Recomp avisa que VU e GS têm desempenho ruim, então não há garantia de que fique jogável. Hoje o jogo já passou do bloqueio de RPC do CD (`libcdvd`), o frame já chega à tela (tela preta, sem magenta) e o jogo roda o loop de frame a ~60 fps, mas ainda não passa de um loop de espera por um objeto de player/loader (ver "Bloqueio atual").

O jogo pronto para executar fica **só localmente, no PC do autor**, gerado a partir da cópia legítima que ele já possui. Essa versão compilada **não vai para o GitHub** por causa dos direitos autorais. O que vai para o repositório são apenas as ferramentas, os patches e a documentação para que cada pessoa gere a sua própria versão com a sua própria cópia do jogo.

## Resumo do estado atual

| Etapa | Estado |
|---|---|
| Extrair ELF e ISO inteira | Feito |
| Compilar analisador e recompilador | Feito |
| Analisar o ELF e gerar `sm.toml` | Feito |
| Recompilar o ELF para C++ (13.651 arquivos, 236 MB) | Feito |
| Compilar o runtime com o C++ gerado | **Feito** (~3h30, binário de 97 MB, 0 erros) |
| Primeiro boot | **Roda**: abre janela, carrega o ELF, carrega os 9 módulos IOP |
| Bind do `libcdvd` (`0x385D80`) | **Resolvido** (Etapa 1): 10 overrides guest→HLE, 16 arquivos encontrados, 0 LBN não resolvido |
| Apresentação do frame no host (tela magenta) | **Resolvida** (Etapa 3): `PMODE`/`SMODE2` e layout de `sceGsDispEnv` corrigidos; `[frame:upload]` contínuo, 0 ocorrências de magenta |
| Jogo chegar ao menu | **Não ainda**: ~60 fps estáveis, mas o jogo espera para sempre um objeto de player/loader (`*(0x4FADEC)` = `0x54fb60`) que fica em status 2 com 0 frames (ver "Bloqueio atual") |
| Causa do bloqueio antigo | **Identificada e contornada**: falta o servidor `cdvdman` (`0x80000597`); plano em `PLANO.md` |
| Rastreador de funções (`ps2_log.txt`) | **Ligado** no binário atual: ~265 MB por execução, recriado em `PS2Recomp/` (ver "Rodada de leitura") |
| Experimento `run16` (servidor `0x4153A0`) | **Feito**: o servidor roda em todo VSync com `srvN=1`, e o objeto continua em status 2 com `cnt=0`; o bloco `sceMpeg*` do jogo nunca é chamado. O servidor consulta dois handles com `iReferThreadStatus` (6424 linhas no rastro (~3212 chamadas)); quatro threads são criados (`CreateThread`, ~4 chamadas, por `0x414AB8`, `0x414B60`, `0x414BE8` e `0x414C70`); `StartThread` (`0x480D48`) e `iWakeupThread` (`0x480BE0`) são stubs HLE que o rastreador não vê, então **não se sabe** se os threads iniciam |

## Ambiente do autor

- Notebook com CachyOS (base Arch), KDE Plasma (Wayland), shell **fish**, terminal Konsole
- CPU i7-7700HQ (4 núcleos, 8 threads), 15,5 GB de RAM, 15 GB de swap, GPU GTX 1050 Mobile (o runtime usou a Intel HD 630 via Mesa)
- GCC 16.2.1, CMake, Ninja, 7zip, Python 3
- `/` está com ~80% de uso, então tudo fica na partição `linux_arquivos` (ext4)
- Opcional: `ghidra` e `jdk-openjdk` instalados para um futuro mapa de funções (o Java padrão é o 17; o Ghidra 12 provavelmente pede 21 ou mais novo; tentar `env JAVA_HOME=/usr/lib/jvm/java-27-openjdk ghidra`, confirmando a pasta com `ls /usr/lib/jvm`)

### Armadilhas do fish

- Não aceita heredoc (`<< 'EOF'`). Para criar arquivos multilinha use um editor (`nano`) ou outro shell
- Curingas sem aspas dão erro se não casarem. Em `grep --include`, use aspas simples: `--include='*.cpp'`
- Não use `\n` dentro de `sed` para inserir código C++ (vira quebra de linha real). Edite com `nano +LINHA arquivo`

## Estrutura de arquivos

```
/run/media/valk/linux_arquivos/projetos/ShaolinMonks/
├── Mortal Kombat - Shaolin Monks (USA).iso      ISO original (não mexer)
├── extraido/                                    só SYSTEM.CNF e SLUS_210.87
├── extraido_full/                               ISO inteira extraída (~3,5 GB). USAR PARA RODAR
│   ├── SLUS_210.87                              ELF do jogo
│   ├── SYSTEM.CNF
│   ├── IOP/                                     módulos IRX do IOP: SIO2MAN, PADMAN, LIBSD, SDRDRV,
│   │                                            MCMAN, MCSERV, CDVDSTM, CRI_ADXI, SNDFI, IOPRP300.IMG
│   ├── MODULES/                                 cópias de alguns IRX + MTAPMAN.IRX
│   ├── CLASSIC/ FRONT/ SURREAL/                 dados do jogo
│   ├── GAMEDATA.WAD .WAE .WAF .WAG .WAH .WAI .WAJ   (~2,3 GB)
│   ├── FILELIST.DIR, WADCRC.BIN, JARVOS.ELF
├── sm.toml                                      config gerada pelo analisador
├── PLANO.md                                     plano para destravar o boot (Etapas 1-5)
├── patches/                                     backup local (01/11/2026): runtime_etapas_1_3.diff, game_overrides_mksm.cpp e cópias de PLANO.md/README.md
├── output/                                      C++ gerado pelo recompilador (13.651 arquivos, 236 MB)
│   ├── sub_XXXXXXXX_0xXXXXXXXX.cpp              uma função recompilada por arquivo
│   ├── register_functions.cpp                   24 MB, 288.437 linhas
│   ├── ps2_recompiled_functions.h, ps2_recompiled_stubs.h
└── PS2Recomp/                                   repo clonado do original (sem fork)
    ├── out/build/                               build do CMake (configurado com build type None)
    │   ├── ps2xAnalyzer/ps2_analyzer            binário do analisador
    │   ├── ps2xRecomp/ps2_recomp                binário do recompilador
    │   └── ps2xRuntime/ps2EntryRunner           binário do runtime (97 MB)
    ├── ps2xRuntime/
    │   ├── cmake/ReleaseMode.cmake              onde o -flto é ligado (função EnableFastReleaseMode)
    │   ├── include/game_overrides.h             PS2_REGISTER_GAME_OVERRIDE e bindAddressHandler
    │   ├── src/main.cpp                         só aceita o ELF como argv[1]
    │   ├── src/runner/                          13.649 .cpp gerados copiados de output/ (gitignored)
    │   ├── src/lib/game_overrides.cpp           registro e aplicação dos overrides por jogo
    │   ├── src/lib/game_overrides_mksm.cpp      override do libcdvd (criado na Etapa 1 do PLANO.md)
    │   └── src/lib/Kernel/
    │       ├── Syscalls/RPC.cpp                 SifBindRpc (linha 254), SifCallRpc
    │       ├── Stubs/SIF.cpp                    sceSifBindRpc chama ps2_syscalls::SifBindRpc
    │       ├── Stubs/CD.cpp                     HLE de sceCdSearchFile (linha 545), sceCdRead (207)
    │       └── Stubs/Helpers/Support.h          pseudo-LBN (linha 7), registerCdFile (325),
    │                                            readCdSectors (429), writeCdSearchResult (520)
    ├── ps2xIOP/src/
    │   ├── iop_subsystem.cpp                    canBindRpc (linha 188)
    │   ├── iop_module_manager.cpp               nomes de módulos reconhecidos (cdvdman, cdvdfsv)
    │   ├── modules/                             serviços HLE: mcserv.cpp, dbcman.cpp, libsd.cpp
    │   └── emulator/
    │       ├── iop_emulator.cpp                 hasRpcServer (linha 756)
    │       └── services/iop_rpc.cpp             sceSifRegisterRpc no case 17 (m_servers[sid] = server)
    ├── imgui.ini                                untracked, ignorar (o `ps2_log.txt`, rastro de chamadas, é recriado a cada execução, ~265 MB; o antigo foi movido para /tmp)
    ├── build.log                                log do build do runtime
    └── run*.log, run*_raw.log                   logs de execução (run14_raw.log e run15_raw.log têm a telemetria do objeto 0x4FADEC)
```

Fora da pasta do projeto:

- `~/mksm-backup/`: backup do IndexedDB do Brave (site antigo mortalkombatsm.online, que emulava o jogo em WebAssembly). Contém a imagem de cartão de memória do usuário
- `~/mksm-venv/` e `~/mksm-extract.py`: venv Python com `cramjam` e o script que extraiu o cartão de memória
- `memcard.ps2` (8.650.752 bytes, formato PS2 com ECC) em `~/mksm-backup/https_mortalkombatsm.online_0.indexeddb.blob/1/00/memcard.ps2`. Pode ser testado no PCSX2 (`~/.config/PCSX2/memcards/`). Não foi confirmado que tem save do jogo
- `~/iop_rpc.cpp.bak`: backup do `iop_rpc.cpp` (só existe se a edição de log da seção "Próximos passos" for feita)
- `~/.config/fish/functions/dis.fish`: função fish `dis ENDEREÇO N`, que imprime as N primeiras instruções da função recompilada que contém o endereço (definição em "Comandos úteis de diagnóstico")
- `/tmp/ps2_log.txt`: o antigo log, movido para lá em 01/11/2026 (hoje `ls` mostra 267M; pode ser apagado). Um novo `PS2Recomp/ps2_log.txt` (265M) já foi recriado por execuções recentes

## Dados da ISO

- Região USA, serial SLUS-21087, NTSC, versão 1.02, volume `MKSM`
- `SYSTEM.CNF`: `BOOT2 = cdrom0:\SLUS_210.87;1`, `VER = 1.02`, `VMODE = NTSC`
- ISO de 3.495.460.864 bytes (3334 MiB)
- ELF: `ELF 32-bit LSB executable, MIPS, MIPS-III, statically linked, stripped`, 4.934.152 bytes. Não está empacotado. Entrada em `0x11c070`; segmento carregado em `0x100000-0x780898`

## Passo a passo para reproduzir

Todos os comandos assumem a pasta do projeto, salvo indicação.

### 1. Extrair a ISO

```
cd /run/media/valk/linux_arquivos/projetos/ShaolinMonks
mkdir -p extraido
7z x "Mortal Kombat - Shaolin Monks (USA).iso" SYSTEM.CNF SLUS_210.87 -oextraido
7z x "Mortal Kombat - Shaolin Monks (USA).iso" -oextraido_full
```

A extração completa (3,5 GB) é necessária para rodar: o runtime lê os módulos IOP e os dados do jogo da pasta do ELF.

### 2. Clonar e compilar as ferramentas

```
git clone --recurse-submodules https://github.com/ran-j/PS2Recomp.git
cd PS2Recomp
cmake -S . -B out/build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build out/build -j4 --target ps2_analyzer ps2_recomp
```

A configuração levou ~21 min por causa do download das dependências (raylib, imgui, SDL2 e outras). Resultado: `ps2_analyzer` (25 MB) e `ps2_recomp` (15 MB).

### 3. Analisar o ELF

```
cd /run/media/valk/linux_arquivos/projetos/ShaolinMonks
./PS2Recomp/out/build/ps2xAnalyzer/ps2_analyzer extraido/SLUS_210.87 sm.toml
```

Resultado (18 s): 155 funções de biblioteca marcadas para stub, 288 detectadas sem handler no runtime, 0 patches, 6 jump tables. Muitas funções ficam como `sub_XXXXXXXX`, esperado num ELF stripped.

Depois, editar o `sm.toml` e adicionar `low_memory_mode = true` logo abaixo de `single_file_output = false` na seção `[general]`:

```
[general]
input = "extraido/SLUS_210.87"
ghidra_output = ""
output = "./output/"
single_file_output = false
low_memory_mode = true
patch_syscalls = false
patch_cop0 = true
patch_cache = true
stubs = [ "exit@0x00202068", "sceGsSyncV@0x00206030", ... ]
```

Os caminhos são relativos: rode os comandos a partir de `ShaolinMonks/`.

### 4. Recompilar o ELF para C++

```
./PS2Recomp/out/build/ps2xRecomp/ps2_recomp sm.toml
ls output | wc -l
du -sh output
```

Resultado: "Recompilation completed successfully", 13.651 arquivos, 236 MB. Os avisos `unresolved JR/JALR ... promoted N fallback entries` são esperados (saltos indiretos resolvidos em tempo de execução).

### 5. Copiar o C++ gerado para o runtime

```
cp output/*.cpp PS2Recomp/ps2xRuntime/src/runner/
cp output/*.h PS2Recomp/ps2xRuntime/include/
```

Conferir: `ls output/*.cpp | wc -l` deve bater com `ls PS2Recomp/ps2xRuntime/src/runner/*.cpp | wc -l` (13.649). O `register_functions.cpp` copiado sobrescreve o placeholder. O CMake do runtime usa `file(GLOB ... CONFIGURE_DEPENDS)`, então enxerga os arquivos novos sozinho.

### 6. Configurar o CMake para o runtime (GCC no Linux)

O projeto é testado principalmente com MSVC, e dois problemas apareceram no GCC 16. Foram descobertos compilando só um arquivo pequeno antes do build completo (dentro de `out/build`: `ninja ps2xRuntime/CMakeFiles/ps2EntryRunner.dir/src/runner/sub_001A2A70_0x1a2a70.cpp.o`). Vale repetir esse teste sempre que mudar flags.

- **Erro `_mm_extract_epi32 ... target specific option mismatch`** (em `ps2_runtime_macros.h`): faltava SSE4.1. Solução: `-msse4.1` em `CMAKE_CXX_FLAGS`. Se aparecer erro parecido com outra instrução, trocar por `-march=native`
- **`-flto=auto` ligado sozinho**: vem de `ps2xRuntime/cmake/ReleaseMode.cmake`. No GCC a função `EnableFastReleaseMode` só faz `set_property(... INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)`, e é chamada só quando o build é Release ou RelWithDebInfo. `-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF` não resolve (a propriedade por alvo ganha). LTO sobre 13 mil arquivos seria inviável numa máquina modesta. Solução: build type `None` com as flags manuais

Configuração final (a partir de `PS2Recomp/`):

```
cmake -S . -B out/build -DCMAKE_BUILD_TYPE=None -DCMAKE_CXX_FLAGS="-msse4.1 -O1 -g0" -DPS2X_ENABLE_RUNNER_UNITY_BUILD=OFF -DPS2X_ENABLE_RUNNER_PCH=OFF
```

Conferir o comando real do compilador:

```
cd out/build
ninja -t commands ps2EntryRunner | grep -m1 sub_001A2A70 | grep -o "msse4.1\|flto=auto\|-O1"
cd ../..
```

Esperado: `msse4.1` e `-O1`, **sem** `flto=auto`. Unity build e PCH ficam em `OFF` para o consumo de memória ser previsível.

### 7. Compilar o runtime

Notebook na tomada, suspensão desativada, navegador fechado se possível. A partir de `PS2Recomp/`:

```
systemd-run --user --scope -p MemoryMax=12G -p MemorySwapMax=4G cmake --build out/build -j2 --target ps2EntryRunner 2>&1 | tee build.log
```

O `systemd-run` limita a memória do build; se estourar, só o build é morto e o desktop continua vivo. Se interromper, rode o mesmo comando de novo (o Ninja continua de onde parou).

Para acompanhar, em outra aba, na mesma pasta, depois que o `build.log` existir:

```
watch -n 5 tail -n 2 build.log
```

Para impedir suspensão durante a noite (deixar a aba aberta, sem Ctrl+C):

```
systemd-inhibit --what=sleep:idle:handle-lid-switch --who=build --why="PS2Recomp compilando" sleep 43200
```

Resultados reais: 13.763 etapas em ~3h30, uns 4 GB de RAM em uso de 15 (o `register_functions.cpp` compilou cedo, na etapa 210, com `.o` de 10 MB), 0 erros, binário final de 97 MB. O build não precisa de internet (as dependências já foram baixadas na configuração). Se falhar: `grep -m5 -n "error" build.log`; se aparecer `Killed` ou `signal 9`, falta de memória (a saída seria dividir o `register_functions.cpp`).

### 8. Rodar

**Rode o ELF de dentro da pasta com a ISO inteira extraída.** O runtime usa a pasta do ELF como raiz do disco (`cdrom0:`). Com o ELF isolado (`extraido/`), os módulos IOP falham com `failed to open IRX` e o jogo trava em `pc=0x41EDB0`; com `extraido_full/` carregam todos. O runtime só aceita o ELF como `argv[1]` e não lê variáveis de ambiente (`getenv` não aparece no código).

```
cd /run/media/valk/linux_arquivos/projetos/ShaolinMonks/PS2Recomp
timeout 90 script -qfc "./out/build/ps2xRuntime/ps2EntryRunner ../extraido_full/SLUS_210.87" run3_raw.log
grep -av "run:tick" run3_raw.log > run3.log
```

**Por que usar `script`:** o `ps2EntryRunner` guarda a saída em blocos de 4 KB quando ela vai para pipe ou arquivo, e o que sobra no buffer se perde quando o `timeout` o mata (o log ficava em 4,1 KB). Nem `grep --line-buffered` nem `stdbuf -oL -eL` resolvem. O `script` cria um pseudo-terminal, e o programa passa a descarregar linha a linha. Outra alternativa é rodar sem `timeout` e fechar a janela do jogo, para ele encerrar normalmente. Use `grep -a` ao filtrar o resultado do `script` (pode haver caracteres de controle). O log tem ticks repetidos (`[run:tick]`, a cada 120 ticks) que convém filtrar com `grep -v`.

## Bloqueio resolvido em 01/11/2026: bind do `libcdvd` (Etapa 1)

> Esta seção registra o diagnóstico que levou à Etapa 1. Ela fica no documento porque explica **por que** o override existe. Para o estado atual, pule para [Bloqueio atual: o jogo espera um objeto de player em status 2](#bloqueio-atual-o-jogo-espera-um-objeto-de-player-em-status-2).

### O que se via antes

O jogo abre uma janela com fundo magenta e o painel "Runtime Debugger" (ImGui) mostrando `Runtime: running`. Todos os módulos IOP carregam (SIO2MAN, PADMAN, LIBSD, SDRDRV, MCMAN, MCSERV, CDVDSTM, CRI_ADXI, SNDFI), o `CRI_ADXI` imprime a inicialização do driver, e o jogo registra um callback de VSync (`sceGsSyncVCallback:set new=0x26efe8`). Depois disso o PC fica parado em `0x385D80` (`RA=0x385DB4`), sem desenhar nada e sem nenhuma mensagem de erro. Foi deixado rodando 5 minutos e não saiu do lugar, então não é lentidão.

Linha do tempo dos travamentos:
- Com o ELF isolado: preso em `pc=0x41EDB0` (módulos IOP não carregavam)
- Com `extraido_full`: avançava até `pc=0x385D80` (bloqueio antigo)

Observações do log: o `PADMAN.IRX` imprime `yet sif hasn't been init` (pode ser inofensivo); o `CRI_ADXI` imprime a inicialização e o `SNDFI.IRX` não imprime nada. Antes da Etapa 1 o callback de VSync aparecia registrado, mas nunca disparava; **depois da Etapa 1 ele dispara** (`[run:tick] pc=0x26efe8` intercalado com `pc=0x24cdd4`). O painel de registradores do depurador é "best-effort" enquanto o jogo roda e mostra snapshots inconsistentes (SP e RA do cabeçalho não batem com a tabela), então **não se deve tirar conclusões dos valores dos registradores**.

### Diagnóstico confirmado

O código do laço está em `output/sub_00385CE0_0x385ce0.cpp` (a função de 45 KB que contém `0x385D80`).

1. O trecho `0x385D80..0x385D94` é só um laço de atraso por contagem regressiva (`addiu v0,v0,-1`, 5 nops, `bne v0,v1`)
2. Em `0x385DA0..0x385DAC` o jogo chama `func_4834E0` com `a0 = 0x758640` (cliente), `a1 = 0x80000597` (id do servidor) e `a2 = 0`
3. `func_4834E0` (`output/sub_004834E0_0x4834e0.cpp`) é um stub que só chama `ps2_stubs::sceSifBindRpc`. **Ou seja, é `sceSifBindRpc(&client, 0x80000597, 0)`**
4. O handler (`ps2xRuntime/src/lib/Kernel/Syscalls/RPC.cpp:254`) **sempre retorna 0** (só retorna -1 se o ponteiro do cliente for inválido). Então não é o valor de retorno que causa a repetição
5. O handler só preenche `client->server` se `PS2IopTransport::canBindRpc(runtime, rpcId)` for verdadeiro (RPC.cpp:298). Isso cai em `IopSubsystem::canBindRpc` (`ps2xIOP/src/iop_subsystem.cpp:188`), que exige uma de duas coisas: uma rota HLE registrada para o id, ou o emulador do IOP ter um servidor com esse id (`emulator.hasRpcServer` → `rpc.hasServer`). O comentário no código diz que `client->server == nullptr` é o sinal de nova tentativa enquanto o servidor do IOP ainda está iniciando
6. Em `0x385DB4` (`bgezl $v0`), se o bind retornou >= 0, o delay slot em `0x385DB8` faz `lw v0,0x24($s0)` — lê `client->server`. Em `0x385E04` (`beqz $v0`) o jogo volta para `0x385D78` enquanto o campo for zero, espera no laço de atraso (`v0 = 0x100000`) e chama o bind de novo. Esse é o padrão do SDK da Sony (`do { bind; esperar } while (client.server == 0)`)
7. Nenhuma das duas condições de `canBindRpc` vale para `0x80000597`, então o jogo gira para sempre. O bind "dá certo" (retorna 0) e não registra nada no log, por isso o log fica mudo

Os módulos IOP são executados de verdade por um emulador de IOP: `ps2xIOP/src/emulator/services/iop_rpc.cpp`, `case 17` (`sceSifRegisterRpc`) guarda o servidor em `m_servers[sid]`. Se um IRX registrar o id `0x80000597`, o bind passa a funcionar. Por HLE, o runtime só conhece três serviços, todos em `ps2xIOP/src/modules/`: MCSERV (`0x80000400`/`0x80000480`), LIBSD (`0x80000701`) e DBCMAN (`0x80001300`). Não existe serviço para o `cdvdman`.

### Quem deveria registrar o `0x80000597`

O `0x80000597` é o SID do **`cdvdman`**, o gerenciador de CD/DVD do IOP. O `libcdvd` do EE fala com ele por SIF RPC. O `CDVDSTM.IRX` que o jogo carrega é **cliente** do `cdvdman` (as strings do IRX mostram `cdvdman`), não o servidor. O `cdvdman` é um módulo de ROM do IOP: o `ps2xIOP` até reconhece o nome (`iop_module_manager.cpp` lista `cdvdman` e `cdvdfsv`), mas **não implementa o serviço RPC**, então nada registra esse SID.

Isso explica por que a busca por `0x80000597` nos IRX não achou nada: o valor não precisa estar em nenhum IRX. Também explica por que o `SNDFI.IRX` não é o culpado.

O `start=2` que o log mostra em vários módulos provavelmente é só o código de retorno da função de entrada (módulo residente), não uma pista.

### Solução executada (Etapa 1)

O plano completo, com código e comandos, está em `PLANO.md` (seções 2 e 10). Em resumo:

1. **Etapa 1 (executada):** um game override (`PS2_REGISTER_GAME_OVERRIDE`) que troca as funções guest do libcdvd (`0x385CE0`, `0x386158`, `0x3861F0`, `0x386290`, `0x386458`, `0x386740`, `0x386818`, `0x467940`, `0x467B28`, `0x467BC0`) pelas HLE equivalentes de `CD.cpp`. Isso evita o bind por completo e não mexe nos 13.649 arquivos gerados
2. **Etapa 2 (condicional, não necessária até agora):** se aparecer `sceCdRead unresolved LBN`, configurar `cdImage` no override e subir `kCdPseudoLbnStart` (`Support.h:7`) para `0x00800000`, porque a ISO tem `0x1A0B10` setores e os pseudo-LBNs atuais (`0x100000`) colidiriam com LBNs reais
3. **Etapa 3:** triagem dos próximos bloqueios (apresentação do frame/GS, VSync/COP0, áudio CRI/LIBSD, imports de IOP)
4. **Etapa 4 (opcional):** implementar o serviço `cdvdman` no `ps2xIOP`, no molde de `libsd.cpp`, para manter o libcdvd guest intacto

Arquivos alterados:

| Arquivo | Mudança |
|---|---|
| `PS2Recomp/ps2xRuntime/src/lib/game_overrides_mksm.cpp` | **novo**: override `mksm-cdvd` com 10 bindings comentados |
| `PS2Recomp/ps2xRuntime/CMakeLists.txt` | `target_sources(ps2EntryRunner PRIVATE .../game_overrides_mksm.cpp)` logo após o `if(PS2X_IS_ANDROID)/else()/endif()` que cria o alvo |

Nada em `output/` nem em `ps2xRuntime/src/runner/` foi alterado.

### Evidência da Etapa 1

Build incremental (a partir de `PS2Recomp/`):

```
cmake --build out/build -j2 --target ps2EntryRunner
...
[1/5] Building CXX object ps2xRuntime/CMakeFiles/ps2EntryRunner.dir/src/lib/game_overrides_mksm.cpp.o
[2/5] Linking CXX executable ps2xRuntime/ps2EntryRunner
```

Execução:

```
timeout 90 script -qfc "./out/build/ps2xRuntime/ps2EntryRunner ../extraido_full/SLUS_210.87" run4_raw.log
grep -av "run:tick" run4_raw.log > run4.log
```

Resultado verificado:

- `[game_overrides] applying 'mksm-cdvd'` e `[mksm] applying libcdvd -> HLE overrides (10 bindings)`
- **10/10 binds OK**, **0 `FAILED to bind`**:

```
sceCdLayerSearchFile @0x385ce0 -> sceCdSearchFile
sceCdNcmdDiskReady   @0x386158 -> sceCdNcmdDiskReady
sceCdSync            @0x3861f0 -> sceCdSync
sceCdSyncS           @0x386290 -> sceCdSyncS
sceCdInit            @0x386458 -> sceCdInit
sceCdMmode           @0x386740 -> sceCdMmode
sceCdRead            @0x467940 -> sceCdRead
sceCdGetError        @0x467b28 -> sceCdGetError
sceCdBreak           @0x467bc0 -> sceCdBreak
sceCdReadClock       @0x386818 -> sceCdReadClock
```

- **16 `[sceCdSearchFile:ok]`**, incluindo `FILELIST.DIR`, `WADCRC.BIN`, todos os `GAMEDATA.WA*` e todos os `FRONT\MOVIES\*.SFD`
- **0 `sceCdRead unresolved LBN`** → a Etapa 2 não foi necessária
- O PC saiu do laço e entrou no loop de frame:

| Fase | PC observado | Significado |
|---|---|---|
| Boot/inicialização | `0x2130A0` | init do jogo |
| Loop principal | `0x26EFE8` | callback de VSync registrado pelo jogo |
| Loop principal | `0x24CDD4` | retorno do wrapper `sub_0024CDC0`, que chama `sceGsSyncV(0)` |

- 44 linhas de tick; último `tick=5280` em ~92 s → **~57 fps**, não é lentidão nem espera infinita
- `activeThreads=1`; o callback de VSync está disparando de verdade

## Etapa 3 (apresentação do frame) resolvida em 01/11/2026

A tela magenta **não era** o jogo desenhando: era o fallback do runtime (`UploadFrame`, em `ps2_runtime.cpp`), que desenha `GenImageColor(..., MAGENTA)` quando `copyLatchedHostPresentationFrame` falha. Havia duas causas, ambas corrigidas (arquivos alterados e detalhes em `PLANO.md`, seções 4.1 e 11):

1. `PMODE` (`0x12000000`) e `SMODE2` (`0x12000020`) são registradores do barramento privilegiado do GS e não podem ser escritos por pacote GIF A+D. Agora são gravados direto na memória de registradores do GS (`GS.cpp`)
2. O layout de `sceGsDispEnv` estava errado. A struct tem 40 bytes (`dispfb1`, `display1`, `dispfb2`, `display2`, `bgcolor`) e o `sceGsPutDispEnv` antigo sobrescrevia o `PMODE` com zero (`GsDispEnvMem`, em `Support.h`)

Resultado: `[frame:upload]` contínuo (640x224 / 640x448), 0 ocorrências de `MAGENTA` nos logs `run11` a `run13`, e a janela passou a mostrar a saída real do GS (tela preta). O `display1=0xdf27f00000000`, que parecia suspeito, é válido: pelo layout do registrador DISPLAY (conferido à mão), `DW=0x27f` (640 px) e `DH=0xdf` (224 linhas).

## Bloqueio atual: o jogo espera um objeto de player em status 2

### O que se vê

Janela preta com o painel "Runtime Debugger". O jogo roda a ~60 fps sem crash (mais de 9480 ticks, ~160 s) e com `activeThreads=1`. O PC alterna entre `0x24CDD4` (retorno do wrapper `sub_0024CDC0`, que chama `sceGsSyncV(0)`), `0x26EFE8` (callback de VSync) e o chamador `caller=0x211BE0`, dentro de `sub_00211B38`: um loop de espera orientado a estado (`0x4EF7E0 == 2`). Ele só sai quando `func_370A98` (que chama `func_26F6E0`) retorna diferente de zero.

Do disco, o jogo fez **um** `sceCdRead` (`FILELIST.DIR`, `lbn=0x100000`, 2 setores) e 16 `sceCdSearchFile` (`FILELIST.DIR`, `WADCRC.BIN`, os 7 `GAMEDATA.WA*` e 7 `.SFD`). Parece catalogação de arquivos, não carregamento. Depois disso não houve mais nenhum `sceCdRead`. (Versões anteriores deste documento diziam que nenhum `sceCdRead` tinha sido feito; estava errado.)

### Telemetria adicionada ao `[run:tick]` (`ps2_runtime.cpp`)

| Campo | O que lê | Observado (run14, ticks 600 a 3480) |
|---|---|---|
| `obj` | `*(0x4FADEC)`, ponteiro do objeto | `0x54fb60` (não nulo) |
| `objw0` / `status` | `*(obj)` / byte `obj+1` | `0x201` / **2** |
| `cnt` | `*(obj+0x20)` | **0** |
| `total` | `*(0x678440)` | 29 (significado desconhecido) |
| `pct` | `*(0x678448)`, igual a `100*cnt/total` | 0 |
| `cb` | `*(0x4FADF0)`, gancho opcional chamado pelo callback de VSync | `0x0` (run15) |
| `flag4ba0` | `*(0x4F4BA0)`; se diferente de zero, o callback pula o servidor `0x4153A0` | ainda não lido (a linha saiu truncada) |
| `vs` | `*(0x678450)`, contador incrementado pelo callback de VSync | ainda não lido (idem) |

Os valores são idênticos do tick 600 ao 3480: o objeto existe, está "ativo" e nunca avança. No run15 o boot demorou mais (ficou em `pc=0x2130a0` até o tick ~1680) e a última linha saiu cortada porque o `timeout` matou o jogo no meio da escrita. Use `timeout 60` e olhe `tail -3`.

### O que o código mostra (leitura de `output/*.cpp`, sem confirmação em execução)

- **Criação:** `0x26F570` destrói o objeto anterior (`0x26F790`, que também zera `0x678448`), chama `func_412CE8(..., 0)` e grava o resultado em `*(0x4FADEC)`. `0x3709F8` é só um wrapper dela. `0x412CE8` é lock + `func_412D30` + unlock, e `0x412D30` aloca (`0x412BA0`), inicializa (`0x412C38`) e, se a inicialização falhar, destrói (`0x413128`) e devolve 0. Como o objeto existe, a criação deu certo
- **Consulta de status:** `0x26F6E0` lê o status via `0x413BB0` → `0x413BE8` (byte `obj+1`). Status 4 ou negativo retorna `-1`; status 3 retorna `1`; qualquer outro retorna `0`. Também grava `0x678444 = obj[0x20]` (via `0x413B80`) e `0x678448 = 100*obj[0x20]/[0x678440]`. Com objeto nulo, os getters caem em `0x42D298` (handler de erro: monta uma mensagem no buffer `0x54FA58` e chama o callback de `[0x54FA50]`, se houver)
- **Lock/unlock vazios:** `0x413F78` e `0x413F90` só saltam para `0x42C7C0` e `0x42C7C8`, que são um `jr $ra` puro. **Não** são stubs do runtime (não aparecem no `sm.toml` nem em `ps2_recompiled_stubs.h`): o ELF é assim
- **Callback de VSync `0x26EFE8`:** chama `func_485FB8` (só `di` com espera no Status), incrementa os contadores `0x67B79C` e `0x678450`, chama o ponteiro em `0x4FADF0` se não for zero, chama `func_4154C8(0)` se `[0x4F4BA0] == 0` e termina com `ei`, retornando 1
- **Servidor periódico (hipótese) `0x4153A0`:** `0x4154C8` só salta para ele. Ele incrementa `0x533490`, usa `0x53423C` como trava de reentrada, só executa o corpo se `[0x534124] > 0`, chama `func_414938` e `func_414D08`, conta execuções em `0x5341B0` e chama um callback de usuário em `[0x534240]`. A interpretação como "servidor do player" é palpite pelo padrão

### Hipóteses (nenhuma confirmada)

1. **Player de vídeo (Sofdec/MPEG) preso "tocando" sem decodificar.** Os status 0 a 4 batem com a convenção que me lembro do Sofdec/`mwply` (parado, preparando, tocando, fim, erro; **de memória, a verificar**) e com o que `0x26F6E0` testa (3 vira sucesso, 4 vira erro). Arquivo provável: `MIDWAY.SFD`, o primeiro da lista. A favor: o ELF traz o `libmpeg` inteiro, e `sceMpegCreate`, `sceMpegInit`, `sceMpegGetPicture`, `sceMpegReset`, `sceMpegAddCallback` e `sceIpu*` (linhas 118 a 129 do `sm.toml`, dentro de `stubs`) são trocadas por HLE do runtime (`Stubs/MPEG.cpp`, `Stubs/IPU.cpp`); os `_sceMpeg*` internos (linhas 336 a 390) estão em `entry_points`, ou seja, são código do jogo recompilado. Incerto: nenhum `sceCdSt*` foi detectado no `sm.toml`, então o streaming pode ir pelos IRX da CRI (`CDVDSTM`/`CRI_ADXI`). **Atualização (rodada de leitura abaixo):** os chamadores de `sceMpegInit`/`Create`/`GetPicture` ficam todos em `0x446xxx`–`0x44Axxx`, longe do loader e do objeto, então esta hipótese está menos firme
2. **O servidor `0x4153A0` não roda** (por `[0x4F4BA0] != 0`) **ou roda sem player registrado** (`[0x534124] == 0`) — **descartada pelo `run16`** (ver abaixo)
3. **Faltam dados ou decodificação:** o MPEG/IPU HLE não produz frames, ou o IOP (CRI/`cdvdman`) não entrega os dados do `.SFD`. O emulador do IOP já trata os imports da biblioteca `cdvdman` (`iop_cdvd.cpp`, versão `0x0226`); o que não existe é o servidor RPC `0x80000597` do lado do EE, que o override da Etapa 1 contorna

Descartadas com os dados desta rodada: objeto nulo (ele existe), `0x42C7C0` ser stub do runtime (é lock/unlock vazio do ELF) e "nenhum `sceCdRead`" (houve um).

### Rodada de leitura (01/11/2026): rastreador, chamadores do bloco MPEG e `dis`

Detalhes em `PLANO.md`, seção 4.7. Resumo do que foi medido e lido (nada disto é um `run16`):

- **O rastreador de funções está ligado.** Existe um `PS2Recomp/ps2_log.txt` novo (265M, recriado por uma execução recente), além do antigo em `/tmp` (267M). O formato é `>> sub_XXXXXXXX_0x... enter` / `<< ... exit`, com indentação pela profundidade de chamada. A contagem do `grep -c` sobre o bloco `0x44C` não saiu legível (o `head -c 600` cortou a linha e o número ficou colado nela), então **ainda não se sabe se esse bloco roda**
- **Chamadores do bloco `sceMpeg*`** (busca do literal em `output/`, só `jal` direto): `sceMpegInit` (`0x44C7B8`) em `sub_0044C7A8` e `sub_00446878`; `sceMpegCreate` (`0x44C808`) em `sub_0044A8D0`; `sceMpegGetPicture` (`0x44CF60`) em `sub_00449BA0`. Todos em `0x446xxx` a `0x44Axxx`, longe do loader (`0x211xxx`, `0x26Fxxx`) e do objeto (`0x412xxx`, `0x413xxx`). Parece um módulo de FMV próprio, sem ligação direta com o que trava
- **`dis` de três funções** (parcial: o `dis` omite alguns delay slots): `0x414938` consulta um status via `func_47FD70` e compara com `4` antes de chamar `func_480BE0`; `0x414D08` é um getter minúsculo de uma global em `0x53xxxx`; `0x412C38` é o inicializador do objeto (opera sobre um sub-objeto em `obj+4` e **não** grava o byte de status em `obj+1`)
- **Efeito:** a hipótese do player Sofdec/MPEG fica menos firme. O objeto pode ser outra coisa (por exemplo um streamer da CRI). Não foi descartada
- Ainda sem leitura: `0x4153A0`, `0x42D838`, `0x417538`, `0x4176D0`, `0x417650`, `0x418300`, `0x418340`, `0x47FD70`, `0x480BE0`

### Resultado do experimento do servidor (`run16`, executado em 01/11/2026)

Medido (ticks 600 a 3480): `flag4ba0=0`, `srvCalls == srvRuns == vs` em todas as linhas (13 no tick 600, 1553 no tick 3480), `srvN=1`, `guard=0`, `usercb=0x0`, e o objeto igual ao de antes (`status=2`, `cnt=0`, `total=29`, `cb=0x0`, `activeThreads=1`). `grep -ai "mpeg\|ipu"` no log não achou nada. Pelo rastreador, o bloco `0x44C` (`sceMpeg*`) tem 0 chamadas e `sub_00415380` tem 5180 linhas (cada chamada gera duas, entrada e saída).

- **Descartadas:** o servidor não roda; o servidor roda sem item registrado; o bloco MPEG do jogo ser a causa (nunca é chamado)
- **O que resta:** o servidor roda em todo VSync com um item registrado, e mesmo assim o objeto não avança. O servidor percorre três slots de handles (tabelas em torno de `0x534168`/`0x5341C0`) e chama `func_414938(handle)`, que consulta um status via `func_47FD70`, compara com `4` e chama `func_480BE0`
- **Confirmado em parte (4.7.9):** `0x47FD70` é um stub de syscall (`-0x31`, que pela tabela do EE de memória é `iReferThreadStatus`). `0x414938` consulta o thread do slot e, se estiver esperando, chama `0x480BE0`, que é um wrapper gerado de `iWakeupThread`. No rastro, `47FD70` rodou 6424 linhas no rastro (~3212 chamadas). **Correção:** a contagem 0 de `480BE0` não vale, porque esse arquivo foi gerado com `_DEBUG` e não com o rastreador; não se sabe se o wakeup acontece. Há dois handles não nulos, então a ideia de "thread inexistente" ficou enfraquecida. O runtime não imprime criação de thread do EE. **Atualização (4.7.13) e correção (4.7.15):** `CreateThread` rodou ~4 vezes. As contagens 0 de `StartThread`/`WakeupThread` nos stubs `0x47FC80`/`0x47FD90`/`0x47FDA0` **não valem**: o jogo usa `StartThread@0x480D48` e `iWakeupThread@0x480BE0` (stubs HLE da biblioteca de threads, sem rastreador). A hipótese "threads criados e nunca iniciados" foi retirada. Detalhes e comandos em `PLANO.md`, seções 4.7.9 a 4.7.16

### Como o experimento foi montado (histórico)

Linhas a editar em `PS2Recomp/ps2xRuntime/src/lib/ps2_runtime.cpp` (achadas com `grep -n 'const uint32_t dbgVs\|<< " vs=" << dbgVs'`): `2428` (`dbgVs`, onde entram as cinco leituras) e `2455` (`<< " vs=" << dbgVs`, onde entram os cinco campos do log). Edite primeiro a de número maior. O código a colar está em `PLANO.md`, seção 4.7.5. Na primeira tentativa as edições entraram duplicadas e foram removidas com `sed` (backup em `~/ps2_runtime.cpp.bak`).

Acrescentar ao `[run:tick]` cinco valores: `srvCalls` (`0x533490`), `srvN` (`0x534124`), `srvRuns` (`0x5341B0`), `guard` (`0x53423C`) e `usercb` (`0x534240`), rodar com `timeout 60` e ler `tail -3` (para também obter `flag4ba0` e `vs`). Leitura do resultado:

- `flag4ba0 != 0` ou `srvCalls` parado: o servidor nunca roda; descobrir quem liga o flag
- `srvCalls` crescendo e `srvN == 0`: o servidor roda, mas o player não está registrado nele
- `srvN > 0`, `srvRuns` crescendo e `cnt` ainda 0: o servidor roda e a falha é de decodificação ou de dados; ler `func_414938`, `func_414D08` e os stubs de MPEG/IPU

Também vale: `grep -ai "mpeg\|ipu" run16_raw.log` (para ver se o runtime registra alguma chamada de `sceMpeg*`/`sceIpu*`) e olhar as abas **File/CD**, **RPC History** e **Threads** do depurador durante o loop. Só depois, como triagem num game override, forçar a saída do loop para ver se o jogo passa a chamar `sceCdRead` dos WADs.

## Próximos passos

O plano detalhado está em `PLANO.md` (a investigação atual está nas seções 4.5 e 4.6). Ordem de execução:

1. ~~**Etapa 1 (recomendada):** criar o override do libcdvd e adicioná-lo ao alvo `ps2EntryRunner`~~ — **concluída e verificada** (ver seção acima e `PLANO.md`, seção 10)
2. ~~**Etapa 3, parte 1:** fazer o frame chegar à tela (tela magenta)~~ — **concluída e verificada** (ver "Etapa 3 (apresentação do frame) resolvida" e `PLANO.md`, seção 11)
3. **Etapa 3, parte 2 (atual):** descobrir por que o objeto do player em `*(0x4FADEC)` fica em status 2 com 0 frames (`PLANO.md`, seções 4.5 e 4.6):
   0. (rápido, sem recompilar) com o rastreador: `grep -c "sub_00415380" PS2Recomp/ps2_log.txt` diz se o servidor `0x4153A0` roda; `grep -c "sub_0044C808\|sub_0044C7B8\|sub_0044CF60" PS2Recomp/ps2_log.txt` diz se o bloco MPEG roda (comandos em `PLANO.md`, 4.7.6)
   1. ~~acrescentar ao `[run:tick]` os cinco valores do servidor~~ — **feito** (`run16`; resultado acima)
   1. ~~ler `0x47FD70` com `dis` e ver no rastreador o que o servidor chama~~ — **feito** (4.7.9)
   1. ~~ler os corpos no runtime e contar `CreateThread`/`StartThread`/`WakeupThread`~~ — **feito** (4.7.13)
   1. (atual) ler os 4 criadores de thread (`0x414AB8` e vizinhos) e ver no runtime se `StartThread`/`iWakeupThread` chegam a rodar, de preferência com um `fprintf` temporário em `Thread.cpp` (`PLANO.md`, 4.7.16)
   2. conforme o resultado, ler `0x4153A0`, `func_414938` e `func_414D08` com `dis` e conferir os stubs `Stubs/MPEG.cpp` e `Stubs/IPU.cpp`
   3. só então, como triagem num game override, forçar a saída do loop para ver se o jogo passa a chamar `sceCdRead` dos WADs
4. **Etapa 2 (só se necessário):** se o log mostrar `sceCdRead unresolved LBN`, apontar `cdImage` para a ISO dentro do override e subir `kCdPseudoLbnStart` para `0x00800000` (`PLANO.md`, seção 3)
5. **Etapa 4 (opcional):** serviço `cdvdman` no `ps2xIOP` (`PLANO.md`, seção 5)
6. **Diagnóstico auxiliar (se precisar confirmar SIDs):** logar os registros de servidor em `iop_rpc.cpp`. A partir de `PS2Recomp/`:
   ```
   grep -n "m_servers\[server.sid\] = server;" ps2xIOP/src/emulator/services/iop_rpc.cpp
   cp ps2xIOP/src/emulator/services/iop_rpc.cpp ~/iop_rpc.cpp.bak
   nano +NUMERO ps2xIOP/src/emulator/services/iop_rpc.cpp
   ```
   Logo abaixo dessa linha, acrescentar (e conferir se o arquivo tem `#include <cstdio>`):
   ```
   std::fprintf(stderr, "[iop:RegisterRpc] sid=0x%08x func=0x%08x\n", server.sid, server.function);
   ```
   Recompilar com `cmake --build out/build -j2 --target ps2EntryRunner 2>&1 | tail -n 5`, rodar com o `script` (seção 8) e conferir com `grep -a RegisterRpc run4_raw.log` quais ids os módulos de fato registram

## Comandos úteis de diagnóstico

```
# Instruções em volta de um endereço (o gerador comenta cada instrução e cria labels label_XXXXXX)
grep -n "0x385d80" output/sub_00385CE0_0x385ce0.cpp
sed -n '185,330p' output/sub_00385CE0_0x385ce0.cpp | cut -c1-170

# Qual arquivo contém um endereço: o arquivo sub_AAAAAAAA com maior endereço inicial <= alvo
ls output | grep -i "sub_00385"

# Mensagens mais frequentes no log
grep -ai "warning\|unimpl\|unhandled\|not implemented\|TODO\|missing" run4.log | cut -c1-200 | sort | uniq -c | sort -rn | head -30

# Procurar um valor de 32 bits nos IRX, lendo bytes de verdade (grep -P erra no UTF-8)
python3 -c 'import glob,re; [print(f, len(re.findall(rb"\x97\x05\x00\x80", open(f,"rb").read()))) for f in sorted(glob.glob("extraido_full/IOP/*"))]'

# Etapa 1: conferir binds e arquivos encontrados
grep -aE "\[mksm\] bound|FAILED to bind" run4_raw.log
grep -ac "sceCdSearchFile:ok" run4_raw.log
grep -ac "unresolved LBN" run4_raw.log

# Etapa 1: resumo do PC e do estado do GS por tick (o log usa CR; tr converte)
tr '\r' '\n' < run4_raw.log | grep -a "run:tick" | tail -5 | cut -c1-200

# Etapa 3: onde o frame é latcheado/apresentado e onde o magenta nasce
grep -n "latchHostPresentationFrame\|copyLatchedHostPresentationFrame\|MAGENTA" ps2xRuntime/src/lib/ps2_runtime.cpp

# Etapa 3 (parte 2): ler a função recompilada que contém um endereço (função fish `dis`, definida abaixo)
dis 0x4153a0 90

# Etapa 3 (parte 2): estado do objeto do player nos últimos ticks
tr '\r' '\n' < PS2Recomp/run15_raw.log | grep -a "run:tick" | tail -3 | cut -c1-600

# Rastreador: primeira aparição de uma função (use -m1, o arquivo tem ~265 MB) e contagem total
grep -n -m1 "sub_00415380" PS2Recomp/ps2_log.txt
grep -c "sub_00415380" PS2Recomp/ps2_log.txt

# Quem chama um endereço (só jal direto): arquivos que contêm o literal
grep -l "0x44C808u" output/sub_*.cpp | head -8
```

A função `dis` (fish) acha sozinha o arquivo `output/sub_XXXXXXXX_*.cpp` com maior endereço inicial menor ou igual ao alvo e imprime as primeiras N instruções. Funciona de qualquer pasta. Delay slots que são `nop` não aparecem na listagem:

```
function dis
    python3 -c 'import glob,re,sys; t=int(sys.argv[1],16); fs=[(int(re.search(r"sub_([0-9A-Fa-f]+)_",f).group(1),16),f) for f in glob.glob("/run/media/valk/linux_arquivos/projetos/ShaolinMonks/output/sub_*.cpp")]; s=max(x for x in fs if x[0]<=t); L=[l.rstrip()[:100] for l in open(s[1]) if l.startswith("    // 0x")]; print(s[1]); print("\n".join(L[:int(sys.argv[2])]))' $argv
end
funcsave dis
```

## Descobertas extras (01/11/2026)

Itens verificados no código e nos arquivos durante a análise que gerou o `PLANO.md`:

- **O binário já está sincronizado com o C++ gerado.** Os MD5 de `output/ps2_recompiled_functions.h` e `output/ps2_recompiled_stubs.h` são idênticos aos de `PS2Recomp/ps2xRuntime/include/ps2_recompiled_*.h`. Não é preciso rodar `ps2_recomp` de novo para testar correções de runtime; um build incremental resolve
- **`ps2_runtime` é `STATIC` e não há `WHOLE_ARCHIVE` no link.** Um arquivo de override que só contém inicializador estático é descartado pelo linker se não for referenciado. Por isso o `game_overrides_mksm.cpp` deve entrar em `target_sources(ps2EntryRunner PRIVATE ...)`, não só na biblioteca
- **A tela magenta é fallback do runtime, não desenho do jogo.** `UploadFrame` (`ps2_runtime.cpp:376`) tenta `latchHostPresentationFrame` + `copyLatchedHostPresentationFrame`; se a cópia falha, desenha `GenImageColor(..., MAGENTA)` em `ps2_runtime.cpp:417`. Ao investigar a Etapa 3, não confundir magenta com "frame limpo" do jogo
- **`display1=0xdf27f00000000` é válido (suspeita resolvida).** Pelo layout do registrador DISPLAY, `DW=0x27f` (640 px) e `DH=0xdf` (224 linhas). O problema real da tela magenta era `PMODE`/`SMODE2` e o layout de `sceGsDispEnv` (ver "Etapa 3 (apresentação do frame) resolvida")
- **`cdImage` nunca é configurado.** `configureIoPathsFromElf` (`ps2_runtime.cpp:1052`) só define `elfPath`, `elfDirectory`, `hostRoot`, `cdRoot` e `mcRoot`. Sem `cdImage`, o `sceCdRead` só resolve pseudo-LBNs registrados por `sceCdSearchFile`
- **Colisão de pseudo-LBN.** `kCdPseudoLbnStart = 0x00100000` (`Support.h:7`) fica abaixo do fim da ISO (`0x1A0B10` setores). Como `readCdSectors` consulta `g_cdFilesByKey` antes da imagem, LBNs reais nesse intervalo seriam sombreados. Só importa depois de configurar `cdImage`; a correção é subir a constante para `0x00800000`
- **`FILELIST.DIR` é texto puro**, 286 bytes, uma lista de nomes (`GAMEDATA.WAD`, `FRONT\MOVIES\...`). Não é um banco DVCI binário; quem interpreta é o código do jogo
- **`CDVDSTM.IRX` é cliente do `cdvdman`**, não o servidor. As strings do IRX mostram `cdvdman` e `cdvdstm`. Quem deveria registrar `0x80000597` é o `cdvdman` (ROM do IOP), que o runtime reconhece só pelo nome em `iop_module_manager.cpp:41-42`
- **`0x385FE0` é só um wrapper fino:** seta `$a2 = 0` e faz tail call para `0x385CE0`. Não precisa de override próprio
- **O layout de `CdlFILE` bate.** O guest copia 32 bytes em `0x385EF8..0x385F34`; `writeCdSearchResult` (`Support.h:520`) escreve o mesmo layout. Por isso a troca direta por `sceCdSearchFile` HLE é segura
- **Números atuais do `sm.toml`:** 156 stubs, 320 entry_points, 382 entradas MMIO, 6 jump tables e 941 funções críticas
- **`ps2xRuntime/src/runner/` está no `.gitignore` do PS2Recomp.** Os 13.649 `.cpp` gerados não devem ser commitados; documentar o comando de regeneração é melhor
- **O `ps2_log.txt` antigo foi movido para `/tmp/ps2_log.txt`** em 01/11/2026 (hoje 267M). Um novo `PS2Recomp/ps2_log.txt` (265M) é recriado a cada execução porque o rastreador está ligado. Os dois ficam fora do repositório (`ps2_log.txt` no `.gitignore`); apagar o de `/tmp` quando não precisar mais

Achados da investigação do loop em `0x211BE0` (detalhes em "Bloqueio atual" e em `PLANO.md`, seções 4.5 e 4.6):

- **O objeto em `*(0x4FADEC)` existe e está parado:** `obj=0x54fb60`, byte de status (`obj+1`) igual a 2, `cnt=0`, `total=29`, idêntico do tick 600 ao 3480. A hipótese do ponteiro nulo foi descartada
- **`0x42C7C0` e `0x42C7C8` não são stubs do runtime:** são `jr $ra` vazios que servem de lock/unlock (`0x413F78`/`0x413F90`). Não aparecem no `sm.toml` nem em `ps2_recompiled_stubs.h`
- **O jogo fez um `sceCdRead`** (o do `FILELIST.DIR`); a afirmação anterior de que nenhum tinha sido feito estava errada. O log de `sceCdRead` só imprime as primeiras 16 leituras, então esse foi o único até o loop
- **Layout do `sm.toml`:** `stubs` nas linhas 24 a 181, `entry_points` de 185 a 506, `[mmio]` na 512, `[jump_tables]` na 897 e `[performance] critical` a partir da 1065. `sceMpegCreate`/`sceMpegGetPicture`/`sceIpu*` etc. estão em `stubs` (linhas 118 a 129), então rodam como HLE do runtime; os `_sceMpeg*` internos (linhas 336 a 390) estão em `entry_points`
- **O runtime já tem implementação real de MPEG e IPU:** `Stubs/MPEG.cpp` (`sceMpegCreate` na linha 1986, `sceMpegGetPicture` na 2283) e `Stubs/IPU.cpp` (`sceIpuInit` na 53), além de `sceCdSt*` em `ps2_call_list.h`. O IOP emula os imports da biblioteca `cdvdman` (`ps2xIOP/src/emulator/imports/iop_cdvd.cpp`)
- **Callback de VSync (`0x26EFE8`):** não decodifica nada diretamente; chama o gancho de `0x4FADF0` (hoje `0x0`) e, se `[0x4F4BA0] == 0`, o servidor `0x4153A0`
- **Servidor `0x4153A0` lido e medido:** corpo executa em todo VSync (`srvCalls == srvRuns == vs`), `srvN=1`, `guard=0`, `usercb=0`. Ele percorre três slots e chama `func_414938(handle)`; `0x414D08` só devolve `[0x534128]`; `0x417538` e `0x4176D0` são wrappers lock/corpo/unlock (`0x418628`/`0x418640`)
- **O rastreador de funções está ligado** (`PS2Recomp/ps2_log.txt`, ~265 MB por execução, formato `>> nome enter` / `<< nome exit`). Serve para responder, sem recompilar, se uma função chegou a rodar e em que ordem
- **`grep -c` no rastreador conta linhas, não chamadas:** cada chamada gera duas linhas (`>>` e `<<`). Dividir por 2
- **Nem todo arquivo gerado usa o rastreador:** `sub_00480BE0` (wrapper de `iWakeupThread`) usa `#ifdef _DEBUG`, então nunca aparece no `ps2_log.txt`. Uma contagem 0 só vale se o arquivo tem `PS2_FUNCTION_LOG_TRACKER` (conferir com `grep -c`)
- **Dois conjuntos de funções de thread:** stubs de syscall puro (`0x47FC60` CreateThread, `0x47FC80`, `0x47FD70`, `0x47FD90`/`0x47FDA0`; nomes pelo número do syscall, de memória), que usam o rastreador, e a biblioteca de threads do SDK (`InitThread@0x480AF0`, `iWakeupThread@0x480BE0`, `StartThread@0x480D48`, em `stubs` do `sm.toml`; `topThread`, `DelayThread` e `_DelayThreadHandler` em `entry_points`), que o jogo de fato chama e que o rastreador **não** vê. `0x480BE0` tem corpo próprio (chama o syscall `-0x2F` e compara), e o runtime o troca inteiro por `iWakeupThread` HLE
- **O `dis` omite alguns delay slots** (vistos: `0x414948`, `0x414974`, `0x414D10`). Para uma leitura completa, use `cut -c1-170` direto no arquivo de `output/`

## Pontos de atenção

- Os endereços nos stubs (`nome@0x...`) e em todas as análises valem só para este ELF (SLUS_210.87, USA, v1.02). Não são portáveis entre regiões ou versões
- Faltam handlers no runtime (o analisador detectou 288 funções de biblioteca sem handler; o `sm.toml` atual tem 320 entry_points). O ciclo da wiki é: rodar com config mínima, corrigir bloqueios, usar stubs `ret0`/`ret1`/`reta0` só para triagem e mover hacks por jogo para "game overrides"
- Correções por jogo devem ir em game override (ver `PLANO.md`), não no runtime genérico. E o arquivo do override precisa ser compilado no executável (`target_sources(ps2EntryRunner ...)`), porque `ps2_runtime` é `STATIC` e o linker descarta objetos não referenciados
- Se for configurar `cdImage`, subir `kCdPseudoLbnStart` (`Support.h:7`) antes; caso contrário os pseudo-LBNs atuais podem sombrear LBNs reais da ISO
- Melhoria opcional: exportar o mapa de funções do Ghidra (`ps2xRecomp/tools/ghidra/ExportPS2Functions.java`) e apontar `ghidra_output` no TOML, para ter nomes em vez de `sub_XXXXXXXX`. A wiki não explica qual plugin de R5900 usar no Ghidra nem como importar o ELF
- Não está claro o quanto Shaolin Monks depende de VU e GS, onde o runtime é mais fraco. Não dá para estimar o esforço total
- O diretório `out/build` agora está configurado com build type `None`. Se recompilar as ferramentas (`ps2_analyzer`, `ps2_recomp`), elas serão reconstruídas com as flags novas
- Se o `sm.toml` ou o ELF mudarem, é preciso refazer os passos 4 a 7; o build completo do runtime leva horas (já uma edição em um único arquivo do runtime leva poucos minutos)

- **Truncamento do último `[run:tick]`:** o `timeout N script -qfc ...` mata o jogo no meio da escrita, então a última linha do log pode sair cortada. Use `timeout 60` e olhe `tail -3`. O boot às vezes demora mais (no run15 ficou em `pc=0x2130a0` até o tick ~1680)
- **A pasta `patches/` já está desatualizada em parte:** o `runtime_etapas_1_3.diff` foi gerado antes da telemetria do objeto do player em `ps2_runtime.cpp`. Antes de publicar, regenerar com `git -C PS2Recomp diff -- . ':!ps2xRuntime/src/runner' > patches/runtime_atual.diff`, e copiar à parte o `game_overrides_mksm.cpp` (arquivo novo, que o `git diff` não pega)
- **Os comandos de leitura de `output/` rodam da raiz do projeto**, não de dentro de `PS2Recomp/` (a pasta `output/` fica um nível acima). Os logs `run*_raw.log` ficam em `PS2Recomp/`

## Publicação no GitHub e distribuição

### Versão jogável local (não vai pro GitHub)

A versão executável do jogo fica **somente no PC do autor**, pronta para rodar localmente:

| Item local | Onde fica | Vai pro GitHub? |
|---|---|---|
| Executável do runtime com o jogo recompilado | `PS2Recomp/out/build/ps2xRuntime/ps2EntryRunner` | Não |
| ISO e arquivos do disco | `ShaolinMonks/extraido_full/` | Não |
| C++ gerado | `ShaolinMonks/output/` | Não |
| Saves e memory card | `~/mksm-backup/` e pastas do runtime | Não |

Para rodar localmente, o executável precisa ser iniciado com o ELF de dentro da pasta com a ISO extraída (ver passo 8). Um script de lançamento local (por exemplo `run_local.sh`) pode ser criado para facilitar, mas ele deve ficar fora do repositório, ou ser genérico, sem caminhos nem arquivos do jogo.

### O que pode e o que não pode ir pro repositório

O C++ gerado em `output/` é o código do jogo traduzido, portanto é material protegido por direitos autorais, assim como o ELF, a ISO e tudo em `extraido_full/`. Um executável pronto também carrega esse código. Publicar qualquer um deles seria distribuir o jogo e arriscaria a derrubada do repositório por DMCA. Binários compilados e logs também ficam de fora.

| Vai pro repositório | Fica fora (cada pessoa gera o seu) |
|---|---|
| Correções e overrides do runtime (patches) | ISO, ELF, `extraido/`, `extraido_full/` |
| `sm.toml` e configurações | `output/` (C++ gerado) |
| Scripts de build (Linux e Windows) | `PS2Recomp/out/` (binários e build) |
| Documentação e diagnósticos | `*.log`, `build.log`, `run*.log`, `ps2_log.txt` |
| README | Memory card, saves e backups pessoais |

Modelo de uso: quem quiser jogar fornece a própria cópia legítima do jogo, e os scripts rodam o analisador, o recompilador, copiam o C++ para o runtime e compilam localmente, exatamente o fluxo deste README, automatizado.

Antes de publicar, ler a licença do PS2Recomp e respeitar o que ela exige para alterações e redistribuição. O caminho mais limpo é um fork dele, ou um repositório próprio com as alterações como patches (e o PS2Recomp como submódulo).

### Estrutura sugerida do repositório

```
shaolin-monks-recomp/
├── README.md              o que é, como gerar o seu build, sem arquivos do jogo
├── .gitignore             *.iso, extraido*/, output/, out/, *.log, ps2_log.txt, build.log
├── sm.toml                config do recompilador
├── scripts/               extract.sh, recomp.sh, build.sh (e versões .ps1 para Windows)
├── patches/               correções do PS2Recomp (flags, handlers, overrides)
├── docs/                  diagnósticos (por exemplo, o do bind de RPC)
└── PS2Recomp/             submódulo ou fork
```

Conteúdo mínimo do `.gitignore`:

```
*.iso
extraido/
extraido_full/
output/
out/
*.log
ps2_log.txt
```

### Windows e Linux

- O PS2Recomp é testado principalmente com MSVC, então o build no Windows deve ser mais direto
- No Linux (GCC 16) foram necessárias duas correções (SSE4.1 e desligar o LTO, ver passo 6). Num repositório compartilhado elas precisam entrar no `CMakeLists.txt` ou num script, para ninguém repetir a investigação
- Nenhum executável pronto pode ser distribuído, pelo motivo acima: cada pessoa compila o seu a partir da própria cópia do jogo

### Quando montar o repositório

Melhor depois de passar do bloqueio atual: o que for descoberto muda os patches e a documentação. Pendências para essa etapa: `.gitignore`, scripts de Linux e Windows, esqueleto do repositório, decisão entre fork e submódulo, e checagem da licença do PS2Recomp.

## Observações legais

O projeto usa só a cópia legítima do jogo que o usuário já possui. Não baixar nem pedir ajuda para obter ROMs de sites de distribuição, e não extrair arquivos do jogo de sites que os distribuíam sem autorização. Não publicar a ISO, o ELF, o C++ gerado nem binários compilados (ver "Publicação no GitHub e distribuição"). O jogo pronto para executar existe apenas localmente, no PC do autor.

## Avaliação de chances (palpite, não medida)

Não existe número medido. Isto é uma estimativa subjetiva, baseada no que já foi visto, e deve ser reavaliada a cada bloqueio resolvido:

| Meta | Chance estimada | Por quê |
|---|---|---|
| Chegar ao menu principal | 35 a 50% | O jogo já passa do boot, do CD e da apresentação de frame, mas ainda falta passar do loop do objeto em `*(0x4FADEC)`, e cada bloqueio resolvido costuma revelar outro |
| Ficar jogável (fases, ~30 fps estáveis) | 5 a 15% | O jogo ainda não desenhou nada de verdade, então o GS e os VUs (onde o README do PS2Recomp admite desempenho ruim) não foram testados. Numa i7-7700HQ com Intel HD 630 é o maior risco |
| Jogo inteiro sem bugs graves | abaixo de 5% | |

Observações:

- "Sem emulador" não é bem o caso: o runtime ainda emula GS, VU, DMA e IOP. O que muda é que o código da CPU vira C++ em vez de ser interpretado
- **Dá para avançar só rodando e colando saídas** nos bloqueios de boot (CD, tela magenta, loader, vídeo, áudio, controle, carregamento dos WADs): não exige entender MIPS. Isso ainda deve aparecer várias vezes
- **Não resolve com patch às cegas** um jogo que desenha e fica lento: aí o problema é o desempenho do GS e dos VUs no runtime, que é arquitetura e exigiria reescrever partes do renderizador ou contribuir upstream. É trabalho de outra ordem, e não há como prever se terminaria bem
- Mesmo sem ficar jogável, o trabalho já serve como estudo e documentação

## Alternativas se o PS2Recomp não render

- Jogar com o PCSX2 usando a ISO de PS2
- Contribuir com o OpenShao (precisa de cópia de Xbox, USA ou Europa, que o usuário não confirmou ter)

## Referências

- PS2Recomp: https://github.com/ran-j/PS2Recomp (wiki: Ps2xAnalyzer, Ps2xRecomp, Ps2xRuntime, Game Override Hooks, PS2 ELF Files, Stripped Game Walkthrough)
- OpenShao: https://github.com/OAleex/OpenShao
