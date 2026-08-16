# rakulang — Raku from JavaScript (Bun)

A single-file ES module over `librakupp`'s C ABI. No build step and no
native addon: the FFI layer is Bun's built-in `bun:ffi`, values cross through
[`rakupp.h`](../../include/rakupp/rakupp.h), and the grammar logic lives in a
Raku shim that ships inside the library itself.

**Bun only.** `bun:ffi` is Bun's, so plain Node.js cannot run this package.
Install Bun from [bun.sh](https://bun.sh) — `curl -fsSL https://bun.sh/install
| bash`.

## 1. What you need

- **Bun.** Check with `bun --version`.
- **`librakupp`.** From the repo root:

  ```bash
  cmake -B build -DCMAKE_BUILD_TYPE=Release -DRAKUPP_BUILD_SHARED=ON
  cmake --build build -j
  ```

  A build directory configured without `-DRAKUPP_BUILD_SHARED=ON` is
  static-only and this package cannot use it.

## 2. Install

In your own project:

```json
{ "dependencies": { "rakulang": "file:/path/to/raku++/bindings/js" } }
```

then `import { interpreter, Grammar } from "rakulang"`.

With `rakupp` on PATH nothing needs configuring — the loader takes
`librakupp` from beside it. To override, name one: an explicit path to
`interpreter("/path/to/librakupp.dylib")`, or `RAKUPP_LIB` (the file), or
`RAKUPP_HOME` (an install prefix with `lib/`). **A library you name is used
as given.** If it cannot be loaded you get that error, not a quiet fall-back
to whichever other library happens to be findable — the usual cause is an
architecture mismatch, and falling back makes the symptom point nowhere near
the cause. Unset the variable to search instead.

## 3. Two minutes

Run both examples from the repo root (`.so` for `.dylib` on Linux):

```bash
RAKUPP_LIB=$PWD/build/librakupp.dylib bun bindings/examples/calc.mjs
```
```bash
RAKUPP_LIB=$PWD/build/librakupp.dylib bun bindings/examples/shopping.mjs
```

`calc` ([calc.mjs](../examples/calc.mjs)) prints:

```
2 + 2 = 4
area(3, 4) = 12
primes below 30: 2 3 5 7 11 13 17 19 23 29
stats: count=8 sum=31 mean=3.88 max=9
greet: Hello, Ada! You are 36.
30! = 265252859812191058636308480000000
died: division by zero
```

`shopping` ([shopping.mjs](../examples/shopping.mjs)) prints:

```
3 items
milk x 2
bread x 1
eggs x 12
total, computed in Raku: 15
as plain JS data: {"item":[{"name":"milk","qty":"2"},{"name":"bread","qty":"1"},{"name":"eggs","qty":"12"}]}
line 2 column 7 while trying <qty>
```

If you see both, the binding works.

## 4. Running Raku

```js
import { interpreter } from "rakulang";
import { readFileSync } from "fs";

const raku = interpreter();            // the process's interpreter

raku.eval("my $x = 41");
raku.eval("$x + 1");                   // 42 — eval keeps state, like the REPL

raku.eval(readFileSync("calc.raku", "utf8"));   // loading subs is an eval
raku.call("area", 3, 4);               // 12
raku.call("stats", [3, 1, 4]);         // { count: 3, sum: 8, ... }
raku.call("greet", { name: "Ada" });   // an object becomes a Raku hash

raku.can("area");                      // true
raku.version;                          // '3.14.0'
```

`eval` returns the last statement's value; `call` looks the routine up in the
mainline scope, so anything an earlier `eval` declared is callable. Arguments
convert automatically — `null`/`undefined`, `boolean`, `number` (integral
ones become Raku `Int`, the rest `Num`), `bigint`, `string`, `Array`, and
plain objects. Anything else throws `RakuError`.

## 5. Parsing with grammars

```js
import { Grammar } from "rakulang";

const log = Grammar.fromFile("log.raku", { name: "Log", actions: "LogActions" });

const m = log.parse(text);             // a handle, not data; null if no match
for (const line of m.get("line"))      // lazy: one engine call per leaf
  console.log(line.get("ip").str(), line.get("status").int());
console.log(m.get("line").at(0).get("size").made());   // computed by actions

const everything = m.tree();           // eager, opt-in (~1.4× the parse)
m.close();                             // required — see §8
```

`fromFile(path, {name, actions})` compiles and caches: identical source
compiles once, and each *named* compile is isolated, so recompiling an edited
grammar never rebinds an earlier `Grammar`'s body. `name` may be omitted only
when the grammar declaration is the file's last statement.

`parse(text, {rule, strict})` anchors to the whole input and returns a match
node or `null`; pass `rule` to parse a fragment with one rule. `get()` and
`at()` build a lazy path — nothing crosses the boundary until `.str()`,
`.int()`, `.num()`, `.truthy()`, `.length`, iteration, `.tree()` or
`.made()`. A repeated capture is iterable.

## 6. Values

Raku `Int` → `number`, `Num`/`Rat` → `number`, `Str` → `string`, `List` →
`Array`, `Hash` → `Object`, `True`/`False` → `boolean`, `Any` → `null`. The
same rules run in reverse for arguments.

An integer wider than 64 bits arrives as a string of digits — JS numbers
would lose it silently, which is the reason. In a `tree()`, a match node with
no sub-captures becomes its matched *text*, so `qty` is the string `"2"`; use
`.int()` on the node, or an actions class, for numbers.

## 7. Errors

`RakuError` is a Raku `die` crossing the boundary. `ParseError` extends it
for a diagnosed non-match, carrying `.line`, `.column`, `.rule` and `.pos`:

```js
try {
  g.parse(text, { strict: true });
}
catch (e) {
  if (!(e instanceof ParseError)) throw e;
  console.log(`line ${e.line} column ${e.column} while trying <${e.rule}>`);
}
```

## 8. Lifetime and threading

One interpreter per process, created on first use; one JS thread talks to it
at a time (Raku code inside it threads freely).

**Call `close()` on a Match when you are done with it.** A Match holds a
rooted value inside the interpreter and there is no reliable GC hook to lean
on, so JS carries the same obligation Go does. Values from `eval` and `call`
are already plain JS data and need nothing.

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

- **`librakupp not found`** — the error lists every path tried. Set
  `RAKUPP_LIB` to the library file. If a `rakupp` binary was found but no
  library beside it, that build directory is static-only: rebuild with
  `-DRAKUPP_BUILD_SHARED=ON`.
- **`incompatible architecture`** — your Bun and the library disagree
  (`file $(which bun)`). Either install a matching Bun, or build the library
  for Bun's architecture:
  `cmake -B build-x64 -DCMAKE_OSX_ARCHITECTURES=x86_64 -DRAKUPP_BUILD_SHARED=ON ...`
- **`Cannot find module "bun:ffi"`** — you ran it under Node. Use `bun`.
- **`rk_new refused`** — something already created an interpreter in this
  process. Use `interpreter()`, which returns the shared one.
