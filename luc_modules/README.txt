================================================================
 LUC AI MODULES  (lanternl ported to LUC)
================================================================

Lanternl (mini-PyTorch in Lua) ported to LUC as third-party modules - logic unchanged.

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

--- NO GPU (CPU only) ---

These all run fine:
  ai.LMTrain        - fast LM training (CPU preset)
  ai.Tokenizer      - BPE tokenizer
  ai.forge          - autograd engine
  ai.Matrix/Tensor  - Lua matrix backend
  nn.*, optim.*, rope.*, positional.*, data.*

GPU modules (GPUTransformer, luaTL) auto-disable without an FFI/C bridge - they print "GPU (luaTL): Not installed". No crash, nothing to do.

--- GPU ON COLAB ---

This version has no LUC GPU path yet. The .cu files stay as-is; a later step adds sys.load to LUC to load luaTL.so (built with nvcc on Colab) instead of FFI.

--- FILE MAP (vs original lanternl repo) ---

  ai.lua                -> ai.luc
  ai/core/*.lua         -> *.luc  (flat filenames)
  ai/nn/*.lua           -> *.luc
  ai/data/*.lua         -> *.luc
  ai/optim/*.lua        -> *.luc
  ai/gpu/*.lua          -> *.luc  (changed: FFI stubbed for the GPU step)
  ai/benchmarks/*.lua   -> *.luc

All flat (no subfolders) since LUC require uses simple module names: import transformer, import optim...
