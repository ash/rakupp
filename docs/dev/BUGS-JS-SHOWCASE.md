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

### G1. A `grammar` definition leaks parser state into the following block — HIGH

The single sharpest one. After a small `grammar`, the *next* routine/block whose
body contains a `;`-terminated statement fails to parse:

```raku
grammar Q { token TOP { \d+ } }
sub f { my $x = 1; say $x; }      # ===SORRY!=== Parse error: Confused (got '}')
f();
```

Expected: parses and prints `1` (Rakudo does). Actual: the closing `}` of `f`
is rejected. Reliably triggered by *any* minimal grammar; a block with no
`;`-terminated statement (`sub f { say 1 }`) is immune, as is an `if`/`unless`
modifier statement.

Curiously, `showcase/js/js.raku`'s own large grammar does **not** trigger it —
its ~120-line grammar (parameterized tokens, proto rules, `%` modifiers) leaves
clean state, while `grammar Q { token TOP { \d+ } }` does not. Truncated
prefixes of the js grammar are also safe. That order/content sensitivity points
at leaked or uninitialised parser/lexer state at the `grammar`-block boundary
rather than anything about the following `sub` — it is not reset deterministically
when the grammar is small.

- **Where to look:** the grammar-body parse exit in `src/Parser.cpp` /
  `src/Regex.cpp` — whatever lexer mode (regex vs. code) or brace-depth counter
  the grammar declarator sets is likely not fully restored, and a larger grammar
  happens to restore it as a side effect.
- **Workaround:** none needed in js.raku (its grammar is large enough); a
  minimal reproducer must avoid `;`-terminated statements in the first block
  after the grammar. This is the highest-value fix here because it is a silent
  trap for anyone writing a small grammar plus a helper sub.

### G2. User-defined `token ws` is ignored by `rule` sigspace — HIGH

```raku
grammar W { token ws { [ \s | '#' \N* ]* } rule TOP { 'a' 'b' } }
say W.parse("a # comment\n b").defined;   # rakupp: False — Rakudo: True
```

`rule` always injects the builtin whitespace matcher; overriding `ws` (the
standard way to make a grammar skip comments) has no effect.

- **Where to look:** how sigspace resolves `<.ws>` in `src/Regex.cpp` — it
  binds the builtin rather than resolving `ws` in the grammar's method table.
- **Workaround:** `showcase/js/js.raku` strips comments in a pre-pass
  (`strip-comments`, blanking `//…` and `/*…*/` while preserving newlines)
  before the grammar ever sees the source. Retiring this needs both `ws`
  override support and the ability to write a comment-skipping `ws`.

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
branch never wins on a tie.

- **Where to look:** the proto/LTM tie-break in `src/Regex.cpp` /
  `src/Interpreter.cpp` (see also `docs/dev/DISPATCH.md`). A literal prefix
  should outrank an open character-class match of equal length.
- **Workaround:** js.raku doesn't classify keywords in the grammar at all — it
  parses every keyword-or-name as `<ident>` and sorts out `null`/`true`/`this`
  etc. in the `primary:sym<ident>` action.

### G4. Unbalanced `{`/`}` inside a regex breaks the source lexer — MEDIUM

A brace inside a regex assertion or string — even correctly quoted or escaped —
confuses the file lexer:

```raku
grammar B { token t { '$' <!before '{'> } token TOP { <t> 'x' } }
# ===SORRY!=== Parse error: expected } (got '')
```

The lexer counts the `{` toward the enclosing block depth instead of treating it
as regex content.

- **Where to look:** brace/blocks tracking while scanning a regex literal in
  `src/Lexer.cpp` — regex interior braces must not affect code-block nesting.
- **Workaround:** js.raku writes `<!before \x7B>` (hex escape) instead of
  `<!before '{'>` in the template-literal token.

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

| # | Summary | Severity | Repro | js.raku workaround |
|---|---|---|---|---|
| G1 | `grammar` leaks parse state into next `;`-block | high | clean | none needed (large grammar) |
| G2 | user `token ws` ignored by `rule` | high | clean | strip comments pre-pass |
| G3 | proto LTM tie → identifier branch | medium | clean | classify keywords in action |
| G4 | brace in regex breaks lexer | medium | clean | `\x7B` escape |
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
