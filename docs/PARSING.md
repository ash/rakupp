# The Front End — from source text to AST

This is the parsing-side companion to [RUNTIME.md](RUNTIME.md). Where that
document explains how the runtime *runs* a program, this one explains how a
program *becomes* one: how Raku source text is lexed into tokens and parsed into
an AST, how the notoriously context-sensitive bits are handled (regex vs.
division, block vs. hash), and — the part that surprises people — how a
user-defined operator like a factorial `postfix:<!>` gets parsed by a compiler
that is a **single forward pass** with no separate grammar.

Everything here lives in four files: [`Lexer`](../src/Lexer.cpp) +
[`Token.h`](../src/Token.h), and [`Parser`](../src/Parser.cpp) +
[`Ast.h`](../src/Ast.h). The output — a `Program` (a tree of AST nodes) — is the
single artifact all four run modes consume (see [ARCHITECTURE.md](ARCHITECTURE.md)).

The load-bearing mechanisms are shown inline as short excerpts tagged with the
source **file** they come from (`// src/Parser.cpp`); they're lightly trimmed but
otherwise verbatim. File names, not line numbers, are the anchors — grep the
function or the quoted code, since the source moves.

## Contents

- [The shape of it](#the-shape-of-it)
- [Two data models: `Token` and the AST](#two-data-models-token-and-the-ast)
- [The lexer](#the-lexer)
  - [`spaceBefore` — whitespace is significant](#spacebefore--whitespace-is-significant)
  - [Regex vs. division](#regex-vs-division)
  - [Quote forms and heredocs](#quote-forms-and-heredocs)
  - [Identifiers, sigils, twigils, Unicode](#identifiers-sigils-twigils-unicode)
  - [Operators are a fixed vocabulary](#operators-are-a-fixed-vocabulary)
- [The parser](#the-parser)
  - [Statement dispatch](#statement-dispatch)
  - [Statement modifiers](#statement-modifiers)
  - [Block vs. hash](#block-vs-hash)
- [Expression parsing: the Pratt core](#expression-parsing-the-pratt-core)
- [User-defined operators](#user-defined-operators)
  - [How `5!` parses](#how-5-parses)
  - [Precedence and associativity traits](#precedence-and-associativity-traits)
  - [Circumfix brackets, and the single-pass rule](#circumfix-brackets-and-the-single-pass-rule)
- [Declarations](#declarations)
- [String interpolation](#string-interpolation)
- [Compile time: the parser runs nothing](#compile-time-the-parser-runs-nothing)
- [What "in-program grammar modification" does and doesn't cover](#what-in-program-grammar-modification-does-and-doesnt-cover)
- [Driver and errors](#driver-and-errors)
- [Honest limitations](#honest-limitations)

## The shape of it

```
source ─► Lexer::tokenize() ─► vector<Token> ─► Parser::parseProgram() ─► Program (AST)
```

Two design decisions set the character of the front end:

1. **It's hand-written, not generated.** Raku's grammar is too context-sensitive
   for a table-driven parser generator — whether `/` starts a regex, whether `{`
   opens a block or a hash, and whether a space changes the meaning of `f (x)`
   all depend on surrounding state. So both stages are hand-rolled: a
   character-level lexer and a recursive-descent parser with a
   **Pratt (precedence-climbing) core** for expressions.

2. **It's a single forward pass.** The parser walks the token vector once,
   left to right; there is no pre-scan and nothing is executed at parse time.
   That has one big consequence for the interesting features: a user-defined
   operator must be **declared textually before it is used**, because the only
   way the parser learns about it is by reaching its declaration.

## Two data models: `Token` and the AST

The lexer's output is a flat `std::vector<Token>`. A `Token` is deliberately
simple — a kind, the raw text, source position, and a few flags:

```cpp
// src/Token.h
enum class Tok { End, IntLit, NumLit, StrLit, VersionLit, StrInterp, RegexLit,
                 SubstLit, QwList, Ident, Var, LParen, RParen, LBrace, RBrace,
                 LBracket, RBracket, Semicolon, Comma, FatArrow, Op };
struct Token {
    Tok kind = Tok::End;
    std::string text;         // identifier / operator spelling / raw literal
    std::string text2;        // s/// replacement, regex adverb flags
    long long ival; double nval;
    int line, col;
    bool spaceBefore = false; // whitespace/comment preceded this token — significant in Raku
    bool flag = false;
};
```

Two things to note. Keywords are **not** a token kind — `if`, `sub`, `class` all
arrive as `Tok::Ident`, and the parser decides from context whether a given
identifier is a keyword. And operators arrive as a generic `Tok::Op` carrying
only their spelling; the lexer has a *fixed* operator vocabulary and does no
precedence or user-operator work — that's entirely the parser's job.

The parser's output is a tree of `Node`s, each tagged by an `NK` enum (a parallel
to the runtime's `VT`), split into `Expr` and `Stmt` subtrees:

```cpp
// src/Ast.h
enum class NK { IntLit, NumLit, StrLit, InterpStr, VarExpr, Binary, Unary, Call,
                MethodCall, Index, Ternary, Range, Pair, BlockExpr, /* … */
                ExprStmt, VarDecl, SubDecl, IfStmt, ForStmt, Block, ClassDecl,
                EnumDecl, /* … */ };
struct Node { NK kind; int line; virtual ~Node() = default; };
struct Expr : Node { /* … */ };   struct Stmt : Node { std::string label; };
```

Unlike the runtime's fat `Value`, the AST *is* a C++ class hierarchy — `IntLit`,
`Binary`, `SubDecl`, … each a `struct` deriving from `Expr`/`Stmt` with its own
fields (`Binary` has `op`/`lhs`/`rhs`; `SubDecl` has `params`/`body`/`traits`/…).
That's the right tool here: AST nodes are built once, live for the whole run, and
are visited by a `switch (kind)` in both the interpreter and the code generator —
so the polymorphism cost that would hurt a transient `Value` is a non-issue for a
long-lived tree.

## The lexer

`Lexer::tokenize()` is a loop that, each iteration, skips whitespace/comments and
then dispatches on the current character to a handler — `lexNumber`,
`lexIdentOrVar`, `lexQuoted`, `lexOperator`, `tryQuoteForm`, `tryRuleDecl`, … —
appending one (or a few) tokens. The interesting parts are the disambiguations
Raku forces on it.

### `spaceBefore` — whitespace is significant

Raku is one of the rare languages where a space can change parsing: `f()` is a
call, `f ()` is `f` applied to a parenthesized list; `%h<k>` is a subscript,
`%h <k>` is not. So every token records whether whitespace preceded it:

```cpp
// src/Lexer.cpp — tokenize loop
size_t before = pos_;
skipWhitespaceAndComments();
bool spaced = (pos_ > before && pos_ != atomDropEnd_) || before == 0;
// … lex the token …
t.spaceBefore = spaced;
```

`spaceBefore` is true iff skipping actually advanced the cursor. The parser leans
on it constantly to tell a postcircumfix (`$x[0]`, tight) from a list (`$x [0]`),
a call from an application, and a postfix operator from a fresh term.

### Regex vs. division

A bare `/` is a regex in term position but division in operator position. The
lexer decides by looking at the **previous** token:

```cpp
// src/Lexer.cpp — regexContext(), the Tok::Op case
case Tok::Op:
    return pv.text != "++" && pv.text != "--" &&
           pv.text != ">" && pv.text != "<" && pv.text != ">>" && pv.text != "<<" &&
           pv.text != ">=" && pv.text != "<=";   // else a term is expected → regex
```

After most operators a term is expected, so `/` starts a regex; after a value
(a number, a variable, `)`, `]`) or a postfix `++`/`--`, division follows. After
an identifier it's a regex only if the word is in a small keyword set
(`if`, `while`, `say`, `grep`, `split`, …). `//` and `/=` are excluded up front
so they lex as operators.

### Quote forms and heredocs

`tryQuoteForm` handles the whole `q`/`qq`/`Q` family plus `m`/`rx`/`s`/`tr`/`qw`,
with adverbs (`:w`, `:to`, `:!c`, …) and **arbitrary delimiters** — any bracket
pair or a non-bracket char. Whether the result interpolates is decided here:
`qq` → `Tok::StrInterp`, `q` → `Tok::StrLit`; adverbs like `:c`/`:s` can flip a
`Q` into interpolating by emitting a feature sentinel the parser reads later.

Heredocs (`q:to/END/`) can't be captured inline — the body is on *later* lines —
so the lexer emits an empty-bodied token now and defers:

```cpp
// src/Lexer.cpp — tryQuoteForm, on a :to adverb
heredocMarker_ = raw;                 // the terminator (END)
heredocInterp_ = (w == "qq");
out = make(heredocInterp_ ? Tok::StrInterp : Tok::StrLit, "");  // body filled at line end
```

The pending heredoc (marker + token index) is queued, and when the lexer reaches
the newline ending the current line, `processHeredocs` reads lines until the
marker, dedents them, and **back-patches** the token's text. Interpolation is
already fixed by the token kind chosen above.

### Identifiers, sigils, twigils, Unicode

`lexIdentOrVar` reads a sigil (`$ @ % &`), an optional twigil (`*` dynamic, `.`/`!`
attribute, `^` placeholder, `?` compile-time), and the name — which may contain
`-`/`'`, `::` package qualifiers, be all-digits (`$0`, `$^1`), or be a Unicode
identifier (`consumeIdentChars` accepts Unicode letters and combining marks). The
whole thing becomes one `Tok::Var`. Superscript numerals are folded too: a run
like `x³` emits a synthetic tight `**` operator plus an `IntLit 3`, so `x³` parses
as `x ** 3`.

### Operators are a fixed vocabulary

`lexOperator` matches multi-character operators against a **hand-ordered table**,
front to back, longer entries first so the first full prefix match wins (it's not
a true longest-match scanner — the ordering is manual):

```cpp
// src/Lexer.cpp — lexOperator
for (const char* op : ops) {
    std::string s(op);
    bool ok = true;
    for (size_t k = 0; k < s.size(); k++) if (peek(k) != s[k]) { ok = false; break; }
    if (ok) { /* consume s, return Tok::Op */ }
}
// … unmatched: a single-char Op
```

Anything unmatched falls through to a **single-character `Tok::Op`** — which is
exactly why a novel user operator still surfaces as a token the parser can pick
up. The lexer also knows Unicode operator aliases (`≤`→`<=`), hyper metaops
(`»op«`), and set operators (`(elem)`, `∈`), but this vocabulary is *static* — it
tracks no user declarations.

One more special case: `token`/`rule`/`regex NAME { … }` bodies are **not**
tokenized as normal Raku. `tryRuleDecl` captures the balanced-brace body raw and
emits it as a single `Tok::RegexLit`, opaque to the token stream — the regex
engine re-lexes it later. Rule bodies are a different sub-language, kept out of
the main token flow.

## The parser

`Parser::parseProgram()` loops to end-of-input, parsing one statement at a time
and requiring a `;` or a statement boundary between them.

### Statement dispatch

`parseStatementImpl` is a keyword ladder that only engages when the leading token
is a `Tok::Ident` — it matches the identifier's *text* (`if`, `while`, `for`,
`sub`, `class`, `my`, `given`, `return`, phaser names, …) and delegates to the
matching parser (`parseIf`, `parseSub`, `parseClass`, …). A scope word
(`my`/`our`/`state`/`has`) strips itself and re-dispatches. Everything not caught
is a **bare expression statement**:

```cpp
// src/Parser.cpp — parseStatementImpl fallthrough
auto es = std::make_unique<ExprStmt>();
es->e = parseExpression();
return applyModifiers(std::move(es));
```

Because keywords are matched by text and only in leading position, there's no
reserved-word list — `if` used anywhere but the front of a statement is just an
ordinary identifier.

### Statement modifiers

A trailing `if`/`unless`/`while`/`until`/`for`/`given`/`with` turns the statement
into the corresponding control node with a `modifier` flag. `applyModifiers`
wraps it, and recurses so modifiers can chain:

```cpp
// src/Parser.cpp — applyModifiers (the `for` case, abridged)
if (isIdent("for")) { advance();
    auto fs = std::make_unique<ForStmt>();
    fs->list = parseExpression();
    fs->modifier = true;              // no implicit block: a `my` in EXPR leaks out
    fs->body = wrapStmt(std::move(s));
    return applyModifiers(std::move(fs));
}
```

The `modifier` flag matters at runtime: `$x for @a` has no implicit block, so a
declaration in `$x` is *not* scoped away — different from a `for @a { … }` block.

### Block vs. hash

`{ … }` is Raku's classic ambiguity — code block or hash literal? The parser uses
a small `looksHash` heuristic: it's a hash if the first token is a pair key
followed immediately by `=>`, or a colon-pair (`:name`, `:$v`, `:!flag`), or a
`%`-variable followed by `,`/`}`. Crucially, **empty `{}` is decided by
position**:

```cpp
// src/Parser.cpp — statement-leading '{' : NOT looksHash ⇒ block
// (so a bare `{}` statement is an empty BLOCK)

// src/Parser.cpp — parsePrimary, expression-position '{' :
if (a.kind == Tok::RBrace) isHash = true;   // {} in value context is an empty HASH
```

So `{}` on its own is an empty block, but `{}` where a value is expected is an
empty hash — matching Raku. And `{ $_ * 2 }` (first token a `$`-var, no `=>`) is
a block, as expected.

## Expression parsing: the Pratt core

Expressions are parsed by precedence climbing. `parseExpr(minbp)` reads a prefix
term, then loops consuming any infix operator whose binding power is `>= minbp`,
recursing for the right-hand side with a bound set by the operator's precedence
and associativity:

```cpp
// src/Parser.cpp
ExprPtr Parser::parseExpr(int minbp) {
    ExprPtr lhs = parsePrefix();
    for (;;) {
        InfixInfo in = classifyInfix(cur());
        if (!in.valid || in.lbp < minbp) break;
        advance();
        int nextMin = in.rightAssoc ? in.lbp : in.lbp + 1;   // left-assoc: lbp+1 blocks re-association
        ExprPtr rhs = parseExpr(nextMin);
        // … build a Binary / Assign / Range / Pair from op, lhs, rhs …
    }
    return lhs;
}
```

Binding powers are a named enum, "higher = tighter", **spaced by 10** on purpose:

```cpp
// src/Parser.cpp — binding powers (higher = tighter)
enum {
    BP_OR = 10, BP_AND = 20, BP_ZIP = 25, BP_COMMA = 30, BP_ASSIGN = 40, BP_TERNARY = 50,
    BP_OROR = 60, BP_ANDAND = 70, BP_COMPARE = 80, BP_RANGE = 90,
    BP_CONCAT = 100, BP_REPLICATE = 110, BP_ADD = 120, BP_MUL = 130, BP_POW = 140, BP_PREFIX = 150
};
```

The gap of 10 between levels is the hook for user-defined precedence: an
`is tighter`/`is looser` trait slots a fresh operator *between* two built-in
levels (below). `classifyInfix` maps each built-in operator spelling to its level
(`+`/`-` → `BP_ADD`, `**` → `BP_POW` right-assoc, assignments → `BP_ASSIGN`
right-assoc, …). Prefix operators (`- ! ~ + ? ++ --`) are handled by `parsePrefix`
and postfix ones (`++ --`, method calls, subscripts, `.()`) by `parsePostfix`,
which interleave around the primary term.

## User-defined operators

This is the part that looks impossible for a single-pass parser and isn't. The
parser carries a set of **live operator tables** that grow as declarations are
parsed:

```cpp
// src/Parser.h — mutated during the parse, consulted at every operator site
std::map<std::string,int> userInfix_;        // name → left binding power
std::set<std::string> userInfixRight_;       // declared `is assoc<right>`
std::set<std::string> userPrefix_, userPostfix_;
std::map<std::string,std::string> userCircumfix_, userPostcircumfix_;  // open → close
```

When `parseSub` reads a sub whose name is `infix`/`prefix`/`postfix`/`circumfix`/
`postcircumfix` followed by `:`, it pulls the operator name out of the `<…>` and
**registers it into those tables on the spot**:

```cpp
// src/Parser.cpp — parseSub, operator declaration
if ((s->name == "infix" || s->name == "prefix" || s->name == "postfix" ||
     s->name == "circumfix" || s->name == "postcircumfix") && isOp(":")) {
    std::string cat = s->name; advance();              // ':'
    std::vector<std::string> w;
    if (isOp("<")) { advance(); w = readAngleWords(">"); }   // the operator's name(s)
    std::string opname = w.empty() ? "" : w[0];
    // … circumfix stores an open→close pair; otherwise:
    s->name = cat + ":<" + opname + ">";
    if (cat == "infix")   { userInfix_[opname] = BP_ADD; declInfix = opname; }  // default precedence
    else if (cat == "prefix")  userPrefix_.insert(opname);
    else if (cat == "postfix") userPostfix_.insert(opname);
}
```

From that point on in the token stream, the operator is live. That's the whole
trick: because it's one forward pass, registering the operator mid-parse means
every *later* use sees it — and a use *before* the declaration doesn't, which is
why Raku requires the declaration first.

### How `5!` parses

Take `sub postfix:<!>($n) { [*] 1..$n }; say 5!;`. The declaration runs the branch
above, doing `userPostfix_.insert("!")`. Later, parsing `5!`: `parseExpr` →
`parsePrefix` → `parsePostfix(parsePrimary())`. `parsePrimary` returns the `5`
literal; `parsePostfix` loops and hits the user-postfix branch:

```cpp
// src/Parser.cpp — parsePostfix
} else if ((cur().kind == Tok::Op ||
            (cur().kind == Tok::Ident && !cur().spaceBefore)) &&
           userPostfix_.count(cur().text)) {
    // user-defined postfix:  5!  ==  postfix:<!>(5)
    std::string opname = advance().text;
    auto call = std::make_unique<Call>();
    call->name = "postfix:<" + opname + ">";
    call->args.push_back(std::move(base));
    base = std::move(call);
}
```

So `5!` becomes a `Call` to `postfix:<!>` with `5` as its argument — an ordinary
call node, which the runtime later dispatches to the user's sub (subs live under
a `&`-prefixed name; `postfix:<!>` is just such a name). Infix operators work the
same way via a dedicated branch at the top of the `parseExpr` loop, building a
`Call` named `infix:<op>` with two arguments and honoring the declared binding
power and associativity.

### Precedence and associativity traits

By default a user infix registers at `BP_ADD`. A precedence trait on the
declaration adjusts it, resolving the referenced operator's level and slotting
`±5` — right into the gap the by-10 spacing left open:

```cpp
// src/Parser.cpp — parseSub, `is tighter/looser/equiv(&infix:<+>)`
int refBp = infixBpOf(refOp);
userInfix_[declInfix] = trait == "equiv"   ? refBp
                      : trait == "tighter" ? refBp + 5
                      :                      refBp - 5;   // looser
// `is assoc<right>` →
if (kind == "right") userInfixRight_.insert(declInfix);
```

So `sub infix:<avg> is tighter(&infix:<+>) { … }` gives `avg` a binding power of
125 — tighter than `+` (120), looser than `*` (130) — and `4 avg 6 + 10` parses
as `(4 avg 6) + 10`. `infixBpOf` resolves a referenced operator's level whether
it's built-in or itself user-defined.

### Circumfix brackets, and the single-pass rule

A `circumfix:<「 」>` (or `postcircumfix`) registers an **open→close** mapping;
`parsePrimary` (circumfix) and `parsePostfix` (postcircumfix) consult those maps
to parse a custom bracket as a `circumfix:<「 」>` call. The classifiers that
decide "does this token start a term / a list-op argument" also consult the user
tables, so a custom prefix or circumfix opener is recognized as the start of an
argument:

```cpp
// src/Parser.cpp — startsTermToken / startsListopArg, Tok::Op case ends with:
userPrefix_.count(t.text) || userCircumfix_.count(t.text);
```

The single-pass, declared-before-use rule is the honest cost of this simplicity —
there is no separate "collect all operator declarations first" phase. The one
escape hatch is `EVAL`: `declareUserOp(kind, name)` lets the evaluator pre-seed
the tables before parsing a snippet, so a program can define an operator and then
`EVAL` a string that uses it. String interpolation gets this for free —
`parseEmbeddedExpr` (below) copies the live tables into its sub-parser, so
`"{ 5! }"` sees the program's `postfix:<!>`.

## Declarations

The declaration parsers all build the rich AST nodes from [`Ast.h`](../src/Ast.h)
that the runtime later interprets:

- **`parseSub`** — reads the name (or operator declaration above), the signature,
  a trait loop (`is export`, `is native(...)`, the precedence traits, and arbitrary
  `is Foo(bar)` captured for runtime `trait_mod:<is>` dispatch), an optional
  `-->`/`of`/`returns` return type, and the body. It also handles `multi`/`proto`,
  `method`/`submethod`, alternate signatures `(a) | (b)` sharing one body, and
  `sub f(...){...}(args)` immediate calls.
- **`parseSignature`** — builds a `std::vector<Param>`, one `Param` per parameter,
  filling its many flags: positional/named, optional/required, slurpy kind
  (`*@`/`**@`/`+@`), defaults, type constraint, `:D`/`:U` smiley, `where` clause,
  coercion (`Int(Str)`), `is rw`/`is copy`, and destructuring sub-signatures
  (`[$a, $b]`, stored recursively in `Param::subSig`). None of these are checked
  at parse time — they're data for the runtime's binder and multi-dispatcher.
- **`parseClass`** covers `class`/`role`/`grammar`/`module`/`package` in one
  function. It parses `is`/`does` parents, then a body of `has` attribute
  declarations (→ `AttrDecl`), `method`/`submethod`/`multi` (→ `SubDecl` in
  `methods`), and — for grammars — `token`/`rule`/`regex` rules (→
  `GrammarRuleDecl`, whose pattern is the opaque `RegexLit` the lexer captured).
- **`parseEnum`** and **`parseSubset`** produce `EnumDecl`/`SubsetDecl`; the enum's
  values and the subset's `where` are left as expressions for the runtime.

## String interpolation

A `"…$x…{ code }…"` literal is parsed, not concatenated blindly.
`parseInterpString(raw)` produces an `InterpStr` whose `parts` alternate literal
`StrLit` chunks with parsed sub-expressions. It recognizes `$x`, `@a[…]`, `%h{…}`,
`$/`/`$0`/`$<name>` captures, backslash escapes (`\n`, `\x[…]`, `\c[NAME]`), and
`{ EXPR }` blocks — with a *commit rule* for method chains: `"$obj.foo"` only
interpolates the `.foo` if it's followed by a call or subscript, otherwise the
`.foo` stays literal text (Raku's rule).

The embedded fragments are parsed by a fresh lexer+parser, which **inherits the
program's user operators** so custom operators work inside interpolations:

```cpp
// src/Parser.cpp — parseEmbeddedExpr copies the live tables into the sub-parser
sub.userInfix_ = userInfix_; sub.userInfixRight_ = userInfixRight_;
sub.userPrefix_ = userPrefix_; sub.userPostfix_ = userPostfix_;
sub.userCircumfix_ = userCircumfix_; sub.userPostcircumfix_ = userPostcircumfix_;
```

## Compile time: the parser runs nothing

A crucial difference from Rakudo: **the Raku++ parser executes no user code.**
It is strictly single-pass and produces an AST; that's all. In particular:

- **`BEGIN` and other phasers** become `Block` statements tagged with a `phaser`
  string. They are *not* run during parsing — the interpreter schedules them
  after the parse (`BEGIN` in source order, `CHECK` reversed, `INIT`, `ENTER`
  around the mainline, `END` on exit).
- **`constant`** is not folded at parse time; it becomes a declaring `VarExpr`
  bound by the interpreter.
- **`use`/`no`** become `UseStmt` nodes; the parser does not act on pragmas
  (module loading and `use v6.e` language-revision selection happen when the
  interpreter executes the node — even `no strict` is handled at runtime, not by
  toggling a parser mode).
- **Named subs are hoisted** — but by the *interpreter*, not the parser.
  `hoistSubs` pre-registers every named `SubDecl` in a scope before running its
  statements, so a sub is callable across its whole enclosing scope regardless of
  textual order. (This is why *subs* don't need to be declared before use, but
  *operators* do — operator visibility is a parse-time fact, sub visibility a
  runtime one.)

The **one** parse-time side effect is the operator-table registration described
above — and that's lexical bookkeeping, not execution. So the pipeline is: a
single-pass parse to an AST, then the interpreter does its own hoist + phaser
pass ([RUNTIME.md](RUNTIME.md) and [ARCHITECTURE.md](ARCHITECTURE.md) cover what
happens next).

## What "in-program grammar modification" does and doesn't cover

Putting the pieces together, here's the line Raku++ draws on changing the
language from inside a program:

**Supported** (all via the mechanisms above):
- Custom **infix / prefix / postfix** operators (`sub infix:<…>` etc.), with
  **precedence** (`is tighter`/`looser`/`equiv`) and **associativity**
  (`is assoc<right>`).
- Custom **circumfix / postcircumfix** brackets (`circumfix:<「 」>`).
- Lexical **named regexes** (`my regex Foo { … }`), callable as `<Foo>`.
- Operators are visible inside string interpolations and pre-seedable for `EVAL`.

**Not supported** — the genuinely grammar-*rewriting* features:
- **Macros** / `RakuAST` / `quasi` — no compile-time AST manipulation.
- **Slangs** — you can't swap in a different sub-grammar for a lexical scope.
- Arbitrary **grammar mutation** of the Raku grammar itself.

The reason is exactly the single-pass, table-based design: adding an entry to
`userInfix_` is cheap and local, so *operators* are easy; but macros and slangs
require the parser to run user code and rewrite its own grammar mid-parse, which
this front end deliberately doesn't do. That keeps the language rakupp accepts
static enough to also compile ahead of time (the `--aot`/`--exe` modes in
[ARCHITECTURE.md](ARCHITECTURE.md)) — the constructs that *would* need a live
compiler (grammars) are exactly the ones those modes fall back to bundling.

## Driver and errors

Lexing and parsing are wired together at the entry point:

```cpp
// src/Runtime.cpp — rakuppRun
Lexer lexer(src);
auto tokens = lexer.tokenize();
Parser parser(std::move(tokens));
parser.declPod_ = lexer.declPod_;    // `#=` declarator descriptions
Program prog = parser.parseProgram();
Interpreter interp;
return interp.run(prog);
```

The lexer also hands over `finishData()` (the `=finish` / data section) and the
`$=pod` DOM. A `ParseError` (carrying a line number) is thrown on a syntax error
and reported as `===SORRY!===` with exit 1, mirroring Rakudo's compile-time error
format. `--dump-ast` runs the same lex+parse and prints the tree via `dumpAst`.

## Honest limitations

- **Single-pass, declared-before-use for operators.** A custom operator used
  above its declaration won't parse. Subs are hoisted (runtime), but operator
  tables are populated textually.
- **User infixes are recognized in word form.** The infix use-site branch keys on
  a `Tok::Ident`, so a *word* infix (`4 avg 6`) works; a purely *symbolic*
  user infix is registered but may not be picked up at the use site. (Prefix,
  postfix, and circumfix operators handle symbols fine.)
- **The operator vocabulary in the lexer is fixed** and hand-ordered; the
  "longest match" is really "first match in a manually-ordered table," so a new
  built-in operator has to be inserted at the right spot.
- **The block/hash heuristic is a heuristic.** It follows Raku's documented rules
  (pair-key, colon-pair, `%`-var; empty `{}` by position), but pathological cases
  that a full grammar would resolve by backtracking can still surprise it.
- **No compile-time execution.** `BEGIN` blocks and `constant` initializers run in
  the interpreter's post-parse pass, not during parsing — fine for the common
  cases, but it means a `BEGIN` cannot influence how *later source* is parsed
  (which is part of why macros/slangs aren't supported).

For how the AST these produce is executed, see **[RUNTIME.md](RUNTIME.md)**; for
the four things done *with* it (interpret, bundle, AOT, native compile), see
**[ARCHITECTURE.md](ARCHITECTURE.md)**.
