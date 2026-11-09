#!/bin/zsh
# Launch the offline game and record how it behaves, without touching the process (no sampler: `sample` freezes it).
# usage: tools/measure-run.sh <tag> [seconds_in_world=360] [outdir]
# Every 30 s: stage, the game's memory footprint and compressed share (top), free RAM, swap, plus a crop of the FPS overlay.
# Stops the game after <seconds_in_world> in the world, or as soon as the run fails.
TAG="$1"; SECS="${2:-360}"; ROOT="$(cd "$(dirname "$0")/.." && pwd)"; OUT="${3:-$ROOT/runs/$TAG}"; mkdir -p "$OUT"
B="$HOME/Library/Application Support/CrossOver/Bottles/StarCitizen"; G="$B/drive_c/Program Files/Roberts Space Industries/StarCitizen/LIVE/Game.log"
LOG="$OUT/log.txt"; : > "$LOG"; old=$(sed -n 2p "$G" 2>/dev/null)
shot() {  # $1 = name; saves <name>-fps.png (overlay crop) and <name>-view.png (downscaled frame)
  local id; id=$("$ROOT/tools/winlist" 2>/dev/null | grep -E "owner='StarCitizen.exe'" | awk -F'bounds=' '{split($2,a," "); split(a[2],d,"x"); if (d[1]+0>400 && d[2]+0>200) print d[1]*d[2], $0}' | sort -rn | head -1 | sed -E 's/^[0-9]+ id=([0-9]+).*/\1/')
  [ -z "$id" ] && return; screencapture -x -o -l "$id" "$OUT/$1.png" 2>/dev/null || return
  local W H; W=$(sips -g pixelWidth "$OUT/$1.png" | tail -1 | awk '{print $2}'); H=$(sips -g pixelHeight "$OUT/$1.png" | tail -1 | awk '{print $2}')
  sips -c $((H*24/100)) $((W*40/100)) --cropOffset $((H*1/100)) $((W*60/100)) "$OUT/$1.png" --out "$OUT/$1-fps.png" >/dev/null 2>&1
  sips -Z 1000 "$OUT/$1.png" --out "$OUT/$1-view.png" >/dev/null 2>&1; rm -f "$OUT/$1.png"
}
( while ! pgrep -f '[B]in64.StarCitizen.exe' >/dev/null; do sleep 3; done
  # Watch, and stop, only the game this run started: a harness left over from an earlier run must never act on a later game.
  PID=$(pgrep -f '[B]in64.StarCitizen.exe' | head -1); PIDS=($(pgrep -f '[B]in64.StarCitizen.exe')); t0=$(date +%s); tin=""; n=0
  while kill -0 "$PID" 2>/dev/null; do
    cur=$(sed -n 2p "$G" 2>/dev/null); stage=boot
    if [ "$cur" != "$old" ]; then
      grep -q 'Loading level megamap' "$G" 2>/dev/null && stage=loading
      grep -q 'ShardSeeding Success' "$G" 2>/dev/null && stage=seeded
      grep -qE 'Exit Bed|SpawnClosetArea' "$G" 2>/dev/null && stage=WORLD
      if grep -qE 'Fatal Error Thrown|ContextEstablisherTaskFailed|crash handler taking over' "$G" 2>/dev/null; then
        echo "$(date +%T) FAILED: $(grep -m1 -oE 'GPU crash message: .{0,80}|Fatal Error Thrown: .{0,120}|WaitForShardSeeding timed out.{0,40}' "$G")" >> "$LOG"; sleep 20; kill $PIDS 2>/dev/null; break; fi
    fi
    # optional: MAX_TOTAL=<s> stops the run that long after the game started (for runs that never enter the world);
    # SHOT_ALWAYS=1 takes the screenshots in every stage
    [ -n "$SHOT_ALWAYS" ] && [ "$stage" != WORLD ] && { k=$((k+1)); shot "s$(printf %02d $k)"; }
    [ -n "$MAX_TOTAL" ] && [ $(( $(date +%s) - t0 )) -ge "$MAX_TOTAL" ] && { echo "$(date +%T) done: $MAX_TOTAL s since start, stopping" >> "$LOG"; kill $PIDS 2>/dev/null; break; }
    [ "$stage" = WORLD ] && [ -z "$tin" ] && { tin=$(date +%s); echo "$(date +%T) IN WORLD after $((tin-t0)) s" >> "$LOG"; }
    m=$(top -l 1 -pid "$PID" -stats mem,cmprs 2>/dev/null | tail -n 1)
    echo "$(date +%T) $stage game mem/compressed: $m | free $(vm_stat | awk '/Pages free/{gsub(/\./,"",$NF); printf "%.1fGB", $NF*16384/1073741824}') swap $(sysctl -n vm.swapusage | awk '{print $6}') compressor $(vm_stat | awk '/stored in compressor/{s=$NF} /occupied by compressor/{o=$NF} END{gsub(/\./,"",s);gsub(/\./,"",o); printf "%.1fGB held in %.1fGB", s*16384/1073741824, o*16384/1073741824}') | cpu $(ps -o %cpu= -p "$PID" | tr -d ' ')%" >> "$LOG"
    ioreg -r -d 1 -w 0 -c IOAccelerator 2>/dev/null | grep -m1 -o '"PerformanceStatistics" = {[^}]*}' | python3 -I -c 'import sys, re; t = sys.stdin.read(); g = lambda k: int((re.search("\"" + re.escape(k) + "\"=([0-9]+)", t) or [0, 0])[1]); print("         gpu: busy %d%% (tiler %d%%, renderer %d%%) | parameter buffer %d MB | split scenes %d | gpu memory in use %.1f GB of %.1f GB allocated" % (g("Device Utilization %"), g("Tiler Utilization %"), g("Renderer Utilization %"), g("Allocated PB Size") >> 20, g("SplitSceneCount"), g("In use system memory") / 2**30, g("Alloc system memory") / 2**30))' >> "$LOG" 2>/dev/null
    if [ "$stage" = WORLD ]; then n=$((n+1)); shot "w$(printf %02d $n)"
      # system paging activity since the previous sample (pages/s) and the busiest threads with priority and user/kernel time
      v=$(vm_stat | awk '/Decompressions/{d=$NF} /Compressions/{c=$NF} /Swapins/{si=$NF} /Swapouts/{so=$NF} /Pageins/{pi=$NF} END{gsub(/\./,"",d);gsub(/\./,"",c);gsub(/\./,"",si);gsub(/\./,"",so);gsub(/\./,"",pi); print d, c, si, so, pi}'); now=$(date +%s)
      if [ -n "$pv" ]; then echo "$v $pv $((now-pt))" | awk '{dt=$11; printf "         paging/s: decompress %d  compress %d  swapin %d  swapout %d  pagein %d\n", ($1-$6)/dt, ($2-$7)/dt, ($3-$8)/dt, ($4-$9)/dt, ($5-$10)/dt}' >> "$LOG"; fi; pv="$v"; pt=$now
      ps -M -p "$PID" 2>/dev/null | awk 'NR>1{ if (NF==6) print $2, $4, $5, $6; else if (NF>=9) print $4, $6, $7, $8 }' | sort -rn | head -6 | awk '{printf "         thread cpu %5s%%  pri %s  sys %s  user %s\n", $1, $2, $3, $4}' >> "$LOG"
      if [ -n "$BG_OFF_AT" ] && [ $(( $(date +%s) - tin )) -ge "$BG_OFF_AT" ] && [ -z "$bgdone" ]; then taskpolicy -B -p "$PID" >> "$LOG" 2>&1; echo "$(date +%T) taskpolicy -B applied to the game (moved out of any background state)" >> "$LOG"; bgdone=1; fi
      :; [ $(( $(date +%s) - tin )) -ge "$SECS" ] && { echo "$(date +%T) done: $SECS s in world, stopping" >> "$LOG"; kill $PIDS 2>/dev/null; break; }; fi
    sleep 30
  done ) &
cd "$ROOT" && ./sc-offline-mac.sh play; wait
echo "$(date +%T) run ended" >> "$LOG"
G2="$G"; echo "frames: $(grep -E '^Frames  ' "$G2" | awk '{print $3}') | main thread: $(grep -E 'CPU \(MainThread\)' "$G2" | awk '{print $4,$5,$6,$7}') | whole frame: $(grep -E 'CPU \(WholeFrame\)' "$G2" | awk '{print $4,$5,$6,$7}')" >> "$LOG"
