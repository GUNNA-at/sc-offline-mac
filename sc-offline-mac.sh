#!/bin/zsh
# Star Citizen OFFLINE (sc-offline mod) inside the CrossOver "StarCitizen" bottle. Never touches CIG servers.
# Every game launch runs inside a macOS sandbox that denies ALL network to the game's process tree
# (no root needed, Wi-Fi can stay on). Only the Wine-internal unix sockets and localhost are allowed.
# Usage:  ./sc-offline-mac.sh play        -> start the game offline with the mod (default)
#         ./sc-offline-mac.sh vanilla     -> plain client, no mod, renderer smoke test (also network-sandboxed)
#         ./sc-offline-mac.sh status      -> what the mod has installed / left behind
#         ./sc-offline-mac.sh uninstall   -> remove the mod from Bin64 (before ever using the RSI Launcher again)
#         ./sc-offline-mac.sh dry         -> show what play would do, start nothing
#         ./sc-offline-mac.sh nettest     -> prove the sandbox blocks TCP+DNS for a Windows process
#         ./sc-offline-mac.sh profile [pretty|lean|low] -> graphics profile for the next launch (no name: list)
#         ./sc-offline-mac.sh squeeze [GB] -> one manual memory squeeze (normally automatic while playing)
#         ./sc-offline-mac.sh kill        -> stop everything in the bottle\n#         ./sc-offline-mac.sh mute|unmute -> game audio off/on (Wine audio driver), applies to the next launch
#         ./sc-offline-mac.sh direct [+cvar value ...] -> mod `install`, start StarCitizen.exe directly with these console variables
#                                        (default: +sys_watchdog_enable_hard_timeout 0), `uninstall` afterwards
CX=/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin
export CX_BOTTLE=StarCitizen
export WINEPREFIX="$HOME/Library/Application Support/CrossOver/Bottles/StarCitizen"
export WINEDEBUG=-all
MOD='C:\users\crossover\sc-offline\sc-offline.exe'
GAME='C:\Program Files\Roberts Space Industries\StarCitizen'
BIN64="$WINEPREFIX/drive_c/Program Files/Roberts Space Industries/StarCitizen/LIVE/Bin64"
PWSH='C:\Program Files\PowerShell\7\pwsh.exe'
NONET='(version 1)
(allow default)
(deny network*)
(allow network* (subpath "/private/tmp"))
(allow network* (subpath "/private/var/folders"))
(allow network-outbound (remote ip "localhost:*"))
(allow network-inbound (local ip "localhost:*"))'
SHOTS="${SHOTS:-$(cd "$(dirname "$0")" && pwd)/tools}"   # winlist/shot.sh live here (screenshots of the crash dialog); optional
fresh() { "$CX/wineserver" -k 2>/dev/null; sleep 2; }   # a wineserver started inside the sandbox stays inside it
case "${1:-play}" in
  kill)      "$CX/wineserver" -k; echo "bottle processes stopped" ;;
  status)    WINEDLLOVERRIDES="dinput8=n,b" "$CX/wine" "$MOD" status --game "$GAME" 2>/dev/null | tr -d '\r' ;;
  uninstall) WINEDLLOVERRIDES="dinput8=n,b" "$CX/wine" "$MOD" uninstall --game "$GAME" 2>/dev/null | tr -d '\r' ;;
  dry)       WINEDLLOVERRIDES="dinput8=n,b" "$CX/wine" "$MOD" play --game "$GAME" --dry-run 2>/dev/null | tr -d '\r' ;;
  nettest)   fresh; sandbox-exec -p "$NONET" "$CX/wine" "$PWSH" -NoProfile -NonInteractive -Command 'try { $c=New-Object Net.Sockets.TcpClient; $c.Connect("1.1.1.1",443); "TCP CONNECTED (sandbox NOT working)" } catch { "TCP blocked: OK" }; try { [Net.Dns]::GetHostAddresses("robertsspaceindustries.com") | Out-Null; "DNS RESOLVED (sandbox NOT working)" } catch { "DNS blocked: OK" }' 2>/dev/null | tr -d '\r' | grep -E 'TCP|DNS' ;;
  vanilla)   fresh; cd "$BIN64" && exec sandbox-exec -p "$NONET" "$CX/wine" 'C:\Program Files\Roberts Space Industries\StarCitizen\LIVE\Bin64\StarCitizen.exe' >"$WINEPREFIX/sc-vanilla.log" 2>&1 ;;
  play)      fresh
    # in-bottle dialog clicker: presses OK/Yes on any message box StarCitizen.exe / CrashHandler.exe raise (exits by itself 20 s after the game is gone)
    ( sleep 15; "$CX/wine" "$PWSH" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File 'C:\users\crossover\sc-offline\dismiss.ps1' ) >"$WINEPREFIX/dismiss-wine.log" 2>&1 &
    # auto-dismiss: when the engine logs a fatal/crash, let its in-process crash handler finish writing error.dmp (readable with
    # the scratchpad's crash/mdump.py), screenshot what it shows, then kill the game and the mod helper. SC_KEEP_CRASH=1 skips the kill.
    ( G="$WINEPREFIX/drive_c/Program Files/Roberts Space Industries/StarCitizen/LIVE/Game.log"; DMP="$WINEPREFIX/drive_c/users/crossover/AppData/Local/star citizen/crashes/error.dmp"
      old=$(sed -n 2p "$G" 2>/dev/null); sleep 30
      while pgrep -f '[B]in64.StarCitizen.exe' >/dev/null; do
        cur=$(sed -n 2p "$G" 2>/dev/null)   # only trust Game.log once THIS run has rewritten it
        if [ -f "$G" ] && [ "$cur" != "$old" ] && grep -qE 'FATAL ERROR|crash handler taking over' "$G"; then
          t0=$(date +%s); last=-1; same=0
          while [ $(( $(date +%s) - t0 )) -lt 120 ] && pgrep -f '[B]in64.StarCitizen.exe' >/dev/null; do
            sz=$(stat -f %z "$DMP" 2>/dev/null || echo 0)
            if [ "$sz" -gt 0 ] && [ "$sz" = "$last" ]; then same=$((same+1)); else same=0; fi; last=$sz
            [ "$same" -ge 2 ] && break; sleep 3; done
          stamp=$(date +%H%M%S); cp "$DMP" "$WINEPREFIX/crash-$stamp.dmp" 2>/dev/null
          [ -x "$SHOTS/shot.sh" ] && "$SHOTS/shot.sh" "crash-$stamp" >/dev/null 2>&1
          if [ "${SC_KEEP_CRASH:-0}" = 1 ]; then echo "$(date +%T) fatal error; SC_KEEP_CRASH=1 so leaving it running (dump: crash-$stamp.dmp, ${last} bytes)" >> "$WINEPREFIX/sc-offline-mac.log"; break; fi
          pkill -f '[C]rashHandler.exe'; pkill -f '[B]in64.StarCitizen.exe'; sleep 3; pkill -9 -f '[B]in64.StarCitizen.exe' 2>/dev/null
          echo "$(date +%T) auto-dismissed a fatal error (see Game.log; dump: crash-$stamp.dmp, ${last} bytes)" >> "$WINEPREFIX/sc-offline-mac.log"; break; fi
        sleep 3; done ) >/dev/null 2>&1 &
    # memory keeper: once the game is in the world, moves compressed data the game never touches again to swap so that live data
    # stays uncompressed (tools/squeezer.sh -> winegrid/balloon auto; log /private/tmp/sc-squeezer.log). SC_NO_SQUEEZE=1 turns it off.
    [ "${SC_NO_SQUEEZE:-0}" = 1 ] || ( "$(cd "$(dirname "$0")" && pwd)/tools/squeezer.sh" >/dev/null 2>&1 & )
    exec sandbox-exec -p "$NONET" env WINEDLLOVERRIDES="dinput8=n,b" "$CX/wine" "$MOD" play --game "$GAME" >"$WINEPREFIX/sc-offline-mac.log" 2>&1 ;;
  direct)    fresh   # start StarCitizen.exe ourselves after the mod's own `install`, so console variables can be passed as +cvar args
    shift; [ $# -eq 0 ] && set -- +sys_watchdog_enable_hard_timeout 0 +sys_watchdog_hard_timeout_loading 3600
    WINEDLLOVERRIDES="dinput8=n,b" "$CX/wine" "$MOD" install --game "$GAME" 2>/dev/null | tr -d '\r' | grep -E 'Mod:|Installed|\[!\]' | tail -n 3
    echo "$(date +%T) direct start: StarCitizen.exe $*" >> "$WINEPREFIX/sc-offline-mac.log"
    # in-bottle dialog clicker: presses OK/Yes on any message box StarCitizen.exe / CrashHandler.exe raise (exits by itself 20 s after the game is gone)
    ( sleep 15; "$CX/wine" "$PWSH" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File 'C:\users\crossover\sc-offline\dismiss.ps1' ) >"$WINEPREFIX/dismiss-wine.log" 2>&1 &
    # auto-dismiss: when the engine logs a fatal/crash, let its in-process crash handler finish writing error.dmp (readable with
    # the scratchpad's crash/mdump.py), screenshot what it shows, then kill the game and the mod helper. SC_KEEP_CRASH=1 skips the kill.
    ( G="$WINEPREFIX/drive_c/Program Files/Roberts Space Industries/StarCitizen/LIVE/Game.log"; DMP="$WINEPREFIX/drive_c/users/crossover/AppData/Local/star citizen/crashes/error.dmp"
      old=$(sed -n 2p "$G" 2>/dev/null); sleep 30
      while pgrep -f '[B]in64.StarCitizen.exe' >/dev/null; do
        cur=$(sed -n 2p "$G" 2>/dev/null)   # only trust Game.log once THIS run has rewritten it
        if [ -f "$G" ] && [ "$cur" != "$old" ] && grep -qE 'FATAL ERROR|crash handler taking over' "$G"; then
          t0=$(date +%s); last=-1; same=0
          while [ $(( $(date +%s) - t0 )) -lt 120 ] && pgrep -f '[B]in64.StarCitizen.exe' >/dev/null; do
            sz=$(stat -f %z "$DMP" 2>/dev/null || echo 0)
            if [ "$sz" -gt 0 ] && [ "$sz" = "$last" ]; then same=$((same+1)); else same=0; fi; last=$sz
            [ "$same" -ge 2 ] && break; sleep 3; done
          stamp=$(date +%H%M%S); cp "$DMP" "$WINEPREFIX/crash-$stamp.dmp" 2>/dev/null
          [ -x "$SHOTS/shot.sh" ] && "$SHOTS/shot.sh" "crash-$stamp" >/dev/null 2>&1
          if [ "${SC_KEEP_CRASH:-0}" = 1 ]; then echo "$(date +%T) fatal error; SC_KEEP_CRASH=1 so leaving it running (dump: crash-$stamp.dmp, ${last} bytes)" >> "$WINEPREFIX/sc-offline-mac.log"; break; fi
          pkill -f '[C]rashHandler.exe'; pkill -f '[B]in64.StarCitizen.exe'; sleep 3; pkill -9 -f '[B]in64.StarCitizen.exe' 2>/dev/null
          echo "$(date +%T) auto-dismissed a fatal error (see Game.log; dump: crash-$stamp.dmp, ${last} bytes)" >> "$WINEPREFIX/sc-offline-mac.log"; break; fi
        sleep 3; done ) >/dev/null 2>&1 &
    ( cd "$BIN64" && sandbox-exec -p "$NONET" env WINEDLLOVERRIDES="dinput8=n,b" "$CX/wine" 'C:\Program Files\Roberts Space Industries\StarCitizen\LIVE\Bin64\StarCitizen.exe' "$@" ) >>"$WINEPREFIX/sc-offline-mac.log" 2>&1
    echo "game exited; removing the mod from Bin64"; WINEDLLOVERRIDES="dinput8=n,b" "$CX/wine" "$MOD" uninstall --game "$GAME" 2>/dev/null | tr -d '\r' | grep -E 'Mod:|removed|\[!\]' | tail -n 2 ;;
  profile)   "$(cd "$(dirname "$0")" && pwd)/tools/profile.sh" "$2" ;;
  squeeze)   "$(cd "$(dirname "$0")" && pwd)/winegrid/balloon" "${2:-6}" 4 15 ;;
  mute)      "$CX/wine" reg add 'HKCU\Software\Wine\Drivers' /v Audio /t REG_SZ /d '' /f >/dev/null 2>&1; echo "game audio OFF (bottle audio driver disabled; takes effect at the next launch)" ;;
  unmute)    "$CX/wine" reg delete 'HKCU\Software\Wine\Drivers' /v Audio /f >/dev/null 2>&1; echo "game audio ON (bottle audio driver restored; takes effect at the next launch)" ;;
  *) echo "unknown command: $1"; exit 1 ;;
esac
