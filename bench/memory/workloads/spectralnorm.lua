local function A(i, j)
  return 1 / (((i + j) * (i + j + 1) / 2) + i + 1)
end

local n = 45
local u = {}
local v = {}
for i = 1, n do
  u[i] = 1
  v[i] = 0
end

for iter = 1, 6 do
  for i = 1, n do
    local sum = 0
    for j = 1, n do
      sum = sum + A(i - 1, j - 1) * u[j]
    end
    v[i] = sum
  end
  for i = 1, n do
    local sum = 0
    for j = 1, n do
      sum = sum + A(j - 1, i - 1) * v[j]
    end
    u[i] = sum
  end
end

local vbv = 0
local vv = 0
for i = 1, n do
  vbv = vbv + u[i] * v[i]
  vv = vv + v[i] * v[i]
end
print(math.floor((vbv / vv) * 1000000))
