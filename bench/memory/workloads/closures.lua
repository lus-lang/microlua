local funcs = {}
local sum = 0
for i = 1, 500 do
  local base = i
  funcs[i] = function(x)
    return base + x
  end
end
for i = 1, 500 do
  sum = sum + funcs[i](i)
end
print(sum)
