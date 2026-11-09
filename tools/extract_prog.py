#!/usr/bin/env python3
"""extract and interleave one edition's 68EC020 program ROMs (ROM_LOAD32_BYTE) from YOUR romset (zip or folder)
usage: extract_prog.py original|extra <romset.zip|folder> <out.bin>"""
import sys, os, zipfile, zlib
SETS = {
    'original': [('d87-12.bin', 0xde78f328), ('d87-11.bin', 0xf7bed18e), ('d87-10.bin', 0x4149f66f), ('d87-16.bin', 0x8f7e5901)],
    'extra':    [('dge_mpr3.bin', 0x1c1e24a7), ('dge_mpr2.bin', 0x7be23e23), ('dge_mpr1.bin', 0xbc030f6f), ('dge_mpr0.bin', 0xc5bd135c)],
}
ed, src, out_path = sys.argv[1], sys.argv[2], sys.argv[3]
want = SETS[ed]
def get(name, crc):
    if os.path.isdir(src):
        for sub in ('', 'dariusg', 'dariusgx'):
            p = os.path.join(src, sub, name)
            if os.path.exists(p): return open(p, 'rb').read()
        sys.exit('%s: not found in %s' % (name, src))
    z = zipfile.ZipFile(src)
    for i in z.infolist():
        if i.CRC == crc: return z.read(i)
    for i in z.infolist():
        if i.filename.split('/')[-1] == name: return z.read(i)
    sys.exit('%s: not found in %s (is this the right edition?)' % (name, src))
parts = []
for n, c in want:
    d = get(n, c)
    if zlib.crc32(d) != c: sys.exit('%s: bad CRC (not the MAME %s set)' % (n, 'dariusg' if ed == 'original' else 'dariusgx'))
    parts.append(d)
out = bytearray(len(parts[0]) * 4)
for k, p in enumerate(parts): out[k::4] = p
open(out_path, 'wb').write(out)
