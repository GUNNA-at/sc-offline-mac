#!/bin/zsh
# look.sh <name> [width=1400]: screenshot of the whole screen, scaled, into the session scratchpad (for checking the game from scripts).
OUT="${SC_SHOTS:-$(cd "$(dirname "$0")/.." && pwd)/shots}"; mkdir -p "$OUT"
screencapture -x -C "$OUT/$1-full.png" && sips -Z ${2:-1400} "$OUT/$1-full.png" --out "$OUT/$1.png" >/dev/null && rm -f "$OUT/$1-full.png" && echo "$OUT/$1.png"
