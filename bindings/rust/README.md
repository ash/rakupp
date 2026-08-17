# rakulang — Raku from Rust

A small crate over `librakupp`'s C ABI: hand-declared `extern "C"` bindings
plus a `build.rs` that finds the library. Values cross through
[`rakupp.h`](../../include/rakupp/rakupp.h), and the grammar logic lives in a
Raku shim that ships inside the library itself.

Rust is the host where lifetime costs nothing: a `Match` owns its rooted
engine value and `Drop` releases it, and a `Node` borrows its `Match`, so a
path cannot outlive the match it walks.

## 1. What you need

- **Rust.** Install via [rustup.rs](https://rustup.rs) if `cargo --version`
  says nothing.
- **`librakupp`.** From the repo root:

  ```bash
  cmake -B build -DCMAKE_BUILD_TYPE=Release -DRAKUPP_BUILD_SHARED=ON
  cmake --build build -j
  ```

  A build directory configured without `-DRAKUPP_BUILD_SHARED=ON` is
  static-only and this crate cannot use it.

## 2. Install

In your `Cargo.toml`:

```toml
[dependencies]
rakulang = { path = "/path/to/raku++/bindings/rust" }
```

Build and run with `RAKUPP_LIB_DIR` pointing at your build directory:

```bash
RAKUPP_LIB_DIR=/path/to/raku++/build cargo run
```

`RAKUPP_LIB_DIR` tells `build.rs` where `librakupp` lives, and it also
becomes the runtime search path. Against a system-installed `librakupp` (from
`cmake --install`) you can omit it.

## 3. Two minutes

Run both examples from the repo root:

```bash
RAKUPP_LIB_DIR=$PWD/build cargo run --manifest-path bindings/rust/Cargo.toml --example calc
```
```bash
RAKUPP_LIB_DIR=$PWD/build cargo run --manifest-path bindings/rust/Cargo.toml --example shopping
```

`calc` ([examples/calc.rs](examples/calc.rs)) prints:

```
2 + 2 = 4
area(3, 4) = 12
primes below 30: 2 3 5 7 11 13 17 19 23 29
stats: count=8 sum=31 mean=3.88 max=9
greet: Hello, Ada! You are 36.
30! = 265252859812191058636308480000000
died: division by zero
```

`shopping` ([examples/shopping.rs](examples/shopping.rs)) prints:

```
3 items
milk x 2
bread x 1
eggs x 12
total, computed in Raku: 15
as plain Rust data: Map({"item": List([Map({"name": Str("milk"), "qty": Str("2")}), Map({"name": Str("bread"), "qty": Str("1")}), Map({"name": Str("eggs"), "qty": Str("12")})])})
line 2 column 7 while trying <qty>
```

If you see both, the binding works.

## 4. Running Raku

```rust
use rakulang::{call, eval, Tree};

eval("my $x = 41")?;
eval("$x + 1")?;                       // Tree::Int(42) — eval keeps state

eval(&std::fs::read_to_string("calc.raku")?)?;   // loading subs is an eval

call("area", &[3.into(), 4.into()])?;            // Tree::Int(12)
call("stats", &[vec![Tree::Int(3), Tree::Int(1)].into()])?;

rakulang::can("area");                 // true
rakulang::version();                   // "3.14.0"
```

`eval` returns the last statement's value; `call` looks the routine up in the
mainline scope, so anything an earlier `eval` declared is callable.

`Tree` is **both** the argument type and the result type, which is what makes
the two directions read alike. `From` is implemented for `i32`, `i64`, `f64`,
`bool`, `&str`, `String`, `Vec<Tree>` and `BTreeMap<String, Tree>`, so call
sites stay short: `&[3.into(), "two".into()]`.

## 5. Parsing with grammars

```rust
use rakulang::Grammar;

let g = Grammar::from_file("log.raku", "Log", "LogActions")?;

if let Some(m) = g.parse(&text, "")? {
    let lines = m.get("line");
    for i in 0..lines.len()? {          // lazy: one engine call per leaf
        println!("{}", lines.at(i).get("ip").str()?);
    }
    println!("{:?}", lines.at(0).get("size").made()?);   // computed by actions
    let everything = m.tree()?;         // eager, opt-in (~1.4× the parse)
}   // m drops here — the rooted engine value releases itself
```

`from_file(path, name, actions)` compiles and caches: identical source
compiles once, and each *named* compile is isolated, so recompiling an edited
grammar never rebinds an earlier `Grammar`'s body. `name` may be `""` only
when the grammar declaration is the file's last statement.

`parse` anchors to the whole input; the `?` handles call errors and the
`Option` is the match. `""` means "the grammar's default rule, TOP" — pass
e.g. `"item"` to parse a fragment with that rule. `parse_strict` diagnoses a
non-match instead of returning `None`. `get` and `at` build a lazy path;
nothing crosses the boundary until `str()?`, `int()?`, `num()?`, `truthy()?`,
`len()?`, `tree()?` or `made()?`.

## 6. Values

Raku `Int` → `Tree::Int(i64)`, `Num`/`Rat` → `Tree::Num(f64)`, `Str` →
`Tree::Str`, `List` → `Tree::List`, `Hash` → `Tree::Map` (a `BTreeMap`, so
iteration is key-sorted and deterministic), `True`/`False` → `Tree::Bool`,
`Any` → `Tree::Null`. The same rules run in reverse for arguments.

An integer wider than 64 bits arrives as `Tree::Str` holding the digits. In a
`tree()`, a match node with no sub-captures becomes its matched *text*, so
`qty` is `Tree::Str("2")`; use `.int()?` on the node, or an actions class,
for numbers.

## 7. Errors

`Error::Raku(String)` is a Raku `die` crossing the boundary.
`Error::Parse { line, col, rule, pos }` is the diagnosed non-match:

```rust
if let Err(Error::Parse { line, col, rule, .. }) = g.parse_strict(&text, "") {
    println!("line {} column {} while trying <{}>", line, col, rule);
}
```

## 8. Lifetime and threading

One interpreter per process, created on first use. `Grammar` and `Match` are
deliberately `!Send` — one thread talks to the engine (Raku code inside it
threads freely).

Lifetime is the part Rust gets for free. A `Match` owns a rooted engine value
and `Drop` unroots it; a `Node` borrows its `Match`, so the compiler rejects
a path that would outlive the match it walks. Values from `eval` and `call`
are plain owned `Tree`s and need nothing.

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

- **`ld: library 'rakupp' not found` while building** — `RAKUPP_LIB_DIR` is
  unset or wrong, or that build directory is static-only: rebuild with
  `-DRAKUPP_BUILD_SHARED=ON`.
- **`incompatible architecture`** — your Rust toolchain and the library
  disagree (`rustc -vV | grep host`). Either install the matching toolchain,
  or build the library for Rust's architecture:
  `cmake -B build-x64 -DCMAKE_OSX_ARCHITECTURES=x86_64 -DRAKUPP_BUILD_SHARED=ON ...`
  and point `RAKUPP_LIB_DIR` there.
- **`rk_new refused`** — something already created an interpreter in this
  process. The crate keeps one; do not make another.
