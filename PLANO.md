
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

## 5. Plano de Ação Imediato (run26)

### Passo 1: Trace de chamadas do ADXF
Pendurar em `dispatchGuestBranch` (ou num log dedicado) as chamadas a `0x413268`, `0x413690`, `0x413128`/`0x4130F8` e `0x412BA0`/`0x413208`, imprimindo `a0..a3`, `$v0` de retorno e `handle[1]` depois da chamada.

*Objetivo:* descobrir **quem** chama `adxf_Stop` no ciclo ruim e com que tamanho de pedido (`func_26F640`), para separar a hipótese "read falhou" da hipótese "callback parou o handle".

### Passo 2: Conferir o total de setores
`[0x678440]` (`total`) era lixo no run24 (`4293920598`), o que estraga `pct` (`[0x678448]`) e pode confundir o próprio jogo. Ver quem grava esse valor (candidato: cabeçalho do `GAMEDATA.WAD` / `FILELIST.DIR`).

### Passo 3: Fix A (fiel ao SDK) — erro em vez de silêncio
Quando `adxf_ReadNw` for parado/falhar, deixar `handle[1] = 4` (READERR) em vez de `1`. O loader então sai do estado 2 com `-1` e o jogo segue o fluxo de erro, em vez do deadlock.

### Passo 4: Fix B (injeção mais restrita)
Só injetar `handle[3] = 1` quando o handle tiver `stm` válido, `status == 2` **e** callback pendente (`handle[8] != 0`). Hoje o runtime dispara o despachante mesmo em handle parado.

### Passo 5: Build + run26 e comparar
`[run:trans]` já mostra a ordem dos eventos; o critério de sucesso é o `status` nunca terminar em `1` com `loopState == 2`.

---

## 6. Fatos do ELF e Runtime (Cheat Sheet)

* **Pseudo-LBN:** `kCdPseudoLbnStart=0x00100000`. LBNs lidos (ex: `0x100038`) caem dentro de `GAMEDATA.WAD` (base `0x100011`).
* **Tabelas de Streaming:**
* `0x54FB60`: Tabela do Estágio 5 (nosso alvo). 16 slots × `0x44`. Despachante em `func_413878`.
* `0x53C504`: Tabela dos Estágios 3/7. 0x28 entradas. Despachante em `func_418178`.

* **Handle CRI ADXF:** `0x54FB60` (+`i*0x44`). Status: `1`=STOP, `2`=READING, `3`=READEND, `4`=READERR. `[+0x20]` = setores da última transferência, `[+0xC]` = total, `[+0x14]` = cursor, `[+0x04]` = `stm`, `[+0x08]` = callback pendente.
* **Funções ADXF:** `413BE8` GetStat, `413B48` GetNumReadSct, `413268` ReadNw, `413690` Stop, `413128`/`4130F8` fecha handle. Wrappers do jogo: `26F640` (read), `26F6E0` (stat + pct de progresso), `26F790` (close).
* **Endereços de Thread:** `InitThread@0x480AF0`, `iWakeupThread@0x480BE0`, `StartThread@0x480D48`.
* **Códigos de Erro:** `KE_ILLEGAL_PRIORITY=-403`, `KE_UNKNOWN_THID`, etc. (Ver `Syscalls/Helpers/State.h`).

---

## 7. Armadilhas Conhecidas do Ambiente e Código

### 7.1 A Macro `PS2_IF_AGRESSIVE_LOGS`

Macro localizada em `ps2_log.h:189`:

```c
#define PS2_IF_AGRESSIVE_LOGS(code) /* if(AGRESSIVE_LOGS) { code; } */

```

**ATENÇÃO:** Esta macro aceita apenas **1 argumento**.

* NUNCA coloque arrays inicializados com vírgulas: `uint32_t jt[5] = {0, 0, 0, 0, 0};` quebra o build (o pré-processador interpreta as vírgulas como 5 argumentos).
* Use o construtor vazio: `uint32_t jt[5]{};`.
* Declarações de variáveis, blocos `for` e `if` devem ficar **antes** da chamada principal do `RUNTIME_LOG(...)`, nunca embutidos nela.

### 7.2 Fish Shell Tricks

* Não use heredocs (`<< EOF`) para passar blocos de código.
* Não use `\n` em `sed` ou Python inline via terminal (pode quebrar strings em multilinhas indesejadas no C++).
* O terminal do KDE quebra linhas longas ao colar; scripts de uma linha em Python podem falhar.

---

## 8. Rotina de Build e Teste

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
