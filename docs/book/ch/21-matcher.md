# The Backtracking Matcher

Chapter 20 ended with a node tree and no way to run it. Running it is where
regex engines usually acquire their bulk: backtracking state, capture
bookkeeping, the machinery to undo a branch that failed halfway through. This
one acquires almost none of it, because the backtracking is the C++ stack
unwinding and everything else fits in a single frame struct.

The matcher is continuation-passing. One function walks the node tree:

```cpp
// src/Regex.h
bool matchNode(const Node* n, MState& st, long pos, const FnRef& k) const;
```

"Match node `n` at position `pos`; if it succeeds, call `k` with the position
after it; return whether the whole thing ultimately succeeded."

That signature is the entire design. Backtracking is the C++ stack unwinding
when a continuation returns `false`, and every construct that has alternatives
simply tries them in order:

```cpp
// src/Regex.cpp
case K::Seq: {
    // match kids[0], and in its continuation match the rest
}
case K::Alt: {
    // try each branch in ranked order; the first whose continuation
    // succeeds wins
}
case K::Rep: {
    // greedy: try one more repetition first, then k(pos)
    // frugal: try k(pos) first, then one more repetition
}
```

There is no explicit backtracking stack, no thread list, no bytecode. The
continuation *is* "the rest of the pattern", and the C++ frame that holds it *is*
the choice point.

## `FnRef`: a continuation that does not allocate

A `std::function` for the continuation would heap-allocate on every node visit,
which for a matcher is fatal. Continuations here live only for the duration of
the match call chain — they are never stored — so the callable can be borrowed
from the caller's stack:

```cpp
// src/Regex.h
struct FnRef {
    void* ctx;
    bool (*fn)(void*, long);
    template <class F, class = std::enable_if_t<
                  !std::is_same_v<std::decay_t<F>, FnRef>>>
    FnRef(F&& f)
      : ctx((void*)&f),
        fn([](void* c, long v) {
              return (*(std::remove_reference_t<F>*)c)(v); }) {}
    bool operator()(long v) const { return fn(ctx, v); }
};
```

Two words, no heap, one indirect call. The safety argument is the lifetime rule
stated in the comment: continuations are never stored, so the borrowed lambda
always outlives the call. Break that rule — store an `FnRef` anywhere — and the
result is a dangling pointer.

## The step budget

Continuation-passing plus backtracking means a pathological pattern can recurse
without bound. `/[a*]* b/` against a long non-matching string is the classic.

```cpp
// src/Regex.cpp — the top of matchNode
if (++st.steps > 8000000) throw StepLimitExceeded{};
```

Eight million steps is far beyond any real match and trips in well under a
second on the pathological cases. The exception is caught at each match entry
point and reported as a failure to match:

```cpp
// src/Regex.h
struct StepLimitExceeded {};
```

The budget is carried *across* the start positions of an unanchored search, so a
quadratic scan cannot escape it by restarting.

This is a blunt instrument and named as one: it prevents a hang, it does not
prevent a pathological pattern from being slow, and a legitimate match that
genuinely needs more than eight million steps would be reported as a
non-match. No such match has appeared.

## `MState`: everything one match frame knows

```cpp
// src/Regex.h — MState, abridged
struct MState {
    const std::string& s;
    std::vector<std::pair<long, long>> caps;                 // $0, $1, …
    GrammarHooks::NamedMap named;                            // $<x>
    ChildMap children;                                       // subrule trees
    const SubResolver* resolver = nullptr;   // plain-regex subrule path
    class GrammarMatcher* grammar = nullptr; // grammar path
    const std::set<std::string>* lexNames = nullptr;  // my regex NAME shadows
    std::map<int, std::vector<std::pair<long, long>>> capReps;
    long startPos = 0;
    long capFrom = -1, capTo = -1;           // the <( … )> span
    const GrammarHooks* hooks = nullptr;     // interpreter callbacks
    const std::string* curSym = nullptr;     // the proto candidate's :sym<…>
    long firstCode = -1;                     // where the LTM prefix ends
    int probing = 0;                         // inside an Alt ranking probe
    int probeAbove = 0;                      // …or in a rule a probe called
    long litPrefix = -1;                     // leading literal run
    long steps = 0;
};
```

`NamedMap` and `ChildMap` are not `std::map`. They are instances of `FlatMap`,
a sorted vector of pairs that offers the subset of the `std::map` interface
the matcher uses. These maps are built, copied and torn down constantly
during a grammar parse, and a heap node per key was most of that cost.
Iteration stays in key order, which `Match` assembly relies on. The price is
that an insert invalidates references to other entries, so the matcher looks
a key up again after running a continuation.

Captures are **byte spans**, not strings. Nothing is copied out of the subject
until a `Match` object is built, which keeps backtracking cheap: undoing a
capture is restoring a pair of integers.

`capFrom` and `capTo` implement `<( … )>`, which narrows what the *overall
match* reports while matching continues outside it.

## Two subrule paths

A `<name>` call inside a pattern resolves one of two ways, and the difference
is fundamental.

