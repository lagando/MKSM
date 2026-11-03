# Guia de modificação do Mortal Kombat: Shaolin Monks (recompilado)

Tudo o que já sabemos e que serve para criar mods: personagens, fases, música, menus, vídeos e regras do jogo. O jogo agora roda como código C++ nativo, o que dá dois caminhos:

1. **Mudar dados:** arquivos do disco (`GAMEDATA.WAD`, filmes, sons).
2. **Mudar código:** substituir qualquer função do jogo por uma função C++ nossa, sem tocar no ELF.

Etiquetas usadas, iguais às do `PLANO.md`:
- **[medido]**: visto rodando.
- **[lido]**: lido no código ou nos dados.
- **[hipótese]**: ainda não confirmado.

Endereços são do **SLUS_210.87 (NTSC-U, v1.02)**; outras versões têm outros endereços.

> **Regra legal:** mods distribuem só patches, scripts, ferramentas e dados criados por você. Nunca o ISO, o ELF, arquivos extraídos (`extraido*/`) ou o C++ gerado (`output/`). Cada pessoa usa o próprio disco.

---

## 1. Como o jogo é carregado (onde entrar)

| Peça | Onde | O que é |
|---|---|---|
| ELF do jogo | `extraido_full/SLUS_210.87` | código do EE, recompilado em `output/sub_*.cpp` (um arquivo por função) |
| Raiz do "CD" | pasta do ELF (`extraido_full/`) | o runtime lê os arquivos **direto do disco do PC** [lido, `ps2_runtime.cpp`: `cdRoot = elfDirectory`] |
| Lista de arquivos | `FILELIST.DIR` | texto: `WADCRC.BIN`, `GAMEDATA.WAD`..`WAJ`, 8 filmes `.SFD` |
| Dados do jogo | `GAMEDATA.WAD` + `.WAE`..`.WAJ` | um arquivo lógico de ~2,8 GB partido em 7 pedaços de 400 MB |
| CRC dos dados | `WADCRC.BIN` (28 KB) | o jogo tem `FreeWadCRCBuffer` e `FreeTextCRCBuffer`; mexer no WAD pode exigir atualizar ou desligar a checagem [hipótese] |
| Filmes | `FRONT/MOVIES/*.SFD` | CRI Sofdec: MPEG + áudio ADX |
| Módulos IOP | `IOP/*.IRX` | áudio CRI ADX (`CRI_ADXI`), driver de som `SNDFI`, cartão, controle |
| Extras | `CLASSIC/MK2.SR` | o MK2 de arcade desbloqueável (ROMs dentro, ex. `mk2eerom.rom`) |
| Extras | `SURREAL/` | demo de *The Suffering 2* (outro ELF, `SLUS_211.89`) |

Como o CD é uma pasta, **trocar um arquivo é só sobrescrever o arquivo em `extraido_full/`**. Não precisa refazer ISO.

### Ferramentas do projeto

```bash
scripts/run.sh log.txt 120
```

```bash
cd PS2Recomp && MKSM_SKIP_INTRO=1 ./out/build/ps2xRuntime/ps2EntryRunner ../extraido_full/SLUS_210.87
```

- `MKSM_SKIP_INTRO=1`: pula os filmes de abertura (flag do próprio jogo em `0x513A04`).
- `MKSM_AUTO_PAD=xs`: aperta X/Start sozinho, para testes automáticos.
- `PS2X_SCREENSHOT_DIR=pasta`: grava a tela a cada 600 quadros.
- `PS2X_PROFILE=N`: espera N s, amostra 20 s e escreve `ps2_profile.txt`.
- Disassembly MIPS de qualquer endereço do ELF:

```bash
llvm-objdump -d --triple=mipsel --mcpu=mips3 --start-address=0x383560 --stop-address=0x383668 extraido_full/SLUS_210.87
```

- O código gerado traz o assembly original em comentário: `grep -n "0x2cd6dc" output/sub_*.cpp`. Endereços constantes já aparecem resolvidos (`FAST_WRITE32(0x4F6E98u, ...)`), então `grep 0x4F6E98u output/*.cpp` acha quem escreve num endereço fixo.

