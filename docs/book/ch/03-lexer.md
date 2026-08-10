\part{The Front End}

# The Lexer

Raku's grammar is too context-sensitive for a generated tokenizer, so the lexer
is hand-written: a loop that skips whitespace and comments, dispatches on the
current character to a handler, and appends one or a few tokens. The output is
a flat `std::vector<Token>`.

What makes it interesting is not the loop. It is the set of decisions Raku
forces the lexer to make that most languages never have to.

## The token

```cpp
// src/Token.h
enum class Tok {
    End, IntLit, NumLit, StrLit, VersionLit, StrInterp, RegexLit,
    SubstLit, QwList, Ident, Var, LParen, RParen, LBrace, RBrace,
    LBracket, RBracket, Semicolon, Comma, FatArrow, Op
};
struct Token {
    Tok kind = Tok::End;
    std::string text;    // identifier / operator spelling / raw literal
    std::string text2;   // s/// replacement; regex adverb flags
    long long ival; double nval;
    int line, col;
    bool spaceBefore = false;
    bool flag = false;   // S/// — the non-mutating substitution
};
```

Two absences are as informative as the contents.

**There is no keyword kind.** `if`, `sub`, `class`, `given` all arrive as
`Tok::Ident`. The parser decides, from position, whether a given identifier is
acting as a keyword. That is why Raku has no reserved-word list and why `my $if
= 1` is legal.

**There is no operator identity.** Every operator arrives as `Tok::Op`
carrying only its spelling. The lexer knows nothing about precedence,
associativity, or which operators exist beyond a fixed vocabulary of spellings.
All of that is the parser's problem, which is precisely what makes
user-declared operators possible (Chapter 5).

## `spaceBefore`: whitespace is significant

Raku is one of the few languages where a space changes the parse. `f()` is a
call; `f ()` is `f` applied to a parenthesised list. `%h<k>` is a subscript;
`%h <k>` is not. So every token records whether whitespace preceded it:

```cpp
// src/Lexer.cpp — the tokenize loop
size_t before = pos_;
skipWhitespaceAndComments();
bool spaced = (pos_ > before && pos_ != atomDropEnd_) || before == 0;
// … lex the token …
t.spaceBefore = spaced;
```

The flag is true exactly when skipping advanced the cursor. The parser then
leans on it constantly: to tell a postcircumfix from a list, a call from an
application, a postfix operator from a fresh term.

## Regex or division?

A bare `/` starts a regex in term position and divides in operator position.
The lexer decides by looking at the *previous* token:

```cpp
// src/Lexer.cpp — regexContext(), the Tok::Op case
case Tok::Op:
    return pv.text != "++" && pv.text != "--" &&
           pv.text != ">"  && pv.text != "<"  &&
           pv.text != ">>" && pv.text != "<<" &&
           pv.text != ">=" && pv.text != "<=";
```

After most operators a term is expected, so `/` opens a regex. After a value —
a number, a variable, a closing bracket — or after a postfix `++`/`--`,
division follows. After an identifier it is a regex only when the word is in a
small set of keywords that take an expression (`if`, `while`, `say`, `grep`,
`split`, and friends). `//` and `/=` are matched earlier so they lex as
operators.

This is a heuristic in the honest sense: it implements Raku's actual rule for
the cases the rule covers, and a sufficiently perverse program can still be
surprised.

## Quote forms and heredocs

`tryQuoteForm` handles the whole `q`/`qq`/`Q` family plus `m`, `rx`, `s`, `tr`
and `qw`, with adverbs (`:w`, `:to`, `:!c`, …) and **arbitrary delimiters** —
any bracket pair, or any non-bracket character. Whether the result interpolates
is decided here and encoded in the token kind: `qq` produces `Tok::StrInterp`,
`q` produces `Tok::StrLit`. Adverbs that flip a `Q` into interpolating emit a
feature sentinel the parser reads later.

Heredocs cannot be captured inline, because the body is on *later* lines. The
lexer emits an empty-bodied token immediately and defers:

```cpp
// src/Lexer.cpp — tryQuoteForm, on a :to adverb
heredocMarker_ = raw;                // the terminator, e.g. END
heredocInterp_ = (w == "qq");
out = make(heredocInterp_ ? Tok::StrInterp : Tok::StrLit, "");
```

The pending heredoc — marker, token index, interpolation flag — is queued. When
the lexer reaches the newline that ends the current line, `processHeredocs`
reads lines until the marker, dedents them, and **back-patches the token's
text**. Because interpolation was already fixed by the token kind, the patched
body needs no further decision.

Several heredocs can be opened on one line; the queue keeps them in order, each
with its own interpolation-feature string.

## Identifiers, sigils, twigils, Unicode

