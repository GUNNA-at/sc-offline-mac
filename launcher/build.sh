#!/bin/zsh
# build.sh: build the kit's parts and the launcher into launcher/build/sc-offline-mac.app
#   needs Xcode (clang, swiftc), mingw-w64 (brew install mingw-w64 vulkan-headers) and a built MoltenVK
#   (patches/install-moltenvk.sh builds it; MOLTENVK_SRC points at the source tree if it is elsewhere)
set -e
D="$(cd "$(dirname "$0")" && pwd)"; ROOT="$(dirname "$D")"; B="$D/build"; APP="$B/sc-offline-mac.app"; K="$B/kit"
VERSION=$(sed -n 's/.*static let version = "\(.*\)".*/\1/p' "$D/Sources/Engine.swift" | head -1)
MVK="${MOLTENVK_SRC:-$ROOT/moltenvk-src/MoltenVK}/Package/Release/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib"
[ -f "$MVK" ] || { echo "no built MoltenVK at $MVK: run patches/install-moltenvk.sh first"; exit 1; }
sign() { codesign -s - --force "$@" 2>/dev/null; }
rm -rf "$K" "$APP"; mkdir -p "$K"

# Mac side: the patched MoltenVK, the shim in front of it, winegrid, the squeezer's balloon
cp "$MVK" "$K/libMoltenVK.real.dylib"
install_name_tool -id @rpath/libMoltenVK.real.dylib "$K/libMoltenVK.real.dylib" 2>/dev/null; sign "$K/libMoltenVK.real.dylib"
clang -O2 -arch x86_64 -arch arm64 -dynamiclib -I/opt/homebrew/include -install_name @rpath/libMoltenVK.dylib \
      -Wl,-reexport_library,"$K/libMoltenVK.real.dylib" -o "$K/libMoltenVK.dylib" "$ROOT/mvkshim/shim.c"
install_name_tool -change @rpath/libMoltenVK.real.dylib @loader_path/libMoltenVK.real.dylib "$K/libMoltenVK.dylib" 2>/dev/null; sign "$K/libMoltenVK.dylib"
clang -O2 -arch x86_64 -arch arm64 -dynamiclib -install_name @rpath/libwinegrid.dylib -o "$K/libwinegrid.dylib" "$ROOT/winegrid/grid.c"; sign "$K/libwinegrid.dylib"
clang -O2 -arch arm64 -arch x86_64 -o "$K/balloon" "$ROOT/winegrid/balloon.c"; sign "$K/balloon"

# Windows side: the Vulkan wrapper, the mod's Mac plugin, the dialog clicker
( cd "$ROOT/vk1wrap" && x86_64-w64-mingw32-gcc -O1 -shared -I/opt/homebrew/include -o "$K/vulkan-1.dll" wrap.c wrap.def )
( cd "$ROOT/macfix" && x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -Wno-stringop-truncation -shared -o "$K/macfix.dll" macfix.c -lpsapi -static-libgcc && cp plugin.ini "$K/" )
x86_64-w64-mingw32-gcc -O2 -Wall -municode -o "$K/dismiss.exe" "$ROOT/tools/dismiss.c" -static-libgcc

# settings: the graphics profiles and the lines every profile sits on
cp -R "$ROOT/profiles" "$K/profiles"
perl -ne 'last if /^-- >>> profile:/; print unless /r_DisplayInfo|overlay while we measure/' "$ROOT/config/user.cfg.example" > "$K/user.cfg.base"
cp "$ROOT/LICENSE" "$K/LICENSE"
# what is in this folder and under which licence
MVKSRC="$(dirname "$(dirname "$(dirname "$(dirname "$(dirname "$(dirname "$(dirname "$MVK")")")")")")")"
[ -f "$MVKSRC/LICENSE" ] && cp "$MVKSRC/LICENSE" "$K/LICENSE-MoltenVK"
cat > "$K/NOTICE.txt" <<NOTICE
sc-offline-mac $VERSION, https://github.com/GUNNA-at/sc-offline-mac

libMoltenVK.real.dylib is MoltenVK 1.4.2 by the Khronos Group (Apache License 2.0, see LICENSE-MoltenVK), built from
its source with this project's patches (the patches folder of the repository).

Everything else in this folder was written for this project and is under the GNU General Public License 3.0 (LICENSE):
libMoltenVK.dylib (the shim), libwinegrid.dylib, balloon, vulkan-1.dll, macfix.dll, dismiss.exe, the profiles.

The launcher downloads Wine, DXVK, the sc-offline mod and the publisher's launcher from their own sources when you
press SET UP. None of them is in this folder. No game files are in this folder.

The pictures on the launcher's Play page (the ui/art folder of the app) are photographs from NASA's open archives,
only cut to shape and resized: iss072e574663 (NASA, Don Pettit), iss061e123823 (NASA) and the Webb telescope's
Cosmic Cliffs (NASA, ESA, CSA, STScI). Their sources and terms are in ui/art/CREDITS.md. None of these bodies has
anything to do with this project or endorses it.
NOTICE

# the app
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
swiftc -O -swift-version 5 -target arm64-apple-macos13.0 -framework AppKit -framework WebKit "$D"/Sources/*.swift -o "$APP/Contents/MacOS/sc-offline-mac"
cp -R "$K" "$APP/Contents/Resources/kit"
cp -R "$D/ui" "$APP/Contents/Resources/ui"
[ -f "$D/AppIcon.icns" ] && cp "$D/AppIcon.icns" "$APP/Contents/Resources/AppIcon.icns"
cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>sc-offline-mac</string>
  <key>CFBundleDisplayName</key><string>sc-offline-mac</string>
  <key>CFBundleIdentifier</key><string>io.github.gunna-at.sc-offline-mac</string>
  <key>CFBundleExecutable</key><string>sc-offline-mac</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>$VERSION</string>
  <key>CFBundleVersion</key><string>$VERSION</string>
  <key>CFBundleIconFile</key><string>AppIcon</string>
  <key>LSMinimumSystemVersion</key><string>13.0</string>
  <key>LSArchitecturePriority</key><array><string>arm64</string></array>
  <key>NSHighResolutionCapable</key><true/>
  <key>LSApplicationCategoryType</key><string>public.app-category.utilities</string>
  <key>NSHumanReadableCopyright</key><string>GPL-3.0. An unofficial fan project, published for research and education.</string>
</dict></plist>
PLIST
codesign -s - --force --deep "$APP" 2>/dev/null
echo "built $APP ($(du -sh "$APP" | cut -f1)), version $VERSION"
ls "$K"
