local t = {}
local sum = 0
for i = 1, 1200 do
  local v = i * 3 + 7
  t[i] = v
  sum = sum + v
end
print(sum)
