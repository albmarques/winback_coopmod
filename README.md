# WinBack Story Co-op (`winback_coop`)

A two-player local split-screen **Story Co-op** mod for **[WinBackRecomp](https://github.com/RetroPulse03/WinBackRecomp)** (*WinBack Recompiled*), the native PC static recompilation of *WinBack: Covert Operations* (Nintendo 64).

---

## Overview

Although *WinBack*'s original N64 engine already supports split-screen viewports, multiple controllers, and multiple characters in **Multiplayer Versus** mode, the **Story** campaign was programmed around a single player in roughly 140 places across stage scripts, enemy AI, room culling, item pickups, cover checks, and cutscenes.

`winback_coop` solves this without rewriting the base game's campaign data. Using [`N64Recomp`](https://github.com/RetroPulse03/WinBackRecomp) runtime hooks (`RECOMP_HOOK`, `RECOMP_HOOK_RETURN`, and `RECOMP_PATCH` in [`src/coop.c`](src/coop.c)), the mod keeps stage loading and game logic running cleanly in Story mode while dynamically coordinating Player 2 (Controller 2), split-screen rendering, enemy targeting, cutscenes, and HUD state every frame.

- **Target Project:** [https://github.com/RetroPulse03/WinBackRecomp](https://github.com/RetroPulse03/WinBackRecomp)
- **Mod ID / Version:** `winback_coop` (`v0.2.0`, requires `WinBackRecomp >= 0.3.0`)
- **Output Package:** `winback_coop.nrm`

---

## Features

- **Two-Player Split-Screen Story Mode:**
  - Spawns Player 2 on Controller 2 near Player 1 using a configurable X-axis offset (`spawn_offset`).
  - Protects Player 2's character slot (`SLOT1_DATA` at `0x8017ACD0`) from being cleared by the Story enemy spawner (`func_80034364`), keeping enemies allocated to slots `2..7`.
  - Temporarily switches `MODE` to split-screen (`2`) during frame drawing (`func_80025F10`) and restores Story mode (`1`) immediately afterward.
- **Playable Engineer Character for Player 2:**
  - Optionally replaces Player 2's model with the **Engineer** (`model_id = 0x3A` / `58`), which shares Jean-Luc's skeleton and animations.
  - Decompresses the Engineer's display lists, textures, and dynamic joint vertices (`D_801DC710`), and protects its buffers across cutscenes and model updates (or lets you keep **Jean-Luc** via the mod menu).
- **Custom Split-Screen Sci-Fi HUD:**
  - Spawns the two-viewport HUD and patches `func_800CF6A8` to render a custom HUD featuring 3D character portraits (**Jean-Luc** for Player 1, **Engineer** for Player 2) alongside a slanted `LIFE` bar (green/yellow/red with a dark-red damage trail) and per-player weapon/ammo/reload indicators.
  - Editable PNG assets are stored in [`assets/hud/`](assets/hud/) and converted to RGBA5551 arrays at build time by [`tools/gen_hud_tex.py`](tools/gen_hud_tex.py).
- **Independent & Shared Progression:**
  - **Health (HP / medkits) & Weapons/Ammo:** Tracked independently per player (`SLOT0_DATA` and `SLOT1_DATA`).
  - **Key / Mission Items:** Stored in the global shared Story inventory (`0x80172500`) so either player can collect key cards/quest items and unlock doors or trigger objectives.
- **Enemy AI & Multi-Room Visibility:**
  - During each enemy update (`func_800337A8`), enemies dynamically target whichever living player is closer (with squared-distance hysteresis `d1 < d0 * 0.64f` to avoid frame-by-frame target oscillation, or fall back to the surviving player).
  - Clears room-culling hide flags (`0x100`) whenever an enemy is in or visible from either player's room.
- **Cover, Melee & Aim Fixes:**
  - Enables wall cover (`func_800ABC04`) and box/pillar/door cover (`func_8003FCE4`) for both players without blocking each other.
  - Disables player-to-player auto-aim lock-on (`func_80058438`) and Y-button (`C-Down`) camera lock-on (`func_8003A054` / `func_8003A6EC`) between allies, and expands auto-aim range/cone limits.
  - Prevents accidental melee kicks (`func_800AB594`) against your partner when friendly fire is turned off.
  - Optional **Friendly Fire** lets player-fired bullets collide with and damage the other player using Story Mode damage rules.
- **Item Pickups & UI Prompts:**
  - Allows Player 2 to pick up stage-placed items (`func_800873A4`), loose/dropped items (`func_800C958C`), and enemy drops (`func_800A8620`).
  - Merges Controller 2 inputs during event management (`func_800944E0`) so Player 2 can also dismiss item popups and skip cutscenes.
- **Dynamic Cutscenes & Death Sequence:**
  - Tracks whichever player triggered a stage event, door/switch interaction, or death (`DEATH_FLAG` at `0x8010E0FA`) and assigns them as the cutscene protagonist (`0x18CC = 0xFF`).
  - Renders in-stage cutscenes in fullscreen without the two-player HUD and restores split-screen upon returning to gameplay.
  - Ensures Player 2's death cutscene focuses on and animates the Engineer properly.
- **Independent Ladders & Area Transitions:**
  - Intra-stage transitions (`func_80098F20`, such as climbing or descending ladders) move only the player who used the transition while preserving each player's `ROOM_STATE` and viewport independently.
- **Per-Viewport Indoor/Outdoor Sky Rendering:**
  - Handles split framebuffer sky vs. indoor black background clears (`func_80026EB8`), 2D clouds (`func_800C4FB0`), and per-viewport 3D Sky Dome matrices when one player is outdoors and the other is inside a closed indoor room.

---

## Configuration Options

Defined in [`mod.toml`](mod.toml) and configurable from the in-game **Mods** menu in *WinBack Recompiled*:

| Option ID | Name | Options / Range | Default | Description |
| :--- | :--- | :--- | :--- | :--- |
| `spawn_offset` | Player 2 start offset | `0` – `200` (step `10`) | `40` | Distance along the X axis between Player 2 and Player 1 at spawn. |
| `p2_character` | Player 2 character | `Engineer`, `Jean-Luc` | `Engineer` | Character model for Player 2 (model `58` vs. Story's Jean-Luc). |
| `hud` | Two-player HUD | `On`, `Off` | `On` | Enable the split-screen two-player HUD. |
| `enemies` | Enemies target nearer player | `On`, `Off` | `On` | Enemies target the nearer living player and stay visible in either player's room. |
| `friendly_fire` | Friendly fire | `On`, `Off` | `Off` | Allow player-fired bullets to hit and damage the other player. |
| `log` | Log status | `Off`, `On` | `On` | Print player pointers, HP, slot flags, and map ID once per second (`--console`). |

---

## Known Limitations

- **No Revive Mechanic Yet:** When either player's HP reaches `0`, the game runs the standard Story death cutscene and Game Over / Continue transition rather than allowing a co-op revive. *(Note: `mod.toml`'s legacy description text still states that cover and cutscenes are unfinished, though both are implemented in [`src/coop.c`](src/coop.c).)*
- **Minor Sky Scroll Discrepancy:** When Player 1 walks, the sky/clouds in Player 2's viewport may appear to scroll faster.

---

## Project Structure

```text
winback_coop/
├── assets/hud/             # Editable PNG sources for the custom HUD & 3D portraits
├── include/
│   └── winback_mod.h       # WinBackRecomp mod API header (hooks, config, log, pad)
├── md/                     # Detailed technical documentation (in Portuguese)
│   ├── COMPILACAO_MOD.md   # MIPS compilation & .nrm packaging pipeline
│   ├── COOP_FUNCIONAMENTO.md # Architecture, RAM map, and subsystem walkthrough
│   ├── HOOKS.md            # Full hook/patch reference and ROM reverse engineering
│   └── README.md           # Workspace overview and early development notes
├── src/
│   ├── coop.c              # Main C implementation of co-op hooks and HUD patch
│   └── hud_tex_data.h      # Auto-generated RGBA5551 HUD texture data
├── tools/
│   └── gen_hud_tex.py      # Composites/converts assets/hud/*.png into src/hud_tex_data.h
├── build.bat               # Windows build & packaging script
├── mod.ld                  # Linker script (RAMBASE = 0x81000000)
├── mod.toml                # Mod manifest and config options
└── RecompModTool.exe       # N64Recomp mod packager
```

---

## Building the Mod

Like all `N64Recomp` mods, `winback_coop` is **not** compiled to native x86_64 code. It is compiled from C into a **32-bit big-endian MIPS II ELF** relocated at `0x81000000` (outside the original 8 MB N64 RDRAM) and packaged into a `.nrm` archive containing `mod.json`, `mod_binary.bin`, and `mod_syms.bin`.

### Prerequisites (Windows)

- **Python 3** with **Pillow** (`PIL`) for [`tools/gen_hud_tex.py`](tools/gen_hud_tex.py)
- **LLVM / Clang + LLD** with MIPS target support (configured in [`build.bat`](build.bat) at `C:\msys64\mingw64\bin`)
- **`RecompModTool.exe`** (included in the repository root)
- **`winback.us.syms.toml`** from a built [WinBackRecomp](https://github.com/RetroPulse03/WinBackRecomp) tree (referenced at `../../generated/winback.us.syms.toml` in [`mod.toml`](mod.toml))

### Build Steps

1. Make sure `WinBackRecompiled.exe` is closed (Windows locks `mods/winback_coop.nrm` while the game is running).
2. Run [`build.bat`](build.bat):
   ```bat
   build.bat
   ```
   This script runs:
   - `[0/3]` `python tools/gen_hud_tex.py` to regenerate [`src/hud_tex_data.h`](src/hud_tex_data.h) from [`assets/hud/`](assets/hud/).
   - `[1/3]` `clang.exe -target mips -mips2 -mabi=32 -O2 -G0 -mno-abicalls -fno-pic -ffreestanding -nostdlib ...` to compile [`src/coop.c`](src/coop.c) into `build/coop.o`.
   - `[2/3]` `ld.lld.exe -m elf32btsmip -nostdlib -T mod.ld --emit-relocs --unresolved-symbols=ignore-all -e 0 ...` to link `build/mod.elf`.
   - `[3/3]` `RecompModTool.exe mod.toml build` to produce `build/winback_coop.nrm` and copy it to the game's `mods/` directory.

---

## Technical Documentation (`md/`)

Detailed technical notes and reverse-engineering documentation (written in Portuguese) are available in [`md/`](md/):

- [`md/COOP_FUNCIONAMENTO.md`](md/COOP_FUNCIONAMENTO.md) — Memory map, struct offsets, subsystem design, and step-by-step hook explanations.
- [`md/HOOKS.md`](md/HOOKS.md) — Complete hook/patch catalog, ROM function addresses, script VM bytecode, and rendering pipeline analysis.
- [`md/COMPILACAO_MOD.md`](md/COMPILACAO_MOD.md) — Deep dive into the MIPS compiler flags, ELF relocations, `.nrm` structure, and how to add new ROM hooks.
- [`md/README.md`](md/README.md) — Original workspace overview covering both `WinBackRecomp` and the early stages of `winback_coop`.