---

## 2. Mudar código: substituir funções do jogo

Esta é a ferramenta mais poderosa do mod. O runtime tem uma tabela "endereço do jogo → função C++". Dá para trocar qualquer entrada:

```cpp
// PS2Recomp/ps2xRuntime/src/lib/game_overrides_mksm.cpp
void sub_001D32E0_0x1d32e0(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime); // original

void minhaVersao(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    // antes: ler/alterar argumentos em ctx ($a0 = getRegU32(ctx, 4) ...)
    sub_001D32E0_0x1d32e0(rdram, ctx, runtime);   // chama o original (opcional)
    // depois: valor de retorno em $v0 (setReturnS32(ctx, x))
}

// dentro de applyMksmOverrides(PS2Runtime &runtime):
runtime.replaceFunction(0x1D32E0u, minhaVersao);
```

- Exemplo real: o `skipIntro` nesse mesmo arquivo liga a flag `0x513A04`, chama a função original e restaura a flag.
- `ps2_game_overrides::bindAddressHandler(runtime, endereço, "nomeDoStub")` liga um endereço a um stub HLE já existente (lista em `ps2_call_list.h`).
- `sm.toml` → `stubs = ["nome@0xENDERECO", ...]`: troca na hora de gerar o código. Exige rodar a recompilação de novo (`scripts/recomp.sh`).
- A RAM do PS2 é `rdram` (32 MB). Ler e escrever é `memcpy(rdram + endereço, ...)`.
- Convenção de chamada (EABI do PS2): argumentos em `$a0..$a3, $t0..$t3` (regs 4–11); do 9º em diante na pilha, em `sp+0`, `sp+8`, `sp+0x10` (8 bytes cada). Retorno em `$v0` (reg 2). Float em `$f12`/`$f0`.

**Cuidado:** a substituição vale para chamadas feitas por `jal`/ponteiro que passam pela tabela. Funções que o recompilador colocou inline dentro de outras não são afetadas.

### Achar quem escreve um valor (técnica usada no bloom)

Para descobrir quem grava um endereço:
1. Proteger a página do host (`mprotect(PROT_READ)` em `rdram + endereço`).
2. No `SIGSEGV`, logar o `RIP` e o backtrace, liberar a página e ligar o passo único (flag TF).
3. No `SIGTRAP`, proteger de novo.
4. `addr2line -f -e ps2EntryRunner <offset>` dá o nome `sub_XXXXXXXX`, ou seja, a função do jogo.

O código completo desta técnica está na seção 5A.16 do `PLANO.md` (foi removido do runtime depois do uso).

---

## 3. Símbolos e endereços conhecidos

### Depuração e trapaças (o jogo ainda tem o código de debug)

| Nome | Endereço | Notas |
|---|---|---|
| `gGodMode` | `0x4C7778` | global; `GodModeToggle` `0x1AAAD8`, `SetGodModeText` `0x1AAE10` (textos `0x666`/`0x667`) [lido, OGmidway] |
| `G_Free_Camera` | `0x511378` | câmera livre; `SelectFreeCam` `0x1AA558`, `SelectFreezeCam` `0x1AA5B8` |
| `DebugMenuSetup` / `DebugMenuUpdate` | `0x1AAE80` / `0x1A88D8` | menu de debug original; `DebugMenuWin` `0x4C73A0` |
| `gMenuSystem` | `0x5D1DE0` | sistema de menus; `SelectWindowId` `0x5D2724`, `ActiveWindowId` `0x5D2728` |
| `gObjectNum` | `0x50C134` | contagem de objetos |
| `gTest_no_particle_update` | `0x6BEE10` | desliga partículas |
| Tamanho do jogador | `0x33C0` bytes por jogador | [lido, OGmidway] |

Strings de menus de debug presentes no ELF: `Debug Menu`, `Level Select` (`0x474A30`), `Warp Player Menu` (`0x478F50`), `Fatality Level Menu`, `Moves List Menu`, `setfatalitylevel`. Um mod de "seletor de fase" pode reaproveitar o `Level Select` original em vez de criar menu do zero. Ele ainda não foi ativado e testado [hipótese].

