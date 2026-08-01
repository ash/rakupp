# Caching — the precompiled parse

Raku++ caches the **parsed form** of every file it runs, so the second run of a
program does not re-read and re-parse text that has not changed. It is on by
default and needs no configuration; this page is for when you want to look
inside it, turn it off, or understand what it can and cannot get wrong.

```bash
rakupp --precomp-info      # where the cache is, and what is in it
rakupp --precomp-clean     # empty it (always safe)
```

---

## What it buys

Parsing is roughly 70% of what loading a module costs; running its declarations
is the rest. The cache removes the parsing.

| | without | with |
|---|---:|---:|
| `use XML` (10 files, 1110 lines), interpreted | 16.0 ms | **5.7 ms** |
| the same from an `--exe` binary | 19.0 ms | **6.6 ms** |
| a 120-line main program | 24.6 ms | **18.1 ms** |
| `use XML` + that mainline, end to end | 34.6 ms | **21.3 ms** |

A single module makes the point: `XML::Element` takes 6.2 ms to parse and
0.7 ms to load from cache.

Compiled binaries benefit too. `--exe` compiles your *program*, but a `use` in
it still goes through the normal module loader at run time, so it pays the same
parse — and now the same saving. (Embedding modules **into** the binary is a
separate, unbuilt idea; see [dev/MODULES.md](dev/MODULES.md).)

---

## What is actually stored

The **AST** — the tree the parser produces — not bytecode. Raku++ executes by
walking that tree, so the tree already *is* its compiled form; there is nothing
further to compile it into. A loaded tree is indistinguishable from a freshly
parsed one, and nothing about how your program runs changes.

Both the **modules** you `use` and the **main program** are cached. `-e` code
has no file behind it and is never cached.

The format is versioned and self-describing. A rakupp that does not understand
an entry ignores it rather than misreading it.

---

## Where it lives

| | |
|---|---|
| `RAKUPP_PRECOMP_DIR=…` | if set, wins outright |
| `$XDG_CACHE_HOME/rakupp/precomp` | if that is set |
| `~/.cache/rakupp/precomp` | otherwise — the usual case |
| no `HOME` at all | caching silently off |

Nothing is ever written next to your sources. Read-only module trees, a
zef-populated `~/.raku`, and a checkout you do not own all work, and your
project directory stays clean.

`rm -rf ~/.cache/rakupp` is safe at any moment. Everything in it is derived data.

---

## When an entry is discarded

**One entry per source file.** Editing a file replaces its entry — it does not
add another. Twenty edits of one module leave one entry.

An entry is used only when *all* of these still hold:

- **The source is byte-for-byte what it was.** Validation is by content hash.
  Timestamps are never consulted: `touch`ing a file changes nothing, and an edit
  whose mtime you backdate to 1970 is still picked up. (This is the failure mode
  of Python's default `.pyc` scheme, which compares mtime and size; CPython
  added an opt-in content-hash mode in PEP 552 for exactly this reason.)
- **The same rakupp wrote it.** Entries record the binary that produced them, so
  rebuilding or upgrading rakupp invalidates them. A stale entry is rewritten in
  place on next use, so a rebuild does not leave the old ones lying around.
- **Every module it read for operators is unchanged.** Parsing a file means
  finding the operators its `use`d modules declare, so a module that gains or
  loses a `sub infix:<…>` reparses its importers even though they did not change.
- **The search path is the same.** `-I`, `RAKULIB`, and the defaults
  `lib` / `.` / `rakulib` all decide *which file* a `use` resolves to, so they
  are part of the identity of a parse. Two directories can give one script two
  different meanings, and they get two entries.

Anything else unexpected — a truncated file, a dependency that moved, an entry
from a rakupp that no longer exists — is treated as a miss. The source is right
there; a miss costs a parse, never a wrong answer.

---

## Looking inside

```bash
rakupp --precomp-info
```

```
/Users/you/.cache/rakupp/precomp
    /Users/you/proj/lib/My/Shapes.rakumod  (7 KB)
    /Users/you/proj/app.raku  (2 KB)
  ! /Users/you/proj/lib/My/Util.rakumod  (3 KB)

3 entries, 12 KB
1 marked ! is stale (3 KB): built by another rakupp, or the source has changed
since. Each is rewritten in place on next use.
```

Entries are listed by the file they were built from. `!` marks one that will not
be used as-is. An entry this rakupp cannot parse at all shows as `(unreadable)`.

```bash
rakupp --precomp-clean
```

Removes every entry and the directories they lived in, leaving the cache root.

---

## Turning it off

```bash
RAKUPP_NO_PRECOMP=1 rakupp app.raku
```

Everything is parsed from source. Worth reaching for if you suspect the cache is
involved in a problem: if behaviour differs with and without this set, that is a
bug in Raku++ — please report it with both outputs.

---

## Compared with Rakudo and with Python

| | Rakudo | CPython | Raku++ |
|---|---|---|---|
| unit cached | serialised bytecode + the objects `BEGIN` produced | bytecode | the parsed AST |
| where | `.precomp/` in the repository | `__pycache__/` beside the source | one central cache directory |
| invalidated by | dependency graph + versions | mtime + size (content hash opt-in) | content hash, always |
| main program | not precompiled | not cached (`__main__` is recompiled every run) | **cached** |

The main-program difference is worth a note: Python's reason for skipping
`__main__` is largely that writing a `.pyc` next to a one-off script is
unwelcome. A central, content-validated cache does not have that problem, so a
large script gets the same saving its modules do.

The `BEGIN` difference cuts the other way, and is the honest limit of this
cache. Rakudo precompiles a module by *running* its compile-time code and
serialising the result. Raku++ caches only the parse; a module's top-level
declarations and `BEGIN` blocks still execute on every run. That is the ~30% the
table above does not remove, and closing it would mean serialising live runtime
state — a much larger and riskier undertaking.

---

## For maintainers

The format lives in `src/AstSerial.{h,cpp}`; the cache itself is in
`Interpreter::loadModule` (modules) and `rakuppRun` (the main program).

Every node type has exactly **one** `visit(io, node)` listing its fields, and it
is instantiated twice — once with a writer, once with a reader. A field cannot be
saved but not restored, because both directions are the same source line.

After touching `src/Ast.h`, run the round-trip check over a corpus:

```bash
rakupp --ast-roundtrip FILE
```

It asserts two different things: re-serializing the rebuilt tree is
byte-identical (catching a reader that skips a field) *and* `dumpAst` of both
trees matches (catching a writer that never writes one).

One lesson from building it, recorded because it was not obvious: every bug
found in this cache so far was in **invalidation**, not in the serializer —
which is where the testing had been concentrated. Round-tripping 800+ files
cleanly says nothing about whether the right entry was chosen. Test what
invalidates.
