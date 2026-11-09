#!/bin/zsh
# squeezer.sh: started by sc-offline-mac.sh play. Waits until the game is in the world, then runs `winegrid/balloon auto`
# for as long as the game lives (see winegrid/balloon.c for what that does and why). Log: /private/tmp/sc-squeezer.log
D="$(cd "$(dirname "$0")/.." && pwd)"; LOG=/private/tmp/sc-squeezer.log
G="$HOME/Library/Application Support/CrossOver/Bottles/StarCitizen/drive_c/Program Files/Roberts Space Industries/StarCitizen/LIVE/Game.log"
gamepid() { ps -axo pid,command | grep '[B]in64.StarCitizen.exe' | grep -v zsh | awk '{print $1}' | head -1; }
T0=$(date +%s); PID=""
until [ -n "$PID" ] || [ $(( $(date +%s) - T0 )) -gt 600 ]; do sleep 5; PID=$(gamepid); done
[ -z "$PID" ] && exit 0
echo "$(date +%T) game pid $PID, waiting for the world" >> "$LOG"
until grep -a -q -E 'Exit Bed|SpawnClosetArea' "$G" 2>/dev/null && [ "$(stat -f %m "$G")" -ge "$T0" ]; do
  kill -0 "$PID" 2>/dev/null || exit 0; sleep 5
done
sleep 20                                    # let the first frames of the world settle
pkill -f '[p]wsh.exe.*dismiss.ps1' 2>/dev/null   # the dialog clicker has done its work once the world is up; it holds ~350 MB
exec "$D/winegrid/balloon" auto "$PID" "$LOG"
