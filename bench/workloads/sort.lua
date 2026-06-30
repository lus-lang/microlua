-- sort: fill a table from a deterministic generator, table.sort it, then fold
-- with an ORDER-SENSITIVE rolling hash so the result depends on the sorted
-- sequence (a correct sort on both runtimes => same number). The rolling hash
-- keeps every intermediate < 2^31 for any N (unlike a position-weighted sum,
-- which would overflow MicroLua's 32-bit ints), so N can scale freely.
local N = 100000

local t = {}
local seed = 1
for i = 1, N do
  seed = (seed * 1103 + 12345) % 100003     -- max 100002*1103+12345 < 2^31
  t[i] = seed
end

table.sort(t)

local h = 0
for i = 1, N do
  h = (h * 31 + t[i]) % 1000003             -- max 1000002*31 + 100002 < 2^31
end

print(h)