**The grammar path** threads the continuation *through* the callee:

```cpp
// src/Regex.cpp — case K::Subrule
if (st.grammar) {
    nodeMeta(n, *st.grammar);
    if (n->noBack)
        return st.grammar->matchSubCommitted(*n->metaCache, n->ruleName,
                                             n->ruleArgs, …, st, pos, k, …);
    return st.grammar->matchSubMeta(*n->metaCache, n->ruleName, n->ruleArgs,
                                    …, st, pos, k, …);
}
```

Because `k` crosses the call, `<a> <b>` can backtrack **into** `<a>` when `<b>`
fails. That is what Raku's grammars require and what most regex engines with
"subroutine calls" do not provide. The `noBack` branch is the exception the
language asks for: a call written under `:ratchet`, which is every call in a
`token` or a `rule`, takes the callee's first completion and does not come
back for another (Chapter 20).

**The plain-regex path** uses a `SubResolver`, which is atomic: it matches the
named rule at a position and answers with a span. No continuation crosses, so no
backtracking into the callee. A lexical `my regex` is the one callee that must
be backtracked into outside a grammar, and it does not go through the
resolver: the `lexCallee` hook answers its compiled body, and the call threads
`k` through it as the grammar path does.

Between them sit the built-in rules — `<alpha>`, `<digit>`, `<ident>`, `<ws>`
and the rest — resolved directly by `builtinRuleMatch`, with one carve-out:

```cpp
// src/Regex.cpp
if (!(st.lexNames && st.lexNames->count(n->ruleName))) { … }
```

A lexical `my regex ident { … }` shadows the built-in of that name. The check is
a set lookup on a set that is usually null.

The `metaCache` field is a per-node cache of the name resolution, so the hot
path never re-resolves a rule name through a string-keyed map. A node can
outlive the matcher that filled it: the pattern a `<{ code }>` block answers
is compiled once and kept for the interpreter's life, and the next parse has
another matcher. So the node also records whose answer it holds, in
`metaOwner`, and `nodeMeta` resolves again when that is not the current
matcher's `serial`. A serial and not an address, because the next parse's
matcher is a local at the same address.

## Lookarounds

A zero-width assertion matches its inner pattern in an **isolated capture
state**, so captures inside a lookahead do not leak:

```cpp
// src/Regex.cpp — case K::Look
MState sub{st.s, std::vector<std::pair<long,long>>(ncaps_, {-1,-1}),
           {}, {}, st.resolver, st.grammar};
sub.hooks = st.hooks; sub.startPos = pos;
m = matchNode(child, sub, pos, [](long) { return true; });
…
return (m != n->negate) ? k(pos) : false;
```

The hooks are propagated into the sub-state, so an embedded code block or `$var`
inside a lookahead still evaluates against the interpreter's live scope. The
final line is the whole of positive-versus-negative: `negate` flips the sense,
and either way the position does not advance.

## The hooks: running Raku during a match

A Raku regex can contain executable code — `{ … }` side effects, `<?{ … }>`
assertions, `:my` declarations, `$var` atoms, and `** { … }` quantifier bounds.
The matcher cannot evaluate Raku, so it calls back:

```cpp
// src/Regex.h — GrammarHooks, abridged
std::function<bool(const std::string&)> hasAction;
std::function<void(ParseNode)> onRule;
std::function<bool(const std::string&, long, long,
                   const NamedMap&, const ParamMap&)> assertPass;   // <?{…}>
std::function<bool(…)> assertPassCaps;    // …with positional captures
std::function<bool(…)> assertPassCursor;  // …and the occurrence lists
std::function<void(…)> run;        // :my / {…}
std::function<void(…)> runCaps;    // …with positional captures, so $0 works
std::function<void(…)> runCursor;  // …and the occurrence lists
std::function<std::string(…)> str;                  // a $var atom
std::function<std::string(…)> argVal;               // a rule argument
std::function<std::pair<long,long>(…)> range;       // ** { … }
std::function<std::shared_ptr<void>()> saveState;
std::function<void(std::shared_ptr<void>)> restoreState;
std::function<std::shared_ptr<const void>()> captureDyn;
std::function<bool(const std::string&, std::string&, std::string&)> namedRule;
std::function<std::shared_ptr<const Regex>(const std::string&)> lexCallee;
std::function<bool(const std::string&)> hasMethod;
std::function<int(…)> callMethod;         // <.method> on a grammar
std::function<const Regex*(…)> dynRule;   // <{ code }>
```

Every hook is optional. A plain `Regex` with none of them set treats code
constructs leniently rather than failing, which is what lets the same engine
serve a simple `~~ /…/` and a full grammar parse.

The current named and positional captures are passed *into* the hook, so the
block's `$/` can offer `$<x>` and `$0` — the assertion in
`/ (\d+) <?{ $0 > 10 }> /` needs them, and they exist only inside the matcher.
The `assertPass` and `run` hooks each come in three widths, and the matcher
calls the widest one that is set. The `Cursor` forms also carry the
per-name occurrence lists, so a repeated name has inside the block the shape
it will have when the match finishes: `<n> '+' <n> { $<n>.elems }` reads 2.

