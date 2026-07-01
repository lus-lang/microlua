-- Adapted from The Computer Language Benchmarks Game reverse-complement Lua
-- program. Generates the FASTA body in-process so both runtimes run the same
-- standalone workload.

local map = {
  A = "T", C = "G", G = "C", T = "A",
  a = "T", c = "G", g = "C", t = "A",
  N = "N", n = "N"
}

local seed = 42
local bases = {"A", "C", "G", "T", "N", "G", "C", "A"}
local input = {}

for i = 1, 3600 do
  seed = (seed * 3877 + 29573) % 139968
  input[i] = bases[(seed % #bases) + 1]
end

local seq = table.concat(input)
local out = {}
local outn = 0
local col = 0

for i = #seq, 1, -1 do
  local c = map[string.sub(seq, i, i)]
  outn = outn + 1
  out[outn] = c
  col = col + 1
  if col == 60 then
    outn = outn + 1
    out[outn] = "\n"
    col = 0
  end
end

if col ~= 0 then
  outn = outn + 1
  out[outn] = "\n"
end

local text = table.concat(out)
print(string.sub(text, 1, 60))
print(string.sub(text, #text - 60, #text - 1))
print(#text)
