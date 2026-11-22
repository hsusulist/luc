#!/usr/bin/env bash
# lanternl vs PyTorch on the same GPU, same data, same model, same training.
#   bash bench/vs_pytorch.sh          (from the repo root, after bash cuda/build_kaggle.sh)
set -e
cd "$(dirname "$0")/.."
if [ ! -f bench/data.txt ]; then
    echo "== downloading tiny shakespeare (1.1 MB)"
    curl -sL https://raw.githubusercontent.com/karpathy/char-rnn/master/data/tinyshakespeare/input.txt -o bench/data.txt
fi
echo "== lanternl"
./luc bench/vs_pytorch.luc | tee /tmp/lanternl.log
echo "== pytorch"
python bench/vs_pytorch.py | tee /tmp/pytorch.log
echo
echo "================ RESULT ================"
grep -h "^RESULT" /tmp/lanternl.log /tmp/pytorch.log
