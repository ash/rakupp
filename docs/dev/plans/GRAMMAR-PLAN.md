# Plan: Raku grammars as a service for other languages

*Written 2026-08-10, before any code. A **v4** plan, sitting on
[ABI-PLAN.md](ABI-PLAN.md) and [EMBED-PLAN.md](EMBED-PLAN.md) — it needs no ABI
change of its own, which is most of the argument for doing it first.*

## The pitch, and why it is the headline

"Embed Raku in Python" is abstract. **"Use Raku grammars from Python"** is a
capability the host language has no equivalent of:

- a regex library gives you one pattern, not a composable grammar with named
  rules, inheritance and longest-token dispatch;
- parser-combinator libraries exist everywhere and are uniformly more verbose
  than the grammar they encode;
- ANTLR-shaped tools exist, and they want a code-generation step in your build.

Raku grammars are the language's genuinely distinctive feature, and rakupp's
engine is fast at them — the course TOC grammar parses ~2× faster here than on
Rakudo. That combination is the reason for someone outside this project to
care about the embedding campaign at all, which is why it gets its own plan
rather than a paragraph inside one.

The same design serves C++, Go, Rust and JS/TS; Python is simply the first host.
What each of them changes is spelling and lifetime, not mechanics — worked
through in [The same design in C++, Go and Rust](#the-same-design-in-c-go-and-rust).

---

## The grammar is a `.raku` file. Everything else is sugar over that.

The tempting design is a host-language class whose members are rules — a
metaclass in Python, a builder in Go, a macro in Rust. That layer may well get
built, but it is **not** the design, and the distinction is the most important
decision in this plan:

> **The class must never be a parallel path — only a generator whose output is
> the same text.**

Because the moment it acquires semantics of its own, every host owes a
maintenance debt that grows with Raku itself. Raku grammar syntax has plenty a
class shape cannot express without new API for each item: `proto` rules,
`also is`, methods called from rules, `<.ws>` overrides, `FAILGOAL`, arguments
to `TOP`, dynamic variables, and inline action code. A generator that emits text
can never develop that gap, because anything it cannot say is said by dropping
to the string — and the program keeps working.

### So the layering is

1. **`from_source(text, name)`** — the one primitive. `name` exists only so
   diagnostics can say `config.raku:12`.
2. **`from_file(path)`** — two lines over it, and what the documentation leads
   with.
3. **A host class, optionally** — sugar that produces exactly what (1) takes,
   with a raw-source escape hatch for any rule it cannot spell.

### Why the primitive is text and not a path

A file-only API would be simpler, and it breaks in three places:

- **The browser has no filesystem.** WASM is a named host, so `from_source` has
  to exist regardless of what is convenient natively.
- **Embedded resources hand you text, not a path** — `include_str!`,
  `embed.FS`, `importlib.resources`. A file-only API would force writing a
  temporary file to feed it.
- **Grammars generated at run time** from host data — a keyword list out of a
  config, a dialect switch.

### What the file buys, beyond avoiding escaping

A `.raku` file is a **module**, not a blob of grammar. It can `use` other
modules, define helper subs, and carry its **action class beside the grammar**.
That last one quietly removes this plan's most expensive tier: actions written
in Raku compute `.made` *inside* the parse, so the host receives finished
values and never needs callable-registration or a boundary crossing per node.

The recommended path is therefore: **write a `.raku` file holding the grammar
and its actions, point the host at it, get results back.** Inline strings,
generated grammars and host callbacks are the escape hatches for the cases that
genuinely cannot do that.

It also means the grammar stays a first-class Raku artifact — runnable under
`rakupp`, testable on its own, syntax-highlighted, copy-pasteable into a Raku
program, and shared unchanged between a Raku program and a Python one.

### Three rejected designs

- **A host combinator DSL** — `seq(ref('key'), lit(' = '), ref('value'))`. More
  verbose than the regex it replaces, strictly weaker unless every Raku regex
  construct gets an object, and worse error messages. Every host language
  already has parser-combinator libraries; if one were the answer, nobody would
  need this.
- **A host→Raku-regex compiler.** The same objection, plus a compiler.
- **A class layer with its own semantics** — the parallel path the rule above
  forbids.

**The honest statement of the product: you write Raku, and your host program
calls it.** The grammar language is the whole reason to be here, so hiding it is
self-defeating. What the host contributes is invocation, results and
integration — not a second notation.

---

## Verified today (2026-08-10)

Three things were checked before the plan was written, because each could have
killed it.

**Grammars can be built from a runtime string.** "No runtime string→regex" is a
known rakupp limitation, so this needed proving:

```
grammar assembled from a string and EVAL'd → parse: True, 3 captures, third: ｢gamma｣
```

**A Match converts to plain nested data in eight lines of Raku**, producing
exactly the shapes the ABI's `rk_type` / `rk_key_at` / `rk_at_pos` already read:

```
${:entry($[{:key("a"), :value("1")}, {:key("b"), :value("two")}])}
```

**ABI 2 already has what a host needs** — `rk_call`, `rk_call_value`, `rk_can`
and `rk_root`. A host can hold a Match across calls (`rk_root`) and reach into
it by calling a Raku helper (`rk_call`).

**So the entire service can be built with zero ABI changes.** That is the
strongest reason to do this before the rest of the embedding work: it is a real
deliverable that also validates the ABI, and if it turns out awkward, the
awkwardness is evidence about the ABI collected cheaply.

---

## The measurements that decide the design

A 2,000-line, 58 KB log file through a five-rule grammar:

| step | time |
|---|---:|
| `Log.parse($input)` | **10 ms** |
| eager conversion of the whole Match to nested data | **31 ms** |
| selective access — two fields per line | **3 ms** |

**Eager conversion costs 3× the parse, and is usually wasted.** A host almost
never wants every capture of every node; it wants two fields per record. The
obvious design — parse, convert everything, hand back a dict — is the expensive
one, and it is expensive in the part the host did not ask for.

So: **lazy by default, eager on request.**

The second measurement is a gap rather than a number. A failed parse gives the
host nothing:

```
failed parse returns: Any / undefined      $/ after failure: (no match)
```

No position, no expectation, no rule name. A parser that answers "no" and
nothing else is unusable in a host program, so error reporting is a phase here
rather than a footnote.

---

## The design

1. **Load grammar source, compile it once, cache the type object.**
   `from_source(text, name)` `EVAL`s the source; `from_file(path)` reads and
   calls it. The cache is keyed on the source, so an edited grammar cannot hit a
   stale compiled type.
2. **`parse()` returns a handle, not data** — a rooted Match (`rk_root`), so
   nothing is converted that nobody asked for.
3. **Lazy access through a small Raku shim module.** `m['line'][3]['ip']`
   becomes one `rk_call` into a helper that walks the Match and returns the leaf.
   No ABI change; the shim ships with the binding.
4. **`.tree()` is the opt-in eager conversion**, documented with the 3× number
   so the choice is informed rather than accidental.
5. **Actions in three tiers**, cheapest first:
   - *Raku actions in the same file* — **the recommended path.** `.made` is
     computed inside the parse, the host receives finished values, and the
     boundary is crossed once. This is why the grammar living in a `.raku`
     module rather than in host strings is a design decision and not a
     convenience.
   - *no actions* — walk the tree or the lazy handle in the host, when the host
     wants the raw shape.
   - *host callbacks* — a Python/JS method per rule. Rare, not merely last: it
     needs host-callable registration (EMBED-PLAN E2, not in ABI 2) **and** costs
     a boundary crossing plus a GIL acquisition per node — on this benchmark
     ~10,000 crossings where the tree costs one. Justify it before building it.
6. **Errors carry a position.** A highwater mark maintained during the parse:
   furthest offset reached, the rule that was trying, line and column. This is
   the one piece that may need interpreter work rather than shim work.
7. **A host class layer, if and when it earns itself** — bound by the rule
   above: a generator whose output is the same text `from_source` takes, with a
   raw-source escape hatch per rule. It is never the layer a feature is added
   to.

---

## The same design in C++, Go and Rust

The mechanics above are host-agnostic — one Raku grammar source, one `EVAL`, a
rooted handle, lazy access. **In every host the primary form is the same two
lines**, because the grammar lives in a `.raku` file:

```
auto  g = rakupp::Grammar::from_file("config.raku");   // C++
g,  err := rakupp.GrammarFromFile("config.raku")       // Go
let   g = rakupp::Grammar::from_file("config.raku")?;  // Rust
```

What follows is the **sugar** — the optional class layer — and it is shown per
host because that is where the languages genuinely differ. Every example below
is a generator whose output is the same grammar text `from_source` takes; none
of them is a second way to mean something. Python gets a metaclass; none of
these three do, and pretending otherwise produces bad bindings.

### C++

No reflection, so the declarative class body is out. An initializer list is the
honest shape — and **raw string literals mean the regex needs no escaping at
all**, which is better than Python manages:

```cpp
#include <rakupp/grammar.hpp>          // header-only, over the C API

static const rakupp::Grammar Config = rakupp::grammar("Config", {
    { rakupp::Rule,  "TOP",   R"(<entry>+ % \n)"     },
    { rakupp::Token, "entry", R"(<key> " = " <value>)" },
    { rakupp::Token, "key",   R"(\w+)"               },
    { rakupp::Token, "value", R"(\N+)"               },
});

auto m = Config.parse(text);
if (!m) throw rakupp::ParseError(m.error());   // line, column, rule — from G1
for (auto entry : m["entry"])
    std::cout << entry["key"].str() << " = " << entry["value"].str() << "\n";
```

**C++ is the host where the lifetime story is free.** `rk_root` / `rk_unroot`
map exactly onto a destructor, so the leak that ABI-PLAN flags as the cost of
rooted handles cannot happen here — the RAII wrapper is the mitigation, and it
compiles into the caller, so no C++ type crosses the boundary
([why C, not C++](ABI-PLAN.md#why-c-and-not-c)).

### Go

No inheritance, no metaprogramming; a builder is the idiomatic shape. Backtick
raw strings suit regex well — with one caveat worth documenting, that a grammar
containing a backtick has to fall back to the interpreted form:

```go
var Config = rakupp.NewGrammar("Config").
    Rule ("TOP",   `<entry>+ % \n`).
    Token("entry", `<key> " = " <value>`).
    Token("key",   `\w+`).
    Token("value", `\N+`)

m, err := Config.Parse(text)
if err != nil { return err }        // a parse failure is an error, per Go idiom
defer m.Close()                     // explicit: a native handle, not GC-managed

for _, e := range m.All("entry") {
    fmt.Println(e.Str("key"), "=", e.Str("value"))
}
```

Two Go-specific obligations:

- **`Close()` is explicit.** A rooted Match is native memory; leaning on
  `runtime.SetFinalizer` to release it is the wrong reflex and non-deterministic
  besides.
- **Go is where the thread contract bites hardest.** Goroutines make "parse
  these thousand files concurrently" the *default* reflex, and today there is
  one interpreter per process (EMBED-PLAN E5) with callbacks arriving on rakupp
  threads. The Go binding must state plainly whether a handle is safe to share
  across goroutines — and while the answer is no, say so in the type, not in the
  documentation.

### Rust

Macros get closest to Python's declarative body, and Rust is the one host that
can do **better** than Python here:

```rust
rakupp::grammar! { Config,
    rule  TOP   = r"<entry>+ % \n";
    token entry = r#"<key> " = " <value>"#;
    token key   = r"\w+";
    token value = r"\N+";
}

let m = Config::parse(text)?;                 // Result<Match, ParseError>
for e in m.iter("entry") {
    println!("{} = {}", e.key()?, e.value()?);   // generated accessors
}
```

The capture names are known statically — they are right there in the macro
input — so a proc macro can **generate typed accessors** (`e.key()`, `e.value()`)
instead of string lookups that fail at run time. A misspelled capture becomes a
compile error. That is a real ergonomic win no dynamic host can match, and it is
the reason to do the Rust binding properly rather than as a thin `libloading`
wrapper.

`Drop` gives the same free lifetime as C++, and `?` composes with the G1 error
tier.

### Summary

| | binds via | rule declaration | lifetime | parse failure | main risk |
|---|---|---|---|---|---|
| Python | `ctypes` / `cffi` | metaclass, class body | `__del__` / context manager | exception | eager `.tree()` habit |
| C++ | the header | initializer list | **RAII — free** | exception | none specific |
| Go | `purego` / cgo | builder chain | **explicit `Close()`** | `error` return | goroutines vs one interpreter |
| Rust | `libloading` / `-sys` | `grammar!` macro | **`Drop` — free** | `Result` | proc-macro complexity |
| JS/TS | `bun:ffi`, `koffi`, WASM | object literal | explicit `free()` | throw | async actions (EMBED-PLAN) |

The row that matters most is *lifetime*: two of the five hosts get it for free,
and the other three need it designed. That is the same asymmetry ABI-PLAN names
when it flags rooted handles as reintroducing a leak the extension arena made
impossible.

---

## Phases

- **G0 — the shim and a Python prototype, zero ABI change.** `from_source` /
  `from_file`, `parse()` returning a handle, lazy access, `.tree()`, and a
  grammar file carrying its own Raku actions. **No class layer in G0** — it is
  sugar, and building it first would make it look like the design. **Gate:** the
  log benchmark from a Python host lands within a documented factor of the same
  grammar run by `rakupp` directly, and results are byte-identical to running
  the grammar file under `rakupp` on its own.
- **G1 — parse failure diagnostics.** Highwater mark, position, expected rule.
  **Gate:** a deliberately broken input produces line, column and rule name in
  the host's own exception type.
- **G2 — ergonomics, and a host that is not Python-shaped.** `from_file()`,
  grammar inheritance, `:sigspace` and the `rule`/`token`/`regex` distinction
  surfaced properly. Then a **second host, chosen to break assumptions**: C++ or
  Rust, because both have RAII and neither has a metaclass, which tests the two
  things a Python-only design would get wrong (lifetime, and declaring rules
  without reflection). Go and JS/TS follow; each is cheap once the shim is
  shared, and each is a test of G0's design rather than new machinery.
- **G3 — host callbacks**, only if a real workload shows the tree walk is the
  bottleneck. Depends on EMBED-PLAN E2.
- **G4 — a native Match walker** in C, only if the shim's per-access `rk_call`
  cost shows up in a profile. Named so it is not built speculatively.

---

## Gates

1. **Byte-identical results** — the same grammar, same input, run from the host
   and run from a `.raku` file, produce the same tree. Every batch.
2. **The benchmark above**, re-run per phase, with the parse / eager / selective
   split kept visible so a regression in one is not hidden by the others.
3. **Roast** unchanged, **module battery** unchanged, **`perf-guard --check`** —
   the shim is Raku and the binding is host code, so none of this should move
   the interpreter at all. If it does, something was added in the wrong place.
4. **Grammar cache correctness** — a changed grammar source must not hit a
   stale compiled type.

---

## Risks, named

- **Eager conversion is the tempting wrong default.** It reads better in a
  README and costs 3× the parse. The number is in the plan so the temptation is
  answerable.
- **Error reporting may reach into the interpreter.** The highwater mark is the
  one item here that might not be shim-shaped; if it needs engine support, that
  is a bigger phase than it looks.
- **The class layer growing its own semantics.** The single largest risk, and
  the reason the rule is stated as a rule: the moment a feature can only be
  expressed through the class, every host owes a maintenance debt that grows
  with Raku. Mitigation is structural rather than vigilant — the class emits
  text and nothing else, and every class carries a raw-source escape hatch, so
  there is always somewhere to go instead of extending it.
- **Raku syntax inside host strings** — escaping is mostly handled by raw
  strings, but editor support is not. `from_file()` is the mitigation, which is
  why it is the documented default rather than the large-grammar fallback.
- **`--slim` conflict.** A grammar service needs the parser *and* the regex
  engine, so an embeddable `librakupp` cannot inherit
  [SLIM-PLAN.md](SLIM-PLAN.md)'s defaults — the same conflict ABI-PLAN records.
- **Threading.** One parse is single-threaded, so the post-v3 callback-thread
  problem stays away — *unless* a host `hyper`s over inputs with host callbacks
  attached. Document it before someone finds it.
- **Scope creep into exposing all of Match.** `.pos`, `.orig`, `.chunks`,
  `.caps` and the rest are a large surface. The lazy accessor answers "give me
  this capture"; everything beyond that needs a reason.

---

## Non-goals

- A host-language DSL for regex bodies, in any form.
- A class layer that can express anything `from_source` cannot. If that ever
  becomes tempting, the answer is a raw-source rule, not a new API.
- Exposing the whole `Match` API.
- Making Raku grammars work without Raku syntax — that is the product, not an
  obstacle.
- Code generation. The grammar is compiled at run time by a real Raku
  compiler, which is the advantage over the ANTLR shape, not a shortcut.
