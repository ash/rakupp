# Grammars

A Raku grammar is a class whose methods can be regexes. That is not a
simplification for the book — it is how it is implemented. `grammar G { … }`
produces a `ClassInfo` with `isGrammar` set, its rules in a string map beside
its methods:

```cpp
// src/Value.h — ClassInfo, the grammar fields
std::map<std::string, std::string> rules;      // name → pattern text
std::vector<std::string> ruleOrder;            // DECLARATION order
std::map<std::string, std::string> ruleKind;   // token / rule / regex
std::map<std::string, std::vector<std::string>> ruleParams;
bool isGrammar = false;
```

Inheritance works because `findRule` walks the parent chain exactly as
`findMethod` does, so `grammar Mine is Base` overrides individual rules.

The pattern text is stored **unparsed**, as the lexer captured it (Chapter 3).
Compilation happens lazily, per rule, on first use.

## The grammar matcher

```cpp
// src/Regex.h
class GrammarMatcher {
public:
    struct Rule { std::string pattern, kind; std::vector<std::string> params; };
    std::map<std::string, Rule> rules;
    std::map<std::string, std::vector<std::string>> protos;
    GrammarHooks hooks;

    bool parse(const std::string& input, const std::string& top, bool subparse,
               ParseNode& out, long& endOut);
    bool matchSub(const std::string& name, const std::string& args,
                  const std::string& capKey, Regex::MState& st, long pos,
                  const FnRef& k);
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
`ClassInfo`, wires the hooks to the interpreter, and runs it.

## A subrule call, in full

`<name>` inside a rule reaches `matchSubMeta`, which is where most of the
engine's cleverness lives. In order:

1. **Resolve the name** — or rather, read the already-resolved metadata,
   which the calling node cached (Chapter 20).
2. **Check the memo**, if this rule is ratcheting.
3. **Compile the rule body**, if this is its first use, and cache it.
4. **Bind parameters**, if the call was `<r($x, 'lit')>`.
5. **Match**, threading the caller's continuation through.
6. **Record a `ParseNode`** in the parent's capture frame.
7. **Fire the action method**, if the grammar's action object has one.

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
    std::map<std::string, std::pair<long, long>> named;
    std::shared_ptr<const ChildMap> kids;      // frozen: replays share
    std::shared_ptr<const std::set<std::string>> listNames;
    std::shared_ptr<const std::set<int>> listCaps;
    std::shared_ptr<const std::map<int,
        std::vector<std::pair<long,long>>>> capReps;
};
std::unordered_map<uint64_t, MemoEntry> memo_;
```

The key is a 64-bit integer built from the rule id, the position and the bound
parameters — not a string, because a string key would allocate on every subrule
call.

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
    std::vector<std::pair<long, long>> caps;
    std::map<std::string, std::pair<long, long>> named;
    std::shared_ptr<const ChildMap> kids;     // null = leaf
    std::shared_ptr<const std::set<std::string>> listNames;
    std::shared_ptr<const std::set<int>> listCaps;
    std::shared_ptr<const std::map<int,
        std::vector<std::pair<long,long>>>> capReps;
};
using ChildMap = std::map<std::string, std::vector<ParseNode>>;
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

## Actions

An action object is an ordinary Raku object whose methods are named after the
grammar's rules. When a rule completes, its method runs and can call `make` to
attach a value to the match.

The engine fires actions **during** the match, through the hooks:

```cpp
// src/Regex.h — GrammarHooks
std::function<bool(const std::string&)> hasAction;
std::function<void(const ParseNode&)> onRule;
```

`hasAction` gates the node assembly, so a rule with no corresponding method
costs nothing. `onRule` fires the method with the completed node.

Three properties of that design, all deliberate:

- **A later backtrack does not unfire an action.** This matches Rakudo; the
  reasoning is in Chapter 20.
- **A memo replay reuses the first firing** rather than firing again, so
  packrat caching does not multiply side effects.
- **`make` writes into a target on the execution context** —
  `ExecContext::makeTargets` — which is how the value attaches to the right
  match rather than to a global.

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
compilation. So `token line($indent) { $indent \N* }` compiles a different
`Regex` for each distinct indent value — and the compiled-rule cache is keyed on
name plus argument values for exactly that reason.

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
which is why `MState` carries a `curSym` pointer.

Candidate selection is **longest-token matching**, not declaration order, and
that is the next chapter.

`ParseNode::actualRule` records which candidate won, so `$<statement>.made` can
tell an `if` from a `while`.

## Entry points and start rules

```cpp
bool parse(const std::string& input, const std::string& top, bool subparse,
           ParseNode& out, long& endOut);
```

`.parse` requires the whole input to be consumed; `.subparse` does not.
`:rule<name>` picks a start rule other than `TOP`. Both go through the same
function with a flag, so there is one implementation of the anchoring rules.

## Where grammars appear in this book again

Grammars are the one construct the native code generator refuses (Chapter 25),
so a program containing one is compiled by bundling rather than transpiling.
They are also the heaviest exercise in the test suite: the showcase interpreters
for JavaScript, Perl 5, Python 3 and Lisp are grammar-driven, and each of them,
when first written, produced a list of genuine bugs in this engine — the
`:my` restore, a subrule-alias mis-parse, `|` versus `||` ranking, and an
optional-block state corruption among them.