### Memória, arquivos e carga

| Nome | Endereço |
|---|---|
| `Memory_Initialise` / `Memory_AllocBlock` / `Memory_SetBlock` | `0x20ECD0` / `0x20F058` / `0x210B28` |
| `alloc_blocks` (128 × 104 bytes) | `0x64E998` |
| `StartAsyncLoadFile` / `AsyncLoadStatus` / `GetAsyncFileSize` | `0x2123A0` / `0x2123C8` / `0x211878` |
| `Async_DataLoad` (máquina de carga, pedidos de 52 bytes) | `0x378A30` |
| `async_load_info` / `async_info` / `file_info` | `0x4EF7E0` / `0x755AB8` / `0x651E30` |
| `cPxFileInterface_*` (carga/status/fecha) | `0x370A70` / `0x370A98` / `0x370AE8` |
| Handle CRI ADXF (16 slots × `0x44`) | `0x54FB60` |
| Descompressão EWDF dos itens do WAD | `0x466690`, `0x4667F8` [lido, OGmidway] |
| Registro de callbacks de carga | `0x379078` (56 callbacks constantes; `tests/find-async-callbacks.py` no OGmidway) |

### Vídeo e GS

| Coisa | Valor [medido] |
|---|---|
| Resolução de desenho | 640×224 por campo (entrelaçado); XYOFFSET da cena 1728,1936 |
| Framebuffers | página `0x00` e `0x46` (CT32, largura 640), alternando |
| Z-buffer | página `0x8C`, Z24, teste `GREATER` (maior = mais perto) |
| Rascunho do bloom | mesma página `0x8C`, 320×128 (XYOFFSET 1888,1992) |
| Cópias de profundidade | `0x12C` (meia resolução) e `0x14C` |
| Parâmetros do bloom | `0x4F6E70`: `+0x1C` ganho das passadas, `+0x28` limiar (`0xFF` = bloom desligado) |
| Configuração visual da fase | `0x1E5858` copia 0x2C bytes do arquivo da fase para `0x4F6E70` |
| Efeito "flash" | `0x2CD700` (liga) / `0x2CD6C0` (restaura) / `0x2CD830` (rampa) |
| Ambiente de desenho + clear por quadro | `sceGsPutDrawEnv` `0x383668`, montado por `sceGsSetDefClear` `0x383560` |

Mod fácil: **intensidade do bloom**. Gravar outro valor em `0x4F6E70+0x28` depois que a fase carrega (por exemplo, substituindo `0x1E5858` por uma versão que chama a original e corrige o valor). `0xFF` desliga o bloom; valores maiores cortam mais o escuro e deixam só o brilho forte.

---

## 4. Fases

- **Nomes e IDs:** a tabela de mapas fica em `0x480910..`, com "This is map N" e o nome. Exemplos: 176 = `Goro's Lair`, 172 = `The Pit`, 173 = `The Dead Pool`, 177 = `The Tower`, 178 = `The Living Forest`, 181 = `The Ice Falls`, 182 = `Kahn's Arena`, 183 = `Wu-shi Arena`, 192 = `Evil Monastery - Map 1`, mais os mapas de Survival/Vs/Practice [lido].
- **Carga:** cada fase é um conjunto de entradas do WAD. O jogo processa por tipo: `ProcessLoadedRegionWad`, `ProcessLoadedObjectTextureWad`, `ProcessLoadedWuppetsWad`, `New_Particle_Add_Subwad_To_Globals` (nomes no ELF) [lido].
- **Áudio da fase:** `InitLevelAmbientSounds` e os bancos `SOUND_BANK_ARENA`, `SOUND_BANK_ARENA_MUSIC` e `SOUND_BANK_ARENA_VO`.
- **Para fazer:** mapear o ID do mapa → entradas do WAD. O caminho é logar os pedidos de `Async_DataLoad` (ID do arquivo = índice no WAD + 30) enquanto cada fase carrega. Com isso dá para trocar texturas e regiões de uma fase, ou apontar um mapa para os dados de outro.
- **Fase nova de verdade:** precisa de um exportador para o formato de região/objeto do engine Paradox, que ainda não foi estudado. Começar trocando texturas (mais simples) e só depois geometria.