`lexIdentOrVar` reads a sigil (`$ @ % &`), an optional twigil (`*` dynamic,
`.`/`!` attribute, `^` placeholder, `?` compile-time), then the name — which
may contain `-` or `'`, may be package-qualified with `::`, may be all digits
(`$0`, `$^1`), and may be a Unicode identifier. The whole thing becomes one
`Tok::Var`.

The rule about `-` and `'` inside a name is small and was a genuine bug, so it
lives in the header where everyone who needs it can see it:

```cpp
// src/Lexer.h
inline bool rakuIdentStart(char c) {
    return std::isalpha((unsigned char)c) || c == '_';
}
inline bool rakuIdentJoins(char sep, char next) {
    return (sep == '-' || sep == '\'') && rakuIdentStart(next);
}
```

A `-` continues a name only when a *letter* follows, never a digit. So `elems-1`
is `elems - 1` and `$x-1` is `$x - 1`.

That rule has to be answered in two places: the lexer, and the string
interpolation scanner in the parser. They disagreed. The interpolation scanner
tested `isalnum`, glued the digit into the name, and handed `"$x-1"` to the
expression parser — which re-split it correctly and then interpolated the
*arithmetic result*. So `my $x = 5; say "$x-1"` printed `4` where Rakudo prints
`5-1`. Six sites implemented the rule three different ways; the fix was to
write it once, in a header, and delete the other five.

Superscripts are folded here too: a run like `x³` emits a synthetic tight `**`
operator and an `IntLit 3`, so it parses as `x ** 3`.

## Operators are a fixed, hand-ordered vocabulary

```cpp
// src/Lexer.cpp — lexOperator
for (const char* op : ops) {
    std::string s(op);
    bool ok = true;
    for (size_t k = 0; k < s.size(); k++)
        if (peek(k) != s[k]) { ok = false; break; }
    if (ok) { /* consume s, return Tok::Op */ }
}
// unmatched: a single-character Op
```

This is not a longest-match scanner. It is a manually ordered table where
longer entries are placed before their prefixes, and the first full match wins.
Adding a built-in operator means inserting it at the right position — a real
maintenance hazard, and named as one.

The fall-through matters more than the table: anything unmatched becomes a
**single-character `Tok::Op`**. That is why a novel user-defined operator still
arrives as a token the parser can pick up, even though the lexer has never
heard of it.

The vocabulary also covers Unicode aliases (`≤` for `<=`), hyper metaoperators
(`»op«`), and the set operators (`(elem)`, `∈`) — but it is *static*. It tracks
no user declarations whatsoever.

## Rule bodies are not tokenized

One construct is deliberately opaque. A `token`/`rule`/`regex NAME { … }`
declaration is not lexed as Raku:

```cpp
// src/Lexer.h
bool tryRuleDecl(std::vector<Token>& out, bool spaced);
```

`tryRuleDecl` captures the balanced-brace body **raw** and emits it as a single
`Tok::RegexLit`. Regex syntax is a different sub-language — `<[a..z]>`, `**`,
`%%`, `<?{ … }>` mean nothing to the Raku tokenizer — so keeping it out of the
token stream avoids teaching the lexer two grammars. The regex engine re-lexes
the captured text later, in Chapter 19.

The same applies to a bare `/…/` literal and the `s///` family: the pattern
text travels in `Token::text` and the replacement in `Token::text2`, both
unparsed.

## What else the lexer carries out

Beyond tokens, `Lexer` produces three side channels the parser and the runtime
need:

| Channel | Contents |
|---|---|
| `finishData()` | the text after `=finish`, for `$=finish` |
| `podData()` | rendered `=begin pod` content, for `--doc` |
| `declPod_` / `leadPod_` | `#=` and `#|` declarator documentation, keyed by line |

Declarator pod is keyed by line because that is how it attaches: a `#|` run
immediately above a declaration documents it, a `#=` run immediately below or
beside it does. The parser resolves that association with `leadingPodFor(line)`
and `trailingPodFor(line)`, and records which lines a *parameter* has already
claimed — otherwise the parameter documentation of a multi-line `sub MAIN`
signature would be picked up a second time as the routine's own usage text.

## Errors

A construct that runs off the end of the file gets a dedicated diagnostic:

```cpp
// src/Lexer.h
[[noreturn]] void runawayQuote(const char* construct,
                               const char* finalDelim, int startLine) const;
[[noreturn]] void runawayTerm(const std::string& close,
                              const std::string& open, int startLine) const;
```

Two spellings, because Rakudo has two: the bare quote forms name the
*construct*, the bracketed `q`/`rx`/comment forms name the *terminator*. Both
quote the line the construct *started* on, which is the only coordinate still
meaningful once the scan has eaten the rest of the file.

An `atEof` flag rides along on the resulting error. Only the REPL reads it, to
tell "give me a continuation line" from "this is a syntax error" — which is how
a half-typed string literal at an interactive prompt asks for more input instead
of failing (Chapter 35).
