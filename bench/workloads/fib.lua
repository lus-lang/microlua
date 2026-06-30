-- fib: recursive Fibonacci -- stresses call / return / frame setup.
-- Depth is linear (n), kept well under MicroLua's ~64-frame stack.
-- Output is a single int32-safe integer: fib(32) = 2178309.
local function fib(n)
  if n < 2 then return n end
  return fib(n - 1) + fib(n - 2)
end

print(fib(32))
