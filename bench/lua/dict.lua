local t = os.clock() local d = {}
for i = 0, 199999 do d["k" .. i] = i end
local s = 0
for r = 0, 9 do for i = 0, 199999 do s = s + d["k" .. i] end end
print("dict", s, os.clock() - t)
