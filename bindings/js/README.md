# rakulang — Raku grammars for JavaScript (Bun)

This package lets a JavaScript program parse text with a Raku grammar. The
grammar stays a plain `.raku` file; the parsing happens in an embedded
Raku++ interpreter (`librakupp`, reached through `bun:ffi`); the package
only moves values across.

**Bun only.** The FFI layer is Bun's built-in `bun:ffi`, so plain Node.js
cannot run this package. Install Bun from [bun.sh](https://bun.sh) —
`curl -fsSL https://bun.sh/install | bash`.

## What you need

- **Bun** (1.0+) — check with `bun --version`.
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
RAKUPP_LIB=$PWD/build/librakupp.dylib bun bindings/examples/shopping.mjs
```

(`RAKUPP_LIB` names the library file. With `rakupp` on PATH from an
installed layout you can omit it — the loader finds the library next to the
binary.) Expected output:

```
3 items
milk x 2
bread x 1
eggs x 12
total, computed in Raku: 15
as plain JS data: {"item":[{"name":"milk","qty":"2"},{"name":"bread","qty":"1"},{"name":"eggs","qty":"12"}]}
line 2 column 7 while trying <qty>
```

If you see this, the binding works.

## The example, explained

The source is [../examples/shopping.mjs](../examples/shopping.mjs); the
grammar it loads is [../examples/shopping.raku](../examples/shopping.raku).
The whole API in one pass:

```js
import { Grammar, ParseError } from "rakulang";   // or a path to rakulang.js

// Compile the grammar file. name is the grammar's name INSIDE the file;
// actions names an actions class in the same file (omit for none).
const g = Grammar.fromFile("shopping.raku",
                           { name: "Shopping", actions: "ShoppingActions" });

// Parse. null means "the text did not match" (an engine failure throws
// RakuError instead). The whole input must match. To parse a fragment with
// one rule: g.parse(text, { rule: "item" }).
const m = g.parse("milk=2\nbread = 1  eggs=12\n");

// Walk the match lazily. get() names a capture; at() indexes a repeated
// one; a repeated capture is iterable. Nothing crosses the engine boundary
// until a terminal call like .str() / .int() / .length — one engine call
// each.
for (const item of m.get("item"))
  console.log(item.get("name").str(), item.get("qty").int());

// What the Raku actions class computed, as native data. ShoppingActions'
// TOP method did `make ...sum`, so this is 15.
console.log(m.made());

// Or convert everything below a node at once (~1.4x the parse's own cost):
// plain objects, arrays, strings, numbers.
const all = m.tree();

// Done with the match? Free it. A Match holds a rooted engine value and
// there is no reliable GC hook for that — closing is YOUR job.
m.close();

// A failed parse, diagnosed: strict throws ParseError with the line,
// column, and the deepest rule the engine was trying there.
try {
  g.parse("milk=2\nbread=x\n", { strict: true });
}
catch (e) {
  if (e instanceof ParseError) console.log(e.line, e.column, e.rule);
}
```

## How data crosses

Into Raku: the text you parse (and the grammar source). Out of Raku, two
channels:

- **Lazy leaf reads** — `.str()` → string, `.int()` / `.num()` → number.
  Cheap and precise; use these when you want a few fields.
- **`tree()` / `made()`** — everything at once, as plain JS values: `null`,
  booleans, numbers, strings, arrays, objects. In a `tree()`, a node with no
  sub-captures becomes its matched *text* (`"2"`, not `2`); named captures
  become object keys; repeated captures become arrays. `made()` carries
  whatever the actions class `make`d — the general-purpose way to have Raku
  *compute* something and hand the result to JS as plain data.

Probing: `.truthy()` answers whether anything matched at a path (reading a
missing capture with `.str()` throws `RakuError`). `.length` is the list
length — 1 for a plain node, 0 for a missing one.

## Rules to remember

- **Call `m.close()`** when you are done with a match — like Go and unlike
  Python, nothing frees it for you.
- **One interpreter per process**, created on first use; keep engine calls
  on one thread (Raku code inside the interpreter threads freely).

## Using it in your own project

```bash
bun init mine && cd mine
bun add rakulang@file:/path/to/raku++/bindings/js
```

then `import { Grammar } from "rakulang"` and run with `RAKUPP_LIB` set (or
`rakupp` on PATH, or `RAKUPP_HOME` pointing at an install prefix).

## Testing it properly

The full gate — same grammar, same 2000-line corpus, this package's output
byte-compared against plain `rakupp`'s — runs from the repo root:

```bash
build/rakupp tools/grammar-smoke.raku
```

Look for `ok - JS output is byte-identical to rakupp's`. The JS leg skips
(loudly) if bun is missing or its architecture cannot load the library — see
below.

## When things go wrong

- **`Cannot find module 'bun:ffi'`** — you ran it with Node. Use `bun`.
- **`librakupp not found`** — the error lists every path it tried. Set
  `RAKUPP_LIB` to the library file. If a `rakupp` binary was found but no
  library, that build is static-only: rebuild with
  `-DRAKUPP_BUILD_SHARED=ON`.
- **`incompatible architecture`** — your Bun and the library disagree
  (common on Apple Silicon with an x86_64 Bun; check
  `file $(which bun)`). Either install arm64 Bun, or build an x86_64
  library to match:
  `cmake -B build-x64 -DCMAKE_OSX_ARCHITECTURES=x86_64 -DRAKUPP_BUILD_SHARED=ON ...`
- **`rk_new refused: an interpreter is already live`** — something else in
  this process already embeds Raku++; the engine allows one interpreter per
  process.
- **`RakuError` from a read** — usually a missing capture read as a value
  (probe with `.truthy()` first), or a die inside an actions method; the
  message is the engine's own.
