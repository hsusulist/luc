#!/usr/bin/env bash
# Build LUC + the lucTL CUDA backend on a Linux GPU box (Kaggle / Colab), then run the GPU check.
#   bash cuda/build_kaggle.sh            (from the repo root)
#   LUCTL_ARCH=75 bash cuda/build_kaggle.sh   force a GPU arch (60 = P100, 75 = T4, 80 = A100, 86/89 = RTX)
set -e
cd "$(dirname "$0")/.."

echo "== 1/3  building luc (Linux)"
gcc -O2 -std=gnu99 -o luc src/luc_core.c src/luc_libs.c src/luc_trans.c -lm -ldl -lpthread
./luc --version 2>/dev/null || true

echo "== 2/3  building luc_modules/lucTL.so (nvcc)"
if [ -z "$LUCTL_ARCH" ]; then
    LUCTL_ARCH=$(nvidia-smi --query-gpu=compute_cap --format=csv,noheader 2>/dev/null | head -1 | tr -d ' .')
fi
LUCTL_ARCH=${LUCTL_ARCH:-75}
echo "   GPU arch: sm_${LUCTL_ARCH}"
nvcc -O3 -std=c++14 --shared -Xcompiler -fPIC --use_fast_math -lineinfo \
     -gencode arch=compute_${LUCTL_ARCH},code=sm_${LUCTL_ARCH} \
     -o luc_modules/lucTL.so cuda/lucTL_train.cu -lcublas

echo "== 3/3  GPU check"
./luc cuda/gpu_check.luc
