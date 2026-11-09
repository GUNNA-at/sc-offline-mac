#!/bin/zsh
# live.sh <switch> on|off|default ...: flip a runtime switch of the patched MoltenVK while the game runs.
#   switches: tess-prepass, async-submit        (no arguments: show what is set)
# on: the file /private/tmp/mvk-live/<switch> exists; off: no-<switch> exists; default: neither (the bottle's
# environment decides: MVK_SC_TESS_PREPASS=1, MVK_SC_ASYNC_SUBMIT=1). MoltenVK looks once a second.
D=/private/tmp/mvk-live; mkdir -p "$D"
while [ $# -ge 2 ]; do
  rm -f "$D/$1" "$D/no-$1"
  case "$2" in on) : > "$D/$1" ;; off) : > "$D/no-$1" ;; esac
  shift 2
done
echo "switches set: $(ls "$D" | tr '\n' ' ')"
