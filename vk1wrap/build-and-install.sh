#!/bin/zsh
# Build the Windows-side vulkan-1.dll wrapper (MinGW) and install it into the StarCitizen bottle with a native override.
set -e; D="$(cd "$(dirname "$0")" && pwd)"; B="$HOME/Library/Application Support/CrossOver/Bottles/StarCitizen"; S32="$B/drive_c/windows/system32"
CX=/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin
cd "$D"; x86_64-w64-mingw32-gcc -O1 -shared -I/opt/homebrew/include -o vulkan-1.dll wrap.c wrap.def
[ -f "$S32/vulkan-1.dll.wine-fake" ] || cp "$S32/vulkan-1.dll" "$S32/vulkan-1.dll.wine-fake"
cp vulkan-1.dll "$S32/vulkan-1.dll"
CX_BOTTLE=StarCitizen WINEPREFIX="$B" "$CX/wine" reg add 'HKCU\Software\Wine\DllOverrides' /v vulkan-1 /t REG_SZ /d 'native,builtin' /f >/dev/null 2>&1
echo "installed system32/vulkan-1.dll wrapper (override native,builtin); remove: restore vulkan-1.dll.wine-fake and delete the override"
