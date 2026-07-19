# Bugs and divergences found while writing the JS/TS showcase

Found 19 July 2026 while building [`showcase/js/`](../../showcase/js/) — a
JavaScript/TypeScript interpreter (grammar + tree-walking evaluator) — under
`build/rakupp` and cross-checked against Rakudo (v2026.06). Writing a full
grammar + evaluator exercises corners the Roast suite doesn't, so this is a
fresh crop distinct from [`BUGS.md`](BUGS.md).

Every entry below is reproduced against the current binary, with a minimal
snippet, expected (Rakudo) vs. actual (rakupp), a pointer to the likely source
area, and the workaround already applied in `showcase/js/js.raku` (so each fix
lets a workaround retire). Ordered roughly by impact. None of these blocked the
showcase — it runs and its `.js` examples match Node byte-for-byte — but each
is a real divergence.

Two things I *thought* were bugs while building and could **not** reproduce on
recheck, so they are deliberately not listed: aliased subrule captures
(`$<x>=<subrule>`) record fine, and placeholder parameters (`$^a`/`$^b`) parse
fine. If you see them referenced in an early memory note, that note is stale.

---

## Grammar / parser

### G1. A grammar named after a quote operator mis-lexes — FIXED

```raku
grammar Q { token TOP { \d+ } }
sub f { my $x = 1; say $x; }      # was: ===SORRY!=== Parse error: Confused (got '}')
f();
```

**The original diagnosis in this doc was wrong.** It was never a "parse-state
leak," and grammar *size* was a red herring — every reproducer here happened to
name the grammar `Q`, and `js.raku`'s grammar is `JSGrammar`. `Q` is Raku's
generic-quote operator (`Q{…}`), so after the `grammar` keyword the lexer read
`Q { token TOP { \d+ } }` as a `Q{…}` quote literal, swallowing the whole body;
the leftover `sub … }` then failed. `grammar Foo { … }` always worked, and so
did `class Q { … }` — because the lexer's `quoteBlockedHere` list (the
declarator keywords after which `q`/`Q`/`m`/`rx`/… are names, not quotes)
included `class` and `role` but **not `grammar`**. Any grammar named `q`, `Q`,
`qq`, `m`, `rx`, `tr`, `s`, … hit it.

- **Fix:** added `"grammar"` to the `decl` set in `quoteBlockedHere`
  (`src/Lexer.cpp`). Lesson: when a bug's trigger seems to correlate with
  something vague (grammar "size"), minimize the reproducer's *incidental*
  details (here, the name) before theorizing about state.

### G2. User-defined `token ws` is ignored by `rule` sigspace — FIXED

```raku
grammar W { token ws { [ \s | '#' \N* ]* } rule TOP { 'a' 'b' } }
say W.parse("a # comment\n b").defined;   # was: False — now: True (as Rakudo)
```

`rule` always injected the built-in whitespace matcher, so overriding `ws`
(the standard way to skip comments) had no effect.

- **Fix:** in `nameMeta` (`src/Regex.cpp`) the built-in-`ws` shortcut was
  unconditional — `m.isWs = (name == "ws")`. Now `m.isWs = (name == "ws" &&
  !rule)`, so a grammar's own `token ws { … }` wins and the built-in is used
  only as the fallback when none is defined.
- **Workaround now removable:** `showcase/js/js.raku`'s `strip-comments`
  pre-pass could be replaced by a comment-skipping `token ws`. Left in place for
  now (the showcase also relies on it for the ASI pass, which needs the comments
  gone before the newline scan).

### G3. Proto longest-match ties resolve to the identifier alternative — MEDIUM

When two proto alternatives match the same length, the one shaped like a bare
identifier wins regardless of declaration order:

```raku
grammar P {
    token ident { <[a..z]>+ }
    rule TOP { <p> }
    proto rule p {*}
    rule p:sym<null>  { 'null' <!before <[a..z]>> }
    rule p:sym<ident> { <ident> }
}
# 'null' dispatches to p:sym<ident>, not p:sym<null>  (Rakudo picks <null>)
```

Rakudo's LTM would prefer the more specific `'null'` literal. Here the keyword
branch never won on a tie.

**FIXED.** Two problems compounded: (1) proto candidates were collected by
iterating a `std::map`, so they were ranked in *alphabetical* order, not
declaration order — and the `stable_sort` tie-break then kept the alphabetically
earlier candidate; (2) there was no specificity tie-break at all.

- **Fix:** `ClassInfo` now records `ruleOrder` (declaration order), and proto
  candidates are built from it (`src/Interpreter.cpp`). The LTM ranking
  (`src/Regex.cpp`) now sorts by `(declEnd desc, litPrefix desc)` with
  declaration order as the stable final tiebreak — where `litPrefix` is the
  length of a candidate's leading literal-atom run, tracked at match time
  (a new `MState::litPrefix`, extended in the `K::Lit` matcher). So `'null'`
  (4-char literal prefix) now outranks `<ident>` (`<[a..z]>+`, 0) at equal
  length, matching S05's tie-break rules, regardless of declaration order.
- **Workaround still fine:** js.raku classifies keywords in the
  `primary:sym<ident>` action; it doesn't need to change.

