-- Integer benchmark: identical computation to bench_int.basic.txt.
-- Tight scalar loop, integers only (LCG-style mixing). Expected sum:
-- 40959312. MicroLua's ints are inline 32-bit; TI-BASIC computes the same
-- thing in 14-digit BCD floats.
local t0 = timer.millis()
local x, s = 1, 0
for k = 1, 20000 do
  x = (37 * x + 11) % 4096
  s = s + x
end
local ms = timer.millis() - t0
print("sum", s)
print("ms", ms)
