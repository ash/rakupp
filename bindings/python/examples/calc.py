# The Python guide's second example: Raku as a library, no grammar involved.
# Evaluate source, call subs with Python values, read results back. Run from
# a checkout:
#
#   RAKUPP_LIB=build/librakupp.dylib python3 bindings/python/examples/calc.py
#
# (.so on Linux; with `pip install -e bindings/python` and rakupp on PATH,
# plain `python3 bindings/python/examples/calc.py` works with no env at all.)

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))            # a checkout; pip install removes this
import rakulang

raku = rakulang.interpreter()

# eval runs source in the mainline scope and keeps it, exactly like the REPL.
print("2 + 2 =", raku.eval("2 + 2"))

# So loading a file of subs is just an eval — they stay callable below.
with open(os.path.join(HERE, "..", "..", "examples", "calc.raku"), encoding="utf-8") as f:
    raku.eval(f.read())

# call passes Python values as arguments and returns Python values.
print("area(3, 4) =", raku.call("area", 3, 4))

primes = raku.call("primes-below", 30)              # a Raku list -> a Python list
print("primes below 30:", " ".join(str(p) for p in primes))

s = raku.call("stats", [3, 1, 4, 1, 5, 9, 2, 6])    # a Python list -> a Raku list
print("stats: count={} sum={} mean={} max={}".format(   # a Raku hash -> a Python dict
    s["count"], s["sum"], s["mean"], s["max"]))

print("greet:", raku.call("greet", {"name": "Ada", "age": 36}))

# Raku integers do not overflow; past 64 bits this one hands back digits.
print("30! =", raku.call("factorial", 30))

# A die inside Raku crosses as RakuError, the host's own exception type.
try:
    raku.call("checked-div", 10, 0)
except rakulang.RakuError as e:
    print("died:", e)
