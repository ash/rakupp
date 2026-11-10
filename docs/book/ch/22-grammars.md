# Grammars

A grammar is the largest thing Raku's regex sub-language is asked to do: a
whole parser, written declaratively, with a parse tree and a set of actions
falling out of it. Four working language interpreters in this project's
showcase — for JavaScript, Perl 5, Python 3 and Lisp — are built that way,
so what follows is load-bearing rather than a demonstration feature.

A Raku grammar is a class whose methods can be regexes. That is not a
simplification for the book — it is how it is implemented. `grammar G { … }`
produces a `ClassInfo` with `isGrammar` set, its rules in a string map beside
its methods:

```cpp
// src/Value.h — ClassInfo, the grammar fields
std::map<std::string, std::string> rules;      // name → pattern text
std::map<std::string, std::shared_ptr<Env>> ruleHome;  // a role's rule: its
                                               // own scope
std::vector<std::string> ruleOrder;            // DECLARATION order
std::map<std::string, std::string> ruleKind;   // token / rule / regex
std::map<std::string, std::vector<std::string>> ruleParams;
bool isGrammar = false;
```

Inheritance works because `findRule` walks the parent chain exactly as
`findMethod` does, so `grammar Mine is Base` overrides individual rules. A
rule composed in from a role keeps the scope it was written in, in
`ruleHome`, so a `<?{ f() }>` in it sees the subs the role's file imported
and not only the grammar's.

The pattern text is stored **unparsed**, as the lexer captured it (Chapter 4).
Compilation happens lazily, per rule, on first use.

## The grammar matcher

```cpp
// src/Regex.h
class GrammarMatcher {
public:
    struct Rule {
        std::string pattern, kind; std::vector<std::string> params;
        struct Lit { std::vector<std::string> args;
                     std::string pattern, kind; };
        std::vector<Lit> lits;     // `multi rule expr(0)` candidates
        bool litOnly = false;      // …and there is no generic body
        int home = 0;              // the scope it was written in
    };
    std::map<std::string, Rule> rules;
    std::map<std::string, std::vector<std::string>> protos;
    GrammarHooks hooks;

    bool parse(const std::string& input, const std::string& top, bool subparse,
               ParseNode& out, long& endOut, long startPos = 0);
    bool matchSub(const std::string& name, const std::string& args,
                  const std::string& capKey, Regex::MState& st, long pos,
                  const FnRef& k);
    long hwPos = -1;               // furthest position a rule failed at
    std::string hwRule;            // …and which rule
private:
    std::unordered_map<uint64_t, MemoEntry> memo_;
    std::unordered_map<std::string, NameMeta> nameMeta_;
    std::map<std::string, std::unique_ptr<Regex>> cache_;
    std::vector<std::map<std::string, std::string>> scope_;
};
```

A `GrammarMatcher` is built **per parse**. That single fact removes a whole
category of problems: every cache in it — compiled rules, name metadata, the
packrat memo, the longest-token automata — dies with the parse, so none of them
needs invalidation logic.

`Interpreter::grammarParse` builds one, fills `rules` and `protos` from the
`ClassInfo`, wires the hooks to the interpreter, and runs it. The rules are
collected most derived first, through the parent and through every composed
role, and a name already taken is skipped, so a derived declaration wins
exactly as a method would. A lexical `my token` in scope is added under its
name when the grammar chain declares no rule of that name.

"Dies with the parse" has one refinement. Destroying the memo walks every
frozen subtree in it, which the source records as about 28% of a capturing
parse when it was measured. The trees are refcounted and the finished parse holds its own
references, so `reapMemo` hands a memo of 512 entries or more to a detached
thread and returns. At most four such threads run at once; past that, and on
a build without threads, the memo is destroyed inline as before.

When a parse fails, the matcher has kept one fact about why. Every named rule
that fails calls `noteFail`, which records the position and the rule's name
if the position is further than any before. Recursive descent fails
innermost first and the comparison is strict, so at one position the deepest
rule wins: `<key>`, not the `<entry>` around it. The interpreter converts the
byte offset to a character position, and after a failed parse the
`rakupp-parse-diagnosis` built-in answers both. It is a Raku++ extension;
Rakudo's grammars have no such call.

## A subrule call, in full

`<name>` inside a rule reaches `matchSubMeta`, which is where most of the
engine's cleverness lives. In order:

1. **Resolve the name** — or rather, read the already-resolved metadata,
   which the calling node cached (Chapter 21).
2. **Check the memo**, if this rule is ratcheting.
3. **Compile the rule body**, if this is its first use, and cache it.
4. **Bind parameters**, if the call was `<r($x, 'lit')>`.
5. **Match**, threading the caller's continuation through.
6. **Fire the action method**, if the grammar's action object has one.
7. **Record a `ParseNode`** in the parent's capture frame, and run the
   caller's continuation.