### G4. Unbalanced `{`/`}` inside a `token`/`rule` body breaks the lexer — FIXED

A brace inside a regex assertion or string — even correctly quoted — confused
the declarator body reader:

```raku
grammar B { token t { '$' <!before '{'> } token TOP { <t> 'x' } }
# was: ===SORRY!=== Parse error: expected } (got '')  — now parses
```

`tryRuleDecl` (`src/Lexer.cpp`) read the `{ … }`-delimited body by counting
braces with only backslash-escape handling — so a `{` inside a string, an
embedded code block, or a char class was miscounted as nesting.

- **Fix:** the body reader is now quote-, code-block-, and char-class-aware,
  matching the `rx{…}`/`m{…}` reader (`readPart`): a `}` inside `'…'`/`"…"`, a
  `{…}` code block, or a `[…]`/`<[…]>` char class no longer ends the delimiter.
  So `<!before '{'>` and `<[{}]>` now lex correctly.
- **Workaround now removable:** js.raku's `<!before \x7B>` could go back to
  `<!before '{'>`.
- **Adjacent, still open:** a literal brace in a char class matched at *run
  time* (`<[{]>`) is a separate regex-*compiler* limitation (`readPart` for
  `rx//`/`m//` has the same char-class-brace gap), and `"ab{"` — an unmatched
  `{` in a double-quoted string — is a string-interpolation quirk, not a
  grammar bug. Neither is fixed here.

---

## Runtime

### R1. An unmatched `CATCH` swallows the exception instead of rethrowing — HIGH

```raku
class AX is Exception {}
class BX is Exception {}
sub inner { BX.new.throw }
sub mid   { inner(); CATCH { when AX { } } }      # no branch matches BX
{ mid(); CATCH { when BX { say "outer sees it" } } }
# rakupp: nothing (BX vanished) — Rakudo: "outer sees it"
```

A `CATCH` whose `when`s don't match the thrown exception must rethrow it to the
enclosing handler. rakupp drops it.

- **Where to look:** the `CATCH` fall-through path in `src/Interpreter.cpp` —
  after no `when`/`default` matches, the exception should propagate, not be
  consumed.
- **Workaround:** every `CATCH` in js.raku carries an explicit
  `default { .rethrow }`. This is verbose but load-bearing; fixing R1 lets all
  of them go.

### R2. `return` / `last` inside a `CATCH` block is lost — HIGH

```raku
class RX is Exception { has $.v }
sub f { RX.new(v => 7).throw; CATCH { when RX { return .v } }; -1 }
say f();     # rakupp: Nil — Rakudo: 7
```

Control-flow verbs executed from within a `CATCH` handler don't take effect.

- **Where to look:** how `CATCH` handler bodies run relative to the routine
  frame in `src/Interpreter.cpp` — a `return` there should unwind the routine.
- **Workaround:** js.raku's function-call path catches `RetX` and assigns the
  value to a variable *after* the guarded block rather than `return`-ing from
  inside the handler (see `call-jsfunc`).

### R3. `with` / `without` statement modifiers aren't recognised — MEDIUM

```raku
my @o;
for 1..3 { my $j = $_ == 2 ?? Nil !! $_; next without $j; @o.push($_) }
# rakupp: "Useless use of $j in sink context" + wrong result
#         (parses as `next;` then a bare `without $j`)
# Rakudo: @o = [1, 3]
```

`if`/`unless` modifiers work; `with`/`without` are not parsed as modifiers, so
`next without $x` becomes `next` followed by a sink-context expression. (In a
richer context this surfaces as the runtime "next without loop construct".)

- **Where to look:** statement-modifier parsing in `src/Parser.cpp` — `with`
  and `without` are missing from the modifier set.
- **Workaround:** js.raku uses plain `if $x.defined { … }` guards instead of
  postfix `with`/`without`.

### R4. `@`-sigil parameters are copied, not aliased — MEDIUM

```raku
sub push99(@a) { @a.push(99) }
my @x = 1, 2;
push99(@x);
say @x;      # rakupp: [1 2] — Rakudo: [1 2 99]
```

In Rakudo a bare `@a` parameter binds the caller's array (mutating methods like
`.push` are visible to the caller); rakupp passes a copy. `$`-sigil parameters
holding the same array *do* alias, which is the workaround.

- **Where to look:** positional array-parameter binding in
  `src/Interpreter.cpp` — the container should bind by reference, read-only
  against rebinding but not against mutation.
- **Workaround:** js.raku's array built-ins take the backing array through a
  `$`-sigil parameter (`sub arr-prop($arr, …)`) so `push`/`sort`/`reverse`
  mutate in place.

### R5. Assigning through another object's public accessor is read-only — MEDIUM

```raku
class Box { has %.data }
my $b = Box.new;
$b.data{'k'} = 1;     # rakupp: "Cannot modify an immutable 'data'" — Rakudo: works
```

A public `has %.data` / `has @.list` accessor returns an immutable view, so
element assignment through it fails. (The same is true for another object's
`.vars{…}` inside a method.)

