# Darius Gaiden recomp — Stage 1 inspection notes

Status: stage 1 record (superseded by README.md, docs/ARCHITECTURE.md and docs/VERIFICATION.md).

## Hardware (Taito F3 Package System)
- Main CPU: Motorola 68EC020 at 16 MHz. This is the program to recompile statically.
- Video: TC0630FDP (4 playfields, line scroll and zoom, sprites, text and pivot layers, alpha).
- Sound: the Taito EN board (68000 + Ensoniq ES5505 + ES5510 ESP + MC68681 DUART + MB87078).
- Storage: 93C46 serial EEPROM (high scores and operator settings).

## ROM archives (CRC32 read from the zip directories, not checked against file contents)
Shared by both sets (12 files, identical CRCs):
d87-01 3848a110, d87-02 9250abae (ES5505 samples); d87-03 4be1666e, d87-04 2616002c,
d87-05 4e5891a9 (sprites); d87-06 3b97a07c, d87-17 e601d63e, d87-08 76d23602 (tiles);
d87-13 15b1fff4, d87-14 eecda29a (sound CPU program).

Edition-specific program ROMs (68EC020, 4 x 512 KB, 32-bit interleaved):
- Original (`dariusg`): d87-12 de78f328, d87-11 f7bed18e, d87-10 4149f66f, d87-16 8f7e5901
- Extra (`dariusgx`): dge_mpr3 1c1e24a7, dge_mpr2 bc030f6f, dge_mpr1 7be23e23, dge_mpr0 c5bd135c

Notes:
- Only the main program differs. Graphics, samples and the sound program are byte-identical.
- `dariusgx.zip` is self-contained (non-merged) but stores its files in a `dariusgx/` subfolder.
  The loader has to accept files at the root and in one subfolder.
- `dariusg.zip` is TorrentZipped and also self-contained.
- Both editions need their own static recompilation, separate EEPROM files and separate save
  states. The ROM fingerprint must include the four program ROM CRCs.
- Confirmed against MAME's ROM_START blocks and GAME lines afterwards (Ver 2.5O 1994/09/19 Original; Extra Version Ver 2.7J 1995/03/06, which MAME titles "(Official Hack)").

## Reusable base: `..\gunlock-recomp` (Gunlock is also a Taito F3 game)
That project already has the F3 video, Taito EN sound, EEPROM, a Musashi fallback, the 68EC020
static recompiler (`gen.sh`), the RetroFront frontend (menus, ROM picker, save states, rewind,
CRT options, gadgets), the game-adapter/profile layout (`games/<id>/adapter_*.cpp`) and the
Windows, deck (glibc 2.17) and AppImage build scripts. The plan is to fork it and add:
1. Two profiles, `darius_gaiden_original` and `darius_gaiden_extra`, each with its own recompiled
   program and its own ROM table.
2. A runtime edition switch (tear down the current session, then start the other edition), which
   RetroFront doesn't support yet.
3. Darius Gaiden's controls (shot, bomb, plus an extra button if verified), DIP and EEPROM layout,
   and gadget addresses checked separately for each edition.
4. Display: Darius Gaiden is horizontal (YOKO). Gunlock defaults to TATE, so that default changes.

Known limitation inherited from Gunlock: the ES5510 reverb isn't emulated.

## Next steps
1. Find the full Gunlock source (`gunlock-recomp-src.tar.gz`) and copy it here.
2. Run `gen.sh` against each program set and run lockstep tests against Musashi.
3. Do the steps after that in the order of section 15 of the request.
