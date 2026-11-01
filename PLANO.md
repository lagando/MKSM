# Plano para destravar o boot do Shaolin Monks

Estado em 01/11/2026. Documento de trabalho complementar ao `README.md`. O objetivo inicial era sair do laço em `pc=0x385D80` (resolvido na Etapa 1) e corrigir a apresentação de frame/tela magenta (resolvido na Etapa 3). O foco atual é destravar o loop de loader/inicialização em `caller=0x211BE0` para avançar aos dados e menus.

> **Status (01/11/2026): Etapa 1 e Etapa 3 (Apresentação de Frame) concluídas e verificadas.**
> 1. **Etapa 1 (OK):** O jogo saiu de `0x385D80`, os 10 binds do libcdvd funcionaram, 16 `sceCdSearchFile` foram resolvidos e não houve `unresolved LBN`.
> 2. **Etapa 3 - Apresentação (OK):** A tela magenta (fallback do runtime) foi **completamente eliminada**. Corrigidos os registradores privilegiados PMODE/SMODE2 e o layout do struct `sceGsDispEnv` (`GsDispEnvMem`). Os frames do GS agora são latcheados e enviados continuamente ao raylib (`[frame:upload]` a 640x224/640x448). A janela apresenta a saída do GS (tela preta inicial).
> 3. **Bloqueio atual (investigação em andamento):** o jogo está num loop de espera assíncrono em `sub_00211B38` (`caller=0x211BE0`, `0x4EF7E0 == 2`), aguardando `func_370A98` retornar diferente de zero enquanto dispara `sceGsSyncV` a 60 fps. Medido em execução: o objeto em `*(0x4FADEC)` existe (`0x54fb60`), mas fica em **status 2 com 0 frames** (`cnt=0`, `total=29`) por milhares de ticks. O padrão do código lembra um player de vídeo (Sofdec/MPEG), hipótese ainda não confirmada. Atualização da mesma data: a rodada de leitura da seção 4.7 deixou essa hipótese menos firme (o bloco `sceMpeg*` do jogo fica em `0x446xxx`–`0x44Axxx`, sem ligação direta com o objeto em `0x4FADEC`) e mostrou que o rastreador de funções está ligado (`ps2_log.txt` recriado, ~265 MB por execução). O experimento `run16` da 4.6 foi **executado**: o servidor `0x4153A0` roda em todo VSync com `srvN=1` e o objeto continua em status 2 com `cnt=0`; o bloco `sceMpeg*` nunca é chamado e o servidor chama `iReferThreadStatus` 6424 linhas no rastro (~3212 chamadas); se ele chega a chamar `0x480BE0` (`iWakeupThread`) não se sabe, porque esse arquivo não usa o rastreador (seções 4.7.9 e 4.7.11). Ver seções 4.5, 4.6 e 4.7.

Todos os caminhos são relativos à raiz do projeto:

```
/run/media/valk/linux_arquivos/projetos/ShaolinMonks/
```

---

## 1. Diagnóstico fechado

O travamento **não é crash, função faltando, corrupção de memória ou lentidão**. É o `libcdvd` do jogo tentando se conectar a um servidor RPC que o runtime nunca publica.

### Cadeia de evidências

1. `0x00385CE0` é `sceCdLayerSearchFile` do libcdvd 3.0 (está em `sm.toml:298`).
2. Em `0x385D54` ela chama `sceSifInitRpc` (`0x482E98`).
3. Em `0x385DAC` chama `sceSifBindRpc(&client, 0x80000597, 0)` com `client = 0x758640`.
4. `SifBindRpc` (`PS2Recomp/ps2xRuntime/src/lib/Kernel/Syscalls/RPC.cpp:254`) **sempre retorna 0**, e só preenche `client->server` se `canBindRpc(sid)` for verdadeiro.
5. `canBindRpc` (`PS2Recomp/ps2xIOP/src/iop_subsystem.cpp:188`) aceita apenas:
   - rotas HLE: MCSERV `0x80000400`/`0x80000480`, LIBSD `0x80000701`, DBCMAN `0x80001300`;
   - servidores registrados por um IRX rodando no emulador de IOP.
6. O `CDVDSTM.IRX` carrega (`run3_raw.log`, `id=7`), mas ele é **cliente** do `cdvdman` (as strings do IRX mostram `cdvdman`). Quem deveria registrar `0x80000597` é o `cdvdman`, um módulo de ROM do IOP que o runtime reconhece como nome (`ps2xIOP/src/iop_module_manager.cpp`) mas **não implementa como serviço RPC**.
7. Logo `client->server` continua `0`.

### O laço, instrução por instrução

Confirmado em `output/sub_00385CE0_0x385ce0.cpp`:

| Endereço | Instrução | Efeito |
|---|---|---|
| `0x385DB4` | `bgezl $v0` | se o retorno do bind for >= 0, pula para o delay slot |
| `0x385DB8` | `lw $v0, 0x24($s0)` | lê `client->server` |
| `0x385E04` | `beqz $v0` | se `server == 0`, volta para `0x385D78` |
| `0x385DE0..0x385DF4` | laço de atraso | `v0 = 0x100000` até `v0 == -1` |
| `0x385DFC` | `b 0x385DA0` | tenta o bind de novo |

Ou seja, o padrão clássico do SDK:

```c
do {
    sceSifBindRpc(&client, 0x80000597, 0);
    /* espera */
} while (client.server == 0);
```

Como o bind "dá certo" (retorna 0) e não registra nada, o jogo gira em silêncio. O caminho de erro `0x385DBC` (que imprimiria `Libcdvd bind err ...`) nunca é tomado, porque ele só acontece com retorno negativo.

### Por que todas as APIs do CD vão travar

O binário tem as strings `Libcdvd bind err CdSearchFile`, `Libcdvd bind err N CMD`, `Libcdvd bind err S cmd`, `Libcdvd bind err CdDiskReady`. O bind é feito por um helper comum do libcdvd, então **não adianta corrigir só o search**: `sceCdInit`, `sceCdDiskReady`, `sceCdRead`, `sceCdSync`, etc. vão bater no mesmo problema. Por isso o plano substitui o conjunto inteiro de APIs públicas do libcdvd pelas HLE equivalentes de uma vez.

### O que já está pronto e não precisa ser refeito

- Os 13.649 `.cpp` gerados estão em `PS2Recomp/ps2xRuntime/src/runner/`.
- Os headers `output/*.h` e `PS2Recomp/ps2xRuntime/include/ps2_recompiled_*.h` têm **MD5 idêntico**.
- O binário `ps2EntryRunner` (96,9 MB) já está compilado com esse código.
- Portanto: **não rodar `ps2_recomp` de novo**. Qualquer correção de runtime é build incremental de minutos.

---

## 2. Etapa 1 — destravar o bind do libcdvd (recomendada)

Usar o mecanismo oficial de **Game Override Hooks** (`PS2_REGISTER_GAME_OVERRIDE` + `bindAddressHandler`) para trocar as funções guest do libcdvd pelas HLE que já existem em `ps2xRuntime/src/lib/Kernel/Stubs/CD.cpp`.

### 2.1 Criar o arquivo de override

Criar `PS2Recomp/ps2xRuntime/src/lib/game_overrides_mksm.cpp`:

```cpp
#include "game_overrides.h"
#include "ps2_runtime.h"

#include <cstdint>
#include <iostream>

namespace
{
    void applyMksmOverrides(PS2Runtime &runtime)
    {
        struct Binding
        {
            uint32_t address;
            const char *handler;
        };

        // libcdvd 3.0 (guest) -> implementacoes HLE do runtime.
        // Todos os nomes abaixo existem em PS2_STUB_LIST e sao resolvidos
        // por ps2_game_overrides::bindAddressHandler.
        constexpr Binding bindings[] = {
            {0x00385CE0u, "sceCdSearchFile"},   // sceCdLayerSearchFile
            {0x00386158u, "sceCdNcmdDiskReady"},
            {0x003861F0u, "sceCdSync"},
            {0x00386290u, "sceCdSyncS"},
            {0x00386458u, "sceCdInit"},
            {0x00386740u, "sceCdMmode"},
            {0x00386818u, "sceCdReadClock"},
            {0x00467940u, "sceCdRead"},
            {0x00467B28u, "sceCdGetError"},
            {0x00467BC0u, "sceCdBreak"},
        };

        for (const Binding &b : bindings)
        {
            if (!ps2_game_overrides::bindAddressHandler(runtime, b.address, b.handler))
            {
                std::cerr << "[mksm] failed to bind " << b.handler
                          << " @0x" << std::hex << b.address << std::dec << "\n";
            }
        }
    }
}

PS2_REGISTER_GAME_OVERRIDE("mksm-cdvd", "SLUS_210.87", 0x0011C070u, 0u, applyMksmOverrides);
```

### 2.2 Por que esses endereços e esses nomes

- `0x385CE0`: `sceCdLayerSearchFile(file, name, layer)`. A HLE `sceCdSearchFile(file, name)` tem a mesma assinatura útil; `$a2` (layer) é ignorado, o que é correto para layer 0.
- `0x385FE0` (wrapper guest `sceCdSearchFile`) **não precisa** ser substituído: ele apenas seta `$a2 = 0` e faz tail call para `0x385CE0`, que agora é a HLE.
- `0x41F048` (wrapper do DVCI) chama `0x385CE0` diretamente com `$a0 = CdlFILE*` e `$a1 = nome`. A HLE escreve os 32 bytes de `CdlFILE` exatamente em `$a0`, o mesmo layout que o guest copiava de `0x385EF8..0x385F34`. Confere com `writeCdSearchResult` (`Support.h:520`).
- `entry=0x11C070` e `elfName="SLUS_210.87"` batem com o `readelf`. `crc32=0` desliga a checagem de CRC.
- `bindAddressHandler` usa `replaceFunction`, que aceita qualquer endereço dentro da tabela densa `[0x100000, 0x49A6F4)` — 944.573 slots. Todos os endereços acima estão dentro.
- O callback roda no fim de `loadELF` (`ps2_runtime.cpp:1006`), **depois** de `configureIoPathsFromElf` e do `reset()` do IOP. É um ponto seguro para mexer em funções e caminhos.

