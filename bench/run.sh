#!/usr/bin/env bash
# usage: bench/run.sh path/to/luc   -> best of 3 per benchmark
LUC=${1:-./luc}; cd "$(dirname "$0")"
for f in fib loop mandel sieve dict strcat strops nbody closure method fn_mandel fn_loop fn_sieve; do
  best=""
  for r in 1 2 3; do
    t=$($LUC $f.luc | awk '{print $NF}')
    best=$(python3 -c "b='$best'; t=float('$t'); print(t if b=='' else min(float(b),t))")
  done
  printf "%-8s %8.3f\n" $f $best
done
