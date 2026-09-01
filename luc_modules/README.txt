================================================================
 LUC AI MODULES  (lanternl ported to LUC)
================================================================

Day la thu vien lanternl (PyTorch-mini bang Lua) da duoc port
sang LUC, dat o dang "third-party modules" - khong sua logic.

YEU CAU: phai dung luc.exe / luc build tu src/ trong cung zip
(canvas moi: metatable + keyword import + os.difftime). Ban luc
cu se bao loi setmetatable / import ngay file dau tien.

--- CACH DUNG ---

Cach 1 (de nhat): dat thu muc luc_modules/ CANH FILE .luc
cua ban, roi trong code viet:

    import ai

    local model = ai.LMTrain {
        data = "hello world, this is luc ai!",
        preset = "auto",
        epochs = 300
    }
    model:run()
    print(model:generate("hello", 20))

Cach 2: dat luc_modules o cho khac thi bat env var:
    Windows:  set LUC_PATH=C:\duong\dan\luc_modules
    Linux:    export LUC_PATH=/duong/dan/luc_modules

Van dung duoc require("ai") nhu cu - import va require
tim module cung mot cho.

--- MAY KHONG CO GPU (chay CPU) ---

OK het nhung module nay:
  ai.LMTrain        - train LM nhanh (CPU preset)
  ai.Tokenizer      - BPE tokenizer
  ai.forge          - autograd engine
  ai.Matrix/Tensor  - matrix backend Lua
  nn.*, optim.*, rope.*, positional.*, data.*

Module GPU (GPUTransformer, luaTL) se TU DONG TAT neu khong
thay FFI/C bridge - in ra dong "GPU (luaTL): Not installed".
Khong crash, khong can thao tac gi.

--- GPU TREN COLAB ---

Phien nay chua noi duong GPU cua LUC. File .cu trong repo
lanternl giu nguyen, buoc sau se them sys.load vao LUC de
nap luaTL.so (build bang nvcc tren Colab) thay cho FFI.

--- FILE MAP (so voi repo lanternl goc) ---

  ai.lua                -> ai.luc
  ai/core/*.lua         -> *.luc  (ten file flat)
  ai/nn/*.lua           -> *.luc
  ai/data/*.lua         -> *.luc
  ai/optim/*.lua        -> *.luc
  ai/gpu/*.lua          -> *.luc  (thay doi: FFI de cho buoc GPU)
  ai/benchmarks/*.lua   -> *.luc

Tat ca dat FLAT (khong con thu muc con) vi LUC require
dung ten module don gian: import transformer, import optim...
