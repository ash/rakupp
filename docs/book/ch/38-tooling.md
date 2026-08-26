\part{Around the Compiler}

# Tooling Built on the AST

An implementation is judged partly on things that are not the language:
whether it can say a program is wrong before running it, colour it, time it, or
let someone try one line at a time. Those usually arrive as separate projects
that re-parse the language from the outside and drift away from it. Here they
are all in the same binary, reading the same tree the interpreter reads.

Once a program is a tree, several useful things become short. This chapter
covers the tools that are built on the front end rather than on the runtime, one
that is built on the call path, and one thing that is not a tool at all: a check
the compiler always runs, whose only job is to stop the program.

## `--lint`: static analysis that runs nothing

```cpp
// src/Lint.h
struct LintFinding {
    int line = 0;
    char severity = 'W';        // 'W' warning, 'N' note
    std::string rule;           // a stable id, e.g. "unused-variable"
    std::string message;
};
std::vector<LintFinding> lintProgram(Program& prog);
```

It walks an already-parsed tree and reports findings sorted by line and rule. It
does not execute the program.

The design constraint is stated in the header and is the whole reason the rule
set is small:

> Rules are deliberately conservative — a missed warning is acceptable, a false
> one is not.

Raku's dynamism is why. Interpolation, dynamic variables, `EVAL`, symbolic
references and introspection all mean that a variable which *looks* unused may
be reached by name at run time. An over-eager analyser in this language produces
warnings people learn to ignore, at which point the tool is worse than nothing.

Every finding carries a **machine-readable rule id** as well as prose, so a
project can suppress or gate on a specific rule without matching message text.

## The undeclared-variable gate: the same principle, with the program stopped

```cpp
// src/DeclCheck.h
struct UndeclaredVar { std::string name; int line; };

using Path = std::vector<std::string>;   // the module search path

std::vector<UndeclaredVar> findUndeclaredVars(const Program& prog,
                                              const std::string& src,
                                              const Path& searchPath);
```

`--lint` is opt-in and only warns. This one nobody asks for and it refuses to
run the program. It is worth reading straight after the linter, because it is
the same design constraint with the consequence raised as far as it goes: a
false warning is an annoyance, a false *refusal* means a working program will
not start.

The problem it solves is one of timing. An undeclared variable was always an
error — the interpreter throws `X::Undeclared` from `lvalueOf`/`evalVarExpr` —
but only when execution *reached* the reference. So

```raku
my $x = 42;
say $x;
say $y;
say "done";
```

printed `42` and then died, where a compiler refuses the file. `-c` was worse: it
parsed the same program and answered `Syntax OK`. And `--exe`, which never asks
the question at all, emitted C++ naming a variable it had never declared and let
the C++ compiler report it:

```
1.rakupp.gen.cpp:38:46: error: use of undeclared identifier 'v_sy';
                        did you mean 'v_sx'?
```

which is a diagnostic about generated code the author never wrote.

So the tree is now asked about the whole unit before any of it runs, and the
answer is a compile-time report:

```
===SORRY!=== Error while compiling 1.raku
Variable '$y' is not declared
at 1.raku:3
------> say ⏏$y;
```

### Where it hangs

Not on the interpreter, and deliberately not on every entry point.

```
rakuppRun ─────► rakuppRunOn(…, declCheck = true)  ─► [gate] ─► run
rk_run ────────► rakuppRunOn(…, declCheck = false) ─────────► run
-c --cpp --bundle --aot --exe ─► [gate] ─► the mode's own pipeline
REPL ──────────────────────────► one line at a time, no gate
```

`rakuppRun` is the CLI's funnel and asks for the gate; `rakuppRunOn` is what an
**embedding host** calls through `rk_run`, and it does not. A host's interpreter
may already hold globals it installed through the extension ABI, and no static
pass over *this* source can see those. The REPL is exempt for a different
reason: each line is its own compilation unit, so the late error costs one line,
which is what a prompt is for. `--mcp` reaches the interpreter through the same
embedding API and is exempt with it.

The two interpreter call sites bracket both branches of the run path — the fresh
parse and the precompiled-AST cache hit (Chapter 30) — because a cached tree is
the same tree and must be asked the same question.

### Two layers, and why the second one exists

The first layer is an AST walk with real lexical scoping. It is
position-**insensitive** within a scope: a declaration anywhere in the enclosing
block counts, even below the use, because the engine is that lenient in places
and a static check must never be stricter than the engine it guards. It knows
the things a text scan could not:

```
$^bb                     IS the declaration of $bb in its block
our $x                   installs into the package — a sibling scope sees it
loop (my $i = 0; …)      builds no implicit block; $i outlives the loop
has $x                   with no twigil, is read as a bare $x in the class
repeat { … } while $c    evaluates the condition inside the body's scope
my $a = 1 if $c          declares $a even when $c is false
```

