#!/bin/zsh
# keys.sh <sendkey arguments>: bring the game to the front and send keys / pointer moves / clicks to it (see sendkey.c).
D="$(cd "$(dirname "$0")" && pwd)"; CX=/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin
"$D/front.sh" >/dev/null || exit 1
CX_BOTTLE=StarCitizen WINEDEBUG=-all "$CX/wine" 'C:\users\crossover\sendkey.exe' "$@" 2>/dev/null | tr -d '\r' | grep -v '^game window'
