local root = {}
local sum = 0
for i = 1, 90 do
  local a = {}
  root[i] = a
  for j = 1, 12 do
    local b = {i, j, i * j}
    a[j] = b
    sum = sum + b[3]
  end
end
print(sum)
