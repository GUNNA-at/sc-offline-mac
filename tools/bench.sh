#!/bin/zsh
# bench.sh <place>: put the character at a fixed spot facing a fixed way, for comparing settings in one and the same view.
#   terrace   outside the habs, the city in view (the heavy view)      hall   the spaceport's ship terminals (a calm view)
# Needs the game in the world and the character on foot. God mode is switched on first (the terrace is reached by a drop).
D="$(cd "$(dirname "$0")" && pwd)"; cd "$D" || exit 1
case "$1" in
  terrace) ZONE=OOC_Stanton_2_Crusader; POS="5295497.86 -863155.41 5282298.5"; YAW=1 ;;
  hall)    ZONE=Spaceport_interior;     POS="-3.44 55.61 0.05";                YAW=0 ;;
  *) echo "usage: bench.sh terrace|hall"; exit 2 ;;
esac
./front.sh >/dev/null 2>&1
./modcmd.sh "creative.god on" >/dev/null
./modcmd.sh "!tp $ZONE $POS" | cut -c1-90
sleep 6
./modcmd.sh "!tp $ZONE $POS" >/dev/null          # again: the first arrival can be ahead of the place loading
sleep 4
for try in 1 2 3; do                              # turn to the reference heading (about 8.9 mouse units per degree)
  yaw=$(./modcmd.sh "!pose" | sed -n 's/.* yaw \(-*[0-9]*\).*/\1/p'); [ -z "$yaw" ] && break
  d=$(( YAW - yaw )); [ $d -gt 180 ] && d=$(( d - 360 )); [ $d -lt -180 ] && d=$(( d + 360 ))
  [ ${d#-} -le 2 ] && break
  ./keys.sh mouse:$(( -d * 889 / 100 )),0 600 >/dev/null
done
./modcmd.sh "!pose" | cut -c1-120
