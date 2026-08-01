# How modules work inside Raku++

What `use Foo;` actually does, where a module's symbols live, and why some of
this diverges from Rakudo. Everything below is what the code does today
(`src/Interpreter.cpp` `loadModule`, `src/Parser.cpp` `scanModuleOps`), not an
aspiration.

## The one-paragraph answer

A module is **source text that gets lexed, parsed into its own `Program` AST,
and executed exactly once** — at the point its `use` statement runs, inside the
same interpreter, on the same object graph as the importing program. It gets a
fresh `Env` chained to the global `Env` for the duration of the load; when the
load finishes, that env's contents are **copied into the global `Env`**. After
that the module has no ongoing identity: its subs are ordinary `Value`s of kind
`VT::Code` in `global_->vars`, its classes are ordinary entries in the
interpreter's one flat `classes_` map. Calling into a module is not a different
operation from calling a sub in the same file.

## Life of a `use Foo;`

### 1. Parse time — only operators are looked at

`Parser::parseStatement` sees `use Foo` and calls
`Parser::scanModuleOps("Foo")` ([src/Parser.cpp:234](../../src/Parser.cpp)).
That helper finds the module's *source file* and **text-scans it** — no lexing,
no parsing — for `sub`/`multi`/`proto`/`only` declarations of
`infix:<…>` / `prefix:<…>` / `postfix:<…>` / `circumfix:<…>` /
`postcircumfix:<…>`, and registers those names in the parser's user-operator
tables so the rest of the importing file parses correctly.

That is the *whole* compile-time effect. It exists because operators change how
the importing file **parses**, and nothing else about a module does:

```raku
use Vector;          # declares `sub infix:<⋅>`
say $a ⋅ $b;         # would be a parse error without the scan
```

Two consequences worth knowing:

- An operator spelled only in ASCII operator characters is **skipped**
  (`asciiOnlyOp`). `multi infix:<*>(Color, Real)` is almost always extending a
  built-in, and registering it as a user op would give it the default
  precedence and silently reshape every expression in the importing file.
- The scan reads the file a second time (once here, once at load). It is a
  substring search over the raw text, so it is cheap, but it *can* be fooled by
  an operator name inside a string or comment. Nothing has tripped on this yet.

### 2. Run time — `loadModule`

`exec(NK::UseStmt)` ([src/Interpreter.cpp:3421](../../src/Interpreter.cpp))
handles the pragma cases inline (`use v6.d` sets `langRev_`, `use lib`
prepends to `libPaths_`, `use Test` sets a flag) and otherwise calls
`Interpreter::loadModule` ([src/Interpreter.cpp:2698](../../src/Interpreter.cpp)).

```
loadModule(name)
 ├─ already in loadedModules_?           → return immediately
 ├─ find the source file (see §3)
 ├─ Lexer → Parser → Program            (its OWN AST)
 ├─ keptPrograms_.push_back(prog)        (AST must outlive the load — see §5)
 ├─ moduleEnv = new Env, parent = global_
 ├─ bind %?RESOURCES / $?DISTRIBUTION into moduleEnv
 ├─ scan the AST for `is export` names
 ├─ tctx_.cur = moduleEnv;  curPkgEnv_ = moduleEnv
 ├─ hoistSubs(prog->stmts)               (named subs visible before their line)
 ├─ exec() every top-level statement
 ├─ publish(): copy moduleEnv->vars into global_
 └─ restore tctx_.cur / curPkgEnv_ / pkgPrefix / finishData_
```

The module's top-level statements run like any other code. A module with side
effects at the top level performs them here, once. `BEGIN` blocks in the module
run as part of this — there is no separate compile phase for them to run in.

### 3. Where the source is found

`libPaths_` is searched first, then installed repositories.

`libPaths_` starts as `{"lib", ".", "rakulib"}` and is extended, in this order:

| Source | Position | Notes |
|---|---|---|
| `RAKULIB` env var | appended | paths separated by `,` or `:` — both accepted |
| `ROAST` env var | appended | adds `$ROAST/packages/Test-Helpers/lib` |
| `-I dir` on the command line | prepended | last one written wins |
| `use lib '…'` at run time | prepended | takes a single path or a list |

`RAKULIB` accepts **both** `,` and `:` as separators. The one carve-out is a
Windows drive letter: a `:` directly after a lone leading letter and before a
slash (`C:\proj\lib`) is part of the path, not a separator — so a relative
directory named with a single letter cannot be followed by a `:` separator
(`x:/b/lib` is one path). `,` has no such ambiguity.

