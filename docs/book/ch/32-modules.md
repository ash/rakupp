\part{Boundaries}

# Modules

A module is **source text that gets lexed, parsed into its own `Program`, and
executed exactly once** — at the point its `use` statement runs, inside the same
interpreter, on the same object graph as the importing program.

It gets a fresh `Env` chained to the global one for the duration of the load;
when the load finishes, **what the module exports is copied out of that
environment** into the scope that said `use` — the global `Env` for a `use` in
the mainline, the block's own for a `use` inside a block or a routine. After
that an imported sub is an ordinary `VT::Code` value in the importer's scope,
and the module's classes are ordinary entries in the one class registry.
Calling into a module is not a different operation from calling a sub in the
same file.

The copy used to be the module's *whole* environment, into global, and the
first drafts of this chapter said so. The divergences at the end are what is
left of that design.

## Parse time: only operators are looked at

`use Foo` triggers exactly one compile-time action:

```cpp
// src/Parser.h
bool scanModuleOps(const std::string& module);
```

which finds the module's *source file* and **text-scans** it — no lexing, no
parsing — for declarations of the five operator categories, registering those
names in the parser's tables so the rest of the importing file parses
(Chapter 6). It finds the file the way the loader will: the search path first,
then the installed stores, so a zef-installed module is scanned as a checkout
is.

Operators are the first thing about a module that changes how the importing
file **parses**:

```raku
use Vector;          # declares `sub infix:<⋅>`
say $a ⋅ $b;         # a parse error without the scan
```

They are not the only thing, and the same scan now collects the rest of what
the parser has to know before it reads on. Each of these arrived with a module
that would not parse without it:

- `sub term:<…>` names, and sigilless constants;
- a `sub` named like a word operator or a quote form (`sub div`, `sub tr`),
  which would otherwise be read as that operator or that quote;
- the type names the module declares;
- an `EXPORTHOW` package declarator, so that `model Foo { … }` is a
  declaration;
- whether the module registers a slang, which is then run and applied to the
  rest of the importing unit.

The scan also follows the module's own `use` lines, because a module that
re-exports another one's names passes its operators on too. It returns `true`
when the lexer has to read the rest of the file again — an imported operator or
quote-word sub can change how the text splits into tokens.

Two consequences. An operator spelled only in ASCII operator characters is
skipped, because `multi infix:<*>(Color, Real)` is almost always extending a
built-in and registering it would silently reshape every expression in the
importing file. And the scan reads the file a second time — cheap, being a
substring search, but it *can* be fooled by an operator name inside a string or
a comment.

## Run time: `loadModule`

```
loadModule(name)
 ├─ the engine answers this name itself? → no load at all
 ├─ being loaded right now?             → refuse: a cycle
 ├─ already loaded?                     → import again, return
 ├─ find the source (see below)
 ├─ Lexer → Parser → Program            (its OWN AST)
 ├─ keptPrograms_.push_back(prog)       (the AST must outlive the load)
 ├─ moduleEnv = new Env, parent = moduleRoot_ → global_
 ├─ bind %?RESOURCES / $?DISTRIBUTION into moduleEnv
 ├─ scan the AST for `is export` names and their tags
 ├─ cur = moduleEnv;  curPkgEnv_ = moduleEnv
 ├─ hoistSubs(prog->stmts)
 ├─ exec() every top-level statement
 ├─ publish(): import what was exported, by tag
 ├─ restore cur / curPkgEnv_ / pkgPrefix / finishData_
 └─ call `sub EXPORT`, if the module has one
```

The module's top-level statements run like any other code, once. `BEGIN` blocks
in the module run as part of this — there is no separate compile phase for them
to run in.

The body runs once, but a repeated `use` is not a no-op: an import is lexical,
so the second `use` of a loaded module imports again into the scope it stands
in. The module's exports were recorded with their tags at the first load, and
its `sub EXPORT` is called again with the new arguments.

`loadedModules_` is checked on entry and the name inserted before the search. A
second set, `modulesLoading_`, holds the names whose bodies are still running,
and a `use` of one of those is refused in Rakudo's words:

```
Circular module loading detected trying to precompile A
```

