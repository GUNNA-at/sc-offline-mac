#!/bin/zsh
# install.sh: build macfix.dll and put it into the bottle as an sc-offline plugin (data/plugins/macfix), with its switches.
# Needs mingw-w64 (brew install mingw-w64). Run again after changing macfix.c; the game loads it at its next start.
set -e
D="$(cd "$(dirname "$0")" && pwd)"; cd "$D"
U="$HOME/Library/Application Support/CrossOver/Bottles/StarCitizen/drive_c/users/crossover"
P="$U/sc-offline/data/plugins/macfix"
x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -Wno-stringop-truncation -shared -o macfix.dll macfix.c -lpsapi -static-libgcc
x86_64-w64-mingw32-gcc -O2 -Wall -o inject.exe inject.c -lpsapi
x86_64-w64-mingw32-gcc -O2 -Wall -o peek.exe peek.c
mkdir -p "$P" "$U/macfix"
# Replaced by rename, never written in place: a running game may have the old file mapped.
cp macfix.dll "$P/macfix.dll.new" && mv -f "$P/macfix.dll.new" "$P/macfix.dll"; cp plugin.ini "$P/"
cp macfix.dll inject.exe peek.exe "$U/macfix/" 2>/dev/null || true     # copies for trying a build on a running game (inject.exe)
[ -f "$U/macfix.ctl" ] || printf 'padfix=1 fixgame=1 keepalive=50 clip=1 diag=0 status=60\n' > "$U/macfix.ctl"
echo "installed: $P (switches: $U/macfix.ctl, log: $U/macfix.log)"
