local co = coroutine.create(function()
  for i = 1, 5 do
    coroutine.yield(i * i)
  end
end)
local sum = 0
for _ = 1, 5 do
  local ok, v = coroutine.resume(co)
  if not ok then error(v) end
  sum = sum + v
end
print(sum)
