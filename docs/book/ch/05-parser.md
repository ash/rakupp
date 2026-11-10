# The Parser

`Parser::parseProgram()` walks the token vector once, left to right, and
produces a `Program`. It is recursive descent for statements and precedence
climbing for expressions — the classic pairing, chosen because it is the one
that survives contact with an irregular grammar.

It is a **single forward pass**, and on every path but one it executes nothing.
The exception is a `use` that activates a slang, which runs that module and then
re-lexes what is left of the unit (Chapter 3).

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
StmtPtr s = applyModifiers(std::move(es));
return s;
```

Because keywords are matched by text and only in leading position, an `if`
anywhere else is an ordinary identifier.

## Statement modifiers

A trailing `if`, `unless`, `while`, `until`, `for`, `given` or `with` turns the
statement into the corresponding control node and sets a `modifier` flag.
`applyModifiers` recurses, so modifiers chain:

```cpp
// src/Parser.cpp — applyModifiers, the `while` case
if (kw == "while" || kw == "until") {
    advance();
    auto ws = std::make_unique<WhileStmt>();
    ws->isUntil = (kw == "until");
    ws->modifier = true; // a `my` in STMT leaks to the enclosing scope
    ws->cond = parseExpression();
    ws->body = wrapStmt(std::move(s));
    return applyModifiers(std::move(ws));
}
```

The `for` case sets the same flag and returns without recursing: a loop
modifier may follow a conditional one, `X if A for B`, and nothing follows a
`for`.

The flag is not cosmetic. `$x for @a` has no implicit block, so a `my`
declaration inside `$x` is *not* scoped away when the loop ends — unlike
`for @a { … }`. Every looping and topicalising node carries the same flag for
the same reason.

## Block or hash?

`{ … }` is Raku's classic ambiguity. The parser settles it in one function,
`braceLooksHash`, a heuristic over the tokens after the brace. It is a hash if
the first token is a literal pair key followed immediately by `=>`, or a
colon-pair (`:name`, `:$v`, `:!flag`), or a `%`-variable followed by `,` or
`}`. A computed key counts too, as long as it is a plain postfix chain that
runs up to the arrow: `{ $d.name => 1 }`, `{ %h<k> => 1 }`, `{ +(X) => 1 }`.
Then a second scan can overrule the first: braces that read the topic at their
own level — `$_`, `@_`, a placeholder variable, a `.method` with nothing before
the dot — or that hold a second statement are code after all, so
`{ 3 => 4, :b($_) }` is a block. The empty case is decided by **position**,
which is the function's one parameter:

```cpp
// src/Parser.cpp — braceLooksHash(bool emptyIsHash)
if (a.kind == Tok::RBrace) isHash = emptyIsHash; // {}
```

`parsePrimary` passes `true` and the statement dispatcher `false`. So a bare
`{}` statement is an empty block, and `{}` where a value is expected is an
empty hash. `{ $_ * 2 }` — first token a `$`-variable, no `=>` — is a block.

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
// src/Parser.cpp — parseExpr, reduced to its skeleton
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

The real function is about 1,300 lines, and nearly all of them sit inside that
loop: user infixes and metaoperators over them, adverbs, chained comparisons,
the item-or-list decision for `=`, and the diagnostics for operators that do
not associate. The skeleton is still the control flow.

Binding powers are a named enum, higher meaning tighter, and **spaced by ten**:

```cpp
// src/Parser.cpp
enum {
    BP_OR = 10, BP_AND = 20, BP_ZIP = 25, BP_COMMA = 30, BP_ASSIGN = 40,
    BP_TERNARY = 50, BP_OROR = 60, BP_ANDAND = 70, BP_COMPARE = 80,
    BP_RANGE = 90,
    BP_JUNC_OR = 93, BP_JUNC_AND = 96,
    BP_CONCAT = 100, BP_REPLICATE = 110, BP_ADD = 120, BP_MUL = 130,
    BP_POW = 140, BP_PREFIX = 150
};
```

The gaps are the whole point, and the next chapter spends them. The engine has
spent two itself: the junction constructors went in between the comparisons
and concatenation, `&` tighter than `|` and `^`, as Raku's own precedence table
has them.

Prefix operators (`- ! ~ + ? ++ --`) are handled in `parsePrefix`; postfix ones
— `++`, `--`, method calls, subscripts, `.()` — in `parsePostfix`. The two
interleave around the primary term, so `-$obj.foo[0]++` composes in the right
order without a special case.

## Declarations

The declaration parsers do the bulk of the work, because Raku declarations
carry a great deal of information that only matters at run time. None of it is
*checked* during parsing; it is recorded as data for the binder and the
dispatcher.

**`parseSub`** reads the name (or an operator declaration, Chapter 6), the
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
remarkable number of flags, all of them from Chapter 7's struct: positional or
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
// src/ParserWordPairs.cpp — inheritSyntax, called by parseEmbeddedExpr
p.userInfix_ = userInfix_;
p.userPrefix_ = userPrefix_;
p.userPrefixBp_ = userPrefixBp_;
// …
p.userPostfix_ = userPostfix_;
p.userCircumfix_ = userCircumfix_;
p.userPostcircumfix_ = userPostcircumfix_;
p.userDeclarators_ = userDeclarators_;
```

