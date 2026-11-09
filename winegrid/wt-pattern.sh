#!/bin/zsh
# ./wt-pattern.sh on|off <MB>: real memory cost of the allocator pattern under Wine
real() { vm_stat | awk '/Anonymous pages/{a=$3} /Pages stored in compressor/{s=$5} END{gsub("\\.","",a); gsub("\\.","",s); printf "%.0f", (a+s)*16384/1048576}'; }
mode=$1; mb=$2; out=/tmp/wt-pattern-$$.txt; r0=$(real)
RAW=1 ./wt.sh $mode pattern $mb > $out &
n=0; until grep -q 'PATTERN committed' $out 2>/dev/null || [ $n -ge 120 ]; do sleep 1; n=$((n+1)); done
sleep 2; pid=$(pgrep -f 'wgtest.exe' | while read p; do ps -o rss= -p $p; echo " $p"; done | paste - - | sort -n | tail -1 | awk '{print $2}'); r1=$(real)
fp=$(footprint -p $pid 2>/dev/null | grep -m1 'Footprint:' | sed 's/.*Footprint: //; s/ (.*//')
regs=$(footprint -p $pid 2>/dev/null | sed -n '5,9p' | cut -c1-120)
echo "[$mode] $(grep -m1 'PATTERN committed' $out)"
echo "[$mode] ledger footprint of the process: $fp | real memory, system-wide change: $((r1 - r0)) MB"
echo "$regs" | sed "s/^/[$mode]   /"
n=0; until grep -q 'PATTERN decommitted' $out 2>/dev/null || [ $n -ge 60 ]; do sleep 1; n=$((n+1)); done
sleep 3; r2=$(real); fp2=$(footprint -p $pid 2>/dev/null | grep -m1 'Footprint:' | sed 's/.*Footprint: //; s/ (.*//')
echo "[$mode] after decommitting half: ledger $fp2 | real change since start: $((r2 - r0)) MB"
wait; echo "[$mode] $(grep -E 'PATTERN (PASS|FAIL)' $out)"; rm -f $out
