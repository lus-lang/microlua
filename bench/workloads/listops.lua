-- listops: element-wise array passes (c[k] = a[k]*b[k] + a[k]) -- stresses
-- int-keyed table reads/writes and integer arithmetic, the dominant costs of
-- per-element VM dispatch. Mirrors the TI-84 CE bench_list workload with the
-- rep count scaled up for host timing. All values stay far below int32.
local REPS = 3000
local N = 500

local a, b, c = {}, {}, {}
for k = 1, N do
  a[k] = k % 97
  b[k] = k % 89
  c[k] = 0
end

for rep = 1, REPS do
  for k = 1, N do
    c[k] = a[k] * b[k] + a[k]
  end
end

local s = 0
for k = 1, N do
  s = s + c[k]
end

print(s)
