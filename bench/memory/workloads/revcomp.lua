local src = "ACGTMRWSYKVHDBN"
local comp = {
  A = "T", C = "G", G = "C", T = "A",
  M = "K", R = "Y", W = "W", S = "S",
  Y = "R", K = "M", V = "B", H = "D",
  D = "H", B = "V", N = "N"
}
local s = ""
for i = 1, 180 do
  s = s .. src
end
local out = {}
local n = string.len(s)
for i = n, 1, -1 do
  out[n - i + 1] = comp[string.sub(s, i, i)]
end
local r = table.concat(out)
print(string.sub(r, 1, 12) .. ":" .. string.len(r))
