-- String benchmark: identical computation to bench_str.basic.txt.
-- Build a 1000-char string by appending "AB" 500 times, then scan it
-- counting 'A's per character. Expected: len 1000, count 500. MicroLua
-- interns every intermediate string (hash per concat); TI-BASIC resizes
-- Str1 per concat and calls sub( per scanned character.
local t0 = timer.millis()
local r = "AB"
for k = 2, 500 do
  r = r .. "AB"
end
local n = 0
for k = 1, 1000 do
  if string.byte(r, k) == 65 then -- 'A'
    n = n + 1
  end
end
local ms = timer.millis() - t0
print("len", #r, "count", n)
print("ms", ms)
