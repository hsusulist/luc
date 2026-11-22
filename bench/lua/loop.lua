local t = os.clock() local s = 0
for i = 0, 50000000 - 1 do s = s + i * 0.5 end
print("loop", s, os.clock() - t)
