local t = os.clock() local n = 0
for i = 0, 299999 do local s = "item" .. i
  if string.sub(s, 1, 4) == "item" then n = n + #s end
  local u = string.upper(s) n = n + string.byte(u, 1) end
print("strops", n, os.clock() - t)
