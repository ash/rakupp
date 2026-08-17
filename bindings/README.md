# Raku from Python, JavaScript, Go, Rust, and C++

`librakupp` is the Raku++ interpreter as a shared library behind a small C
ABI. Each binding here is a thin layer over it, so your program can do two
things:

- **Run Raku.** Evaluate source, call Raku routines with your own values,
  get results back as your own types.
- **Parse with Raku grammars.** Hand the engine a grammar (a plain `.raku`
  file) and some text, and walk the match.

Nothing is re-implemented per language. The Raku is exactly what plain
`rakupp` runs, and a test gate byte-compares the two to keep it that way.

| language | guide | binds via | package name |
|---|---|---|---|
| Python | [python/README.md](python/README.md) | `ctypes` | `rakulang` (PyPI) |
| JavaScript | [js/README.md](js/README.md) | `bun:ffi` (Bun only) | `rakulang` (npm) |
| Go | [go/README.md](go/README.md) | cgo | `rakulang` |
| Rust | [rust/README.md](rust/README.md) | `extern` + `build.rs` | `rakulang` (crates.io) |
| C++ | [cpp/README.md](cpp/README.md) | linked, header-only | `<rakupp/raku.hpp>` |

Every guide has the same nine sections in the same order, so you can read one
and skim the rest.

## 1. Build the library

