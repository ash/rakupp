# rakulang — Raku from Python

A pure-source Python package over `librakupp`'s C ABI. No compiled glue: the
loader is `ctypes`, values cross through
[`rakupp.h`](../../include/rakupp/rakupp.h), and the grammar logic lives in a
small Raku shim (`rakulang/grammar_shim.raku`) the binding evaluates into its
interpreter at startup.

The package is named for the language, in the Raku community's disambiguated
spelling — `raku` is an unrelated package on PyPI. The engine underneath is
Raku++: `rakupp` the binary, `librakupp` the library. `import rakulang as
raku` if you like the short spelling.

Python is the reference binding; the other four follow it.

## 1. What you need

- **Python 3.8+.** No third-party packages — `ctypes` is in the standard
  library.
- **`librakupp`.** From the repo root:

  ```bash
  cmake -B build -DCMAKE_BUILD_TYPE=Release -DRAKUPP_BUILD_SHARED=ON
  cmake --build build -j
  ```

  A build directory configured without `-DRAKUPP_BUILD_SHARED=ON` is
  static-only and this package cannot use it.

## 2. Install

From a checkout, `pip install -e bindings/python`, after which plain `import
rakulang` works. The examples below add the directory to `sys.path` instead,
so they run against a fresh checkout with nothing installed.

Finding the library usually needs no configuration: if `rakupp` is on PATH,
the loader takes `librakupp` from beside it — an installed layout's sibling
`lib/`, a Homebrew keg's, or the build directory the binary sits in. A
platform wheel carries its own copy and needs nothing at all.

