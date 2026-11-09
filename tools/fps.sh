#!/bin/zsh
# fps.sh [seconds=15]: wait, then print what the MoltenVK shim counted over that time (needs MVKSHIM_FRAME_STATS=1 in the
# bottle): average frames per second, render passes and draws per frame, tessellated draws, plus the graphics chip's load.
N=${1:-15}; L=/private/tmp/mvkshim.log
sleep "$N"
K=$(( N / 5 )); [ $K -lt 1 ] && K=1
grep -a "frames:" "$L" | tail -n $K | awk '{ fps += $3; rp += $8; dr += $11; n++ } { for (i = 1; i <= NF; i++) if ($i == "tessellated") { td += $(i-3); tp += $(i+2) } }
  END { if (n) printf "%.1f fps | %d passes, %d draws, %.0f tessellated draws, %.0f patches per frame", fps/n, rp/n, dr/n, td/n, tp/n }'
ioreg -r -d 1 -w 0 -c IOAccelerator 2>/dev/null | grep -o '"[A-Za-z ]*Utilization %"=[0-9]*' | tr -d '"' | awk -F'[ =]' '
  /^Device/ { d = $NF } /^Renderer/ { r = $NF } /^Tiler/ { t = $NF } END { printf " | gpu busy %s%% (renderer %s%%, tiler %s%%)\n", d, r, t }'
