local t = {}
local sum = 0
for i = 1, 900 do
  local s = "k" .. tostring(i)
  t[i] = s
  sum = sum + string.len(s)
end
print(sum)
