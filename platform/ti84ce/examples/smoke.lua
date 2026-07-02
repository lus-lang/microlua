-- MicroLua CE smoke test: deterministic output across host and calculator.
print("MicroLua on the TI-84+ CE")
local t = {}
for i = 1, 10 do
  t[i] = i * i
end
local sum = 0
for i = 1, #t do
  sum = sum + t[i]
end
print("sum", sum)
print(string.format("fmt %d %s", 42, string.rep("ab", 3)))
local co = coroutine.create(function(a)
  local b = coroutine.yield(a + 1)
  return b * 2
end)
local _, x = coroutine.resume(co, 1)
local _, y = coroutine.resume(co, 10)
print("coro", x, y)
print(math.floor(3.9), math.max(2, 7))
print("ok")
