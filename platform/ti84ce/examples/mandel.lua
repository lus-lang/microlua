-- Mandelbrot benchmark: identical computation to examples/mandel.basic.txt
-- (TI-BASIC), for a MicroLua vs TI-BASIC performance comparison.
--
-- Phase 1 (timed): compute escape iterations for a 32x24 grid over
-- re [-2.25, 0.75], im [-1.125, 1.125], max 24 iterations, storing one
-- "0"/"1" (outside/inside) row string per grid row. (The grid is 32x24 so
-- the BASIC twin fits its results in a single list: the CE OS caps
-- matrices at 400 elements and lists at 999.)
-- Phase 2 (timed): draw one pixel per inside-the-set cell from the rows.
-- Prints the inside count (binary64 reference: 202; binary32 may differ by
-- a cell or two at the boundary) and both elapsed times in ms.

local grid = {}
local inside = 0

local t0 = timer.millis()
for y = 0, 23 do
  local row = ""
  for x = 0, 31 do
    local a = -2.25 + 3 * x / 32
    local b = -1.125 + 2.25 * y / 24
    local u, v, i = 0, 0, 0
    while i < 24 and u * u + v * v <= 4 do
      local w = u * u - v * v + a
      v = 2 * u * v + b
      u = w
      i = i + 1
    end
    if i == 24 then
      inside = inside + 1
      row = row .. "1"
    else
      row = row .. "0"
    end
  end
  grid[y + 1] = row
end
local computeMs = timer.millis() - t0

gfx.begin()
gfx.fill(255)
gfx.color(0)
t0 = timer.millis()
for y = 0, 23 do
  local row = grid[y + 1]
  for x = 0, 31 do
    if string.byte(row, x + 1) == 49 then -- '1'
      gfx.pixel(x + 144, y + 108)
    end
  end
end
local drawMs = timer.millis() - t0
gfx.swap()
timer.sleep(4000)
gfx.finish()

print("inside", inside)
print("compute ms", computeMs)
print("draw ms", drawMs)
