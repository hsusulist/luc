local t = os.clock() local N = 5000000 local flags = {}
for i = 0, N do flags[#flags + 1] = true end
local cnt = 0 local i = 2
while i <= N do
  if flags[i + 1] then cnt = cnt + 1 local j = i * i
    while j <= N do flags[j + 1] = false j = j + i end end
  i = i + 1 end
print("sieve", cnt, os.clock() - t)
