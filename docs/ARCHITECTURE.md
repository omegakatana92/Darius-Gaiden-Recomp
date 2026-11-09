# Darius Gaiden recomp: architecture

The project is terminology-sensitive, so the terms are used precisely here:
- **Static recompilation:** the 68EC020 program of each edition is translated ahead of time into C by
  `tools/recomp.py` and compiled natively.
- **Interpretation:** Musashi executes what the recompiler leaves out (about 2% of instructions, plus any
  indirect-jump target not yet seen) and the whole sound 68000 program.
- **Reimplementation:** the rest of the board is new C code following MAME's documented behaviour.

Nothing is decompiled into readable game logic, and no external emulator is wrapped.

```
tools/recomp.py        static recompiler 68000/68EC020 -> C (recursive descent from vectors + recorded landing pads);
                       symbols are prefixed per program (--prefix dg / dgx) so both editions link into one binary
tools/extract_prog.py  interleaves one edition's four program ROMs (ROM_LOAD32_BYTE) from YOUR set
gen.sh                 extract + recompile both editions -> gen/dg, gen/dgx, gen/sources.txt (generated, not in git)
coverage/dg_landing.txt, dgx_landing.txt   indirect-jump targets recorded by interpreter runs (addresses only, no ROM data)
src/rc_editions.c      registry: edition -> recompiled chunk table + statistics
src/rc_run.c           dispatcher; rc_select_program() switches the active table when an edition is loaded
src/hw.c               F3 main board: memory map, TC0640FIO I/O, 93C46 EEPROM, watchdog, IRQ2/IRQ3 scheduling,
                       two-edition ROM loader (CRC-validated, verify-only mode), save-state container
src/cpu.c              Musashi glue: main 68EC020 + sound 68000 contexts, interrupt acknowledge, debugging hooks
src/video.c            TC0630FDP scanline compositor, non-extended mode (8 x 512x512 tilemaps), sprite lag 2, 320x232
src/sound.c            Taito EN: 68000 bus, ES5505, MC68681 timer, MB87078, ES5510 host interface, 44.1 kHz resampler
games/dariusg/         RetroFront adapter (one class, parameterised by edition) + embedded profiles
profiles/              darius_gaiden_original.json, darius_gaiden_extra.json
retrofront/            shared frontend, with the multi-edition extension described below
```

## Facts this build relies on (sources: MAME taito_f3*.cpp, and our own measurements)

| Fact | Source |
|---|---|
| Both sets: `f3` machine config, 320x232 visible (`set_raw(..., 46, 366, 262, 24, 256)`), ROT0, `init_dariusg` | MAME |
| Config table `{ DARIUSG, extend 0, sprite_lag 2 }`; dariusgx shares it through init_dariusg | MAME |
| Non-extended mode: 8 tilemaps at pf_ram + 0x1000*k; playfields 3/4 use tilemap +2 when colscroll bit 0x200 is set | MAME |
| 24-bit palette (12-bit only for SPCINVDX/RIDINGF/ARABIANM/RINGRAGE) | MAME |
| ROM layouts, CRCs, sample ROM offsets (d87-02 at 0x400000) | MAME, plus CRC32 of the actual files |
| First boot with a blank EEPROM writes defaults, masks interrupts and waits for the watchdog; the program kicks the watchdog every 1-2 frames afterwards | Measured (both editions) |
| The input routine reads 3 buttons per player; the 4th line is unused | Disassembly of the input routine |
| Button 3 toggles "[ PAUSE ]" in Extra; no visible effect in Original | Observed in scripted play |
| 1P score: 32-bit BE at 0x41b1d8 (Original), 0x41bba0 (Extra) | Matched against on-screen scores at 3 frames per edition |

Timing: 16 MHz main CPU, 271,445 cycles per 58.97 Hz frame. The sound CPU (15.238 MHz) is caught up to the
main CPU in 4096-cycle slices, and whenever the main CPU touches the dual-port RAM or the sound reset lines.

## One application, two editions

`rf::run_frontend_multi(MultiOptions)` takes a list of `EditionEntry`, each with:
- a stable id (`darius_gaiden_original`, `darius_gaiden_extra`)
- a label, long label and revision text
- its embedded profile JSON
- a factory that creates its adapter

`run_frontend()` for single-game builds is now a one-entry wrapper, so the other projects can keep using it.

- **Only the active edition has an adapter.** `switch_edition()` takes these steps in order:
  1. optional autosave, then EEPROM flush;
  2. clear the audio queue, rewind ring and thumbnails;
  3. save the settings;
  4. destroy the adapter;
  5. clear the displayed frame;
  6. build the new adapter, profile, feature set, input table and save folder;
  7. load its ROMs, which replaces the whole C machine;
  8. suppress inputs until released.
