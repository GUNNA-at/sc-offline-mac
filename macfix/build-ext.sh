#!/bin/zsh
# build-ext.sh: build macfix-ext.dll (the "!" commands of the current macfix.c) for injection next to the macfix plugin that the
# running game has loaded, and inject it. For trying new commands without a game restart (a plugin cannot reload itself).
# It reads where g_api and g_self live in the INSTALLED plugin DLL, which must be the build the game loaded.
set -e
D="$(cd "$(dirname "$0")" && pwd)"; cd "$D"
U="$HOME/Library/Application Support/CrossOver/Bottles/StarCitizen/drive_c/users/crossover"
P="$U/sc-offline/data/plugins/macfix/macfix.dll"
base=$(x86_64-w64-mingw32-objdump -p "$P" | awk '/^ImageBase/{print $2}')
api=$(x86_64-w64-mingw32-nm "$P" | awk '$3=="g_api"{print $1}'); self=$(x86_64-w64-mingw32-nm "$P" | awk '$3=="g_self"{print $1}')
[ -n "$api" ] && [ -n "$self" ] || { echo "g_api / g_self not found in $P"; exit 1; }
rva_api=$(python3 -c "print(hex(int('$api',16)-int('$base',16)))"); rva_self=$(python3 -c "print(hex(int('$self',16)-int('$base',16)))")
N=${1:-1}
x86_64-w64-mingw32-gcc -O2 -Wall -Wno-unused-function -Wno-unused-variable -Wno-stringop-truncation -DMACFIX_EXT -DMACFIX_EXT_API=$rva_api -DMACFIX_EXT_SELF=$rva_self \
  -shared -o macfix-ext.dll macfix.c -lpsapi -static-libgcc
cp macfix-ext.dll "$U/macfix/macfix-ext$N.dll"
echo "g_api at +$rva_api, g_self at +$rva_self; injecting macfix-ext$N.dll"
CX=/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin
CX_BOTTLE=StarCitizen WINEDEBUG=-all "$CX/wine" 'C:\users\crossover\macfix\inject.exe' StarCitizen.exe "C:\\users\\crossover\\macfix\\macfix-ext$N.dll" 2>/dev/null | tr -d '\r'
