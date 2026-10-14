"""The other side of t/oracle/int.raku: Python's int, an arbitrary-precision
integer that shares no code with src/BigInt.cpp, as the oracle for Raku's Int.

    python3 -I int-oracle.py gen FILE SEED COUNT

writes COUNT cases to FILE, one per line, tab-separated:

    <op> <a> <b> <c> <expected>

Operands and the answer are decimal (`-` for an unused operand). The Raku
semantics each op stands for were checked against Rakudo 2026.09 where they
could differ from Python's (negative operands of gcd, lcm, the bitwise ops and
the shifts, floor `div` and `%`): they agree.
"""

import math
import random
import sys

if hasattr(sys, 'set_int_max_str_digits'):        # 3.11+ caps int <-> str at 4300 digits
    sys.set_int_max_str_digits(0)

BASE = 10 ** 9     # src/BigInt.h's limb


# ---- operands --------------------------------------------------------------
#
# Sizes are drawn in LIMBS, around every threshold the code has: one limb, the
# u64 and u128 fast paths (2-3 and 4-5 limbs), the schoolbook kernel's carry
# every 18 rows, Karatsuba from 96 limbs. Shapes are the ones long division
# and carry propagation get wrong: limbs of all nines, a top limb either side of
# BASE/2 (where algorithm D's normalisation factor changes), powers of two and
# of ten plus or minus a little, and the int64 edges the integer kernels bail
# out at.

LIMB_SIZES = [1, 2, 3, 4, 5, 6, 17, 18, 19, 35, 36, 37, 95, 96, 97, 98, 191, 192, 193, 400, 1000, 2500]

INT64_EDGES = [0, 1, -1, 2 ** 31 - 1, 2 ** 31, 2 ** 32 - 1, 2 ** 32, 2 ** 63 - 1, -2 ** 63, 2 ** 63,
               -2 ** 63 - 1, 2 ** 64 - 1, 2 ** 64, 10 ** 18, 10 ** 18 - 1, 10 ** 19, 2 ** 127, 2 ** 128 - 1,
               2 ** 128, 10 ** 36 - 1, 10 ** 36]


def limbs(r, n):
    kind = r.randint(0, 9)
    if kind == 0:                                  # all nines
        return BASE ** n - 1
    if kind == 1:                                  # top limb at BASE/2 - 1, BASE/2, BASE/2 + 1
        top = BASE // 2 + r.randint(-1, 1)
        return top * BASE ** (n - 1) + r.randrange(BASE ** (n - 1)) if n > 1 else top
    if kind == 2:                                  # top limb tiny: normalisation by a big factor
        return r.randint(1, 3) * BASE ** (n - 1) + r.randrange(BASE ** (n - 1)) if n > 1 else r.randint(1, 3)
    if kind == 3:                                  # power of ten, plus or minus a little
        return max(1, 10 ** (9 * n - r.randint(0, 8)) + r.randint(-3, 3))
    if kind == 4:                                  # power of two, plus or minus a little
        return max(1, 2 ** int(29.9 * n - r.randint(0, 29)) + r.randint(-3, 3))
    if kind == 5:                                  # sparse: a few random limbs, zeros between
        v = 0
        for _ in range(r.randint(1, 4)):
            v += r.randrange(1, BASE) * BASE ** r.randrange(n)
        return v + BASE ** (n - 1)
    return r.randrange(BASE ** (n - 1), BASE ** n)


def operand(r, max_limbs=2500):
    if r.random() < 0.15:
        return r.choice(INT64_EDGES) * r.choice([1, 1, -1])
    n = r.choice([s for s in LIMB_SIZES if s <= max_limbs])
    v = limbs(r, n)
    return -v if r.random() < 0.3 else v


def related(r, a):
    """A second operand built from the first, for the shapes random pairs never
    produce: a divisor that goes into it almost exactly, or one off from it."""
    kind = r.randint(0, 3)
    if kind == 0:
        return a + r.randint(-2, 2) or 1
    if kind == 1:
        return a // BASE ** r.randint(1, 3) or 1
    if kind == 2:
        return -a or 1
    return abs(a) // 2 + 1


# ---- the ops, as Raku means them -------------------------------------------

def floor_div(a, b): return a // b             # Raku `div` and `%` floor, as Python's do
def rk_lcm(a, b): return 0 if a == 0 or b == 0 else abs(a * b) // math.gcd(a, b)


def base_str(v, b):
    digits = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ'
    if v == 0:
        return '0'
    s, n = '', abs(v)
    while n:
        n, d = divmod(n, b)
        s = digits[d] + s
    return ('-' if v < 0 else '') + s


def lsb(v): return (v & -v).bit_length() - 1