### 2.3 Linking (ponto que costuma dar errado)

`ps2_runtime` é uma biblioteca `STATIC` e **não há `WHOLE_ARCHIVE`** no link. Um objeto que só contém um inicializador estático e não é referenciado por ninguém é descartado pelo linker, e o override nunca roda.

Solução: compilar o arquivo **direto no executável**. Em `PS2Recomp/ps2xRuntime/CMakeLists.txt`, logo depois do bloco `if(PS2X_IS_ANDROID) ... else() ... endif()` que cria o alvo (perto das linhas 450-466), adicionar:

```cmake
target_sources(ps2EntryRunner PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/lib/game_overrides_mksm.cpp"
)
```

Alternativa (menos limpa): manter no `ps2_runtime` e forçar o símbolo com `-Wl,--whole-archive`/`--undefined`, mas isso não é necessário.

### 2.4 Build e execução

```bash
cd PS2Recomp

cmake --build out/build -j2 --target ps2EntryRunner

./out/build/ps2xRuntime/ps2EntryRunner ../extraido_full/SLUS_210.87 \
  2>&1 | grep -v "run:tick" | tee run4.log
```

Se o log em pipe perder o final (buffer de 4 KB), usar o mesmo truque do README:

```bash
timeout 90 script -qfc "./out/build/ps2xRuntime/ps2EntryRunner ../extraido_full/SLUS_210.87" run4_raw.log
grep -av "run:tick" run4_raw.log > run4.log
```

### 2.5 Resultado esperado

```
[game_overrides] applying 'mksm-cdvd'
[game_overrides] applied 1 matching override(s).
[sceCdSearchFile] pc=0x... ra=0x... file=0x... path="..."
[sceCdSearchFile:ok] path="..." lsn=0x100000 size=0x... sectors=0x...
```

E o `[run:tick]` deve sair de `pc=0x385d80`.

O `lsn=0x100000` é o pseudo-LBN do HLE (`kCdPseudoLbnStart`), não um LBN real da ISO. Isso é normal nesta etapa: o HLE registra o arquivo do host em `g_cdFilesByKey` e o `sceCdRead` HLE resolve por esse mapa.

### 2.6 Se algum bind falhar

O log vai mostrar:

```
[game_overrides] unresolved handler 'nome' for address 0x...
[mksm] failed to bind nome @0x...
```

Aí o nome não existe em `PS2_STUB_LIST` ou o endereço está fora da tabela densa. Conferir com:

```bash
grep -n "X(sceCd" PS2Recomp/ps2xRuntime/include/ps2_call_list.h
grep -n "0x385CE0\|0x386158\|0x3861F0" sm.toml
```

Se necessário, remover só aquele binding e rodar de novo. O resto continua valendo.

---

## 3. Etapa 2 — leituras de CD reais (condicional)

Só executar se aparecer no log:

```
sceCdRead unresolved LBN 0x... (no mapped file and no configured CD image)
```

Isso significa que o jogo está lendo setores crus por LBN (ISO9660/DVCI, filmes `.SFD`, streaming) que não passaram por um `sceCdSearchFile` HLE.

### 3.1 Apontar o `cdImage`

O runner **nunca** configura `cdImage`: `configureIoPathsFromElf` (`ps2_runtime.cpp:1052`) só preenche `elfPath`, `elfDirectory`, `hostRoot`, `cdRoot` e `mcRoot`. Sem isso, `sceCdRead` só resolve pseudo-LBNs.

No início de `applyMksmOverrides`, adicionar:

```cpp
PS2Runtime::IoPaths paths = PS2Runtime::getIoPaths();
if (paths.cdImage.empty() && !paths.elfDirectory.empty())
{
    paths.cdImage = paths.elfDirectory.parent_path()
                  / "Mortal Kombat - Shaolin Monks (USA).iso";
    PS2Runtime::setIoPaths(paths);
}
```

O callback roda depois de `configureIoPathsFromElf`, então `elfDirectory` já é `.../extraido_full` e `parent_path()` é a raiz do projeto. `setIoPaths` normaliza o caminho para absoluto.

### 3.2 Corrigir a colisão de pseudo-LBN (obrigatório se usar `cdImage`)

`PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/Helpers/Support.h:7`:

```cpp
constexpr uint32_t kCdPseudoLbnStart = 0x00100000;
```

A ISO tem 3.495.460.864 bytes = **`0x1A0B10` setores**. Como `readCdSectors` (`Support.h:429`) consulta `g_cdFilesByKey` **antes** da imagem, qualquer LBN real em `[0x100000, 0x1A0B10]` seria sombreado por um arquivo pseudo, devolvendo dados errados sem erro.

Correção mínima: subir a constante para acima de qualquer DVD9 (máximo ~`0x3F8000` setores):

```cpp
constexpr uint32_t kCdPseudoLbnStart = 0x00800000;
```

Alternativa mais "correta": em `registerCdFile` (`Support.h:325`), usar `max(0x100000, totalSectors + 1)`, mas isso depende de a imagem já estar configurada no momento do registro. A constante resolve sem essa ordem.

### 3.3 Observação sobre o `FILELIST.DIR`

O `extraido_full/FILELIST.DIR` tem apenas 286 bytes e é uma **lista de nomes em texto puro** (`GAMEDATA.WAD`, `FRONT\MOVIES\...`), não um banco DVCI binário. Quem interpreta esse arquivo é o código do próprio jogo (DVCI). O HLE não precisa parsear nada: ele só resolve os `sceCdSearchFile` que o jogo fizer.

---

## 4. Etapa 3 — Apresentação do Frame e Diagnóstico do Loop de Loader (Novo Foco)

A Etapa 1 resolveu o CD (`pc=0x385D80`) e fez o VSync disparar. Em seguida, a causa da tela magenta foi diagnosticada e corrigida com sucesso no runtime, permitindo que os frames sejam latcheados e apresentados. O bloqueio atual agora é o **loop de loader/inicialização** em `caller=0x211BE0`.

### 4.1 Fato observado e resolução da tela magenta

Antes, a cada tick de VSync, `UploadFrame` (`ps2_runtime.cpp:376`) tentava latchear e copiar o frame (`copyLatchedHostPresentationFrame`), mas falhava porque o CRT estava desabilitado (`PMODE == 0`). O runtime então caía no fallback `GenImageColor(..., MAGENTA)`.

A causa raiz foi identificada e corrigida:
1. **Registradores Privilegiados do GS:** `PMODE` (`0x12000000`) e `SMODE2` (`0x12000020`) são registradores do barramento privilegiado do GS. Eles **não podem** ser escritos via pacote GIF A+D (cujos IDs de registradores só vão de 0x00 a 0x5F). Tentar enviá-los via GIF A+D em `sceGsResetGraph` era ignorado. A correção aplicou `pmode` e `smode2` diretamente na memória de registradores do GS (`runtime->memory().gs()`).
2. **Layout incorreto de `sceGsDispEnv` em `Support.h`:** A struct `GsDispEnvMem` continha `pmode` e `smode2` nas primeiras duas qwords. Na especificação oficial da Sony (SDK `libgraph`), `sceGsDispEnv` possui 40 bytes (5 qwords):
   - `+0x00 dispfb1` (0x12000070)
   - `+0x08 display1` (0x12000080)
   - `+0x10 dispfb2` (0x12000090)
   - `+0x18 display2` (0x120000a0)
   - `+0x20 bgcolor`  (0x120000e0)
   Ao chamar `sceGsPutDispEnv`, o runtime antigo sobrescrevia `pmode` e `smode2` com os dados de `dispfb1`/`display1` do jogo (zerando o CRT).
3. **Resultado:** Com `PMODE=0x8005` e `SMODE2=0x3` preservados e o layout corrigido, o CRT1/CRT2 permaneceram ativos. O `copyLatchedHostPresentationFrame` passou a ter sucesso absoluto (`[frame:upload] size=640x224 preferred=0`). A **tela magenta foi 100% eliminada** e substituída pela imagem real do GS (tela preta inicial do jogo).

---

### 4.2 Novo bloqueio: Loop de Loader em `caller=0x211BE0`

Com a apresentação funcionando, o jogo continua rodando a ~60 fps estáveis sem travar nem dar crash (alcançou mais de `tick=9480` / ~160 segundos de execução contínua), porém não avança para o menu ou vídeo.

#### Dados da telemetria nos ticks (`run12_raw.log` / `run13_raw.log`)

```
[run:tick] tick=9480 pc=0x24cdd4 ra=0x24cdd4 sp=0x1fffdd0 gp=0x5b9ff0
           dispfb1=0x1400 display1=0xdf27f00000000
           dispfb2=0x1446 display2=0xdf27f00000000
           pmode=0x8005 smode2=0x3 caller=0x211be0
           loopState=2 loopCursor=0x8ade80
           activeThreads=1 dma=7 gif=3 gsw=0 vif=4
```

- O PC oscila entre `0x24CDD4` (retorno do wrapper de VSync `sub_0024CDC0`), o callback de interrupção de VSync `0x26EFE8`, e o endereço que chamou o wrapper: **`caller = 0x211BE0`**.
- O jogo chamou `sceCdSearchFile` para 16 arquivos (localizando LBNs para `FILELIST.DIR`, `WADCRC.BIN`, todos os `GAMEDATA.WA*` e todos os `FRONT\MOVIES\*.SFD`) e fez **um único** `sceCdRead` (`FILELIST.DIR`, `lbn=0x100000`, 2 setores). Depois disso não houve mais leituras. (Correção de 01/11/2026: este item dizia que nenhum `sceCdRead` tinha sido feito, e o exemplo de telemetria acima mostrava `loopState=0 loopCursor=0x0`; os valores medidos no loop são `loopState=2` e `loopCursor=0x8ade80`.)

