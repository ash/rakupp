# Recursion, list comprehension, and Python's arbitrary-precision ints.
def fib(n):
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)

print([fib(i) for i in range(15)])

# iterative, to reach a big one (bigints come for free)
def fib_big(n):
    a, b = 0, 1
    for _ in range(n):
        a, b = b, a + b
    return a

print(fib_big(100))
