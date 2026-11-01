```markdown
# ShaolinMonks: Plano de Investigação e Recompilação PS2

Estado em 01/11/2026 (noite, após run22). Este é o documento técnico detalhado (diário de bordo). O `README.md` é o resumo de status. Convenção: **[medido]** = visto em execução, **[lido]** = lido no código, **[memória]** = vem do conhecimento da IA sobre o SDK do PS2.

## 1. Contexto e Objetivo

Estamos portando Mortal Kombat: Shaolin Monks (PS2) usando recompilação estática via PS2Recomp. A CPU roda nativa em C++, mas o hardware auxiliar (GS, VU, DMA, IOP) ainda é emulado. O objetivo atual é **passar da tela de loading para o menu principal**. O jogo está travando num loop infinito enquanto tenta ler arquivos do `GAMEDATA.WAD` via `sceCdRead`.

## 2. Estado das Fases Anteriores

| Fase | Status | Detalhe |
|---|---|---|
| Bootstrap & ELF | ✅ | ISO extraída, 13.651 arquivos C++ gerados. |
| Tela Magenta | ✅ | `PMODE` e `sceGsDispEnv` corrigidos. |
| CDVD Bind | ✅ | 10 binds HLE (`mksm-cdvd`) substituem as chamadas de libcdvd do guest. |
| Threads | ✅ | `CreateThread` com prio 0 retornava -403. Fix em `EeScheduler.cpp:426` (`< 0` em vez de `< 1`). 5 threads ativas [run19]. |
| Leitura de CD | ⚠️ | Lê setores corretamente, mas o *polling* de conclusão falha e trava a máquina de estados. |

## 3. A Dissecação do Bloqueio Atual (Run22)

O jogo não crasha. Ele entra num loop apertado que impede o VSync de avançar (o contador `vs` congela em 35).

### 3.1 A Máquina de Estados (`func_378A30`)

Localizada em `0x378A30`, é a responsável por gerenciar o streaming. 
- Ela checa o bit 0 de `[0x755AB8]` (ocupado).
- Lê o estado atual em `[0x755ABC]`.
- Salta para o handler correspondente na tabela em `0x592790`.

**A Jump Table (medida no run22):**
| Estado (`[0x755ABC]`) | Handler | O que faz |
|---|---|---|
| `0` | `0x378A90` | Init. Chama `func_211878` (requisição) e calcula tamanhos. |
| `1` | `0x378BF8` | **Poll**. Chama `func_2123C8`. Se `0` -> Estado 2. Se `-1` -> Estado 0. Outro -> Fica no 1. |
| `2` | `0x378C3C` | Dispara o `sceCdRead` (`func_467670`) no slot atual e vai pro Estado 3. |
| `3` | `0x378CA8` | Verifica callback em `[ptr+0x24]`. Se existir, chama e vai pro Estado 4. |
| `4` | `0x378CDC` | Finaliza e reseta pro Estado 0. |

### 3.2 O Loop Infinito no Estado 1

**Antes do travamento (tick 600-1200):**
- O `status` da estrutura principal (`0x54FB60`) está em 2 (lendo).
- `busySt=1` (a máquina está no estado 1).
- O `busyPtr` cicla entre 4 slots (`0x755ad0`, `0x755b04`, `0x755b38`, `0x755b6c`).
- O `busyRes` (`[ptr+0xC]`) contém contadores de bytes válidos (200, 813, 3998).
- O `obj14` avança (28 -> 39), indicando que o WAD está sendo escrito.

**Depois do travamento (tick 1320+):**
- O objeto principal muda para `status=1` (leitura concluída). `obj8` (callback pendente) vira `0x0` (foi consumido).
- **Porém**, a máquina de estados continua presa no `busySt=1`.
- O `busyPtr` congela em `0x755b04`.
- O `busyRes` vira lixo (`0x803AA402`).
- O `vs` (VSync) congela. O jogo morreu.

**O que isso significa:** A `func_2123C8` (chamada no Estado 1) está checando algum campo no slot (provavelmente `[+0x1C]`, `[+0x20]` ou `[+0x30]`). O HLE do `sceCdRead` (no runtime) terminou a leitura e mudou o status do objeto global, **mas não sinalizou o campo específico que o slot interno espera**. A `func_2123C8` nunca retorna `0` (sucesso) nem `-1` (erro), então o jogo fica em loop eterno.

### 3.3 O Objeto de Streaming (`0x54FB60`)
Sabemos que:
- `obj[1]`: Status (1 = lido, 2 = lendo).
- `obj[3]`: Flag de processamento. Fica em `0` e trava o fluxo.
- `obj[8]`: Callback pendente. (Foi consumido).
- `obj[0x14]`: Cursor de escrita.

## 4. Próximos Passos (Plano de Ação)

Para resolver o travamento, precisamos ver o que a `func_2123C8` faz e adicionar os campos que ela lê à telemetria.

**Passo 1: Dissecar o Poll e o "Outro" caminho**
```fish
cd /run/media/valk/linux_arquivos/projetos/ShaolinMonks
dis 0x2123C8 100
dis 0x378D38 80  # O código que roda se o poll não retornar 0 nem -1
```

**Passo 2: Adicionar campos de telemetria do Slot**
No `ps2_runtime.cpp`, no bloco do `busyPtr`, adicione a leitura dos campos suspeitos:
```cpp
uint32_t busyF4 = 0u;   // [ptr+0x4] (flags)
uint32_t busyF8 = 0u;   // [ptr+0x8] (buffer)
uint32_t busyF1C = 0u;  // [ptr+0x1C] 
uint32_t busyF20 = 0u;  // [ptr+0x20] 
uint32_t busyF30 = 0u;  // [ptr+0x30] (end offset)
if (busyPtr != 0u && (busyPtr & 3u) == 0u && busyPtr < PS2_RAM_SIZE - 0x34u) {
    // ... leituras anteriores ...
    busyF4  = m_memory.read32(busyPtr + 0x04u);
    busyF8  = m_memory.read32(busyPtr + 0x08u);
    busyF1C = m_memory.read32(busyPtr + 0x1Cu);
    busyF20 = m_memory.read32(busyPtr + 0x20u);
    busyF30 = m_memory.read32(busyPtr + 0x30u);
}
```
E no `RUNTIME_LOG`:
```cpp
<< " busyF4=0x" << std::hex << busyF4
<< " busyF8=0x" << busyF8
<< " busyF1C=0x" << busyF1C
<< " busyF20=0x" << busyF20
<< " busyF30=0x" << busyF30 << std::dec
```

**Passo 3: Analisar o resultado (run23)**
- Comparar `busyF30` com `busyRes` (`[+0xC]`). Se forem iguais, a leitura chegou ao fim. O bug está na lógica da `func_2123C8`.
- Se `busyF20` mudar de 0 para 1, o problema é a sincronia.

**Passo 4: Correção no Runtime (HLE)**
Se descobrirmos qual flag a `func_2123C8` espera, precisamos injetar essa flag no `game_overrides_mksm.cpp` (ou no `CD.cpp`) logo após o `sceCdRead` terminar, forçando a máquina a sair do estado 1.

**Passo 5 (Triagem / Hack):**
Se a investigação demorar, podemos forçar a saída no game override alterando a memória: quando `[0x755ABC] == 1` e o objeto principal tiver `status == 1`, forçar `[0x755ABC] = 2`.

## 5. Armadilhas Conhecidas do Ambiente e Código

### 5.1 A Macro `PS2_IF_AGRESSIVE_LOGS`
Macro em `ps2_log.h:189`:
```c
#define PS2_IF_AGRESSIVE_LOGS(code) /* if(AGRESSIVE_LOGS) { code; } */
```
**ATENÇÃO:** Esta macro aceita apenas **1 argumento**.
- NUNCA coloque arrays inicializados com vírgulas: `uint32_t jt[5] = {0, 0, 0, 0, 0};` quebra o build (o pré-processador acha que são 5 argumentos).
- Use `uint32_t jt[5]{};`.
- Declarações, `for` e `if` devem ficar **antes** da string do `RUNTIME_LOG(...)`, nunca dentro dos `<<`.

### 5.2 Fish Shell
- Não use heredocs (`<< EOF`).
- Não use `\n` em `sed` ou Python inline para não quebrar strings.
- O terminal quebra linhas longas ao colar: comandos Python longos podem falhar.

## 6. Fatos do ELF e Runtime (Cheat Sheet)

- **Pseudo-LBN:** `kCdPseudoLbnStart=0x00100000`. LBNs lidos (ex: `0x100038`) caem dentro de `GAMEDATA.WAD` (base `0x100011`).
- **Tabelas de Streaming:**
  - `0x54FB60`: Tabela do Estágio 5 (nosso alvo). 16 slots × `0x44`. Despachante em `func_413878`.
  - `0x53C504`: Tabela dos Estágios 3/7. 0x28 entradas. Despachante em `func_418178`.
- **Máquina de Estados (`func_378A30`):**
  - `0x755AB8`: Bit de ocupado.
  - `0x755ABC`: Estado (0 a 4).
  - `0x755AC8`: Ponteiro para o slot atual.
  - `0x592790`: Jump table (5 ponteiros).
- **Endereços de Thread:** `InitThread@0x480AF0`, `iWakeupThread@0x480BE0`, `StartThread@0x480D48`.
- **Códigos de Erro:** `KE_ILLEGAL_PRIORITY=-403`, `KE_UNKNOWN_THID`, etc. em `Syscalls/Helpers/State.h`.

## 7. Rotina de Build e Teste

**Build Incremental (Runtime editado):**
```fish
cd /run/media/valk/linux_arquivos/projetos/ShaolinMonks/PS2Recomp
cmake --build out/build -j2 --target ps2EntryRunner
```

**Execução e Captura de Log:**
```fish
timeout 60 script -qfc "./out/build/ps2xRuntime/ps2EntryRunner ../extraido_full/SLUS_210.87" runNN_raw.log
tr '\r' '\n' < runNN_raw.log | grep -a "run:tick" | tail -3 | cut -c1-1700
```
*Nota: Sempre usar `script` e `grep -a` para não perder o buffer do terminal.*

**Comandos Úteis:**
```fish
dis 0x413878 100                      # Despachante de streaming
dis 0x413788 100                      # Handler do slot
dis 0x378A30 150                      # Máquina de estados do loader
dis 0x378BF8 80                       # State 1 (Poll)
dis 0x2123C8 100                      # O poll em si
```

**Função `dis` (fish):**
```fish
function dis
    python3 -c 'import glob,re,sys; t=int(sys.argv[1],16); fs=[(int(re.search(r"sub_([0-9A-Fa-f]+)_",f).group(1),16),f) for f in glob.glob("/run/media/valk/linux_arquivos/projetos/ShaolinMonks/output/sub_*.cpp")]; s=max(x for x in fs if x[0]<=t); L=[l.rstrip()[:100] for l in open(s[1]) if l.startswith("    // 0x")]; print(s[1]); print("\n".join(L[:int(sys.argv[2])]))' $argv
end
```
```

**Próximo passo:** Rode o Passo 1 do "Plano de Ação" (`dis 0x2123C8 100` e `dis 0x378D38 80`), adicione os campos de telemetria (Passo 2) e me mande o log do **run23**. Assim que identificarmos exatamente qual flag o poll está esperando, nós criamos o fix!