#### Análise reversa da função do laço (`output/sub_00211B38_0x211b38.cpp`)

A função `sub_00211B38` (faixa `0x211B38 - 0x211DE0`) é uma rotina de controle assíncrono / loader orientada a estado:

1. **Estado em `0x4EF7E0`:**
   - Em `0x211B88`, chama `sub_00211878` para inicializar a requisição.
   - Seta `0x4EF7E0 = 1`.
   - Se `0x4EF7E0 == 1`, executa `func_370A70` e transiciona `0x4EF7E0 = 2`.
2. **Laço de polling no Estado 2:**
   - Quando `0x4EF7E0 == 2`, executa `label_211c5c`:
     - Chama `func_370A98` (que chama `func_26F6E0` passando o objeto em `0x4FADEC`).
     - `func_26F6E0` invoca `func_413BB0`.
     - `func_413BB0` chama `func_413F78`, que faz um salto (`j`) para `func_42C7C0` (`jr $ra` vazio).
     - Se `func_370A98` retornar `0`, a condição de conclusão não foi satisfeita.
   - Não estando pronto, o código salta para `label_211c84` -> `label_211bc8`:
     - Chama `func_3199E0`.
     - Chama `func_24CDC0` (`sceGsSyncV(0)`, que espera o próximo VSync).
     - Retorna em `0x211BE0` e repete a checagem no próximo frame.
3. **Condição de saída:**
   - Quando `func_370A98` retornar != 0, o loop chama `func_370AE8`, zera a variável de estado (`sw $zero, 0x4EF7E0`) e segue o fluxo para iniciar o processamento dos buffers e as leituras de disco.

---

### 4.3 Ordem de investigação para destravar o loader

> Atualização (01/11/2026): os itens 1 e 4 foram parcialmente respondidos na seção 4.5. `func_42C7C0` **não** é um stub preenchido dinamicamente: é um `jr $ra` vazio que serve de lock/unlock, e é assim no próprio ELF.

1. **Identificar o objeto em `0x4FADEC` e a biblioteca em `0x413xxx`:**
   - As funções na faixa `0x413000` a `0x420000` pertencem à camada de I/O de arquivos / streaming (CRI ADX / DVCI / middleware de arquivo).
   - Verificar se `func_42C7C0` é um stub que deveria ter sido preenchido dinamicamente por um ponteiro de função/driver, ou se o status em `0x4FADEC` depende de uma notificação assíncrona (como interrupção de IOP SIF ou callback).
2. **Verificar os serviços de IOP e SIF RPC:**
   - Confirmar se o driver de streaming (`CRI_ADXI.IRX` ou `CDVDSTM.IRX`) enviou algum RPC ou se aguarda uma resposta do IOP que não foi entregue.
   - Inspecionar a aba *RPC History* no painel do runtime durante o loop.
3. **Teste de simulação / bypass em Game Override:**
   - Implementar um override experimental ou stub para `func_370A98` (`0x370A98`) ou na checagem de status de `sub_0026F6E0`, forçando o retorno de conclusão quando solicitado para observar se o jogo inicia as chamadas de `sceCdRead` dos WADs ou dos vídeos SFD.
4. **Player de Vídeo (MPEG / SFD):**
   - O primeiro arquivo na lista de busca foi `FRONT\MOVIES\MIDWAY.SFD;1` e `OPENING.SFD;1`. Verificar se o jogo está tentando iniciar o player de vídeo IPU/MPEG antes de qualquer carregamento de fase.

---

### 4.4 Comandos de diagnóstico para o laço

```bash
cd /run/media/valk/linux_arquivos/projetos/ShaolinMonks   # raiz do projeto: output/ fica aqui, não dentro de PS2Recomp/

# Inspecionar as rotinas do loop de espera
grep -n "0x211be0\|0x211c5c\|0x211c84" output/sub_00211B38_0x211b38.cpp
grep -n "func_413BB0\|0x4fadec" output/sub_0026F6E0_0x26f6e0.cpp

# Inspecionar os stubs em 0x413xxx
ls output/sub_00413B*.cpp output/sub_00413F*.cpp

# Verificar se há menção a RPCs pendentes no log raw
grep -aiE "rpc|sif|iop|unhandled" PS2Recomp/run13_raw.log | grep -v "run:tick" | head -40
```

---

### 4.5 Resultado da investigação do objeto em `0x4FADEC` (01/11/2026)

Telemetria acrescentada ao `[run:tick]` (ver 11.4) e funções lidas em `output/*.cpp` (listagem de instruções, sem confirmação em execução, a não ser onde dito).

#### 4.5.1 Medido em execução (`run14_raw.log`, `run15_raw.log`)

```
obj=0x54fb60 objw0=0x201 status=2 cnt=0 total=29 pct=0 cb=0x0
```

- Idêntico do tick 600 ao 3480. O objeto existe, está ativo (`objw0=0x201`: byte 0 = 1, byte 1 = status = 2) e `cnt` (frames/unidades processadas) nunca sai de 0
- `cb=0x0`: o gancho opcional em `0x4FADF0`, que o callback de VSync chamaria, está vazio
- `flag4ba0` e `vs` ainda não foram lidos (a última linha do `run15` saiu truncada pelo `timeout`)
- Do disco: **um** `sceCdRead` (`FILELIST.DIR`) e 16 `sceCdSearchFile`; nenhuma leitura de WAD ou de `.SFD`

#### 4.5.2 Mapa das funções lidas

| Endereço | Papel (pela leitura do código) |
|---|---|
| `sub_00211B38` | loop do loader, estado em `0x4EF7E0`; no estado 2 chama `func_370A98` a cada VSync |
| `0x370A98` | chama `0x26F6E0` |
| `0x26F6E0` | consulta de status: `-1` se status for 4 ou negativo, `1` se for 3, `0` nos demais; grava `0x678444 = obj[0x20]` e `0x678448 = 100*obj[0x20]/[0x678440]` |
| `0x413BB0` | lock (`0x413F78`) + `0x413BE8` + unlock (`0x413F90`) |
| `0x413BE8` | getter do status (byte `obj+1`); com objeto nulo chama `0x42D298` (handler de erro) |
| `0x413B48` / `0x413B80` | getter do contador `obj+0x20`, também com lock/unlock |
| `0x413F78`, `0x413F90` | só saltam para `0x42C7C0` / `0x42C7C8` (`jr $ra` puro): lock/unlock vazios do ELF, **não** são stubs do runtime |
| `0x26F570` | cria o objeto: destrói o anterior (`0x26F790`, zera `0x678448`), chama `0x412CE8` e grava em `*(0x4FADEC)` |
| `0x3709F8` | wrapper de `0x26F570` |
| `0x412CE8` / `0x412D30` | lock + construtor + unlock; o construtor aloca (`0x412BA0`), inicializa (`0x412C38`) e destrói (`0x413128`) se a inicialização falhar |
| `0x26EFE8` | callback de VSync: `0x485FB8` (`di` com espera no Status), contadores `0x67B79C` e `0x678450`, gancho `[0x4FADF0]` se não nulo, `0x4154C8(0)` se `[0x4F4BA0] == 0`, `ei`, retorna 1 |
| `0x4154C8` | só salta para `0x4153A0` |
| `0x4153A0` | servidor periódico (hipótese): contador `0x533490`, trava `0x53423C`, executa o corpo só se `[0x534124] > 0`, chama `0x414938` e `0x414D08`, conta execuções em `0x5341B0`, callback de usuário em `[0x534240]` |

#### 4.5.3 Hipóteses (nenhuma confirmada)

1. **Player de vídeo (Sofdec/MPEG) "tocando" sem decodificar.** Status 0 a 4 como parado, preparando, tocando, fim e erro é a convenção que me lembro do Sofdec/`mwply` (**de memória, a verificar**) e bate com o que `0x26F6E0` testa. Arquivo provável: `MIDWAY.SFD`, o primeiro da lista. A favor: o ELF traz o `libmpeg` completo; `sceMpegCreate`, `sceMpegInit`, `sceMpegGetPicture`, `sceMpegGetPictureRAW8`, `sceMpegReset`, `sceMpegAddCallback`, `sceMpegClearRefBuff`, `_sceMpegFlush` e `sceIpuStopDMA`/`RestartDMA`/`Sync`/`Init` estão em `stubs` no `sm.toml` (linhas 118 a 129), logo rodam como HLE do runtime (`Stubs/MPEG.cpp`, `Stubs/IPU.cpp`); os `_sceMpeg*` internos (linhas 336 a 390) estão em `entry_points` (código do jogo recompilado)
   - Atualização (seção 4.7.4): os chamadores diretos de `sceMpegInit`/`Create`/`GetPicture` ficam todos em `0x446xxx`–`0x44Axxx`, longe do loader e do objeto; a hipótese perde apoio
2. **O servidor `0x4153A0` não roda** (`[0x4F4BA0] != 0`) **ou roda sem player registrado** (`[0x534124] == 0`) — **descartada pelo `run16`** (seção 4.7.7): roda em todo VSync e `srvN=1`
3. **Faltam dados ou decodificação:** o MPEG/IPU HLE não gera frames, ou o IOP (CRI/`cdvdman`) não entrega os dados. O emulador do IOP já trata os imports da biblioteca `cdvdman` (`iop_cdvd.cpp`); o que não existe é o servidor RPC `0x80000597` do EE, contornado pelo override da Etapa 1. Nenhum `sceCdSt*` foi detectado no `sm.toml`, então o streaming pode passar pelos IRX da CRI

Descartadas: objeto nulo; `0x42C7C0` ser stub do runtime; "nenhum `sceCdRead`".