The second layer is a scan of the **source text**, and it only ever removes
findings: a candidate survives only if the text, too, never spells that name as
a declaration — anywhere in the file, in any scope. That layer is there because the parser silently drops
things. `repeat until $c -> $x { … }` has nowhere in `RepeatStmt` to put `$x`;
`with $e -> (Int() $v is copy) { … }` cannot fit a destructuring signature in
`GivenStmt::var`; and `stripPseudoPkg` rewrites `$OUR::x` and `$CALLER::y` down
to a bare `$x`/`$y` before the AST ever sees them. Each of those makes a
correctly declared variable look undeclared, and refusing such a program would
be much worse than the late error being replaced.

What the backstop costs is the cross-scope case — a name declared in one routine
and used in another is not reported. What it buys is that a finding means *this
name is declared nowhere in this file*, which no gap in the parser can falsify.
The two layers are not redundant: the AST pass knows about placeholders and
signatures, which no text scan could; the text scan knows about binders the AST
lost. Neither alone is both safe and useful.

### Standing down

The same instinct, taken to its conclusion: several constructs end the check for
the whole unit rather than risk a guess.

| Construct | Why nothing can be known |
|---|---|
| `EVAL` / `EVALFILE` | compiles new code against this scope at run time |
| `::($name)` | names any symbol at run time — and assigning through one creates it |
| `require` | imports a set of names only the run can determine |
| `no strict` | is precisely the pragma that makes an undeclared variable legal |
| `use lib $expr` | a computed search path: any module could be behind it |

Imports are the one case that is resolved rather than surrendered to, because a
module may export a **variable** (`our $setting is export`) and that name is
declared nowhere in the importing file. Before reporting anything, the imported
module's source is located with `rakuppFindModuleSource` — the loader's own
resolver, over the loader's own search path plus any literal `use lib` — and
searched for the name. A module that cannot be found, or one carrying a
`sub EXPORT` that can export whatever it likes, ends the check.

That lookup reads files, so it is deliberately the **last** thing that happens:
it runs only once a candidate already exists, which is to say only on the way to
refusing the program. A working run never touches the disk for it.

The match there has to be on the whole name. Matching a prefix let a module's
`$setting` clear a bogus `$s` in the program, which quietly switched off
detection for most short names in any program that imports anything — a good
illustration of how an over-eager *clearing* rule fails silently, in the
direction that is hard to notice.

### The predicate that must not drift

Which names are exempt — twigils, `$_`, `$/`, `$!`, `$0`…, `@_`, `%_`, `$a`/`$b`,
every `&`-sigil name, anything package-qualified — is not re-implemented here.
`isSpecialVar` in `Interpreter.cpp` stopped being `static` and is declared in
`Interpreter.h`, so this pass calls the same function the throw sites call. Two
copies of that list would drift, and the failure mode of drift is this check
refusing a program the interpreter runs happily. Writing it down once was also
how `$¢`, the match cursor, turned out to be missing from it.

### What it costs

One extra walk of the tree, once, before the program starts: about 0.15 µs per
line of source — 0.32 ms for a 2,200-line program. That is 2–3% of a
parse-and-exit (`-c`), 0.2–0.6% of a real run, and immeasurable for anything
long-lived. `RAKUPP_NO_DECLCHECK=1` switches it off, for the day it is wrong
about a program that works.

## `--highlight`: colouring with the compiler's own knowledge

```cpp
// src/Highlight.h
std::string highlight(const std::string& source, const std::string& format);
```

Two renderers: `html` produces the same CSS token classes Pygments uses, so
existing stylesheets apply unchanged; `ansi` produces terminal escapes.

The class names are borrowed, but the classification is not. A regex-based
highlighter has to guess; this one knows. `$obj.role` is coloured as a method
call rather than as the keyword `role`, because the lexer and parser already
established which it is.

That is the general advantage of building a highlighter inside the compiler, and
it is why the output is worth the coupling.

## `--profile`: a routine-level wall-time profiler

```cpp
// src/Profiler.h
namespace prof {
    extern bool on;              // read on the hot path
    void setDest(const std::string& dest);
    void enter(const void* key, const char* name, const char* file);
    void leave();
    void report();
}
```

Two hooks — `callCallableRaw` for routines with a frame boundary, and
`invokeMethod` — feed a per-thread shadow stack. Per routine it aggregates call
count, inclusive and exclusive wall time.

Three decisions worth naming.

**Builtins are attributed to their caller**, because the hooks are on *user
code* routine entry rather than on the builtin dispatch chain. That is a
deliberate simplification: it keeps the hot path to one branch, and a profile
that says "this routine spent 40% of its time in built-ins" is usually the
answer you wanted anyway.

**Recursion is handled the standard way**: only the outermost active frame of a
routine adds to its inclusive time, so a recursive function's inclusive figure
does not count the same seconds many times.

**The off-cost is one predicted branch on a plain `bool` per call**, measured at
zero against the noise band before it was built. A profiler that cannot be
compiled in unconditionally is a profiler people forget to use.

