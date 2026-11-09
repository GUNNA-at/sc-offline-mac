#!/bin/zsh
# install-moltenvk.sh [--install-only]: build MoltenVK 1.4.2 with the patches in this folder and install it as the
# library the shim forwards to (CrossOver's lib64/libMoltenVK.real.dylib). CrossOver's own MoltenVK is kept once as
# libMoltenVK.dylib.cx-<version>. Needs Xcode. The first build fetches MoltenVK's dependencies (about 2 minutes in all).
#   MOLTENVK_SRC=<dir>   where the MoltenVK source tree lives (default: ../moltenvk-src/MoltenVK, cloned if missing)
#   --install-only       skip the build, install what is already built there
set -e
D="$(cd "$(dirname "$0")" && pwd)"; SRC="${MOLTENVK_SRC:-$D/../moltenvk-src/MoltenVK}"
X=/Applications/CrossOver.app/Contents/SharedSupport/CrossOver; L="$X/lib64"
VER=$(defaults read /Applications/CrossOver.app/Contents/Info CFBundleShortVersionString 2>/dev/null || echo unknown)
if [ "$1" != "--install-only" ]; then
  if [ ! -d "$SRC/.git" ]; then
    mkdir -p "$(dirname "$SRC")"
    git clone --depth 1 --branch v1.4.2 https://github.com/KhronosGroup/MoltenVK.git "$SRC"
  fi
  cd "$SRC"
  for p in "$D"/moltenvk-*.patch; do
    if git apply --reverse --check "$p" 2>/dev/null; then echo "already applied: $(basename "$p")"
    else git apply "$p" && echo "applied: $(basename "$p")"; fi
  done
  [ -d External/build ] || ./fetchDependencies --macos
  make macos
fi
BUILT="$SRC/Package/Release/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib"
[ -f "$BUILT" ] || { echo "no built library at $BUILT"; exit 1; }
# CrossOver's own MoltenVK, kept once. If the shim is already in place, libMoltenVK.dylib is the shim: do not keep that.
if ! ls "$L"/libMoltenVK.dylib.cx-* >/dev/null 2>&1; then
  if otool -L "$L/libMoltenVK.dylib" | grep -q libMoltenVK.real.dylib; then
    echo "note: the shim is already installed and no copy of CrossOver's own MoltenVK was kept"
  else
    cp -p "$L/libMoltenVK.dylib" "$L/libMoltenVK.dylib.cx-$VER"; echo "kept CrossOver's MoltenVK as libMoltenVK.dylib.cx-$VER"
  fi
fi
cp "$BUILT" "$L/libMoltenVK.real.dylib.new"
install_name_tool -id @rpath/libMoltenVK.real.dylib "$L/libMoltenVK.real.dylib.new" 2>/dev/null
codesign -s - --force "$L/libMoltenVK.real.dylib.new" 2>/dev/null
mv -f "$L/libMoltenVK.real.dylib.new" "$L/libMoltenVK.real.dylib"      # a new file: a running game keeps the old one
echo "installed: $L/libMoltenVK.real.dylib"
echo "next, if the shim is not installed yet: ../mvkshim/build-and-install.sh"
