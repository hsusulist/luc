#!/usr/bin/env bash
# Re-run the old test: original LuaJIT lanternl vs PyTorch vs LUC lanternl,
# all on this GPU with exactly the same data, model and training settings.
#   bash bench/vs_luajit.sh        (from the luc repo root, after bash cuda/build_kaggle.sh)
# The LuaJIT lanternl is cloned untouched into /tmp/lanternl and built with its own
# kernels (luaTL, no cuBLAS). Only one CUDA 13 fix is applied to that copy:
# cudaDeviceProp lost clockRate / memoryClockRate, so they are read with
# cudaDeviceGetAttribute instead (same values). Nothing in the training code changes.
set -e
cd "$(dirname "$0")/.."
ROOT=$(pwd)
if [ ! -f bench/data.txt ]; then
    curl -sL https://raw.githubusercontent.com/karpathy/char-rnn/master/data/tinyshakespeare/input.txt -o bench/data.txt
fi

echo "== setting up LuaJIT lanternl"
command -v luajit >/dev/null || (apt-get install -y luajit -qq >/dev/null 2>&1 || sudo apt-get install -y luajit -qq)
rm -rf /tmp/lanternl
git clone -q --depth 1 https://github.com/hsusulist/lanternl.git /tmp/lanternl
(cd /tmp/lanternl && git log -1 --format='   lanternl commit %h  (%ad)')
python - <<'PY'
p = "/tmp/lanternl/cuda/luaTL_core.cu"
s = open(p).read()
helper = ("static int ltl_attr(int d, cudaDeviceAttr a) "
          "{ int v = 0; cudaDeviceGetAttribute(&v, a, d); return v; }\n")
s = s.replace("static void luaTL_fill_devinfo", helper + "static void luaTL_fill_devinfo", 1)
s = s.replace("p->memoryClockRate", "ltl_attr(device, cudaDevAttrMemoryClockRate)")
s = s.replace("p->clockRate", "ltl_attr(device, cudaDevAttrClockRate)")
open(p, "w").write(s)
PY
ARCH=$(nvidia-smi --query-gpu=compute_cap --format=csv,noheader | head -1 | tr -d ' .')
nvcc -O3 -std=c++14 --shared -Xcompiler -fPIC --use_fast_math \
     -gencode arch=compute_${ARCH},code=sm_${ARCH} \
     -o /tmp/lanternl/luaTL.so /tmp/lanternl/cuda/luaTL_train.cu
cp bench/vs_luajit.lua /tmp/lanternl/

run() { echo "== $1"; shift; "$@" | tee -a /tmp/bench_luajit.log; }
rm -f /tmp/bench_luajit.log
for S in 0 1; do
    export SMALL=$S
    run "luajit lanternl (small=$S)"         bash -c "cd /tmp/lanternl && DATA=$ROOT/bench/data.txt luajit vs_luajit.lua"
    run "luajit lanternl + graph (small=$S)" bash -c "cd /tmp/lanternl && GRAPH=1 DATA=$ROOT/bench/data.txt luajit vs_luajit.lua"
    run "luc lanternl (small=$S)"            ./luc bench/vs_pytorch.luc
    run "luc lanternl + graph (small=$S)"    env GRAPH=1 ./luc bench/vs_pytorch.luc
    run "pytorch (small=$S)"                 python bench/vs_pytorch.py
done
echo
echo "================ RESULT (epoch 2 = steady speed, no warmup) ================"
grep -a "^RESULT" /tmp/bench_luajit.log
