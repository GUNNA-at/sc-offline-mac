#!/bin/zsh
# Double-click to play Star Citizen OFFLINE (sc-offline mod) in the CrossOver bottle, network-sandboxed.
# Everything happens in sc-offline-mac.sh next to this file; this window shows the launcher's log.
cd "$(dirname "$0")" || exit 1
echo "Starting Star Citizen offline... the first window appears after ~1 min, the loading screen after ~3 min, in game after ~8 min."
echo "(Two information boxes are dismissed automatically. Audio: ./sc-offline-mac.sh mute|unmute)"
./sc-offline-mac.sh play
echo; echo "Game closed. You can close this window."
