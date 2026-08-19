# AST Serialization and the Precompiled Parse

Parsing is not free, and a program that pulls in a dozen modules pays for all
twelve on every run before it does any work of its own. Every implementation of
every language eventually answers that with a precompilation format. This one
has both an easier version of the problem and a harder one.

Raku++ executes by walking a tree, so **the tree already is the compiled form**;
there is nothing further to compile it into. That makes caching straightforward
in one sense — store the tree — and delicate in another, because a stored tree
must be indistinguishable from a freshly parsed one.

## The format

```cpp
// src/AstSerial.h
inline constexpr uint32_t kAstSerialVersion = 5;
struct AstSerialError { std::string msg; };
std::string serializeAst(const Program& prog);
void deserializeAst(const std::string& blob, Program& out);
```

A self-describing byte string with a header. The version constant is bumped
whenever the encoding or the AST changes shape, and an entry carrying a
different version is **ignored, never reinterpreted**.

Two properties of the design are load-bearing.

**Both directions are driven by one visitor per node type.** A field cannot be
written but not read, which is the failure mode that makes a format like this
quietly wrong rather than loudly broken. Adding a field to `SubDecl` and
forgetting the reader is not possible; the single visitor handles both.

**The mutable per-node caches are deliberately not stored.**

```
Binary::simpleOp, fastShape, litVal
Index::fastShape, litIdx
NumLit::cacheN, cacheD
Block::hoistNeed
StrLit::nfcDone
```

Each has an "undecided" sentinel and is rebuilt lazily on first evaluation
(Chapter 19), so a loaded tree behaves identically to a freshly parsed one — it
just has not warmed up yet. Storing them would be both larger and more fragile,
since `litVal` is a pointer.

Everything else round-trips, **including `line` and `label`**, because
diagnostics depend on them.

## Two independent switches

```sh
rakupp --precomp-modules=on    # cache the parse of `use`d modules
rakupp --precomp-files=on      # cache the main program's own parse
rakupp --precomp-info          # what is on, where it lives, what it holds
rakupp --precomp-clean         # empty it (always safe)
```

Both are **off by default** — nothing is written to disk until asked — and they
are separate because they are worth very different amounts.

**Modules.** A dependency tree is a lot of source and none of it changes between
runs:

| | no cache | cached |
|---|---:|---:|
| `use XML` (10 files, 1,110 lines) | 16.0 ms | **5.7 ms** |

**Files.** A script's own parse is already sub-millisecond, and the few-
millisecond floor of a small program is process startup, not parsing:

| bare file | no cache | cached | saved |
|---|---:|---:|---:|
| 50 lines | 2.8 ms | 2.4 ms | 0.4 ms |
| 1,000 lines | 10.0 ms | 5.1 ms | 4.9 ms |
| 20,000 lines | 158.8 ms | 66.5 ms | **92.3 ms** |

Across the twenty-two fastest example programs, turning `files` on made **no
measurable difference at all**. There is also a cost on the run that *writes* an
entry — about 22 milliseconds at 20,000 lines — so for a script run once,
`files` caching is a small net loss.

If you enable one, enable **modules**. That asymmetry is precisely why these are
two switches rather than one.

## What invalidates an entry

This is the interesting part, and it is where the naive design would be wrong.

```cpp
// src/Interpreter.h
bool precompLoadProgram(const std::string& srcPath, const std::string& src,
                        const std::vector<std::string>& searchPath,
                        Program& out, std::string& finishOut);
void precompStoreProgram(const std::string& srcPath, const std::string& src,
                         const std::vector<std::string>& searchPath,
                         const Program& prog, const std::string& finish,
                         const std::vector<std::pair<std::string,
                                                     std::string>>& deps);
```

An entry is valid only while **four** things still hold.

**The source has not changed.** Content-validated, not timestamp-validated.

**The rakupp binary is the same one.** A tree from another build may not match
this build's node shapes.