### 4.6 Próximo experimento e árvore de decisão

**Experimento (executado em 01/11/2026; resultado na seção 4.7.7):** acrescentar ao `[run:tick]` cinco valores: `srvCalls` (`0x533490`), `srvN` (`0x534124`), `srvRuns` (`0x5341B0`), `guard` (`0x53423C`) e `usercb` (`0x534240`). Rodar com `timeout 60` e ler `tail -3` (para obter também `flag4ba0` e `vs`). Rodar também `grep -ai "mpeg\|ipu" run16_raw.log` para ver se o runtime registra alguma chamada de MPEG/IPU.

| Resultado | Interpretação | Próximo passo |
|---|---|---|
| `flag4ba0 != 0` ou `srvCalls` parado | o servidor nunca roda | descobrir quem liga `[0x4F4BA0]` |
| `srvCalls` cresce e `srvN == 0` | o servidor roda, mas o player não está registrado | ler `0x414938` e quem incrementa `[0x534124]` |
| `srvN > 0`, `srvRuns` cresce e `cnt == 0` | o servidor roda e falta decodificação ou dados | ler `0x414938` e `0x414D08`; conferir `Stubs/MPEG.cpp` e `Stubs/IPU.cpp` e as abas File/CD, RPC History e Threads do depurador |
| `status` chega a 3 ou 4 | o objeto avança | investigar a lógica de saída em `sub_00211B38` |

Resultado do `run16` (seção 4.7.7): `flag4ba0=0`, `srvCalls == srvRuns == vs`, `srvN=1`, `guard=0`, `usercb=0x0`, `cnt=0`. Cai na terceira linha da tabela, com a ressalva de que a decodificação MPEG não é a suspeita principal (o bloco `sceMpeg*` nunca é chamado); a pista agora é o que `0x414938` faz com os handles dos slots e a existência de uma thread de trabalho.

O bypass de `func_370A98` num game override (seção 4.3, item 3) continua válido só como **triagem**, para ver se o jogo passa a chamar `sceCdRead` dos WADs. Ele pode esconder o problema real e apenas empurrar o travamento para a frente.

### 4.7 Rodada de leitura: rastreador, chamadores do bloco MPEG e `dis` (01/11/2026)

Resultados de comandos rodados depois da seção 4.6. As seções 4.7.1 a 4.7.4 vieram antes do `run16`; o resultado do `run16` está na 4.7.7. O que está medido é dito como medido; o resto é leitura de código.

#### 4.7.1 O rastreador de funções está ligado

- O `ls -l` mostrou dois arquivos: `PS2Recomp/ps2_log.txt` (265M, 15:24) e `/tmp/ps2_log.txt` (267M, 12:16, o antigo movido na Etapa 5). Ou seja, **um novo `ps2_log.txt` foi recriado em `PS2Recomp/`**, provavelmente por uma execução recente: o binário atual grava o rastro de chamadas (é o `PS_LOG_ENTRY` que aparece nos `.cpp` gerados; o nome da opção de CMake não foi conferido). A documentação falava em 242 MB para o antigo e o `ls` mostra 267M; a diferença não foi investigada.
- Formato (primeiros 600 bytes): `>> sub_XXXXXXXX_0xXXXXXXXX enter` e `<< ... exit`, uma linha por entrada e saída, com indentação pela profundidade. Começa em `sub_0011C070` (entry do ELF) e desce por `sub_00486228`, `sub_00486020`, `sub_0047FE60`.
- O `grep -c "sub_0044C808\|sub_0044C7B8\|sub_0044CF60"` rodou no mesmo comando, mas **a contagem não saiu legível** (o `head -c 600` corta no meio de uma linha e o número fica colado nela). Não se sabe se o bloco `0x44C` roda. Repetir o `grep -c` sozinho (4.7.6).
- Custo: ~265 MB por execução, quase tudo do loop de VSync. Apagar o de `/tmp` quando não precisar. Adicionar `ps2_log.txt` ao `.gitignore` (o padrão `*.log` não pega `.txt`).

#### 4.7.2 Quem chama o bloco `sceMpeg*` do jogo

Busca do literal `0x<endereço>u` em `output/sub_*.cpp` (acha só `jal` direto; chamada indireta não aparece). Nomes pelo `sm.toml`:

| Alvo | Função | Arquivos que contêm o literal |
|---|---|---|
| `0x44C7B8` | `sceMpegInit` | `sub_0044C7A8`, `sub_00446878` |
| `0x44C808` | `sceMpegCreate` | `sub_0044A8D0` |
| `0x44CF60` | `sceMpegGetPicture` | `sub_00449BA0` |

- `sub_0044C7A8` fica colada no alvo (`0x44C7A8` contra `0x44C7B8`); provavelmente só encadeia (queda direta ou tail call) e não é um chamador independente. Não confirmado.
- Todos os chamadores estão em `0x446xxx` a `0x44Axxx`. Nenhum está em `0x211xxx`, `0x26Fxxx`, `0x370xxx`, `0x412xxx`, `0x413xxx` ou `0x415xxx`, onde ficam o loader e o objeto de `*(0x4FADEC)`.
- Leitura: o jogo parece ter um módulo de FMV próprio usando o `libmpeg` da Sony direto. Nada liga esse módulo ao objeto que trava. Isso **não prova** que são independentes (podem se ligar por chamada indireta).

#### 4.7.3 Funções lidas com `dis`

Atenção: o `dis` não mostra alguns delay slots (por exemplo `0x414948`, `0x414974`, `0x414D10`), então as leituras abaixo são parciais. Para ver tudo: `cut -c1-170 output/sub_00414D08_0x414d08.cpp`.

- **`0x414938`** (`sub_00414938`, chamada pelo servidor `0x4153A0`): recebe `$a0` em `$s0`; se for zero, pula tudo. Senão chama `func_47FD70` passando `$sp` como ponteiro de saída, lê `[sp]`, compara com `4` e com `$a1`, e dependendo disso chama `func_480BE0`. Parece uma consulta de status de um handle seguida de uma ação. O significado de `47FD70` e `480BE0` não foi lido.
- **`0x414D08`** (`sub_00414D08`): três linhas visíveis, `lui $v1, 0x53`, `jr $ra`, `nop`. Parece um getter minúsculo de uma variável global em `0x53xxxx` (a região do servidor: `0x533490`, `0x534124`, `0x5341B0`). O valor devolvido depende do delay slot em `0x414D10`, que o `dis` não mostrou.
- **`0x412C38`** (inicializador chamado por `0x412D30`; recebe `$a0 = obj` e `$a1 = parâmetro`): se `$a1 == 0`, chama `func_42D298` (handler de erro) com a string em `0x5A4438`. Senão grava `obj+0x34`, `obj+0x38`, zera `obj+0x14` e `obj+0x3C`, e chama sobre o sub-objeto `[obj+4]` as funções `0x417538`, `0x4176D0` e, conforme o retorno de `0x4176D0` ser `4` ou não, `0x417650` ou `0x418300`, e por fim `0x418340`, cujo retorno vai para `obj+0x10`/`+0x40`/`+0x0C`. O trecho listado **não grava o byte de status em `obj+1`**: quem põe o `2` está em outro lugar (talvez dentro de `0x417538` ou em `0x412BA0`). O valor `4` reaparece como constante de comparação.
- Continuam sem leitura: `0x42D838`, `0x4175A8`, `0x417708`, `0x417650`, `0x418300`, `0x418340`, `0x418628`, `0x418640`, `0x47FD70`, `0x480BE0`. O corpo de `0x4153A0` e os wrappers `0x417538`/`0x4176D0` foram lidos (4.7.7).

#### 4.7.4 Efeito nas hipóteses (4.5.3)

- A hipótese 1 (player Sofdec/MPEG) fica **menos firme**. O que a sustentava era a presença do `libmpeg` no ELF e uma convenção de status (0 a 4) lembrada de memória, sem base verificada. Contra: o único código `sceMpeg*` achado fica num módulo à parte e nada o liga ao objeto. O objeto pode ser outra coisa, por exemplo um streamer da CRI (o `CRI_ADXI.IRX` é carregado no IOP). Não está descartada, só sem apoio.
- As hipóteses 2 e 3 continuam abertas e dependem do experimento da 4.6.

#### 4.7.5 Preparação do experimento `run16` (ainda não executado)

Linhas de `ps2xRuntime/src/lib/ps2_runtime.cpp` encontradas com `grep -n 'const uint32_t dbgVs\|<< " vs=" << dbgVs'`:

- `2428`: `const uint32_t dbgVs = m_memory.read32(0x678450u);`
- `2455`: `<< " vs=" << dbgVs`

Editar primeiro a de número maior (para a outra não mudar de lugar). Em `2455`, descer uma linha e colar:

```cpp
                                               << " srvCalls=" << dbgSrvCnt
                                               << " srvN=" << dbgSrvN
                                               << " srvRuns=" << dbgSrvRuns
                                               << " guard=" << dbgSrvGuard
                                               << " usercb=0x" << std::hex << dbgUserCb << std::dec
```

Em `2428`, descer uma linha e colar:

```cpp
                const uint32_t dbgSrvCnt = m_memory.read32(0x533490u);
                const uint32_t dbgSrvN = m_memory.read32(0x534124u);
                const uint32_t dbgSrvRuns = m_memory.read32(0x5341B0u);
                const uint32_t dbgSrvGuard = m_memory.read32(0x53423Cu);
                const uint32_t dbgUserCb = m_memory.read32(0x534240u);
```

Compilar e rodar (a partir de `PS2Recomp/`):

```bash
cmake --build out/build -j2 --target ps2EntryRunner 2>&1 | tail -n 3
timeout 60 script -qfc "./out/build/ps2xRuntime/ps2EntryRunner ../extraido_full/SLUS_210.87" run16_raw.log
tr '\r' '\n' < run16_raw.log | grep -a "run:tick" | tail -3 | cut -c1-900
grep -ai "mpeg\|ipu" run16_raw.log | head -10
```

