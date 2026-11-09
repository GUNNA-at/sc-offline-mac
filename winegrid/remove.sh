#!/bin/zsh
# Undo install.sh: put CrossOver's original ntdll.so back and delete the library.
X=/Applications/CrossOver.app/Contents/SharedSupport/CrossOver; N="$X/lib/wine/x86_64-unix/ntdll.so"
B=$(ls "$N".cx-* 2>/dev/null | head -1)
if [ -n "$B" ]; then cp -p "$B" "$N.new" && mv -f "$N.new" "$N" && echo "restored $N from $B"; else echo "no backup of ntdll.so found (nothing was patched?)"; fi
rm -f "$X/lib64/libwinegrid.dylib" && echo "removed libwinegrid.dylib"
