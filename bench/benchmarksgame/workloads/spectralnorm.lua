-- Adapted from The Computer Language Benchmarks Game spectral-norm Lua program.

local function A(i, j)
  local ij = i + j - 1
  return 1.0 / (ij * (ij - 1) / 2 + i)
end

local function Av(x, y, n)
  for i = 1, n do
    local a = 0.0
    for j = 1, n do
      a = a + A(i, j) * x[j]
    end
    y[i] = a
  end
end

local function Atv(x, y, n)
  for i = 1, n do
    local a = 0.0
    for j = 1, n do
      a = a + A(j, i) * x[j]
    end
    y[i] = a
  end
end

local function AtAv(x, y, t, n)
  Av(x, t, n)
  Atv(t, y, n)
end

local n = 80
local u, v, t = {}, {}, {}

for i = 1, n do
  u[i] = 1.0
  v[i] = 0.0
  t[i] = 0.0
end

for _ = 1, 10 do
  AtAv(u, v, t, n)
  AtAv(v, u, t, n)
end

local vBv, vv = 0.0, 0.0
for i = 1, n do
  vBv = vBv + u[i] * v[i]
  vv = vv + v[i] * v[i]
end

print(string.format("%.7f", math.sqrt(vBv / vv)))
