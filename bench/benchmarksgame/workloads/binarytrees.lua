-- Adapted from The Computer Language Benchmarks Game binary-trees Lua program.
-- Scaled to keep local CI/developer runs short while still allocating many
-- short-lived tree nodes.

local function pow2(n)
  local x = 1
  for _ = 1, n do
    x = x * 2
  end
  return x
end

local function bottom_up_tree(depth)
  if depth <= 0 then
    return {nil, nil}
  end
  return {bottom_up_tree(depth - 1), bottom_up_tree(depth - 1)}
end

local function item_check(tree)
  if tree[1] == nil then
    return 1
  end
  return 1 + item_check(tree[1]) + item_check(tree[2])
end

local min_depth = 4
local max_depth = 10
local stretch_depth = max_depth + 1
local stretch_tree = bottom_up_tree(stretch_depth)
print("stretch tree of depth " .. stretch_depth .. "\t check: " .. item_check(stretch_tree))

local long_lived_tree = bottom_up_tree(max_depth)

for depth = min_depth, max_depth, 2 do
  local iterations = pow2(max_depth - depth + min_depth)
  local check = 0
  for _ = 1, iterations do
    check = check + item_check(bottom_up_tree(depth))
  end
  print(iterations .. "\t trees of depth " .. depth .. "\t check: " .. check)
end

print("long lived tree of depth " .. max_depth .. "\t check: " .. item_check(long_lived_tree))