so `"{ 5! }"` sees the program's own `postfix:<!>`. The same function seeds the
parser of a colon-pair inside a word list, and it carries more than operators:
the names the unit declares as subs where the grammar has a keyword or a word
operator, so that `"{ div 3 }"` calls the unit's own `sub div`.

## Compile time: what the parser runs, and what it does not

This is a real divergence from Rakudo and it shapes what Raku++ can support.

- **Phasers** become `Block` statements tagged with a phaser name. `BEGIN` is
  not executed during parsing; the interpreter schedules it after the parse.
- **`constant`** is not folded. It becomes a declaring `VarExpr` that the
  interpreter binds. The parser reads one off the source text in a single
  case, an operator whose name is spelled through it (`sub infix:[$sym]`),
  because that name has to be in the tables before the next token.
- **`use` and `no`** become `UseStmt` nodes. No ordinary module is loaded, and
  even `no strict` is handled at run time rather than by toggling a parser mode.
  There are two exceptions, and both have to be. `use v6.X` sets `Parser::langRev_`
  as it is parsed, because a language revision changes what the *rest of the
  file* means. `langRev_ >= 2` is what makes prefix `//` a defined-check rather
  than an empty regex, and what the module search consults for 6.e resolution
  rules. A pragma that decides how the following tokens parse cannot wait for
  run time. And `use` of a module that registers a *slang* does load and run it,
  in a scratch interpreter of its own, because there is no way to learn what a
  slang registered without running the code that registers it; the rest of the
  unit is then re-lexed through the tokens it handed back (Chapter 3).
- **Named subs are hoisted** — but by the *interpreter*, not the parser.
  `hoistSubs` pre-registers every named `SubDecl` in a scope before running its
  statements, which is why subs need not be declared before use.

Apart from a slang, the one parse-time side effect in the entire front end is
registering a user-declared operator, which is lexical bookkeeping rather than
execution. `use Foo` participates in exactly that much: `scanModuleOps` finds
the module's source and *text-scans* it for the declarations that change how
the importing file parses — operators first, and the handful of other names
Chapter 6 lists. It does not lex or parse the module (Chapter 33). When what it
found changes how the file *lexes*, the rest of the unit is lexed again.

## Errors

```cpp
// src/Parser.h
struct ParseError : std::runtime_error {
    int line;
    std::string exType;    // X::Parameter::Twigil, … ("" = generic)
    std::vector<std::pair<std::string, std::string>> exAttrs;
    bool atEof = false;
    std::string got;       // the text of the token the parse died on
    std::string file;      // a module that failed during `use`; "" = main
    int col = 0;           // the byte column the excerpt puts a `^` under
};
```

A parse error carries a line, and optionally a *typed* diagnostic — an
exception class name plus attributes — so that compile-time failures Roast
checks by class can be reported as that class rather than as a generic message.
The driver prints `===SORRY!===` and exits 1, matching Rakudo's compile-error
shape, and quotes the source line with a caret under `col`. `got` is what the
language server underlines. `atEof` is the REPL's continuation signal, as in
the previous chapter.

`checkRedeclarations` runs after the parse over each scope's statement list and
reports duplicate subs and types in the same scope — one of the few checks that
happens before anything runs.

## Honest limitations

- **Single pass, declared before use, for operators.** A custom operator used
  above its declaration will not parse. Subs are hoisted at run time; operator
  tables are populated textually.
- **The operator vocabulary in the lexer is hand-ordered**, so "longest match"
  is really "first match in a manually ordered table".
- **The block/hash rule is a heuristic.** It implements Raku's documented rules,
  but pathological cases a backtracking grammar would resolve can still surprise
  it.
- **No compile-time execution on the ordinary path**, which is why a `BEGIN`
  block cannot influence how later source parses, and why macros are out of
  scope. A slang is the exception, and it is a narrow one: `use` of a module
  that registers a slang runs that module in an interpreter of its own and
  re-lexes the rest of the unit through the tokens it registered (Chapter 3).