For each base, both `<base>/` and `<base>/lib/` are tried (so `-I` pointing at a
distribution *root* works, which is how Rakudo resolves it via `META6.json`),
with extensions `.rakumod`, `.pm6`, `.raku`, `.pm`.

If nothing matches, the **installed** repositories are searched:
`~/.raku` plus any Homebrew Rakudo `share/perl6/{site,vendor}`
(`rakuRepoPrefixes()`). Resolution mirrors a real `CompUnit::Repository::
Installation`: SHA-1 the module name, read `<repo>/short/<sha>/<entry>`, take
line 4 as the source content-id, and read `<repo>/sources/<that>`. So Raku++
loads **zef-installed modules directly out of Rakudo's own install tree**.

`RAKUPP_TRACE=1` prints the search path and every resolution:

```bash
RAKUPP_TRACE=1 rakupp myprogram.raku
```

```
[LibPaths] [lib] [.] [rakulib]
[Load] JSON::Fast <- source lib/JSON/Fast.rakumod
[Load] Test::Util <- installed /Users/you/.raku dist=E3A0…
```

### 4. Failure is fatal, deliberately

A module that is **missing**, **fails to parse**, or **throws while loading**
aborts the program. This was not always so — it used to warn and carry on, and
that was a divergence with real consequences. A `use` that silently vanished
left the program running against a state nobody designed: the module's `BEGIN`
blocks never ran and its exports never materialised. It also flattered every
measurement — in the module battery, twenty probes "produced output" purely
because the load had been skipped, so the comparison against Rakudo was
meaningless for them.

The one exemption is `Slang::*`, which are compile-time grammar mutators
Raku++ cannot apply at all; failing on them would reject programs it otherwise
runs perfectly.

Pragmas that have no file behind them (`strict`, `fatal`, `experimental`,
`newline`, `precompilation`, …) are listed explicitly in `loadModule` rather
than being allowed to fall out of a failed lookup — ignoring an unimplemented
pragma must be a deliberate entry, not an accident.

## Answers to the questions people actually ask

### Is there an `Env` that holds the module?

**During the load, yes; afterwards, no.** `moduleEnv` exists only for the
duration of `loadModule`. Its real job is *scoping the module's own code*: the
module's subs are defined there, so they see one another, and each `Callable`
captures `moduleEnv` as its `closure`. That closure is what keeps the module's
lexical world alive after the load — a module sub calling another module sub
still resolves it through `moduleEnv`, even long after `publish()` ran.

What the importer sees is the *copy* in `global_`. There is no `Stash` object,
no per-module symbol table you can enumerate at run time, and no
`Foo::EXPORT::DEFAULT`.

### Is it parsed in isolation? Is there a separate AST?

**Yes to both — but the second AST is never walked as a unit again.** Each
module gets its own `Lexer`/`Parser`/`Program`. That `Program` is executed once,
top to bottom, and then only survives as *storage*: `Callable::params` and
`Callable::body` are **borrowed raw pointers into that AST**
([src/Value.h:59](../../src/Value.h)), which is why the `shared_ptr<Program>` is
pushed onto `keptPrograms_` and never released. Calling a module sub walks the
statements of that module's AST directly; there is no bytecode, no IR, and no
merged tree.

Parsing is isolated in one direction only. The module is parsed with a **fresh
`Parser`**, so the importing file's user-declared operators, lexical regexes and
sigilless names do not leak into it. In the other direction, the module's
operators *do* reach the importer — via the `scanModuleOps` text scan, not via
the parse.

### Is calling a module routine different from calling one in the same file?

**No.** `evalCall` ([src/Interpreter.cpp:14134](../../src/Interpreter.cpp))
resolves every named call the same way:

```cpp
if (Value* f = tctx_.cur->find("&" + c->name))
    return callCallable(*f, std::move(args), &c->args, /*ownFrame=*/false, …);
…
auto it = builtins_.find(c->name);          // built-in fallback
```

`Env::find` walks the parent chain. A local `my sub` is found in a nearby env; a
module sub is found in `global_`, at the end of the chain. So the only
difference is a slightly longer chain walk to *find* it — the call itself
(`callCallable`, frame setup, parameter binding) is byte-for-byte identical. The
`Callable` does not record which file it came from, only which package
(`Callable::pkg`, for `&?ROUTINE.package`).