Executado; resultado na 4.7.7.

#### 4.7.6 Comandos com o rastreador (a partir da raiz do projeto)

Cada `grep` percorre ~265 MB; o `-m1` para na primeira ocorrência.

```bash
# o bloco sceMpeg* do jogo chegou a rodar? (contagem sozinha, sem head)
grep -c "sub_0044C808\|sub_0044C7B8\|sub_0044CF60" PS2Recomp/ps2_log.txt

# primeira aparição (número da linha) de cada função do caminho do loader
for f in 0026F570 003709F8 00412D30 00415380 00414938 00414D08
    echo "== $f =="
    grep -n -m1 "sub_$f" PS2Recomp/ps2_log.txt
end

# o servidor 0x4153A0 (dentro de sub_00415380) roda a cada VSync?
grep -c "sub_00415380" PS2Recomp/ps2_log.txt
```

Se `sub_00415380` nunca aparece, o servidor não roda (compatível com `flag4ba0 != 0`). Se aparece muito, ele roda e falta saber por que o corpo não avança o objeto. Ver também `dis 0x4153a0 90` e `cut -c1-170 output/sub_00414D08_0x414d08.cpp`.

#### 4.7.7 Resultado do `run16` e leitura do servidor `0x4153A0`

**Medido** (`run16_raw.log`, ticks 600 a 3480; última linha, tick 3480):

```
loopState=2 obj=0x54fb60 objw0=0x201 status=2 cnt=0 srvCalls=1553 srvN=1 srvRuns=1553 guard=0 usercb=0x0 total=29 cb=0x0 flag4ba0=0x0 vs=1553 activeThreads=1
```

- `flag4ba0=0`: o callback de VSync chama o servidor. Em todas as linhas `srvCalls == srvRuns == vs` (13, 77, 142, ... 1553): **o corpo do servidor executa em toda chamada**.
- `srvN=1`: há um item registrado. `guard=0`: sem reentrância presa. `usercb=0x0`: sem callback de usuário.
- `status=2`, `cnt=0`, `total=29`, `cb=0x0`: iguais aos do run14/run15. O objeto não avança, apesar do servidor rodar.
- `grep -ai "mpeg\|ipu" run16_raw.log`: **nenhuma linha**. O runtime não registrou chamada de `sceMpeg*`/`sceIpu*` (isso só vale se esses stubs imprimem log; não verificado).
- `activeThreads=1` o tempo todo.
- Rastreador (`ps2_log.txt`): bloco `0x44C` = 0 chamadas; `sub_00415380` = 5180 linhas no rastro (~2590 chamadas); primeira aparição na linha 7433953, logo depois da criação do objeto (`0x3709F8`, `0x26F570` e `0x412D30` nas linhas 7432181 a 7432188). `sub_00414938` e `sub_00414D08` aparecem logo depois (linhas 7433954 e 7433962).
- `srvCalls` cresce ~65 por 120 ticks, ou seja, os "ticks" não são VSyncs 1:1. Não investigado.
- Na primeira tentativa as edições do `ps2_runtime.cpp` entraram duplicadas e foram removidas com `sed`. Ordem final no log: os cinco campos novos ficam entre `cnt=` e `total=`.

**Hipóteses eliminadas:** "o servidor nunca roda" e "o servidor roda sem item registrado". O bloco `sceMpeg*` do jogo nunca é chamado, então ele não é a causa. O que resta: o servidor roda, com um item, e mesmo assim `obj+0x20` nunca sai de 0 nem o status de 2.

**Leitura do corpo de `sub_00415380`** (a partir de `0x4153A0`; o `dis` omite delay slots, por exemplo `0x415404` e `0x4153F4`, então os endereços de tabela abaixo são aproximados):

- Incrementa `[0x533490]` (`srvCalls`). Se `[0x53423C]` (guard) for diferente de zero, sai. Senão grava 1 no guard.
- Se `[0x534124]` (`srvN`) for menor ou igual a 0, pula para `0x41547C`. Com `srvN=1` o corpo roda.
- Percorre três slots (flags em torno de `0x534168`, `0x53416C`, `0x534170`; handles em torno de `0x5341BC`, `0x5341C0`, `0x5341C4`). Para cada slot cuja flag é diferente de 1, chama `func_414938(handle)`. Entre o segundo e o terceiro chama `func_414D08`, que só devolve `[0x534128]`, e compara com 1.
- Depois soma 1 em `[0x5341B0]` (64 bits, `srvRuns`), chama `func_42D838`, zera o guard e chama `[0x534240]` se não for nulo.
- `0x414938` consulta um status por `func_47FD70` (grava em `[sp]`), compara com `4` e, conforme o caso, chama `func_480BE0`. Essas duas funções ficam na faixa `0x47F`–`0x486`, onde estão os wrappers de syscall/SIF do ELF (por exemplo `0x4834E0`, que é `sceSifBindRpc`).
- **Palpite a verificar, não confirmado:** `0x47FD70` seria algo como `ReferThreadStatus` e `0x480BE0` algo como `WakeupThread`; o servidor acordaria threads de trabalho cujo estado é "esperando" (valor 4 no `ReferThreadStatus` do PS2). Se for isso, os handles nos slots são IDs de thread, e **`activeThreads=1` sugere que a thread que deveria fazer o objeto avançar não existe**. Isso explicaria o objeto parado com o servidor rodando. Só `dis` das duas funções e o log de threads confirmam ou derrubam. **Atualização (4.7.9):** o `dis` confirmou que `0x47FD70` é um stub de syscall de status de thread e o rastro mostrou dois handles não nulos; a parte "thread inexistente" ficou enfraquecida.
- `0x417538` e `0x4176D0` (chamadas por `0x412C38`) são wrappers do mesmo estilo de `0x413BB0`: `0x418628` (lock) + corpo (`0x4175A8` e `0x417708`) + `0x418640` (unlock). `0x4176D0` devolve o resultado de `0x417708` (um estado do sub-objeto em `obj+4`), que `0x412C38` compara com `4`.

#### 4.7.8 Próximos comandos (a partir da raiz do projeto)

```fish
# 1. que syscall são 0x47FD70 e 0x480BE0?
dis 0x47fd70 40
dis 0x480be0 40

# 2. o jogo cria/inicia threads? (o log do runtime imprime criação de thread?)
grep -ai "thread" PS2Recomp/run16_raw.log | grep -av "run:tick" | head -20

# 3. o que o servidor chama de fato, na primeira e na milésima vez
set n (grep -n -m1 "sub_00415380" PS2Recomp/ps2_log.txt | cut -d: -f1)
sed -n "$n,+40p" PS2Recomp/ps2_log.txt | cut -c1-120
set m (grep -n "sub_00415380" PS2Recomp/ps2_log.txt | sed -n '1000p' | cut -d: -f1)
sed -n "$m,+40p" PS2Recomp/ps2_log.txt | cut -c1-120

# 4. as syscalls suspeitas rodam?
grep -c "sub_0047FD70" PS2Recomp/ps2_log.txt
grep -c "sub_00480BE0" PS2Recomp/ps2_log.txt
```

Como o `ps2_log.txt` é recriado a cada execução, use o `grep -n -m1` do item 3 em vez de números de linha antigos.

#### 4.7.9 Resultado dos comandos da 4.7.8 (01/11/2026)

**Medido:**

- `dis 0x47fd70`: `addiu $v1, $zero, -0x31` + `syscall` + `jr $ra`. É um stub de syscall do EE com número `-0x31`. O stub seguinte (`0x47FD80`) carrega `0x32`. Pela tabela de syscalls do EE que me lembro (**de memória, a verificar no código do runtime**), `0x31` é `iReferThreadStatus` (a forma usada em contexto de interrupção, como o callback de VSync) e `0x32` é `SleepThread`. Isso bate com o uso: `0x414938` chama o stub com `$a0 = handle` e `$a1 = $sp`
- `dis 0x480be0`: **veio vazio** (só o caminho do arquivo). O arquivo não tem linhas no formato que o `dis` reconhece; precisa ser lido direto
- Rastreador: `sub_0047FD70` = **6424 linhas no rastro (~3212 chamadas)**; `sub_00480BE0` = 0, mas **esse número não vale** (correção na 4.7.11: o arquivo não usa o rastreador)
- Primeira passagem pelo servidor, no rastro: `414938` chamado **duas vezes**, cada uma chamando `47FD70`; `480BE0` não aparece, mas não apareceria de qualquer forma (4.7.11); depois `414D08` e `42D838` (que chama `42D968` → `414D18` e `4275E8`). O terceiro slot não foi consultado
- `grep -ai "thread" run16_raw.log` só devolveu os três `printf` de prioridade do driver CRI do IOP. **O runtime não imprime criação de thread do EE**, então esse log não responde nada sobre threads do jogo
- O nome `sub_00415380` no rastro cobre o arquivo inteiro (`0x415380`–`0x4154C8`): o wrapper de 5 instruções em `0x415380` (que faz `j func_426EF8`) e o corpo em `0x4153A0`. A contagem de 5180 linhas mistura os dois; a medida confiável do corpo é `srvRuns` (4.7.7)

**Leitura de `0x414938` fechada** (combinando com o `dis` anterior; os delay slots ocultos tornam o detalhe do segundo valor comparado uma inferência): se `$a0 != 0`, chama `iReferThreadStatus(handle, &st)`; o primeiro campo da estrutura é o status do thread. Se for `4` ou `0xC` (WAIT e WAIT+SUSPEND, **de memória**), chama `func_480BE0(handle)`; senão volta sem fazer nada. O padrão (consultar o thread e, se estiver esperando, acordá-lo) é o de um servidor de VSync que acorda threads de trabalho.

