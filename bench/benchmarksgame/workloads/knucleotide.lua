-- Adapted from The Computer Language Benchmarks Game k-nucleotide Lua program.
-- Uses explicit nil checks instead of metatable defaults.

local seed = 42
local bases = {"A", "C", "G", "T", "G", "C", "A", "T"}
local parts = {}

for i = 1, 2400 do
  seed = (seed * 3877 + 29573) % 139968
  parts[i] = bases[(seed % #bases) + 1]
end

local seq = table.concat(parts)

local function kfrequency(k)
  local freq = {}
  local k1 = k - 1
  for i = 1, #seq - k1 do
    local fragment = string.sub(seq, i, i + k1)
    local count = freq[fragment]
    if count == nil then
      freq[fragment] = 1
    else
      freq[fragment] = count + 1
    end
  end
  return freq
end

local function count_fragment(freq, fragment)
  local value = freq[fragment]
  if value == nil then
    return 0
  end
  return value
end

local freq1 = kfrequency(1)
local freq2 = kfrequency(2)
local freq3 = kfrequency(3)
local freq4 = kfrequency(4)
local freq6 = kfrequency(6)

print("A " .. count_fragment(freq1, "A"))
print("CG " .. count_fragment(freq2, "CG"))
print("TGG " .. count_fragment(freq3, "TGG"))
print("GGCA " .. count_fragment(freq4, "GGCA"))
print("GGTGTA " .. count_fragment(freq6, "GGTGTA"))
