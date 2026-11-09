# Verification record (2026-11-08)

Everything below was run in this session. All runs are scripted (no human play), and **no physical
controller, Steam Deck or listening test was done**.

Environments:
- Windows 11 x64 (mingw-w64 13.2 build, run natively)
- WSL Ubuntu 24.04: gcc 13, SDL2 2.30, Xvfb
- Steam Deck / Linux release binary: zig 0.17.1 targeting glibc 2.17 (built in WSL Ubuntu 24.04); highest
  required symbol GLIBC_2.16, no libstdc++

## ROM sets (user's archives)

- Every file in `dariusg.zip` (14 files) and `dariusgx.zip` (14 files, inside a `dariusgx/` subfolder) was
  decompressed and its CRC32 compared with the zip directory and with MAME's `dariusg` / `dariusgx`
  ROM_START blocks. All matched.
- 10 files are byte-identical between the sets. Only the four 68EC020 program ROMs differ.
- Interleaving the program ROMs gives valid reset vectors in both editions (SSP 0x41fffc, PC 0x010000)
  and readable strings. The Extra program contains "EXTRA VERSION". 1,478,286 of 2,097,152 program bytes
  differ.
- Both archives are self-contained: no parent set is needed. A split `dariusgx` (only `dge_*`) is supported
  via a parent `dariusg.zip` or `dariusg/` folder beside it.

## Core

| Check | Result |
|---|---|
| Boot both editions (blank EEPROM -> defaults -> watchdog restart -> title) | Pass; one watchdog reset per first boot, then kicks every <= 2 frames |
| Recompiled vs interpreter, 12,000 frames of random play, per-frame machine hash | **Identical**: Original 21,064/21,541 native; Extra 21,955/22,457 native |
| Save state round trip (save at 5000, replay 2000 frames with recorded inputs) | 0/2000 mismatches, both editions; 733 KB / 726 KB per state |
| Save in one process, load in a new process, 800 frames of scripted play | Identical, both editions |
| Truncated state / one corrupted byte | Rejected, running state unchanged |
| Original state loaded into Extra, and vice versa | Rejected |
| Audio | Non-silent output in both editions (about 13.3M / 11.7M non-zero samples in 12,000 frames); **not listened to** |
| Buttons | B1 shot (projectiles observed), B2 bomb (bomb count 3 -> 2, black-hole effect), B3: Extra shows "[ PAUSE ]", Original no visible effect; B4 never read (input routine at 0x1ab4 / 0x1b52) |
| Score gadget | Original 0x41b1d8 = 6800/9600/15600 at 3 frames; Extra 0x41bba0 = 8500/37700/57700 at 3 frames, all matching the HUD |

## ROM validation cases

These were core-level checks; the path holds a space and Japanese characters.

| Input | Edition | Result |
|---|---|---|
| split dariusgx.zip + dariusg.zip beside it | Extra | loads |
| split dariusgx.zip alone | Extra | "Missing d87-03.bin. This looks like a split 'dariusgx' clone set …" |
| d87-06.bin with one flipped bit | Extra | "d87-06.bin has a bad checksum …" |
| d87-05.bin removed | Extra | "Missing d87-05.bin (a shared graphics/sound ROM). The set is incomplete." |
| dariusgx.zip renamed dariusg.zip | Original | "This is the Darius Gaiden - Extra Edition romset ('dariusgx') …" |
| same file | Extra | loads (CRC match, name ignored) |
| unzipped folder / folder with a dariusgx/ subfolder | Extra | loads |
| merged dariusg.zip (both program sets) | Original and Extra | both load |
| text file named .zip; missing folder | Original | clear errors |
| dariusgx.zip offered to Original, dariusg.zip offered to Extra | – | each names the correct edition |

## Frontend (RF_AUTOTEST scripts)

| Scenario | Linux/Xvfb | Windows native | AppImage/Xvfb |
|---|---|---|---|
| Startup with only the Original set: main menu, Select Edition shows Extra "ROM needed" | Pass | – | – |
| Select Extra with no ROM: "ROM SET NEEDED" page with the edition's requirements; switch back | Pass | – | – |
| Start Original, coin, start, play; F5 quick save | Pass | Pass (score gadget = HUD 8000) | – |
| Quick menu › Switch Edition › Save & Switch -> Extra main menu, autosave written | Pass | Pass (Switch) | – |
| Extra: play, quick save, quick load ("Quick load" toast) | Pass | – | – |
| Return to menu, switch back, Continue resumes the Original autosave | Pass | – | – |
| 40 consecutive edition switches from the main menu | Pass; RSS flat at 278 MB after the first switch | – | – |
| ROM found next to the AppImage; data written to ~/.local/share/RetroFront/darius_gaiden/<edition>/ | – | – | Pass |
| Full play / save / Save & Switch / Continue script with the zig-built release binary | Pass | – | – |
| Last edition remembered across launches | – | Pass | – |

## Not verified

- Listening quality, A/V sync on real displays, and latency.
- Physical Xbox/PlayStation/Steam Deck controllers, hotplug, gamescope, Wayland.
- SteamOS itself (the binary's glibc 2.17 baseline was checked with objdump only).
- Display/CRT options beyond the default rendering.
- Rewind and fast-forward through the UI; they use the snapshot path tested above.
- Long sessions beyond stage A.
