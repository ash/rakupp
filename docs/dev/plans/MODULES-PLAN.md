# Plan: modules that travel

*Written 2026-08-08, before any code. A **v4 pillar** alongside
[EMBED-PLAN.md](EMBED-PLAN.md) — see the forming v4 section in
[VERSIONS.md](VERSIONS.md#v400--raku-that-travels-forming-2026-08-08). The rest
of v4's list is still open.*

Two ends of the same story, which is why they are one plan:

1. **Getting a module without installing Rakudo** — `rakupp install`.
2. **Shipping a program with its modules**, runnable on a machine that has
   neither Raku nor the module — the `--exe` / `--aot` / `--bundle` family.

Today the first is impossible (installing needs Rakudo + `zef`) and the second
mostly works but **promises nothing** — which turns out to be the more
interesting half.

Not to be confused with [ecosystem/V2-MODULES-PLAN.md](../ecosystem/V2-MODULES-PLAN.md),
the shipped v2.0.0 campaign. That one was *make other people's modules run*;
this one is *make them portable*.

---

## Where we are — verified, not assumed

Checked against this tree on 2026-08-08. Line numbers drift; the function names
are the durable reference.

**Reading the installed store already works.** `rakuRepoPrefixes()` yields
`~/.raku` plus the Homebrew Rakudo `site`/`vendor` repos, and
`findModuleSourceFor()` resolves through the CURI `short/<sha>` index
(`Interpreter.cpp`, ~3469). Fifty distributions exercise this.

**What the store actually is** — inspected on this machine, because the write
side and especially the *delete* side turn entirely on its shape:

```
~/.raku/version              store format version (here: 2)
        repo.lock            the lock every writer must take
        dist/<dist-id>       JSON: name, ver, auth, provides{module→path},
                             files{path→source-id}
        sources/<source-id>  one file's content, addressed by its own hash
        short/<sha1(name)>/<dist-id>
                             the index: one DIRECTORY per module name,
                             one FILE per distribution providing it
        resources/<id>       %?RESOURCES payloads
        bin/                 installed scripts
        precomp/             Rakudo's precompiled artifacts
```

Three consequences, and they are what makes update/uninstall a design problem
rather than a flag:

- **It is content-addressed and shared.** Two distributions — or two versions of
  one — that contain an identical file point at the *same* `sources/<id>` blob.
  Deleting a distribution therefore cannot unlink its blobs directly; it has to
  establish that nothing else references them.
- **`short/<name>/` is a directory of distributions, not a single entry.** Many
  versions coexist by design (`use Foo:ver<1.2>`), so *installing a newer
  version does not replace the old one* — it adds an entry beside it.
- **`repo.lock` exists**, which says concurrency was designed for. A writer that
  ignores it can corrupt the store while zef or Rakudo is using it.

**All three compile modes already embed the module graph** as serialized ASTs —
`collectModuleGraph()` + `emitModuleTable()`, called from `--bundle`
(`main.cpp:401`), `--exe` (`:477`) and `--aot` (`:536`). This is further along
than the item's framing suggests, and the plan is shaped by that.

**It genuinely produces standalone binaries.** Measured, not inferred:

```sh
echo 'use JSON::Fast; say to-json({a => 1});' > /tmp/modtest.raku
rakupp --exe /tmp/modtest.raku -o /tmp/modtest-exe
env HOME=/tmp/emptyhome RAKULIB= /tmp/modtest-exe   # -> {"a": 1}
```

With the module store hidden behind an empty `HOME`, the binary still runs.
Same for `Abbreviations`, `MIME::Base64` and `YAMLish` — every distribution
installed on this machine.

### What is missing

1. **No guarantee, and silent degradation.** A module whose AST will not
   serialize is skipped by `catch (AstSerialError&) { continue; }`, and one that
   is not found is skipped by the line above it. Both are silent. The build
   still succeeds, and the binary quietly needs the disk at run time. On the
   author's machine the store is present, so it works — and fails for whoever
   receives it. **This is the real gap, and it is a correctness gap, not a
   feature gap.**
2. **Resources are not embedded.** `%?RESOURCES` is built by reading the
   installed distribution from disk (`Interpreter.cpp` ~2819 / ~2853).
   *Unverified* — no distribution installed here uses resources, so the current
   behaviour of a resource-using module under `--exe` is unmeasured. Measure
   before designing.
3. **Native libraries can never be embedded.** A module with `is native` needs
   its shared library at run time. That is not fixable; it is reportable.
4. **Dynamic loads escape the static graph** — `require`, an `EVAL`'d `use`.
5. **No installer.** Getting a module needs Rakudo and zef.

---

## Part A — `rakupp install`

### The shape, and why it is not C++

`rakupp install Foo` is a thin front-end that runs a **Raku program shipped with
the release** — the `python -m pip` arrangement, with a nicer spelling. `pip`
ships with CPython and `CPAN.pm` is core perl; neither makes you install a
different interpreter. That is the property worth copying, and it does not
require the code to live in the binary.

Three reasons it must not:

- **The compile modes and the embedding story.** A binary produced by `--exe`,
  or a `rakupp` linked into someone's C++ application, must not carry an HTTP
  client, an ecosystem-index parser and a tar reader. That is size, attack
  surface, and it is exactly the objection that shaped this design.
- **Cadence.** An installer tracks a moving ecosystem API; the interpreter
  should not have to ship for it.
- **It is a Raku program**, which makes it another real dogfooding target
  alongside the Unicode generators and the Roast harness.

Both native dependencies are already `dlopen`ed on demand, so they cost an
unused binary nothing: TLS through the system libssl (the existing HTTPS
mechanism) and zlib the same way, via NativeCall.

### What it does

1. Fetch the ecosystem index (fez/360; REA as a second source).
2. Resolve name → version → distribution URL. Newest satisfying version, no
   SAT solver.
3. Fetch the tarball over HTTPS **with certificate verification** (see Risks).
4. Inflate and untar via `dlopen`ed zlib.
5. Read `META6.json`, walk `depends`, recurse.
6. Run the distribution's **own test suite** under rakupp before marking it
   installed — the standard set in v1.5.2.
7. Write the CURI store rakupp already reads: `sources/`, `dist/`,
   `short/<sha>/`.

Step 7 is the design point. **Install is the writer for a reader that already
exists.** No parallel module universe: what rakupp installs, Rakudo sees, and
vice versa.

### Phases

- **M1 — resolve, write nothing.** `rakupp install --dry-run Foo` prints the
  index entry, the resolved version, the `META6.json` and the full dependency
  plan. Everything hard about resolution, none of the risk.
- **M2 — certificate verification** in the HTTPS client, plus checksum
  verification against the index. A prerequisite, not a follow-on.
- **M3 — one dist, end to end.** A pure-Raku distribution with no dependencies:
  fetch, unpack, install, then `use` it **from rakupp and from Rakudo**. Both,
  or the shared-store claim is false.
- **M4 — the graph**, plus the test gate from step 6.
- **M5 — update.** See below; it is not what `pip install -U` means.
- **M6 — uninstall, and a store checker.** The destructive half, last, and
  gated hardest.

### M5 — update, which is not replacement

Raku's store holds many versions at once and resolution picks among them, so
**there is no in-place upgrade**. `rakupp install Foo` on an already-installed
`Foo` writes another distribution beside the existing one and leaves resolution
to pick the newest satisfying version. That is the whole of "update", and it is
*already* what M3/M4 do — no separate mechanism, only a decision to state:

- `rakupp install Foo` — install the newest satisfying version, additively.
- `rakupp install Foo:ver<1.2.3>` — a specific version, likewise additively.
- Nothing is removed. Reclaiming space is M6's job, deliberately separate, so
  that "get the new one" is never coupled to "destroy the old one".

The cost of additive updates is disk and a growing store, which is the right
trade for a package manager whose delete path is the dangerous one.

### M6 — uninstall as garbage collection

Removing a distribution means, from its `dist/<dist-id>` record:

1. delete `short/<sha1(name)>/<dist-id>` for **every** name it provides;
2. delete its `bin/` wrappers;
3. release its `sources/`, `resources/` and `precomp/` blobs **only if nothing
   else references them** — they are content-addressed and shared;
4. delete `dist/<dist-id>` last, so a crash mid-way leaves a record that still
   describes what to finish rather than orphans nobody can identify.

That is a mark-and-sweep over a content-addressed store, not an `rm -rf`, and
it runs on a store **shared with zef and Rakudo**. So:

- **Take `repo.lock`.** Non-negotiable; the store is not ours alone.
- **Order matters.** Unlink index entries *before* blobs: a missing blob behind
  a live index entry is a broken `use`; an orphaned blob behind no index entry
  is only wasted disk.
- **Check reverse dependencies** and refuse by default when something installed
  still `depends` on the target, with an explicit override.
- **Do not delete what we did not install, by default.** A distribution zef put
  there is zef's; removing it silently is a surprise in someone else's tool.
  `--force` exists for people who mean it.
- **`rakupp install --check`** — a store checker that reports dangling index
  entries, unreferenced blobs and unreadable records, and fixes nothing unless
  asked. Written *before* the delete path, because it is how the delete path is
  tested: run it before and after every uninstall in the suite.
- **Stale `precomp/`.** Rakudo's precompiled artifacts are keyed to
  distributions; whether removing a distribution requires invalidating them,
  and how Rakudo behaves if it does not, is **unverified** — measure before
  designing, and if zef's own uninstall has semantics here, match them rather
  than invent.

`rakupp install --list` belongs here too: what is installed, and where a name
resolves when several distributions provide it — the question the shape of
`short/` makes worth asking.

### Non-goals for Part A

Replacing zef. A plugin architecture. Build backends: a distribution needing a
real build step prints *this one needs zef* and stops — the target is the
pure-Raku majority. Publishing (`fez upload`) stays out entirely, consistent
with the standing rule on the module repo. No lockfile or project manifest:
`rakupp install` manages a store, not a project — if per-project dependency
sets are ever wanted, that is a different design and a different plan.

---

## Part B — binaries that are actually standalone

The mechanism exists and works. What is missing is the **guarantee**, so this
half is strictness and proof rather than plumbing.

- **B1 — say what happened.** Every compile mode reports the modules it
  embedded and, individually, every module it could **not**, with the reason:
  AST would not serialize / not found on the search path / native / reached
  only through `require`.
- **B2 — `--standalone`.** The same, except a module that cannot be embedded is
  an **error**. This is the flag someone shipping a binary actually wants, and
  it is the smallest change that converts "worked on my machine" into a
  build-time failure.
- **B3 — resources.** Embed `%?RESOURCES` payloads and serve them from memory.
  Measure the current behaviour first (gap 2 above is unverified).
- **B4 — the gate that proves it.** Build a set of module-using programs and run
  each with `HOME` pointed at an empty directory, `RAKULIB` cleared and the CURI
  store renamed. Cheap, and it is a test rather than a claim. Four distributions
  pass it manually today; this makes that a standing check.
- **B5 — name the native dependencies.** `--standalone` lists the shared
  libraries the binary will still `dlopen`. It cannot embed them. It must not
  hide them.

---

## How the halves meet

One `META6.json` reader and one CURI layout, shared: the installer **writes**
the store, the bundler **walks** it. If those two drift apart, `--standalone`
starts lying — a module installed one way and embedded another. Sharing that
code is the reason to do both together rather than as two unrelated features,
and it is why this is one plan.

---

## Gates

Every batch, the standing ones (definitions in [COUNTING.md](../../status/COUNTING.md)):

1. **Roast** — zero regressions.
2. **Module battery** — 50/59 unchanged ([MODULE-FINDINGS.md](../ecosystem/MODULE-FINDINGS.md)).
3. **`perf-guard --check`** — a release gate, not an eyeball.

Plus, for this campaign specifically:

4. **The cross-engine store test** (Part A): a distribution installed by
   `rakupp install` is loadable by **Rakudo**, and one installed by `zef` is
   loadable by rakupp. Both directions, every batch.
4b. **The store survives destruction** (M6): `rakupp install --check` is clean
   before and after every uninstall in the suite; everything *not* uninstalled
   still loads under **both** engines; and a distribution installed by zef is
   still intact after rakupp uninstalls a different one. Run against a scratch
   store, never the developer's own — this is the one operation in the campaign
   that can destroy someone's working environment.
5. **The empty-`HOME` standalone gate** (Part B), over a set of module-using
   programs.
6. **No new dependency in the binary** — `--exe` output size and the linked
   library list must not move because an installer exists. Checked, not assumed.

---

## Risks, named

- **Certificate verification is open.** Fetching executable code over
  unverified TLS is a supply-chain hazard, and no amount of convenience trades
  against it. M2 exists because of this, and A does not ship without it.
- **The index is someone else's API.** Pin the expected shape, fail legibly on
  a change, keep REA as a second source.
- **Silent degradation is the *current* behaviour of Part B**, which makes B1/B2
  the first work in this plan regardless of what else lands. A binary that works
  only where it was built is worse than one that refuses to build.
- **Serialization coverage** — `serializeAst()` decides what can be embedded,
  and its gaps are Part B's real backlog. The `--aot` lossiness bug that the
  precomp cache exposed is the precedent for how these surface.
- **Scope creep into a package manager.** Naive version resolution is a
  decision, not an omission.
- **The delete path is the only genuinely dangerous code in this campaign.** It
  mutates a store that zef and Rakudo also own, over blobs that are shared by
  content hash, and its failure mode is a `use` that stops working in a *third*
  tool. Everything about M6 — the checker first, the lock, the ordering, the
  refusal to touch what we did not install — exists because of that, and it is
  why it is last rather than first.
- **Additive updates grow the store.** Chosen deliberately (M5): the
  alternative couples "get the new version" to "run the dangerous path".
- **Bundle size.** Embedding modules (and later resources) grows the binary;
  the runtime already dominates it. Measure per phase and report it.

---

## Non-goals

- Replacing zef, or publishing to the ecosystem.
- Embedding native shared libraries.
- A new module store format — the whole value is using the one that exists.
- Making `--exe` natively compile *module* code. Modules ride as ASTs; most
  declare a package, which codegen does not take. That is a different project.
