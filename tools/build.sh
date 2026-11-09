#!/bin/zsh
# build.sh: compile the small helper tools next to their sources. The Mac-side ones need Xcode (swiftc, clang); the
# Windows-side ones need mingw-w64 and are copied into the bottle, where keys.sh and friends expect them.
# Every binary is written under a new name and renamed over the old one: a helper that is running (the squeezer's
# balloon, for one) must not have its file rewritten under it.
D="$(cd "$(dirname "$0")" && pwd)"; R="$(cd "$D/.." && pwd)"
U="$HOME/Library/Application Support/CrossOver/Bottles/StarCitizen/drive_c/users/crossover"
cd "$D" || exit 1
for t in winlist whiteness activate rec psotest rtcompile; do
  swiftc -O "$t.swift" -o "$t.new" 2>/dev/null && mv -f "$t.new" "$t" && echo "built tools/$t" || echo "FAILED tools/$t"
done
clang -O2 -o "$R/winegrid/balloon.new" "$R/winegrid/balloon.c" && mv -f "$R/winegrid/balloon.new" "$R/winegrid/balloon" && echo "built winegrid/balloon"
for t in sendkey wininfo; do
  x86_64-w64-mingw32-gcc -O2 -o "$t.new" "$t.c" && mv -f "$t.new" "$t.exe" && echo "built tools/$t.exe" || echo "FAILED tools/$t.exe"
done
[ -d "$U" ] && cp sendkey.exe wininfo.exe "$U/" && echo "copied sendkey.exe and wininfo.exe into the bottle"
