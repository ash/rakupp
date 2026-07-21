# List operations, slicing, comprehensions.
def primes_up_to(n):
    sieve = [True] * (n + 1)
    sieve[0] = sieve[1] = False
    for i in range(2, int(n ** 0.5) + 1):
        if sieve[i]:
            for j in range(i * i, n + 1, i):
                sieve[j] = False
    return [i for i in range(n + 1) if sieve[i]]

primes = primes_up_to(50)
print(primes)
print(f"{len(primes)} primes, sum = {sum(primes)}")
