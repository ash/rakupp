# A declared `sub s` / `sub q` / `sub ms` versus the quoting syntax

*2026-07-29. Attempted, measured, reverted. This is the record so it does not have
to be rediscovered.*

## The bug

Reported from a real port ([the Cognates findings](../../README.md), finding 9).
`ms` is `m` with `:sigspace`, so `ms { … }` is a legitimate match with brace
delimiters. Declare a routine of that name and Rakudo gives you the routine:

```raku
sub ms(&c) { 'ms-called' }

ms { 1 }        # Raku++: Regex (rx/ 1 /)      Rakudo: 'ms-called'
ms(-> { 1 })    # Raku++: Regex                Rakudo: 'ms-called'
```

There is no way to reach the sub at all, and the failure is silent — you get a
`Regex` where you expected the return value, and it surfaces as a type confusion
somewhere else entirely.

## Why the obvious fix does not work

rakupp lexes the whole unit to a token vector and then parses it. The lexer
therefore has no symbol table, and `tryQuoteForm` has to commit to `ms`/`q`/`s`
being a quote before anything knows what the program declares.

The attempt: lex once, scan the resulting token stream for a declarator followed
by a quote word (comments and string bodies are already tokenized away, so there
are no false positives from the text), and lex a second time suppressing those
quote forms. It works for the reported case.

**It cost 217 Roast assertions.** Two files stopped parsing outright:

```
S05-substitution/subst.t   191/191 -> dies at line 44
S02-literals/quoting.t      40/44  -> dies at line 34
```

Both declare exactly what the fix looks for — and then rely on the quote form
anyway, because **the shadowing is lexically scoped**:

```raku
# S05-substitution/subst.t
is s/a/b/, 'a', ...              # line 44: file scope, s/// is substitution
{
    sub s { 'sub s' }            # line 398
    ok s:s,foo,bar, , 's with colon is always substitution';
    is s(),  'sub s', 'can call sub s as "s()"';
    is s,    'sub s', 'can call sub s as "s"';
}
```

Note the third line of that block: inside the very scope that declares `sub s`,
`s:s,foo,bar,` is **still** substitution. So the rule is not even "a declaration
wins in its scope" — an adverb forces the quote reading regardless.

## Why a lexical shortcut cannot substitute for the symbol table

The tempting rule is "a quote form needs its delimiter adjacent, so `name {` after
a space is a call". It does not hold — a space is allowed before a quote
delimiter, and Rakudo parses both of these as quotes:

```raku
say q {abc}     # abc
say q{abc}      # abc
```

which is character-for-character the shape of `ms { 1 }`. The only thing that
separates them is whether a routine of that name is in scope at that point, which
is precisely the knowledge the lexer does not have.

## What a real fix needs

The lexer would have to consult a **scoped** symbol table as it runs — either by
interleaving lexing with parsing, or by pre-computing block ranges and the names
declared in each, then having `tryQuoteForm` test the current position against
them. That is an architectural change, and it must also reproduce the adverb
carve-out above.

Given the severity — the affected names are `q`, `qq`, `Q`, `rx`, `m`, `ms`, `mm`,
`s`, `S`, `ss`, `SS`, `tr`, `qw` and friends, and shadowing one is rare — this was
not judged worth it. The workaround is to name the routine something else; the
reporter renamed theirs to `elapsed-ms`.

If it is ever attempted again: run `S05-substitution/subst.t` and
`S02-literals/quoting.t` first, not last. They encode the exact semantics.
