# ShaolinMonks RPG

**[⬇️ Download](#download)** · **[Status](#status)** · **[Stage testing](#stage-testing)** · **[Requirements](#requirements)** · **[Build and run](#build-and-run)** · **[Other fighters](#other-fighters)** · **[RPG mode](#rpg-mode-optional)** · **[Repository](#how-the-repository-is-organized)** · **[Contributing](#contributing)** · **[Credits](#credits)**

*Mortal Kombat: Shaolin Monks* (PS2) recompiled for PC, with an RPG mode and extra fighters.

### [⬇️ Download now (Windows and Linux)](#download)

**English** · [Português (Brasil)](README.pt-BR.md)

<a id="download"></a>

## ⬇️ Download

| System | Download | Size |
|---|---|---|
| **Windows 10/11** | [**ShaolinMonksRPG-Windows.zip**](https://github.com/lagando/MKSM/releases/latest/download/ShaolinMonksRPG-Windows.zip) | 334 MB (includes the build kit) |
| **Linux** | [**ShaolinMonksRPG-Linux.zip**](https://github.com/lagando/MKSM/releases/latest/download/ShaolinMonksRPG-Linux.zip) | 23 MB |

**Unpack → put your game's ISO in `coloque_o_jogo_aqui` → double-click `Instalar.bat` (Windows) or `Instalar.sh` (Linux).** The first run builds for a few hours; after that just open `portatil/…/Jogar.bat` or `jogar.sh`. Details in [Build and run](#build-and-run); every version is on the [Releases page](https://github.com/lagando/MKSM/releases).

> The zip contains neither the game nor a prebuilt executable: it builds the game on your PC from your own ISO.

**ShaolinMonks RPG** is a **static recompilation** of *Mortal Kombat: Shaolin Monks* for **PlayStation 2** (USA, `SLUS-210.87`) to native PC code, built on [**PS2Recomp**](https://github.com/ran-j/PS2Recomp).

This is **not an emulator** and **not a remaster**. The game's MIPS code (its ELF) is translated to C++ and compiled as a native program. The hardware that is not the CPU (GS, VU, DMA, IOP, SPU2) is still emulated by the PS2Recomp runtime, with our own changes on top.

> **This repository contains no game data.** No ISO, ELF, generated code, recordings or assets, and it does not help you get them. You need your own copy of the game.

## What ShaolinMonks RPG adds

- **Optional RPG mode:** XP gives levels and each level gives an attribute point (Strength, Defense, Health, Life steal, Regeneration, Wisdom). In Ko-op each player has their own level. The level and an XP bar show on the HUD, in the game's style. [Details](#rpg-mode-optional).
- **67 fighters in the story and Ko-op:** besides the monks and the Versus roster, Shao Kahn, Goro, Kano, Shang Tsung, Kintaro, Mileena, Jade, Jax, the Boss versions of the Versus fighters, story enemies and cut characters. Pick one with L2/R2 on the difficulty screen of a new game. [Details](#other-fighters).
- **Every Versus fighter unlocked**, and Sub-Zero and Scorpion already in the story select.
- **Ko-op with two controllers:** each player remaps their own buttons (R3 on their own controller), and up on controller 2's D-pad toggles a held block.
- **PC graphics:** internal resolution up to 4x, 16:9, FXAA, fullscreen and a menu with presets for weak PCs.
- **Its own window:** titled "Mortal Kombat Shaolin Monks RPG", with the dragon from the game's save icon.
- **Linux and Windows**, with portable folders to take to another computer.

## Status

| Area | State |
|---|---|
| Recompile the ELF (13,651 C++ files) and build the runtime | ✅ Done |
| Boot, threads, disc reads (CRI ADXF / `GAMEDATA.WAD`) | ✅ Done |
| Save screens, main menu, character select | ✅ Done |
| First 3D stage (Goro's Lair) with HUD, pause and map | ✅ Done |
| Music and voices (ADX) | ✅ Working |
| GS on the GPU (Vulkan) | ✅ New batching renderer at 2x by default (per-pixel mipmaps); the old exact renderer stays behind `PS2X_GS_HW=0` |
| VU1 on its own thread, x86-64 JIT | ✅ Working |
| Full speed (60 fields/s) | 🟡 Right speed everywhere (no slow motion); on a GTX 1050 gameplay runs ~55–60 frames/s, heavy scenes drop frames instead (`PLANO_DESEMPENHO.md`, N20; next steps in N22) |
| CRI movies (FMV) | ✅ Intro and all 8 movies play; Start skips |
| Memory card (save and load) | ✅ Working (files in `extraido_full/mc0/`) |
| Gameplay sound | ✅ 48 kHz without warble; SPU2 reverb approximated (`PS2X_REVERB`) |
| Movie sound | ✅ Played straight from the `.SFD` files (no hiss) |
| Kung Lao as partner | ✅ Visible (used to be invisible) |
| Every other stage tested start to finish | 🟡 Goro's Lair and Wu Shi Academy 100%, The Portal 10%; the rest not yet (see [Stage testing](#stage-testing)) |
| Windows build | 🟡 Builds on Windows itself with the zip's kit (`Instalar.bat`) or from Linux (`scripts/build_windows.sh`); already played on real Windows, with small stutters (plan N22 in [`PLANO_DESEMPENHO.md`](PLANO_DESEMPENHO.md)); also opens without Vulkan (CPU drawing) |
| Floors, walls and light rays in Goro's Lair | ✅ As in the original (the VIF now runs ahead of VU1: the game sends each batch's colors right after starting it) |
| Liu Kang spinning while standing (an R5900 MMI instruction read the wrong register) | ✅ Fixed |
| Heavy cutscene moments (Raiden arriving) | 🟡 Drop to ~35 frames/s for a few seconds, with both renderers (EE/VU1 limit, N20) |
| RPG mode (levels and attribute points) | 🟡 Implemented, Single Player and Ko-op, with the level and an XP bar on the HUD in the game's style (see [RPG mode](#rpg-mode-optional)); balance still being tested |
| Fighters | ✅ All 8 unlocked in Versus, and Sub-Zero and Scorpion in the story character select (`MKSM_VERSUS_TODOS=0` restores the original unlocks); 67 fighters (Versus, bosses such as Shao Kahn, Goro and Shang Tsung, enemies and cut characters) can be picked for a new story or Ko-op game with **L2/R2** on the difficulty screen; story compatibility still being tested ([`PLANO_PERSONAGENS.md`](PLANO_PERSONAGENS.md)) |
| Fullscreen | ✅ F11 |
| Window | ✅ Titled "Mortal Kombat Shaolin Monks RPG", with the dragon from the game's save icon |
| Ko-op with two controllers | ✅ Each player remaps their own buttons (R3 on their own controller); held block on up on controller 2's D-pad |
| Higher internal resolution | ✅ New batching renderer at 2x by default, as fast as the old one; `PS2X_GS_HW=0` goes back to the old one, `PS2X_SCALE=1..4` picks the scale |
| Graphics menu for weaker PCs | ✅ R3 in menus/pause (or F1): internal resolution, FXAA, upscale filter, field blend, renderer; saved to `mksm.ini` |
| Widescreen | ✅ 16:9 in the Graphics menu or `PS2X_WIDESCREEN=1` (3D gets the wider view; 2D/HUD stretches, as in PCSX2; movies stay 4:3) |

Detailed state: [`ESTADO.md`](ESTADO.md). Performance plan with measurements: [`PLANO_DESEMPENHO.md`](PLANO_DESEMPENHO.md).

## Stage testing

How much of each stage has been played start to finish in the recompilation (as of 2026-11-18). Legend: ✅ 100% tested, 🟡 partially tested, ⬜ 0% not tested yet.

Full campaign order, with the main menu before it:

| # | Stage | Progress |
|---|---|---|
| — | Main menu — character select and save screens | ✅ 100% |
| 1 | Goro's Lair | ✅ 100% |
| 2 | Wu Shi Academy | ✅ 100% (known bug: in some fights the HUD shows only the text) |
| 3 | The Portal — portal between the realms (central area) | 🟡 10% |
| 4 | Evil Monastery | ⬜ 0% |
| 5 | Living Forest | ⬜ 0% |
| 6 | Reptile's Lair | ⬜ 0% |
| 7 | The Portal — back to the central portal | ⬜ 0% |
| 8 | Soul Tombs | ⬜ 0% |
| 9 | The Iron Maiden Room | ⬜ 0% |
| 10 | The Spiked Ceiling Room | ⬜ 0% |
| 11 | The Flaming Floor | ⬜ 0% |
| 12 | The Spinning Fan Room | ⬜ 0% |
| 13 | The Soul Collector Room | ⬜ 0% |
| 14 | Wastelands | ⬜ 0% |
| 15 | Sub-Zero's Boat | ⬜ 0% |
| 16 | Goro's Arena — the fight with Goro | ⬜ 0% |
| 17 | The Portal — back to the portal | ⬜ 0% |
| 18 | The Dead Pool | ⬜ 0% |
| 19 | Netherrealm | ⬜ 0% |
| 20 | Scorpion's Lair | ⬜ 0% |
| 21 | The Foundry | ⬜ 0% |
| 22 | The Prison — where Kitana and Johnny Cage are held | ⬜ 0% |
| 23 | Kano's Secret Room — the secret fight with Kano | ⬜ 0% |
| 24 | Shao Kahn's Arena — final bosses' arena | ⬜ 0% |
| 25 | Shang Tsung — first final boss | ⬜ 0% |
| 26 | Kintaro — second final boss | ⬜ 0% |
| 27 | Shao Kahn — final boss | ⬜ 0% |
| 28 | Ending | ⬜ 0% |

Extra and optional areas (visited while exploring; none tested yet):

- The Warrior Shrine — ⬜ 0%
- The Armory — ⬜ 0%
- The Pit — ⬜ 0%
- The Rooftops — ⬜ 0%
- The Tower — ⬜ 0%
- Ice Falls — ⬜ 0%
- Kombat Tomb — ⬜ 0%
- Wu Shi Shrine — ⬜ 0%

## Requirements

- Your own copy of the game (PS2, USA, `SLUS-210.87`).
- **Windows 10 or 11 (64-bit):** nothing to install. The Windows zip carries the build kit (GCC, CMake and Ninja).
- **64-bit Linux:** the system compiler and development libraries (`g++`, `cmake`, `ninja`, `7z`, `glslc`, Vulkan, FFmpeg, X11). `Instalar.sh` installs whatever is missing on Arch, Ubuntu/Debian or Fedora, asking for your password once.
- **CPU:** 64-bit x86-64 with SSE4.1 and 4 or more cores (fewer works, but heavy scenes stay below 60). No AVX/AVX2 needed.
- **GPU with Vulkan 1.1:** NVIDIA since the GTX 600, AMD since the HD 7000, Intel HD 620 and later (on Linux, the Intel HD 4000 has an incomplete Vulkan that draws wrong). Without Vulkan the game still opens and draws on the CPU, much slower.
- **To build:** 8 GB of RAM, ~20 GB of free disk and a few hours the first time (the generated code is 13 thousand files). To play, ~1.5 GB of RAM is enough.

> **There is no ready-made executable to download, and there won't be.** The executable contains the game's translated code, so shipping it would mean shipping the game. Everyone builds from their own copy with the script below.

## Build and run

**The simplest way:**

1. Download the zip for your system from the [**Releases** page](https://github.com/lagando/MKSM/releases/latest) (`ShaolinMonksRPG-Linux.zip` or `ShaolinMonksRPG-Windows.zip`: they carry everything needed to build offline) and unpack it. On Linux the plain repository also works (`git clone`; the installer then downloads what is missing); on Windows use the zip, which carries the build kit.
2. Put your game's ISO (PS2, USA, `SLUS-210.87`) in the `coloque_o_jogo_aqui` folder.
3. Double-click the installer:
   - **Linux:** `Instalar.sh`. It installs what is missing (asks for your password once), builds and assembles `portatil/MKSM-linux`.
   - **Windows:** `Instalar.bat`. It builds on Windows itself with the kit shipped in the Windows zip (GCC, CMake and Ninja, in the `ferramentas` folder), **without installing anything, without admin and without internet**, assembles `portatil\MKSM-windows` and, at the end, deletes the kit and the intermediate files.
4. Play: double-click `jogar.sh` (Linux) or `Jogar.bat` (Windows) inside the `portatil` folder.

The first build takes a few hours. If it stops halfway, run the installer again: finished steps are skipped. The `portatil` folder contains your game: it is for you only.

**Manual steps** (what the installers do underneath):

One command extracts the ISO, fetches PS2Recomp at the pinned commit, applies our patches, recompiles the ELF and builds everything:

```bash
scripts/instalar.sh "path/to/your.iso"
```

Use `JOBS=1` if you run out of memory. If it stops halfway, run it again: finished steps are skipped.

Optional, on Linux: `scripts/pgo_runtime.sh` trains the runtime with your game (~15 min, opens the window by itself) and rebuilds it with the profile. The image stays identical, and VU1 and drawing get ~10% lighter.

Play:

```bash
scripts/jogar.sh
```

The window is titled "Mortal Kombat Shaolin Monks RPG" and uses the dragon from the game's save icon (`mc0/BASLUS-21087/game.icn`, created by the first save; without it, the default icon). It reads optional settings from `mksm.ini` in the project root (one `VARIABLE=value` per line; the list is at the top of [`scripts/jogar.sh`](scripts/jogar.sh)). Keys: **F11** fullscreen, **F10** screenshot (`capturas/`), **F9** records the current scene for bug reports (`gsrec/`, game data: don't share it publicly). **Graphics menu:** **R3** on the controller in menus, the Select pause and the map, or **F1** anytime (internal resolution, 16:9, FXAA, upscale filter, field blend, renderer; saved to `mksm.ini`). **F12** opens the debug panel.

**Portable folders:** after building, `scripts/empacotar_linux.sh` makes `portatil/MKSM-linux` (runs on another Linux without installing anything) and `scripts/build_windows.sh` makes `portatil/MKSM-windows` (double-click `Jogar.bat`). They contain your game files: keep them for yourself.

**Laptops with two GPUs:** the window may open on the integrated GPU. Prefix the command with `prime-run` (or `__NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia`).

**Controller:** a regular USB/Bluetooth gamepad works; the analog sticks have a 25% dead zone (`PS2X_DEADZONE` in `mksm.ini`). Each player remaps their own buttons in the **Controles** tab: player 1 with F1/R3, player 2 by pressing **R3 on the second controller** (handy for generic controllers that map buttons differently; saved as `PS2X_PAD2_*`). On the second controller, **D-pad up** toggles a permanent block (as if holding L2), so one person can play Ko-op with a single controller.

Useful environment variables (all listed in [`ARQUITETURA.md`](ARQUITETURA.md) §6):

| Variable | Effect |
|---|---|
| `MKSM_SKIP_INTRO=1` | Skips the intro videos |
| `PS2X_VOL_MASTER`, `PS2X_VOL_MUSIC`, `PS2X_VOL_SFX` | Volumes, 0–100 |
| `PS2X_REVERB=22` | Echo on sound effects (0 turns it off) |
| `PS2X_FXAA=1` | Edge smoothing (on by default on Windows; off by default on Linux, which gives more frames) |
| `PS2X_GS_HW=0` | Old renderer (PS2 resolution, bit-exact) instead of the new one |
| `PS2X_SCALE=1` | Internal resolution of the new renderer, 1–4 (default 2; 1 for weak GPUs) |
| `PS2X_WIDESCREEN=1` | 16:9 (wider 3D view; 2D/HUD stretches; movies stay 4:3) |
| `PS2X_RUMBLE=0` | No controller vibration |
| `PS2X_DEINTERLACE=1` | Blends both fields (on by default on Windows; off by default on Linux, which eases the CPU) |
| `PS2X_VSYNC=1` | Vertical sync: the image changes with the monitor (against stutter with the game at 60; also in the Graphics menu) |
| `PS2X_UNCAPPED=1` | No real-time cap (throughput testing; the game speeds up) |
| `PS2X_PERF=1` | Prints fps, GS and GPU load and where each frame's time goes, every 2 s |
| `PS2X_PIN=0` | Don't pin the four busy threads to separate physical cores (Linux and Windows) |
| `PS2X_GS_GPU=0` | With `PS2X_GS_HW=0`: the old renderer on the CPU only |
| `PS2X_GS_RASTER=0` | GPU compute tiles instead of the rasterizer |
| `PS2X_VU1_THREAD=0` | VU1 on the game thread |

## Other fighters

- **Unlocked:** all 8 Versus fighters (Liu Kang, Kung Lao, Sub-Zero, Scorpion, Baraka, Reptile, Kitana, Johnny Cage), and Sub-Zero and Scorpion in the story character select (the game normally unlocks them after finishing the story with each monk). `MKSM_VERSUS_TODOS=0` in `mksm.ini` restores the original unlocks.
- **Story and Ko-op with another fighter:** on a **new game**, on the *Select Difficulty* screen, each player presses **L2/R2** on their own controller (**left/right Shift** on the keyboard for player 1) (held, it keeps scrolling) to pick one of 67 fighters, in the order of the table below. The choice is saved with the game and can't be changed afterwards, so bought moves never mix between characters. In the story, specials are **R1 + a button**.

| Group (in list order) | Fighters | Status |
|---|---|---|
| Monks and Versus | Liu Kang, Kung Lao, Sub-Zero, Scorpion, Kitana, Johnny Cage, Baraka, Reptile | Attacks, specials and fatalities (the only ones with fatality animations in the game data) |
| With specials | Shao Kahn, Goro (two), Kano, Shang Tsung, Inferno Scorpion, Ermac (the boss), Kintaro (two) | Their own attacks and specials; Goro stands with his arms spread when idle; Shang Tsung doesn't come back if he vanishes |
| Normal attacks only | Mileena, Jade, Jax, the story versions of the Versus fighters (Scorpion Boss, Baraka Boss, Reptile Boss, Kitana Boss, Sub-Zero Boss, Johnny Cage Boss), Shadow Priest, Orochi Hellbeast, the story enemies (Mercs, Mask Guard, Oni Warlord, Demons, Tarkata, Corpses, Elemental Knight, Living Trees...) and some cut characters (Shokan, Green Tarkata, Banshee) | They attack, no specials; Mileena and Jade move perfectly; the Corpses explode and vanish |
| Attacks, but buggy | Oni (glitches when attacking on the run), Female Ninja, Ninja Wraith, Chan Chan, Nagiri Swamp, Fire Oni, Horned Oni | They attack, with broken animation |
| No attacks | Noob Saibot, Smoke, Raiden, Kabal, Sonya, the other Shang Tsung, Ermac (Versus), Burning Monk, Order of Light Monk, Armor Lion, the man in a suit, Shiva | They only walk |

- **Ko-op: we recommend one player uses one of the 4 classics** (Liu Kang, Kung Lao, Sub-Zero or Scorpion). Some parts of the story ask for a Brutality or Multality, which only those four are sure to do.
- **Multality gates:** a fighter without a Multality (Johnny Cage and others) gets past the "perform a Multality" gate by entering the arrows; the game cancels the move but the gate opens.
- **Fatality gates:** when no player has a fatality (no monk or Versus fighter in the game), the story's "perform a fatality" gates open by themselves. Shao Kahn also deals 2.5× damage and takes 0.7× in the story.
- **Block (L2):** the fighter's own block. Shao Kahn blocks normally; fighters whose move data has no block simply don't block.
- **Left out of the list:** characters that break the camera, can't stand or freeze the game (creatures, trees, giant bosses and some cut characters). They're listed in [`PLANO_PERSONAGENS.md`](PLANO_PERSONAGENS.md).

- **Not perfect yet:** Versus fighters were made for 1-on-1 fights. The move shop, story fatalities and cutscenes (which still show the monks) may not fit. Giving moves to those without them (and fixing Goro's idle animation) would mean mixing two characters' data packs, which hasn't been figured out yet. Status, character IDs and the game's damage formula: [`PLANO_PERSONAGENS.md`](PLANO_PERSONAGENS.md).
- **Testing:** the Windows portable folder includes `TESTAR.txt`, a step-by-step checklist (RPG, fighters, Ko-op, Versus, saves).

## RPG mode (optional)

An optional mode, chosen only when starting a new game, where experience gives **levels** and each level gives an **attribute point**. The game's own economy does not change: enemies give the same XP and moves cost the same.

- **Turn it on:** Single Player or Ko-op → character → on *Select Difficulty*, put the cursor on **Hard** and press **L3** (**F2** on the keyboard). A gold "RPG" appears next to Hard. RPG games are always Hard, with tougher enemies at the start (+30% damage and +30% health), so the early game is harder than Hard and gets easier as you grow.
- **Levels:** the level comes from all the XP earned (buying moves doesn't lower it). Level 1→2 costs 400 XP, then 500, 650, 850... (each step costs +100 up to level 10, +200 from 10 to 29 and +300 from 30 on), up to level 100. The level, a bar of the XP to the next one and the number (`120/550`) show above the "EXP" counter, in the game's HUD style; a green "+N" blinks when there are points to spend. In Ko-op each player has their own.
- **Dying:** when you die and continue, you lose the XP earned since the last checkpoint (the same the game takes from its "EXP"), but you **never lose a level**: the loss stops at the start of your current level. Replaying a stage and earning XP again from the same enemies still counts. Loading a save also brings the RPG back to what was saved. `RPG_PERDE_XP_AO_MORRER=0` turns the loss off.
- **Attributes** (1 point per level, up to 20 each and 5 in Wisdom): Strength (+5% damage dealt), Defense (−3% damage taken), Health (+5% max health), Life steal (heals 1% of the damage dealt), Regeneration (0.1% of max health per second), Wisdom (+3% XP toward levels). Movement speed is never touched, so moves, combos and hit boxes stay the original.
- **Spend points:** Select pause → **R3** (or **F1**) → **RPG** tab (L1/R1 switch tabs). X spends a point; "Redistribuir pontos" (X twice) gives them all back. In Ko-op, each player sees only their own levels and points: player 2 opens the window with R3 on the second controller.
- **Saves:** a save made in RPG mode stays RPG. The *Load Game* screen tags it "RPG · Nível N". The RPG data lives next to `mksm.ini` in `rpg_mc0_slot<n>.ini` (one per save slot); the memory card save itself is untouched and still opens without the mode. Copy those files along with `mc0/` when you back up.
- **Tuning:** every number (XP curve, effect per point, caps, enemy boost) is a `RPG_*` key in `mksm.ini` (listed at the top of [`scripts/jogar.sh`](scripts/jogar.sh)). With the mode off, the game runs exactly as the original.
- Design, addresses and test status: [`PLANO_RPG.md`](PLANO_RPG.md).

## How the repository is organized

PS2Recomp is **not vendored**. `scripts/aplicar_patches.sh` clones it at the commit in [`patches/UPSTREAM_COMMIT`](patches/UPSTREAM_COMMIT) and applies [`patches/runtime_atual.diff`](patches/runtime_atual.diff) plus the new files listed in [`patches/arquivos.txt`](patches/arquivos.txt). The `PS2Recomp/` folder that results is ignored by git; you work there and run `scripts/atualizar_patches.sh` to bring your changes back into `patches/`.

| Path | What it is |
|---|---|
| `sm.toml` | PS2Recomp configuration for this game (function stubs, overrides, idle loops) |
| `patches/` | Our changes to the PS2Recomp runtime, IOP, recompiler and tests |
| `scripts/` | Build, patch and test helpers |
| [`ARQUITETURA.md`](ARQUITETURA.md) | Architecture: threads, locks, the path of a frame, timing, game traps, testing without booting the game. **Start here.** |
| [`PLANO_DESEMPENHO.md`](PLANO_DESEMPENHO.md) | The active plan: performance, image, open tasks, measurements |
| [`PLANO_RPG.md`](PLANO_RPG.md) | The optional RPG mode: design, game addresses, test status |
| [`PLANO_PERSONAGENS.md`](PLANO_PERSONAGENS.md) | Extra fighters: unlocks, character IDs, damage formula, what works in the story |
| [`PLANO.md`](PLANO.md) | History: the investigation log up to 13/11, with the evidence for each fix |
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
