# Caching — the precompiled parse

Raku++ can cache the **parsed form** of the files it runs, so a later run does
not re-read and re-parse text that has not changed. It is **off by default** —
nothing is written to your disk until you ask — with two independent switches,
because the two halves are worth very different amounts:

- **modules.** Caching the parse of everything a program `use`s.
- **files.** Caching the main program's own parse.

```bash
rakupp --precomp-modules=on    # cache the parse of `use`d modules
rakupp --precomp-files=on      # cache the main program's own parse
rakupp --precomp-info          # what is on, where it lives, what it holds
rakupp --precomp-clean         # empty it (always safe)
```

Either setting persists in `~/.config/rakupp/rakupp.config`. If you turn on only
one, make it `modules` — the numbers are below.

---

## Which switch is worth turning on

Measured with `min` of 15 runs on an otherwise idle machine, comparing a run with
nothing cached against a run with a warm cache.

**`--precomp-modules`** — worth it as soon as a program `use`s anything. A
dependency tree is a lot of source, and none of it changes between runs:

| | no cache | cached |
|---|---:|---:|
| `use XML` (10 files, 1110 lines) | 16.0 ms | **5.7 ms** |

**`--precomp-files`** — worth it only for *large* single files. A script's own
parse is already sub-millisecond, and the ~4 ms floor of a small program is
process startup, not parsing:

| bare file, no modules | no cache | cached | saved |
|---|---:|---:|---:|
| 50 lines | 2.8 ms | 2.4 ms | 0.4 ms |
| 200 lines | 3.8 ms | 2.8 ms | 1.0 ms |
| 1 000 lines | 10.0 ms | 5.1 ms | 4.9 ms |
| 5 000 lines | 40.3 ms | 17.8 ms | 22.5 ms |
| 20 000 lines | 158.8 ms | 66.5 ms | **92.3 ms** |

Across the 22 fastest programs in [`examples/`](../examples) — 12 to 106 lines
each — turning `files` on made **no measurable difference at all** (−1%, inside
the noise). Those programs are dominated by process startup.

There is also a cost on the run that *writes* an entry: +0.6 ms at 50 lines,
+1.5 ms at 1 000, +22 ms at 20 000. So for a script you run once, `files`
caching is a small net loss; for one you run repeatedly, it pays from about a
thousand lines up.

So if you enable one, enable **`modules`**. That difference is why these are two
switches rather than one, and `modules` is the one likely to become a default in
a later release.

---

## What is actually stored

The **AST** — the tree the parser produces — not bytecode. Raku++ executes by
walking that tree, so the tree already *is* its compiled form; there is nothing
further to compile it into. A loaded tree is indistinguishable from a freshly
parsed one, and nothing about how your program runs changes.

Either half can be cached: the **modules** a program `use`s (`--precomp-modules`)
and the **main program's own** parse (`--precomp-files`), independently. `-e`
code has no file behind it and is never cached.

The format is versioned and self-describing. A rakupp that does not understand
an entry ignores it rather than misreading it.

---

## Where it lives

The **settings** go in `$XDG_CONFIG_HOME/rakupp/rakupp.config`, else
`~/.config/rakupp/rakupp.config` (override with `RAKUPP_CONFIG`). It is a plain
`key = value` file you can edit or check into a dotfiles repo. It does not exist
until you set something; this is what it looks like after
`rakupp --precomp-modules=on`:

```
# rakupp settings. See `rakupp --precomp-info` and docs/CACHING.md.
precomp-modules = on
```

A key that is absent takes its default, which for both of these is off.

The **entries** go in `$XDG_CACHE_HOME/rakupp/precomp`, else
`~/.cache/rakupp/precomp` (override with `RAKUPP_PRECOMP_DIR`).

Nothing is ever written next to your sources. Read-only module trees, a
zef-populated `~/.raku`, and a checkout you do not own all work, and your project
directory stays clean. `rm -rf ~/.cache/rakupp` is safe at any moment —
everything in it is derived data.

For one invocation, without touching the saved settings:

```bash
RAKUPP_PRECOMP_MODULES=1 rakupp app.raku     # this run only
RAKUPP_NO_PRECOMP=1      rakupp app.raku     # force both off
```

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
rakupp --precomp-modules=off
rakupp --precomp-files=off
RAKUPP_NO_PRECOMP=1 rakupp app.raku    # one run, both halves
```

The cache must never change behaviour. If a program behaves differently with and
without it, that is a bug in Raku++ — please report it with both outputs.

---

## Compared with Rakudo and with Python

| | Rakudo | CPython | Raku++ |
|---|---|---|---|
| unit cached | serialised bytecode + the objects `BEGIN` produced | bytecode | the parsed AST |
| where | `.precomp/` in the repository | `__pycache__/` beside the source | one central cache directory |
| invalidated by | dependency graph + versions | mtime + size (content hash opt-in) | content hash, always |
| main program | not precompiled | not cached (`__main__` is recompiled every run) | cached, if `files` is on |
| on by default | yes | yes | **no** |

Two rows are worth a note. Raku++ *can* cache the main program, which Python does
not — CPython's reason for skipping `__main__` is largely that writing a `.pyc`
next to a one-off script is unwelcome, and a central cache does not have that
problem; the measurements above are why it is nonetheless the half not worth
enabling for script-sized files. And nothing here is enabled without being asked
for, where both Rakudo and Python cache as a matter of course.

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