- **The C core is a single machine** (static state). Loading an edition frees and rebuilds everything it
  owns:
  - graphics and sound ROM buffers
  - the program image and the recompiled table
  - sound, video and CPU state
  - fingerprint and EEPROM

  Adapters act only while `core_edition()` equals their own edition.
- **Status of inactive editions:** a temporary adapter's `verify_content()` calls `core_verify_romset()`.
  That runs the same checks without touching the running machine.
- **Settings:** `settings.json` in `RetroFront/darius_gaiden/` is shared (display, audio, controls,
  gadgets, accent colour). Per-edition keys live under `editions.<id>.*`: ROM location and interface theme.
  Save states, EEPROM and screenshots live in `RetroFront/darius_gaiden/<edition id>/`.
- **Save-state compatibility:** three layers.
  1. The SaveManager file meta records the edition id, date and creation time; a different id is
     rejected.
  2. The adapter header holds the magic, header version, edition, ROM fingerprint, runtime compatibility
     number and size.
  3. The core blob holds its own magic, format version, fingerprint and CRC32.

  Loading is atomic: the core runs a full validation pass and restores a backup on any failure.

## RetroFront changes made for this project

- `run_frontend_multi`, `EditionEntry`, `MultiOptions`.
- New adapter API calls `verify_content()` and `state_error()`.
- New pages: Select Edition, ROM Configuration. The quick menu gains Switch Edition, and the no-content
  page shows per-edition status.
- An edition badge on the logo; Start, Continue and Load State show the edition.
- Save & Switch / Switch / Cancel dialog.
- Accent colour setting; two original themes (Abyss Teal, Ember Extra).
- Content search ignores non-zip files and reports errors only for the edition's own file names.
- `DisplayManager::clear_frame()`.
- QA automation hook `RF_AUTOTEST`, inert unless the environment variable is set.

---

# RetroFront: a universal frontend for native recompilation projects

RetroFront is a reusable C++17 frontend (UI and integration layer) for native recompilations of retro games.
It is **not an emulator**. Each game ships its own recompiled runtime and connects to the frontend through a
small adapter class. Menus, settings, saves, gadgets, display, audio and input are shared by every game.

ESP Ra.De. (`games/esprade/`) is the first adapter.

## 1. What was in the repository (analysis)

| Area | Found |
|---|---|
| Language | C11 runtime (`src/`, `gen/`), Python generator (`tools/recomp.py`) |
| CPU | 68000 code statically recompiled to C (`gen/*.c`), Musashi as the fallback interpreter (`musashi/`) |
| Rendering | Software renderer → 320×240 XRGB8888 frame buffer (`src/video.c`); SDL2 `SDL_Renderer` for presentation |
| Audio | YMZ280B port producing 44.1 kHz stereo int16, pulled once per frame |
| Input | Two 16-bit active-high masks (`g_input[]`), read by the memory-mapped I/O |
| Timing | One `core_run_frame()` = one 57.55 Hz frame. Deterministic |
| Saves | 93C46 EEPROM (high scores + settings) as a 128-byte file. **No snapshot support existed** |
| Old frontend | `src/main_sdl.c`: a minimal SDL window. Kept for reference, no longer built |

Integration decisions:

- **Recompilation engine and game logic: unchanged.** The bit-exact recompiler-vs-interpreter check still passes.
- **Save states:** added as an explicit serializer (`src/state.h` plus `hw_state` / `ymz_state` / `video_state` / `cpu_state`).
  - It is only called between frames and is versioned (magic number).
  - Loading is atomic: the input is validated, and on failure the previous state is restored.
  - Verified: save at frame N → play 2,000 frames → load → replay the same inputs gave 0/2,000 hash mismatches. A truncated file is rejected and leaves the game intact.
- **Rendering stays on SDL2's renderer** (no new GPU API dependency). The CRT effects use render targets, MOD blending, and an `SDL_RenderGeometry` warped mesh for curvature.

## 2. Module map

