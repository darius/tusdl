#!/bin/sh
rm -i evo178.ppm
grep -a '^# 39' $1 | head -1 | awk '{sub(/^# /, ""); print;}' >tmp1 &&
cat tmp1 evo-state >tmp2 &&
mv tmp2 evo-state &&
./evo.sh &&
echo convert evo178.ppm -resize 1200x800 -quality 90 $1.jpg
