================================================================
 LUC AI MODULES  (lanternl ported to LUC)
================================================================

Lanternl (mini-PyTorch, first written in Lua) ported to LUC as third-party modules - logic unchanged.

REQUIRES: luc.exe / luc build from src/ in the same zip (needs metatable + import + os.difftime). Older luc errors on setmetatable / import.

--- USAGE ---

Option 1 (easiest): put luc_modules/ next to your .luc file, then:

    import ai

    local model = ai.LMTrain {
        data = "hello world, this is luc ai!",
        preset = "auto",
        epochs = 300
    }
    model:run()
    print(model:generate("hello", 20))

Option 2: luc_modules elsewhere needs the env var:
    Windows:  set LUC_PATH=C:\path\to\luc_modules
    Linux:    export LUC_PATH=/path/to/luc_modules

require("ai") still works - import and require search the same place.

--- NATIVE CPU KERNELS ---

luc builds that include `aikern` (require("aikern"), built into luc.exe) run the
matrix products (matrix.multiply / matmul_nt / matmul_tn) in C. Results are
bit-for-bit the same as the LUC loops, about 7x faster training overall.
`print(ai.Matrix.native)` shows whether they are active; older luc builds simply
fall back to the LUC loops.

--- LUC LISTS ---

Tensor.from, matrix.from, Embedding, cross_entropy, loss targets, Transformer
ids and LMTrain:generate accept LUC lists too: F.Tensor.from([[0, 1], [1, 0]]).

--- NO GPU (CPU only) ---

These all run fine:
  ai.LMTrain        - fast LM training (CPU preset)
  ai.Tokenizer      - BPE tokenizer
  ai.forge          - autograd engine
  ai.Matrix/Tensor  - LUC matrix backend
  nn.*, optim.*, rope.*, positional.*, data.*

Without luc_modules/lucTL.so the GPU modules (GPUTransformer, LucTL) switch themselves off
and the ai module stays on the CPU. No crash, nothing to do.

--- GPU (lucTL, CUDA) ---

lucTL is the CUDA backend (cuda/lucTL_core.cu + cuda/lucTL_train.cu). LUC talks to it
through lucffi, a small FFI built into luc (require("lucffi")). Linux x64 + NVIDIA GPU.

Kaggle / Colab (GPU on, from the repo root):
    bash cuda/build_kaggle.sh          builds ./luc and luc_modules/lucTL.so, runs the check

Then train on the GPU:
    import ai
    create model = ai.LMTrain({ "data": text, "backend": "gpu", "graph": true })
    model:run()

ai.LucTL.available tells you if the backend loaded; ai.Matrix.backends() lists
luctl_adapter / gpu (cuBLAS) / blas (OpenBLAS). See cuda/README.txt for details.

Renamed from the Lua version: luaTL -> lucTL (luaTL.luc -> lucTL.luc,
luaTL_train.luc -> lucTL_train.luc, luatl_adapter.luc -> luctl_adapter.luc,
luaTL.so -> lucTL.so, C functions luaTL_* -> lucTL_*, ai.LuaTL -> ai.LucTL).

--- FILE MAP (vs original lanternl repo) ---

  ai.lua                -> ai.luc
  ai/core/*.lua         -> *.luc  (flat filenames)
  ai/nn/*.lua           -> *.luc
  ai/data/*.lua         -> *.luc
  ai/optim/*.lua        -> *.luc
  ai/gpu/*.lua          -> lucTL*.luc / luctl_adapter.luc (ffi -> lucffi)
  ai/benchmarks/*.lua   -> *.luc

All flat (no subfolders) since LUC require uses simple module names: import transformer, import optim...
