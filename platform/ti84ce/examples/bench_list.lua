-- List/array benchmark: identical computation to bench_list.basic.txt.
-- 60 element-wise passes of c = a*b + a over 500-element arrays, then a
-- sum. Expected sum: 1025620. This is TI-BASIC's best case: L1L2+L1 is a
-- single vectorized statement running in OS assembly, while Lua pays VM
-- dispatch per element. Setup (array fill) is untimed on both sides.
local a, b, c = {}, {}, {}
for k = 1, 500 do
  a[k] = k % 97
  b[k] = k % 89
  c[k] = 0
end

local t0 = timer.millis()
for rep = 1, 60 do
  for k = 1, 500 do
    c[k] = a[k] * b[k] + a[k]
  end
end
local s = 0
for k = 1, 500 do
  s = s + c[k]
end
local ms = timer.millis() - t0
print("sum", s)
print("ms", ms)
