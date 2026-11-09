#!/bin/zsh
# usage: shot.sh <name>  -> captures the biggest RSI Launcher / Star Citizen window to <name>.png (+ -small.png)
OUT="$(dirname "$0")"; N="$1"
ID=$("$OUT/winlist" 2>/dev/null | grep -E "owner='(StarCitizen.exe|RSI Launcher.exe)'" | awk -F'bounds=' '{split($2,a," "); split(a[2],d,"x"); if (d[1]+0>400 && d[2]+0>200) print d[1]*d[2], $0}' | sort -rn | head -1 | sed -E 's/^[0-9]+ id=([0-9]+).*/\1/')
[ -z "$ID" ] && { echo "no game/launcher window found"; exit 1; }
screencapture -x -o -l "$ID" "$OUT/$N.png" && sips -Z 1000 "$OUT/$N.png" --out "$OUT/$N-small.png" >/dev/null && echo "captured window $ID -> $N.png ($(sips -g pixelWidth -g pixelHeight "$OUT/$N.png" | tail -2 | awk '{print $2}' | paste -sd x -))"
