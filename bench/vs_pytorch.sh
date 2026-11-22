#!/usr/bin/env bash
# lanternl vs PyTorch on the same GPU, same data, same model, same training.
# Two fair pairs:
#   default   : lanternl            vs  pytorch (eager)
#   optimized : lanternl + graph    vs  pytorch + torch.compile(mode="reduce-overhead")
#               (both replay CUDA Graphs; "total" includes graph capture / compile time)
#   bash bench/vs_pytorch.sh          (from the repo root, after bash cuda/build_kaggle.sh)
set -e
cd "$(dirname "$0")/.."
if [ ! -f bench/data.txt ]; then
    echo "== downloading tiny shakespeare (1.1 MB)"
    curl -sL https://raw.githubusercontent.com/karpathy/char-rnn/master/data/tinyshakespeare/input.txt -o bench/data.txt
fi
echo "== lanternl";               ./luc bench/vs_pytorch.luc            | tee /tmp/b1.log
echo "== pytorch";                python bench/vs_pytorch.py            | tee /tmp/b2.log
echo "== lanternl + graph";       GRAPH=1 ./luc bench/vs_pytorch.luc    | tee /tmp/b3.log
echo "== pytorch + compile";      python bench/vs_pytorch.py --compile  | tee /tmp/b4.log
echo
echo "================ RESULT (epoch 2 = steady speed, no warmup) ================"
grep -h "^RESULT" /tmp/b1.log /tmp/b2.log
echo "---- optimized ----"
grep -h "^RESULT" /tmp/b3.log /tmp/b4.log
