# WinBack Story Co-op (`winback_coop`)

A two-player local split-screen **Story Co-op** mod for **[WinBackRecomp](https://github.com/RetroPulse03/WinBackRecomp)** (*WinBack Recompiled*), the native PC static recompilation of *WinBack: Covert Operations* (Nintendo 64).

---

## Overview

Although *WinBack*'s original N64 engine already supports split-screen viewports, multiple controllers, and multiple characters in **Multiplayer Versus** mode, the **Story** campaign was programmed around a single player in roughly 140 places across stage scripts, enemy AI, room culling, item pickups, cover checks, and cutscenes.

`winback_coop` solves this without rewriting the base game's campaign data. Using [`N64Recomp`](https://github.com/RetroPulse03/WinBackRecomp) runtime hooks (`RECOMP_HOOK`, `RECOMP_HOOK_RETURN`, and `RECOMP_PATCH` in [`src/coop.c`](src/coop.c)), the mod keeps stage loading and game logic running cleanly in Story mode while dynamically coordinating Player 2 (Controller 2), split-screen rendering, enemy targeting, cutscenes, and HUD state every frame.

- **Target Project:** [https://github.com/RetroPulse03/WinBackRecomp](https://github.com/RetroPulse03/WinBackRecomp)
- **Mod ID / Version:** `winback_coop` (`v0.3.26`, requires `WinBackRecomp >= 0.3.0`)
- **Latest Release (`.nrm`):** [v0.3.26 Release](https://github.com/albmarques/winback_coopmod/releases/tag/v0.3.26) ([direct download `winback_coop.nrm`](https://github.com/albmarques/winback_coopmod/releases/download/v0.3.26/winback_coop.nrm))

---

## Features

- **Two-Player Split-Screen Story Mode:**
  - Spawns Player 2 on Controller 2 on Player 1's start point (`spawn_distance` = 0, the default), so the game gives Player 2 the right room. Above 0, Player 2 is moved sideways (`spawn_side`, as seen from Player 1's start camera) and a short spawn guard pushes Player 2 off Player 1; near a wall or doorway that move can leave Player 2 with Player 1's room (walls not blocking, parts of the map not drawn in Player 2's view).
  - Protects Player 2's character slot (`SLOT1_DATA` at `0x8017ACD0`) from being cleared by the Story enemy spawner (`func_80034364`), keeping enemies allocated to slots `2..7`.
  - Temporarily switches `MODE` to split-screen (`2`) during frame drawing (`func_80025F10`) and restores Story mode (`1`) immediately afterward.
- **Playable Engineer Character for Player 2:**
  - Optionally replaces Player 2's model with the **Engineer** (`model_id = 0x3A` / `58`), which shares Jean-Luc's skeleton and animations.
  - Decompresses the Engineer's display lists, textures, and dynamic joint vertices (`D_801DC710`), and protects its buffers across cutscenes and model updates (or lets you keep **Jean-Luc** via the mod menu).
- **Custom Split-Screen Sci-Fi HUD:**
  - Spawns the two-viewport HUD and patches `func_800CF6A8` to render a custom HUD featuring 3D character portraits (**Jean-Luc** for Player 1, **Engineer** for Player 2) alongside a slanted `LIFE` bar (green/yellow/red with a dark-red damage trail) and per-player weapon/ammo/reload indicators.
  - Editable PNG assets are stored in [`assets/hud/`](assets/hud/) and converted to RGBA5551 arrays at build time by [`tools/gen_hud_tex.py`](tools/gen_hud_tex.py).
- **1 PLAYER / 2 PLAYER in Story:**
  - After choosing STORY on the title menu, the Versus player-count screen is reused to ask 1 PLAYER (original single-player Story) or 2 PLAYER (co-op). The cursor starts on 1 PLAYER and B goes back to the main menu. The "Story players" option can skip the question.
  - In 1 PLAYER the custom life bar moves to the lower right corner (the co-op offsets are made for half-height views).
- **Partner Radar:**
  - A small round radar in the lower right of each view shows where the other player is (blue = Player 1, orange = Player 2). The dot stays on the edge when the partner is out of range, turns hollow when they are in another room, and goes grey when they are down.
  - The radar is drawn by [`src/radar.h`](src/radar.h) as one more 2D sprite right after the life bar. It turns with each view's camera and shows an arrow for the character's facing.
  - It also draws the game's own pause-menu map of the level (rooms already discovered, the whole level, or an explored-area trail). The map data is extracted from your ROM by [`tools/extract_maps.py`](tools/extract_maps.py) into `src/map_data.h` (not in the repository).
- **Weapon Row and Ammo Panel:**
  - Top right of each view: the weapons that player owns (handgun, submachine gun, shotgun, rocket launcher, C4), with the one in hand highlighted in the player's colour. Icons are original art made by [`tools/gen_weapon_icons.py`](tools/gen_weapon_icons.py).
  - Lower left of each view (replacing the game's bullet column): magazine count (red when low), reserve (∞ for the handgun, magazines for the SMG/shotgun) and one round per bullet. Art from [`tools/gen_ammo_tex.py`](tools/gen_ammo_tex.py), drawn by [`src/ammo_hud.h`](src/ammo_hud.h).
- **Co-op Difficulty:**
  - Normal / Hard / Brutal: enemies take 100/70/50% damage and deal 100/130/160%; on Hard and Brutal enemies split their attacks between both players and ammo pickups give 70/50%.
- **Separate Inventories:**
  - Health and magazines live in each player's own data. The Story inventory (weapons owned, SMG/shotgun reserves, rockets, C4, handgun magazine upgrade) is one global block at `0x801587C0..0x801587CC`; the mod keeps Player 2's own copy and swaps it in while the game runs Player 2's update, weapon and pickups. Player 2 starts each Story session with a copy of Player 1's inventory. The "Inventory" option switches back to the shared original.
  - Medkits and key items are not separated yet (their location is still being mapped).
- **Enemy AI & Multi-Room Visibility:**
  - During each enemy update (`func_800337A8`), enemies dynamically target whichever living player is closer (with squared-distance hysteresis `d1 < d0 * 0.64f` to avoid frame-by-frame target oscillation, or fall back to the surviving player).
  - Clears room-culling hide flags (`0x100`) whenever an enemy is in or visible from either player's room.
  - Bosses (own update `func_800AD9F0`) get the same: they go after the nearer living player, keep fighting when Player 1 is down, and stay visible in either player's room.
- **Smarter Enemies (`enemy_ai`: Original / Smart / Elite, default Smart):** ([`src/enemy_ai.h`](src/enemy_ai.h))
  - *Senses:* the game lets each enemy look for the player only one frame in 7, inside a 70° cone each side and up to 2000 units. Smart looks every 2 frames, 85°, 2600; Elite every frame, 100°, 3200. Line of sight is still required.
  - *Reactions:* the pause after spotting you is cut to 1 frame, and the aim pause before a burst from 6 frames to 3 (Smart) or 1 (Elite).
  - *Firepower:* one (Smart) or two (Elite) more shots per burst, and the per-shot aim drift is 60% / 35% of the original, so bursts stay on target.
  - *Squad alert:* an enemy that spots a player warns the others in its room within 1500 (Smart) / 2500 (Elite): for a moment they look all around them instead of only ahead.
  - *Target memory and revenge:* an enemy keeps fighting the same player (switches only if that player leaves its room or the other is much closer) and turns on whoever shoots it for 4 s (Smart) / 6 s (Elite). One player can draw fire while the other flanks.
  - Stacks with the `difficulty` option (damage and toughness). Needs `enemies` On; 1-player Story is never changed.
- **Cover, Melee & Aim Fixes:**
  - Enables wall cover (`func_800ABC04`) and box/pillar/door cover (`func_8003FCE4`) for both players without blocking each other.
  - Disables player-to-player auto-aim lock-on (`func_80058438`) and Y-button (`C-Down`) camera lock-on (`func_8003A054` / `func_8003A6EC`) between allies, and expands auto-aim range/cone limits.
  - Prevents accidental melee kicks (`func_800AB594`) against your partner when friendly fire is turned off.
  - Optional **Friendly Fire**: the game never tests players against player bullets in Story, so the mod detects each shot (magazine count dropping) and checks the partner against the shooter's aim line (body yaw + free-aim offset, as the game's shot code does). It does not test walls.
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
  - Handles split framebuffer sky vs. indoor black background clears (`func_80026EB8`), 2D clouds (`func_800C4FB0`), and per-viewport 3D Sky Dome matrices, so each view's sky follows its own player and the indoor half is cleared black.

---

## Configuration Options

Defined in [`mod.toml`](mod.toml) and configurable from the in-game **Mods** menu in *WinBack Recompiled*:

| Option ID | Name | Options / Range | Default | Description |
| :--- | :--- | :--- | :--- | :--- |
| `spawn_distance` | Player 2 start offset | `0` – `200` (step `10`) | `0` | Distance between Player 2 and Player 1 at spawn. 0 = Player 1's start point (recommended); above 0 can give Player 2 the wrong room. |
| `spawn_side` | Player 2 start side | `Left`, `Right` | `Left` | Side of Player 1 where Player 2 starts, seen from Player 1's camera. |
| `p2_character` | Player 2 character | `Engineer`, `Jean-Luc` | `Engineer` | Character model for Player 2. |
| `players` | Story players | `Menu`, `2 Players`, `1 Player` | `Menu` | Ask 1P/2P after STORY, or always use one. |
| `hud` | Two-player HUD | `On`, `Off` | `On` | Split-screen two-player HUD. |
| `enemies` | Enemies target nearer player | `On`, `Off` | `On` | Enemies target the nearer living player and stay visible in either room. |
| `enemy_ai` | Enemy AI | `Original`, `Smart`, `Elite` | `Smart` | Co-op enemy senses, reactions, bursts, squad alert, target memory and revenge. |
| `ladder_fade` | Ladder fade | `Climber only`, `Full screen` | `Climber only` | Co-op: the black fade of ladders/vaults only covers the view of the player using them. |
| `door_fix` | Door map change fix | `On`, `Off` | `On` | Co-op (spawn distance 0): P2 starts a little ahead of P1 at each stage/door map load, so the bodies don't push P2 back through the door (stage 2 double door). |
| `boss_toughness` | Boss toughness | `Original`, `Tough`, `Brutal` | `Tough` | Co-op: bosses take 55% / 40% damage and ignore new staggers for ~1.5 s / ~3 s after one (no stun-lock). |
| `boss_bar` | Boss health bar | `On`, `Off` | `On` | Co-op: BOSS health bar centred under the split line during boss fights. |
| `inventory` | Inventory | `Separate`, `Shared` | `Separate` | Per-player weapons and ammo, or the original shared inventory. |
| `difficulty` | Co-op difficulty | `Normal`, `Hard`, `Brutal` | `Normal` | Enemy toughness/damage, attack split and pickup amount. |
| `friendly_fire` | Friendly fire | `On`, `Off` | `Off` | Shots can hurt the partner (same room, in the line of fire). |
| `ammo_hud` | Ammo display | `New`, `Original` | `New` | New ammo panel or the game's bullet column. |
| `weapon_row` | Weapon row | `On`, `Off` | `On` | Owned-weapons row in the top right. |
| `live_menu` | Live menu (D-pad down) | `On`, `Off` | `On` | Per-player menu (MAP, WEAPON with 3D weapons, ITEM, EXIT) in each half screen, game keeps running; P1 blue, P2 orange. On the WEAPON page, R gives one magazine of the selected weapon's ammo to the partner (separate inventories). Start opens the original pause menu. |
| `radar` | Partner radar | `On`, `Off` | `On` | Radar in the lower right. |
| `radar_range` | Radar range | `Short`, `Medium`, `Long` | `Medium` | Radar scale. |
| `radar_mode` | Radar orientation | `Heading-up`, `North-up` | `Heading-up` | Radar turns with the camera or stays fixed. |
| `radar_map` | Radar map | `Level map`, `Full level map`, `Explored trail`, `Off` | `Level map` | Map drawn on the radar. |
| `radar_rot` | Radar rotation fix | `0`/`90`/`180`/`270 degrees` | `0 degrees` | Extra rotation if the map looks turned. |
| `radar_mirror` | Radar left/right fix | `Off`, `On` | `Off` | Mirror if left/right look swapped. |
| `log` | Log status | `Off`, `On` | `On` | Console log (`--console`): player state, radar, friendly-fire shots, story-state changes, enemy AI (`coop: ai alert` / `coop: ai switch`). |

---

## Known Limitations

- **No revive yet:** when a player's HP reaches `0`, the standard Story death cutscene and Game Over run.
- **Medkits and key items are still shared** between the players; the pause menu shows Player 1's inventory.
- **Player 2's inventory is not saved:** after loading a save, Player 2 starts from a copy of Player 1's inventory.
- **Friendly fire ignores walls** inside the same room.

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
│   ├── enemy_ai.h          # Smarter enemies (senses, reactions, bursts, squad alert, revenge)
│   ├── boss_bar.h          # Co-op boss health bar (centre of the screen)
│   ├── boss_tex_data.h     # Boss bar frame (generated by tools/make_boss_hud.py)
│   ├── boss_ai.h           # Tougher co-op bosses (damage scale + super armour)
│   ├── radar.h             # Partner radar drawing (texture renderer)
│   ├── ammo_hud.h          # Ammo panel drawing (texture renderer)
│   ├── ammo_tex.h          # Generated ammo panel art (plate, digits, bullet)
│   ├── weapon_icons.h      # Generated weapon-row icons
│   ├── hud_tex_data.h      # Auto-generated RGBA5551 HUD texture data
│   ├── map_data.h          # Generated from your ROM by tools/extract_maps.py (not committed)
│   ├── wmodel.h            # Software 3D renderer for the live menu's weapon models
│   ├── weapon_models.h     # Generated from your ROM by tools/extract_weapon_models.py (not committed)
│   └── weapon_spin.h       # Generated from your own recording by tools/gen_weapon_spin.py (optional, not committed; unused when weapon_models.h exists)
├── tools/
│   ├── gen_hud_tex.py      # Composites/converts assets/hud/*.png into src/hud_tex_data.h
│   ├── gen_weapon_icons.py # Draws the weapon-row icons into src/weapon_icons.h
│   ├── gen_ammo_tex.py     # Draws the ammo panel art into src/ammo_tex.h
│   ├── extract_maps.py     # Extracts the level maps from your ROM into src/map_data.h
│   ├── extract_weapon_models.py # Reads the pause menu's 3D weapon models from your ROM into src/weapon_models.h
│   ├── gen_weapon_spin.py  # Cuts the rotating weapons out of your pause-menu recording into src/weapon_spin.h
│   └── make_hud_frame.py   # Helper for the HUD frame art
├── build.bat               # Windows build & packaging script
├── mod.ld                  # Linker script (RAMBASE = 0x81000000)
├── mod.toml                # Mod manifest and config options
└── RecompModTool.exe       # N64Recomp mod packager
```

---

## Download & Installation

You can **either download the pre-compiled release** or **compile the mod from source** (see [Building the Mod](#building-the-mod) below):

1. **Use the pre-compiled release (easiest):** Download `winback_coop.nrm` from the **[GitHub Releases page (v0.3.26)](https://github.com/albmarques/winback_coopmod/releases/tag/v0.3.26)** (or [latest release](https://github.com/albmarques/winback_coopmod/releases/latest)).
2. **Or compile from source:** Run [`build.bat`](build.bat) to generate `build/winback_coop.nrm`.
3. Place `winback_coop.nrm` inside the **`mods/`** folder of your *WinBack Recompiled* installation (for example, `mods/winback_coop.nrm` in the same directory as `WinBackRecompiled.exe`).
4. Launch the game and configure options in the in-game **Mods** menu (`Select` / `Esc`).

---

## Building the Mod

Like all `N64Recomp` mods, `winback_coop` is **not** compiled to native x86_64 code. It is compiled from C into a **32-bit big-endian MIPS II ELF** relocated at `0x81000000` (outside the original 8 MB N64 RDRAM) and packaged into a `.nrm` archive containing `mod.json`, `mod_binary.bin`, and `mod_syms.bin`.

### Prerequisites (Windows)

- **Python 3** with **Pillow** (`PIL`) for [`tools/gen_hud_tex.py`](tools/gen_hud_tex.py)
- **LLVM / Clang + LLD** with MIPS target support (configured in [`build.bat`](build.bat) at `C:\msys64\mingw64\bin`)
- **`RecompModTool.exe`** (included in the repository root)
- **`winback.us.syms.toml`** from a built [WinBackRecomp](https://github.com/RetroPulse03/WinBackRecomp) tree (referenced at `../../generated/winback.us.syms.toml` in [`mod.toml`](mod.toml))

### Build Steps

0. One-time setup:
   - Generate the radar maps from your own ROM (the data is not distributed): `python tools/extract_maps.py path\to\winback.us.z64` → `src/map_data.h`.
   - Real 3D weapons on the live menu's WEAPON page (all five, turning smoothly), read from your own ROM: `python tools/extract_weapon_models.py path\to\winback.us.z64` → `src/weapon_models.h`. Without it the page falls back to the recorded frames below, or to the drawn icons.
   - Optional (only used when `src/weapon_models.h` is missing): rotating weapons on the live menu's WEAPON page, cut from your own recording of the pause menu's WEAPON page: `python tools/gen_weapon_spin.py recording.mkv --handgun <s> --smg <s> --shotgun <s>` → `src/weapon_spin.h` (without it, the page shows the drawn icon).
   - Add the extra game functions the mod uses to `generated/winback.us.syms.toml` (list in [`md/COMPILACAO_MOD.md`](md/COMPILACAO_MOD.md), section 6). Without them the build or the game's mod loader reports a missing reference symbol.
1. Make sure `WinBackRecompiled.exe` is closed (Windows locks `mods/winback_coop.nrm` while the game is running).
2. Run [`build.bat`](build.bat):
   ```bat
   build.bat
   ```
   This script runs:
   - `[0/3]` `python tools/gen_hud_tex.py` to regenerate [`src/hud_tex_data.h`](src/hud_tex_data.h) from [`assets/hud/`](assets/hud/).
   - `[1/3]` `clang.exe -target mips -mips2 -mabi=32 -O2 -G0 -mno-abicalls -fno-pic -ffreestanding -nostdlib ...` to compile [`src/coop.c`](src/coop.c) into `build/coop.o`.
   - `[2/3]` `ld.lld.exe -m elf32btsmip -nostdlib -T mod.ld --emit-relocs --unresolved-symbols=ignore-all -e 0 ...` to link `build/mod.elf`.
   - `[3/3]` `RecompModTool.exe mod.toml build` to produce `build/winback_coop.nrm`.
3. Ensure the compiled `winback_coop.nrm` is placed inside your *WinBack Recompiled* `mods/` folder.

> Note: `build.bat` automatically copies `build\winback_coop.nrm` to `..\..\WinBackRecompiled-v0.3.0-Windows-x64\mods\winback_coop.nrm` for the local workspace layout. If your game is located elsewhere, copy `build/winback_coop.nrm` into your game's `mods/` directory manually.

---

## Technical Documentation (`md/`)

Detailed technical notes and reverse-engineering documentation (written in Portuguese) are available in [`md/`](md/):

- [`md/COOP_FUNCIONAMENTO.md`](md/COOP_FUNCIONAMENTO.md) — Memory map, struct offsets, subsystem design, and step-by-step hook explanations.
- [`md/HOOKS.md`](md/HOOKS.md) — Complete hook/patch catalog, ROM function addresses, script VM bytecode, and rendering pipeline analysis.
- [`md/COMPILACAO_MOD.md`](md/COMPILACAO_MOD.md) — Deep dive into the MIPS compiler flags, ELF relocations, `.nrm` structure, and how to add new ROM hooks.
- [`md/README.md`](md/README.md) — Original workspace overview covering both `WinBackRecomp` and the early stages of `winback_coop`.
