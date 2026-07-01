local function item_check(t)
  if t[2] == nil then
    return t[1]
  end
  return t[1] + item_check(t[2]) - item_check(t[3])
end

local function bottom_up_tree(item, depth)
  if depth > 0 then
    return {item, bottom_up_tree(2 * item - 1, depth - 1),
            bottom_up_tree(2 * item, depth - 1)}
  end
  return {item}
end

local sum = 0
for i = 1, 90 do
  sum = sum + item_check(bottom_up_tree(i, 6))
end
print(sum)
