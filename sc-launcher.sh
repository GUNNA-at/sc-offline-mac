#!/bin/zsh
# Start the RSI Launcher inside the CrossOver "StarCitizen" bottle.
# Usage:  ./sc-launcher.sh            -> start the RSI Launcher (SwiftShader GL in-process: the flags that render on macOS)
#         ./sc-launcher.sh kill       -> stop everything running in the bottle
#         ./sc-launcher.sh shell      -> open a cmd.exe in the bottle (debugging)
#         ./sc-launcher.sh log        -> tail the launcher's own log
#         ./sc-launcher.sh plain      -> start with no Chromium flags (white window on this Mac, kept for testing)
CX=/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin
export CX_BOTTLE=StarCitizen
export WINEPREFIX="$HOME/Library/Application Support/CrossOver/Bottles/StarCitizen"
export WINEDEBUG=-all
export WINEDLLOVERRIDES="winemenubuilder.exe=d"
LAUNCHER='C:\Program Files\Roberts Space Industries\RSI Launcher\RSI Launcher.exe'
LOG="$WINEPREFIX/sc-launcher.log"
# Found 2026-11-08 by sweeping Chromium flags: Wine's D3D11 path crashes the GPU process and the
# software fallback never paints under CrossOver's Mac driver; ANGLE on SwiftShader, in-process, renders.
RENDER_FLAGS=(--use-gl=angle --use-angle=swiftshader --in-process-gpu --disable-features=CalculateNativeWinOcclusion)
case "$1" in
  kill)  "$CX/wineserver" -k; echo "bottle processes stopped" ;;
  shell) "$CX/wine" cmd ;;
  log)   tail -n 50 -f "$WINEPREFIX/drive_c/users/crossover/AppData/Roaming/rsilauncher/logs/"*.log ;;
  plain) "$CX/wineserver" -k 2>/dev/null; sleep 2; exec "$CX/wine" "$LAUNCHER" >"$LOG" 2>&1 ;;
  *)     "$CX/wineserver" -k 2>/dev/null; sleep 2; exec "$CX/wine" "$LAUNCHER" "${RENDER_FLAGS[@]}" "$@" >"$LOG" 2>&1 ;;
esac
