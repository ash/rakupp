\part{Boundaries}

# Modules

A module is **source text that gets lexed, parsed into its own `Program`, and
executed exactly once** — at the point its `use` statement runs, inside the same
interpreter, on the same object graph as the importing program.

It gets a fresh `Env` chained to the global one for the duration of the load;
when the load finishes, that environment's contents are **copied into the global
`Env`**. After that the module has no ongoing identity: its subs are ordinary
`VT::Code` values in the global scope, its classes ordinary entries in the one
flat class registry. Calling into a module is not a different operation from
calling a sub in the same file.

That paragraph is also the source of nearly every divergence at the end of this
chapter.

## Parse time: only operators are looked at

`use Foo` triggers exactly one compile-time action:

```cpp
// src/Parser.h
void scanModuleOps(const std::string& module);
```

which finds the module's *source file* and **text-scans** it — no lexing, no
parsing — for declarations of the five operator categories, registering those
names in the parser's tables so the rest of the importing file parses
(Chapter 5).

That is the whole compile-time effect, because operators are the only thing
about a module that changes how the importing file **parses**:

```raku
use Vector;          # declares `sub infix:<⋅>`
say $a ⋅ $b;         # a parse error without the scan
```

Two consequences. An operator spelled only in ASCII operator characters is
skipped, because `multi infix:<*>(Color, Real)` is almost always extending a
built-in and registering it would silently reshape every expression in the
importing file. And the scan reads the file a second time — cheap, being a
substring search, but it *can* be fooled by an operator name inside a string or
a comment.

## Run time: `loadModule`

```
loadModule(name)
 ├─ already loaded?                     → return immediately
 ├─ find the source (see below)
 ├─ Lexer → Parser → Program            (its OWN AST)
 ├─ keptPrograms_.push_back(prog)       (the AST must outlive the load)
 ├─ moduleEnv = new Env, parent = global_
 ├─ bind %?RESOURCES / $?DISTRIBUTION into moduleEnv
 ├─ scan the AST for `is export` names
 ├─ cur = moduleEnv;  curPkgEnv_ = moduleEnv
 ├─ hoistSubs(prog->stmts)
 ├─ exec() every top-level statement
 ├─ publish(): copy moduleEnv->vars into global_
 └─ restore cur / curPkgEnv_ / pkgPrefix / finishData_
```

The module's top-level statements run like any other code, once. `BEGIN` blocks
in the module run as part of this — there is no separate compile phase for them
to run in.

`loadedModules_` is checked on entry and the name inserted immediately, so
recursive and repeated `use`s are no-ops. A cyclic `use` therefore will not
hang, but the second module sees a *partially loaded* first one.

## Where the source is found

`libPaths_` starts as `{"lib", ".", "rakulib"}` and is extended:

| Source | Position |
|---|---|
| `RAKULIB` | appended |
| `ROAST` | appended (adds the test-helper library) |
| `-I dir` | prepended |
| `use lib '…'` at run time | prepended |

`RAKULIB` accepts **both** `,` and `:` as separators, with one carve-out: a `:`
directly after a lone leading letter is a Windows drive letter, not a separator.

That splitting logic is shared rather than duplicated, and the comment in the
header says why:

```cpp
// src/Interpreter.h
// Shared so the PARSER's copy of the search path — which is what finds a
// `use`d module's operator declarations while the importing file is still
// being parsed — cannot drift from the interpreter's. It did: the
// interpreter learned ',' and the parser did not, so `RAKULIB=a,b`
// silently stopped importing operators.
std::vector<std::string> splitSearchPath(const std::string& spec);
```

For each base, both `<base>/` and `<base>/lib/` are tried, with the extensions
`.rakumod`, `.pm6`, `.raku` and `.pm`.

If nothing matches, the **installed** repositories are searched. Resolution
mirrors a real installation repository: SHA-1 the module name, read the short-name
index entry, take its content id, and read the source by that id. So Raku++
loads zef-installed modules directly out of Rakudo's own install tree.

```sh
RAKUPP_TRACE=1 rakupp myprogram.raku
```

```
[LibPaths] [lib] [.] [rakulib]
[Load] JSON::Fast <- source lib/JSON/Fast.rakumod
[Load] Test::Util <- installed /Users/you/.raku dist=E3A0…
```

## Failure is fatal, deliberately

A module that is missing, fails to parse, or throws while loading **aborts the
program**.

It did not always. It used to warn and carry on, and that was a divergence with
real consequences: a `use` that silently vanished left the program running
against a state nobody designed — the module's `BEGIN` blocks never ran, its
exports never materialised. It also flattered every measurement. In the module
compatibility battery, twenty probes "produced output" purely because the load
had been skipped, so the comparison against Rakudo was meaningless for them.

The one exemption is `Slang::*`, which are compile-time grammar mutators Raku++
cannot apply at all; failing on them would reject programs it otherwise runs
perfectly.

Pragmas with no file behind them — `strict`, `fatal`, `experimental`,
`newline`, `precompilation` — are listed **explicitly** rather than being
allowed to fall out of a failed lookup. Ignoring an unimplemented pragma must be
a deliberate entry, not an accident.

## Questions people actually ask

### Is there an `Env` that holds the module?

**During the load, yes; afterwards, no.** `moduleEnv` exists only for the
duration of `loadModule`. Its real job is scoping the module's *own* code: the
module's subs are defined there so they see one another, and each `Callable`
captures it as its closure. That closure is what keeps the module's lexical
world alive after publication — a module sub calling another module sub still
resolves it through `moduleEnv`.