- **Where to look:** accessor generation for `has` attributes in
  `src/Interpreter.cpp` — the public accessor should return the container
  itself (assignable), matching Rakudo, at least for `%`/`@` attributes.
- **Workaround:** js.raku's `Env` exposes a `set-here` mutator method that
  writes `%!vars{$n}` from inside the class rather than through the accessor.

### R6. An attribute named `self` shadows to `Any` — LOW

```raku
class C { has $.self }
C.new(self => 5).self;    # rakupp: Any — Rakudo: 5
```

`has $.self` never returns its value — the invocant `self` shadows the
attribute accessor.

- **Where to look:** method/accessor name resolution vs. the implicit `self` in
  `src/Interpreter.cpp`.
- **Workaround:** js.raku renamed the field (`Bound.receiver`, not
  `Bound.self`). Low priority — trivial to avoid — but a surprising silent
  wrong-value.

### R7. A `sub` in a class body isn't callable from that class's methods — LOW

```raku
class C {
    sub helper($x) { $x * 2 }
    method m($n) { helper($n) }
}
C.new.m(21);    # rakupp: call fails — Rakudo: 42
```

Lexically-scoped `sub`s declared inside a class aren't visible to the class's
methods.

- **Where to look:** lexical scope threading between a class body and its method
  bodies in `src/Interpreter.cpp`.
- **Workaround:** js.raku declares all helpers at file scope.

---

## Native (`--exe`) only

These affect the compiled binary but not the interpreter, so they're lower
priority for a showcase (which runs interpreted) but matter for anyone shipping
`--exe` output. A task chip was filed 19 July 2026 for the first; all three are
likely related to the same native-backend divergence and share `showcase/js/`
reproducers. Compile with
`build/rakupp --exe -o /tmp/jsdemo showcase/js/js.raku`.

### N1. Much smaller recursion budget than the interpreter — HIGH (for --exe)

JS recursion deeper than ~15 levels (a few hundred Raku frames) crashes the
native binary while the interpreter handles it fine (`fastFib(78)` in
`examples/fib.js`). The crash surfaces as the misleading
`No such method 'message' for invocant of type 'X::Method::NotFound'` — a
*secondary* failure: `X::Method::NotFound` lacks `.message` in exe mode, masking
the real stack error.

- **Where to look:** this looks like the macOS twin of the Windows fix in
  commit `cdf3034` ("give the native binary a real main-thread stack"); see
  `findRuntime` / the `onBigStack` machinery. Two sub-fixes: (a) raise the
  exe's usable stack to interpreter parity; (b) make `X::*` types answer
  `.message` in exe mode so real errors aren't masked.
- **Workaround:** documented as a known limitation in
  `showcase/js/README.md` (the deep memoized `fib.js` run is interpreter-only).

### N2. A block-final `if/elsif/else` chain loses its value under `--exe` — MEDIUM

A pointy block whose last statement is an `if/elsif/else` returns the wrong
value in the native binary. Found via `String.split` returning empty under
`--exe`; fixed in js.raku's `str-prop` by using explicit `return`s in the
`split` native instead of relying on the block's final if/else value.

### N3. `.sort` two-arg comparators returning constructed `Order` are ignored — MEDIUM

Under `--exe`, `@a.sort(-> $x, $y { … ?? Less !! More })` (a comparator that
hand-builds `Order` values) does not sort, while a `<=>`-based comparator works.
js.raku's `arr-prop` `sort` returns `<=> 0e0` for exactly this reason.

---

## Quick status table

| # | Summary | Status | Fix location |
|---|---|---|---|
| G1 | grammar named after a quote op (`Q`/`m`/…) mis-lexes | **FIXED** | `quoteBlockedHere` += `grammar` (Lexer.cpp) |
| G2 | user `token ws` ignored by `rule` | **FIXED** | `m.isWs = name=="ws" && !rule` (Regex.cpp) |
| G3 | proto LTM tie → identifier branch | **FIXED** | decl-order candidates + litPrefix specificity |
| G4 | brace in `token`/`rule` body breaks lexer | **FIXED** | quote/block/class-aware body reader (Lexer.cpp) |

The R\* (runtime) and N\* (`--exe`) rows below are **not yet fixed** — this pass
covered the grammar/parser pitfalls only. Roast: +2 assertions, 0 regressions
(full suite 187696 → 187698).
| R1 | unmatched `CATCH` swallows | high | clean | `default { .rethrow }` |
| R2 | `return`/`last` in `CATCH` lost | high | clean | set flag after block |
| R3 | `with`/`without` modifiers unparsed | medium | clean | `if …defined` guard |
| R4 | `@`-param copies not aliases | medium | clean | pass array via `$` param |
| R5 | assign via public accessor is RO | medium | clean | private mutator method |
| R6 | `has $.self` returns Any | low | clean | rename attribute |
| R7 | class-body `sub` unseen by methods | low | clean | file-scope subs |
| N1 | `--exe` recursion budget + masked error | high (exe) | clean | interpreter-only note |
| N2 | `--exe` block-final if/else value | medium (exe) | via split | explicit `return` |
| N3 | `--exe` sort with `Order` comparator | medium (exe) | via sort | `<=>`-based comparator |