The destination follows Rakudo's convention — the extension selects the format:
`-` writes a table to standard error, a path writes the table there, and a path
ending in `.json` writes a machine-readable dump.

One portability note is recorded in the header: everything in it is portable
C++17, with no `__builtin_expect` and no attributes, because the prototype's
GCC-isms broke the MSVC build once already.

## The REPL

```cpp
// src/Repl.h
int rakuppRepl(const std::string& exePath,
               const std::vector<std::string>& libPaths);
bool stdinIsTerminal();
bool replForced();
```

The REPL is entered **only** by a bare `rakupp` attached to a terminal. Anything
arriving on a pipe or a redirect is a complete program and keeps running as one;
the terminal test in `main` is the whole of that decision.

It lives in the executable rather than the runtime library, so nothing about an
interactive session is linked into the binaries the compiling modes produce.

A REPL never calls `run()`. It keeps **one** `Interpreter` alive and feeds it
`evalString` per line, so the mainline scope *is* the session. Two functions
cover what `run()` would otherwise have done at either end:

```cpp
// src/Interpreter.h
void replStart(std::vector<std::string> args);  // define @*ARGS, arm `state`
void replFinish();                              // run END phasers, once
std::vector<std::string> replNames() const;     // for tab completion
```

The nicest detail is how an incomplete line is recognised. A parse that dies on
end-of-input is a request for more input, not a syntax error:

```cpp
// src/Parser.h — ParseError
bool atEof = false;
```

```cpp
// src/Interpreter.h
Value evalString(const std::string& src, bool mainlinePH = false,
                 bool* incompleteOut = nullptr);
```

The flag is set by the lexer's runaway-construct diagnostics and by the parser
when it fails on the end token, so typing `sub f {` at the prompt asks for a
continuation line while `sub f )` reports an error. One boolean, threaded from
the lexer to the prompt.

## Pod and `--doc`

```cpp
// src/Pod.h
std::vector<Value> parsePod(const std::string& src);
```

Pod blocks are parsed into the `$=pod` DOM — a list of `Pod::Block` values, each
a `Hash` tagged `"Pod"` with a class, a name, a level, a configuration and
contents. Delimited (`=begin`/`=end`), paragraph (`=for`) and abbreviated
(`=head1 …`) forms, nested blocks, and whitespace-collapsed paragraphs.

Note the representation: a Pod block is not a new `VT` or a C++ class. It is a
tagged `Hash`, which is the same technique `Set`, `Proxy` and `DateTime` use
(Chapter 8). The Raku program manipulating it sees an ordinary object.

Declarator documentation — `#|` above a declaration, `#=` beside or below it — is
collected by the lexer keyed by line and attached by the parser, answering
`.WHY` on routines, classes and attributes.

`--doc` runs `DOC` phasers and prints the rendered content, and `=finish` data
travels from the lexer to `$=finish`.

## `--dump-ast`, and the tool built on it

```sh
rakupp --dump-ast prog.raku
RAKUPP_DUMPTOKENS=1 rakupp prog.raku
```

`AstDump.cpp` prints the tree as an indented outline. It is the first thing to
reach for when a parse produces something unexpected.

It is also an **interface**, not just a debugging aid. `tools/ast-opportunity.raku`
reads its output and counts syntactic patterns across a corpus — which is the
tool that measured, in two minutes, that constant folding had almost nothing to
fold in real Raku and that operand shapes were 25 per thousand nodes
(Chapter 19). A textual tree dump turned out to be a perfectly good static
analysis substrate.

## The tools that are not in the binary

A standing rule of the project: **build the ecosystem tooling in Raku, run it
with rakupp.** Not because it is elegant, but because it is the largest
available body of real-program testing.

| Tool | What it does |
|---|---|
| `tools/run-roast.raku` | the Roast harness |
| `tools/run-bench.raku` | the benchmark harness, three engines |
| `tools/perf-guard.raku` | the release performance gate |
| `tools/run-optbench.raku` | compiles each optimiser showcase twice, checks byte-identical output, then times |
| `tools/gen-unicode.raku` | generates Unicode tables |
| `tools/ast-opportunity.raku` | counts AST patterns |
| `tools/doc-examples-diff.raku` | runs every documented example on both engines |
| `tools/recheck-divergences.raku` | re-tests the recorded divergence list |

Each of these is a substantial Raku program that runs on every release. When one
of them breaks, it has usually found a bug in the compiler rather than in
itself — which is exactly the point of the rule.

The showcase programs go further. Interpreters for JavaScript, Perl 5, Python 3,
Lisp and Forth, each written in Raku, each producing byte-identical output to the
real implementation on its examples. Writing one is the single most productive
bug-finding activity in the project's history: each of them, on first
completion, yielded a list of genuine defects — grammar mis-parses, a lost
`return`, dynamic-variable restore, slip flattening, precedence traps.

An interpreter for another language exercises grammars, recursion, closures,
string handling and dispatch simultaneously and at a scale no unit test reaches.
It is a test suite that writes itself, and it has an oracle: the language it
implements already has one.