def case(r):
    op = r.choice(['add', 'sub', 'mul', 'mul', 'div', 'div', 'mod', 'divmod-identity', 'cmp',
                   'gcd', 'lcm', 'pow', 'expmod', 'shl', 'shr', 'and', 'or', 'xor', 'not',
                   'base', 'parse-base', 'msb', 'lsb', 'str'])
    a = operand(r)
    b = operand(r) if r.random() < 0.6 else related(r, a)
    c = '-'
    if op == 'add':
        want = a + b
    elif op == 'sub':
        want = a - b
    elif op == 'mul':
        want = a * b
    elif op in ('div', 'mod', 'divmod-identity'):
        if b == 0:
            b = 7
        if r.random() < 0.3:                      # dividend = b*q + r, r at an edge
            q = operand(r, 400)
            a = b * q + r.choice([0, 1, -1, abs(b) - 1, -(abs(b) - 1)])
        want = a // b if op == 'div' else a % b if op == 'mod' else a
    elif op == 'cmp':
        if r.random() < 0.3:
            b = a + r.choice([-1, 0, 1])
        want = (a > b) - (a < b)
    elif op == 'gcd':
        # Past 64 bits this is Lehmer's algorithm over the binary form: it
        # reads the top 61 bits of one operand and the same bits of the other,
        # runs Euclid on them while they decide the quotients, and divides
        # (algorithm D on 64-bit words) when they cannot decide the first.
        a, b = operand(r, 2500), operand(r, 2500)
        kind = r.randint(0, 9)
        if kind <= 3:                              # a large common factor
            g = operand(r, 1000)
            a, b = a * g, b * g
        elif kind == 4:                            # neighbouring Fibonacci numbers: every quotient 1
            n = r.randint(90, 60000)
            f0, f1 = 0, 1
            for _ in range(n):
                f0, f1 = f1, f0 + f1
            a, b = f1 * r.choice([1, -1]), f0
            if r.random() < 0.5:
                g = abs(operand(r, 50)) or 3
                a, b = a * g, b * g
        elif kind == 5:                            # a huge quotient, then the rest
            b = operand(r, 400) or 11
            a = b * operand(r, 2000) + r.choice([0, 1, -1, abs(b) // 3])
        elif kind == 6:                            # long runs of ones and zeros in binary
            def runs():
                n = r.randint(65, 80000)
                return (1 << n) - (1 << r.randint(0, n - 1)) + r.randint(0, 3)
            a, b = runs(), runs()
        elif kind == 7:                            # a common power of two, and one more of it
            k = r.randint(1, 20000)
            a, b = a << k, b << (k + r.randint(0, 64))
        want = math.gcd(a, b)
    elif op == 'lcm':
        a, b = operand(r, 400), operand(r, 400)
        want = rk_lcm(a, b)
    elif op == 'pow':
        a = operand(r, 40)
        b = r.randint(0, max(1, 4000 // max(1, len(str(abs(a))))))
        want = a ** b
    elif op == 'expmod':
        a, b = abs(operand(r, 60)), abs(operand(r, 20))
        m = abs(operand(r, 60)) or 7
        c = m
        # Rakudo answers 1 for a zero exponent whatever the modulus, 1 included
        # (libtommath's mp_exptmod does not reduce it), and Raku++ agrees.
        want = 1 if b == 0 else pow(a, b, m)
    elif op in ('shl', 'shr'):
        b = r.choice([0, 1, 29, 30, 31, 32, 63, 64, 65, 127, 128, r.randint(0, 5000)])
        want = a << b if op == 'shl' else a >> b
    elif op == 'and':
        want = a & b
    elif op == 'or':
        want = a | b
    elif op == 'xor':
        want = a ^ b
    elif op == 'not':
        b = '-'
        want = ~a
    elif op == 'base':
        a = operand(r, 400)
        b = r.randint(2, 36)
        return '%s\t%d\t%d\t-\t%s' % (op, a, b, base_str(a, b))
    elif op == 'parse-base':
        a = operand(r, 400)
        b = r.randint(2, 36)
        return '%s\t%s\t%d\t-\t%d' % (op, base_str(a, b), b, a)
    elif op in ('msb', 'lsb'):
        a = abs(a) or 1
        b = '-'
        want = a.bit_length() - 1 if op == 'msb' else lsb(a)
    else:                                          # str: the round trip through text
        b = '-'
        want = a
    return '%s\t%s\t%s\t%s\t%d' % (op, a, b, c, want)


def gen(path, seed, count):
    r = random.Random(seed)
    with open(path, 'w') as f:
        for _ in range(count):
            f.write(case(r) + '\n')


if __name__ == '__main__':
    if sys.argv[1] == 'gen':
        gen(sys.argv[2], int(sys.argv[3]), int(sys.argv[4]))
    else:
        sys.exit('usage: int-oracle.py gen FILE SEED COUNT')
