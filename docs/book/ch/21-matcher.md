# The Backtracking Matcher

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
    std::map<std::string, std::pair<long, long>> named;      // $<x>
    std::map<std::string, std::vector<ParseNode>> children;  // subrule trees
    const SubResolver* resolver = nullptr;   // plain-regex subrule path
    class GrammarMatcher* grammar = nullptr; // grammar path
    const std::set<std::string>* lexNames = nullptr;  // my regex NAME shadows
    std::map<int, std::vector<std::pair<long, long>>> capReps;
    long startPos = 0;
    long capFrom = -1, capTo = -1;           // the <( … )> span
    const GrammarHooks* hooks = nullptr;     // interpreter callbacks
    const std::string* curSym = nullptr;     // the proto candidate's :sym<…>
    long firstCode = -1;                     // where the LTM prefix ends
    long litPrefix = -1;                     // leading literal run
    long steps = 0;
};
```

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
    if (!n->metaCache) n->metaCache = &st.grammar->nameMeta(n->ruleName);
    return st.grammar->matchSubMeta(*n->metaCache, n->ruleName, n->ruleArgs,
                                    …, st, pos, k, …);
}
```

Because `k` crosses the call, `<a> <b>` can backtrack **into** `<a>` when `<b>`
fails. That is what Raku's grammars require and what most regex engines with
"subroutine calls" do not provide.

**The plain-regex path** uses a `SubResolver`, which is atomic: it matches the
named rule at a position and answers with a span. No continuation crosses, so no
backtracking into the callee.

Between them sit the built-in rules — `<alpha>`, `<digit>`, `<ident>`, `<ws>`
and the rest — resolved directly by `builtinRuleMatch`, with one carve-out:

```cpp
// src/Regex.cpp
if (!(st.lexNames && st.lexNames->count(n->ruleName))) { … }
```

A lexical `my regex ident { … }` shadows the built-in of that name. The check is
a set lookup on a set that is usually null.

The `metaCache` field is a per-node cache of the name resolution, so the hot
path never re-resolves a rule name through a string-keyed map. It is safe
because compiled regexes live in the matcher's own cache, so the node and the
matcher share a lifetime.

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
std::function<void(const ParseNode&)> onRule;
std::function<bool(const std::string&, long, long,
                   const NamedMap&, const ParamMap&)> assertPass;   // <?{…}>
std::function<void(…)> run;        // :my / {…}
std::function<void(…)> runCaps;    // …with positional captures, so $0 works
std::function<std::string(…)> str;                  // a $var atom
std::function<std::pair<long,long>(…)> range;       // ** { … }
std::function<std::shared_ptr<void>()> saveState;
std::function<void(std::shared_ptr<void>)> restoreState;
std::function<bool(const std::string&, std::string&, std::string&)> namedRule;
```

Every hook is optional. A plain `Regex` with none of them set treats code
constructs leniently rather than failing, which is what lets the same engine
serve a simple `~~ /…/` and a full grammar parse.

The current named and positional captures are passed *into* the hook, so the
block's `$/` can offer `$<x>` and `$0` — the assertion in
`/ (\d+) <?{ $0 > 10 }> /` needs them, and they exist only inside the matcher.

`saveState` and `restoreState` exist for one specific purpose: the longest-token
ranker in Chapter 23 measures branch lengths by *probing*, and a probe must not
leave the interpreter's `:my` variables and deferred `make`s behind. They
snapshot and roll back that state around a measurement pass.

## When side effects fire

This is the subtlest semantic decision in the engine, and it differs between the
two paths.

**On the grammar path, blocks fire eagerly.** A completed subrule fires its
action method immediately, and a later backtrack does **not** unfire it. That
sounds wrong, and it is what Rakudo does: `HTTP::Header` sets header fields from
the actions of a parse whose `TOP` ultimately fails on a missing trailing
newline, and expects the fields to be there. A later `<?{ … }>` assertion also
needs to see what an earlier `{ … }` set.

**On the plain-regex path, bare blocks are queued.** The matcher is
continuation-passing and backtracking, so a block sitting on a branch that is
later abandoned would otherwise fire anyway — and fire again on every retry. So
the plain entry points queue them, unwind the queue when a branch fails, and run
them in order only once the overall match is accepted.

The gating flag is why: only the plain-regex entry points turn queuing on and
drain it. The grammar path wants eager.

`hasAction` exists so that rules with no action method cost nothing. Assembling a
`ParseNode` for a completed rule is not free, so the matcher asks first whether
anyone is listening.

## Entry points

```cpp
// src/Regex.h
bool search(const std::string& subject, long startPos, RxMatch& out) const;
bool matchAt(const std::string& subject, long pos, RxMatch& out,
             const SubResolver& r, const std::set<std::string>* lexNames) const;
std::vector<RxMatch> searchExhaustive(const std::string& subject,
             const SubResolver& r, const std::set<std::string>* lexNames) const;
```

`search` is the unanchored scan: try each start position in turn. `matchAt` is
anchored, used by grammar subrule calls. `searchExhaustive` implements
`:exhaustive` — every match at every start position and every length — which is
the only mode that does not stop at the first success.

The result is a `RxMatch`: spans, capture arrays, named spans, the child
`ParseNode` trees for subrules, and the shared frozen sets that record which
captures are list-valued. The interpreter turns that into a Raku `Match` object,
which is a `VT::Match` `Value` carrying the subject in `s`, the span in
`rFrom`/`rTo`, positional captures in `arr` and named ones in `hash` — the
canonical example of a `Value` with four fields live at once (Chapter 8).

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
