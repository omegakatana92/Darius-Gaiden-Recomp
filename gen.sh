#!/bin/sh
# Generates the statically recompiled C code for BOTH editions from your own romsets.
#   ./gen.sh /path/to/dariusg.zip /path/to/dariusgx.zip      (zips or unzipped folders)
# Output: gen/dg (Original), gen/dgx (Extra Edition), gen/sources.txt. Nothing derived from the ROMs is committed.
set -e
cd "$(dirname "$0")"
[ -n "$2" ] || { echo "usage: $0 /path/to/dariusg.zip /path/to/dariusgx.zip"; exit 1; }
mkdir -p rom gen/dg gen/dgx
python3 -I tools/extract_prog.py original "$1" rom/dg_main.bin
python3 -I tools/extract_prog.py extra "$2" rom/dgx_main.bin
[ -f musashi/m68kops.c ] || { gcc -O2 -o musashi/m68kmake musashi/m68kmake.c && (cd musashi && ./m68kmake . m68k_in.c); }
[ -f tools/libdis.so ] || gcc -O2 -fPIC -shared -Imusashi -o tools/libdis.so tools/dislib.c musashi/m68kdasm.c
python3 -I tools/recomp.py rom/dg_main.bin gen/dg --prefix dg --cpu 68ec020 coverage/dg_landing.txt
python3 -I tools/recomp.py rom/dgx_main.bin gen/dgx --prefix dgx --cpu 68ec020 coverage/dgx_landing.txt
{ sed 's#^#dg/#' gen/dg/sources.txt; sed 's#^#dgx/#' gen/dgx/sources.txt; } > gen/sources.txt
echo "generated gen/sources.txt ($(wc -l < gen/sources.txt) files)"
