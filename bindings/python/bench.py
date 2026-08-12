# The G0 benchmark: the log grammar from a Python host vs the same grammar
# run by rakupp directly (GRAMMAR-PLAN's gate: "within a documented factor").
#
#   python3 bindings/python/bench.py [build-dir] [lines]
#
# Phases, best of three each, matching the plan's measurements:
#   parse     - Grammar.parse of the whole corpus (one boundary crossing)
#   tree      - eager conversion of the whole Match (one crossing)
#   selective - two fields per line, lazily (two crossings PER LINE — the
#               phase where host overhead is structurally visible)
#
# The factors stay split per phase on purpose; the plan forbids hiding a
# regression in one behind the others.

import os
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BUILD = os.path.join(ROOT, sys.argv[1] if len(sys.argv) > 1 else "build")
LINES = int(sys.argv[2]) if len(sys.argv) > 2 else 2000

RAKUPP = os.path.join(BUILD, "rakupp")
LIB = os.path.join(BUILD, "librakupp.dylib" if sys.platform == "darwin" else "librakupp.so")
GRAMMAR = os.path.join(ROOT, "tools", "grammar", "log.raku")
SHIM = os.path.join(ROOT, "bindings", "python", "rakulang", "grammar_shim.raku")


def best(f, n=3):
    times = []
    for _ in range(n):
        t0 = time.perf_counter()
        f()
        times.append((time.perf_counter() - t0) * 1000)
    return min(times)


with tempfile.TemporaryDirectory(prefix="grammar-bench-") as tmp:
    corpus = os.path.join(tmp, "log.txt")
    with open(corpus, "wb") as f:
        subprocess.run([RAKUPP, os.path.join(ROOT, "tools", "grammar", "gen-log.raku"),
                        str(LINES)], stdout=f, check=True)

    # rakupp-direct numbers, from the engine's own clock
    bench = os.path.join(tmp, "bench.raku")
    with open(bench, "w") as out, open(SHIM) as a, \
         open(os.path.join(ROOT, "tools", "grammar", "bench-body.raku")) as b:
        out.write(a.read() + "\n" + b.read())
    ref = subprocess.run([RAKUPP, bench, GRAMMAR, corpus],
                         capture_output=True, text=True, check=True)
    direct = dict(line.split() for line in ref.stdout.strip().splitlines())

    # the same phases from Python
    sys.path.insert(0, os.path.join(ROOT, "bindings", "python"))
    import rakulang

    rakulang.interpreter(LIB)
    g = rakulang.Grammar.from_source(open(GRAMMAR).read(), name="Log")
    text = open(corpus, encoding="utf-8").read()

    m = None

    def do_parse():
        global m
        m = g.parse(text)

    py_parse = best(do_parse)
    assert m, "bench: parse failed"
    py_tree = best(lambda: m.tree())
    n = len(m["line"])
    assert n == LINES

    def selective():
        for i in range(n):
            m["line"][i]["ip"].str()
            m["line"][i]["status"].str()

    py_sel = best(selective)

    size = os.path.getsize(corpus)
    print(f"corpus: {LINES} lines, {size} bytes; grammar: tools/grammar/log.raku")
    print(f"{'phase':<10} {'direct ms':>10} {'shim ms':>10} {'python ms':>10} {'py/direct':>10}")
    for phase, py in (("parse", py_parse), ("tree", py_tree), ("selective", py_sel)):
        key = {"selective": "sel"}.get(phase, phase)
        d = float(direct[f"direct_{key}"])
        s = float(direct[f"shim_{key}"])
        print(f"{phase:<10} {d:>10.2f} {s:>10.2f} {py:>10.2f} {py / d:>9.1f}x")
