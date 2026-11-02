
```markdown
# 🗺️ ShaolinMonks: Plano de Investigação e Diário de Bordo

**Estado:** 01/11/2026 (Noite, após `run24`).  
Este é o documento técnico detalhado (diário de bordo e engenharia reversa). O `README.md` é o resumo de status para o repositório.  
*Convenção:* **[medido]** = visto em execução no log, **[lido]** = lido no código C++ gerado, **[memória]** = conhecimento arquitetural sobre o SDK do PS2.

---

## 1. Contexto e Objetivo Atual

Estamos portando Mortal Kombat: Shaolin Monks (PS2) usando recompilação estática via PS2Recomp. A CPU roda nativa em C++, mas o hardware auxiliar (GS, VU, DMA, IOP) ainda é emulado. 
O objetivo atual é **passar da tela de loading para o menu principal**. O jogo estava travando num *polling* de CD, e após nossa intervenção no HLE, avançou para fazer leituras reais, mas agora sofre um *spinloop* irreversível ao tentar processar o carregamento do `GAMEDATA.WAD`.

---

## 2. Estado das Fases Anteriores

| Fase | Status | Detalhe |
|---|:---:|---|
| Bootstrap & ELF | ✅ | ISO extraída, 13.651 arquivos C++ gerados a partir do executável. |
| Tela Magenta | ✅ | `PMODE` e `sceGsDispEnv` corrigidos no emulador do GS. |
| CDVD Bind | ✅ | 10 binds HLE (`mksm-cdvd`) substituem as chamadas de libcdvd do guest. |
| Threads | ✅ | `CreateThread` com prio 0 retornava -403. Fix em `EeScheduler.cpp:426` (`< 0` em vez de `< 1`). 5 threads ativas no guest. |
| Leitura de CD | ⚠️ | O backend HLE lê os setores (`FILELIST.DIR`, `.SFD`, `.WAD`), mas a injeção do callback de conclusão corrompe a sincronia da thread. |

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

### 3.2 O Objeto de Slot de Leitura (`0x54FB60`)
O despachante avalia as flags neste bloco de memória (tamanho `0x44` por slot):
- `obj[1]`: Status da Leitura (`1` = Concluído, `2` = Lendo).
- `obj[3]`: **Flag de Processamento / Sinal Verde** (O ponto crítico de injeção HLE).
- `obj[8]`: Ponteiro para o Callback pendente.
- `obj[0x14]`: Cursor de escrita no buffer.

---

## 4. A Evolução do Bloqueio: Do "Run22" ao "Run24"

### O Bloqueio Original (Run 22)
No Run22, a máquina ficava eternamente presa no **Estado 1**. O HLE terminava a leitura (colocando `status = 1`), mas o poll `func_2123C8` nunca retornava sucesso (0) ou erro (-1). 

### O "Hack Agressivo" e o Spinloop (Run 24)
Para forçar a saída do Estado 1, tentamos injetar o sinal verde (`obj[3] = 1`) diretamente no HLE (`game_overrides_mksm.cpp`) em todos os frames.
* **Avanço:** A máquina destravou. O log mostrou o jogo lendo com sucesso a tabela `FILELIST.DIR`, setores do `GAMEDATA.WAD` e filmes da pasta `FRONT\MOVIES`.
* **Corrupção:** Como a injeção foi a cada frame (sem checar se o sistema estava pronto), atropelamos o fluxo. O cursor de escrita (`obj[0x14]`) pulou para `75777` (lixo).
* **Queda:** O jogo entrou num **spinloop eterno**, com a thread principal (PC) travada em `0x379220`, esperando uma flag que nunca vai chegar. O VSync não congela, mas o Guest entra em *deadlock*.

---

## 5. Plano de Ação Imediato

Precisamos de duas ações coordenadas para resolver a sincronia perfeita do CDVD:

### Passo 1: Dissecando o Spinloop (Engenharia Reversa)
Precisamos ver o que a CPU está executando no endereço corrompido para saber o que travou a thread.
```fish
# Executar na raiz do projeto:
dis 0x379220 100

```

*Objetivo:* Descobrir qual registrador ou endereço de memória a instrução em `0x379220` testa repetidamente.

### Passo 2: O Hack Cirúrgico HLE

Implementar um simulador de `sceCdCallback` preciso. Ele só deve injetar o sinal verde quando o jogo estiver ativamente lendo, garantindo a integridade do cursor de dados.

```cpp
// Em game_overrides_mksm.cpp, no ciclo de update do HLE:
uint32_t table_base = 0x54FB60u;
for (int i = 0; i < 16; ++i) {
    uint32_t slot = table_base + (i * 0x44u);
    if (m_memory.read8(slot) == 1) { 
        uint8_t slot_status = m_memory.read8(slot + 1);
        uint8_t cb_flag = m_memory.read8(slot + 3);
        
        // INJEÇÃO CIRÚRGICA: Apenas se status for 2 (Lendo) e cb_flag for 0 (Aguardando)
        if (slot_status == 2 && cb_flag == 0) {
            m_memory.write8(slot + 3, 1);
        }
    }
}

```

### Passo 3: Telemetria de Segurança (Opcional)

Se o Passo 2 não resolver sozinho, precisaremos reincluir as flags `busyF4`, `busyF1C`, `busyF20`, e `busyF30` na rotina de log do `ps2_runtime.cpp` para entender a estrutura de memória estendida que o despachante espera.

---

## 6. Fatos do ELF e Runtime (Cheat Sheet)

* **Pseudo-LBN:** `kCdPseudoLbnStart=0x00100000`. LBNs lidos (ex: `0x100038`) caem dentro de `GAMEDATA.WAD` (base `0x100011`).
* **Tabelas de Streaming:**
* `0x54FB60`: Tabela do Estágio 5 (nosso alvo). 16 slots × `0x44`. Despachante em `func_413878`.
* `0x53C504`: Tabela dos Estágios 3/7. 0x28 entradas. Despachante em `func_418178`.


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
timeout 60 script -qfc "./out/build/ps2xRuntime/ps2EntryRunner ../extraido_full/SLUS_210.87" run_raw.log
tr '\r' '\n' < run_raw.log | grep -a "run:tick" | tail -3 | cut -c1-1700

```

*Sempre usar `script` e `grep -a` para não perder o buffer do terminal com saídas longas.*

**Função `dis` (Adicione no ~/.config/fish/functions/dis.fish):**

```fish
function dis
    python3 -c 'import glob,re,sys; t=int(sys.argv[1],16); fs=[(int(re.search(r"sub_([0-9A-Fa-f]+)_",f).group(1),16),f) for f in glob.glob("/run/media/valk/linux_arquivos/projetos/ShaolinMonks/output/sub_*.cpp")]; s=max(x for x in fs if x[0]<=t); L=[l.rstrip()[:100] for l in open(s[1]) if l.startswith("    // 0x")]; print(s[1]); print("\n".join(L[:int(sys.argv[2])]))' $argv
end

```

```

<FollowUp> Com essa versão completa documentada, pode rodar o `dis 0x379220 100` e colar a saída do assembly aqui para descobrirmos quem está prendendo essa thread? </FollowUp>

```
