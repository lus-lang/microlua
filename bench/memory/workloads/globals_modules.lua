module_like = {}
module_like.values = {}
module_like.total = 0

function module_like.add(k, v)
  module_like.values[k] = v
  module_like.total = module_like.total + v
end

for i = 1, 420 do
  module_like.add("key" .. tostring(i), i)
end

print(module_like.total)
