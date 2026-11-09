#!/bin/sh
# Builds release packages into dist/ (no ROM data, nothing derived from ROMs except the recompiled code
# compiled into the binaries - see README "Distribution" before sharing builds):
#   DariusGaiden-Recomp-Windows-x64.zip     dariusg.exe (SDL2 static) + README + licences
#   DariusGaiden-Recomp-x86_64.AppImage     Steam Deck / Linux (bundled SDL2 used only if the system has none)
#   DariusGaiden-Recomp-Linux-x86_64.tar.gz the same Linux binary without AppImage packaging
#   SHA256SUMS.txt
# needs: python3, appimagetool (APPIMAGETOOL=path). Set BUILD=0 to package binaries that were already built
# (e.g. the .exe built natively on Windows and the Linux binary built on an older distribution).
set -e
cd "$(dirname "$0")"
VER=${VER:-0.1.0}
if [ "${BUILD:-1}" = 1 ]; then ./build_frontend.sh windows; ./build_frontend.sh deck; fi
[ -f build/fe-windows/dariusg.exe ] && [ -f build/fe-deck/dariusg ] || { echo "missing build/fe-windows/dariusg.exe or build/fe-deck/dariusg"; exit 1; }
rm -rf dist; mkdir -p dist/stage
# ---- docs common
mkdir -p dist/stage/licences
cp README.md dist/stage/README.md
cp docs/CONTROLS.md docs/ROM_SETUP.md dist/stage/
cp musashi/readme.txt dist/stage/licences/Musashi.txt
cp assets/fonts/OFL-*.txt dist/stage/licences/
cp docs/THIRD_PARTY.md dist/stage/licences/
cp third_party/miniz/LICENSE dist/stage/licences/miniz.txt
DOCS="README.md CONTROLS.md ROM_SETUP.md licences"
pack_zip() { python3 -c 'import sys,os,zipfile
z=zipfile.ZipFile(sys.argv[1],"w",zipfile.ZIP_DEFLATED)
for r,d,fs in os.walk(sys.argv[2]):
    for f in fs:
        p=os.path.join(r,f); z.write(p,os.path.relpath(p,os.path.dirname(sys.argv[2])))' "$1" "$2"; }
# ---- windows
W=dist/stage/DariusGaiden-Recomp-Windows-x64; mkdir -p $W
cp build/fe-windows/dariusg.exe $W/
(cd dist/stage && cp -r $DOCS DariusGaiden-Recomp-Windows-x64/)
pack_zip dist/DariusGaiden-Recomp-Windows-x64.zip $W
# ---- linux tarball
L=dist/stage/DariusGaiden-Recomp-Linux-x86_64; mkdir -p $L
cp build/fe-deck/dariusg $L/
(cd dist/stage && cp -r $DOCS DariusGaiden-Recomp-Linux-x86_64/)
(cd dist/stage && tar czf ../DariusGaiden-Recomp-Linux-x86_64.tar.gz DariusGaiden-Recomp-Linux-x86_64)
# ---- AppImage
A=dist/stage/AppDir; mkdir -p $A/usr/bin $A/usr/lib $A/usr/share/doc/dariusg
cp build/fe-deck/dariusg $A/usr/bin/
cp third_party/sdl2linux/lib/libSDL2-2.0.so.0 $A/usr/lib/
(cd dist/stage && cp -r $DOCS AppDir/usr/share/doc/dariusg/)
cp win/dariusg.png $A/dariusg.png
cat > $A/dariusg.desktop <<'D'
[Desktop Entry]
Type=Application
Name=Darius Gaiden Recomp
Comment=Darius Gaiden (Taito, 1994) - Original and Extra Edition - native recompilation
Exec=dariusg
Icon=dariusg
Categories=Game;ArcadeGame;
D
cat > $A/AppRun <<'R'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
# prefer the system SDL2 (Steam Deck / Steam runtime builds handle gamescope + controllers best)
if ! (ldconfig -p 2>/dev/null | grep -q 'libSDL2-2.0.so.0'); then export LD_LIBRARY_PATH="$HERE/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"; fi
exec "$HERE/usr/bin/dariusg" "$@"
R
chmod +x $A/AppRun
ARCH=x86_64 ${APPIMAGETOOL:-appimagetool} --no-appstream $A dist/DariusGaiden-Recomp-x86_64.AppImage >/dev/null 2>&1
rm -rf dist/stage
(cd dist && sha256sum * > SHA256SUMS.txt && cat SHA256SUMS.txt)
