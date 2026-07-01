local total = 0
for round = 1, 160 do
  local t = {}
  for i = 1, 40 do
    t[i] = "tmp" .. tostring(round) .. ":" .. tostring(i)
  end
  total = total + string.len(t[round % 40 + 1])
end
print(total)
