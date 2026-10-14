"""The other side of t/oracle/rat.raku: Python's fractions.Fraction as the
oracle for FatRat, and float() of an int or a Fraction, which Python rounds
correctly, as the oracle for .Num.

    python3 -I rat-oracle.py gen FILE SEED COUNT

writes COUNT cases to FILE, one per line, tab-separated:

    <op> <an> <ad> <bn> <bd> <expected>

a = an/ad and b = bn/bd (`-` where unused). Exact answers are written n/d,
reduced, denominator positive, which is what `.nude` gives. The Raku meaning
of each op was checked against Rakudo 2026.09 where it could differ: `round`
takes a half UP (toward +Inf), `floor` and `ceiling` as usual.
"""

import math
import random
import sys
from fractions import Fraction

if hasattr(sys, 'set_int_max_str_digits'):
    sys.set_int_max_str_digits(0)


def num(r, max_digits):
    kind = r.randint(0, 6)
    if kind == 0:
        return r.choice([0, 1, 2, 3, 10, 2 ** 53, 2 ** 53 + 1, 2 ** 64, 10 ** 18, 2 ** 63 - 1])
    if kind == 1:                       # a power of two or ten, give or take one
        return max(0, r.choice([2, 10]) ** r.randint(1, max_digits * 3) + r.randint(-1, 1))
    return r.randrange(10 ** r.randint(0, max_digits))


def rat(r, max_digits=60):
    n = num(r, max_digits)
    d = num(r, max_digits) or 1
    if r.random() < 0.2:                # numerator and denominator sharing a big factor
        g = num(r, max_digits) or 1
        n, d = n * g, d * g
    if r.random() < 0.4:
        n = -n
    return n, d


def nd(f):
    return '%d/%d' % (f.numerator, f.denominator)


def float_or_inf(x):
    try:
        return repr(float(x))
    except OverflowError:
        return 'inf' if x > 0 else '-inf'


def base_repeating(f, base):
    """Raku's .base-repeating: the digits before the repeating part, and the
    repeating part, as Rakudo's Rational.base-repeating writes them."""
    digits = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ'
    neg = f < 0
    f = abs(f)
    whole = f.numerator // f.denominator
    rem = f.numerator - whole * f.denominator
    d = f.denominator
    w = ''
    n = whole
    while True:
        n, k = divmod(n, base)
        w = digits[k] + w
        if not n:
            break
    if not rem:                         # integral: no point, as Rakudo writes it
        return ('-' if neg else '') + w, ''
    seen, frac = {}, ''
    while rem and rem not in seen:
        seen[rem] = len(frac)
        rem *= base
        frac += digits[rem // d]
        rem %= d
    if rem:
        i = seen[rem]
        nonrep, rep = frac[:i], frac[i:]
    else:
        nonrep, rep = frac, ''
    return ('-' if neg else '') + w + '.' + nonrep, rep


def case(r):
    op = r.choice(['add', 'sub', 'mul', 'div', 'cmp', 'floor', 'ceiling', 'round', 'num', 'num',
                   'int-num', 'base-repeating', 'nude'])
    an, ad = rat(r)
    bn, bd = rat(r)
    a, b = Fraction(an, ad), Fraction(bn, bd)
    if op == 'add':
        want = nd(a + b)
    elif op == 'sub':
        want = nd(a - b)
    elif op == 'mul':
        want = nd(a * b)
    elif op == 'div':
        if b == 0:
            bn, bd, b = 7, 3, Fraction(7, 3)
        want = nd(a / b)
    elif op == 'cmp':
        if r.random() < 0.3:
            bn, bd = an * 3, ad * 3
            b = a
        want = str((a > b) - (a < b))
    elif op == 'floor':
        want = str(math.floor(a))
    elif op == 'ceiling':
        want = str(math.ceil(a))
    elif op == 'round':
        if r.random() < 0.4:                 # exactly a half
            an, ad = 2 * r.randrange(-10 ** 20, 10 ** 20) + 1, 2
            a = Fraction(an, ad)
        want = str(math.floor(a + Fraction(1, 2)))
    elif op == 'num':
        if r.random() < 0.3:                 # halfway between two doubles
            m = r.randrange(2 ** 52, 2 ** 53)
            e = r.randint(-1100, 1000)
            an, ad = (2 * m + 1) * (2 ** e if e > 0 else 1), (2 ** -e if e < 0 else 1) * 2
            a = Fraction(an, ad)
        want = float_or_inf(a)
    elif op == 'int-num':
        an = num(r, 330) * r.choice([1, -1])
        if r.random() < 0.3:                 # halfway between two doubles
            an = (2 * r.randrange(2 ** 52, 2 ** 53) + 1) << r.randint(0, 960)
        ad = '-'
        want = float_or_inf(an)
        bn = bd = '-'
        return '%s\t%d\t-\t-\t-\t%s' % (op, an, want)
    elif op == 'base-repeating':
        an, ad = r.randrange(-10 ** 6, 10 ** 6), r.randint(1, 5000)
        if r.random() < 0.1:                 # cycles past 10,000 digits
            ad = r.choice([10007, 10009, 65537, 100003])
        if r.random() < 0.1:                 # integral
            ad = 1
        base = r.choice([2, 3, 7, 10, 12, 16, 36])
        bn, bd = base, '-'
        nonrep, rep = base_repeating(Fraction(an, ad), base)
        want = nonrep + '|' + rep
        return '%s\t%d\t%d\t%d\t-\t%s' % (op, an, ad, base, want)
    else:
        want = nd(a)
    return '%s\t%d\t%d\t%s\t%s\t%s' % (op, an, ad, bn, bd, want)


def gen(path, seed, count):
    r = random.Random(seed)
    with open(path, 'w') as f:
        for _ in range(count):
            f.write(case(r) + '\n')


if __name__ == '__main__':
    if sys.argv[1] == 'gen':
        gen(sys.argv[2], int(sys.argv[3]), int(sys.argv[4]))
    else:
        sys.exit('usage: rat-oracle.py gen FILE SEED COUNT')
