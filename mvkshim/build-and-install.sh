#!/bin/zsh
# Rebuild the MoltenVK shim and install it in front of MoltenVK 1.4.2 inside CrossOver.
set -e; D="$(cd "$(dirname "$0")" && pwd)"; L=/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/lib64
cd "$L"
[ -f libMoltenVK.real.dylib ] || cp libMoltenVK.dylib libMoltenVK.real.dylib          # first run: current lib (1.4.2) becomes backend
otool -D libMoltenVK.real.dylib | grep -q 'libMoltenVK.real.dylib' || { install_name_tool -id @rpath/libMoltenVK.real.dylib libMoltenVK.real.dylib; codesign -s - --force libMoltenVK.real.dylib; }
cd "$D"
clang -O2 -arch x86_64 -arch arm64 -dynamiclib -I/opt/homebrew/include -install_name @rpath/libMoltenVK.dylib \
      -Wl,-reexport_library,"$L/libMoltenVK.real.dylib" -o libMoltenVK.shim.dylib shim.c
codesign -s - --force libMoltenVK.shim.dylib
cp libMoltenVK.shim.dylib "$L/libMoltenVK.dylib.new" && mv -f "$L/libMoltenVK.dylib.new" "$L/libMoltenVK.dylib"   # new inode: safe while a running game still maps the old file
echo "installed: $L/libMoltenVK.dylib (shim, re-exports libMoltenVK.real.dylib)"
