# Instruções para IAs neste projeto

Recompilação estática do Mortal Kombat: Shaolin Monks (PS2, SLUS_210.87) com PS2Recomp.

## Leia antes de mexer

1. **[`ARQUITETURA.md`](ARQUITETURA.md):** threads, locks, fluxo de um quadro, tempo/vsync, armadilhas do jogo, como testar sem bootar o jogo. Comece por aqui; não redescubra o que já está escrito.
2. **[`PLANO_DESEMPENHO.md`](PLANO_DESEMPENHO.md):** o plano de execução. Leia as seções 1 a 4 (regras, mapa, rotinas com comandos prontos, armadilhas) e faça as tarefas da seção 5 **na ordem, uma por vez**, seguindo o formato de cada uma (o que fazer, como verificar, quando está pronta). Marque `[x]` e anote os números medidos na tarefa ao terminar.
3. **[`PLANO.md`](PLANO.md):** a história de cada correção com evidência (seção 5A = fase 3D, GS, bloom).
4. **[`MODDING.md`](MODDING.md):** endereços e formatos do jogo.

**Mantenha o `ARQUITETURA.md` atualizado:** se você mudar threads, locks, o caminho de um quadro, interfaces ou descobrir uma armadilha nova, atualize a seção certa no mesmo commit.

## Regras

- **Commits e PRs:** só o usuário é autor. Nunca adicione `Co-Authored-By`, "Generated with Claude Code" ou qualquer atribuição a IA.
- **Legal:** nunca envie ao git o ISO, o ELF, `extraido*/`, `output/` (C++ gerado), binários, logs ou `gsrec/` (gravações com dados do jogo).
- **`PS2Recomp/` não é commitado diretamente:**
  - Rode `scripts/atualizar_patches.sh`: regenera `patches/runtime_atual.diff` e copia os arquivos novos listados em `patches/arquivos.txt`.
  - Arquivo novo entra em `patches/arquivos.txt` (o script avisa os que faltam).
  - Nunca rode `git checkout`/`git restore` em arquivos do `PS2Recomp`: o trabalho só existe na árvore e no patch.
- **Headers incluídos por `ps2xRuntime/include/ps2_runtime.h`** forçam recompilar o código gerado (13 mil arquivos, demora). Prefira `.cpp` e headers que só o runtime usa.
- **Diagnósticos temporários** (logs, contadores) saem antes do commit.
- **Shell:** o padrão é zsh/fish e não divide variáveis em palavras. Use `bash -c '...'` para laços com listas.

## Comandos

```bash
cmake --build PS2Recomp/out/build --target ps2EntryRunner
```

```bash
PS2Recomp/out/build/ps2xTest/ps2x_tests gs
```

```bash
PS2Recomp/out/build/ps2xTest/ps2x_gs_replay gsrec/menu.gsr --repeat 2
```

```bash
cd PS2Recomp && MKSM_SKIP_INTRO=1 MKSM_AUTO_PAD=xs ./out/build/ps2xRuntime/ps2EntryRunner ../extraido_full/SLUS_210.87
```

- `ps2x_tests` sem argumentos roda todas as suítes. A suíte `memory` aborta num assert antigo do `EeScheduler`; rode as suítes por nome.
- `ps2x_gs_replay` reproduz quadros gravados sem o jogo; ver `ARQUITETURA.md` §6.
- Para rodar o jogo, a sessão gráfica precisa estar desbloqueada: com a tela travada, a janela não abre.
