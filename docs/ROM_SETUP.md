# ROM setup

You need your own MAME romsets. Nothing is copied, extracted to disk or modified; every file is read
directly from the zip (or folder) and checked by CRC32.

## Darius Gaiden — Original (`dariusg`, Ver 2.5O 1994/09/19)

| File | Size | CRC32 | Use |
|---|---|---|---|
| d87-12.bin | 512 KB | de78f328 | 68EC020 program, byte 0 |
| d87-11.bin | 512 KB | f7bed18e | program, byte 1 |
| d87-10.bin | 512 KB | 4149f66f | program, byte 2 |
| d87-16.bin | 512 KB | 8f7e5901 | program, byte 3 |
| + the 10 shared files below | | | |

## Darius Gaiden — Extra Edition (`dariusgx`, Ver 2.7J 1995/03/06)

| File | Size | CRC32 | Use |
|---|---|---|---|
| dge_mpr3.bin | 512 KB | 1c1e24a7 | 68EC020 program, byte 0 |
| dge_mpr2.bin | 512 KB | 7be23e23 | program, byte 1 |
| dge_mpr1.bin | 512 KB | bc030f6f | program, byte 2 |
| dge_mpr0.bin | 512 KB | c5bd135c | program, byte 3 |
| + the 10 shared files below | | | |

## Shared by both editions (identical files)

| File | Size | CRC32 | Use |
|---|---|---|---|
| d87-03.bin, d87-04.bin | 2 MB each | 4be1666e, 2616002c | sprites (low planes) |
| d87-05.bin | 2 MB | 4e5891a9 | sprites (high planes) |
| d87-06.bin, d87-17.bin | 2 MB each | 3b97a07c, e601d63e | playfield tiles (low planes) |
| d87-08.bin | 2 MB | 76d23602 | playfield tiles (high planes) |
| d87-13.bin, d87-14.bin | 256 KB each | 15b1fff4, eecda29a | sound 68000 program |
| d87-01.bin, d87-02.bin | 2 MB each | 3848a110, 9250abae | ES5505 samples |

## Accepted layouts

- **Non-merged sets** (each zip complete): `dariusg.zip`, `dariusgx.zip`. Files may sit at the root of
  the zip or in a subfolder (for example `dariusgx/`).
- **Merged set:** a single `dariusg.zip` that also contains the `dge_*` files can serve both editions.
- **Split clone set:** a `dariusgx.zip` holding only the four `dge_*` files works when `dariusg.zip`, or
  a `dariusg/` folder, is in the same folder.
- **Unzipped folders:** a folder holding the files, or a `dariusg/` or `dariusgx/` folder inside it. In the
  file browser, choose **[ Use this folder ]**.
- Files are matched by CRC first, then by name, so renamed files are fine. Paths with spaces or non-ASCII
  characters work on both Windows and Linux.

Not supported: `dariusgj` (Ver 2.5J) and `dariusgu` (Ver 2.5A) have different program ROMs and aren't
recompiled in this build.

## Error messages

| Message | Meaning |
|---|---|
| "This is the … Extra Edition romset ('dariusgx'), not the … Original set" | You picked the other edition's set. Set it under the other edition in ROM Configuration. |
| "Missing program ROM … Other revisions (for example dariusgj or dariusgu) …" | The shared files are there, but not this edition's program ROMs. |
| "Missing d87-xx.bin. This looks like a split 'dariusgx' clone set …" | Put `dariusg.zip` beside it, or use a merged or non-merged set. |
| "Missing d87-xx.bin (a shared graphics/sound ROM). The set is incomplete" | One file is absent. |
| "… has a bad checksum" / "… has the wrong size" | The file is damaged or comes from another dump or revision. |
| "Cannot open … as a zip archive" | Not a zip file, or the file is damaged. |
| "… does not contain Darius Gaiden ROM files" | A folder with none of the expected files. |

The supplied development paths aren't built into the program; each user picks their own locations.
Locations are remembered per edition in `settings.json`.
