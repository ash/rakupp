# How the JS showcase does semicolons (ASI)

JavaScript lets you leave semicolons out: a newline ends a statement wherever
the language's [Automatic Semicolon
Insertion](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Lexical_grammar#automatic_semicolon_insertion)
rules say a statement can end. The [`js.raku`](js.raku) showcase supports this.
This note explains how, because the obvious approaches don't work here and the
implementation went through several non-obvious corrections worth recording.

The whole feature lives in one pre-pass, `insert-asi`, plus a two-word grammar
guard. No evaluator changes.

## The problem: the grammar can't see newlines

The parser is a Raku `grammar`. Its statement rules end in a literal `';'`:

```raku
rule statement:sym<expr> { <expr> ';' }
rule statement:sym<return> { <.kw('return')> <expr>? ';' }
```

A `rule` matches whitespace between atoms with the built-in `ws` token, and
`ws` eats *all* whitespace, newlines included. So by the time a statement rule
runs, the line breaks are gone — the grammar literally cannot tell
`a = 1 \n b = 2` from `a = 1 b = 2`. To make ASI work in the grammar we would
need newlines to be significant, which means overriding `ws`.

That is the normal way to do it in Raku, and it is blocked by a rakupp bug:
**a user-defined `token ws` is ignored — `rule` sigspace always uses the builtin**
(bug G2 in [`docs/dev/BUGS-JS-SHOWCASE.md`](../../docs/dev/BUGS-JS-SHOWCASE.md)).
So we can't make newlines significant inside the grammar at all.

## The approach: insert real semicolons before parsing

Since the grammar can't see newlines, we give it a program that doesn't need
to. `insert-asi` runs after comment stripping and **inserts a literal `;` at
every point a statement terminator is required** — statement-ending line
breaks, before a block-closing `}`, and at end of input. The grammar is then
handed fully-semicoloned source and stays exactly as it was.

```
source ──▶ strip-comments ──▶ insert-asi ──▶ grammar ──▶ AST ──▶ eval
```

This mirrors the existing `strip-comments` pass (comments are blanked in place,
newlines preserved). `insert-asi` is a single left-to-right character scan;
its output is the input verbatim plus inserted `;`. You can see the transform:

```sh
build/rakupp showcase/js/js.raku --asi=file.js
```

## The core rule

At each newline the scanner asks: *does a statement end here?* It inserts `;`
unless the break is a continuation. The decision (`asi-should-insert`) is:

Insert a `;` **iff** all of these hold:

1. the **last token can end a statement** (a value, `)`, `]`, an object/expr
   `}`, `++`/`--`, or a bare `return`/`break`/`continue`) — not an operator,
   `.`, `(`, a keyword expecting more, etc.;
2. we are **not inside `(…)` or `[…]`** (those are always expression
   continuations);
3. we are **not directly inside an object literal**;
4. the **next token does not continue** the statement — it's not `.`, a binary
   operator, `)`, `,`, `else`/`catch`/`finally` after a block, and so on.

This is JavaScript's own heuristic — "end the statement at the line break unless
the next thing obviously continues it" — expressed as a last-token / next-token
/ nesting test.

### The token alphabet

The scanner doesn't build a full token list; it just remembers the previous
significant token symbolically (`$lastTok`) and peeks the next one
(`asi-peek`). Sentinels are chosen so they can't collide with real
identifiers:

| symbol | means | can end a stmt? |
|---|---|---|
| a word, e.g. `foo`, `return` | identifier or keyword | yes, unless a non-ending keyword |
| `#num` / `#str` | number / string / template literal | yes |
| `#op` | any operator run (`=`, `+`, `===`, `&&`, …) | no |
| `=>` | arrow | no |
| `)` | close of an expression paren | yes |
| `)ctrl` | close of an `if`/`for`/`while` header | no (a body follows) |
| `]` | close of an index/array | yes |
| `}` | close of an object literal or function/arrow body | yes |
| `}block` | close of a statement block (function decl, class, `if`…) | no |
| `++` / `--` | postfix | yes |
| `(` `[` `{` `.` `,` `;` | structural | no |

`return`/`break`/`continue` are *ending* words on purpose: a bare one at a line
end terminates the statement. That is JS's "restricted production" rule —
`return`↵`42` becomes `return; 42;`, returning `undefined`, exactly as in a
browser.

## The hard cases

The core rule is simple; getting the `{ }` boundaries right was not. Each of
these was a real failure caught by diffing against Node, then fixed.

### 1. Object vs. block `{` — the `function` regression

First cut: treat every `}` as statement-ending. Then `function f() { … }`
became `function f() { … };`, and the grammar's longest-match parsed
`function f(){…};` as an **expression** statement (a function *expression*
followed by an empty statement) rather than a **declaration** — so functions
silently weren't defined (`ReferenceError: fib is not defined`).

The fix is that `}` is not one thing. A `{` opens one of three kinds of brace,
tracked on a stack:

- **`object`** — an object literal (`{` in expression position). No `;` between
  its members; a `;` *may* follow its `}` (the enclosing statement can end).
- **`block`** — a statement block: a function/class **declaration** body, an
  `if`/`for`/`while` block, a bare block. Needs `;` between its inner
  statements; **no** `;` after its `}` (which would break a declaration, or
  detach an `else`).
- **`exprfn`** — a function-expression or arrow body. Needs `;` between inner
  statements **and** a `;` may follow its `}` (the statement `const f = () => {…}`
  ends there).

`{` is classified by the token before it: expression position (`(`, `[`, `,`,
an operator, `return`) → object; after `=>` → exprfn; a `function` keyword in
expression position sets its body to exprfn (tracked with `$pendingBrace`);
otherwise → block. On `}` the popped kind decides `$lastTok`: `}` (endable) for
object/exprfn, `}block` (not endable) for block.

### 2. IIFEs — bracket depth must be brace-local

`const f = (function () { … })()` put the function body inside a `(`, so the
"suppress inside `(…)`" rule (2 above) killed **every** insertion in the body.
Nothing inside the IIFE got a semicolon.

Fix: paren/bracket depth is **brace-local**. On every `{` the current
`(round, square)` counts are pushed and reset to zero; on the matching `}` they
are restored (`@depths`). The parens enclosing a function body are irrelevant
to where statements end inside it.

### 3. `enum` bodies look like blocks but act like objects

`enum Color { Red, Green }` — the `{` follows a name, so it classified as a
block, and the scanner inserted `;` before `}` (`{ Red, Green; }`), which the
enum rule rejects. Fix: the `enum` keyword marks the next `{` as `object`
(its members are comma-separated, like an object literal).

### 4. `do … while` needs its trailing `;`

`do { … } while (cond)` is a single statement ending after the `)`, but that
`)` had been tagged `)ctrl` (a *control* paren, "a body follows") — so no `;`
was inserted and `while (cond) console.log(…)` merged into the next line. The
`while` of a do-while is different from the `while` that starts a loop. Fix:
push each `do`'s brace depth on `@doDepths`; when a `while` appears at that same
depth right after the do's block, its paren is an ordinary (statement-ending)
one, not a control paren.

### 5. `else`/`catch`/`finally` — block vs. unbraced body

`else` binds back to its `if`, so a `;` must not be inserted before it after a
braced body (`if (x) {…}`↵`else` must not become `…{}; else`). But after an
*unbraced* body it must (`if (x) foo()`↵`else` needs `foo()` terminated — Node
inserts there). Fix: suppress before `else`/`catch`/`finally` only when the
previous token is `}block`. Infix keywords (`in`, `of`, `instanceof`) always
continue and are always suppressed.

### 6. The bare `return;` tie — a two-word grammar guard

With ASI emitting `return;` (case 1's restricted production), that bare
statement tied with parsing `return` as an *identifier* expression, and rakupp
resolved the tie to the identifier (proto longest-match, bug G3). So the one
change ASI needed outside the pre-pass: `<ident>` no longer matches the three
reserved words that can stand alone as statements —

```raku
token ident { <!rword> <[A..Za..z_$]> <[A..Za..z0..9_$]>* }
token rword { [ 'return' | 'break' | 'continue' ] <!before <[A..Za..z0..9_$]>> }
```

## What it deliberately does not do

It is a heuristic, not the full ECMAScript algorithm, and it matches JS by
*staying out of the way* in the ambiguous case. The classic gotcha —

```js
a = b
(c).d()
```

— is one statement in JavaScript (`a = b(c).d()`), because `(` continues the
expression. `insert-asi` suppresses before a leading `(`/`[` too, so it also
treats this as one statement: same result as Node, surprising as that is. The
lesson JS teaches (put the semicolon in, or don't start a line with `(`) applies
here unchanged.

Out of scope entirely: object/array destructuring patterns outside `for…of`,
`switch`, and labels — none are supported by the interpreter, so the pre-pass
doesn't try to reason about their braces.

## How it was validated

The test is differential: run the same **semicolon-free** source through Node
and through `js.raku` and diff. Because Node has real ASI, identical output
means the pre-pass agreed with a browser.

- All six `.js` examples produce byte-identical output with **and** without
  semicolons (ASI is idempotent — fully-semicoloned code is unchanged).
- All four `.ts` examples run identically semicolon-free.
- A dedicated edge suite (restricted `return`, method chains, `do`/`while`,
  `if`/`else` and Allman braces on separate lines, the `a = b`↵`(c)` gotcha)
  and a torture test (multi-line object/array literals, classes, arrow-returning-
  object `() => ({…})`, ternaries across lines, postfix at line end, multi-line
  template literals, nested functions, `for…of` destructuring) both match Node.

## Where the code is

All in [`js.raku`](js.raku):

- `insert-asi` — the scanner (called from `parse-js`, after `strip-comments`).
- `asi-should-insert` / `asi-ending` / `asi-exprpos` / `asi-peek` — the decision
  helpers.
- `token ident` / `token rword` — the reserved-word guard.
- `MAIN(:$asi)` — the `--asi=file` debug dump.
