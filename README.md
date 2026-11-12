# Mortal Kombat: Shaolin Monks — PS2 static recompilation

**English** · [Português (Brasil)](README.pt-BR.md)

A **static recompilation** of *Mortal Kombat: Shaolin Monks* for **PlayStation 2** (USA, `SLUS-210.87`) to native PC code, built on [**PS2Recomp**](https://github.com/ran-j/PS2Recomp).

This is **not an emulator** and **not a remaster**. The game's MIPS code (its ELF) is translated to C++ and compiled as a native program. The hardware that is not the CPU (GS, VU, DMA, IOP, SPU2) is still emulated by the PS2Recomp runtime, with our own changes on top.

> **This repository contains no game data.** No ISO, ELF, generated code, recordings or assets, and it does not help you get them. You need your own copy of the game.

## Status

| Area | State |
|---|---|
| Recompile the ELF (13,651 C++ files) and build the runtime | ✅ Done |
| Boot, threads, disc reads (CRI ADXF / `GAMEDATA.WAD`) | ✅ Done |
| Save screens, main menu, character select | ✅ Done |
| First 3D stage (Goro's Lair) with HUD, pause and map | ✅ Done |
| Music and voices (ADX) | ✅ Working |
| GS on the GPU (Vulkan: compute + exact rasterizer with pixel interlock) | ✅ Working, bit-exact with the CPU renderer |
| VU1 on its own thread, x86-64 JIT | ✅ Working |
| Full speed in gameplay (60 fields/s) | ✅ 60 on a GTX 1050; heavy scenes drop frames instead of going into slow motion |
| CRI movies (FMV) | ✅ Intro and all 8 movies play; Start skips |
| Memory card (save and load) | ✅ Working (files in `extraido_full/mc0/`) |
| Gameplay sound | ✅ 48 kHz without warble; SPU2 reverb still missing |
| Kung Lao as partner | ✅ Visible (used to be invisible) |
| Every other stage tested start to finish | ❌ To do |
| Windows build | ❌ Not tested |
| Light rays in Goro's Lair render as white bars | ❌ Open ([`PLANO_DESEMPENHO.md`](PLANO_DESEMPENHO.md), track E) |
| Image matching the original (dark specks on floors, blur) | 🟡 In progress (track E) |
| Higher internal resolution, fullscreen, widescreen | ❌ Planned (track E: better than the original) |

Detailed state: [`ESTADO.md`](ESTADO.md). Performance plan with measurements: [`PLANO_DESEMPENHO.md`](PLANO_DESEMPENHO.md).

## Requirements

- Your own copy of the game (PS2, USA, `SLUS-210.87`).
- Linux (tested on Arch/CachyOS with GCC). Windows/MSVC is PS2Recomp's main target but has not been tried here.
- `git`, `cmake`, `ninja`, `7z`, `g++`, `glslc` (Vulkan SDK / shaderc), `libvulkan`.
- A Vulkan GPU. Optional but much faster: `VK_EXT_fragment_shader_interlock` (NVIDIA since Maxwell, recent AMD/Intel). Without Vulkan the CPU renderer is used.
- ~5 GB of RAM per build job and a few hours for the first build (the generated code is huge).

> **There is no ready-made executable to download, and there won't be.** The executable contains the game's translated code, so shipping it would mean shipping the game. Everyone builds from their own copy with the script below.

## Build and run

One command extracts the ISO, fetches PS2Recomp at the pinned commit, applies our patches, recompiles the ELF and builds everything:

```bash
scripts/instalar.sh "path/to/your.iso"
```

Use `JOBS=1` if you run out of memory. If it stops halfway, run it again: finished steps are skipped.

Play:

```bash
cd PS2Recomp && ./out/build/ps2xRuntime/ps2EntryRunner ../extraido_full/SLUS_210.87
```

**Laptops with two GPUs:** the window may open on the integrated GPU. Prefix the command with `prime-run` (or `__NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia`).

**Controller:** a regular USB/Bluetooth gamepad works; the analog sticks have a 25% dead zone.

Useful environment variables (all listed in [`ARQUITETURA.md`](ARQUITETURA.md) §6):

| Variable | Effect |
|---|---|
| `MKSM_SKIP_INTRO=1` | Skips the intro videos |
| `PS2X_DEINTERLACE=0` | Shows one field at a time instead of blending both (harsher image) |
| `PS2X_UNCAPPED=1` | No real-time cap (throughput testing; the game speeds up) |
| `PS2X_PERF=1` | Prints fps, GS and GPU load every 2 s |
| `PS2X_PIN=0` | Don't pin the four busy threads to separate physical cores |
| `PS2X_GS_GPU=0` | CPU renderer only |
| `PS2X_GS_RASTER=0` | GPU compute tiles instead of the rasterizer |
| `PS2X_VU1_THREAD=0` | VU1 on the game thread |

## How the repository is organized

PS2Recomp is **not vendored**. `scripts/aplicar_patches.sh` clones it at the commit in [`patches/UPSTREAM_COMMIT`](patches/UPSTREAM_COMMIT) and applies [`patches/runtime_atual.diff`](patches/runtime_atual.diff) plus the new files listed in [`patches/arquivos.txt`](patches/arquivos.txt). The `PS2Recomp/` folder that results is ignored by git; you work there and run `scripts/atualizar_patches.sh` to bring your changes back into `patches/`.

| Path | What it is |
|---|---|
| `sm.toml` | PS2Recomp configuration for this game (function stubs, overrides, idle loops) |
| `patches/` | Our changes to the PS2Recomp runtime, IOP, recompiler and tests |
| `scripts/` | Build, patch and test helpers |
| [`ARQUITETURA.md`](ARQUITETURA.md) | Architecture: threads, locks, the path of a frame, timing, game traps, testing without booting the game. **Start here.** |
| [`PLANO.md`](PLANO.md) | Log of every fix, with evidence |
| [`PLANO_DESEMPENHO.md`](PLANO_DESEMPENHO.md) | Performance plan and measurements |
| [`MODDING.md`](MODDING.md) | Game addresses and formats (level select, stage data, bloom...) |
| [`ESTADO.md`](ESTADO.md) | Technical state and manual build steps |
| `notas/` | Investigation notes |

The technical documents are in Portuguese. Issues and pull requests in English or Portuguese are both welcome.

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md). In short: never commit game data, keep the renderer bit-exact (there are replay tests for that), and keep the docs in sync with the code.

## License

[GPL-3.0](LICENSE), the same as PS2Recomp, from which `patches/` derives.

## Credits

- [PS2Recomp](https://github.com/ran-j/PS2Recomp) by ran-j and contributors: the recompiler and runtime this project builds on.
- *Mortal Kombat: Shaolin Monks* © Midway / Warner Bros. Interactive Entertainment. This is a non-commercial fan project with no affiliation to the rights holders.