**Consequências:**

- Há **dois handles não nulos** nos slots. A hipótese "thread de trabalho inexistente" (4.7.7) fica enfraquecida: algo foi criado e guardado
- **Não está provado que o servidor nunca acorda ninguém** (correção na 4.7.11). Se não acorda, os threads dos slots não estão em WAIT (nunca iniciados, terminados ou rodando) ou o `iReferThreadStatus` do runtime devolve status errado; se acorda, a falha está no que o thread acordado faz ou no `iWakeupThread` do runtime
- Os valores dos handles continuam desconhecidos (`0x480BE0` é wrapper de `iWakeupThread`, ver 4.7.11)

#### 4.7.10 Próximos comandos (a partir da raiz do projeto)

```fish
# A. o que o runtime faz no syscall de status de thread (ReferThreadStatus / iReferThreadStatus)
grep -rn "ReferThreadStatus" PS2Recomp/ps2xRuntime/src/lib --include='*.cpp' | head -20

# B. corpo de 0x480BE0 (o dis veio vazio)
cut -c1-170 output/sub_00480BE0_0x480be0.cpp | head -60

# C. arquivos-stub dos syscalls 0x20 (CreateThread), 0x22 (StartThread) e 0x33/0x34 (Wakeup)
grep -l 'addiu *[$]v1, [$]zero, 0x20$' output/sub_0047F*.cpp
grep -l 'addiu *[$]v1, [$]zero, 0x22$' output/sub_0047F*.cpp
grep -l 'addiu *[$]v1, [$]zero, -\?0x3[34]$' output/sub_0047F*.cpp
```

Com os endereços do item C, dá para contar no rastreador quantas vezes cada um rodou (`grep -c "sub_XXXXXXXX" PS2Recomp/ps2_log.txt`). Também vale abrir a aba **Threads** do Runtime Debugger durante o loop: ela deve listar os threads do EE e seus estados. Se A e B não bastarem, o próximo experimento é acrescentar ao `[run:tick]` a leitura dos handles e das flags dos slots (`0x5341BC`, `0x5341C0`, `0x5341C4` e `0x534168`, `0x53416C`, `0x534170`; endereços aproximados, ver 4.7.7).

#### 4.7.11 Resultado dos comandos da 4.7.10 e correção da 4.7.9

**Correção:** `sub_00480BE0` é um wrapper gerado que só chama `ps2_syscalls::iWakeupThread(rdram, ctx, runtime)`. Esse arquivo foi gerado com `#ifdef _DEBUG` / `PS_LOG_ENTRY`, e não com `PS2_FUNCTION_LOG_TRACKER` como `sub_00414D08` e `sub_0047FD70`. Portanto o rastreador **não registra** `0x480BE0`, e a contagem 0 da 4.7.9 não vale. A conclusão "o servidor nunca acorda ninguém" **não é sustentada** e foi retirada. Não se sabe se `iWakeupThread` é chamado. (Outros arquivos de stub gerados podem ter o mesmo problema; antes de confiar numa contagem 0, conferir o guard do arquivo.)

**Medido/lido:**

- A: o runtime implementa `ReferThreadStatus` em `Kernel/Syscalls/Thread.cpp:334` e `iReferThreadStatus` em `:370` (só chama a primeira). O `Kernel/Syscalls/Dispatcher.cpp` liga os números de syscall a elas (linhas 119 e 122). `Kernel/EeScheduler.cpp:141` comenta que o status devolvido por `ReferThreadStatus` depende de metadados gravados por `SetupThread`. Os corpos ainda não foram lidos
- B: `0x480BE0` = wrapper de `iWakeupThread` (acima)
- C, pela ordem dos três greps: `0x47FC60` = syscall `0x20` (CreateThread), `0x47FC80` = `0x22` (StartThread), `0x47FD90` e `0x47FDA0` = `0x33`/`0x34` (WakeupThread/iWakeupThread; qual é qual não foi conferido). Os nomes vêm do número do syscall, de memória
- O `grep -c "sub_0047XXXX"` da rodada anterior foi rodado com o texto-modelo, não com um endereço real; o resultado 0 não significa nada

**Estado das hipóteses:** continua sem resposta por que o objeto não avança. Duas leituras são possíveis: (1) os threads dos slots nunca estão em WAIT segundo o `iReferThreadStatus` do runtime (threads não iniciados, terminados, ou status errado), e o servidor nunca os acorda; (2) o servidor acorda e a falha está depois (no que o thread acordado faz, ou no `iWakeupThread` do runtime). Falta o dado que distingue as duas.

#### 4.7.12 Próximos comandos (a partir da raiz do projeto)

```fish
# A. corpos no runtime
sed -n '325,375p' PS2Recomp/ps2xRuntime/src/lib/Kernel/Syscalls/Thread.cpp
sed -n '100,135p' PS2Recomp/ps2xRuntime/src/lib/Kernel/Syscalls/Dispatcher.cpp
sed -n '130,160p' PS2Recomp/ps2xRuntime/src/lib/Kernel/EeScheduler.cpp

# B. os stubs de thread usam o rastreador? (guard do arquivo)
grep -c "PS2_FUNCTION_LOG_TRACKER" output/sub_0047FC60_0x47fc60.cpp output/sub_0047FC80_0x47fc80.cpp output/sub_0047FD90_0x47fd90.cpp output/sub_0047FDA0_0x47fda0.cpp

# C. contagem no rastreador (só vale se o item B der 1 ou mais)
for f in 0047FC60 0047FC80 0047FD90 0047FDA0
    echo "== $f =="
    grep -c "sub_$f" PS2Recomp/ps2_log.txt
end

# D. o runtime loga wakeup/create/start?
grep -ai "wakeup\|CreateThread\|StartThread" PS2Recomp/run16_raw.log | grep -av "run:tick" | head
```

#### 4.7.13 Resultado dos comandos da 4.7.12 (01/11/2026)

**Correção de método:** o `grep -c` conta **linhas**, e cada chamada gera duas no rastro (`>> ... enter` e `<< ... exit`). Os números anteriores eram linhas, não chamadas: `sub_00415380` 5180 linhas (~2590 chamadas) e `sub_0047FD70` 6424 linhas (~3212 chamadas). Os textos acima foram ajustados.

**Medido:**

- Os quatro stubs de thread **usam o rastreador** (o `grep -c` achou `PS2_FUNCTION_LOG_TRACKER` em todos), então as contagens abaixo valem
- `0x47FC60` (syscall `0x20`, CreateThread): 8 linhas = **~4 chamadas**
- `0x47FC80` (syscall `0x22`, StartThread): **0**
- `0x47FD90` (`0x33`, WakeupThread): **0**; `0x47FDA0` (`-0x34`, iWakeupThread): **0**
- Os endereços avançam de 0x10 em 0x10 junto com o número do syscall (`0x20` → `47FC60`, `0x31` → `47FD70`, `0x32` → `47FD80`, `0x33` → `47FD90`, `0x34` → `47FDA0`), o que reforça a identificação (os nomes vêm de memória)
- `Dispatcher.cpp` (cerca das linhas 119–135): `-0x31` → `iReferThreadStatus`, `0x33` → `WakeupThread`, `-0x34` → `iWakeupThread`. Os stubs do jogo chegam mesmo às implementações do runtime
- `ReferThreadStatus` (`Thread.cpp:334`): procura o thread pelo id; se **não existe**, devolve `KE_UNKNOWN_THID` e **não escreve** a estrutura; se existe, grava `status = rawThreadStatus(thread->status)`. `iReferThreadStatus` só chama essa
- `EeScheduler.cpp:141`: o thread principal nasce `Ready`
- O `dis` de `0x414938` lê `[sp]` logo depois do `jal 47FD70` e **não testa o retorno** (`$v0`). Com um handle inexistente, o `[sp]` fica com lixo da pilha

**Interpretação (inferência, não confirmada):**

- (**RETIRADO na 4.7.15**) Quatro threads foram **criados** e nenhum foi **iniciado** pelos stubs rastreados. Um thread criado e não iniciado fica DORMANT (`0x10`, de memória), não em WAIT, e não conta como ativo. Isso combina com `activeThreads=1` e com o servidor nunca encontrar um thread em espera
- A pergunta passa a ser: **quem deveria chamar `StartThread`, e por que não chama?** Pode ser que o jogo chame `StartThread` por outro ponto de entrada que o rastreador não vê (como acontece com `0x480BE0`, que usa `_DEBUG`), ou que o código que o chama nunca seja alcançado
- Também continua possível que os handles dos slots sejam inválidos, e aí o status lido é lixo; não se sabe quais são os valores

#### 4.7.14 Próximos comandos (a partir da raiz do projeto)

```fish
# 1. todos os pontos de entrada relacionados a thread no sm.toml (há outro StartThread?)
grep -n "Thread" sm.toml

# 2. bytes reais em 0x480BE0: é mesmo um stub de syscall? (esperado, se for -0x34: 2403ffcc 0000000c 03e00008)
python3 -c '
import struct,sys
d=open("extraido_full/SLUS_210.87","rb").read()
ph=struct.unpack_from("<I",d,0x1c)[0]; n=struct.unpack_from("<H",d,0x2c)[0]; es=struct.unpack_from("<H",d,0x2a)[0]
va=int(sys.argv[1],16)
for i in range(n):
    t,off,v,pa,fs,ms,fl,al=struct.unpack_from("<8I",d,ph+i*es)
    if t==1 and v<=va<v+fs:
        o=off+va-v
        for k in range(8):
            print(hex(va+4*k), "%08x"%struct.unpack_from("<I",d,o+4*k)[0])
' 0x480BE0

# 3. quem chama CreateThread e o que vem logo depois (o recuo mostra o chamador)
grep -n -B4 -A6 ">> sub_0047FC60" PS2Recomp/ps2_log.txt | cut -c1-140 | head -80
```

