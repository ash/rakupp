# rakulang — Raku grammars for Rust

This crate lets a Rust program parse text with a Raku grammar. The grammar
stays a plain `.raku` file; the parsing happens in an embedded Raku++
interpreter (`librakupp`); the crate only moves values across. You do not
need to know Raku's internals, and this guide assumes no prior Rust beyond
having it installed.

## What you need

- **Rust** — install via [rustup.rs](https://rustup.rs) if `cargo --version`
  says command not found. The crate has zero dependencies, so no network is
  needed to build it.
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
RAKUPP_LIB_DIR=$PWD/build cargo run --manifest-path bindings/rust/Cargo.toml --example shopping
```

`RAKUPP_LIB_DIR` tells the build script where `librakupp` lives; it also
embeds that path into the binary (rpath), so the program finds the library
at run time with no further setup. Expected output:

```
3 items
milk x 2
bread x 1
eggs x 12
total, computed in Raku: 15
as plain Rust data: Map({"item": List([Map({"name": Str("milk"), "qty": Str("2")}), ...])})
line 2 column 7 while trying <qty>
```

If you see this, the binding works.

## The example, explained

The source is [examples/shopping.rs](examples/shopping.rs); the grammar it
loads is [../examples/shopping.raku](../examples/shopping.raku). The whole
API in one pass:

```rust
use rakulang::{Error, Grammar, Tree};

// Compile the grammar file. "Shopping" is the grammar's name INSIDE the
// file; "ShoppingActions" names an actions class in the same file (pass ""
// for none). The ? bubbles errors up, Rust's idiom for exceptions.
let g = Grammar::from_file("shopping.raku", "Shopping", "ShoppingActions")?;

// Parse. Ok(None) means "valid call, but the text did not match" — hence
// the ? (call errors) followed by .expect/match (no-match). "" means
// "use the grammar's default rule, TOP"; pass e.g. "item" to parse a
// fragment with just that rule.
let m = g.parse("milk=2\nbread=1\neggs=12\n", "")?.expect("no match");

// Walk the match lazily. get("item") names a capture; at(i) indexes a
// repeated one. Nothing crosses the engine boundary until a terminal call
// like .str() / .int() / .len() — those cost one engine call each.
let items = m.get("item");
for i in 0..items.len()? {
    let item = items.at(i);
    println!("{} x {}", item.get("name").str()?, item.get("qty").int()?);
}

// What the Raku actions class computed, as native data. ShoppingActions'
// TOP method did `make ...sum`, so this is Tree::Int(15).
if let Tree::Int(total) = m.made()? {
    println!("total: {}", total);
}

// Or convert everything below a node at once (~1.4x the parse's own cost).
let all: Tree = m.tree()?;

// A failed parse, diagnosed: parse_strict returns Error::Parse with the
// line, column, and the deepest rule the engine was trying there.
if let Err(Error::Parse { line, col, rule, .. }) = g.parse_strict("milk=2\nbread=x\n", "") {
    println!("failed at {}:{} in <{}>", line, col, rule);
}
```

## How data crosses

Into Raku: the text you parse (and the grammar source). Out of Raku, two
channels:

- **Lazy leaf reads** — `.str()` → `String`, `.int()` → `i64`, `.num()` →
  `f64`. Cheap and precise; use these when you want a few fields.
- **`tree()` / `made()`** — everything at once, as the `Tree` enum:

  ```rust
  pub enum Tree {
      Null, Bool(bool), Int(i64), Num(f64), Str(String),
      List(Vec<Tree>),
      Map(BTreeMap<String, Tree>),
  }
  ```

  In a `tree()`, a node with no sub-captures becomes its matched text
  (`Str("2")`, not `Int(2)`); named captures become map keys; repeated
  captures become lists. `made()` carries whatever the actions class
  `make`d — that is the general-purpose way to have Raku *compute* something
  and hand the result to Rust as plain data.

Probing: `.truthy()?` answers whether anything matched at a path (a missing
capture read with `.str()` is an `Err`, not a panic). `.len()?` is the list
length — 1 for a plain node, 0 for a missing one.

## What Rust takes care of for you

- **Freeing matches** — a `Match` owns a rooted engine value and unroots it
  in `Drop`. No `close()` to remember; when the match goes out of scope the
  engine value is released, and the borrow checker stops a `Node` path from
  outliving its `Match`.
- **Thread safety** — `Grammar` and `Match` are `!Send`, so the compiler
  itself enforces the engine's contract: one interpreter per process, one
  thread talking to it.

## Using it in your own project

`cargo new myparser`, then in `myparser/Cargo.toml`:

```toml
[dependencies]
rakulang = { path = "/path/to/raku++/bindings/rust" }
```

Build and run with `RAKUPP_LIB_DIR` pointing at your build directory:

```bash
RAKUPP_LIB_DIR=/path/to/raku++/build cargo run
```

The env variable matters at *build* time (linking + embedded rpath). Against
a system-installed librakupp (e.g. `cmake --install`), you can omit it — the
system linker paths are searched.

## Testing it properly

The full gate — same grammar, same 2000-line corpus, this crate's output
byte-compared against plain `rakupp`'s — runs from the repo root:

```bash
build/rakupp tools/grammar-smoke.raku
```

Look for `ok - Rust output is byte-identical to rakupp's`. The Rust leg
skips (loudly) if cargo is missing or its architecture cannot load the
library — see below.

## When things go wrong

- **`ld: library 'rakupp' not found` while building** — `RAKUPP_LIB_DIR` is
  unset or points at a directory with no shared library. A plain build
  directory is static-only: rebuild with `-DRAKUPP_BUILD_SHARED=ON`.
- **`incompatible architecture` / `found architecture 'arm64'...`** — your
  Rust toolchain and the library disagree (common on Apple Silicon with an
  x86_64 rustup install; check `rustc -vV | grep host`). Either install the
  arm64 toolchain, or build an x86_64 library to match:
  `cmake -B build-x64 -DCMAKE_OSX_ARCHITECTURES=x86_64 -DRAKUPP_BUILD_SHARED=ON ...`
  and point `RAKUPP_LIB_DIR` there.
- **`rk_new refused: an interpreter is already live`** — something else in
  this process already embeds Raku++; the engine allows one interpreter per
  process.
- **`Error::Raku(...)`** — a die from the engine: broken grammar source, a
  missing capture read as a value, an exception inside an actions method.
  The message is the engine's own.
