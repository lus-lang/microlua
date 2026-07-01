local dna = "GGTATTTTAATTTATAGT"
local s = ""
for i = 1, 140 do
  s = s .. dna
end

local counts = {}
for i = 1, string.len(s) - 2 do
  local k = string.sub(s, i, i + 2)
  counts[k] = (counts[k] or 0) + 1
end

local total = 0
for _, v in pairs(counts) do
  total = total + v
end
print(total)
