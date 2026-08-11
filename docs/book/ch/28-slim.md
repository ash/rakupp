# What a Compiled Binary Keeps

A `--exe` binary carries the whole runtime, and most of the runtime is Unicode
tables. `say "Hello"` used to compile to 9.9 MB, of which the program's own code
was a rounding error and the character-name table alone was 3.1 MB — data that
program could not reach even in principle.

`--slim` is the flag that removes what a program cannot use. The interesting
part is not the flag; it is what has to be true of the runtime before a linker
can be *asked* to leave something out, and how the compiler decides what is
safe to ask for.

## The problem is the linker's, not the compiler's

A static archive is pulled in by object file. If any symbol in an object is
referenced, the whole object comes along. So a table is droppable only when
**nothing in the binary refers to it** — and the runtime referred to everything,
because `Unicode.cpp` named every table directly.

That gives the shape of the work, and it came in stages: make the tables
reachable only through a seam; split the runtime so each cuttable thing is its
own archive; build a stub archive that stands in the gap; and only then write
the analysis that decides which gaps to open.

## The seam

```cpp
// src/ucd_seam.h
namespace ucd {
struct NameEnt { const char* name; uint32_t cp; };
const NameEnt* namesTable(size_t*);
const int64_t* numvTable(size_t*);
// …collation, script/block/bidi ranges…
}
```

Four cuttable table groups, each reached **only** through an accessor defined in
the same translation unit as its data. That co-location is the whole mechanism:
the accessor is the only symbol anyone outside references, so replacing the
object replaces the data.

Two details in the header are worth lifting out.

**`Unicode.cpp` no longer declares the tables at all.** A new direct reference is
therefore a compile error rather than a quiet hole in the seam. The seam is
enforced by the language, not by a convention someone has to remember.

**The never-cut tables keep their direct references on purpose** — general
category, binary properties, normalization, case mapping, grapheme break. Those
are reached by ordinary string operations, so no program can prove it does not
need them, and a seam there would be indirection with no stub ever standing
behind it.

There is a performance constraint on the seam, and it is stated where the code
is:

> Accessors return pointer and count **once**; callers hoist both into locals
> before any loop, so inner loops still index raw memory.

Collation touches its table once per collation *element*, so an accessor call
per element would have been a measured regression. The gate for that stage was
`perf-guard` flat on the collation paths.

## Five archives

```cmake
foreach(feat rakupp_ucd_names rakupp_ucd_coll rakupp_ucd_props
             rakupp_parse rakupp_stubs)
  add_library(${feat} STATIC ${${FEAT_VAR}})
endforeach()
```

The runtime became `librakupp_rt.a` plus four feature archives and one stub
archive. Three hold Unicode data. The fourth, `rakupp_parse`, holds the **lexer
and parser** — because a compiled program that never calls `EVAL` does not need
a Raku compiler inside it.

That last one creates a genuine cycle: `Runtime.cpp` drives the parser, and the
parser builds AST nodes whose helpers live in the runtime. Rather than leave it
to link order, the cycle is declared:

```cmake
target_link_libraries(rakupp_rt    INTERFACE rakupp_parse)
target_link_libraries(rakupp_parse INTERFACE rakupp_rt)
```

which lets CMake repeat the archives for a single-pass linker instead of
producing an unresolved symbol on some platforms and not others.

## The stubs

```cpp
// src/stubs/stub_ucd_names.cpp
const NameEnt* namesTable(size_t*) {
    featureMissing("unicode-names",
                   "uniname/uniparse (the Unicode name table)");
}
```

One object per feature, in `librakupp_stubs.a`, linked **in place of** the real
archive. The linker pulls exactly the stubs whose archive is absent.

The stubs are deliberately not part of the runtime or of `librakupp`, and the
comment says why: an interpreter carrying both the real table and a throwing
double would be a one-definition-rule violation waiting for a link-order change.

What they throw is an ordinary typed Raku exception:

```cpp
// src/FeatureGate.cpp
throw FeatureNotBuilt{{Value::typeObj("X::Feature::NotBuilt"),
    std::string(neededFor) + " needs the '" + feature +
    "' feature, and this binary was compiled without it (--slim=-" +
    feature + "). …"}};
```

Catchable with `when X::Feature::NotBuilt`, naming the feature, the operation
that wanted it, and the flag that would put it back. **A cut feature is a build
decision the program ran into, not an internal error** — so it is reported the
way a program can handle rather than as a crash.

`featureMissing` lives in the runtime rather than in a stub, so both sides of
the seam can reach it.

## The levels

Four, at most one per spec:

| level | what it does |
|---|---|
| `none` | nothing: no dead-strip, symbols kept. For debugging a binary. |
| `safe` | **the default with no flag.** Dead-strip and symbol strip. No feature removed, no analysis run. |
| `auto` | **what bare `--slim` means.** `safe`, plus every feature the scan *proves* unreachable. Sound. |
| `max` | `auto`, ignoring the force-full triggers. Unsound by design. |

`safe` being the default matters more than it sounds: it costs nothing
analytically and takes `say "Hello"` from 9.9 MB to 8.1 MB, byte-identical in
behaviour. Its only real cost is that a C++-level crash reports addresses rather
than names, which is why `+symbols` exists.

A spec naming no level means `auto`, so `--slim=+eval` reads as "automatic
pruning, but keep eval".

## The scan

```cpp
// src/SlimScan.h
struct SlimScanResult {
    bool used[4];                        // per feature
    std::vector<std::string> triggers;   // why everything was kept
    struct Site { int feat; std::string what; std::string where; int line; };
    std::vector<Site> sites;             // every use, in walk order
};
SlimScanResult slimScan(const Program& prog,
                        const std::vector<BundledModule>& mods);
```

