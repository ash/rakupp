# rakupp — Raku from C++

Two header-only headers over `librakupp`'s C ABI. Unlike the other four
bindings there is no package here to install: the headers ship with the
engine, and C++ *links* against the library rather than `dlopen`ing it.

| header | what it gives you |
|---|---|
| [`<rakupp/raku.hpp>`](../../include/rakupp/raku.hpp) | `eval`, `call`, `Tree`, `RakuError` — running Raku |
| [`<rakupp/grammar.hpp>`](../../include/rakupp/grammar.hpp) | `Grammar`, `Match`, `Node`, `ParseError` — parsing; includes `raku.hpp` |

Include `grammar.hpp` when you want both; it pulls in `raku.hpp`.

## 1. What you need

- **A C++17 compiler.**
- **`librakupp`.** From the repo root:

  ```bash
  cmake -B build -DCMAKE_BUILD_TYPE=Release -DRAKUPP_BUILD_SHARED=ON
  cmake --build build -j
  ```

  A build directory configured without `-DRAKUPP_BUILD_SHARED=ON` is
  static-only and cannot be linked against here.

## 2. Install

Nothing to install from a checkout — compile with `-Iinclude` and link
against the library you built:

```bash
c++ -std=c++17 -Iinclude your.cpp build/librakupp.dylib -Wl,-rpath,$PWD/build -o your
```

On Linux: `-Lbuild -lrakupp -Wl,-rpath,$PWD/build -lpthread`.

After `cmake --install`, the headers land in `<prefix>/include/rakupp/` and
the include lines in your source are unchanged — the flags become just
`-lrakupp`. See [include/README.md](../../include/README.md) for how the
public headers are separated from the engine's internal ones.

## 3. Two minutes

Run both examples from the repo root:

```bash
c++ -std=c++17 -Iinclude bindings/cpp/examples/calc.cpp build/librakupp.dylib -Wl,-rpath,$PWD/build -o calc && ./calc
```
```bash
c++ -std=c++17 -Iinclude bindings/cpp/examples/shopping.cpp build/librakupp.dylib -Wl,-rpath,$PWD/build -o shopping && ./shopping
```

`calc` ([examples/calc.cpp](examples/calc.cpp)) prints:

```
2 + 2 = 4
area(3, 4) = 12
primes below 30: 2 3 5 7 11 13 17 19 23 29
stats: count=8 sum=31 mean=3.88 max=9
greet: Hello, Ada! You are 36.
30! = 265252859812191058636308480000000
died: division by zero
```

`shopping` ([examples/shopping.cpp](examples/shopping.cpp)) prints:

```
3 items
milk x 2
bread x 1
eggs x 12
total, computed in Raku: 15
items in the tree: 3
bindings/examples/shopping.raku: no match — failed at line 2 column 7 while trying <qty>
```

If you see both, the binding works.

## 4. Running Raku

```cpp
#include <rakupp/raku.hpp>

rakupp::eval("my $x = 41");
rakupp::eval("$x + 1").int_();          // 42 — eval keeps state, like the REPL

rakupp::eval(source_of("calc.raku"));   // loading a file of subs is an eval

rakupp::call("area", {3, 4}).int_();    // 12
rakupp::Tree nums = std::vector<rakupp::Tree>{3, 1, 4};
rakupp::Tree s = rakupp::call("stats", {nums});

rakupp::can("area");                    // true
rakupp::version();                      // "3.14.0"
```

`eval` returns the last statement's value; `call` looks the routine up in the
mainline scope, so anything an earlier `eval` declared is callable.

`Tree` is **both** the argument type and the result type. It converts
implicitly from `int`, `long long`, `double`, `bool`, `const char*`,
`std::string`, `std::vector<Tree>` and `std::map<std::string, Tree>`, so
`call("f", {1, "two", 3.0})` builds an Int, a Str and a Num.

Two C++ habits this API asks for, both visible in `calc.cpp`:

