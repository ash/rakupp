# rakulang — Raku grammars for Python

A pure-source Python package over `librakupp`'s C ABI. No compiled glue: the
loader is `ctypes`, the values cross through `rakupp.h`/`rakupp_ext.h`, and
the grammar logic lives in a small Raku shim (`rakulang/grammar_shim.raku`)
that the binding evaluates into its interpreter at startup.

The package is named for the language, in the Raku community's own
disambiguated spelling (`raku` is an unrelated package on PyPI and npm;
`rakulang` is the name in both, and on crates.io, for the bindings to come).
The engine underneath is Raku++ — `rakupp` the binary, `librakupp` the
library. `import rakulang as raku` if you like the short spelling.

This is GRAMMAR-PLAN G0: the primitive layer, deliberately without a Python
class layer on top. The grammar stays a `.raku` file.

```python
import rakulang

log = rakulang.Grammar.from_file("log.raku", name="Log", actions="LogActions")

m = log.parse(text)                    # a handle, not data; None if no match
for line in m["line"]:                 # lazy: one engine call per leaf
    print(line["ip"].str(), line["status"].int())
print(m["line"][0]["size"].made)       # computed by LogActions, inside the parse

everything = m.tree()                  # eager, opt-in: costs ~1.4x more than
                                       # the parse itself on the gate corpus
```

## Installing and finding the library

The package is pip-installable (`pip install -e bindings/python` from a
checkout — a published wheel is release wiring, not done yet), after which
plain `import rakulang` works with no path fiddling.

Usually there is nothing to configure for the library either: if `rakupp` is
on PATH, the loader finds `librakupp` from it — an installed layout's or
Homebrew keg's sibling `lib/`, or the same build directory the binary sits
in. Overrides, in order: an explicit path
(`rakulang.interpreter("/path/to/librakupp.dylib")`), `RAKUPP_LIB` (the file),
`RAKUPP_HOME` (an install prefix with `lib/`), then the system loader path.
On ELF platforms the library is loaded `RTLD_GLOBAL` so Raku extensions
dlopen'ed later can resolve `rk_*` — that is a requirement from ABI-PLAN A3,
not a preference.

## The API, in five sentences

`Grammar.from_file(path, name=..., actions=...)` compiles a grammar (cached:
identical source compiles once; each *named* compile is isolated in its own
wrapper package, so recompiling an edited grammar under the same name works
and earlier handles keep the body they were compiled from — without `name`
there is no wrapper, and a same-name recompile raises the engine's
X::Redeclaration); `name` is the grammar's name in the file and may be
omitted only when the grammar declaration is the file's last statement;
`actions` names an actions class in the same file, and every parse then runs
a fresh instance of it. `parse(text, rule=...)` anchors to the whole input
and returns a `Match` or `None`. Indexing a match — `m["entry"]`,
`m["entry"][3]["key"]` — builds a lazy path; nothing crosses the boundary
until a terminal: `.str()`, `.int()`, `.num()`, `.made`, `bool()`, `len()`,
iteration, `.tree()`, or `.match()` (that last one returns an independent
rooted `Match`). A failed terminal on a missing capture raises `RakuError`
(`bool()`/`len()` answer `False`/`0` instead — that is how you probe).
Matches hold rooted values in the interpreter: `close()` them, use a `with`
block, or let the GC do it.

One interpreter per process, created on first use; one host thread may talk
to it at a time (Raku code inside it threads freely).

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

## Tests

`build/rakupp tools/grammar-smoke.raku <build-dir>` — the shim's contract
under plain rakupp, then this binding driving the same grammar and corpus as
the Raku driver, byte-compared. Runs in CI on every push.