The same holds for methods: `classes_` is one flat map from name to
`ClassInfo`, so a class from a module and a class from the current file are
indistinguishable at dispatch time.

### Does `use Foo` twice load it twice?

No. `loadedModules_` is checked on entry and the name inserted immediately, so
recursive and repeated `use`s are no-ops. This also means a cyclic `use` won't
hang — but the second module will see a *partially loaded* first one.

### Is there precompilation / a module cache?

**Optionally, for the parse.** With `rakupp --precomp-modules=on` the AST a
module parses to is cached on disk and reused while the source, the rakupp
binary, the operators of anything it `use`s, and the search path all stay put —
see **[CACHING.md](../CACHING.md)**. `use XML` costs 16.0 ms cold and 5.7 ms
warm. The main program can be cached the same way (`--precomp-files=on`), but
that only pays for large files. Both are OFF by default.

What is *not* cached is the second half of a load: a module's top-level
declarations and its `BEGIN` blocks still execute on every run, which is the
~30% of load time that is not parsing. Rakudo goes further and serialises the
objects its compile-time code produced; matching that would mean serialising
live runtime state.

The cache stores the tree the parser built, so a loaded module behaves exactly
as a freshly parsed one — everything else on this page is unchanged by it.
`RAKUPP_NO_PRECOMP=1` turns it off.

### What about `--exe` (native codegen)?

Modules are **not** compiled into the binary. `Codegen` emits a call to
`Interpreter::rtUse(module, arg)` ([src/Codegen.cpp:1312](../../src/Codegen.cpp)),
which is a thin mirror of `exec(UseStmt)` and calls the same `loadModule`. A
compiled binary therefore still needs its modules on disk at run time, and still
interprets them. Only the *main program* is compiled.

### Where do `our` and `unit module` fit in?

`tctx_.pkgPrefix` is the mechanism. `unit module Foo;` with an empty body sets
`pkgPrefix = "Foo::"` for the rest of the compilation unit; a braced
`module Foo { … }` sets it for the body and restores it after. Then:

- an `our sub bar` inside the prefix is also published as `&Foo::bar` in
  `global_`;
- an `our` sigil variable is published as `$Foo::x`; a `my` one is not
  published at all;
- a **class or grammar** declared under the prefix registers under its
  qualified name — `grammar Schema::Core` inside `unit module YAMLish` is
  `YAMLish::Schema::Core`, and a compound name nests the same way a simple one
  does. (This was a bug until 2026-08-01: compound names skipped the prefix, so
  the type was unreachable from any importer.)

`loadModule` saves and restores `pkgPrefix`, so a module's prefix can never leak
into the importing program.

A short-name **alias** is also registered: a qualified class name answers to its
tail (`Path` for `URI::Path`) unless a real class or a built-in already claims
that name (`classAliases_`, [src/Interpreter.cpp:4384](../../src/Interpreter.cpp)).

### How does `is export` work? Does `EXPORT` work?

`is export` is scanned for, but it is **not** what makes a symbol visible —
see the divergence table below. Its one real effect today is the
built-in-collision carve-out in `publish()`:

> A sub that is **not** `is export` and whose name collides with a built-in
> stays module-private.

That is what lets `Test::Util`'s `our sub run` coexist with the built-in `run`:
inside the module, `run` resolves through `moduleEnv` to the module's own; an
importer's bare `run(...)` reaches the built-in.

The `sub EXPORT(*@_)` protocol *is* implemented: if the module defines it, it is
called with the `use` statement's `<…>` arguments and the `Map` it returns
(`'&name' => &code`) is defined in the **using** scope.

`use Mod <name:alias>` imports a routine under a second name.

### `require`?

`require ::($name)` at run time calls the same `loadModule` (with
`quiet=true`, so a missing module is not fatal) and yields the loaded module's
type object, throwing if nothing loadable was found. That shape is what zef's
plugin probe (`(try require ::($m)) ~~ Nil`) relies on.

### When do a module's `END` blocks run?

At **process** end, not at load — and *before* the mainline's own `END` blocks,
LIFO, so a module loaded later cleans up earlier. `File::Temp` registers its
tempfile cleanup this way. They capture the module scope, so they still see the
module's lexicals.

## Divergences from Rakudo

These are known and mostly deliberate; they are the reason a module can behave
differently under Raku++ than under Rakudo even when nothing is "broken".

