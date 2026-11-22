local t = os.clock() local N = 400 local count = 0
for y = 0, N - 1 do local ci = 2.0 * y / N - 1.0
  for x = 0, N - 1 do local cr = 2.0 * x / N - 1.5
    local zr, zi = 0.0, 0.0 local k = 0
    while k < 100 and zr * zr + zi * zi < 4.0 do
      local nz = zr * zr - zi * zi + cr zi = 2.0 * zr * zi + ci zr = nz k = k + 1 end
    if k == 100 then count = count + 1 end end end
print("mandel", count, os.clock() - t)
