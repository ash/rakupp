# User-Defined Operators in a Single Pass

Raku lets a program add operators to the language it is written in:

```raku
sub postfix:<!>($n) { [*] 1..$n }
say 5!;                                   # 120

sub infix:<avg>($a, $b) is tighter(&infix:<+>) { ($a + $b) / 2 }
say 4 avg 6 + 10;                         # 15 — (4 avg 6) + 10

sub circumfix:<⟦ ⟧>(*@x) { @x.sum }
say ⟦1, 2, 3⟧;                            # 6
```

That looks like it needs a parser that can rewrite its own grammar. It does
not. It needs a parser that keeps a mutable table and reads the file exactly
once.

## The tables

```cpp
// src/Parser.h — mutated during the parse, consulted at every operator site
std::map<std::string, int> userInfix_;    // name → left binding power
std::set<std::string> userInfixRight_;    // declared `is assoc<right>`
std::set<std::string> userPrefix_, userPostfix_;
std::map<std::string, std::string> userCircumfix_, userPostcircumfix_;
```

Five containers, all keyed by the operator's spelling. `userInfix_` maps to a
binding power so a user infix can sit at any level; the bracket maps go from
opening delimiter to closing delimiter.

## Registration happens mid-parse

When `parseSub` reads a sub whose name is one of the operator categories
followed by a colon, it pulls the operator's spelling out of the `<…>` and
writes it into the tables **on the spot**:

```cpp
// src/Parser.cpp — parseSub, operator declaration
if ((s->name == "infix" || s->name == "prefix" || s->name == "postfix" ||
     s->name == "circumfix" || s->name == "postcircumfix") && isOp(":")) {
    std::string cat = s->name; advance();               // ':'
    std::vector<std::string> w;
    if (isOp("<")) { advance(); w = readAngleWords(">"); }
    std::string opname = w.empty() ? "" : w[0];
    s->name = cat + ":<" + opname + ">";
    if (cat == "infix") { userInfix_[opname] = BP_ADD; declInfix = opname; }
    else if (cat == "prefix")  userPrefix_.insert(opname);
    else if (cat == "postfix") userPostfix_.insert(opname);
}
```

From that token onward, the operator is live. Everything *later* in the file
sees it; everything earlier does not. That asymmetry is not a limitation
invented here — it is Raku's own rule, and it falls out of the single pass for
free.

The declaration is otherwise an ordinary sub. Its name becomes the string
`"postfix:<!>"`, and subs live in the environment under a `&`-prefixed key, so
at run time it is found exactly like any other routine.

## How `5!` parses

Take `sub postfix:<!>($n) { [*] 1..$n }; say 5!;`.

The declaration runs the branch above and does `userPostfix_.insert("!")`.
Later, parsing `5!`: `parseExpr` calls `parsePrefix`, which calls
`parsePostfix(parsePrimary())`. `parsePrimary` returns the `5`. `parsePostfix`
loops and reaches the user-postfix arm:

```cpp
// src/Parser.cpp — parsePostfix
} else if ((cur().kind == Tok::Op ||
            (cur().kind == Tok::Ident && !cur().spaceBefore)) &&
           userPostfix_.count(cur().text)) {
    std::string opname = advance().text;
    auto call = std::make_unique<Call>();
    call->name = "postfix:<" + opname + ">";
    call->args.push_back(std::move(base));
    base = std::move(call);
}
```

`5!` becomes a `Call` node named `postfix:<!>` with one argument. The runtime
never learns that an operator was involved; it dispatches a named call. Infixes
work the same way through a branch at the top of the `parseExpr` loop, building
a two-argument `Call` named `infix:<op>` and honouring the recorded binding
power and associativity.

That is the whole trick, and it is worth stating plainly: **a user-defined
operator is a parse-time spelling for an ordinary sub call.** Every mechanism
that works on subs — closures, multi dispatch, `&infix:<avg>` as a value,
passing one to `.reduce` — works on operators without further effort.

## Precedence traits and the gaps of ten

A user infix registers at `BP_ADD` by default. A precedence trait resolves the
referenced operator's level and slots the new one five away:

```cpp
// src/Parser.cpp — parseSub, `is tighter/looser/equiv(&infix:<+>)`
int refBp = infixBpOf(refOp);
userInfix_[declInfix] = trait == "equiv"   ? refBp
                      : trait == "tighter" ? refBp + 5
                      :                      refBp - 5;
if (kind == "right") userInfixRight_.insert(declInfix);
```

This is what the by-ten spacing in the binding-power enum was reserved for.
`is tighter(&infix:<+>)` yields 125: tighter than `+` at 120, looser than `*`
at 130. `infixBpOf` resolves the referenced operator whether it is built in or
itself user-declared, so traits chain.

The obvious question is what happens when someone declares eight operators each
tighter than the last. The answer is that they collide at the same level after
the gap is exhausted — the levels are integers with a fixed spacing, not a
rational ordering. It has not come up in practice, and the alternative (a real
partial order rebuilt on each declaration) has not been worth building.

