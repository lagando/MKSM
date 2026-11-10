# patches/

Our changes to [PS2Recomp](https://github.com/ran-j/PS2Recomp) at the commit in [`UPSTREAM_COMMIT`](UPSTREAM_COMMIT). / Nossas mudanças no PS2Recomp no commit de `UPSTREAM_COMMIT`.

| File / Arquivo | What / O quê |
|---|---|
| `runtime_atual.diff` | Every change to tracked PS2Recomp files (runtime, IOP, recompiler, tests), except the generated `ps2xRuntime/src/runner/`. |
| [`arquivos.txt`](arquivos.txt) | New files (untracked upstream) and where each one goes. / Arquivos novos e o destino de cada um. |
| the other `.cpp/.h/.comp/.vert` | Those new files. / Os arquivos novos. |
| `runtime_etapas_1_3.diff` | Historical reference only (stages 1–3). Do not apply. / Só referência histórica. Não aplicar. |

Apply to a clean clone / Aplicar num clone limpo:

    scripts/aplicar_patches.sh            # clones into ./PS2Recomp if missing

Bring your work back before committing / Trazer seu trabalho de volta antes do commit:

    scripts/atualizar_patches.sh

A new source file must be added to `arquivos.txt`; the script warns about unlisted ones. / Arquivo de código novo precisa entrar em `arquivos.txt`; o script avisa.
