#!/usr/bin/env bash
# LUC vs Lua 5.4 vs LuaJIT on the same machine (Kaggle, Colab, any Linux x86-64).
#   bash bench/speed.sh            (from the luc repo root)
# Builds ./luc if missing, installs lua5.4 + luajit with apt when they are missing.
# Each number is the best of 3 runs, in seconds (lower is better).
set -e
cd "$(dirname "$0")/.."
[ -x ./luc ] || gcc -O2 -std=gnu99 -w -o luc src/luc_core.c src/luc_libs.c src/luc_trans.c -lm -ldl -lpthread
if ! command -v luajit >/dev/null || ! command -v lua5.4 >/dev/null; then
    (apt-get install -y -qq luajit lua5.4 >/dev/null 2>&1 || sudo apt-get install -y -qq luajit lua5.4 >/dev/null 2>&1) || true
fi
best() {   # best <cmd...>: run 3 times, print the smallest last field
    local b=""
    for r in 1 2 3; do
        t=$("$@" 2>/dev/null | awk '{print $NF}')
        [ -z "$t" ] && { echo "-"; return; }
        b=$(python3 -c "b='$b'; t=float('$t'); print(t if b=='' else min(float(b),t))")
    done
    printf "%.3f" "$b"
}
printf "%-10s %10s %10s %10s\n" bench luc lua5.4 luajit
for f in fib loop mandel sieve dict strcat strops nbody closure method; do
    l=$(best ./luc bench/$f.luc)
    a=$(command -v lua5.4 >/dev/null && best lua5.4 bench/lua/$f.lua || echo "-")
    j=$(command -v luajit >/dev/null && best luajit bench/lua/$f.lua || echo "-")
    printf "%-10s %10s %10s %10s\n" $f $l $a $j
done
echo "-- hot loop inside a function (LUC JIT on Windows x64 and Linux x86-64):"
for f in fn_mandel fn_loop fn_sieve; do printf "%-10s %10s\n" $f $(best ./luc bench/$f.luc); done
