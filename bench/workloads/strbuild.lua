-- strbuild: repeated string concatenation -- stresses string allocation,
-- interning, and GC churn (each concat makes a new string; the old one dies).
-- Output is the final byte length (int32-safe).
local N = 8000

local s = ""
for i = 1, N do
  s = s .. tostring(i)
end

print(#s)
