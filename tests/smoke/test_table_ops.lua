-- Test table insert/remove/sort
math.randomseed(42)
local t = {}
for i = 1, 20 do
  table.insert(t, math.random(1, 100))
end
table.sort(t)
local sum = 0
for i = 1, #t do
  sum = sum + t[i]
end
print(sum)
