# Language bindings — Raku grammars from Python, JavaScript, Go, Rust, and C++

`librakupp` is the Raku++ interpreter as a shared library, behind a small
C ABI. Each folder here is a thin binding over it: your Python/JS/Go/Rust
program hands the engine a grammar (a plain `.raku` file) and some text, and
reads the parse results back as native values. Nothing is re-implemented per
language — the parsing is exactly what plain `rakupp` does, and a test gate
byte-compares the two to keep it that way.

Per-language guides, each with a runnable example and troubleshooting:

| language | guide | binds via | you free a Match by |
|---|---|---|---|
| Python | [python/README.md](python/README.md) | ctypes | GC, `close()`, or `with` |
| JavaScript | [js/README.md](js/README.md) | `bun:ffi` (Bun only) | calling `close()` |
| Go | [go/README.md](go/README.md) | cgo | calling `Close()` (use `defer`) |
| Rust | [rust/README.md](rust/README.md) | extern + build.rs | nothing — `Drop` does it |
| C++ | [../src/grammar.hpp](../src/grammar.hpp) + below | linked, header-only | nothing — RAII does it |

## One-time setup: build the shared library

All bindings load the same library. From the repo root:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DRAKUPP_BUILD_SHARED=ON
cmake --build build -j
```

That produces `build/rakupp` (the CLI) and `build/librakupp.dylib` (macOS —
`librakupp.so` on Linux). A build directory without `-DRAKUPP_BUILD_SHARED=ON`
is static-only and no binding can use it.

## The ten-minute tour

[examples/](examples/) holds one small grammar, `shopping.raku`, and the same
program written in every language. The grammar parses a shopping list and its
actions class sums the quantities *inside the parse*. TOP and item are `rule`s,
so they are `:sigspace` — each space in the pattern matches `<.ws>`, and the
list may run one item per line or several to a line, with or without spaces
around the `=`:

```raku
grammar Shopping {
    rule  TOP  { <item>+ }
    rule  item { <name> '=' <qty> }
    token name { \w+ }
    token qty  { \d+ }
}

class ShoppingActions {
    method item($/) { make $<qty>.Int }
    method TOP($/)  { make $<item>.map(*.made).sum }
}
```

Every version prints the same thing:

```
3 items
milk x 2
bread x 1
eggs x 12
total, computed in Raku: 15
as plain <language> data: {item: [{name: milk, qty: 2}, ...]}
line 2 column 7 while trying <qty>
```

Run commands (from the repo root; each is explained in its guide):

```bash
RAKUPP_LIB=$PWD/build/librakupp.dylib python3 bindings/examples/shopping.py
```
```bash
RAKUPP_LIB=$PWD/build/librakupp.dylib bun bindings/examples/shopping.mjs
```
```bash
cd bindings/go && CGO_LDFLAGS="-L$PWD/../../build -Wl,-rpath,$PWD/../../build" go run ./examples/shopping
```
```bash
RAKUPP_LIB_DIR=$PWD/build cargo run --manifest-path bindings/rust/Cargo.toml --example shopping
```
```bash
c++ -std=c++17 -Isrc bindings/examples/shopping.cpp build/librakupp.dylib \
    -Wl,-rpath,$PWD/build -o shopping && ./shopping
```

## The shape every binding shares

The API is the same five ideas everywhere; only the spelling is local:

1. **Compile** a grammar from a file, naming the grammar (and optionally an
   actions class) inside it:
   `Grammar.from_file("g.raku", name="G", actions="GActions")`.
2. **Parse** text. A match returns a `Match`; a non-match returns the
   language's "nothing" (`None` / `null` / `ErrNoMatch` / `Ok(None)` /
   `std::nullopt`). The whole input must match. Pass a rule name to parse a
   fragment with one rule instead of the whole grammar.
3. **Walk lazily.** Indexing a match (`m["item"][0]["qty"]`) costs nothing;
   the engine is called once per *leaf* you read: `.str()`, `.int()`,
   `.num()`, `.made`, truthiness, length.
4. **Or convert eagerly.** `tree()` turns everything below a node into plain
   native data — dicts/lists in Python, objects/arrays in JS, maps/slices in
   Go, a `Tree` enum in Rust, a `Tree` variant in C++. It costs ~1.4× the
   parse itself, so prefer the lazy walk when you want less than about half
   of the match.
5. **Read what Raku computed.** An actions class runs inside the parse; each
   node's `made` value crosses as native data. This is the general-purpose
   data channel *out* of Raku: anything the actions `make` — numbers,
   strings, lists, maps — arrives as the host's own types.

Diagnosed failures work everywhere too: the strict parse variant reports the
line, column, and deepest rule the engine reached, in the host's own error
type.

Two rules hold in every language:

- **One interpreter per process**, created on first use. One host thread may
  talk to it at a time (Raku code inside it threads freely).
- **A Match owns an engine value.** How it is freed is per-language — see
  the table above.

## How values cross the boundary

| engine-side | Python | JS | Go | Rust | C++ |
|---|---|---|---|---|---|
| matched text | `str` | `string` | `string` | `String` | `std::string` |
| `.int()` | `int` | `number` | `int64` | `i64` | `long long` |
| `.num()` | `float` | `number` | `float64` | `f64` | `double` |
| `tree()` list | `list` | `Array` | `[]interface{}` | `Tree::List` | `Tree` (vector) |
| `tree()` map | `dict` | `Object` | `map[string]interface{}` | `Tree::Map` | `Tree` (map) |
| no value | `None` | `null` | `nil` | `Tree::Null` | `nullptr` alt |

In a `tree()`, a node with no sub-captures becomes its matched *text* (so
`qty` arrives as the string `"2"`); use `.int()` on the node, or an actions
class, when you want numbers.

## Testing — does the binding actually work?

The standing gate is one command, from the repo root:

```bash
build/rakupp tools/grammar-smoke.raku
```

It checks the Raku shim's own contract, then drives the same grammar and the
same 2000-line corpus through every binding whose toolchain it finds —
Python, C++, JS, Go, Rust — and byte-compares each output against plain
`rakupp`'s. Legs whose toolchain is missing (or is an x86_64 build that
cannot load an arm64 library) skip loudly. It runs in CI on every push.

For a one-language check, run that language's example from the tour above —
each guide lists the expected output and the common failure modes.

## C++ in brief

C++ has no folder here because the binding is one header that ships with the
engine: [src/grammar.hpp](../src/grammar.hpp) (installed as
`<rakupp/grammar.hpp>`). Unlike the other hosts it *links* against the
library instead of dlopen'ing it — see the compile line in the tour, and
[examples/shopping.cpp](examples/shopping.cpp) for the full program. Lifetime
is RAII: a `Match`'s destructor releases its engine value.

## Design notes

One design, five hosts (docs/dev/plans/GRAMMAR-PLAN.md): the grammar stays a
`.raku` file, the Raku shim ships inside `librakupp` (`rk_grammar_shim`), and
every binding is just invocation, results and lifetime in its host's idiom.
The package name is `rakulang` in every registry (PyPI, npm, crates.io — the
Raku community's disambiguated spelling; `raku` is taken by unrelated
packages). Python is the reference binding — its README carries the measured
overhead table. The Python platform wheel (librakupp bundled, works with no
rakupp installed) is built by `tools/build-wheel.sh` on every release.
Publishing to the registries is a separate, manual decision.
