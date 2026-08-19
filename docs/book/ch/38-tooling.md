\part{Around the Compiler}

# Tooling Built on the AST

An implementation is judged partly on things that are not the language:
whether it can say a program is wrong before running it, colour it, time it, or
let someone try one line at a time. Those usually arrive as separate projects
that re-parse the language from the outside and drift away from it. Here they
are all in the same binary, reading the same tree the interpreter reads.

Once a program is a tree, several useful things become short. This chapter
covers the five tools that are built on the front end rather than on the
runtime, and one that is built on the call path.

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
