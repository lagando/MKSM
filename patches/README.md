# patches/

Snapshot do estado do `PS2Recomp/` em relação ao upstream `ran-j/PS2Recomp` @ `e563698`.

- `runtime_atual.diff` — diff completo de `ps2xRuntime/` (exceto `src/runner/`, que é gerado). Inclui `CMakeLists.txt`, `GS.cpp`, `CD.cpp`, `Support.h`, `Thread.cpp`, `EeScheduler.cpp`, `ps2_runtime.cpp`, etc.
- `runtime_etapas_1_3.diff` — histórico (etapas 1–3), só referência. Não aplicar.
- `game_overrides_mksm.cpp` — arquivo **novo** (não aparece no diff). Copiar para `ps2xRuntime/src/lib/`.

## Aplicar em checkout limpo

    git clone https://github.com/ran-j/PS2Recomp.git PS2Recomp
    cd PS2Recomp && git checkout e563698
    git apply ../patches/runtime_atual.diff
    cp ../patches/game_overrides_mksm.cpp ps2xRuntime/src/lib/

## Regenerar antes de cada push que mexa no runtime

    git -C PS2Recomp diff -- . ':!ps2xRuntime/src/runner' > patches/runtime_atual.diff
    cp PS2Recomp/ps2xRuntime/src/lib/game_overrides_mksm.cpp patches/game_overrides_mksm.cpp