```
retrofront/include/rf/adapter.h   Game Adapter API (the only header a runtime needs)
retrofront/include/rf/frontend.h  run_frontend() entry point
retrofront/src/
  frontend.cpp    Frontend Core: state machine, Main Menu, In-Game Quick Menu, settings pages, loop/pacing
  menu.*          Main Menu System: declarative pages (action/toggle/choice/slider/header/info), animation, mouse
  gfx.*           2D drawing, TrueType text atlas (stb_truetype), Theme Engine (built-in + themes/*.json)
  display.*       Display Manager: orientation, aspect, integer/sharp scaling, brightness/gamma LUT, CRT pipeline
  io.*            Audio Manager (device, volume, latency, capped queue) + Controller Manager (bindings, presets, turbo, capture, hotplug)
  saves.*         Save Manager (slots + thumbnails + autosave, compressed rewind ring) + Gadget Manager
  platform.cpp    Platform Integration Layer (paths, Steam Deck/gamescope detection, file browser) + Configuration Manager
  fonts_embed.cpp Generated: embedded OFL fonts
games/esprade/    ESP Ra.De. adapter + embedded profile
profiles/         External game profiles (override the embedded ones)
themes/           Optional user themes
```

## 3. Game Adapter API (summary)

```cpp
class GameAdapter {
  GameInfo info();               // id, title, platform, refresh Hz, audio rate, cabinet rotation
  Capabilities capabilities();   // can_pause, save_states, native_save, attract_preview, gadgets[]...
  ContentInfo content();         // file names / extensions for the ROM picker and auto-search
  const std::vector<InputAction>& input_actions();  // id, label, player, role (Up/Down/.../Button/Start/Coin/Test)
  bool load_content(path, error);
  void reset();
  void run_frame(const InputState&);   // exactly one frame
  VideoFrame video();                  // CPU buffer (or an SDL_Texture* for SDL-rendered runtimes)
  size_t read_audio(int16_t*, max);    // drains produced stereo samples
  bool save_state(vector&) / load_state(ptr, n);    // exact snapshots only
  void load_native_save(dir) / flush_native_save(dir);
  bool gadget(id, GadgetValue&);       // verified game data only
  vector<ExtraAction> extras(); bool run_extra(id, msg);
  void about(kv&);
};
```

The frontend never reads game memory.
- **Score, lives and stage gadgets** exist only if an adapter declares them in `Capabilities::gadgets` and returns `valid = true`.
- **ESP Ra.De. declares none:** this release has no documented memory map for those values, so they are hidden rather than guessed.

**Feature validation.** A feature is shown only when the profile requests it **and** the adapter supports it. For example, `f_states = caps.save_states && profile.features.save_states`. Anything unsupported is hidden.

## 4. Game profile (JSON)

The profile is embedded at build time. `profiles/<game_id>.json` next to the executable, or `profile.json` in the user data folder, can override it at runtime.

```json
{
  "game_id": "esprade", "title": "ESP Ra.De.", "platform": "Arcade", "theme": "arcade_gold",
  "display":  { "aspect_ratio": "4:3", "integer_scaling": true, "filter": "nearest", "orientation": "auto" },
  "features": { "save_states": true, "rewind": true, "gadgets": true, "crt_shader": true, "attract_preview": true },
  "content":  { "search_paths": ["~/Downloads"] },
  "control_presets": { "default": { "p1.a": [{"key":"Z"},{"button":"a"}], "p1.up": [{"axis":"-lefty"}] } },
  "control_preset_names": { "default": "Standard" },
  "gadgets":  { "items": { "input": {"on": true, "x": 0.04, "y": 0.62} } }
}
```

**Binding syntax:**
- `{"key": <SDL scancode name>}`
- `{"button": <SDL game controller button name>}`
- `{"axis": "+righttrigger" | "-lefty" ...}`

Actions with no preset entry get role-based defaults, so any adapter is playable with an empty profile.

