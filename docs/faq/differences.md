# FAQ — where Raku++ and Rakudo differ

Raku++ targets the same language as Rakudo and is measured against it: every
runnable example in the official documentation is executed on both, and every
Roast assertion is scored. **943 of 1,451 documentation examples produce
byte-identical output on both engines**, and ~90% of Roast's declared tests pass.

The two are not the same program, though, and this page is the honest list of
where you will notice — in both directions.

## Where Raku++ does something Rakudo does not

**It compiles to a standalone binary.** `--exe` generates C++ and links it; the
result needs no Raku installed, and with `-O` runs 4×–500× faster than the
interpreter depending on the kernel — string building at the low end, tight
integer loops at the high. See [compiling.md](compiling.md).

**It runs in a browser.** The same interpreter compiled to WebAssembly — no
server, no install: <https://raku.online>.

**It starts in ~2ms.** There is no runtime to boot. That is the difference
between a Raku script being usable in a shell pipeline and not.

**It ships a static analyser.** `rakupp --lint` reports unused variables,
unreachable code and redeclarations without running the program, and exits
non-zero — so it drops into CI. See [LINT.md](../LINT.md).

**It is one binary with no third-party dependencies.**

**It terminates on some inputs Rakudo does not.** A string sequence whose
endpoint `.succ` can never reach loops forever in Rakudo; Raku++ stops:

```raku
say ('a!' ... 'zz!').elems;    # Raku++: 702.  Rakudo: does not terminate.
```

That one is a deliberate divergence, not an accident — a compiler that hangs is
worse than one that answers.

**And a handful of documented behaviours it gets right where Rakudo has drifted.**
The conformance sweep classifies 19 examples as *doc and Raku++ agree, Rakudo does
not*. Most are small — `1.asinh` gives the documented `0.881373587019543` here and
`0.8813735870195429` in Rakudo, for instance. A few are randomness or stale docs
rather than real wins; the classified list lives in
[the spec site's source](https://github.com/ash/raku.online/tree/main/sites/spec).

## Where Rakudo is ahead

**Maturity and coverage.** Rakudo is a complete, production implementation with
two decades behind it. Raku++ passes ~90% of Roast; the remaining 10% is real
language, and you will find it if you go looking.

**`die` prints a backtrace.** Raku++ prints the message only — see
[debugging.md](debugging.md).

**Stricter parsing.** Rakudo rejects some programs Raku++ accepts, a missing
semicolon between statements among them. If you want your syntax checked
strictly, Rakudo is the better checker.

**The full ecosystem.** Many zef modules work under Raku++, but not all; see
[ECOSYSTEM.md](../ECOSYSTEM.md) and [MODULES.md](../MODULES.md).

## Differences you will actually run into

**Output from a child process is buffered, not live.** Without `:out`, Rakudo
gives the child your terminal so output appears as produced; Raku++ captures and
echoes when it exits. With `:out` both behave identically. See
[shell.md](shell.md).

**`Str.succ` on a trailing non-alphanumeric.** `'a!'.succ` is `'b!'` here and
`'a!'` in Rakudo. Deliberate: Rakudo's own source comments describe a rule its
code does not implement, it ships a leftward-scanning helper matching ours that
`Str.succ` does not call, and the spec tests pass under either reading. Ours also
terminates where Rakudo's loops.

**Multi-character string ranges.** Rakudo iterates `('ab'..'ba')` as a
per-position cross product — `ab aa bb ba`; Raku++ climbs by `.succ`. Known, and
on the list.

**Hash iteration order.** Raku++ iterates sorted; Rakudo's order is its own and
varies. Neither is guaranteed by the language — sort if you depend on it.

**`$*RAKU.compiler.version` reports a Rakudo era, not the Raku++ release.**
It answers `v2026.07` — the Rakudo release Raku++ is verified byte-identical
against — while the rest of the object says who is actually running:

```raku
say $*RAKU.compiler.name;       # → Raku++      (Rakudo says: rakudo)
say $*RAKU.compiler.version;    # → v2026.07    (the era tracked, not our release)
say $*RAKU.compiler.release;    # → 1.7.0       (Rakudo leaves this empty)
say $*RAKU.compiler.id;         # → 1.7.0       (Rakudo: a commit SHA)
say $*RAKU.compiler.backend;    # → cpp         (Rakudo: moar)
```

This is deliberate, and it reverses an earlier decision to report our own
version there. Modules gate features on `$*RAKU.compiler.version < v2023.12`,
using the compiler version as a proxy for *do I have modern semantics?* —
answering `v1.7.0` compares as a pre-2000 Rakudo, and every such module refuses
to load. JSON::Class was the witness.

So: **detect the engine with `.name`, not with `.version`.** The number answers
what the language does; the name answers who implements it. The era constant is
`kOracleEra` in `src/Builtins.cpp`, and it moves when the conformance oracle
moves — not when Raku++ is released.

## Keeping yourself portable

If a program must run on both, the reliable habits are:

- `sort` before comparing anything that came out of a hash
- pass `:out` when you capture, rather than relying on pass-through timing
- decontainerise explicitly — `@(…)` — rather than relying on a coercion
- diff the two engines on your own test data; it takes one line and finds these
  before your users do

---

The measured, per-example classification is in
[the spec site's source](https://github.com/ash/raku.online/tree/main/sites/spec);
the Roast standing and how it is counted are in [ROAST.md](../ROAST.md) and
[COUNTING.md](../COUNTING.md).

Back to the [FAQ index](README.md).
