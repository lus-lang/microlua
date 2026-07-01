local sum = 0
local t = {}
for i = 1, 220 do
  local v = i + 1 * 2 - 3
  t[i] = v
  sum = sum + v
end
for i = 1, 220 do
  if t[i] % 3 == 0 then
    sum = sum + t[i]
  else
    sum = sum - 1
  end
end
print(sum)
