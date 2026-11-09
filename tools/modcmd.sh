#!/bin/zsh
# modcmd.sh "<command> [args]" ...: run sc-offline commands in the running game through the macfix plugin and print the replies.
#   tools/modcmd.sh "creative.god on" "creative.status"      tools/modcmd.sh "?list"      (needs the game started with the macfix plugin)
#   EXT=1 tools/modcmd.sh "!sit <ship>"   sends to a side-loaded macfix-ext (macfix/build-ext.sh) instead
U="$HOME/Library/Application Support/CrossOver/Bottles/StarCitizen/drive_c/users/crossover"; F=macfix${EXT:+-ext}; LOG="$U/$F.log"
n0=$(wc -l < "$LOG" 2>/dev/null | tr -d ' '); n0=${n0:-0}
printf '%s\n' "$@" > "$U/$F.cmd.tmp" && mv "$U/$F.cmd.tmp" "$U/$F.cmd"
for i in {1..75}; do
  got=$(tail -n +$((n0+1)) "$LOG" 2>/dev/null | grep -c '\[cmd\] .* -> \|\[cmd\] [0-9]* commands')
  [ "$got" -ge $# ] && break; sleep 0.2
done
sleep 0.3; tail -n +$((n0+1)) "$LOG" 2>/dev/null | grep '\[cmd\]' | tr -d '\r' | sed -E 's/^[0-9:.]+ \[cmd\] //'
