#!/usr/bin/env bash
# lanternl vs PyTorch on the same GPU, same data, same model, same training.
#   default   : lanternl          vs  pytorch (eager)
#   optimized : lanternl + graph  vs  pytorch + torch.compile(mode="reduce-overhead")
#               (both replay CUDA Graphs; "total" includes graph capture / compile time)
#   (S) rows  : the same four runs with a tiny model (dim 64, 2 layers, batch 8 x 64),
#               where per-step overhead matters more than GEMM speed
#   bash bench/vs_pytorch.sh          (from the repo root, after bash cuda/build_kaggle.sh)
set -e
cd "$(dirname "$0")/.."
if [ ! -f bench/data.txt ]; then
    echo "== downloading tiny shakespeare (1.1 MB)"
    curl -sL https://raw.githubusercontent.com/karpathy/char-rnn/master/data/tinyshakespeare/input.txt -o bench/data.txt
fi
run() { echo "== $1"; shift; "$@" | tee -a /tmp/bench_all.log; }
rm -f /tmp/bench_all.log
for S in 0 1; do
    export SMALL=$S
    run "lanternl (small=$S)"            ./luc bench/vs_pytorch.luc
    run "pytorch (small=$S)"             python bench/vs_pytorch.py
    run "lanternl + graph (small=$S)"    env GRAPH=1 ./luc bench/vs_pytorch.luc
    run "pytorch + compile (small=$S)"   python bench/vs_pytorch.py --compile
done
echo
echo "================ RESULT (epoch 2 = steady speed, no warmup) ================"
grep -a "^RESULT" /tmp/bench_all.log