## 5. Personagens

- **Strings:** `scorpion`, `liu_drgn`, `liu1_fat`/`liu2_fat` (fatalities), `fnd1_jax`, `kitanaDone`/`kitanafree`, `DoBarakaMonkStr`, `Baraka Battle`, `Goro Battle`, `Reptile Battle`, `Scorpion Battle` [lido].
- **Som por personagem:** `SOUND_BANK_PLAYER_1/2`, `SOUND_BANK_ENEMY_1..4`, `SOUND_BANK_SCORPION`, `SOUND_BANK_CHAR`.
- **Estrutura do jogador:** `0x33C0` bytes por jogador. Os campos ainda não foram mapeados. Primeiro passo: achar o ponteiro do jogador 1 e diferenciar a estrutura ao perder vida, pular e trocar de golpe.
- **Personagem jogável novo:** o caminho realista é **substituir** um existente (modelo, texturas, som e golpes), não adicionar um slot. Adicionar slot exige mexer na seleção (`Character Select`), nos bancos de som e nas tabelas de golpes. Fazer isso por código C++ (seção 2) é mais fácil que editar o ELF.
- A seleção de personagens já funciona no runtime (Liu Kang / Kung Lao) [medido].

## 6. Música e som

- **Música:** CRI **AIX** (ADX intercalado com várias faixas; `AIXP_GetNumSong`, `Ps2/Cri/Music.cpp`, `InitMusicPlayback`/`DeInitMusicPlayback`). Opções do jogo: `musicenable`, `musicintensity`, `musicoverride` [lido]. A música muda de "intensidade" em combate, o que explica as várias faixas no mesmo AIX.
- **Efeitos e vozes:** driver `SNDFI.IRX` (RPC `0x53465356`/`0x534E4446`), com bancos `SOUND_BANK_*` (17 tipos) dentro do WAD.
- **Trocar música, opção A (dados):** gerar um ADX/AIX com as mesmas faixas e taxa e substituir a entrada no WAD. Isso exige o empacotador do WAD (seção 8) e talvez o `WADCRC.BIN`.
- **Trocar música, opção B (código, mais fácil):** no runtime, o áudio CRI passa pelo nosso IOP HLE (`ps2xIOP`, perfil `mk_cri`). Dá para interceptar o pedido de música e tocar um `.ogg`/`.flac` do PC no lugar, sem mexer em nenhum arquivo do jogo.
- **Estado atual:** o áudio do runtime ainda não foi validado (ver `PLANO.md`). Faça mods de música só depois que o áudio original tocar certo.

## 7. Menus e interface

- **Engine dos menus:** Flash próprio da Paradox (`Paradox/Flash/AnimMenuGUI.cpp`, `AnimMenuMan.cpp`). Os menus são animações com texto, carregadas do WAD.
- **Telas conhecidas:** `FE Menu`, `Frontend Menu`, `Load Menu`, `Save Menu`, `Start Button Menu`, `Select Button Menu`, `Moves List Menu`, `VS Match Over Menu`, `Pause` (`GameDisplay/PauseMenu.cpp`), `Konten` (`fe_MainOptionKotentMenu.cpp`).
- **Textos:** procurados por ID e CRC (`FreeTextCRCBuffer`; god mode usa os IDs `0x666`/`0x667`). Uma tradução PT-BR pode trocar a tabela de textos do WAD ou interceptar a função que devolve o texto por ID. Esta segunda forma é mais segura e não mexe nos dados.
- **Melhorias fora do jogo (no runtime):** a janela já redimensiona; ainda dá para fazer filtro de escala, widescreen e um menu de opções do runtime (o `ps2_debug_panel.cpp` já tem uma UI ImGui que serve de base).
- **Widescreen:** precisa mudar a projeção da câmera no código do jogo e o corte no GS. Ainda não foi mapeado.

## 8. O arquivo `GAMEDATA.WAD`

Formato "PWF " [lido; parte vem da decodificação do OGmidway, conferida aqui]:

