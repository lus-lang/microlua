-- tableconcat (anti-overfit): fill a table with strings, then table.concat
-- them with a separator. This exercises a DIFFERENT string-joining path (the
-- table library, not the .. operator), so it independently checks string-heavy
-- behaviour. Output is the joined byte length (int32-safe).
local N = 40000

local t = {}
for i = 1, N do
  t[i] = tostring(i)
end

print(#table.concat(t, ","))
