local s = "the quick brown fox jumps over the lazy dog"
local p, q = string.find(s, "quick")
print(p, q)
local matches = {}
local pos = 1
local words = { "the", "quick", "brown", "fox" }
for i = 1, #words do
  local start_pos, end_pos = string.find(s, words[i], pos)
  if not start_pos then break end
  table.insert(matches, string.sub(s, start_pos, end_pos))
  pos = end_pos + 1
end
print(#matches)
