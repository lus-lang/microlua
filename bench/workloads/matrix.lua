-- matrix: build an N x N nested table then fold it -- stresses nested-table
-- allocation and indexed access. All arithmetic is modular to stay int32-safe.
local N = 700

local a = {}
for i = 1, N do
  a[i] = {}
  for j = 1, N do
    a[i][j] = (i * j) % 97
  end
end

local sum = 0
for i = 1, N do
  local row = a[i]
  for j = 1, N do
    sum = (sum + row[j]) % 1000003
  end
end

print(sum)
