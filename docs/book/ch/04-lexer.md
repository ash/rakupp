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
    size_t off = 0;      // byte offset just past the token, in the source
    bool spaceBefore = false;
    bool flag = false;   // S/// — the non-mutating substitution
    int outerScopes = 0; // a heredoc whose marker line closed blocks
    bool heredoc = false;
};
```

`off` is the newest field and the one the tooling leans on. It is what lets a
`use` re-lex the rest of the file from the byte just past itself (Chapter 3),
and what the formatter and the language server map a token back to its source
with.

Two absences are as informative as the contents.

**There is no keyword kind.** `if`, `sub`, `class`, `given` all arrive as
`Tok::Ident`. The parser decides, from position, whether a given identifier is
acting as a keyword. That is why Raku has no reserved-word list and why `my $if
= 1` is legal.

**There is no operator identity.** Every operator arrives as `Tok::Op`
carrying only its spelling. The lexer knows nothing about precedence,
associativity, or which operators exist beyond a fixed vocabulary of spellings.
All of that is the parser's problem, which is precisely what makes
user-declared operators possible (Chapter 6).

## `spaceBefore`: whitespace is significant

Raku is one of the few languages where a space changes the parse. `f()` is a
call; `f ()` is `f` applied to a parenthesised list. `%h<k>` is a subscript;
`%h <k>` is not. So every token records whether whitespace preceded it:

```cpp
// src/Lexer.cpp — the tokenize loop
size_t before = pos_;
skipWhitespaceAndComments();
// an unspace isn't whitespace: `"xxxxxx"\.chars` keeps `.chars` tight-postfix
bool spaced = (pos_ > before && pos_ != unspaceEnd_) || before == 0;
// … lex the token …
t.spaceBefore = spaced;
```

The flag is true when skipping advanced the cursor — unless the cursor landed
exactly after an *unspace*, a backslash followed by whitespace, which is written
to be invisible and so does not count as space — and always for the very first
token. The parser then
leans on it constantly: to tell a postcircumfix from a list, a call from an
application, a postfix operator from a fresh term.

## Regex or division?

A bare `/` starts a regex in term position and divides in operator position.
The lexer decides by looking at the *previous* token:

```cpp
// src/Lexer.cpp — regexContext(), the Tok::Op case
case Tok::Op:
    return pv.text != "++" && pv.text != "--" &&
           pv.text != ">" && pv.text != "<" &&
           pv.text != ">>" && pv.text != "<<" &&
           pv.text != ">=" && pv.text != "<=" &&
           pv.text != "*" &&    // Whatever term: `* / 2` is a DIVISION
           pv.text != "\xE2\x88\x9E" &&     // U+221E, infinity: a TERM
           pv.text != "\xC2\xAB" && pv.text != "\xC2\xBB";  // guillemets
```

After most operators a term is expected, so `/` opens a regex. After a value —
a number, a variable, a closing bracket — or after a postfix `++`/`--`,
division follows. After an identifier it is a regex when the word is in a
small set of keywords that take an expression (`if`, `while`, `say`, `grep`,
`split`, and friends). It is also a regex after any other lower-case bare name
followed by a space, read as a listop with the regex as its first argument —
unless the name is a method name, a term such as `pi`, or a constant the file
declares, and only if a closing `/` stands on the same line. `//` and `/=` are
matched earlier so they lex as operators.

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
// src/Lexer.cpp — tryQuoteForm, on a :to adverb, abridged
heredocMarker_ = raw;                // the terminator, e.g. END
heredocPending_ = true;
heredocInterp_ = (w == "qq");
// … `qq:!c:to` takes a feature away, `q:s:to` adds one …
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
    return ascii::isalpha((unsigned char)c) || c == '_';
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
// src/Lexer.cpp — lexOperator, abridged
for (const auto& e : lexOpIndex().byFirst[(unsigned char)peek()]) {
    const char* op = e.op;
    size_t n = 0;
    while (op[n] && pos_ + n < src_.size() && src_[pos_ + n] == op[n]) n++;
    if (!op[n]) { /* consume it, return Tok::Op */ }
}
char c = advance();                  // unmatched: a single-character Op
return make(Tok::Op, std::string(1, c));
```

This is not a longest-match scanner. It is a manually ordered table, `kLexOps`,
where longer entries are placed before their prefixes, and the first full match
wins. The index only buckets the table by first byte, each bucket in the
table's own order, so a token start that begins no operator costs one lookup.
Adding a built-in operator means inserting it at the right position — a real
maintenance hazard, and named as one.

The fall-through matters: anything unmatched becomes a **single-character
`Tok::Op`**, so a novel user-defined operator arrives as *something* the parser
can pick up.

For a while that was the whole answer, and it was not enough. A spelling like
`sub infix:<%%%>` gets lexed a character at a time, and the built-in table
swallows a prefix of it first — `%%%` came out as `%%` then `%`, so `5 %%% 2`
divided by an empty hash. So the lexer's constructor now pre-scans the file for
its own operator and term declarations and tries them, longest first, ahead of
the table:

```cpp
// src/Lexer.cpp — scanUserOps, called by the constructor
for (const char* cat : {"infix:", "prefix:", "postfix:", "term:"}) { … }
```

A declared spelling loses only to a *longer* built-in one written at the same
place: with `..` declared, `..^` is still `..^`.

Only *this* file's declarations are visible to that scan. An imported operator
reaches the lexer the other way round: when the parser's scan of a `use`d
module (Chapter 6) finds an exported operator or term whose spelling the
built-in rules would split, it hands every spelling in scope to a fresh lexer
through `noteUserOp` and re-lexes the rest of the file. That closed what used
to be a real limitation: an imported operator spelled with one symbol
twice was lexed as two of the one-symbol operator.

The vocabulary also covers Unicode aliases (`≤` for `<=`), hyper metaoperators
(`»op«`), and the set operators (`(elem)`, `∈`). That part is static — but "the
lexer tracks no user declarations" is no longer true of it as a whole.

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
the captured text later, in Chapter 20.

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

Declarator pod is keyed by line, and the parser attaches it by where the lines
fall among the tokens. A `#|` documents the next declaration below it, however
far: blank lines and code that declares nothing leave it waiting, and the
first declarator keyword or block after it takes it. `leadingPodAt(tok)` walks
the tokens up from a declaration to that boundary, and remembers which
declaration took each line, because a parameter has no keyword to stop the
walk of the one after it. A `#=` documents the declaration beside it (the last
one, when several share the line) or the one just above when it stands alone;
that is `trailingPodAt(tok)`. The parser also records which lines a
*parameter* has already claimed — otherwise the parameter documentation of a
multi-line `sub MAIN` signature would be picked up a second time as the
routine's own usage text.

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

The lexer that reads a whole program is run *tolerant*: a lexical error does
not throw out of `tokenize()`. It is stored, and the token stream ends there
with an `End` token that carries its index; the parser rethrows it only if it
arrives at that token. A `use` further up may activate a slang or import an
operator that re-lexes the tail, and the text the first lex could not read may
then be perfectly good.

An `atEof` flag rides along on the resulting error. Only the REPL reads it, to
tell "give me a continuation line" from "this is a syntax error" — which is how
a half-typed string literal at an interactive prompt asks for more input instead
of failing (Chapter 39).
