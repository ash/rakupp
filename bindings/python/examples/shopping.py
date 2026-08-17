# The Python guide's example: compile a grammar from a .raku file, parse
# text, and move the results into Python. Run from a checkout:
#
#   RAKUPP_LIB=build/librakupp.dylib python3 bindings/python/examples/shopping.py
#
# (.so on Linux; with `pip install -e bindings/python` and rakupp on PATH,
# plain `python3 bindings/python/examples/shopping.py` works with no env at all.)

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))            # a checkout; pip install removes this
import rakulang

g = rakulang.Grammar.from_file(os.path.join(HERE, "..", "..", "examples", "shopping.raku"),
                               name="Shopping", actions="ShoppingActions")

m = g.parse("milk=2\nbread = 1  eggs=12\n")   # a Match, or None if no match

items = m["item"]
print(len(items), "items")
for item in items:                            # lazy: one engine call per leaf
    print(item["name"].str(), "x", item["qty"].int())

print("total, computed in Raku:", m.made)     # ShoppingActions made this

print("as plain Python data:", m.tree())     # the whole match, eagerly

# A non-match returns None; strict=True raises a diagnosed error instead.
try:
    g.parse("milk=2\nbread=lots\n", strict=True)
except rakulang.ParseError as e:
    print(f"line {e.line} column {e.column} while trying <{e.rule}>")