O item 3 mostra as funções que criam os quatro threads. Com os nomes delas dá para ler o código e ver onde deveria vir o `StartThread`.

#### 4.7.15 Resultado dos comandos da 4.7.14 e retirada da interpretação da 4.7.13 (01/11/2026)

**Medido/lido:**

- `sm.toml`, lista `stubs`: `InitThread@0x00480AF0` (linha 138), `iWakeupThread@0x00480BE0` (139) e `StartThread@0x00480D48` (140). Em `entry_points`: `topThread@0x00480A18`, `DelayThread@0x00480C80` e `_DelayThreadHandler@0x00480EC0` (linhas 422 a 424). Ou seja, o `StartThread` e o `iWakeupThread` que o jogo chama são essas funções da biblioteca de threads do SDK (`0x480xxx`), **não** os stubs de syscall puro `0x47FC80`/`0x47FDA0`
- Bytes reais em `0x480BE0`: `27bdffe0` (prólogo), `ffbf0010`, `ffb00000`, `2403ffd1` (`$v1 = -0x2F`), `0000000c` (`syscall`), `0040802d`, `12040005` (`beq`), `2e020100` (`sltiu ... 0x100`). Não é um stub `-0x34`: é uma função maior que chama o syscall `-0x2F` (em memória, GetThreadId) e compara com `$a0`. O runtime a **substitui inteira** por `iWakeupThread` HLE
- Rastro: quatro funções criam threads, cada uma chamando `0x47FC60` (CreateThread): `sub_00414AB8` (linha 7426710), `sub_00414B60` (7426722), `sub_00414BE8` (7426728) e `sub_00414C70` (7426734). Em volta aparecem `0x47FD50`, `0x47FD60` (`ReferThreadStatus` não-i), `0x47FCF0` e `0x47FDD0`, e uma função auxiliar `sub_00414A58`. Os números de syscall desses stubs, pelo passo de 0x10 a partir de `0x47FC60`, seriam `0x2F`, `0x30`, `0x29` e `0x37` (inferência por endereço, não verificada)

**Retirada:** a 4.7.13 concluiu que os threads são criados e nunca iniciados porque `0x47FC80` (StartThread) deu 0 chamadas. Isso **não vale**: o jogo chama `StartThread` por `0x480D48`, que é stub HLE e, como `0x480BE0`, provavelmente não usa o rastreador. A contagem 0 em `0x47FC80`, `0x47FD90` e `0x47FDA0` é só uma propriedade de qual wrapper o jogo usa. Continua **desconhecido** se os threads são iniciados e acordados.

O que é sustentado: quatro threads são criados pelas funções acima; `activeThreads=1` continua medido; o servidor consulta dois handles com `iReferThreadStatus` e não há registro de resultado.

#### 4.7.16 Próximos comandos (a partir da raiz do projeto)

```fish
# 1. leitura dos criadores: onde ficam a função de entrada e a prioridade de cada thread
dis 0x414ab8 70
dis 0x414a58 50

# 2. sequência no rastro, da criação do primeiro thread até depois do último
sed -n '7426660,7426800p' PS2Recomp/ps2_log.txt | cut -c1-110

# 3. implementações no runtime
grep -n "void CreateThread\|void StartThread\|void WakeupThread\|void iWakeupThread\|void SetupThread" PS2Recomp/ps2xRuntime/src/lib/Kernel/Syscalls/Thread.cpp

# 4. o stub 0x480D48 usa o rastreador?
grep -c "PS2_FUNCTION_LOG_TRACKER" output/sub_00480D48_0x480d48.cpp
head -20 output/sub_00480D48_0x480d48.cpp
```

O item 2 só vale se o `ps2_log.txt` não foi recriado por uma execução nova desde a rodada anterior (as linhas citadas vêm daquele arquivo). Se tiver sido, refaça o `grep -n -m1 ">> sub_00414AB8"` para achar a linha.

Depois dessa leitura, o experimento limpo é acrescentar um `fprintf` temporário em `CreateThread`, `StartThread` e `iWakeupThread` do runtime (id, status, prioridade) para obter a resposta direta, sem depender do rastreador.

---

## 5. Etapa 4 — serviço `cdvdman` de verdade (opcional, qualidade upstream)

O caminho mais fiel é implementar um serviço HLE para o SID `0x80000597` no `ps2xIOP`, no molde de `libsd.cpp`/`mcserv.cpp`/`dbcman.cpp`:

- Criar `PS2Recomp/ps2xIOP/src/modules/cdvd.cpp`.
- `sids()` → `{0x80000597}`.
- `moduleAliases()` → `{"cdvdman", "cdvdfsv"}` (para o serviço ativar quando o módulo for reconhecido).
- `handleRpc()` decodifica o protocolo do libcdvd (comandos de `CdDiskReady`, `CdSearchFile`, `CdRead`, etc.) e reutiliza `IopHost::hostPath(HostPathKind::CdRoot)` e leitura de arquivo do host.
- Registrar em `iop_subsystem.cpp` (junto de MCSERV/DBCMAN/LIBSD) e em `module_factories.h`.

Vantagem: o libcdvd guest continua intacto, sem overrides por endereço. Desvantagem: exige engenharia reversa do pacote RPC (o formato não está documentado no repo). Fazer **depois** de validar o boot com a Etapa 1.

---

## 6. Etapa 5 — higiene do projeto

- `PS2Recomp/ps2_log.txt` tinha **242 MB** e estava untracked. **Movido para `/tmp/ps2_log.txt`** em 01/11/2026 (hoje `ls` mostra 267M); apagar quando não precisar mais. **Um novo `ps2_log.txt` (265M) já foi recriado em `PS2Recomp/`**: o rastreador de funções está ligado e escreve ~265 MB a cada execução (seção 4.7.1). Incluir `ps2_log.txt` no `.gitignore`.
- `PS2Recomp/imgui.ini` está untracked; ignorar.
- O único arquivo versionado modificado no clone é `ps2xRuntime/src/runner/register_functions.cpp` (gerado e, na prática, descartável).
- `ps2xRuntime/src/runner/` está no `.gitignore` do PS2Recomp; não commitar os 13.649 `.cpp`.
- Manter as mudanças de runtime num branch ou numa pasta `patches/` com diffs, porque o clone é upstream `ran-j/PS2Recomp` em `e563698`. **Feito em 01/11/2026** (`ShaolinMonks/patches/`: `runtime_etapas_1_3.diff`, `game_overrides_mksm.cpp` e cópias de `PLANO.md`/`README.md`). O diff foi gerado antes da telemetria do objeto em `ps2_runtime.cpp`; regenerar antes de publicar.
- Documentar o comando de regeneração do runner para não precisar recommitar nada gerado.

---

## 7. Critérios de sucesso

| Etapa | Critério | Status em 01/11/2026 |
|---|---|---|
| 1 | `[game_overrides] applying 'mksm-cdvd'` no log e `pc` sai de `0x385D80` | **OK** |
| 1 | pelo menos um `[sceCdSearchFile:ok]` com `path` do jogo | **OK** (16 arquivos) |
| 2 | nenhum `sceCdRead unresolved LBN` | **OK** (0 ocorrências) |
| 3 | tela magenta eliminada e frame apresentado no host | **OK** (resolvido com patch de PMODE/SMODE2 e struct `sceGsDispEnv`) |
| 3 | saída do loop de loader em `0x211BE0` / início de leitura ou menu | **Pendente** (objeto do player em `*(0x4FADEC)` parado em status 2 com `cnt=0`; ver 4.5) |
| 3 | objeto em `*(0x4FADEC)` chega ao status 3 (`func_370A98` retorna diferente de zero) | **Pendente** |
| 3 | experimento `run16` (cinco valores do servidor `0x4153A0` no tick) | **OK** (servidor roda em todo VSync, `srvN=1`, objeto parado; ver 4.7.7) |
| 3 | descobrir se os threads criados por `0x414AB8`/`0x414B60`/`0x414BE8`/`0x414C70` chegam a ser iniciados e acordados (`StartThread`/`iWakeupThread` são stubs HLE não rastreados) | **Pendente** (4.7.15 e 4.7.16) |
| 4 | `canBindRpc(0x80000597) == true` e libcdvd guest funcionando sem overrides | Não iniciado (opcional) |

---

## 8. Rollback

- Etapa 1: remover `game_overrides_mksm.cpp` e a linha `target_sources(ps2EntryRunner PRIVATE ...)`. Rebuild incremental.
- Etapa 2: remover o bloco que seta `cdImage`; reverter `kCdPseudoLbnStart` para `0x00100000` (só é seguro sem `cdImage`).
- Etapa 3: reverter alterações em `ps2xRuntime/src/lib/Kernel/Stubs/GS.cpp` e `Support.h`.
- Telemetria do objeto do player (`obj`, `status`, `cnt`, `total`, `pct`, `cb`, `flag4ba0`, `vs`): remover as variáveis `dbg*` e as linhas correspondentes do `RUNTIME_LOG` de `[run:tick]` em `ps2_runtime.cpp`.
- Telemetria do servidor (`srvCalls`, `srvN`, `srvRuns`, `guard`, `usercb`), se aplicada: remover as variáveis `dbgSrv*`/`dbgUserCb` e as cinco linhas `<< " srv..."` correspondentes em `ps2_runtime.cpp`.
- Nada em `output/` ou nos `.cpp` gerados é alterado por este plano.

---

## 9. Referências de código

