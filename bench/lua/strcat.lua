local t = os.clock() local parts = {}
for i = 0, 999999 do parts[#parts + 1] = tostring(i) end
local s = table.concat(parts, ",")
print("strcat", #s, os.clock() - t)