Four kinds of name leave that sequence at step 1. A proto dispatches to its
candidates (below). A name that is an ordinary method of the grammar is
called through the `callMethod` hook. A built-in such as `<alpha>` or `<ws>`
that the grammar does not redefine goes to `builtinRuleMatch`, the same
function the plain-regex path uses. And a rule whose body is one character
is tested in place (Chapter 20), with no memo and no frame.

### The per-name metadata

```cpp
// src/Regex.h
struct GrammarRuleMeta {
    bool ratchet = false; int id = 0;
    Regex* singleChar = nullptr;   // body is one char matcher: inline it
    const void* rule = nullptr;
    Regex* noArg = nullptr;        // pre-compiled parameterless body
    const std::vector<std::string>* proto = nullptr;
    bool isWs = false;
    bool isMethod = false;         // an ordinary method of the grammar
    bool viaHow = false;           // the grammar's HOW overrides find_method
    bool scoped = false;           // body declares `:my`
    bool dynDep = false;           // …or reads a dynamic var: do not memoise
    std::string builtinClass;      // unknown name → built-in class flags
    mutable std::shared_ptr<LtmNfa> protoNfa;
    mutable bool protoNfaTried = false;
};
```

Computed once per name and reached through a pointer cached on the calling AST
node, so the hot path never touches a string-keyed map.

Two flags on it encode a real correctness rule. `scoped` means the rule's body
declares `:my` variables, so the interpreter's scope must be saved and restored
around the call. `dynDep` means the body declares `:my` **or reads a dynamic
variable** — and therefore its match can depend on caller state that is not part
of the memo key, so **it must not be memoised**.

That second one is the kind of condition that is obvious in hindsight and
invisible in advance. A memo keyed on (rule, position) is only sound if the rule
is a function of (rule, position).

### The packrat memo

A ratcheting rule does not backtrack, so its first complete match at a position
*is* the match. Caching it collapses the exponential re-descent that recursive
longest-token probing would otherwise cause:

```cpp
// src/Regex.h
struct MemoEntry {
    bool matched = false;
    long end = 0;
    long declEnd = 0;      // where the declarative prefix ended
    long litPrefix = 0;    // leading literal run, for the LTM tie-break
    long capFrom = -1, capTo = -1;
    std::vector<std::pair<long, long>> caps;
    GrammarHooks::NamedMap named;
    std::shared_ptr<const ChildMap> kids;      // frozen: replays share
    std::shared_ptr<const std::set<std::string>> listNames;
    std::shared_ptr<const std::set<int>> listCaps;
    std::shared_ptr<const std::map<int,
        std::vector<std::pair<long,long>>>> capReps;
    bool fired = false;    // its action has run
};
std::unordered_map<uint64_t, MemoEntry> memo_;
```

The key is a 64-bit integer built from the rule id, the position and the
values of the call's arguments — not a string, because a string key would
allocate on every subrule call.

Two completions are not stored. A rule with `dynDep` set is one, as above.
The other is a rule whose body, under a ranking probe, stepped over a grammar
method: the probe measured a stand-in, and that must not answer for the real
match later.

The child map is stored **frozen behind a `shared_ptr`**, which is the detail
that makes the memo pay. Replaying a memoised match is then a refcount bump
rather than a subtree copy, and a deeply nested grammar replays large subtrees
constantly.

## `ParseNode`: the tree the matcher records

```cpp
// src/Regex.h
struct ParseNode {
    std::string name;
    std::string actualRule;    // for a proto: the winning name:sym<…>
    long from = 0, to = 0;
    int capLocal = -1;         // which $n of its parent, if positional
    uint64_t aliasId = 0;      // nonzero: one capture filed under two keys
    std::vector<std::pair<long, long>> caps;
    GrammarHooks::NamedMap named;
    std::shared_ptr<const ChildMap> kids;     // null = leaf
    std::shared_ptr<const std::set<std::string>> listNames;
    std::shared_ptr<const std::set<int>> listCaps;
    std::shared_ptr<const std::map<int,
        std::vector<std::pair<long,long>>>> capReps;
    std::shared_ptr<const void> dynScope;     // the `*` dynamics live then
};
using ChildMap = FlatMap<std::vector<ParseNode>>;
```

The child map is a map to a **vector**, so repeated captures of the same name
collate into a list — which is Raku's rule for `<pair>*`.

Except that the rule is stronger than "collate when repeated". A capture under a
repetition quantifier is list-valued **even when it matched once or not at
all**, so the arity has to come from the *pattern* rather than from the match.
That is what `listNames`, `listCaps` and `capReps` carry, shared from the
compiled `Regex` rather than rebuilt per node.

The interpreter walks the finished tree and builds `Match` values from it, with
`$<name>` reading the child map and `$0` reading the positional spans.

`<alias=rule>` files one capture under two keys, the alias and the rule name.
Both records carry the same `aliasId`, so the builder makes one `Match`,
publishes it under each key, and fires the rule's action once.

## Actions

An action object is an ordinary Raku object whose methods are named after the
grammar's rules. When a rule completes, its method runs and can call `make` to
attach a value to the match.

