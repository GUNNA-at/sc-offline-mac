#!/bin/zsh
# profile.sh <name>: switch the game's graphics profile (takes effect at the next launch). Profiles live in ../profiles:
#   <name>.cfg    lines for LIVE/user.cfg (placed between the profile markers)      <name>.attrs   SysSpec_* values for the menu
# With no name it lists the profiles and shows the active one.
D="$(cd "$(dirname "$0")/.." && pwd)"; B="$HOME/Library/Application Support/CrossOver/Bottles/StarCitizen"
LIVE="$B/drive_c/Program Files/Roberts Space Industries/StarCitizen/LIVE"; U="$LIVE/user.cfg"; A="$LIVE/user/client/0/Profiles/default/attributes.xml"
if [ -z "$1" ]; then echo "profiles: $(ls "$D/profiles" | sed -n 's/\.cfg$//p' | tr '\n' ' ')"; grep -m1 -- '-- >>> profile:' "$U" | sed 's/-- >>> /active /'; exit 0; fi
[ -f "$D/profiles/$1.cfg" ] || { echo "no such profile: $1"; exit 1; }
python3 -I - "$U" "$D/profiles/$1.cfg" "$1" <<'PY'
import sys
u, cfg, name = sys.argv[1:4]; s = open(u).read(); body = open(cfg).read().rstrip('\n')
a = s.find('-- >>> profile:')
if a < 0:                                   # first use: everything after the frame-pacing lines becomes the profile part
    k = s.find('sys_maxFps'); k = s.find('\n', k) + 1 if k >= 0 else len(s); head = s[:k]
else: head = s[:a]
open(u, 'w').write(head.rstrip('\n') + '\n-- >>> profile: %s (written by tools/profile.sh; edit profiles/%s.cfg, then run it again)\n%s\n-- <<< profile\n' % (name, name, body))
PY
[ -f "$D/profiles/$1.attrs" ] && python3 -I "$D/tools/set-quality.py" "$A" $(cat "$D/profiles/$1.attrs") >/dev/null
echo "profile '$1' is set for the next launch"
