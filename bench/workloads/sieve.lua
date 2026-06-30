-- sieve: Sieve of Eratosthenes -- stresses sequential table fill + indexed
-- reads/writes over a large array part (no holes). Avoids math.sqrt so the
-- whole computation is pure integer. Output: number of primes below N.
-- N keeps the array (~N*8 bytes plus doubling-growth headroom) inside the
-- default 16 MB heap so the workload runs without an explicit --memory-limit.
local N = 500000

local is_prime = {}
for i = 1, N do is_prime[i] = true end

local count = 0
for i = 2, N do
  if is_prime[i] then
    count = count + 1
    local j = i + i
    while j <= N do
      is_prime[j] = false
      j = j + i
    end
  end
end

print(count)
