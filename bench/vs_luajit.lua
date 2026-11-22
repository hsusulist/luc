-- The original LuaJIT lanternl (github.com/hsusulist/lanternl) on the same test as
-- bench/vs_pytorch.luc / vs_pytorch.py: same data, model and training settings.
-- Run through bench/vs_luajit.sh (from the luc repo root).
local ai    = require("ai")
local now   = require("lmlog").now
local GRAPH = os.getenv("GRAPH") == "1"
local SMALL = os.getenv("SMALL") == "1"
local NAME  = (GRAPH and "luajit+graph" or "luajit") .. (SMALL and " (S)" or "")

local f = assert(io.open(os.getenv("DATA"), "rb"))
local text = f:read("*a"); f:close()

local EPOCHS, VAL = 2, 0.1
local m = ai.LMTrain{
    data = text, backend = "gpu", graph = GRAPH,
    dim = SMALL and 64 or 128, layers = SMALL and 2 or 4, heads = 4,
    ffn = SMALL and 128 or 256, max_seq = SMALL and 64 or 128,
    batch_size = SMALL and 8 or 32, epochs = EPOCHS, lr = 3e-3,
    val_split = VAL, seed = 1, hints = false,
}

local t0 = now()
m:run()
local secs = now() - t0

local h = m.history
local ep2 = h[#h].time - h[#h - 1].time
local toks = #text * (1 - VAL)
print(string.format("RESULT %-15s | total %5.1f s | epoch 2 %5.1f s = %6.0f tok/s | train %.4f | val %.4f",
    NAME, secs, ep2, toks / ep2, m.loss, m.val_loss))
print("SAMPLE " .. NAME .. " |")
print(m:generate("ROMEO:", 200))