All five bindings load the same `librakupp`. From the repo root:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DRAKUPP_BUILD_SHARED=ON
cmake --build build -j
```

That produces `build/rakupp` (the CLI) and `build/librakupp.dylib` on macOS —
`librakupp.so` on Linux, `rakupp.dll` on Windows. **A build directory
configured without `-DRAKUPP_BUILD_SHARED=ON` is static-only and no binding
can use it**; that is the single most common first failure.

## 2. Run the examples

Two examples, each written in all five languages. They are the fastest way to
confirm a binding works on your machine, and they are what the test gate runs.

| example | what it shows | the Raku it runs |
|---|---|---|
| **calc** | running Raku: eval source, call subs with host values, read results back, catch a die | [examples/calc.raku](examples/calc.raku) |
| **shopping** | parsing: compile a grammar, walk a match lazily, read what Raku actions computed, diagnose a failure | [examples/shopping.raku](examples/shopping.raku) |

Each example is in two halves. [examples/](examples/) holds what the five
languages share — the `.raku` they run and the output they must produce —
and each language's own program lives in `<lang>/examples/`, where its
toolchain expects it.

Run them from the repo root (`.so` for `.dylib` on Linux; `<example>` is
`calc` or `shopping`):

```bash
RAKUPP_LIB=$PWD/build/librakupp.dylib python3 bindings/python/examples/<example>.py
```
```bash
RAKUPP_LIB=$PWD/build/librakupp.dylib bun bindings/js/examples/<example>.mjs
```
```bash
cd bindings/go && CGO_LDFLAGS="-L$PWD/../../build -Wl,-rpath,$PWD/../../build" go run ./examples/<example>
```
```bash
RAKUPP_LIB_DIR=$PWD/build cargo run --manifest-path bindings/rust/Cargo.toml --example <example>
```
```bash
c++ -std=c++17 -Iinclude bindings/cpp/examples/<example>.cpp build/librakupp.dylib -Wl,-rpath,$PWD/build -o ex && ./ex
```

`calc` prints exactly the same seven lines in every language — that is the
point of it:

```
2 + 2 = 4
area(3, 4) = 12
primes below 30: 2 3 5 7 11 13 17 19 23 29
stats: count=8 sum=31 mean=3.88 max=9
greet: Hello, Ada! You are 36.
30! = 265252859812191058636308480000000
died: division by zero
```

`shopping` deliberately differs in one line per language, where it prints the
whole match as that language's own native data. All ten outputs are recorded
in [examples/expected/](examples/expected).

## 3. Running Raku — the API

One interpreter, reached the way each language reaches a process-wide
resource. Every binding spells the same two verbs:

| | Python | JavaScript | Go | Rust | C++ |
|---|---|---|---|---|---|
| the interpreter | `rakulang.interpreter()` | `interpreter()` | (implicit) | (implicit) | (implicit) |
| evaluate source | `.eval(src)` | `.eval(src)` | `rakulang.Eval(src)` | `rakulang::eval(src)` | `rakupp::eval(src)` |
| call a routine | `.call("f", 1, 2)` | `.call("f", 1, 2)` | `rakulang.Call("f", 1, 2)` | `rakulang::call("f", &[1.into(), 2.into()])` | `rakupp::call("f", {1, 2})` |
| is it there? | `.can("f")` | `.can("f")` | `rakulang.Can("f")` | `rakulang::can("f")` | `rakupp::can("f")` |
| engine version | `.version` | `.version` | `rakulang.Version()` | `rakulang::version()` | `rakupp::version()` |

**`eval` keeps state.** It runs in the interpreter's mainline scope and
leaves it there, exactly as the REPL does — `eval("my $x = 41")` then
`eval("$x + 1")` gives 42. That is why loading a file of subs is just an
`eval` of its text, and `call` finds them afterwards. All five `calc`
examples are built on that one fact.

## 4. Parsing with grammars — the API

| | Python | JavaScript | Go | Rust | C++ |
|---|---|---|---|---|---|
| compile | `Grammar.from_file(p, name=, actions=)` | `Grammar.fromFile(p, {name, actions})` | `rakulang.FromFile(p, n, a)` | `Grammar::from_file(p, n, a)` | `Grammar::from_file(p, n, a)` |
| parse | `.parse(text)` | `.parse(text)` | `.Parse(text)` | `.parse(text, "")` | `.parse(text)` |
| no match is | `None` | `null` | `ErrNoMatch` | `Ok(None)` | `std::nullopt` |
| diagnosed instead | `strict=True` | `{strict: true}` | `.ParseStrict` | `.parse_strict` | `.parse_or_throw` |
| walk | `m["item"][0]["qty"]` | `m.get("item").at(0)` | `m.Get("item").At(0)` | `m.get("item").at(0)` | `m["item"][0]` |
| leaf | `.str() .int() .num()` | `.str() .int() .num()` | `.Str() .Int() .Num()` | `.str()? .int()? .num()?` | `.str() .int_() .num()` |
| what actions made | `.made` | `.made()` | `.Made()` | `.made()?` | `.made()` |
| everything, eagerly | `.tree()` | `.tree()` | `.Tree()` | `.tree()?` | `.tree()` |

The grammar always stays a `.raku` file. `name` is the grammar's name inside
it, and may be omitted only when the grammar declaration is the file's last
statement; `actions` names an actions class in the same file, and each parse
runs a fresh instance of it.

**Walking is lazy.** Indexing a match costs nothing; the engine is called
once per *leaf* you actually read. `tree()` is the eager opposite — it
converts everything below a node at about 1.4× the cost of the parse, so
prefer the lazy walk when you want less than roughly half of a match.

## 5. How values cross the boundary

The same rules apply to arguments you send and results you get back.

| Raku | Python | JavaScript | Go | Rust | C++ |
|---|---|---|---|---|---|
| `Int` | `int` | `number` | `int64` | `Tree::Int(i64)` | `Tree` / `int_()` |
| `Num`, `Rat` | `float` | `number` | `float64` | `Tree::Num(f64)` | `Tree` / `num()` |
| `Str` | `str` | `string` | `string` | `Tree::Str` | `Tree` / `str()` |
| `List`, `Array` | `list` | `Array` | `[]interface{}` | `Tree::List` | `Tree` / `list()` |
| `Hash` | `dict` | `Object` | `map[string]interface{}` | `Tree::Map` | `Tree` / `map()` |
| `True` / `False` | `bool` | `boolean` | `bool` | `Tree::Bool` | `Tree` / `boolean()` |
| `Any` | `None` | `null` | `nil` | `Tree::Null` | `Tree::is_null()` |

Three things are worth knowing before they surprise you:

- **A `Rat` arrives as a float.** `1/3` crosses as `0.333…`. When the exact
  text matters, format it on the Raku side — `calc.raku` does exactly that
  for its `mean`, because a Rat printed by five languages is five different
  strings.
- **An integer wider than 64 bits arrives as a string of digits.** That is
  the honest conversion; `30!` in the `calc` example is the demonstration.
- **In a `tree()`, a match node with no sub-captures becomes its matched
  text.** So `qty` arrives as the string `"2"`, not the number 2. Use
  `.int()` on the node, or an actions class, when you want numbers.

## 6. Errors

A Raku `die` crosses as the host's own error type, carrying the message. A
diagnosed non-match adds the position and the deepest rule the engine
reached.

| | Python | JavaScript | Go | Rust | C++ |
|---|---|---|---|---|---|
| a die | `RakuError` | `RakuError` | `*RakuError` | `Error::Raku` | `rakupp::RakuError` |
| a diagnosed non-match | `ParseError` | `ParseError` | `*ParseError` | `Error::Parse` | `rakupp::ParseError` |
| fields on it | `.line .column .rule .pos` | `.line .column .rule .pos` | `.Line .Col .Rule .Pos` | `{line, col, rule, pos}` | `.line .col .rule .pos` |

Calling a routine with the **wrong number of arguments** is one of these
errors, not a silent wrong answer: `call("area", 3)` against
`sub area($w, $h)` raises, exactly as the same call written in Raku would.

## 7. Lifetime and threading

Two rules hold in every language.

**One interpreter per process**, created on first use. `rk_new` refuses a
second while one is live. One host thread may talk to it at a time — Raku
code *inside* it threads as it pleases.

**A Match owns an engine value** that must be released. How differs, and it
is the one place the bindings are not uniform, because each language's honest
answer is different:

| language | you free a Match by |
|---|---|
| Python | garbage collection, `close()`, or a `with` block |
| JavaScript | calling `close()` — there is no reliable GC hook |
| Go | calling `Close()`, usually with `defer` |
| Rust | nothing; `Drop` does it, and a `Node` borrows its `Match` |
| C++ | nothing; the destructor does it |

Values from `eval` and `call` need none of this — they are converted to
native data before they reach you.

## 8. Testing

Two commands, from the repo root. Run the first one first.

```bash
build/rakupp tools/bindings-smoke.raku
```

Runs every example in every language whose toolchain is present, and compares
each output against [examples/expected/](examples/expected). It is the
answer to "is my setup working?" and to "did I break a binding?", and it is
what keeps the outputs printed in these guides honest. Missing toolchains
skip loudly; after an intended change, re-record with `--record`.

```bash
build/rakupp tools/grammar-smoke.raku
```

The deep gate: the Raku shim's own contract under plain `rakupp`, then the
same grammar and a 2000-line corpus driven through every binding and
byte-compared against what plain `rakupp` produces. A host must get exactly
what `rakupp` gets, or the service is decoration. Both run in CI on every
push.

For the C ABI underneath — and for embedding without a language binding —
`build/rakupp tools/embed-smoke.raku` is the equivalent gate.

## 9. Where the files are

Every language directory has the same three things: a guide, the binding
itself, and an `examples/` holding its half of both examples.

```
bindings/
  README.md              this file
  examples/              the SHARED half — language-neutral
    calc.raku            the Raku the calc example runs
    shopping.raku        the grammar the shopping example parses with
    expected/            recorded outputs, checked by tools/bindings-smoke.raku
  python/  README.md  rakulang/{__init__,_abi}.py, grammar_shim.raku   examples/{calc,shopping}.py
  js/      README.md  rakulang.js                                      examples/{calc,shopping}.mjs
  go/      README.md  rakulang.go                                      examples/{calc,shopping}/main.go
  rust/    README.md  src/lib.rs, build.rs                             examples/{calc,shopping}.rs
  cpp/     README.md  (no source — the headers ship with the engine)   examples/{calc,shopping}.cpp