**User settings:** `settings.json` in the per-game data folder, written atomically.
- Windows: `%APPDATA%\RetroFront\<id>\`
- Linux / Steam Deck: `~/.local/share/RetroFront/<id>/`

## 5. Adding another game

1. Implement `rf::GameAdapter` around the runtime (for a Genesis recomp, wrap its per-frame entry point, VDP buffer and YM2612/PSG output).
2. `main()`: `return rf::run_frontend(std::make_unique<MyAdapter>(), opt, argc, argv);`
3. Write `profiles/<id>.json`, embed it with `tools/embed.py`, and link the `retrofront/src/*.cpp` files.

**Runtimes that don't fit the software-frame model:**
- **SDL-rendered runtimes:** pass the texture in `VideoFrame::sdl_texture`. *(The field exists, but the Display Manager doesn't consume it yet; see section 7.)*
- **OpenGL / Vulkan runtimes** (typical for PS2, GameCube or Xbox 360 recomps): these need an interop adapter (shared texture or readback). That is planned, not implemented.
- **Runtimes that can't pause safely:** set `can_pause = false`. The quick menu is then not offered.
- **Runtimes without exact snapshots:** leave `save_states = false`. Save states and rewind are then hidden automatically.

## 6. Frame loop and latency

- **Frame pacing:** a high-resolution accumulator at the adapter's refresh rate (57.55 Hz here). Input is sampled immediately before every `run_frame`.
- **Presentation:** V-Sync is optional. No frames are buffered ahead.
- **Audio:** one SDL device buffer plus about 3 frames of queue. Anything over the cap is dropped, never queued, so audio cannot drift behind the picture.
- **Leaving a menu:** gameplay input is suppressed until every bound input is released, so the button that closed the menu never reaches the game.

## 7. Implemented vs planned

| Feature | Status |
|---|---|
| Main menu (Start, Continue, Load, Display, Audio, Controller, Gameplay, Extras, About, Exit), animated, attract-mode preview | Implemented |
| In-game quick menu (Resume, Restart, Save/Load/Quick, settings, Gadgets, Screenshot, game extras, Main Menu, Exit) | Implemented |
| ROM picker (controller-navigable file browser, drag-and-drop, auto-search, remembered path, clear errors) | Implemented |
| Display: orientation TATE/YOKO, original/square/stretch aspect, integer scaling, size, nearest/bilinear/sharp-bilinear, fullscreen, V-Sync, brightness, gamma, border (theme / black / custom image) | Implemented |
| CRT: scanlines (follow tube rotation), curvature + vignette, aperture grille / shadow mask, phosphor glow, 3 presets | Implemented |
| Audio: master volume, mute, device selection, latency | Implemented. Separate music/SFX: not exposed by this hardware, so hidden |
| Controllers: per-player remapping with capture, presets, dead zone, menu shortcut, autofire, hotplug, keyboard/mouse/pad navigation, Xbox/PlayStation/Deck via SDL GameController | Implemented |
| Saves: 8 slots + quick slot + autosave/Continue, thumbnails, dates, play time, delete; native EEPROM saving | Implemented |
| Rewind (compressed ring, 96 MB cap), fast-forward, screenshots (PNG) | Implemented |
| Gadgets: FPS, frame time, play time, input display; reposition/resize/show-hide editor | Implemented |
| Gadgets: score / hi-score / lives / health / stage / timer | API implemented; hidden for ESP Ra.De. (no verified data source) |
| Themes: 4 built-in + JSON user themes | Implemented |
| Gameplay video recording | Planned. Deterministic input replay would be the natural route |
| `VideoFrame::sdl_texture` consumption, OpenGL/Vulkan runtime interop | Planned |
| Per-game artwork images in the menu (`profile.artwork`) | Planned (the menu shows the live attract preview and a typographic logo) |
| Hardware testing on a physical Steam Deck / Windows PC | **Not done**: tested under Xvfb (Linux) and Wine (Windows .exe) only |

## 8. Building

```sh
./build_frontend.sh linux     # native gcc build for testing
./build_frontend.sh deck      # Steam Deck / any distro: glibc 2.16 baseline (zig cc), uses the system libSDL2
./build_frontend.sh windows   # Windows x64, mingw-w64, SDL2 statically linked, single .exe
```

**Requirements:**
- gcc/g++ 9+ or clang
- python3, plus `pip install ziglang` for the `deck` target
- mingw-w64 for Windows
- SDL2 2.0.18+ (`SDL_RenderGeometry`)

On Windows with MSYS2 you can build natively: `pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2`, then run the `windows` target with `CC=gcc CXX=g++`.

Regenerate embedded resources after editing a profile or font:
`python3 tools/embed.py games/esprade/profile_embed.cpp esprade_profile_json=profiles/esprade.json`

## 9. Files added or changed in this stage

- **New:**
  - `retrofront/` (whole framework)
  - `games/esprade/adapter_esprade.cpp`, `games/esprade/profile_embed.cpp`
  - `profiles/esprade.json`, `themes/example_neon.json`
  - `src/state.h`, `tools/embed.py`, `build_frontend.sh`, `docs/ARCHITECTURE.md`
  - `assets/fonts/` (OFL fonts + licences)
  - `third_party/json`, `third_party/stb`
- **Changed:**
  - `src/hw.c`: state serializer, `core_state_save` / `load`, `core_last_error`, `core_eeprom_factory`, frees old data when a ROM is reloaded
  - `src/cpu.c`, `src/ymz280b.c`, `src/video.c`: state serializers
  - `src/core.h`: C++ guards, new API
  - `src/main_test.c`: save-state determinism test (`STATE_TEST=<frame>`)
- **Unchanged:** the recompiler, the generated code, and the CPU/video/sound emulation logic.
