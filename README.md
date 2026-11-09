# Darius Gaiden (Taito, 1994) — Original + Extra Edition, native recompilation + RetroFront

Darius Gaiden Native Recomp is a fan-developed native port of Taito's 1994 arcade shoot-'em-up, bringing the
classic horizontal-scrolling shooter to modern Windows, Linux, and Steam Deck systems. It aims to preserve the
original arcade gameplay, graphics, sound and performance while adding modern features and quality-of-life
improvements.

One application, two selectable editions:

| Edition | Stable id | MAME set | Revision |
|---|---|---|---|
| **Darius Gaiden — Original** | `darius_gaiden_original` | `dariusg` | Ver 2.5O 1994/09/19 |
| **Darius Gaiden — Extra Edition** | `darius_gaiden_extra` | `dariusgx` | Extra Version, Ver 2.7J 1995/03/06 |

Both run on the Taito F3 board. Each edition's 68EC020 program was **statically recompiled into C** and
compiled natively. The two programs are different code (about 1.48 MB of the 2 MB program differs), so each
edition has its own recompiled program. The remaining ~2% of instructions, plus any unseen indirect-jump
target, are executed by the bundled Musashi interpreter.

The rest of the board is **reimplemented in C**, not emulated through an external emulator:
- TC0630FDP video
- the I/O chip, 93C46 EEPROM and watchdog
- the Taito EN sound board (68000 + Ensoniq ES5505)

The 68000 sound CPU runs on the Musashi interpreter; it isn't recompiled. The shared **RetroFront**
frontend, also used by the Gunlock and ESP Ra.De. recomps, provides the menus, edition selector, ROM
configuration, save states, rewind, display/CRT options, controller mapping and gadgets.

**No game data is included in the source.** You need your own MAME `dariusg` and/or `dariusgx` romsets.

## Quick start

- **Windows 10/11:** unzip `DariusGaiden-Recomp-Windows-x64.zip` and run `dariusg.exe`. It's a single
  file with no DLLs.
- **Steam Deck / Linux:** see below.

The program looks for `dariusg.zip` (Original) and `dariusgx.zip` (Extra Edition) in:
- the folder the program or AppImage is in, and its `roms/` subfolder
- `~/Downloads`, `~/roms`, `~/Emulation/roms/arcade`, `~/Emulation/roms/mame`

If it finds nothing, the main menu offers a controller-friendly file browser. You can also open
**Main Menu › ROM Configuration** to set each edition separately; locations are remembered per edition. See
[docs/ROM_SETUP.md](docs/ROM_SETUP.md) for accepted set types (non-merged, merged, split + parent, unzipped
folders) and every error message.

Command line: `dariusg [--edition original|extra] [--fullscreen|--windowed] [path-to-romset]`.

### Steam Deck
1. In Desktop Mode, put `DariusGaiden-Recomp-x86_64.AppImage` in a folder such as `~/Games`, with your
   zips next to it or in `~/Downloads`.
2. Right-click the AppImage, then choose **Properties › Permissions › Is executable**.
3. In Steam, choose **Add a Non-Steam Game** and select the AppImage. It starts fullscreen in Gaming Mode
   (gamescope is detected). Use the "Gamepad" controller layout. Menus are laid out for 1280×800 and work
   entirely with the controller.

This is a native Linux build and doesn't need Proton.

**Baseline:** the Linux binary is built with zig 0.17.1 for a glibc 2.17 target. The highest symbol
version it requires is GLIBC_2.16, and it needs no C++ runtime. Any distribution from about 2012 onward,
SteamOS included, can run it, though it has **not been tested on a Steam Deck**. The AppImage uses the system
SDL2 when present and its bundled copy otherwise.