| # | Raku++ | Rakudo |
|---|---|---|
| 1 | **`publish()` copies the module's whole env to `global_`.** A module's `my sub`, its non-exported `our sub`, and its classes are all visible bare to the importer. | Only `is export` symbols are imported; everything else needs the qualified name, or is invisible. |
| 2 | `need Foo` still makes Foo's symbols visible (same `publish()`). | `need` loads without importing. |
| 3 | Types are resolved **at run time** via `classes_`. `NoSuchType.new` is a run-time "No such method", after the program has already produced output. | `Undeclared name` at compile time, before anything runs. |
| 4 | The PARSE is cached ([CACHING.md](../CACHING.md)); declarations and `BEGIN` still run every time. | `.precomp` caches the parse *and* the objects compile-time code produced, so `BEGIN` runs once per *compile*. |
| 5 | A module's operators reach the importer by **text scan**, so only declaration syntax the scanner recognises is honoured. | Real lexical export of the operator's `sub` into the importer's grammar. |
| 6 | One flat `classes_` map — no per-compunit type isolation. Two modules declaring the same unqualified class name collide. | Each compunit has its own stash. |
| 7 | `use Foo:ver<…>:auth<…>` adverbs are accepted and **discarded** — `UseStmt` has no version field, and the first match on the search path wins. (`:ver`/`:auth`/`:api` on a *declaration* are kept, in `pkgMeta_`, and answer `.^ver`/`.^auth`/`.^api`.) | Full version/auth/api resolution against the installed repos. |

Divergence #1 is the big one and it cuts both ways: it is why some modules work
under Raku++ that would need more machinery otherwise, and it is why a module's
internal helper can silently shadow something in the importing program.

Demonstration (`lib/Demo.rakumod` declaring `my sub private-sub`,
`our sub our-sub`, `sub exported-sub is export`, and `class Widget`):

| Called from the importer | Raku++ | Rakudo |
|---|---|---|
| `private-sub()` | `"private"` | not visible |
| `our-sub()` | `"our"` | not visible |
| `exported-sub()` | `"exported"` | `"exported"` |
| `Demo::our-sub()` | `"our"` | `"our"` |
| `Demo::Widget.greet` | `"widget"` | `"widget"` |
| bare `Widget.greet` | `"widget"` | not visible |

## Debugging a module problem

1. **`RAKUPP_TRACE=1`** — confirm *which* file was loaded. Half of all "module
   bug" reports are the wrong copy of the module being picked up, usually
   because `.` is on `libPaths_` or because an installed copy shadowed a
   checkout.
2. **`RAKULIB` takes `,` or `:`.** If the same value also has to drive
   `raku`, write `,`.
3. **Instrument the real module, not a reduction.** Copy its `lib/` to a
   scratch dir, add `note` calls, and run it under both engines. Reductions of
   grammar/module behaviour have repeatedly *passed* while the real module
   failed — the reduction drops the one piece of context that mattered.
4. **Load failures are loud now**, so a module that appears to do nothing is
   loading fine and behaving differently — not silently skipped.

## Where the code lives

| Concern | Location |
|---|---|
| Precompiled-AST cache | `AstSerial.{h,cpp}`; `loadModule` (modules) and `rakuppRun` (mainline) — see [CACHING.md](../CACHING.md) |
| `use`/`no`/`need` execution, pragmas, `use lib` | `Interpreter.cpp` `exec(NK::UseStmt)` ≈ line 3421 |
| Module resolution + load + publish | `Interpreter.cpp` `loadModule` ≈ line 2698 |
| Installed-repo (`~/.raku`) prefixes | `Interpreter.cpp` `rakuRepoPrefixes` ≈ line 2563 |
| `%?RESOURCES` / `$?DISTRIBUTION` | `Interpreter.cpp` `buildResourceMap`, `buildDistribution` |
| Parse-time operator import | `Parser.cpp` `scanModuleOps` ≈ line 234 |
| `unit module` / braced `module` / `our` publishing | `Interpreter.cpp` `exec(NK::ClassDecl)` ≈ line 3783 |
| `require ::($name)` | `Interpreter.cpp` ≈ line 13537 (unary op handler) |
| `--exe` path | `Codegen.cpp` ≈ line 1312 → `Interpreter::rtUse` ≈ line 1164 |
| `$*REPO`, `CompUnit::Repository::*` | `Builtins.cpp` ≈ line 2320 |

Related: [DISPATCH.md](DISPATCH.md) for what a call costs once resolved,
[MODULE-FINDINGS.md](MODULE-FINDINGS.md) for the ecosystem-compatibility log.