It used to be allowed, and the second module saw a partially loaded first one.
A load that throws takes its name out of `loadedModules_` again, so the next
`use` of it fails as the first did instead of answering "already loaded".

The first line of the diagram is `Data::Native` and its four siblings
(`JSON::Native`, `CSV::Native`, `Digest::Native`, `Compress::Zlib::Native`):
their functions are inside the binary, so the `use` defines them and nothing
is read from disk. An explicit `-I` copy still wins.

## Where the source is found

`libPaths_` starts as `{"lib", "rakulib"}`, both relative to the current
directory, and is extended:

| Source | Position |
|---|---|
| `RAKULIB` | appended |
| `ROAST` | appended (adds the test-helper library) |
| the `rakulib/` beside the binary, if there is one | appended |
| `-I dir` | prepended |
| `use lib '…'` at run time | prepended |

`RAKUPP_NO_CWD_LIB=1` removes the two defaults, and `--sandbox` does the same:
a sandboxed program loads modules only from where its host said they live.

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

For each base, both `<base>/` and `<base>/lib/` are tried. A `META6.json`
there whose `provides` maps the name is followed first; otherwise the path is
derived from the name, with the extensions `.rakumod`, `.pm6` and `.pm` (under
6.e, the first two). `.raku` is deliberately not among them: it is the
extension a program wears, and while it was accepted a scratch file named
`paths.raku` shadowed the installed `paths` distribution.

If nothing matches, the **installed** repositories are searched. Resolution
mirrors a real installation repository: SHA-1 the module name, read the short-name
index entries, take the newest version that satisfies the `use`, and read the
source by its content id. So Raku++
loads zef-installed modules directly out of Rakudo's own install tree. That
store — its layout, the installer that writes it, and how the engines share
it — is Chapter 34.

The binary carries a few modules itself, and they sit at the two ends of that
order. A **shadow** answers before the search path is consulted:
`NativeHelpers::Blob`, `::CStruct` and `::Pointer`, whose ecosystem versions
read MoarVM's memory layout and cannot run here, and `Arduino` with its two
submodules. A **bundled** module answers only after the stores have failed:
`JSON::Native::Raku`, the fallback behind the engine's own `JSON::Native`.
Their sources are in `rakulib/` and are compiled into the binary as bytes, so
an installed rakupp needs no `rakulib/` beside it. A binary built with `--exe`
goes before all of this: a module embedded in it is taken without looking at
the disk.

```sh
RAKUPP_TRACE=1 rakupp -e 'use JSON::Fast; use NativeHelpers::Blob'
```

```
[LibPaths] [lib] [rakulib] [/Users/you/rakupp/build/../rakulib]
[Load] JSON::Fast <- installed /Users/you/.raku dist=CA11E82B…
[Load] JSON::Fast parse 4.0 ms, run 0.6 ms
[Load] NativeHelpers::Blob <- the shadow compiled into this binary
[Load] NativeHelpers::Blob parse 0.3 ms, run 0.1 ms
```

(Abridged: the `[LibPaths]` line is printed again before every search.) The
second `[Load]` line of each pair says where the load's time went: getting the
tree (`parse`, or `precomp` when it came from the cache, or `embedded`) and
running its top level.

## Failure is fatal, deliberately

A module that is missing, fails to parse, or throws while loading **aborts the
program**.

It did not always. It used to warn and carry on, and that was a divergence with
real consequences: a `use` that silently vanished left the program running
against a state nobody designed — the module's `BEGIN` blocks never ran, its
exports never materialised. It also flattered every measurement. In the module
compatibility battery, twenty probes "produced output" purely because the load
had been skipped, so the comparison against Rakudo was meaningless for them.

`Slang::*` modules used to be exempt, as compile-time grammar mutators Raku++
could not apply. Slangs are applied now, and a slang module that is missing or
will not parse fails like any other.

Pragmas with no file behind them — `strict`, `fatal`, `experimental`,
`newline`, `precompilation` — are listed **explicitly** rather than being
allowed to fall out of a failed lookup. Ignoring an unimplemented pragma must be
a deliberate entry, not an accident.

The error for a missing module lists every place that was searched, and then
says what to do about it:

```
$ rakupp app.raku
Could not find No::Such in:
    lib
    rakulib
    /Users/you/rakupp/build/../rakulib
    /Users/you/.raku
    /opt/homebrew/Cellar/rakudo/2026.09/share/perl6/site
    /opt/homebrew/Cellar/rakudo/2026.09/share/perl6/vendor
app.raku uses a module that is not installed: No::Such
  to install it:          rakupp install app.raku
  to install it and run:  rakupp --auto app.raku
  (X::CompUnit::UnsatisfiedDependency)
```

Under `--auto` the loader asks the installer for the name before it gives up,
and searches the stores once more. That is the one place a `use` can reach the
network, and it is opt-in (Chapter 34). A `require` never does: it is how code
asks whether an optional module is there.

## Questions people actually ask

### Is there an `Env` that holds the module?

**During the load, yes; afterwards, no.** `moduleEnv` exists only for the
duration of `loadModule`. Its real job is scoping the module's *own* code: the
module's subs are defined there so they see one another, and each `Callable`
captures it as its closure. That closure is what keeps the module's lexical
world alive after publication — a module sub calling another module sub still
resolves it through `moduleEnv`.

What the importer sees is the *copy* of each export in its own scope. The
module's export table is kept as well, under Rakudo's names, so
`Foo::EXPORT::DEFAULT::.keys` lists what a plain `use Foo` brings in, and a
program that loads a module with `require` can reach its exports through
`::("Foo::EXPORT::DEFAULT::&name")`.

### Is there a separate AST?

**Yes, and it is never walked as a unit again.** Each module gets its own lexer,
parser and `Program`. That `Program` is executed once, top to bottom, and then
survives only as *storage*: `Callable::params` and `Callable::body` are borrowed
raw pointers into it (Chapter 7), which is why it is pushed onto
`keptPrograms_` and never released.

Parsing is isolated in **one direction only**. The module is parsed with a fresh
`Parser`, so the importing file's operators, lexical regexes and sigilless names
do not leak into it. Nor do they at run time: module scopes hang off
`moduleRoot_`, an `Env` between them and the global one whose job is to keep
the program's own operator definitions out of every module. In the other
direction the module's operators *do* reach
the importer — through the text scan, not the parse.

### Is calling a module routine different?

**No.** `evalCall` resolves every named call the same way (Chapter 16), and
`Env::find` walks the parent chain. A local `my sub` is found nearby; a module
sub imported in the mainline is found in the global environment at the end of
the chain. The only
difference is a slightly longer walk to *find* it; the call itself is
byte-for-byte identical.

The same holds for methods: the class registry is one map, so a class from
a module and a class from the current file are indistinguishable at dispatch
time.

### How does `is export` work?

`is export` **is** what makes a symbol visible, as in Rakudo. `publish()` walks
the module's environment and copies out only what was exported:

- A routine that is not `is export` stays in `moduleEnv`. The module's exported
  subs still reach it through their closure, and an `our sub` is reachable by
  its package name. That is what lets a module's `our sub run` coexist with the
  built-in `run`.
- A plain `is export` is the tag `DEFAULT`. `use Mod :tag` asks for that tag
  and *replaces* the default set; `:ALL` takes everything; `MANDATORY` always
  comes in. A tag the module exports nothing under is `X::Import::NoSuchTag`.
- Variables, constants, enums and types are exported the same way, by their
  tags. A module's `my` variables and unexported `our` variables are never the
  importer's.
- `need Mod` and `use Mod ()` load the module and import nothing.
- The import is lexical: `{ use Mod; … }` leaves nothing behind after the
  block. An exported multi joins the importing scope's own multi of the same
  name, and an import that lands on a routine the importing scope declares
  itself is `X::Redeclaration`.

The `sub EXPORT(*@_)` protocol is implemented too: if the module defines it, it
is called with the `use` statement's arguments and the map it returns is
defined in the **using** scope. `use Mod <name:alias>` imports a routine under a
second name.

### `our` and `unit module`?

`tctx().pkgPrefix` is the mechanism. `unit module Foo;` sets the prefix for the
rest of the compilation unit; a braced `module Foo { … }` sets it for the body
and restores it after. Then an `our sub bar` is also published as `&Foo::bar`, an
`our` variable as `$Foo::x`, a `my` one not at all, and a class or grammar
registers under its qualified name.

