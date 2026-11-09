#!/bin/zsh
# front.sh [pid]: bring the game (or the process with that pid) to the front on the Mac and print which app was in front before.
# NSRunningApplication.activate is refused while another app is active (macOS cooperative activation); System Events is not.
PID=${1:-$(ps -axo pid,command | grep 'Bin64.StarCitizen.exe' | grep -v -e grep -e zsh | awk '{print $1}' | head -1)}
[ -z "$PID" ] && { echo "game is not running"; exit 1; }
PREV=$(lsappinfo info -only pid "$(lsappinfo front 2>/dev/null)" 2>/dev/null | sed -E 's/.*=//')
osascript -e "tell application \"System Events\" to set frontmost of (first application process whose unix id is $PID) to true" >/dev/null 2>&1
for i in 1 2 3 4 5 6 7 8 9 10; do
  NOW=$(lsappinfo info -only pid "$(lsappinfo front 2>/dev/null)" 2>/dev/null | sed -E 's/.*=//')
  [ "$NOW" = "$PID" ] && break; sleep 0.2
done
echo "$PREV front_now=$NOW"