What the importer sees is the *copy* in the global environment. There is no
stash object, no per-module symbol table to enumerate, no `Foo::EXPORT::DEFAULT`.

### Is there a separate AST?

**Yes, and it is never walked as a unit again.** Each module gets its own lexer,
parser and `Program`. That `Program` is executed once, top to bottom, and then
survives only as *storage*: `Callable::params` and `Callable::body` are borrowed
raw pointers into it (Chapter 6), which is why it is pushed onto
`keptPrograms_` and never released.

Parsing is isolated in **one direction only**. The module is parsed with a fresh
`Parser`, so the importing file's operators, lexical regexes and sigilless names
do not leak into it. In the other direction the module's operators *do* reach
the importer — through the text scan, not the parse.

### Is calling a module routine different?

**No.** `evalCall` resolves every named call the same way (Chapter 15), and
`Env::find` walks the parent chain. A local `my sub` is found nearby; a module
sub is found in the global environment at the end of the chain. The only
difference is a slightly longer walk to *find* it; the call itself is
byte-for-byte identical.

The same holds for methods: the class registry is one flat map, so a class from
a module and a class from the current file are indistinguishable at dispatch
time.

### How does `is export` work?

`is export` is scanned for, but it is **not** what makes a symbol visible. Its
one real effect today is the built-in collision carve-out in `publish()`:

> A sub that is **not** `is export` and whose name collides with a built-in
> stays module-private.

That is what lets a module's `our sub run` coexist with the built-in `run`:
inside the module, `run` resolves through `moduleEnv` to the module's own; an
importer's bare `run(...)` reaches the built-in.

The `sub EXPORT(*@_)` protocol *is* implemented: if the module defines it, it is
called with the `use` statement's arguments and the map it returns is defined in
the **using** scope. `use Mod <name:alias>` imports a routine under a second
name.

### `our` and `unit module`?

`tctx_.pkgPrefix` is the mechanism. `unit module Foo;` sets the prefix for the
rest of the compilation unit; a braced `module Foo { … }` sets it for the body
and restores it after. Then an `our sub bar` is also published as `&Foo::bar`, an
`our` variable as `$Foo::x`, a `my` one not at all, and a class or grammar
registers under its qualified name.

That last one was a bug until it was not: compound names skipped the prefix, so
`grammar Schema::Core` inside `unit module YAMLish` registered unqualified and
was unreachable from any importer.

### When do a module's `END` blocks run?

At **process** end, not at load, and *before* the mainline's own `END` blocks,
last-in-first-out, so a module loaded later cleans up earlier. They capture the
module scope, so they still see its lexicals.

### What about `--exe`?

Modules are **not** compiled into the binary. The generator emits a call to
`Interpreter::rtUse`, a thin mirror of the interpreter's `use` handling, calling
the same `loadModule`. A compiled binary still interprets its modules — though
it can carry their serialised ASTs inside itself (Chapter 24), so it needs
neither their sources nor a warm cache to start.

## Divergences from Rakudo

These are known and mostly deliberate. They are the reason a module can behave
differently under Raku++ than under Rakudo even when nothing is broken.

| # | Raku++ | Rakudo |
|---|---|---|
| 1 | `publish()` copies the module's **whole** environment to global. A `my sub`, a non-exported `our sub`, and its classes are all visible bare to the importer. | Only `is export` symbols are imported. |
| 2 | `need Foo` still makes Foo's symbols visible. | `need` loads without importing. |
| 3 | Types resolve **at run time**. `NoSuchType.new` is a run-time "no such method", after output has appeared. | `Undeclared name` at compile time. |
| 4 | The parse is cached; declarations and `BEGIN` still run every time. | `.precomp` caches the parse *and* the objects compile-time code produced. |
| 5 | A module's operators reach the importer by **text scan**. | Real lexical export of the operator into the importer's grammar. |
| 6 | One flat class registry — no per-compunit type isolation. | Each compunit has its own stash. |
| 7 | `use Foo:ver<…>:auth<…>` adverbs are accepted and **discarded**; the first match on the search path wins. | Full version, auth and API resolution. |

Divergence 1 is the big one and it cuts both ways. It is why some modules work
under Raku++ that would otherwise need more machinery, and it is why a module's
internal helper can silently shadow something in the importing program.

Demonstrated with a module declaring `my sub private-sub`, `our sub our-sub`,
`sub exported-sub is export` and `class Widget`:

| Called from the importer | Raku++ | Rakudo |
|---|---|---|
| `private-sub()` | `"private"` | not visible |
| `our-sub()` | `"our"` | not visible |
| `exported-sub()` | `"exported"` | `"exported"` |
| `Demo::our-sub()` | `"our"` | `"our"` |
| bare `Widget.greet` | `"widget"` | not visible |

## Debugging a module problem

1. **`RAKUPP_TRACE=1`** — confirm *which* file was loaded. Half of all module
   bug reports are the wrong copy being picked up, usually because `.` is on the
   search path or an installed copy shadowed a checkout.
2. **`RAKULIB` takes `,` or `:`.** If the same value also has to drive Rakudo,
   write `,`.
3. **Instrument the real module, not a reduction.** Copy its `lib/` to a scratch
   directory, add `note` calls, run it under both engines. Reductions of
   grammar and module behaviour have repeatedly *passed* while the real module
   failed — the reduction drops the one piece of context that mattered.
4. **Load failures are loud now**, so a module that appears to do nothing is
   loading fine and behaving differently, not being silently skipped.