**The search path is the same.** This is the one people do not expect, and it is
in the signature for a reason: the search path decides which *file* a `use`
resolves to for operator scanning, so the same source under a different `-I` can
legitimately parse differently.

**The operators of everything it `use`s are unchanged.** This is the deepest
one. A module that gains or loses an operator declaration changes how *this*
file parses, without this file changing at all (Chapter 6). So the parser records
every source it scanned:

```cpp
// src/Parser.h
std::vector<std::pair<std::string, std::string>> opScanned_;
```

and those pairs go into the entry as dependencies. It is the same list the
parser needed anyway; making it a cache key was nearly free.

## Where it lives, and what it reports

Settings persist in a small config file under the user's config directory;
`RAKUPP_PRECOMP_MODULES` and `RAKUPP_PRECOMP_FILES` override for one
invocation, and `RAKUPP_NO_PRECOMP=1` forces both off.

```cpp
// src/Interpreter.h
struct PrecompEntry {
    std::string source; unsigned long long bytes; bool usable; bool orphan;
};
std::vector<PrecompEntry> precompCacheList();
std::pair<size_t, unsigned long long> precompCacheClear();
```

The `orphan` flag distinguishes the one cache state that is *pure garbage* from
the ones that are merely misses waiting to happen: the source file is gone, so
the entry can never be reused **or** rewritten. Everything else is derived data,
which is why clearing the cache is always safe.

## What is *not* cached

Only the parse. A module's top-level declarations and its `BEGIN` blocks still
execute on every run, which is roughly the 30% of module load time that is not
parsing.

Rakudo goes further: its `.precomp` serialises the *objects* that compile-time
code produced, so `BEGIN` runs once per compile rather than once per run.
Matching that would mean serialising live runtime state — class registries,
closures, whatever a `BEGIN` block built — which is a different and much larger
project.

Compared with the neighbours:

| | what it stores | where |
|---|---|---|
| Raku++ | the AST | a central content-validated directory |
| Rakudo `.precomp` | bytecode plus compile-time objects | beside the source |
| Python `__pycache__` | bytecode | beside the source |

Storing entries centrally rather than beside the source is what makes caching
the **main program** defensible at all. Python never caches `__main__`, and the
usual objection is that it would litter the source tree; that objection does not
apply here.

## The other consumer: embedded modules

The same serialisation carries modules inside a compiled binary (Chapter 25):

```cpp
// src/Interpreter.h
struct BundledModule { std::string name, blob, finish, src; };
void rakuppRegisterModule(const std::string& name, const char* blob,
                          size_t blobLen, const std::string& finish);
```

`collectModuleGraph` resolves the transitive `use` graph at build time and
serialises each module, dependencies first. `loadModule` consults the embedded
table before the disk.

That the cache format and the embedding format are the *same* format is not a
coincidence — it is why a bug in either is found twice as fast.

## The AOT emitter, and where it differs

`--aot` solves a similar problem in a completely different way: instead of a
byte encoding, it emits **C++ source that rebuilds the tree**.

```cpp
// src/Ast.h
struct AstEmitError { std::string msg; };
void emitAstProgram(const Program& prog, std::ostream& out,
                    const std::string& fileName, const std::string& finish,
                    const std::vector<BundledModule>& mods);
```

The trade-offs are opposite. The serialiser is compact and fast to read but
needs its own reader; the emitter needs no reader at all — the C++ compiler is
the reader — but produces one function per node, so a few hundred lines of Raku
become tens of thousands of lines of C++.

Both fail *soft*. An unserialisable module is left out of the binary and loaded
from disk; an unemittable node makes `--aot` fall back to bundling. Neither
produces a wrong tree.

That shared property is the design rule worth taking away: **a derived-data
mechanism should degrade to recomputation, never to a guess.** Every failure
mode in this chapter — a version mismatch, a changed dependency, a missing
source, an unhandled node — ends in "parse it again", which is slower and
correct.
