local parts = {}
for i = 1, 1800 do
  parts[i] = "a"
end

local text = table.concat(parts)

local out = {}
for i = 1, #text do
  out[i] = string.sub(text, i, i)
end

print(#table.concat(out))
