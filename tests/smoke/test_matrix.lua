function matrix_mult(n)
  -- Simple matrix operations
  local a = {}
  local b = {}
  for i = 1, n do
    a[i] = {}
    b[i] = {}
    for j = 1, n do
      a[i][j] = i + j
      b[i][j] = i * j
    end
  end
  local sum = 0
  for i = 1, n do
    for j = 1, n do
      sum = sum + a[i][j] * b[i][j]
    end
  end
  return sum
end
print(matrix_mult(10))
