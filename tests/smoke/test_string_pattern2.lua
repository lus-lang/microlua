-- Test string.find and string.match - simpler
local s = "the quick brown fox"
local p, q = string.find(s, "quick")
print(p, q)
print(q + 1)