## Custom brackets

A `circumfix:<「 」>` or `postcircumfix` declaration registers an
open-to-close mapping. `parsePrimary` consults `userCircumfix_` when it meets an
unrecognised opening token, and `parsePostfix` consults `userPostcircumfix_`
after a term. The token classifiers that decide "does this start a term?" and
"does this start a list-operator argument?" consult the tables too:

```cpp
// src/Parser.cpp — startsTermToken / startsListopArg, the Tok::Op case ends:
userPrefix_.count(t.text) || userCircumfix_.count(t.text);
```

so a custom bracket can open an argument to a list operator, not just a
standalone term.

One guard is needed: `pcfxClose_` holds the active postcircumfix's closing
bracket while its content is parsed, so a bracket pair spelled with the same
character on both sides does not re-open itself inside its own contents.

## Operators are lexically scoped, and roll back

A declaration inside a block must not leak out of it. If it did, a
`sub postfix:<!!>` in some helper block would eat every later ternary's `!!`.

So every registration is logged, and `parseBlock` rewinds to its entry mark:

```cpp
// src/Parser.h
struct OpUndo {
    char table; std::string name; bool existed; int oldBp; std::string oldClose;
};
std::vector<OpUndo> opUndo_;

void opRollback(size_t mark) {
    while (opUndo_.size() > mark) {
        OpUndo& u = opUndo_.back();
        switch (u.table) {
            case 'i': if (u.existed) userInfix_[u.name] = u.oldBp;
                      else userInfix_.erase(u.name); break;
            case 'p': if (!u.existed) userPrefix_.erase(u.name); break;
            // … 'P' postfix, 'r' right-assoc, 'c'/'C' the bracket maps …
        }
        opUndo_.pop_back();
    }
}
```

The undo record keeps the *previous* value, not just the fact of insertion, so
a block that shadows an outer operator at a different precedence restores the
outer one on exit rather than deleting it.

This is an undo log rather than a stack of scopes because registration is
scattered across `parseSub` and the trait handling, and a single mark-and-rewind
call at one place in `parseBlock` is much harder to get wrong than a matched
push and pop at each registration site.

## Two escape hatches

The single-pass rule has exactly two ways out, and both are about *another*
parse seeing the current one's tables.

**`EVAL`.** A snippet is parsed by a fresh `Parser`, which would know nothing
about the enclosing program's operators. So the evaluator pre-seeds them:

```cpp
// src/Parser.h
void declareUserOp(const std::string& kind, const std::string& name) {
    if (kind == "infix") userInfix_[name] = 120;
    else if (kind == "prefix")  userPrefix_.insert(name);
    else if (kind == "postfix") userPostfix_.insert(name);
}
```

which is why this works:

```raku
use MONKEY-SEE-NO-EVAL;
sub infix:<z>($a, $b) { $a * 10 + $b }
say EVAL("40 z 2");                    # 402
```

**String interpolation**, shown at the end of the previous chapter, copies the
live tables wholesale into the sub-parser.

## `use Foo` and imported operators

A module that declares an operator changes how the *importing file* parses.
Nothing else about a module does. So `use Foo` triggers exactly one
compile-time action, and it is not a parse:

```cpp
// src/Parser.h
void scanModuleOps(const std::string& module);
void scanOpsIn(const std::string& src, const std::string& srcPath);
```

`scanModuleOps` resolves the module's *source file* through the same search
path the loader uses and **text-scans** it — a substring search over raw bytes,
no lexing, no parsing — for `sub`, `multi`, `proto` and `only` declarations of
the five operator categories, registering what it finds.

Two consequences worth knowing:

- An operator spelled only in ASCII operator characters is **skipped**. A
  `multi infix:<*>(Color, Real)` is almost always extending a built-in, and
  registering it as a user operator would give it the default precedence and
  silently reshape every expression in the importing file.
- The scan can in principle be fooled by an operator declaration inside a
  string or a comment. Nothing has tripped on it yet.

Every source the scan read is recorded in `opScanned_` as a (path, content)
pair. That is not for the parse; it is for the cache. A file's parse is only
valid while the modules it scanned still declare what they declared — an
imported module that gains or loses an operator changes how *this* file parses,
without this file changing at all. Chapter 30 uses that list as a cache key.

## The line this draws

Everything above is table manipulation: cheap, local, and reversible. That is
why all six operator categories work, with precedence and associativity, in a
parser that runs no user code.

The features on the other side of the line — `macro`, `quasi`, `RakuAST`,
slangs — need the parser to *execute user code mid-parse and then rewrite its
own grammar with the result*. None of them are implemented, and the reason is
structural rather than a matter of effort.

There is a compensation. Because the language Raku++ accepts stays static
enough to know in full at build time, the whole program can be compiled ahead
of time — which is what the `--aot` and `--exe` modes exploit. The one
remaining genuinely dynamic construct, a runtime-constructed grammar, is exactly
what those modes fall back to bundling.