```

Go wants a directory per `main`, and Rust and Go both require `examples/` at
exactly that path for `go run ./examples/…` and `cargo run --example`; the
other three follow the same shape so all five read alike.

The C++ binding has no source of its own here: it is two headers that install
with the engine, [include/rakupp/raku.hpp](../include/rakupp/raku.hpp) and
[include/rakupp/grammar.hpp](../include/rakupp/grammar.hpp). See
[include/README.md](../include/README.md) for the public/internal split.

Underneath all five sits the C ABI itself,
[include/rakupp/rakupp.h](../include/rakupp/rakupp.h) — documented in
[docs/guide/EMBEDDING.md](../docs/guide/EMBEDDING.md) for hosts with no
binding here.

## Troubleshooting

**`librakupp not found` / `cannot open shared object file`.** The library is
not built, or not findable. Build it with `-DRAKUPP_BUILD_SHARED=ON`, then
point at it: every binding accepts `RAKUPP_LIB` (the file) or `RAKUPP_HOME`
(an install prefix with `lib/`), and finds it automatically when `rakupp` is
on PATH. C++ and Go link instead of searching, so they take a linker flag.

**`RAKUPP_LIB names …, which could not be loaded`.** A library you name is
used as given — the searching hosts never fall back to a different one, so
this is the error rather than some other build silently running. Read the
cause it quotes (usually the architecture mismatch below), or unset the
variable to let the loader search.

**`incompatible architecture`.** The toolchain and the library disagree — an
x86_64 `python3`/`bun`/`go` against an arm64 `librakupp`, or the reverse.
Build the library for the architecture your toolchain runs, or run the
toolchain under the other one. `tools/bindings-smoke.raku` detects this and
skips rather than failing.

**`rk_new refused: an interpreter is already live in this process`.** You
created a second interpreter. There is one per process; use the shared one.

**A grammar compiles but never matches.** The parse anchors to the *whole*
input. Pass a rule name to parse a fragment with one rule instead.

## Design notes

One design, five hosts
([docs/dev/plans/GRAMMAR-PLAN.md](../docs/dev/plans/GRAMMAR-PLAN.md)): the
grammar stays a `.raku` file, the Raku shim ships inside `librakupp`
(`rk_grammar_shim`), and every binding is invocation, results and lifetime in
its host's idiom. The package name is `rakulang` in every registry — the Raku
community's disambiguated spelling, since `raku` is taken by unrelated
packages. Python is the reference binding; its README carries the measured
overhead table. The Python platform wheel (librakupp bundled, works with no
rakupp installed) is built by `tools/build-wheel.sh` on every release.
Publishing to the registries is a separate, manual decision.