- **Bind a result to a named `Tree` before reading a container out of it.**
  `list()` and `map()` return references *into* the `Tree`, so
  `for (auto& x : call("f").list())` walks a destroyed temporary.
- **Name a container argument as a `Tree` too.** Brace elision would
  otherwise flatten `{std::vector<Tree>{1, 2, 3}}` into three separate
  arguments.

## 5. Parsing with grammars

```cpp
#include <rakupp/grammar.hpp>

auto g = rakupp::Grammar::from_file("log.raku", "Log", "LogActions");

if (auto m = g.parse(text)) {           // std::nullopt if no match
    auto lines = (*m)["line"];
    for (size_t i = 0; i < lines.size(); i++)   // lazy: one call per leaf
        std::cout << lines[i]["ip"].str() << "\n";
    std::cout << lines[0]["size"].made().int_() << "\n";   // from the actions
    rakupp::Tree all = m->tree();       // eager, opt-in (~1.4× the parse)
}   // m's destructor unroots the engine value
```

`from_file(path, name, actions)` compiles and caches: identical source
compiles once, and each *named* compile is isolated, so recompiling an edited
grammar never rebinds an earlier `Grammar`'s body. `name` may be empty only
when the grammar declaration is the file's last statement.

`parse` anchors to the whole input and returns `std::optional<Match>`;
`parse_or_throw` raises `ParseError` with the diagnosis instead. Pass a rule
name to parse a fragment with one rule. `operator[]` builds a lazy path;
nothing crosses the boundary until `str()`, `int_()`, `num()`, `truthy()`,
`size()`, `tree()` or `made()`.

## 6. Values

Raku `Int` → `Tree` / `int_()`, `Num`/`Rat` → `num()`, `Str` → `str()`,
`List` → `list()`, `Hash` → `map()`, `True`/`False` → `boolean()`, `Any` →
`is_null()`. The same rules run in reverse for arguments.

An integer wider than 64 bits arrives as a string of digits: the C ABI hands
integers over as an `int64`, and the standard library has no wider integer to
put one in. In a `tree()`, a
match node with no sub-captures becomes its matched *text*, so `qty` is the
string `"2"`; use `.int_()` on the node, or an actions class, for numbers.

## 7. Errors

`rakupp::RakuError` (a `std::runtime_error`) is a Raku `die` crossing the
boundary. `rakupp::ParseError` derives from it for a diagnosed non-match,
carrying `.line`, `.col`, `.rule` and `.pos`:

```cpp
try {
    g.parse_or_throw(text);
}
catch (const rakupp::ParseError& e) {
    std::cout << e.what() << "\n";      // includes line, column and rule
}
```

## 8. Lifetime and threading

One interpreter per process, created on first use; one thread may talk to it
at a time (Raku code inside it threads freely).

Lifetime is the part C++ gets for free, which is why it was the plan's second
host: a `Match` holds a rooted value and its destructor unroots — the leak
that rooted handles reintroduce in the `dlopen`ing hosts cannot happen here.
Values from `eval` and `call` are owned `Tree`s and need nothing.

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

- **`ld: library not found for -lrakupp`** — the `-L` path has no library, or
  that build directory is static-only: rebuild with
  `-DRAKUPP_BUILD_SHARED=ON`.
- **`ignoring file …, found architecture 'x86_64', required 'arm64'`** — the
  compiler and the library disagree. Build the library for your compiler's
  architecture, or add `-arch` to match the library.
- **`undefined symbol: rk_…` at link time** — you compiled against the
  headers but did not link the library. Add it to the command line.
- **`rk_new refused`** — something already created an interpreter in this
  process. The session is a singleton; do not make another.
- **A dangling `list()` or `map()`** — you read a container out of a
  temporary `Tree`. Bind it to a named variable first; see §4.

## Embedding without the C++ layer

These headers are sugar over the C ABI. A host that wants the ABI directly —
or that is not C++ at all — should read
[docs/guide/EMBEDDING.md](../../docs/guide/EMBEDDING.md), which documents
[`<rakupp/rakupp.h>`](../../include/rakupp/rakupp.h) itself.