That last one was a bug until it was not: compound names skipped the prefix, so
`grammar Schema::Core` inside `unit module YAMLish` registered unqualified and
was unreachable from any importer.

### When do a module's `END` blocks run?

At **process** end, not at load. They are sorted into one last-in-first-out
order with the mainline's own, at the position of the `use` that loaded them:
an `END` the program wrote after the `use` runs before the module's, one
written before it runs after, and a module loaded later cleans up earlier.
They capture the module scope, so they still see its lexicals.

### What about `--exe`?

The generator emits a call to `Interpreter::rtUse`, a thin mirror of the
interpreter's `use` handling, calling the same `loadModule`. The binary carries
its modules' serialised ASTs inside itself (Chapter 25), so it needs neither
their sources nor a warm cache to start, and the interpreter still runs them:
every declaration, export, role and signature binding keeps the one
implementation it has.

What `--exe` adds is a native **body** for each module routine the C++ back
end can compile (`src/AotModules.h`). The interpreter still makes the call —
it dispatches, binds the signature into a fresh frame and runs the phasers —
and where it would walk the body's statements it enters the native function
instead. The two sides agree on which routine a body belongs to by walking the
same deserialized AST in the same order. A body that cannot bind a name it
needs answers false before running anything, and the interpreter walks that
routine itself.

## Divergences from Rakudo

These are known and mostly deliberate. They are the reason a module can behave
differently under Raku++ than under Rakudo even when nothing is broken.

| # | Raku++ | Rakudo |
|---|---|---|
| 1 | A class a module declares inside its package is visible to the importer by its **short** name as well. | Only by its qualified name, unless it is `is export`. |
| 2 | Types resolve **at run time**. `NoSuchType.new` is a run-time "Undeclared name", after output has appeared. | `Undeclared name` at compile time. |
| 3 | The parse can be cached (off by default, Chapter 30); declarations and `BEGIN` still run every time. | `.precomp` caches the parse *and* the objects compile-time code produced. |
| 4 | A module's operators reach the importer by **text scan**. | Real lexical export of the operator into the importer's grammar. |
| 5 | One class registry, with the lexical scoping of a block-level `use` laid over it. | Each compunit has its own stash. |
| 6 | `use Foo:ver<…>` is honoured against the **installed** index (line 1 of a short entry) and, on the lib path, against the `version` in the `META6.json` beside the module. `:auth` is passed to the installer under `--auto` and otherwise not consulted; `:api` never is. | Full version, auth and API resolution. |

This table used to open with two more rows: that the module's whole
environment was copied to global, so a `my sub` and an unexported `our sub`
were callable from the importer, and that `need` imported as `use` did. Both
are gone. Row 1 is what remains of them.

Demonstrated with a module `unit module Demo;` declaring `my sub private-sub`,
`our sub our-sub`, `sub exported-sub is export` and `class Widget`, run under
both engines (Rakudo v2026.09):

| Called from the importer | Raku++ | Rakudo |
|---|---|---|
| `private-sub()` | not visible | not visible |
| `our-sub()` | not visible | not visible |
| `exported-sub()` | `"exported"` | `"exported"` |
| `Demo::our-sub()` | `"our"` | `"our"` |
| `Demo::Widget.greet` | `"widget"` | `"widget"` |
| bare `Widget.greet` | `"widget"` | not visible |

## Debugging a module problem

1. **`RAKUPP_TRACE=1`** — confirm *which* file was loaded. Half of all module
   bug reports are the wrong copy being picked up, usually because the current
   directory's `lib/` is on the search path or an installed copy shadowed a
   checkout.
2. **`RAKULIB` takes `,` or `:`.** If the same value also has to drive Rakudo,
   write `,`.
3. **Instrument the real module, not a reduction.** Copy its `lib/` to a scratch
   directory, add `note` calls, run it under both engines. Reductions of
   grammar and module behaviour have repeatedly *passed* while the real module
   failed — the reduction drops the one piece of context that mattered.
4. **Load failures are loud now**, so a module that appears to do nothing is
   loading fine and behaving differently, not being silently skipped.
