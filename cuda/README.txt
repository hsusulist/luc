lucTL - CUDA backend for LUC's ai module (lanternl)
====================================================

Files
  cuda/lucTL_core.cu     runtime, memory pool, tensors, GEMM + elementwise kernels, pipeline
  cuda/lucTL_train.cu    training kernels (backward passes, embedding, cross-entropy, AdamW,
                         Program / CUDA Graph replay). #includes lucTL_core.cu: build this one.
  cuda/build_kaggle.sh   builds ./luc and luc_modules/lucTL.so, then runs cuda/gpu_check.luc
  cuda/Makefile          same build for several GPU archs (make -f cuda/Makefile)
  cuda/gpu_check.luc     checks the backend, compares a GPU matmul with the CPU, runs a short
                         GPU training job

How LUC reaches the GPU
  luc_modules/lucTL.luc         loads lucTL.so with lucffi and wraps the C API
  luc_modules/lucTL_train.luc   training ops + Program
  luc_modules/luctl_adapter.luc bridge used by matrix.luc / gpu_transformer.luc
  lucffi (built into luc, src/luc_ffi.c): cdef / load / new / cast / typeof / metatype /
  string / copy / fill, LuaJIT-style. lucffi.load also searches the script folder,
  ./luc_modules and LUC_PATH, so lucTL.so can sit next to the .luc modules.

Kaggle
  1. Notebook settings: Accelerator = GPU (T4 or P100), Internet = on.
  2. Cell:
        !git clone https://github.com/hsusulist/luc.git
        %cd luc
        !bash cuda/build_kaggle.sh
  3. Your own script, from the repo root:
        !./luc my_train.luc
     with backend = "gpu" in ai.LMTrain.

Notes
  - Linux x64 only for the GPU path (lucffi calls follow the x86-64 SysV ABI there).
    Windows builds of luc include lucffi too, but lucTL.dll is not built or tested.
  - LUC has no garbage-collector finalizers: GPU tensors are freed with t:free()
    (gpu_transformer frees its own buffers in Model:free()).
  - GPU arch: build_kaggle.sh reads it from nvidia-smi; force it with LUCTL_ARCH=75 etc.
