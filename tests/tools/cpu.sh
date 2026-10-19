#!/bin/sh
# cpu.sh PID SEGUNDOS — % de CPU usada pelo processo no intervalo
t() { awk '{print $14+$15}' /proc/$1/stat; }
a=$(t $1); sleep $2; b=$(t $1)
echo "$a $b $2 $(getconf CLK_TCK)" | awk '{printf "%.1f%%\n", ($2-$1)*100/($3*$4)}'
