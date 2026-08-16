# rakulang — Raku grammars for Go

This package lets a Go program parse text with a Raku grammar. The grammar
stays a plain `.raku` file; the parsing happens in an embedded Raku++
interpreter (`librakupp`, reached through cgo); the package only moves
values across. This guide assumes no prior Go beyond having it installed.

## What you need

- **Go** (1.17+) and a **C compiler** — cgo needs one; on macOS
  `xcode-select --install` provides it, on Linux install gcc or clang.
- **The shared library** — from the repo root:

  ```bash
  cmake -B build -DCMAKE_BUILD_TYPE=Release -DRAKUPP_BUILD_SHARED=ON
  cmake --build build -j
  ```

  This gives you `build/librakupp.dylib` (macOS) / `build/librakupp.so`
  (Linux). Any build directory works; the commands below say `build`.

## Run the example

From the repo root:

```bash
cd bindings/go
CGO_LDFLAGS="-L$PWD/../../build -Wl,-rpath,$PWD/../../build" go run ./examples/shopping
```

`CGO_LDFLAGS` does two jobs: `-L` lets the build find the library, and
`-Wl,-rpath` bakes its directory into the binary so it is also found at run
time. Expected output (possibly after some harmless linker warnings):

```
3 items
milk x 2
bread x 1
eggs x 12
total, computed in Raku: 15
as plain Go data: map[item:[map[name:milk qty:2] map[name:bread qty:1] map[name:eggs qty:12]]]
line 2 column 7 while trying <qty>
```

If you see this, the binding works.

## The example, explained

The source is [examples/shopping/main.go](examples/shopping/main.go); the
grammar it loads is [../examples/shopping.raku](../examples/shopping.raku).
The whole API in one pass:

```go
import "rakulang"

// Compile the grammar file. "Shopping" is the grammar's name INSIDE the
// file; "ShoppingActions" names an actions class in the same file (pass ""
// for none).
g, err := rakulang.FromFile("shopping.raku", "Shopping", "ShoppingActions")

// Parse. A non-match is the sentinel error rakulang.ErrNoMatch (check with
// errors.Is); any other error is a real failure. The whole input must
// match. To parse a fragment with one rule: g.Parse(text, "item").
m, err := g.Parse("milk=2\nbread = 1  eggs=12\n")
defer m.Close()   // a Match holds an engine value — freeing is YOUR job

// Walk the match lazily. Get names a capture; At indexes a repeated one.
// Nothing crosses the engine boundary until a terminal call like
// Str/Int/Len — those cost one engine call each.
items := m.Get("item")
for i := 0; i < items.Len(); i++ {
    item := items.At(i)
    fmt.Println(item.Get("name").Str(), item.Get("qty").Int())
}

// What the Raku actions class computed, as native data. ShoppingActions'
// TOP method did `make ...sum`, so this is int64(15).
fmt.Println(m.Made())

// Or convert everything below a node at once (~1.4x the parse's own cost):
// nested map[string]interface{} / []interface{} / string values.
all := m.Tree()

// A failed parse, diagnosed: ParseStrict returns *ParseError with the
// line, column, and the deepest rule the engine was trying there.
_, err = g.ParseStrict("milk=2\nbread=x\n")
var pe *rakulang.ParseError
if errors.As(err, &pe) {
    fmt.Println(pe.Line, pe.Col, pe.Rule)
}
```

## How data crosses

Into Raku: the text you parse (and the grammar source). Out of Raku, two
channels:

- **Lazy leaf reads** — `.Str()` → `string`, `.Int()` → `int64`, `.Num()` →
  `float64`. Cheap and precise; use these when you want a few fields.
- **`Tree()` / `Made()`** — everything at once, as `interface{}` values:
  `nil`, `bool`, `int64`, `float64`, `string`, `[]interface{}`,
  `map[string]interface{}`. In a `Tree()`, a node with no sub-captures
  becomes its matched text (`"2"`, not `2`); named captures become map keys;
  repeated captures become slices. `Made()` carries whatever the actions
  class `make`d — the general-purpose way to have Raku *compute* something
  and hand the result to Go as plain data.

Probing: `.Truthy()` answers whether anything matched at a path — reading a
missing capture with `Str()`/`Int()` panics (deliberately, per Go's `Must*`
idiom: it is a programmer error, so probe first). `.Len()` is the list
length — 1 for a plain node, 0 for a missing one.

## Rules to remember

- **`defer m.Close()`** after every successful parse. There is deliberately
  no finalizer — a Match holds a rooted engine value and freeing it is
  explicit, like a file handle.
- **One interpreter per process**, created on first use, and **one goroutine
  at a time** may use Grammars and Matches — they are not safe to share
  across goroutines. (Raku code inside the interpreter threads freely.)

## Using it in your own project

The package must see the checkout (its cgo header path points into
`../../src`). In your project's `go.mod`:

```
require rakulang v0.0.0
replace rakulang => /path/to/raku++/bindings/go
```

Then build/run with the same `CGO_LDFLAGS` as above, pointing at your build
directory. Against a system-installed librakupp you can drop the flags.

## Testing it properly

The full gate — same grammar, same 2000-line corpus, this package's output
byte-compared against plain `rakupp`'s — runs from the repo root:

```bash
build/rakupp tools/grammar-smoke.raku
```

Look for `ok - Go output is byte-identical to rakupp's`. The Go leg skips
(loudly) if go is missing or its architecture cannot load the library — see
below.

## When things go wrong

- **`library 'rakupp' not found` at link time** — `CGO_LDFLAGS` unset or its
  `-L` points at a directory with no shared library. A plain build directory
  is static-only: rebuild with `-DRAKUPP_BUILD_SHARED=ON`.
- **`dyld: Library not loaded` at run time** — the binary was linked without
  the `-Wl,-rpath,...` part of `CGO_LDFLAGS`; add it (or set
  `DYLD_LIBRARY_PATH`/`LD_LIBRARY_PATH`).
- **`incompatible architecture`** — your Go toolchain and the library
  disagree (common on Apple Silicon with an x86_64 Go; check `go version` —
  `darwin/amd64` on an M-series machine is the wrong one). Either install
  arm64 Go, or build an x86_64 library to match:
  `cmake -B build-x64 -DCMAKE_OSX_ARCHITECTURES=x86_64 -DRAKUPP_BUILD_SHARED=ON ...`
- **`could not determine kind of name for C.rk_...`** — the package cannot
  find `rakupp.h`; it expects to live inside the checkout (the `replace`
  directive above keeps that true).
- **panic while reading a value** — you read `Str()`/`Int()` on a capture
  that did not match. Probe with `Truthy()` first.
- **`rk_new refused: an interpreter is already live`** — something else in
  this process already embeds Raku++; the engine allows one interpreter per
  process.
