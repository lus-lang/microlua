-- strjoin (anti-overfit): multi-operand append with a separator, so each step
-- is a 3-operand concat (s .. "," .. tostring(i)) rather than strbuild's binary
-- one. Confirms the incremental-hash concat win generalizes past the exact
-- pattern it was tuned on. Output is the final byte length (int32-safe).
local N = 12000

local s = ""
for i = 1, N do
  s = s .. "," .. tostring(i)
end

print(#s)