To override, name one: an explicit path to
`rakulang.interpreter("/path/to/librakupp.dylib")`, or `RAKUPP_LIB` (the
file), or `RAKUPP_HOME` (an install prefix with `lib/`). **A library you name
is used as given.** If it cannot be loaded you get that error, not a quiet
fall-back to whichever other library happens to be findable — the usual cause
is an architecture mismatch, and falling back makes the symptom (some other
build's behaviour) point nowhere near the cause. Unset the variable to search
instead.

On ELF platforms the library is loaded `RTLD_GLOBAL` so Raku extensions
`dlopen`'ed later can resolve `rk_*` — a requirement from ABI-PLAN A3, not a
preference.

## 3. Two minutes

Run both examples from the repo root (`.so` for `.dylib` on Linux):

```bash
RAKUPP_LIB=$PWD/build/librakupp.dylib python3 bindings/python/examples/calc.py
```
```bash
RAKUPP_LIB=$PWD/build/librakupp.dylib python3 bindings/python/examples/shopping.py
```

`calc` ([examples/calc.py](examples/calc.py)) prints:

```
2 + 2 = 4
area(3, 4) = 12
primes below 30: 2 3 5 7 11 13 17 19 23 29
stats: count=8 sum=31 mean=3.88 max=9
greet: Hello, Ada! You are 36.
30! = 265252859812191058636308480000000
died: division by zero
```

`shopping` ([examples/shopping.py](examples/shopping.py)) prints:

```
3 items
milk x 2
bread x 1
eggs x 12
total, computed in Raku: 15
as plain Python data: {'item': [{'name': 'milk', 'qty': '2'}, {'name': 'bread', 'qty': '1'}, {'name': 'eggs', 'qty': '12'}]}
line 2 column 7 while trying <qty>
```

If you see both, the binding works.

## 4. Running Raku

```python
import rakulang

raku = rakulang.interpreter()          # the process's interpreter

raku.eval("my $x = 41")
raku.eval("$x + 1")                    # 42 — eval keeps state, like the REPL

raku.eval(open("calc.raku").read())    # so loading a file of subs is an eval
raku.call("area", 3, 4)                # 12
raku.call("stats", [3, 1, 4])          # {'count': 3, 'sum': 8, ...}
raku.call("greet", {"name": "Ada"})    # a dict becomes a Raku hash

raku.can("area")                       # True
raku.version                           # '3.14.0'
```

`eval` returns the last statement's value; `call` looks the routine up in the
mainline scope, so anything an earlier `eval` declared is callable. Arguments
convert automatically — `None`, `bool`, `int` (any width), `float`, `str`,
`list`, `tuple`, `dict`. Anything else raises `TypeError`.

## 5. Parsing with grammars

```python
log = rakulang.Grammar.from_file("log.raku", name="Log", actions="LogActions")

m = log.parse(text)                    # a handle, not data; None if no match
for line in m["line"]:                 # lazy: one engine call per leaf
    print(line["ip"].str(), line["status"].int())
print(m["line"][0]["size"].made)       # computed by LogActions, in the parse

everything = m.tree()                  # eager, opt-in (~1.4× the parse)
```

`from_file(path, name=..., actions=...)` compiles and caches: identical
source compiles once, and each *named* compile is isolated in its own wrapper
package, so recompiling an edited grammar under the same name works and
earlier handles keep the body they were compiled from. Without `name` there
is no wrapper, and a same-name recompile raises the engine's
`X::Redeclaration`. `name` may be omitted only when the grammar declaration
is the file's last statement.

`parse(text, rule=...)` anchors to the whole input and returns a `Match` or
`None`; pass `rule=` to parse a fragment with one rule. Indexing builds a
lazy path — nothing crosses the boundary until a terminal: `.str()`,
`.int()`, `.num()`, `.made`, `bool()`, `len()`, iteration, `.tree()`, or
`.match()` (which returns an independent rooted `Match`).

## 6. Values

Raku `Int` → `int`, `Num`/`Rat` → `float`, `Str` → `str`, `List` → `list`,
`Hash` → `dict`, `True`/`False` → `bool`, `Any` → `None`. The same rules run
in reverse for arguments.

An integer wider than 64 bits arrives as a string of digits. In a `tree()`, a
match node with no sub-captures becomes its matched *text* — `qty` is the
string `"2"` — so use `.int()` on the node, or an actions class, for numbers.

## 7. Errors

`RakuError` is a Raku `die` crossing the boundary. `ParseError` is its
subclass for a diagnosed non-match, carrying `.line`, `.column`, `.rule` and
`.pos`:

```python
try:
    g.parse(text, strict=True)
except rakulang.ParseError as e:
    print(f"line {e.line} column {e.column} while trying <{e.rule}>")
```

A failed terminal on a missing capture raises `RakuError`; `bool()` and
`len()` answer `False`/`0` instead, which is how you probe for one.

## 8. Lifetime and threading

One interpreter per process, created on first use; one host thread talks to
it at a time (Raku code inside it threads freely). Matches hold rooted values
in the interpreter — `close()` them, use a `with` block, or let the GC do it.
Values from `eval` and `call` are already plain Python data and need nothing.

## 9. Testing

```bash
build/rakupp tools/bindings-smoke.raku
```

Runs both examples in all five languages and checks the output against
[../examples/expected/](../examples/expected). For the deep gate — this
binding driving the same grammar and 2000-line corpus as the Raku reference
driver, byte-compared — run `build/rakupp tools/grammar-smoke.raku`. Both run
in CI on every push.

## When things go wrong

- **`librakupp not found`** — the loader lists every path it tried. Set
  `RAKUPP_LIB` to the library file. If a `rakupp` binary was found but no
  library beside it, that build directory is static-only: rebuild with
  `-DRAKUPP_BUILD_SHARED=ON`.
- **`incompatible architecture`** — your `python3` and the library disagree
  (`file $(which python3)` against `file build/librakupp.dylib`). Build the
  library for your interpreter's architecture:
  `cmake -B build-x64 -DCMAKE_OSX_ARCHITECTURES=x86_64 -DRAKUPP_BUILD_SHARED=ON ...`
- **`rk_new refused`** — something already created an interpreter in this
  process. Use `rakulang.interpreter()`, which returns the shared one.

## Numbers (G0 gate, 2026-08-11, M-series macOS)

2000-line / 168 KB access log, seven-token grammar, best of three
(`python3 bindings/python/bench.py build-shared`):

| phase | rakupp direct | via shim, engine-side | Python host | host/direct |
|---|---:|---:|---:|---:|
| parse | 11.6 ms | 10.7 ms | 10.5 ms | **1.0×** |
| tree (eager) | 68.0 ms | 68.2 ms | 93.0 ms | **1.4×** |
| selective (2 fields × 2000 lines) | 1.7 ms | 25.9 ms | 52.6 ms | **~31×** |

Parse is engine-bound — the host boundary adds nothing. Selective access
costs ~13 µs per leaf (half the walk sub, half ABI + ctypes); it exists
because eager conversion of everything nobody asked for is usually the worse
deal, but if a profile ever shows the per-leaf cost dominating a real
workload, that is GRAMMAR-PLAN G4's cue (a native Match walker), not a reason
to grow this layer.
