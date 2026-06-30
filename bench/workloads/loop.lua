-- loop: a tight arithmetic loop -- stresses raw VM dispatch with no allocation.
-- Sum of (i % 7) over a long range; the total stays well within int32.
local N = 10000000

local sum = 0
for i = 1, N do
  sum = sum + (i % 7)
end

print(sum)