```
0x00  "PWF "                 assinatura
0x04  0x804F8000             (flags/versão, não decodificado)
0x08  2                      (versão?)
0x0C  7139                   número de entradas
0x10  0x19000000             tamanho de cada pedaço (419.430.400 bytes: WAD, WAE, ..., WAJ)
0x14  0x800                  tamanho do setor
0x20  entrada[7139]          8 bytes cada; ID do arquivo = índice + 30
```

- **Entrada comprimida (bit 63 = 1), 2959 entradas** [conferida na entrada 35, a do bug do `BGEZ`]:
  - bits 0–21: setor do início (× 0x800 = offset no arquivo lógico);
  - bits 22–34: tamanho comprimido;
  - bits 41–62: tamanho de saída alinhado.
  - O payload é **EWDF**, descomprimido por `0x466690`/`0x4667F8`; não é zlib puro.
  - Ex.: ID 35 = `0x803AA0026D400028` → offset `0x14000`, 2485 bytes comprimidos, saída `0x1D50`.
  - Os offsets crescem em ordem, o que confirma o campo de setor.
  - **Atenção:** algumas entradas têm razão de compressão alta demais (ex. 6376 → 727049 bytes). A unidade do tamanho comprimido pode ser outra em parte dos casos [hipótese].
- **Entrada simples (bit 63 = 0), 4180 entradas** [lido, plausível]: 32 bits baixos = offset em bytes; 32 bits altos = tamanho em bytes. Ex.: `0x1000000A7800` → `0xA7800`, 4096 bytes.
- O offset lógico ÷ `0x19000000` dá o pedaço (0 = `.WAD`, 1 = `.WAE`, …) e o resto dá a posição dentro dele.
- **Ferramenta a criar:** `scripts/wad.py` com `list` (tabela), `extract ID` (com descompressão EWDF, reimplementando `0x466690`) e `replace ID arquivo`. O `replace` acrescenta no fim e regrava a entrada, assim não precisa refazer o arquivo inteiro. Depois disso, testar se o `WADCRC.BIN` reclama.

## 9. Vídeos

- **Arquivos:** `FRONT/MOVIES/*.SFD` (CRI Sofdec): `MIDWAY`, `ESRB`, `OPENING`, `BLITZ`, `GAUNTLET`, `END006`, `END011`, `FINAL`.
- **Trocar um vídeo:** gerar um SFD (MPEG-1/2 + ADX) e sobrescrever o arquivo; o FFmpeg lê SFD, mas para *gerar* precisa das ferramentas CRI ou de um muxer próprio.
- **Alternativa mais simples:** interceptar no runtime e tocar um `.mp4` do PC.
- **Estado:** o filme ainda não toca no nosso runtime (`PLANO.md`, seção 5A.5).

---

## 10. Ordem sugerida para começar a modificar

1. **Trapaças e debug** (só código, risco baixo): god mode (`0x4C7778`), câmera livre, ativar o `Level Select` e o `Warp Player` originais.
2. **Ajustes visuais:** bloom (`0x4F6E70+0x28`), filtro de escala, depois widescreen.
3. **Ferramenta do WAD** (`list`/`extract`/`replace`), testando primeiro com uma textura de menu.
4. **Tradução PT-BR** interceptando a busca de texto por ID.
5. **Música trocada pelo runtime** (opção B da seção 6), quando o áudio original estiver validado.
6. **Trocar um personagem** (texturas → modelo → som → golpes), mapeando a estrutura de `0x33C0` bytes.
7. **Fases:** primeiro trocar texturas e dados de uma fase existente; criar fase nova só com o formato de região entendido.

## 11. Referências

- `PLANO.md`: histórico técnico completo; a seção 5A cobre a fase 3D, o GS e o bloom.
- `README.md`: estado atual e o protocolo CRI ADXF.
- OGmidway/mksm-recomp: `games/mk-shaolin-monks/ghidra/verified-symbols.tsv` (símbolos conferidos por hash), `docs/PROTOTYPE.md` (protótipo, WAD e debug) e `docs/SFSV-RPC.md` (som).
- PCSX2 GameDB, `SLUS-21087`: `autoFlush: 2` (bloom) e `halfPixelOffset: 5`. Indica o que o hardware real faz de diferente.