The engine fires actions **during** the match, through the hooks:

```cpp
// src/Regex.h — GrammarHooks
std::function<bool(const std::string&)> hasAction;
std::function<void(ParseNode)> onRule;
```

`hasAction` gates the node assembly, so a rule with no corresponding method
costs nothing. `onRule` fires the method with the completed node: the
interpreter builds the node's `Match` then and there, runs the method, and
keeps the result. The parent's build, a code block's `$/` and the finished
tree all take that same `Match`, so each node is built, and fired, once. That
is what lets a `<?{ $<tag>.made … }>` right after the call read what the
action made.

Three properties of that design, all deliberate:

- **A later backtrack does not unfire an action.** This matches Rakudo; the
  reasoning is in Chapter 21.
- **A memo replay reuses the first firing** rather than firing again, so
  packrat caching does not multiply side effects.
- **`make` writes into a target on the execution context** —
  `ExecContext::makeTargets` — which is how the value attaches to the right
  match rather than to a global.
- **A probe fires nothing.** A completion reached while a `|` or a proto is
  only being ranked is stored with `fired` clear, and its action runs on the
  first real replay.

A rule that declares `:my $*X` has its dynamic scope rolled back when it
exits, and a node with no action of its own is built after the parse is
over. So the dynamics that were live when the rule completed travel with its
node, in `dynScope`, and the build puts them back around the node's subtree.
The pointer is null for a parse that declared none.

## Parameterised rules

`<r($x, 'lit')>` binds arguments to the rule's declared parameter names:

```cpp
// src/Regex.h
std::vector<std::map<std::string, std::string>> scope_;
const std::map<std::string, std::string>& currentParams() const;
Regex* compiled(const std::string& name, const std::string& argstr,
                std::map<std::string, std::string>& boundOut);
std::string interpParams(const std::string& pat,
                         const std::map<std::string, std::string>& sc) const;
```

Bindings are strings, and they are interpolated into the pattern *text* before
compilation, quoted as literals. So `token line($indent) { $indent \N* }`
compiles a different `Regex` for each distinct indent value — and the
compiled-rule cache is keyed on name plus argument values for exactly that
reason. A value that is not a `Str` travels with a one-letter type tag in
front, so a code block in the callee gets a `Bool`, an `Int` or the object
itself back, and only the interpolation reads its string form.

Arguments are sorted as a method call's are: `:name(v)` and `:$name` bind a
named parameter, a parameter may have a default, and `|[…]` spreads. A rule
may also have candidates constrained to literal values, `multi token e(0)`
beside `multi token e($n)`. A call evaluates its arguments once and takes the
first candidate whose literals all match, and otherwise the generic body.
That is the base case of a recursive rule such as `<e($n - 1)>`.

That design has a clear cost (a compilation per distinct argument tuple) and a
clear benefit: parameterised rules need no support at all in the matcher. It is
also what makes an off-side-rule parser — a Python tokenizer written in a Raku
grammar — possible, and writing one is how the interpreter's `:my`
dynamic-variable restore bug was found.

`currentParams` merges outer dynamic-variable parameters into the current
frame's, so a nested rule can see an enclosing rule's parameter.

## Protoregexes

```raku
proto token statement {*}
token statement:sym<if>    { <sym> \s+ <condition> <block> }
token statement:sym<while> { <sym> \s+ <condition> <block> }
```

A `proto` is a dispatch group. Its candidates are collected by name:

```cpp
// src/Regex.h
std::map<std::string, std::vector<std::string>> protos;
```

and `<sym>` inside a candidate matches that candidate's own `:sym<…>` literal —
which is why `MState` carries a `curSym` pointer. A candidate may also be
spelled `:sym«…»`, `name:<…>`, or with a plain adverb as in
`token match:character-class`.

Candidate selection is **longest-token matching**, not declaration order, and
that is the next chapter.

`ParseNode::actualRule` records which candidate won, so `$<statement>.made` can
tell an `if` from a `while`.

## Entry points and start rules

```cpp
bool parse(const std::string& input, const std::string& top, bool subparse,
           ParseNode& out, long& endOut, long startPos = 0);
```

`.parse` requires the whole input to be consumed; `.subparse` does not.
`:rule<name>` picks a start rule other than `TOP`. Both go through the same
function with a flag, so there is one implementation of the anchoring rules.

## Where grammars appear in this book again

Grammars were for a long time the one construct the native code generator
refused, so a program containing one was compiled by bundling rather than
transpiling. That is no longer so: `src/Codegen.cpp` emits the rule table, and
`--exe` on a grammar reports `Compiled (native)` (Chapter 26).
They are also the heaviest exercise in the test suite: the showcase interpreters
for JavaScript, Perl 5, Python 3 and Lisp are grammar-driven, and each of them,
when first written, produced a list of genuine bugs in this engine — the
`:my` restore, a subrule-alias mis-parse, `|` versus `||` ranking, and an
optional-block state corruption among them.
