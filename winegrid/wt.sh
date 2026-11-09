#!/bin/zsh
# run wgtest.exe in the StarCitizen bottle:  ./wt.sh [on|off] <wgtest arguments>
CX=/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin
export CX_BOTTLE=StarCitizen WINEPREFIX="$HOME/Library/Application Support/CrossOver/Bottles/StarCitizen" WINEDEBUG=-all
mode=$1; shift
if [ "$mode" = on ]; then export WINEGRID=1 WINEGRID_ONLY=wgtest; else unset WINEGRID; fi
if [ -n "$RAW" ]; then exec "$CX/wine" 'C:\users\crossover\wgtest.exe' "$@" 2>/dev/null; fi   # unbuffered, for scripts that watch the output
"$CX/wine" 'C:\users\crossover\wgtest.exe' "$@" 2>/dev/null | tr -d '\r'
