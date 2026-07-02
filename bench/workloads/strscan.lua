-- strscan: build a string by repeated append, then scan it one position at a
-- time with string.byte -- stresses positional string indexing on a long
-- string plus concat/GC churn. Mirrors the TI-84 CE bench_str workload scaled
-- up for host timing. Pure ASCII, so byte- and codepoint-based string.byte
-- agree and the stdout gate holds against reference Lua.
local N = 20000 -- final length in chars; built two chars per append

local r = "AB"
for k = 2, N / 2 do
  r = r .. "AB"
end

local n = 0
for k = 1, N do
  if string.byte(r, k) == 65 then -- 'A'
    n = n + 1
  end
end

print(#r)
print(n)
