# The Parser

`Parser::parseProgram()` walks the token vector once, left to right, and
produces a `Program`. It is recursive descent for statements and precedence
climbing for expressions — the classic pairing, chosen because it is the one
that survives contact with an irregular grammar.

It is a **single forward pass**, and it executes nothing.

## Statement dispatch

`parseStatementImpl` is a keyword ladder that only engages when the leading
token is a `Tok::Ident`. It matches the identifier's *text* and delegates:

| Leading word | Goes to |
|---|---|
| `if` / `unless` | `parseIf` |
| `while` / `until` | `parseWhile` |
| `for` | `parseFor` |
| `loop` / `repeat` | the C-style and post-condition loops |
| `sub` / `multi` / `proto` / `method` / `submethod` | `parseSub` |
| `class` / `role` / `grammar` / `module` / `package` | `parseClass` |
| `my` / `our` / `state` / `has` / `constant` | strip the scope word, re-dispatch |
| `enum`, `subset` | `parseEnum`, `parseSubset` |
| `given` / `when` / `default` | the topicalisers |
| `use` / `no` / `need` | `UseStmt` |
| a phaser name | a `Block` tagged with the phaser |

Everything not caught is a bare expression statement:

```cpp
// src/Parser.cpp — the parseStatementImpl fallthrough
auto es = std::make_unique<ExprStmt>();
es->e = parseExpression();
return applyModifiers(std::move(es));
```

Because keywords are matched by text and only in leading position, an `if`
anywhere else is an ordinary identifier.

## Statement modifiers

A trailing `if`, `unless`, `while`, `until`, `for`, `given` or `with` turns the
statement into the corresponding control node and sets a `modifier` flag.
`applyModifiers` recurses, so modifiers chain:

```cpp
// src/Parser.cpp — applyModifiers, the `for` case, abridged
if (isIdent("for")) { advance();
    auto fs = std::make_unique<ForStmt>();
    fs->list = parseExpression();
    fs->modifier = true;      // no implicit block
    fs->body = wrapStmt(std::move(s));
    return applyModifiers(std::move(fs));
}
```

The flag is not cosmetic. `$x for @a` has no implicit block, so a `my`
declaration inside `$x` is *not* scoped away when the loop ends — unlike
`for @a { … }`. Every looping and topicalising node carries the same flag for
the same reason.

## Block or hash?

`{ … }` is Raku's classic ambiguity. The parser uses a small `looksHash`
heuristic: it is a hash if the first token is a pair key followed immediately by
`=>`, or a colon-pair (`:name`, `:$v`, `:!flag`), or a `%`-variable followed by
`,` or `}`. The empty case is decided by **position**:

```cpp
// src/Parser.cpp — a statement-leading '{' is a BLOCK unless looksHash
// src/Parser.cpp — parsePrimary, expression position:
if (a.kind == Tok::RBrace) isHash = true;    // {} in value context: a Hash
```

So a bare `{}` statement is an empty block, and `{}` where a value is expected
is an empty hash. `{ $_ * 2 }` — first token a `$`-variable, no `=>` — is a
block.

There is a second, subtler brace rule. A `}` that closed a **block** at the end
of a line ends the statement, so an infix on the next line starts a new
statement rather than continuing the expression:

```raku
($r, $g, $b) = (…).map: { … }      # the statement ends here
%(r => $r, …)                       # a new statement, not `} % (…)`
```

A subscript's `}` does not count, because `%h{'a'}` followed by a newline and
`+ 3` really is a continuation. That is why the parser records the token
*index* of the last block-closing brace, `lastBlockClose_`, rather than testing
the token kind.

## Expressions: the Pratt core

```cpp
// src/Parser.cpp
ExprPtr Parser::parseExpr(int minbp) {
    ExprPtr lhs = parsePrefix();
    for (;;) {
        InfixInfo in = classifyInfix(cur());
        if (!in.valid || in.lbp < minbp) break;
        advance();
        int nextMin = in.rightAssoc ? in.lbp : in.lbp + 1;
        ExprPtr rhs = parseExpr(nextMin);
        // … build a Binary / Assign / Range / Pair …
    }
    return lhs;
}
```

Read a prefix term; then loop, consuming any infix whose left binding power is
at least `minbp`, recursing for the right-hand side with a bound derived from
the operator's precedence and associativity. For a left-associative operator
the recursive bound is `lbp + 1`, which is what stops it re-associating.

Binding powers are a named enum, higher meaning tighter, and **spaced by ten**:

```cpp
// src/Parser.cpp
enum {
    BP_OR = 10, BP_AND = 20, BP_ZIP = 25, BP_COMMA = 30, BP_ASSIGN = 40,
    BP_TERNARY = 50, BP_OROR = 60, BP_ANDAND = 70, BP_COMPARE = 80,
    BP_RANGE = 90, BP_CONCAT = 100, BP_REPLICATE = 110, BP_ADD = 120,
    BP_MUL = 130, BP_POW = 140, BP_PREFIX = 150
};
```

The gaps are the whole point, and the next chapter spends them.

Prefix operators (`- ! ~ + ? ++ --`) are handled in `parsePrefix`; postfix ones
— `++`, `--`, method calls, subscripts, `.()` — in `parsePostfix`. The two
interleave around the primary term, so `-$obj.foo[0]++` composes in the right
order without a special case.

## Declarations

The declaration parsers do the bulk of the work, because Raku declarations
carry a great deal of information that only matters at run time. None of it is
*checked* during parsing; it is recorded as data for the binder and the
dispatcher.

