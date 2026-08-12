# Language bindings — Raku grammars everywhere

One design, five hosts (GRAMMAR-PLAN): the grammar stays a `.raku` file, the
Raku shim ships inside `librakupp` (`rk_grammar_shim`), and every binding is
just invocation, results and lifetime in its host's idiom. Each is gated by
`tools/grammar-smoke.raku`: the same grammar and 2000-line corpus, driven by
each host and byte-compared against plain `rakupp`.

| host | where | binds via | lifetime | registry name |
|---|---|---|---|---|
| Python | [python/](python/) | ctypes | GC / `close()` / `with` | `rakulang` (PyPI free) |
| C++ | [src/grammar.hpp](../src/grammar.hpp) | linked, header-only | **RAII — free** | ships with the engine |
| JS/TS | [js/](js/) | `bun:ffi` | explicit `close()` | `rakulang` (npm free) |
| Go | [go/](go/) | cgo | explicit `Close()` | module `rakulang` |
| Rust | [rust/](rust/) | extern + build.rs | **`Drop` — free** | `rakulang` (crates.io free) |

The same two lines everywhere, per the plan:

```python
g = rakulang.Grammar.from_file("log.raku", name="Log")     # Python
```
```cpp
auto g = rakupp::Grammar::from_file("log.raku", "Log");    // C++
```
```js
const g = Grammar.fromFile("log.raku", { name: "Log" });   // Bun
```
```go
g, err := rakulang.FromFile("log.raku", "Log", "")         // Go
```
```rust
let g = rakulang::Grammar::from_file("log.raku", "Log", "")?;  // Rust
```

Every host gets: lazy leaf access (one engine call per terminal), the eager
`tree()` (documented at ~1.4× the parse), same-file Raku actions via `.made`,
`:rule` fragment parsing, and G1's diagnosed failures — line, column, and the
deepest failing rule, in the host's own error type.

Python is the reference binding (README there has the measured overhead
table); the C++ header ships in the install layout; the Python platform wheel
(librakupp bundled, works with no rakupp installed) is built by
`tools/build-wheel.sh` on every release. Publishing to the registries is a
separate, manual decision.
