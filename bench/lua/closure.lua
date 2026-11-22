local t = os.clock()
local function mk(k) return function(x) return x + k end end
local f = mk(3) local s = 0
for i = 0, 4999999 do s = f(s) end
print("closure", s, os.clock() - t)
