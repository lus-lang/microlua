local rows = {}
local sum = 0
for i = 1, 350 do
  local t = {i, i + 1, i + 2}
  t.name = "row" .. tostring(i)
  rows[i] = t
  sum = sum + t[1] + t[2] + t[3] + string.len(t.name)
end
print(sum)