Three of the later hooks are calls out of the pattern. `hasMethod` and
`callMethod` serve a `<name>` that is neither a rule nor a proto but an
ordinary method of the grammar, as in `<.panic("…")>`: the method runs with
the cursor as `self`, and a `Match` it returns continues the parse at its
end. `dynRule` serves `<{ code }>`: the block runs at match time and its
result is the pattern to call.

`saveState` and `restoreState` exist for one specific purpose: the longest-token
ranker in Chapter 23 measures branch lengths by *probing*, and a probe must not
leave the interpreter's `:my` variables and deferred `make`s behind. They
snapshot and roll back that state around a measurement pass.

## When side effects fire

This is the subtlest semantic decision in the engine, and it is the same on
both paths: **blocks fire where the matcher reaches them.**

A `{ … }` block runs when the match gets to it, and a later backtrack does
not undo it. A block on a branch that is abandoned has run, and a block after
a quantifier runs once for each length the quantifier is tried at. On a
grammar, a completed subrule fires its action method immediately, and a later
backtrack does **not** unfire it. That sounds wrong, and it is what Rakudo
does: `HTTP::Header` sets header fields from the actions of a parse whose
`TOP` ultimately fails on a missing trailing newline, and expects the fields
to be there. A later `<?{ … }>` assertion also needs to see what an earlier
`{ … }` set.

```raku
my @l;
"aab" ~~ / [ a { @l.push("x") } ac | a { @l.push("y") } ab ] /;
say @l;    # [x y]
```

Rakudo 2026.09 prints the same. (A comment in `MState` still describes a
queue that held a plain regex's blocks until the match was accepted. No such
queue is in the code.)

The one place a block does not run is a ranking probe. The probe ranker of
Chapter 23 runs each branch of a `|` to measure it, and `MState::probing` is
nonzero while it does. A bare block is zero-width and always passes, so
running it would change no measurement and would fire its side effect a
second time; under a probe it is skipped. Assertions still run, because they
decide the branch. A grammar method is skipped too, in the probed frame and
in every rule frame below it, which is what `probeAbove` carries down.

`hasAction` exists so that rules with no action method cost nothing. Assembling a
`ParseNode` for a completed rule is not free, so the matcher asks first whether
anyone is listening. An action fires on a fresh completion only: a packrat
replay reuses the first firing, and a completion inside a probe fires nothing
until its first real replay.

## Entry points

```cpp
// src/Regex.h
bool search(const std::string& subject, long startPos, RxMatch& out) const;
bool search(const std::string& subject, long startPos, RxMatch& out,
            const SubResolver& r, const std::set<std::string>* lexNames,
            const GrammarHooks* hooks) const;
bool matchAt(const std::string& subject, long pos, RxMatch& out,
             const SubResolver& r, const std::set<std::string>* lexNames,
             const GrammarHooks* hooks) const;
std::vector<RxMatch> searchExhaustive(const std::string& subject,
             const SubResolver& r, const std::set<std::string>* lexNames,
             const GrammarHooks* hooks) const;
```

`search` is the unanchored scan: try each start position in turn. A start
position is a grapheme boundary, since a match never begins inside a
character; over ASCII that is a step of one byte. One `MState` serves the
whole scan and is reset field by field between attempts, and the prefilters
of Chapter 20 decide which positions get an attempt at all. `matchAt` is
anchored, used by grammar subrule calls. `searchExhaustive` implements
`:exhaustive` — every match at every start position and every length — which is
the only mode that does not stop at the first success.

The result is a `RxMatch`: spans, capture arrays, named spans, the child
`ParseNode` trees for subrules, and the shared frozen sets that record which
captures are list-valued. Before it is handed over, `localizeCaps` re-reads
the flat capture slots as the pattern's own `$0 $1 …`, leaving out a capture
nested inside another, which the capture that owns it presents. The
interpreter turns the result into a Raku `Match` object, which is a
`VT::Match` `Value` carrying the matched text in `s`, the span in
`rFrom`/`rTo`, and the positional captures, the named ones and `.made`
together in one `MatchData` body (Chapter 8).

## Substitution

`s///`, `.subst` and `tr///` share one entry point on the interpreter, because
the occurrence-selection adverbs are the complicated part:

```cpp
// src/Interpreter.h
std::string substSelect(const std::string& subj, const std::string& pat,
                        Value* replArg, ValueList& args, long& nsub,
                        bool literal = false,
                        const std::string* tmplRepl = nullptr,
                        Value* matchResult = nullptr);
Value substApply(Value* target, const std::string& pattern,
                 const std::string& repl, bool nonMut);
```

`substSelect` handles `:g`, `:x`, `:nth`, `:p`, `:c` and the `same`-family
adverbs (`:samecase`, `:samespace`, `:samemark`), which adjust the replacement
to match the case, spacing or marks of what it replaced.

`substApply` is the single call the compiled backend emits, so a compiled
`s///` and an interpreted one run the same code — including the `readonly` check
that makes `s///` on a bound parameter die.
