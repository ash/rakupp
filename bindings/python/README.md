# rakulang — Raku from Python

A pure-source Python package over `librakupp`'s C ABI. No compiled glue: the
loader is `ctypes`, values cross through
[`rakupp.h`](https://github.com/ash/rakupp/blob/main/include/rakupp/rakupp.h), and the grammar logic lives in a
small Raku shim (`rakulang/grammar_shim.raku`) the binding evaluates into its
interpreter at startup.

The package is named for the language, in the Raku community's disambiguated
spelling — `raku` is an unrelated package on PyPI. The engine underneath is
Raku++: `rakupp` the binary, `librakupp` the library. `import rakulang as
raku` if you like the short spelling.

Python is the reference binding; the other four follow it.

## 1. What you need

- **Python 3.9+.** No third-party packages — `ctypes` is in the standard
  library.
- **`librakupp`**, unless the wheel from PyPI is what you install: that one
  carries the library inside it. From a checkout, build it at the repo root:

  ```bash
  cmake -B build -DCMAKE_BUILD_TYPE=Release -DRAKUPP_BUILD_SHARED=ON
  cmake --build build -j
  ```

  A build directory configured without `-DRAKUPP_BUILD_SHARED=ON` is
  static-only and this package cannot use it.

## 2. Install

```bash
pip install rakulang
```

The wheel carries `librakupp` inside it, so it needs no rakupp on the
machine; `rakulang.interpreter().version` says which engine it holds, and the
package version is that engine's. It is built for macOS (universal) and Linux
(x86_64 and aarch64), and each wheel's own platform tag names the floor it
needs.

From a checkout, `pip install -e bindings/python` instead, after which plain
`import rakulang` works. The examples below add the directory to `sys.path`,
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

`calc` ([examples/calc.py](https://github.com/ash/rakupp/blob/main/bindings/python/examples/calc.py)) prints:

```
2 + 2 = 4
area(3, 4) = 12
primes below 30: 2 3 5 7 11 13 17 19 23 29
stats: count=8 sum=31 mean=3.88 max=9
greet: Hello, Ada! You are 36.
30! = 265252859812191058636308480000000
died: division by zero
```

`shopping` ([examples/shopping.py](https://github.com/ash/rakupp/blob/main/bindings/python/examples/shopping.py)) prints:

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

raku.version                           # '3.14.0'
```

`eval` returns the last statement's value, converted to Python data.

### Define a sub in Raku, call it from Python

Declare the sub with `eval` — the declaration stays in the interpreter's
mainline scope — then `call` it by name. Python arguments bind to the
signature's positional parameters, in order, and the return value comes back
as Python data:

```python
raku.eval("""
    sub area($w, $h)   { $w * $h }
    sub total(@prices) { @prices.sum }
    sub describe(%p)   { "%p<name> costs %p<price>" }
    sub hello($name, $greeting = 'Hello') { "$greeting, $name!" }
""")

raku.call("area", 3, 4)                # 12 — one argument per parameter
raku.call("total", [1, 2, 3.5])        # 6.5 — a list binds to @prices
raku.call("describe", {"name": "tea", "price": 3})
                                       # 'tea costs 3' — a dict binds to %p
raku.call("hello", "Ada")              # 'Hello, Ada!' — the default fills in
raku.can("area")                       # True; False before the eval
```

The subs may as well come from a file: `raku.eval(open("calc.raku").read())`
declares everything in it, which is how [examples/calc.py](https://github.com/ash/rakupp/blob/main/bindings/python/examples/calc.py)
loads [../examples/calc.raku](https://github.com/ash/rakupp/blob/main/bindings/examples/calc.raku).

Arguments convert automatically — `None`, `bool`, `int` (any width), `float`,
`str`, `list`, `tuple`, `dict`. Anything else raises `TypeError`. The
conversion is by Python type, so `sub flag(Bool $b)` wants `True`, not `1`.

The call is checked exactly as a call written in Raku is. Too few or too many
arguments, or a value that fails a type constraint, raise `RakuError`
carrying the engine's message instead of binding silently:

```python
raku.call("area", 3)
# RakuError: Calling area(Int) will never work with declared signature ($w, $h)
```

**Named parameters.** `call` passes positionals only. A sub declared with
named parameters is called by writing the call in Raku and evaluating it:

```python
raku.eval('sub greet(:$name, :$age = 0) { "Hello, $name! You are $age." }')

raku.eval('greet(name => "Ada", age => 36)')   # 'Hello, Ada! You are 36.'
raku.call("greet", {"name": "Ada"})            # RakuError — the dict is one
                                               # positional Hash, not two names
```

Or declare the sub to take a hash, `sub greet(%who)`, and pass a dict — which
is what `calc.raku` does. `multi` subs, slurpy `*@args`, and `our sub`s inside
a package (`raku.call("Geo::perimeter", 3, 4)`) all resolve through `call`. A
method is reached through `eval`: `raku.eval("Counter.new.bump.n")`.

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

An integer wider than 64 bits arrives as an ordinary Python `int`: the C ABI
hands integers over as an `int64`, so the binding reads the digits instead
whenever that saturates. In a `tree()`, a
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
[../examples/expected/](https://github.com/ash/rakupp/tree/main/bindings/examples/expected). For the deep gate — this
binding driving the same grammar and 2000-line corpus as the Raku reference
driver, byte-compared — run `build/rakupp tools/grammar-smoke.raku`. Both run
in CI on every push.

## Troubleshooting

Four questions settle most reports: which Python ran, which copy of the
package it imported, which library file that copy loaded, and which engine
that library is. One line answers all four:

```bash
python3 -c "import rakulang, sys; r = rakulang.interpreter(); print(sys.executable, rakulang.__file__, r._lib._name, r.version, sep='\n')"
```

`_lib._name` is the loaded file's path — a private attribute, fine for
diagnosis. Compare the last line with `rakupp --version` for the binary on
PATH: the library reports the plain release number, the binary adds its git
describe suffix, and the leading numbers should agree.

The search order decides the third line. A library you name is used as
given: the path passed to `interpreter()`, else `RAKUPP_LIB`, else
`RAKUPP_HOME/lib/`. Otherwise the loader takes, in this order, a copy bundled
inside the package (`rakulang/_lib/`), the library beside the `rakupp` on
PATH (its sibling `lib/`, then its own directory), and the system linker
path.

**`librakupp not found`.** Nothing bundled, nothing beside `rakupp`, nothing
on the linker path; the message lists every path it tried. If it continues
`A rakupp binary WAS found (…) but its build carries no shared library`, the
build directory on PATH is configured without `-DRAKUPP_BUILD_SHARED=ON`.
Reconfigure it with that option and build again, set `RAKUPP_LIB` to a build
that has the library, or install the platform wheel (below).

**`RAKUPP_LIB names …, which could not be loaded`.** A named library is
authoritative; the loader does not fall back to another. The quoted `dlopen`
error says why: `no such file` when the path does not exist — a relative path
is resolved against the current directory, so a shell profile wants an
absolute one — or the architecture mismatch below. Unset the variable to
search instead.

**`incompatible architecture`.** Your `python3` and the library disagree
(`file $(which python3)` against `file build/librakupp.dylib`). Build the
library for your interpreter's architecture:
`cmake -B build-x64 -DCMAKE_OSX_ARCHITECTURES=x86_64 -DRAKUPP_BUILD_SHARED=ON ...`

**`.version` is older than `rakupp --version`.** The library the loader
found is a leftover. A build directory keeps its `librakupp.*` files until a
build overwrites them, and a directory reconfigured without
`-DRAKUPP_BUILD_SHARED=ON` never does: the binary beside them stays current
while the library keeps the version it had. `make rakupp` rebuilds the binary
only; `cmake --build <dir>` with no target rebuilds the library too. Delete
the leftovers or rebuild the shared target — the loader cannot tell a leftover
from a fresh build.

**`AttributeError: dlsym(…, rk_…): symbol not found`.** Raised from
`interpreter()` when the library lacks an entry point this package declares,
which means the library predates the package. Rebuild it from the same
checkout the package came from.

**`import rakulang` is not the copy you edited.** `rakulang.__file__` says
which one loaded. `pip install -e bindings/python` imports the checkout
itself; a plain `pip install bindings/python`, or a wheel, copies the package
at install time and does not follow later edits — reinstall to refresh. `python`
and `python3` can be different interpreters with different site-packages.

**The platform wheel.** `tools/build-wheel.sh <build-dir>` bundles that
build's library into the package, and a `pip install` of the result needs no
`rakupp` on PATH and no variables. The bundled copy is a snapshot: `.version`
reports it, and refreshing it is a rebuild and a reinstall. The script builds
in a scratch venv, so it needs pip access to PyPI.

```bash
tools/build-wheel.sh build dist-wheel
```
```bash
python3 -m pip install --force-reinstall --no-deps dist-wheel/rakulang-*.whl
```

**`rk_new refused: an interpreter is already live in this process`.**
Something already created an interpreter in this process. Use
`rakulang.interpreter()`, which returns the shared one.

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
