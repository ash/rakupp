# rakulang — Raku from Go

A single-file cgo package over `librakupp`'s C ABI. cgo does the loading and
the value marshalling; values cross through
[`rakupp.h`](../../include/rakupp/rakupp.h), and the grammar logic lives in a
Raku shim that ships inside the library itself.

## 1. What you need

- **Go 1.18+ with cgo**, which needs a C compiler. On macOS
  `xcode-select --install` provides one; on Linux install gcc or clang.
- **`librakupp`.** From the repo root:

  ```bash
  cmake -B build -DCMAKE_BUILD_TYPE=Release -DRAKUPP_BUILD_SHARED=ON
  cmake --build build -j
  ```

  A build directory configured without `-DRAKUPP_BUILD_SHARED=ON` is
  static-only and this package cannot use it.

## 2. Install

Go links rather than searching at runtime, so the library path is a linker
flag, not an environment variable. The package must see the checkout — its
cgo header path points into `../../include`. In your project's `go.mod`:

```
require rakulang v0.0.0
replace rakulang => /path/to/raku++/bindings/go
```

Then build and run with `CGO_LDFLAGS` pointing at your build directory:

```bash
CGO_LDFLAGS="-L/path/to/raku++/build -Wl,-rpath,/path/to/raku++/build" go run .
```

Against a system-installed `librakupp` you can drop the flags.

## 3. Two minutes

Run both examples:

```bash
cd bindings/go && CGO_LDFLAGS="-L$PWD/../../build -Wl,-rpath,$PWD/../../build" go run ./examples/calc
```
```bash
cd bindings/go && CGO_LDFLAGS="-L$PWD/../../build -Wl,-rpath,$PWD/../../build" go run ./examples/shopping
```

`calc` ([examples/calc/main.go](examples/calc/main.go)) prints:

```
2 + 2 = 4
area(3, 4) = 12
primes below 30: 2 3 5 7 11 13 17 19 23 29
stats: count=8 sum=31 mean=3.88 max=9
greet: Hello, Ada! You are 36.
30! = 265252859812191058636308480000000
died: division by zero
```

`shopping` ([examples/shopping/main.go](examples/shopping/main.go)) prints:

```
3 items
milk x 2
bread x 1
eggs x 12
total, computed in Raku: 15
as plain Go data: map[item:[map[name:milk qty:2] map[name:bread qty:1] map[name:eggs qty:12]]]
line 2 column 7 while trying <qty>
```

If you see both, the binding works.

## 4. Running Raku

```go
import "rakulang"

rakulang.Eval("my $x = 41")
v, _ := rakulang.Eval("$x + 1")        // 42 — Eval keeps state, like the REPL

src, _ := os.ReadFile("calc.raku")
rakulang.Eval(string(src))             // loading a file of subs is an Eval

area, _ := rakulang.Call("area", 3, 4)             // int64(12)
s, _ := rakulang.Call("stats", []interface{}{3, 1, 4})
greet, _ := rakulang.Call("greet", map[string]interface{}{"name": "Ada"})

rakulang.Can("area")                   // true
rakulang.Version()                     // "3.14.0"
```

`Eval` returns the last statement's value; `Call` looks the routine up in the
mainline scope, so anything an earlier `Eval` declared is callable. Arguments
convert automatically — `nil`, `bool`, `int`/`int32`/`int64`,
`float32`/`float64`, `string`, `[]interface{}`, `map[string]interface{}`.
Anything else returns a `*RakuError`.

Both return `interface{}`; type-assert it to the shape you expect
(`v.(int64)`, `v.([]interface{})`, `v.(map[string]interface{})`).

## 5. Parsing with grammars

```go
g, err := rakulang.FromFile("log.raku", "Log", "LogActions")

m, err := g.Parse(text)                // errors.Is(err, ErrNoMatch) if no match
defer m.Close()                        // required — see §8

lines := m.Get("line")
for i := 0; i < lines.Len(); i++ {      // lazy: one engine call per leaf
    line := lines.At(i)
    fmt.Println(line.Get("ip").Str(), line.Get("status").Int())
}
fmt.Println(lines.At(0).Get("size").Made())   // computed by actions

everything := m.Tree()                 // eager, opt-in (~1.4× the parse)
```

`FromFile(path, name, actions)` compiles and caches: identical source
compiles once, and each *named* compile is isolated, so recompiling an edited
grammar never rebinds an earlier `Grammar`'s body. `name` may be empty only
when the grammar declaration is the file's last statement.

`Parse` anchors to the whole input; a non-match is the sentinel error
`rakulang.ErrNoMatch` (check with `errors.Is`) and any other error is a real
failure. `ParseStrict` diagnoses the non-match instead. Pass a rule name —
`g.Parse(text, "item")` — to parse a fragment with one rule. `Get` and `At`
build a lazy path; nothing crosses the boundary until `Str()`, `Int()`,
`Num()`, `Truthy()`, `Len()`, `Tree()` or `Made()`.

`SortedKeys` is provided for printing a `map[string]interface{}` in a
deterministic order.

## 6. Values

Raku `Int` → `int64`, `Num`/`Rat` → `float64`, `Str` → `string`, `List` →
`[]interface{}`, `Hash` → `map[string]interface{}`, `True`/`False` → `bool`,
`Any` → `nil`. The same rules run in reverse for arguments.

An integer wider than 64 bits arrives as a string of digits. In a `Tree()`, a
match node with no sub-captures becomes its matched *text*, so `qty` is the
string `"2"`; use `.Int()` on the node, or an actions class, for numbers.

## 7. Errors

`*RakuError` is a Raku `die` crossing the boundary. `*ParseError` is the
diagnosed non-match, carrying `.Line`, `.Col`, `.Rule` and `.Pos`:

```go
_, err = g.ParseStrict(text)
var pe *rakulang.ParseError
if errors.As(err, &pe) {
    fmt.Printf("line %d column %d while trying <%s>\n", pe.Line, pe.Col, pe.Rule)
}
```

## 8. Lifetime and threading

One interpreter per process, created on first use. **One goroutine at a
time** may use Grammars and Matches — they are not safe to share across
goroutines (Raku code inside the interpreter threads freely).

**Call `Close()` on a Match when you are done with it**, usually with
`defer`. It holds a rooted value inside the interpreter and Go's GC will not
release that for you. Values from `Eval` and `Call` are already plain Go data
and need nothing.

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

- **`ld: library not found for -lrakupp`** — `CGO_LDFLAGS` does not point at
  a directory holding the library, or that build directory is static-only:
  rebuild with `-DRAKUPP_BUILD_SHARED=ON`.
- **`incompatible architecture`** — your Go toolchain and the library
  disagree (`go env GOARCH`; `darwin/amd64` on an M-series machine is the
  wrong one). Either install a matching Go, or build the library for Go's
  architecture:
  `cmake -B build-x64 -DCMAKE_OSX_ARCHITECTURES=x86_64 -DRAKUPP_BUILD_SHARED=ON ...`
- **`rk_new refused`** — something already created an interpreter in this
  process. The package keeps one; do not make another.
