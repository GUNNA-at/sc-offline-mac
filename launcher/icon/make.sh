#!/bin/zsh
# make.sh: icon.svg -> ../AppIcon.icns (needs Google Chrome for the one rendering step; the result is kept in the folder)
set -e; D="$(cd "$(dirname "$0")" && pwd)"; T=$(mktemp -d); P=$(mktemp -d)
"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --headless=new --disable-gpu --hide-scrollbars --user-data-dir="$P" \
  --window-size=1024,1024 --default-background-color=00000000 --screenshot="$T/icon-1024.png" "file://$D/icon.svg" >/dev/null 2>&1
mkdir "$T/AppIcon.iconset"
for s in 16 32 128 256 512; do
  sips -z $s $s "$T/icon-1024.png" --out "$T/AppIcon.iconset/icon_${s}x${s}.png" >/dev/null
  sips -z $((s*2)) $((s*2)) "$T/icon-1024.png" --out "$T/AppIcon.iconset/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns "$T/AppIcon.iconset" -o "$D/../AppIcon.icns"
cp "$T/icon-1024.png" "$D/icon-1024.png"; rm -rf "$T" "$P"; echo "made $D/../AppIcon.icns"