**`parseSub`** reads the name (or an operator declaration, Chapter 5), the
signature, a trait loop, an optional return type from `-->` / `of` / `returns`,
and the body. It also handles `multi` and `proto`, `method`/`submethod`,
alternate signatures `(a) | (b)` sharing one body, and the immediate-call form
`sub f($n) {…}(1)`.

The trait loop is worth a note. Built-in traits (`is export`, `is native(…)`,
`is rw`, the precedence traits) are consumed into dedicated fields; anything
else is captured as a `SubTraitSpec` — a name and an unevaluated argument
expression — and dispatched at run time to a user-defined
`multi trait_mod:<is>`. That is how a module's `is json-name('x')` reaches its
own handler without the parser knowing anything about it.

**`parseSignature`** builds a `std::vector<Param>`. One `Param` carries a
remarkable number of flags, all of them from Chapter 6's struct: positional or
named, optional or required, the slurpy kind (`*@` flattening, `**@`
non-flattening, `+@` single-arg rule), a default expression, a type constraint,
a `:D`/`:U` smiley, a `where` clause, a coercion type `Int(Str)`, `is rw` /
`is copy` / `is raw`, and a nested sub-signature for destructuring parameters
like `[$a, $b]`.

**`parseClass`** covers `class`, `role`, `grammar`, `module` and `package` in
one function. It parses `is` and `does` parents, then a body of `has`
attributes, `method`/`submethod`/`multi` declarations, and — for grammars —
`token`/`rule`/`regex` rules, whose pattern is the opaque `Tok::RegexLit` the
lexer captured.

**`parseEnum`** and **`parseSubset`** leave their interesting parts as
expressions: the enum's value list and the subset's `where` clause are
evaluated by the interpreter, not the parser.

## String interpolation is parsed, not concatenated

A `"…$x…{ code }…"` literal goes through `parseInterpString(raw)`, which builds
an `InterpStr` node whose `parts` alternate literal `StrLit` chunks with parsed
sub-expressions. It recognises `$x`, `@a[…]`, `%h{…}`, the capture variables
`$/`, `$0`, `$<name>`, backslash escapes including `\x[…]` and `\c[NAME]`, and
`{ EXPR }` blocks.

There is a commit rule for method chains that matches Raku's: `"$obj.foo"`
interpolates the `.foo` only if a call or a subscript follows it; otherwise the
`.foo` stays literal text.

Embedded fragments are parsed by a fresh lexer and parser, which **inherits the
program's user-declared operators**:

```cpp
// src/Parser.cpp — parseEmbeddedExpr
sub.userInfix_ = userInfix_;   sub.userInfixRight_ = userInfixRight_;
sub.userPrefix_ = userPrefix_; sub.userPostfix_ = userPostfix_;
sub.userCircumfix_ = userCircumfix_;
sub.userPostcircumfix_ = userPostcircumfix_;
```

so `"{ 5! }"` sees the program's own `postfix:<!>`.

## Compile time: the parser runs nothing

This is a real divergence from Rakudo and it shapes what Raku++ can support.

- **Phasers** become `Block` statements tagged with a phaser name. `BEGIN` is
  not executed during parsing; the interpreter schedules it after the parse.
- **`constant`** is not folded. It becomes a declaring `VarExpr` that the
  interpreter binds.
- **`use` and `no`** become `UseStmt` nodes. No module is loaded, no pragma
  takes effect, and even `no strict` is handled at run time rather than by
  toggling a parser mode.
- **Named subs are hoisted** — but by the *interpreter*, not the parser.
  `hoistSubs` pre-registers every named `SubDecl` in a scope before running its
  statements, which is why subs need not be declared before use.

The one parse-time side effect in the entire front end is registering a
user-declared operator, which is lexical bookkeeping rather than execution.
`use Foo` participates in exactly that much: `scanModuleOps` finds the module's
source and *text-scans* it for operator declarations, so the rest of the
importing file parses. It does not lex or parse the module (Chapter 29).

## Errors

```cpp
// src/Parser.h
struct ParseError : std::runtime_error {
    int line;
    std::string exType;    // X::Parameter::Twigil, … ("" = generic)
    std::vector<std::pair<std::string, std::string>> exAttrs;
    bool atEof = false;
};
```

A parse error carries a line, and optionally a *typed* diagnostic — an
exception class name plus attributes — so that compile-time failures Roast
checks by class can be reported as that class rather than as a generic message.
The driver prints `===SORRY!===` and exits 1, matching Rakudo's compile-error
shape. `atEof` is the REPL's continuation signal, as in the previous chapter.

`checkRedeclarations` runs after the parse over each scope's statement list and
reports duplicate subs and types in the same scope — one of the few checks that
happens before anything runs.

## Honest limitations

- **Single pass, declared before use, for operators.** A custom operator used
  above its declaration will not parse. Subs are hoisted at run time; operator
  tables are populated textually.
- **A purely symbolic user infix may not be picked up at the use site.** The
  infix use-site branch keys on a `Tok::Ident`, so word-form infixes (`4 avg 6`)
  work; prefix, postfix and circumfix handle symbols fine.
- **The operator vocabulary in the lexer is hand-ordered**, so "longest match"
  is really "first match in a manually ordered table".
- **The block/hash rule is a heuristic.** It implements Raku's documented rules,
  but pathological cases a backtracking grammar would resolve can still surprise
  it.
- **No compile-time execution**, which is why a `BEGIN` block cannot influence
  how later source parses — and, ultimately, why macros and slangs are out of
  scope.
