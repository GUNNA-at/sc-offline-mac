#!/bin/zsh
# standup.sh [keys...]: give the game keyboard focus for a few seconds, press keys in it (default: hold Y, which leaves
# the bed you wake up in), then hand focus back to the app that had it. For tests without hands on the keyboard.
D="$(cd "$(dirname "$0")" && pwd)"; CX=/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin
PID=$(ps -axo pid,command | grep 'Bin64.StarCitizen.exe' | grep -v -e grep -e zsh | awk '{print $1}' | head -1)
[ -z "$PID" ] && { echo "game is not running"; exit 1; }
R=$("$D/front.sh" $PID); PREV=${R%% *}
[ $# -eq 0 ] && set -- 400 Y:1500 600
CX_BOTTLE=StarCitizen WINEDEBUG=-all "$CX/wine" 'C:\users\crossover\sendkey.exe' "$@" 2>/dev/null | tr -d '\r'
[ -n "$PREV" ] && [ "$PREV" != 0 ] && [ "$PREV" != "$PID" ] && [ -z "$KEEP_FRONT" ] && "$D/front.sh" $PREV >/dev/null
