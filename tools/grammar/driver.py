# The Python half of the byte-identical gate: the same walk as
# driver-body.raku, through the rakulang package over librakupp. Run by
# grammar-smoke.raku; outputs are compared byte for byte.
#
#   python3 driver.py <librakupp-path> <grammar-file> <input-file>

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "..", "bindings", "python"))
import rakulang

lib, grammar_file, input_file = sys.argv[1], sys.argv[2], sys.argv[3]


def canon(x):
    if isinstance(x, list):
        return "[" + ",".join(canon(v) for v in x) + "]"
    if isinstance(x, dict):
        return "{" + ",".join(k + ":" + canon(x[k]) for k in sorted(x)) + "}"
    return str(x)


def raku_bool(b):
    return "True" if b else "False"


it = rakulang.interpreter(lib)
g = rakulang.Grammar.from_file(grammar_file, name="Log", actions="LogActions")

with open(input_file, encoding="utf-8") as f:
    m = g.parse(f.read())
assert m, "gate: the log corpus did not parse"

lines = m["line"]
print("lines", len(lines))
print("islist", raku_bool(lines._walk("islist")))

for line in lines:
    print(line["ip"].str(), line["status"].str())

print("made", m["line"][0]["size"].made)
print("req.str", m["line"][42]["req"].str())
print("size.int", m["line"][42]["size"].int())
print("missing", raku_bool(bool(m["nope"])))
print("tree", canon(m["line"][999].tree()))

one = g.parse('7.7.7.7 - - [x] "GET / HTTP/1.1" 200 5\n', rule="line")
print("rule-parse", one["status"].str())

bad = g.parse("this is not a log line")
print("failed-parse", "Match" if bad else "None")

try:
    g.parse("this is not a log line", strict=True)
    print("diag none")
except rakulang.ParseError as e:
    print(f"diag line {e.line} col {e.column} rule {e.rule}")
