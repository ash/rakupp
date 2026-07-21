# Higher-order functions, closures, map/filter, generator expressions.
def make_adder(n):
    return lambda x: x + n

add5 = make_adder(5)
print(add5(10))

nums = list(range(1, 11))
evens = list(filter(lambda x: x % 2 == 0, nums))
squares = list(map(lambda x: x ** 2, nums))
print(evens)
print(squares)
print(sum(x for x in nums if x > 5))