| Assunto | Arquivo:linha |
|---|---|
| `SifBindRpc` | `PS2Recomp/ps2xRuntime/src/lib/Kernel/Syscalls/RPC.cpp:254` |
| `client->server` preenchido | `RPC.cpp:298` |
| `canBindRpc` | `PS2Recomp/ps2xIOP/src/iop_subsystem.cpp:188` |
| Rotas HLE | `PS2Recomp/ps2xIOP/src/iop_subsystem.cpp:22-25` |
| `sceSifRegisterRpc` (IRX) | `PS2Recomp/ps2xIOP/src/emulator/services/iop_rpc.cpp:211` |
| Módulos reconhecidos (`cdvdman`) | `PS2Recomp/ps2xIOP/src/iop_module_manager.cpp:41-42` |
| `sceCdSearchFile` HLE | `PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/CD.cpp:545` |
| `sceCdRead` HLE | `CD.cpp:207` |
| `kCdPseudoLbnStart` | `.../Stubs/Helpers/Support.h:7` |
| `registerCdFile` | `Support.h:325` |
| `readCdSectors` | `Support.h:429` |
| `writeCdSearchResult` | `Support.h:520` |
| Overrides | `PS2Recomp/ps2xRuntime/include/game_overrides.h` |
| Aplicação do override | `PS2Recomp/ps2xRuntime/src/lib/ps2_runtime.cpp:1006` |
| `configureIoPathsFromElf` | `ps2_runtime.cpp:1052` |
| Layout de `sceGsDispEnv` (Sony libgraph) | `PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/Helpers/Support.h:1495` |
| Reset e registradores privilegiados GS | `PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/GS.cpp:810` |
| Loop de controle / loader | `output/sub_00211B38_0x211b38.cpp:177` |
| Consulta de status assíncrono | `output/sub_0026F6E0_0x26f6e0.cpp:41` |
| `add_executable(ps2EntryRunner)` | `PS2Recomp/ps2xRuntime/CMakeLists.txt:460` |
| `add_library(ps2_runtime STATIC)` | `PS2Recomp/ps2xRuntime/CMakeLists.txt:378` |
| Criador do objeto do player | `output/sub_0026F570_0x26f570.cpp` (wrapper em `0x3709F8`; construtor em `0x412D30`) |
| Callback de VSync | `output/sub_0026EFE8_0x26efe8.cpp` |
| Servidor periódico (hipótese) | `0x4153A0`, dentro de `output/sub_00415380_0x415380.cpp` |
| Getters de status e contador do objeto | `0x413BE8` (byte `obj+1`), `0x413B80` (`obj+0x20`) |
| Linhas a editar para o experimento `run16` | `ps2_runtime.cpp:2428` (`dbgVs`) e `:2455` (`<< " vs=" << dbgVs`) |
| Chamadores do bloco `sceMpeg*` do jogo | `output/sub_0044C7A8_*`, `sub_00446878_*` (Init); `sub_0044A8D0_*` (Create); `sub_00449BA0_*` (GetPicture) |
| Funções lidas na rodada 4.7 | `output/sub_00414938_0x414938.cpp`, `sub_00414D08_0x414d08.cpp`, `sub_00412C38_0x412c38.cpp` |
| `sceMpeg*` / `sceIpu*` HLE | `PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/MPEG.cpp` (`sceMpegCreate`:1986, `sceMpegGetPicture`:2283), `Stubs/IPU.cpp` (`sceIpuInit`:53) |
| Imports `cdvdman` no IOP | `PS2Recomp/ps2xIOP/src/emulator/imports/iop_cdvd.cpp` |
| Stubs / entry_points no `sm.toml` | `sm.toml`: `stubs` 24 a 181, `entry_points` 185 a 506 |

---

## 10. Resultado da Etapa 1 (executado em 01/11/2026)

### 10.1 Arquivos alterados

| Arquivo | Mudança |
|---|---|
| `PS2Recomp/ps2xRuntime/src/lib/game_overrides_mksm.cpp` | **novo**: override do libcdvd com 10 bindings |
| `PS2Recomp/ps2xRuntime/CMakeLists.txt` | `target_sources(ps2EntryRunner PRIVATE .../game_overrides_mksm.cpp)` |

Nada em `output/` ou em `ps2xRuntime/src/runner/` foi alterado.

### 10.2 Build

```bash
cd PS2Recomp
cmake --build out/build -j2 --target ps2EntryRunner
```

Saída relevante:

```
[1/5] Building CXX object ps2xRuntime/CMakeFiles/ps2EntryRunner.dir/src/lib/game_overrides_mksm.cpp.o
[2/5] Linking CXX executable ps2xRuntime/ps2EntryRunner
```

### 10.3 Evidência de sucesso

- **10/10 binds OK**, 0 `FAILED to bind`.
- **16 `[sceCdSearchFile:ok]`**, incluindo `FILELIST.DIR`, `WADCRC.BIN`, todos os `GAMEDATA.WA*` e todos os `FRONT\MOVIES\*.SFD`.
- **0 `sceCdRead unresolved LBN`** → a Etapa 2 não foi necessária.
- O PC saiu do laço infinito de bind em `0x385D80`.

---

## 11. Resultado da Etapa 3 — Apresentação de Frame e Resolução da Tela Magenta (executado em 01/11/2026)

### 11.1 Arquivos alterados

| Arquivo | Mudança |
|---|---|
| `PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/GS.cpp` | Privileged registers `pmode` e `smode2` setados diretamente na memória do GS (`runtime->memory().gs()`) e removidos do pacote GIF A+D (que suportava apenas regs `<= 0x5F`). |
| `PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/Helpers/Support.h` | Corrigido o layout de `GsDispEnvMem` (40 bytes, 5 qwords: `dispfb1`, `display1`, `dispfb2`, `display2`, `bgcolor`). Removidos `pmode`/`smode2` da struct, impedindo que `sceGsPutDispEnv` sobrescreva `PMODE` com zero. |
| `PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/CD.cpp` | Adicionado log de progresso para `sceCdRead` (registra as primeiras 16 leituras e sumário a cada 64 MB). |
| `PS2Recomp/ps2xRuntime/src/lib/ps2_runtime.cpp` | Adicionada telemetria aos ticks `[run:tick]`: `caller` (lido da pilha `[sp]`), registradores do GS (`pmode`, `smode2`, `dispfb2`, `display2`), e variáveis de loader `loopState` (`0x4EF7E0`) e `loopCursor` (`0x4EF7EC`). |

### 11.2 Evidência de sucesso

1. **Eliminação do Magenta:**
   - Nos logs `run11_raw.log` a `run13_raw.log`, a busca por `MAGENTA` retorna **0 ocorrências**.
   - O runtime executa `[frame:upload]` a cada tick com sucesso contínuo:
     ```
     [frame:upload] idx=127 tick=137 displayFbp=0 sourceFbp=0 size=640x224 preferred=0
     ```
   - As capturas de tela `/tmp/mksm_fix.png` e `/tmp/mksm_frame2.png` geram PNGs válidos de 640x448 com saída preta (pixels limpos pelo GS do jogo), provando que o host está recebendo e apresentando o framebuffer real.

2. **Estabilidade de Execução:**
   - O jogo alcançou mais de `tick=9480` (~158 segundos) a 60 fps constantes.
   - Não há travamento de CPU, lentidão ou vazamento de memória.

3. **Novo Diagnóstico Identificado:**
   - O jogo está atualmente esperando no loop `sub_00211B38` (`caller=0x211BE0`), consultando o status assíncrono em `func_370A98` a cada VSync (`sceGsSyncV`).
   - Todos os arquivos do disco foram catalogados pelo jogo, aguardando o início do streaming/leitura.

### 11.3 Artefatos gerados

| Arquivo | Conteúdo |
|---|---|
| `PS2Recomp/run12_raw.log` | Execução longa de ~160 segundos com telemetria de `caller` e registradores |
| `PS2Recomp/run13_raw.log` | Execução demonstrando fluxo contínuo de `[frame:upload]` e ausência de magenta |
| `/tmp/mksm_fix.png` | Frame do jogo renderizado em 640x448 (tela preta apresentada pelo GS) |
| `/tmp/mksm_frame2.png` | Segundo snapshot confirmando apresentação contínua sem fallback |
| `PS2Recomp/run14_raw.log` | Execução com a telemetria do objeto do player (`obj`, `status`, `cnt`, `total`, `pct`): objeto `0x54fb60` em status 2 com `cnt=0` do tick 600 ao 3480 |
| `PS2Recomp/run15_raw.log` | Execução com `cb`, `flag4ba0` e `vs` acrescentados (`cb=0x0`; a última linha saiu truncada pelo `timeout`) |
| `PS2Recomp/run16_raw.log` | Execução com `srvCalls`, `srvN`, `srvRuns`, `guard`, `usercb` (ticks 600 a 3480): `srvN=1`, `srvRuns == srvCalls == vs`, `cnt=0` |
| `PS2Recomp/ps2_log.txt` | Rastro de chamadas do rastreador de funções (265M); recriado a cada execução; ver 4.7.1 |

### 11.4 Telemetria adicional do objeto do player (01/11/2026)

Em `PS2Recomp/ps2xRuntime/src/lib/ps2_runtime.cpp`, junto das variáveis `loopState`/`loopCursor` do `[run:tick]`, foram acrescentadas as leituras abaixo e os campos correspondentes no `RUNTIME_LOG`:

| Variável | Lê | Campo no log |
|---|---|---|
| `dbgObj` | `*(0x4FADEC)` | `obj` |
| `dbgObjW0` | `*(obj)` (só se `obj` for válido e alinhado) | `objw0`, `status` (byte `obj+1`) |
| `dbgObjCnt` | `*(obj+0x20)` | `cnt` |
| `dbgTotal` | `*(0x678440)` | `total` |
| `dbgProg` | `*(0x678448)` | `pct` |
| `dbgCb` | `*(0x4FADF0)` | `cb` |
| `dbgFlag` | `*(0x4F4BA0)` | `flag4ba0` |
| `dbgVs` | `*(0x678450)` | `vs` |

Resultado: ver 4.5.1. Cada edição de `ps2_runtime.cpp` leva só um build incremental (compilação de um arquivo e link), poucos minutos.