It walks the parsed program **and every module embedded alongside it**, asking
per feature: can any site reach this? The governing rule is one sentence in the
header:

> The default answer to uncertainty is always "keep".

The detectors are more specific than "does the name appear". `uniprop('Script')`
marks `unicode-props`; `uniprop('Name')` marks `unicode-names` instead, because
Name and Numeric_Value live in the names tables; a computed property name marks
both. A bare `&uniprop` reference marks both, because the argument is unknown.
`<:Script<…>>` inside a regex marks props; `\c[…]` marks names, because the
engine resolves that name at match time.

The `eval` feature has the widest surface, and most of it is not spelled `EVAL`:
`require`, `use-ok`, `eval-dies-ok`, a regex code block, `:my` in a regex, an
`s///` replacement that interpolates, `throws-like` with code in a string. Each
of those reaches `evalString` at run time, so each keeps the parser.

## The triggers

Some constructs mean the program can run code the scan never saw. Under `auto`
any of them keeps **everything**:

```
EVAL / EVALFILE / require
a symbolic reference (::($name))
an indirect method call (."$name"())
a metamodel lookup (.^lookup)
a regex interpolating a subregex (<$var> / <{…}>)
a `use`d module that could not be embedded alongside the program
an AST node the scan does not model
```

That last one is the important one, and it is what makes the analysis
maintainable: **a node kind the walker does not recognise is itself a trigger.**
Adding a construct to the language cannot silently create a hole in the scan;
the worst it can do is make `auto` conservative until someone teaches the walker
about it.

Literal regex code blocks are **not** triggers. Their source is visible, so the
scan parses and walks them like any other code.

And the triggers are reported by name and line, rather than merely suppressing
the cut:

```
$ rakupp --exe trig.raku --slim=list
--slim=auto for trig.raku (--exe):
  unicode-names      keep    3.1 MB   kept: a dynamic construct keeps everything
  …
dynamic constructs keeping everything (--slim=max cuts anyway):
  an indirect method call (."$name"()) (in the program, line 2)
would cut 0 KB of 5.0 MB cuttable (archive sizes)
```

## What it is worth

```
$ rakupp --exe hello.raku --slim=list
--slim=auto for hello.raku (--exe):
  unicode-names      cut     3.1 MB   proven unused
  unicode-collation  cut     743 KB   proven unused
  unicode-props      cut     141 KB   proven unused
  eval               cut     971 KB   proven unused
would cut 5.0 MB of 5.0 MB cuttable (archive sizes)
```

`say "Hello"`: 9.9 MB unstripped, 8.1 MB at `safe`, **4.6 MB** under bare
`--slim`. A program that uses one feature keeps one:

```
$ rakupp --exe uses-names.raku --slim=list
  unicode-names      keep    3.1 MB   used: uniname (line 1)
  unicode-collation  cut     743 KB   proven unused
  unicode-props      keep    141 KB   used: <:Script<…>> in a regex (line 2)
  eval               cut     971 KB   proven unused
```

## The directives, and the one that proves it

Four, one per spec:

| directive | |
|---|---|
| `help` | the grammar and the feature table, with the real archive sizes beside *this* rakupp |
| `list` | keep/cut per feature for this program, with the reason and the bytes. Analyses only; does not compile |
| `why:FEAT` | every site — program or module, with the line — that forces FEAT to be kept |
| `verify` | build the slim binary **and** a full reference, run both, and emit the slim one only if stdout, stderr and exit status agree |

`verify` is the interesting one, because it answers the objection the whole
feature invites: how do you know the analysis was right? By not trusting it —
build both, run both, compare. A nondeterministic program cannot agree with
anything, so `verify` refuses that too rather than reporting a false difference.

It composes with an explicit cut, which is exactly when you want the proof:
`--slim=safe,-eval,verify` is "I claim this program never compiles code at run
time; prove it".

`help` deserves a note of its own. It prints the real archive sizes measured
from the rakupp you are running, not numbers baked into a string — so the
documentation cannot drift from the build.

## Where it declines

Two modes refuse the scan, loudly rather than quietly.

**`--bundle`** embeds source and parses it at run time, so nothing can be proven
unused — and `--slim=-eval` is refused there outright, because bundling *is* the
eval feature. **`--aot`** keeps every feature until the scan is wired for it.
Explicit `±feature` still applies in both.

`--slim` shapes the link, so it applies to the compile modes only. The
interpreter never slims.

## The manifest

Every compiled binary embeds a one-line build manifest — version, mode, slim
level, cut list — which `rakupp --exe-info BIN` prints.

Keeping a string alive in a binary that was built to drop unreferenced data is
its own small problem, and the solution is in `FeatureGate.cpp`:

```cpp
static const char* g_exeManifest = nullptr;
void rakuppKeepManifest(const char* m) { g_exeManifest = m; }
```

A pre-main initializer parks the pointer there. The call is what anchors it:
**an escaped address into another translation unit is beyond any optimizer's
reach.** And because the reader scans bytes rather than symbol tables, the
manifest survives symbol stripping — `strings BIN | grep RAKUPP-EXE` finds it
too.

## The shape of the argument

`--slim` is a static analysis whose failure mode was designed before the
analysis was. Every layer has an answer to being wrong:

- an unmodelled construct is a **trigger**, so the scan fails towards keeping;
- a wrong cut throws a **typed, catchable exception** at the point of use, never
  a crash and never a wrong answer;
- `verify` will **build both and compare** rather than ask to be believed;
- and `max`, the one unsound level, says so in its own help text.

That ordering is the point. An optimisation that removes code from a program is
only as good as its worst case, and the worst case here was decided first.
