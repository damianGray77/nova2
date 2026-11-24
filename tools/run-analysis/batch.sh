#!/bin/bash
# batch.sh <picks> <out> [run_dir]
# Trace each picked genome alone (400 steps) with the run's own exe and origin.cfg, classify the first thread
# (classify.awk) and tally the calls made inside the genome (taint4.awk). <picks> is anc.ps1 -Action pick output:
# "row hash first_tick len gen hex". run_dir defaults to runs/open-2 beside this tools folder.
# Each output line: classify fields | first_byte==len tick gen | calls (see classify.awk and taint4.awk); sum.awk summarises.
here=$(cd "$(dirname "$0")" && pwd)
run=${3:-$here/../../runs/open-2}
X=$run/nova2.exe; C=$run/origin.cfg
tmp=$(mktemp)
: > "$2"
while read -r row hash tick len gen hex; do
  [ ${#hex} -ne $((len*2)) ] && continue
  fb=$((16#${hex:0:2})); [ $fb -gt 127 ] && fb=$((fb-256))
  "$X" trace "$hex" "$C" --steps 400 > "$tmp" 2>&1
  a=$(awk -v tag="$row" -v len=$len -v fb=$fb -f "$here/classify.awk" "$tmp")
  b=$(awk -v tag=x -v len=$len -f "$here/taint4.awk" "$tmp" | cut -d'|' -f2)
  echo "$a $tick $gen | $b" >> "$2"
done < "$1"
rm -f "$tmp"
echo done $(wc -l < "$2")
