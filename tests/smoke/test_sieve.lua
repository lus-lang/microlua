function sieve(n)
  local is_prime = {}
  for i = 1, n do is_prime[i] = true end
  for i = 2, math.floor(math.sqrt(n)) do
    if is_prime[i] then
      for j = i*i, n, i do
        is_prime[j] = false
      end
    end
  end
  local count = 0
  for i = 2, n do
    if is_prime[i] then count = count + 1 end
  end
  return count
end
print(sieve(100))
