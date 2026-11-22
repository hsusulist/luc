local Point = {} Point.__index = Point
local function newp(x, y) return setmetatable({ x = x, y = y }, Point) end
function Point:add(o) return newp(self.x + o.x, self.y + o.y) end
function Point:len2() return self.x * self.x + self.y * self.y end
local t = os.clock() local p = newp(0, 0) local d = newp(1, 2) local s = 0
for i = 0, 1999999 do p = p:add(d) s = s + p:len2() * 1e-12 end
print("method", s, os.clock() - t)
