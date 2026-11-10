# Contributing / Como contribuir

**English** below · [Português](#português)

## English

Thanks for helping. This is a reverse-engineering and systems project, so the bar is evidence: say what you measured, what you changed and how you checked it.

### Ground rules

1. **Never commit game data.** No ISO, ELF, `extraido*/`, `output/` (generated C++), `gsrec/` recordings, memory dumps, VU microprogram dumps, screenshots of copyrighted assets beyond what an issue needs, logs, or binaries. `.gitignore` covers the usual paths; check `git status` before every commit.
2. **`PS2Recomp/` is never committed.** It is a clone made by `scripts/aplicar_patches.sh`. Work inside it, then run `scripts/atualizar_patches.sh` so `patches/` carries your change. A new source file must also be listed in `patches/arquivos.txt` (the script warns you).
3. **Never `git checkout`/`git restore` files inside `PS2Recomp/`**: your work exists only in that tree and in `patches/`.
4. **Keep the renderer exact.** The GPU paths must produce the same VRAM, byte for byte, as the CPU renderer. Run the replay check below.
5. **Keep the game thread deterministic.** Changes on the EE/IOP side must not depend on host timing (online lockstep depends on it).
6. **Update the docs in the same commit.** If you change threads, locks, the path of a frame or an interface, or find a new game trap, update the right section of [`ARQUITETURA.md`](ARQUITETURA.md). Fixes with their evidence go to [`PLANO.md`](PLANO.md); performance work to [`PLANO_DESEMPENHO.md`](PLANO_DESEMPENHO.md).
7. **Remove temporary diagnostics** (counters, prints) before committing.

### Setup

```bash
scripts/instalar.sh "path/to/your.iso"   # first time: hours
```

Rebuild after a change:

```bash
cmake --build PS2Recomp/out/build --target ps2EntryRunner
```

Avoid editing headers included by `ps2xRuntime/include/ps2_runtime.h`: they force a rebuild of the 13,000 generated files. Prefer `.cpp` files and headers only the runtime uses.

### Testing without booting the game

- Unit tests (run suites by name; `memory` aborts on an old scheduler assert):

  ```bash
  PS2Recomp/out/build/ps2xTest/ps2x_tests gs
  PS2Recomp/out/build/ps2xTest/ps2x_tests vu1
  ```

  Known baseline: `gs` 44 passed / 33 failed (the same failures with and without the GPU), `vu1` 42/0, `vu` 44/0. A change must not add failures.
- **GS replays.** Record your own frames from the game (`PS2X_GS_RECORD`, see [`ARQUITETURA.md`](ARQUITETURA.md) §6; recordings contain game data, keep them local in `gsrec/`) and compare GPU against CPU:

  ```bash
  PS2X_GS_GPU=0 PS2Recomp/out/build/ps2xTest/ps2x_gs_replay gsrec/x.gsr --dump-vram /tmp/cpu.bin
  PS2Recomp/out/build/ps2xTest/ps2x_gs_replay gsrec/x.gsr --compare /tmp/cpu.bin
  ```

  `--repeat N --sync 0` measures throughput like the running game.
- Live: `PS2X_PERF=1` prints fps and load; `PS2X_PROFILE=<s> PS2X_PROFILE_THREAD=<name>` writes a sampling profile.

### Pull requests

- One topic per PR. Describe the problem, the evidence (log lines, replay hashes, measurements) and how you verified the fix.
- Before/after numbers for performance changes, measured on an idle machine.
- Commit messages may be in English or Portuguese.
- Using AI assistants is fine; you are responsible for what you submit. Instructions for AI agents working on this repo are in [`AGENTS.md`](AGENTS.md).

### Good first areas

See the open items in [`PLANO_DESEMPENHO.md`](PLANO_DESEMPENHO.md) and the ❌ rows in the README: CRI movies, the light rays in Goro's Lair, testing other stages, a Windows build, IOP interpreter speed.

### Conduct

Be respectful and assume good faith. Harassment is not tolerated. Do not ask for or share ways to obtain the game illegally.

---

## Português

Obrigado por ajudar. Este é um projeto de engenharia reversa e de sistemas, então a régua é evidência: diga o que você mediu, o que mudou e como conferiu.

### Regras

1. **Nunca envie dados do jogo.** Nada de ISO, ELF, `extraido*/`, `output/` (C++ gerado), gravações em `gsrec/`, dumps de memória, dumps de microprogramas da VU, capturas de arquivos do jogo além do que uma issue precisa, logs ou binários. O `.gitignore` cobre os caminhos comuns; confira o `git status` antes de cada commit.
2. **O `PS2Recomp/` nunca é commitado.** Ele é um clone feito pelo `scripts/aplicar_patches.sh`. Trabalhe nele e depois rode `scripts/atualizar_patches.sh` para que `patches/` leve a sua mudança. Arquivo de código novo também precisa entrar em `patches/arquivos.txt` (o script avisa).
3. **Nunca rode `git checkout`/`git restore` em arquivos do `PS2Recomp/`**: o seu trabalho só existe naquela árvore e em `patches/`.
4. **Mantenha o renderizador exato.** Os caminhos da GPU precisam dar a mesma VRAM, byte a byte, que o renderizador da CPU. Rode a conferência de replay abaixo.
5. **Mantenha a thread do jogo determinística.** Mudanças do lado da EE/IOP não podem depender do tempo do host (o online em lockstep depende disso).
6. **Atualize a documentação no mesmo commit.** Se mudar threads, locks, o caminho de um quadro ou uma interface, ou descobrir uma armadilha nova do jogo, atualize a seção certa do [`ARQUITETURA.md`](ARQUITETURA.md). Correções com evidência vão para o [`PLANO.md`](PLANO.md); trabalho de desempenho, para o [`PLANO_DESEMPENHO.md`](PLANO_DESEMPENHO.md).
7. **Tire os diagnósticos temporários** (contadores, prints) antes do commit.

### Montar o ambiente

```bash
scripts/instalar.sh "caminho/da/sua.iso"   # primeira vez: horas
```

Recompilar depois de uma mudança:

```bash
cmake --build PS2Recomp/out/build --target ps2EntryRunner
```

Evite mexer nos headers incluídos por `ps2xRuntime/include/ps2_runtime.h`: eles forçam recompilar os 13 mil arquivos gerados. Prefira `.cpp` e headers que só o runtime usa.

### Testar sem bootar o jogo

- Testes (rode as suítes por nome; a `memory` aborta num assert antigo do scheduler):

  ```bash
  PS2Recomp/out/build/ps2xTest/ps2x_tests gs
  PS2Recomp/out/build/ps2xTest/ps2x_tests vu1
  ```

  Linha de base: `gs` 44 passam / 33 falham (as mesmas falhas com e sem GPU), `vu1` 42/0, `vu` 44/0. Uma mudança não pode acrescentar falhas.
- **Replays do GS.** Grave seus próprios quadros do jogo (`PS2X_GS_RECORD`, ver [`ARQUITETURA.md`](ARQUITETURA.md) §6; gravações contêm dados do jogo, deixe-as locais em `gsrec/`) e compare GPU com CPU:

  ```bash
  PS2X_GS_GPU=0 PS2Recomp/out/build/ps2xTest/ps2x_gs_replay gsrec/x.gsr --dump-vram /tmp/cpu.bin
  PS2Recomp/out/build/ps2xTest/ps2x_gs_replay gsrec/x.gsr --compare /tmp/cpu.bin
  ```

  `--repeat N --sync 0` mede vazão como no jogo rodando.
- Ao vivo: `PS2X_PERF=1` mostra fps e carga; `PS2X_PROFILE=<s> PS2X_PROFILE_THREAD=<nome>` grava um perfil por amostragem.

### Pull requests

- Um assunto por PR. Descreva o problema, a evidência (linhas de log, hashes de replay, medições) e como você conferiu a correção.
- Números de antes e depois para mudanças de desempenho, medidos com a máquina livre.
- Mensagens de commit em português ou inglês.
- Usar assistentes de IA é permitido; você responde pelo que enviar. As instruções para agentes de IA neste repositório estão no [`AGENTS.md`](AGENTS.md).

### Boas áreas para começar

Veja os itens abertos no [`PLANO_DESEMPENHO.md`](PLANO_DESEMPENHO.md) e as linhas ❌ do README: filmes CRI, os raios de luz no Goro's Lair, testar outras fases, build para Windows, velocidade do interpretador da IOP.

### Conduta

Seja respeitoso e presuma boa-fé. Assédio não é tolerado. Não peça nem compartilhe formas de obter o jogo ilegalmente.
