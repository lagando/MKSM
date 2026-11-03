# patches/

Snapshot do estado do `PS2Recomp/` em relação ao upstream `ran-j/PS2Recomp` @ `e563698`.

Estado: **run162** (03/11/2026). A história de cada mudança fica no `PLANO.md`; a arquitetura, no `ARQUITETURA.md`.

- `runtime_atual.diff` — diff completo do `PS2Recomp/` (runtime, IOP, recompilador, testes), exceto `ps2xRuntime/src/runner/`, que é gerado.
- `runtime_etapas_1_3.diff` — histórico (etapas 1–3), só referência. Não aplicar.
- Arquivos **novos** (o `git diff` não mostra arquivos não rastreados), copiados para o destino:
  - `game_overrides_mksm.cpp` → `ps2xRuntime/src/lib/`
  - `ps2_log_stream_override.cpp` → `ps2xRuntime/src/lib/`
  - `ps2_sampling_profiler.cpp` → `ps2xRuntime/src/lib/`
  - `ps2_vu1_jit.cpp` → `ps2xRuntime/src/lib/vu/`
  - `ps2_vu_clip.h` → `ps2xRuntime/include/runtime/`
  - `ps2_vu_sub.h` → `ps2xRuntime/include/runtime/`
  - `gs_recording_backend.h` → `ps2xRuntime/include/runtime/gs/`
  - `gs_recording_backend.cpp` → `ps2xRuntime/src/lib/gs/`
  - `gs_replay.cpp` → `ps2xTest/tools/`
  - `ps2_perf.h` → `ps2xRuntime/include/runtime/`
  - `ps2_perf.cpp` → `ps2xRuntime/src/lib/`
- `wip_cache_texturas.diff`: experimento do cache de texturas decodificadas, ainda sem ganho provado. Aplicar por cima só para testar; pode não aplicar limpo sobre o `gs_cpu_backend.cpp` atual.

## Aplicar em checkout limpo

    git clone https://github.com/ran-j/PS2Recomp.git PS2Recomp
    cd PS2Recomp && git checkout e563698
    git apply ../patches/runtime_atual.diff
    cp ../patches/game_overrides_mksm.cpp ps2xRuntime/src/lib/
    cp ../patches/ps2_log_stream_override.cpp ps2xRuntime/src/lib/
    cp ../patches/ps2_sampling_profiler.cpp ps2xRuntime/src/lib/
    cp ../patches/ps2_vu1_jit.cpp ps2xRuntime/src/lib/vu/
    cp ../patches/ps2_vu_clip.h ps2xRuntime/include/runtime/
    cp ../patches/ps2_vu_sub.h ps2xRuntime/include/runtime/
    cp ../patches/gs_recording_backend.h ps2xRuntime/include/runtime/gs/
    cp ../patches/gs_recording_backend.cpp ps2xRuntime/src/lib/gs/
    cp ../patches/gs_replay.cpp ps2xTest/tools/
    cp ../patches/ps2_perf.h ps2xRuntime/include/runtime/
    cp ../patches/ps2_perf.cpp ps2xRuntime/src/lib/

## Regenerar antes de cada push que mexa no runtime

    git -C PS2Recomp diff -- . ':!ps2xRuntime/src/runner' > patches/runtime_atual.diff
    cp PS2Recomp/ps2xRuntime/src/lib/game_overrides_mksm.cpp patches/game_overrides_mksm.cpp
    cp PS2Recomp/ps2xRuntime/src/lib/ps2_log_stream_override.cpp patches/ps2_log_stream_override.cpp
    cp PS2Recomp/ps2xRuntime/src/lib/ps2_sampling_profiler.cpp patches/ps2_sampling_profiler.cpp
    cp PS2Recomp/ps2xRuntime/src/lib/vu/ps2_vu1_jit.cpp patches/ps2_vu1_jit.cpp
    cp PS2Recomp/ps2xRuntime/include/runtime/ps2_vu_clip.h patches/ps2_vu_clip.h
    cp PS2Recomp/ps2xRuntime/include/runtime/ps2_vu_sub.h patches/ps2_vu_sub.h
    cp PS2Recomp/ps2xRuntime/include/runtime/gs/gs_recording_backend.h patches/gs_recording_backend.h
    cp PS2Recomp/ps2xRuntime/src/lib/gs/gs_recording_backend.cpp patches/gs_recording_backend.cpp
    cp PS2Recomp/ps2xTest/tools/gs_replay.cpp patches/gs_replay.cpp
    cp PS2Recomp/ps2xRuntime/include/runtime/ps2_perf.h patches/ps2_perf.h
    cp PS2Recomp/ps2xRuntime/src/lib/ps2_perf.cpp patches/ps2_perf.cpp