### Where data is stored
Settings, save states, EEPROM data and screenshots are kept outside the program, so the read-only AppImage
works:
- Linux: `~/.local/share/RetroFront/darius_gaiden/`
- Windows: `%APPDATA%\RetroFront\darius_gaiden\`

`settings.json` (display, audio, controls, interface) is shared. Each edition has its own subfolder,
`darius_gaiden_original/` and `darius_gaiden_extra/`, containing:
- `saves/` (save states)
- `dariusg.nv` or `dariusgx.nv` (EEPROM: high scores and operator settings)
- `screenshots/`

## Editions

- **Main Menu › Select Edition** lists both editions with their ROM status: Ready, ROM needed or ROM
  problem. The selected edition is shown as a badge under the logo, and on **Start Game**, **Continue** and
  **Load State**. The last selected edition is remembered.
- If only one edition has a valid ROM set, you can still select the other. Its page explains exactly which
  set it needs. The program never substitutes one edition for the other: each loader accepts only its own
  program ROMs, and offering the wrong set gives a message naming the right edition.
- **Switching during play:** use **Quick Menu › Switch Edition**. It asks **Save & Switch / Switch /
  Cancel**. Switching is a controlled transition:
  1. the current session ends and its EEPROM is flushed;
  2. audio is cleared;
  3. the rewind buffer and the adapter are destroyed;
  4. the new edition's adapter and ROMs are loaded from scratch, which replaces all machine memory,
     sound state, timers and recompiled code;
  5. held inputs are ignored until released.

  A live session is never carried across editions.

## Controls

See [docs/CONTROLS.md](docs/CONTROLS.md). Defaults:

| | Keyboard | Controller / Steam Deck |
|---|---|---|
| Move | Arrow keys | D-pad / left stick |
| Button A – Shot | Z | A |
| Button B – Black Hole Bomb | X | B |
| Button C | C | Y |
| Start / Coin | 1 / 5 | Menu (Start) / View (Select) |
| Player 2 | WASD, J K L, 2, 6 | second controller |
| Quick menu | Esc | View + Menu (configurable) |
| Quick save / load | F5 / F8 | bindable |
| Fast-forward / rewind | Tab / Backspace (hold) | bindable |
| Screenshot / fullscreen | F12 / F11 | |

The program reads three buttons per player in both editions. Button C toggles an on-screen "[ PAUSE ]" in
the Extra Edition; no effect was observed in the Original.

## Save states

- There are 8 slots per edition, plus a quick slot and an autosave ("Continue") written when you return
  to the menu, switch editions or exit (configurable).
- Slots show thumbnails, dates and play time. Overwriting or deleting asks for confirmation.
- A state is an exact snapshot of the whole machine, taken between frames:
  - both CPUs
  - work RAM
  - all video RAMs and registers, plus the sprite framebuffer and sprite lag
  - dual-port RAM
  - sound RAM and all 32 ES5505 voices
  - DUART timer, MB87078, ES5510 host registers
  - EEPROM serial state, watchdog, frame timing and coin pulses
- Each state records:
  - the edition
  - the ROM fingerprint (built from all 14 ROM CRCs)
  - the state format version and a runtime compatibility number
  - its creation time
  - a CRC32 of the contents
- A state from the other edition, from different ROMs, from another build or a damaged one is rejected
  with a message, and the running game is untouched. Saves are stored per edition and never offered to
  the other.

## Status

| Feature | Status |
|---|---|
| ROM validation (CRC32 of every file; split, merged, non-merged sets, folders, wrong-edition detection, Unicode/space paths) | **Implemented and tested** (core-level test cases; see docs/VERIFICATION.md) |
| Static recompilation, Original and Extra (~97.8% native) | **Implemented and tested**: bit-identical to the interpreter over 12,000 frames of scripted play per edition |
| Boot, attract, gameplay (stage A), HUD, sprites, playfields, alpha effects | **Implemented and tested** (screenshots, both editions) |
| Watchdog reset (needed on first boot with a blank EEPROM) | **Implemented and tested** |
| Sound: 68000 + ES5505 + DUART + MB87078 | **Implemented**; non-silent output measured in both editions; **not listened to** |
| ES5510 "ESP" reverb DSP | **Unsupported**: effect channels are mixed dry |
| Save states: slots, quick, autosave, rejection of wrong edition, damaged and truncated states | **Implemented and tested** (0/2000 replay mismatches, identical across processes, cross-edition rejected) |
| Edition selector, switching (menu and in-game, Save & Switch), one-ROM-set operation, Continue after switching | **Implemented and tested** under Xvfb (Linux) and natively on Windows with scripted input |
| 1P score gadget | **Implemented and tested**: address verified per edition against the on-screen score |
| 2P score, high score, lives, stage gadgets | **Not provided**: no verified data source yet |
| Game service/test menu | **Blocked**: holding the test switch at boot stops at "WAIT A MOMENT" (cause under investigation, see Known issues). Extras offers service credit and EEPROM reset instead |
| Display, CRT, audio, controller pages, themes per edition, accent colour, rewind, fast-forward, screenshots | **Implemented** (shared RetroFront code); spot-checked, not exhaustively tested |
| Physical controllers, Steam Deck hardware, gamescope | **Implemented but untested** on real hardware |
| Windows .exe | **Built and run** natively on Windows 11 with scripted input |
| Linux AppImage (glibc 2.17 baseline via zig) | **Built**; started and fully exercised under Xvfb on Ubuntu 24.04; not run on SteamOS |
| Gameplay video recording | **Planned** |
| Sound-CPU recompilation | **Planned** (runs on the interpreter) |
| Other revisions (dariusgj, dariusgu) | **Unsupported** (different program ROMs) |

## Known issues

- **Test mode:** holding the test switch at power-on reaches the game's "WAIT A MOMENT" screen but goes no
  further. Difficulty and lives therefore stay at factory defaults. The most likely cause is an
  unemulated device the test menu waits on, such as the ES5510 DSP; this hasn't been confirmed.
- **No reverb:** the game's reverb/echo (ES5510) is missing.
- **Flip screen:** the flip-screen offsets come from MAME's extended-playfield games and haven't been
  verified for Darius Gaiden. Not relevant unless the cabinet flip option is set.
- **First boot:** each edition's first boot with an empty EEPROM writes defaults and restarts itself via
  the watchdog. This takes about 3 seconds, as on a new board.

## Building

Requirements: python3, gcc/g++, SDL2 development files (Linux), mingw-w64 (Windows; the bundled SDL2 is
linked statically), and appimagetool.

```sh
./gen.sh /path/to/dariusg.zip /path/to/dariusgx.zip   # extract both programs from YOUR romsets and generate gen/ (static recompilation)
./build_frontend.sh linux       # build/fe-linux/dariusg
./build_frontend.sh windows     # build/fe-windows/dariusg.exe (works from Git Bash with a mingw-w64 gcc on PATH)
./build_frontend.sh deck        # build/fe-deck/dariusg: glibc 2.17 baseline via 'python3 -m ziglang' (pip install ziglang, or PYTHONPATH to an unpacked wheel); falls back to system gcc
BUILD=0 APPIMAGETOOL=/path/to/appimagetool ./build_release.sh   # dist/: Windows zip, AppImage, Linux tar.gz, SHA256SUMS.txt
```

Developer tools:
- `./build_test.sh [interp]` builds the headless harness `build/gltest`. Select the edition with
  `EDITION=original|extra`.
- `tools/lockstep.sh` runs the recompiled code against the interpreter and compares per-frame
  machine-state hashes.
- `RF_AUTOTEST=script.txt` drives the frontend for QA: key presses, screenshots and state dumps. See the
  comment in `retrofront/src/frontend.cpp`.

## Distribution

The source tree contains no ROM data and no generated code (`gen/`, `rom/`, `*.zip`, `*.bin` and `*.nv` are
git-ignored). The **compiled binaries do contain the game's program code**, translated from your ROMs by
`gen.sh`, though no graphics or sound data. Treat release builds as personal copies made from your own
ROMs, not as something to publish.

## Project information

**Developer:** omegakatana92  
**Platforms:** Windows | Linux | Steam Deck  
**Project type:** Native arcade recompilation  
**Licence:** GPL-2.0 (see LICENSE); third-party components keep their own licences (docs/THIRD_PARTY.md)

This is an independent fan project and is not affiliated with or endorsed by Taito or the original rights holders.
Original game ROMs and copyrighted assets are not included.
