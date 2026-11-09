#!/bin/zsh
# Build winegrid and make CrossOver's Wine load it at process start (a weak dependency added to ntdll.so).
# Nothing changes for a bottle unless its environment has WINEGRID=1. Undo with ./remove.sh.
set -e; D="$(cd "$(dirname "$0")" && pwd)"; X=/Applications/CrossOver.app/Contents/SharedSupport/CrossOver
L="$X/lib64"; N="$X/lib/wine/x86_64-unix/ntdll.so"; LIB="$L/libwinegrid.dylib"
VER=$(defaults read /Applications/CrossOver.app/Contents/Info CFBundleShortVersionString 2>/dev/null || echo unknown)
cd "$D"
clang -O2 -arch x86_64 -dynamiclib -install_name "$LIB" -o libwinegrid.dylib grid.c
codesign -s - --force libwinegrid.dylib
cp libwinegrid.dylib "$LIB.new" && mv -f "$LIB.new" "$LIB"            # new inode: safe while Wine processes still map the old one
echo "installed: $LIB"
if python3 -I "$D/patch-ntdll.py" check "$N" "$LIB"; then echo "ntdll.so already loads it"; exit 0; fi
[ -f "$N.cx-$VER" ] || cp -p "$N" "$N.cx-$VER"                        # the original, once
cp -p "$N" "$N.new"
python3 -I "$D/patch-ntdll.py" add "$N.new" "$LIB"
codesign -s - --force "$N.new"
# the patched file must load, and must pull the library in, before it replaces the original
WINEGRID=1 WINEGRID_LOG=/dev/stderr arch -x86_64 /usr/bin/python3 -I -c "import ctypes,sys; ctypes.CDLL(sys.argv[1]); print('load test ok')" "$N.new" || { echo "patched ntdll.so does not load, leaving the original in place"; rm -f "$N.new"; exit 1; }
mv -f "$N.new" "$N"
echo "patched: $N (original kept as $N.cx-$VER)"
