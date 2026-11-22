local t = os.clock() local bodies = {}
for i = 0, 4 do bodies[#bodies + 1] = { x = i * 1.0, y = i * 0.5, z = i * 0.25, vx = 0.0, vy = 0.0, vz = 0.0, m = 1.0 + i } end
local n = #bodies
for step = 0, 199999 do
  for i = 1, n do local bi = bodies[i]
    for j = i + 1, n do local bj = bodies[j]
      local dx, dy, dz = bi.x - bj.x, bi.y - bj.y, bi.z - bj.z
      local d2 = dx * dx + dy * dy + dz * dz + 0.01
      local mag = 0.001 / (d2 * math.sqrt(d2))
      bi.vx = bi.vx - dx * bj.m * mag bi.vy = bi.vy - dy * bj.m * mag bi.vz = bi.vz - dz * bj.m * mag
      bj.vx = bj.vx + dx * bi.m * mag bj.vy = bj.vy + dy * bi.m * mag bj.vz = bj.vz + dz * bi.m * mag end end
  for i = 1, n do local b = bodies[i] b.x = b.x + 0.001 * b.vx b.y = b.y + 0.001 * b.vy b.z = b.z + 0.001 * b.vz end end
print("nbody", bodies[1].x, os.clock() - t)
