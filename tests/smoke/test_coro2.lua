-- Test coroutines - debug
local function producer(n)
  for i = 1, n do
    coroutine.yield(i * i)
  end
end
local co = coroutine.create(producer)
local ok, v = coroutine.resume(co, 5)
print(ok, v)
print(type(v))
