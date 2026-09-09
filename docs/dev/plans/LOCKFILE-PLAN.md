# Plan: reproducible module installs — `rakupp.lock`

*Written 2026-09-09, before any code. A follow-on to the shipped installer in
[MODULES-PLAN.md](MODULES-PLAN.md): that plan made modules installable without
Rakudo; this one makes the result repeatable on another machine and at another
time.*

Goal: a project can commit one resolution of its module graph and ask rakupp to
reproduce it exactly. A new ecosystem release, a changed index, or a fuller
global module store must not silently change a frozen install.

**The claim a stranger can re-measure:** given the same `META6.json` and
`rakupp.lock`, two empty stores on supported machines of the same platform,
running `rakupp install --frozen`, contain the same distribution identities and
byte-identical source/resource artifacts; neither run consults dependency
resolution or changes the lockfile.

This is deliberately two claims, not one vague promise of “reproducible
builds”:

1. **Resolution reproducibility:** the same exact distributions are selected.
2. **Artifact reproducibility:** the downloaded bytes are the bytes the lock
   names.

It does **not** initially promise byte-identical native build products. A
`Build.rakumod` may invoke the host compiler, inspect system libraries, embed a
path or timestamp, or download something of its own. Those inputs must be made
visible, but hermetic native builds are a later campaign.

---

## Where we are

The installer is a Raku program in [`tools/install.raku`](../../../tools/install.raku),
compiled into the CLI at build time and dispatched by `rakupp install`. The useful pieces already exist:

- zef-index-first resolution with REA fallback;
- dependency-first plans over `depends`, `build-depends` and `test-depends`;
- `name`, `version`, `auth`, `api`, `provides` and dependency metadata;
- exact-version requests and additive installation of multiple versions;
- `--dry-run`, `--to`, `--list`, `--check`, `--gc`, uninstall and reinstall;
- fetched fez archives checked against the SHA-1 in their content-addressed
  path;
- a content-addressed CURI store shared correctly with Rakudo and zef;
- store mutation under `repo.lock`, with an append-only diagnostic trace;
- local fixture indexes in `t/install`, so resolver tests need no network.

What is absent is a durable record of the plan. Each ordinary install resolves
against the indexes and the distributions already present in the target store.
That is right for an interactive installer and wrong for CI: the same command
tomorrow may select a newer graph.

There are four boundaries this plan must respect.

1. **Module name is not distribution identity.** More than one distribution
   can provide a module. The lock records the selected distribution and its
   archive, not merely `JSON::Fast`.
2. **The shared store is not an environment.** It intentionally contains many
   versions and foreign distributions. A frozen project must not start using a
   global candidate merely because it is newer or already installed.
3. **REA has weaker source identity today.** Fez archive paths carry a content
   hash; the current REA path is protected by TLS but has no recorded archive
   digest. Creating a lock is the opportunity to hash those bytes locally.
4. **The current resolver is not a SAT solver.** Locking must first record and
   replay the resolution the installer already makes. Constraint merging and
   better conflict explanations are valuable, but cannot be smuggled into the
   first lockfile release as an unmeasured semantic change.

---

## User model

### Two files, two jobs

`META6.json` states the acceptable graph: names and version/auth/api
constraints. `rakupp.lock` states one exact graph that satisfied it.

- Applications commit both files.
- Libraries normally commit `META6.json`; they may commit a lockfile for their
  own development and CI, but consumers resolve from the library's declared
  constraints, never from its development lock.
- The lockfile is generated data. Users do not need to edit it by hand, but it
  is text, deterministic, and reviewable in a diff.

### Commands

The first surface is intentionally small:

```sh
rakupp install --lock                 # resolve/install, write rakupp.lock
rakupp install --frozen               # execute exactly rakupp.lock
rakupp install --frozen --to=PREFIX   # exact graph into an explicit empty store
rakupp lock verify                    # verify lock syntax, graph and available artifacts
rakupp lock why URI                   # roots -> dependency edges -> selected dist
```

The existing positional form remains useful:

```sh
rakupp install --lock Cro::HTTP
```

It records those explicit roots in the lock. With no positional roots,
`--lock` reads the current directory's `META6.json` and locks that
distribution's runtime/build/test dependency graph. `--frozen` must not accept
new roots on the command line: a root absent from the lock is a request to
resolve, and frozen mode never resolves.

Update commands come only after replay is proven:

```sh
rakupp update                         # re-resolve every root and rewrite the lock
rakupp update JSON::Fast              # unlock the named selection and necessary closure
rakupp update --dry-run JSON::Fast    # show the lock diff, write nothing
```

`install --lock` is create-or-refresh-all. `update NAME` is the surgical
operation. Keeping those meanings separate prevents a routine install from
moving unrelated dependencies.

### Default paths

Lock discovery walks from the current directory toward the filesystem root,
using the first `rakupp.lock` beside a `META6.json`, like other project tools.
An explicit `--lockfile=PATH` overrides discovery.

The first release does **not** silently change module lookup or create a hidden
environment. Frozen installation writes to the existing default CURI store, or
to `--to`. Project-local environments are P3, after exact replay itself is
correct. This ordering keeps lock semantics independent from repository-search
semantics.

---

## Lock format

### Format choice

Version 1 is canonical JSON. The installer already has an in-engine JSON codec,
JSON represents META6 identities without a second parser, and every supported
host can inspect it. The writer controls key order and indentation so identical
graphs produce byte-identical files.

The root shape:

```json
{
  "lock-version": 1,
  "generated-by": {
    "engine": "rakupp",
    "version": "3.26.0"
  },
  "language": "6.d",
  "roots": [
    { "name": "My-App", "constraint": "*", "groups": ["runtime", "build", "test"] }
  ],
  "platform": {
    "os": "macos",
    "arch": "arm64"
  },
  "distributions": [
    {
      "id": "JSON-Fast:ver<0.19.2>:auth<zef:lizmat>:api<1>",
      "name": "JSON-Fast",
      "version": "0.19.2",
      "auth": "zef:lizmat",
      "api": "1",
      "source": {
        "kind": "archive",
        "url": "https://example.invalid/JSON-Fast.tar.gz",
        "sha256": "0123456789abcdef..."
      },
      "provides": {
        "JSON::Fast": "lib/JSON/Fast.rakumod"
      },
      "dependencies": [
        { "name": "JSON::Marshal", "group": "runtime", "selected": "JSON-Marshal:ver<0.0.25>" }
      ]
    }
  ]
}
```

The example is illustrative, not a frozen schema. The schema below is the
contract.

### Required top-level fields

| field | meaning |
|---|---|
| `lock-version` | integer schema version; unknown versions are refused |
| `generated-by` | diagnostic provenance, never a compatibility constraint |
| `roots` | the explicit requests whose closure was resolved |
| `platform` | the one platform this v1 lock covers |
| `distributions` | exact selected nodes, sorted by canonical identity |

`language` is recorded when a root `META6.json` declares it. It is checked only
where the installer already knows how to judge the constraint; recording a
field must not invent enforcement.

### Distribution identity

Every node records:

- distribution `name`, exact `version`, `auth` and `api` (empty is explicit);
- a canonical `id` derived from those fields, used by dependency edges;
- the exact source URL or local-source descriptor;
- a SHA-256 of the raw archive bytes;
- `provides`, because resolution begins from module names;
- normalized runtime, build and test dependency edges;
- index provenance (`zef`, `REA`, local override) for diagnosis, not trust.

The archive SHA-256 is the integrity authority. The fez path SHA-1 remains
checked because it is part of the ecosystem protocol, but SHA-1 alone is not a
new lockfile's security boundary. REA downloads gain an exact digest when the
lock is written.

For Git sources, the immutable identity is a commit SHA plus a SHA-256 of the
canonical exported tree/archive. A branch or tag may be recorded as display
provenance, but it is never sufficient for frozen mode.

For `rakupp install --lock .`, v1 records a local root as mutable:

```json
"source": { "kind": "path", "path": ".", "tree-sha256": "..." }
```

`--frozen` verifies the tree hash after applying an explicit ignore list for
build output and VCS metadata. A path outside the lockfile's project tree is
refused unless `--allow-external-paths` was used while creating the lock; such
a lock says so visibly and is not portable.

### Dependency edges

An edge records both the declared request and its answer:

```json
{
  "name": "URI",
  "constraint": ":ver<0.3+>:auth<zef:someone>",
  "group": "runtime",
  "selected": "URI:ver<0.3.7>:auth<zef:someone>"
}
```

This duplication is intentional. `selected` makes replay trivial; the request
lets `lock verify` prove that the recorded answer still satisfies the metadata
that produced it and lets `lock why` explain the graph without fetching an
index.

System dependencies (`:from<bin>` and `:from<native>`) are recorded as
requirements with no selected Raku distribution. Frozen mode probes them using
the same rules as an ordinary install and fails with a platform-prerequisite
diagnostic. It does not pretend their bytes are locked.

Unresolved `any(...)` alternatives remain a refusal. A lockfile must never make
the policy guess the ordinary installer declines to make.

### Canonicalization

One graph must have one textual form:

- UTF-8, LF newlines, final newline;
- two-space indentation;
- fixed top-level and object field order;
- distributions sorted by canonical `id`;
- `provides` sorted by module name;
- dependency edges sorted by group, requested name, then selected identity;
- no timestamps;
- no absolute cache, temporary or store paths;
- URLs normalized only as much as the current index defines them—do not rewrite
  a signed or content-addressed URL into a guessed equivalent.

`generated-by.version` may make two otherwise equivalent files differ across
engine releases. `rakupp lock normalize` rewrites formatting/schema only when
explicitly requested; frozen install never rewrites provenance.

---

## Frozen semantics

`rakupp install --frozen` has one mode: execute, do not decide.

It must:

1. locate and validate the lockfile before touching a store;
2. reject an unsupported schema or platform mismatch;
3. verify every root and every dependency edge names a node in the lock;
4. reject cycles only if the ordinary install cannot execute them—the lock may
   represent a valid ecosystem cycle, but its install order must be explicit;
5. use locked URLs directly, without fetching either ecosystem index;
6. fetch from the local artifact cache when possible, otherwise the exact URL;
7. verify SHA-256 before extraction or build hooks;
8. verify extracted `META6.json` identity and `provides` against the lock;
9. install in the recorded dependency-first order using the existing test,
   build-hook, repository-lock and CURI writer paths;
10. leave `rakupp.lock` byte-for-byte unchanged.

It must not:

- pick a newer distribution;
- substitute an already-installed distribution with a different identity;
- fetch an index;
- weaken `auth` or `api` constraints;
- accept a moved tag, changed archive or redirect to different bytes;
- rewrite the lock to make reality fit;
- fall back from a missing locked artifact to an unlocked one.

An exact locked distribution already present and intact is reused. Under
`--to`, a foreign store entry is exact only when the store metadata and all
content-addressed blobs match; name/version equality alone is insufficient.

### Manifest drift

When the lock was created from `META6.json`, it records a canonical digest of
the root dependency inputs—not the entire manifest. Changes to description or
tags do not stale the lock; changes to identity, `depends`, `build-depends`,
`test-depends`, `provides`, language constraint or support/platform inputs do.

Frozen mode reports the first structural difference:

```text
rakupp.lock is stale:
  META6.json now requires JSON::Fast:ver<0.21+>
  the lock records JSON::Fast:ver<0.19.2>

Run `rakupp update JSON::Fast` to resolve a new graph.
```

It does not simply report “manifest hash mismatch”; a reproducibility feature
whose failures cannot be acted on will be bypassed.

### Failure atomicity

The lockfile writer uses the same discipline expected of the store:

1. write a sibling temporary file;
2. parse it back and validate the complete graph;
3. flush and close it;
4. atomically rename it over `rakupp.lock`.

A failed resolution, fetch, checksum, build hook, test, or store write leaves
the previous lock untouched. Lock creation occurs after a complete plan has
been resolved and all source identities are known; it need not wait until the
store is modified, but it is published only when the install succeeds.

The CURI store is additive and already guarded by `repo.lock`. This plan does
not claim a transactional rollback of files written before a later
distribution fails. The trace names those writes, a rerun is idempotent, and
the lockfile remains unpublished. Full store transactions are a separate
problem.

---

## Phases

### P0 — freeze the contract in fixtures

Before implementation, add fixture graphs covering the identity rules the
format must preserve:

- two distributions providing the same module;
- two versions of one distribution;
- `ver`, `auth` and `api` constraints;
- runtime/build/test groups;
- a system `bin`/`native` dependency;
- a dependency cycle;
- a local path root;
- a zef source and an REA source;
- an archive whose URL is stable but whose bytes are changed by the test.

Check in the expected canonical v1 lockfile for each. These goldens define the
format before the writer happens to define it.

### P1 — record the existing resolution

Refactor the installer's plan into data with no printing or store writes:

```text
parse requests -> resolve -> normalized graph -> order -> execute
```

Today those stages exist but share index-entry hashes and side effects. Give the
selected node and edge shapes names, make `--dry-run` render the normalized
graph, then add the canonical lock writer.

P1 changes **no resolver decisions**. For every fixture and a pinned live
sample, ordinary install before the refactor and `install --lock` after it must
select the same identities in the same executable order.

Deliverables:

- lock schema version and validator;
- canonical writer with atomic replacement;
- SHA-256 support through the in-process digest builtin, with an external-tool
  fallback when the installer runs under Rakudo;
- archive caching keyed by SHA-256;
- `install --lock`, `--lockfile` and useful `--dry-run` output;
- lock provenance in `trace.log`.

### P2 — exact replay

Add a second plan source: the lockfile rather than the resolver. Both feed the
same executor.

Deliverables:

- `install --frozen` with no index access;
- complete schema, graph, root-input and platform validation;
- exact installed-distribution reuse;
- digest before extraction and metadata verification after extraction;
- offline replay from the artifact cache;
- explicit failures for unavailable artifacts and system prerequisites;
- a trace assertion that no resolver/index function ran.

P2 is the campaign's first releasable boundary and the claim at the top of this
file.

### P3 — project-local environments

Once exact replay is proven independently, make isolation convenient:

```text
project/
  META6.json
  rakupp.lock
  .rakupp/
    repo/       project CURI store (ignored by Git)
    cache/      optional project artifact cache
```

Commands:

```sh
rakupp env create              # frozen when a lock exists, resolving otherwise
rakupp env path
rakupp env run -- COMMAND ...
rakupp env clean
```

When invoked inside a project, `rakupp` may eventually put `.rakupp/repo` ahead
of shared repositories automatically. That default flips only after a lookup
differential proves that commands outside a project and projects without a lock
are unchanged. The first P3 release can require `rakupp env run` and avoid an
implicit lookup change.

The local store is still CURI, not a new package layout. Existing repository
reading, integrity checks, precomp behavior, and module selection remain the
authority.

### P4 — controlled updates and explanations

Implement `rakupp update` only after P2 can prove exactly what stays pinned.

For `update NAME`:

1. unlock the named node;
2. unlock nodes that cannot remain valid after it moves;
3. retain every other exact selection as a hard resolver input;
4. resolve;
5. show old identity -> new identity plus the reason for every collateral move;
6. install and atomically publish the new lock.

If the current resolver cannot satisfy retained pins, fail with the constraint
chains; do not quietly turn a targeted update into a global update.

`rakupp lock why NAME` walks recorded edges from every root and needs no index.
`rakupp lock verify` validates format, graph, constraints, cached artifacts and,
with `--store`, installed content.

### P5 — vendoring and multi-platform locks

Vendoring copies raw locked artifacts into a project-controlled directory:

```sh
rakupp vendor --to=vendor/rakupp
rakupp install --frozen --offline --vendor=vendor/rakupp
```

The lock continues to name original sources and hashes; vendoring changes
availability, not identity. No extracted source trees: raw archives are smaller
and preserve the exact bytes the digest authenticates.

Lock v1 covers one OS/architecture. A later schema may hold platform variants:

```sh
rakupp lock add-platform linux-x86_64
```

Until that exists, a platform mismatch is an honest refusal with a command for
generating that platform's lock. Silently re-resolving only the platform-shaped
nodes would break the meaning of frozen.

---

## Implementation map

### `tools/install.raku`

This remains the implementation home. Add small, testable layers rather than a
second installer:

- `ResolvedNode`/`ResolvedEdge`-shaped hashes or classes;
- normalization from index/store/local entries;
- canonical graph ordering;
- lock parse, validate and write;
- graph-to-install-order;
- locked artifact fetch and verification;
- update pinning and graph explanation.

The ordinary resolver and the lock reader must converge before fetch/build/test
or store mutation. There must be one executor; duplicating the install path is
how frozen and ordinary installs would drift.

If the lock code grows beyond the script's readable limit, extract pure modules
under `tools/lib/Rakupp/Install/` and ship them beside `install.raku`. Do not put
resolver or lock policy in `src/`: compiled programs and embedders still must
not carry the installer.

### `src/main.cpp`

Only CLI dispatch/help changes belong here: recognizing `rakupp lock`,
`rakupp update`, and later `rakupp env`, then forwarding to the shipped
installer program. It must not parse the lockfile.

### `src/Interpreter.cpp`

No lockfile logic. P3 may need a small, general repository-discovery hook so a
project-local `inst#` store can precede the shared stores. That hook is tested
as module-resolution behavior, not installer behavior.

### `t/install/run.raku`

The existing network-free suite is the main gate. Extend its local fixture
index and archives; do not create a separate harness that can disagree about
how the installer is invoked.

Add focused pure-data tests if lock parsing/writing is extracted, but keep at
least one end-to-end process test for every command contract.

### Documentation

- `docs/guide/CLI.md`: command reference and failure output;
- `docs/guide/MODULES.md`: manifest versus lock, applications versus libraries;
- `docs/guide/faq/modules.md`: stale locks, offline installs and platform
  mismatches;
- `docs/dev/RELEASING.md`: the frozen-install gate once P2 lands.

---

## Gates

### G1 — canonical round trip

For every fixture:

1. parse the checked-in lock;
2. write it;
3. byte-compare it with the original;
4. parse the output again and compare normalized graphs.

Randomize index object order before resolution; the resulting lock must stay
byte-identical.

### G2 — resolver parity

Against a pinned fixture index, compare the identity/order list from the last
pre-lock installer with `install --lock`. Zero selection changes. Any intended
resolver improvement lands separately with its own ecosystem measurement.

### G3 — frozen means no resolution

Create a lock, then replace both configured indexes with paths that fail if
opened. `install --frozen` into an empty store must still succeed from the
artifact fixture/cache. The trace is also checked for zero index/resolver
events.

### G4 — time does not move the graph

Create a lock, add a newer compatible distribution to the fixture index, and
install frozen into a second empty store. Both store identity listings and all
source/resource blob hashes must match.

### G5 — integrity fails before execution

For each source kind, change the fetched bytes without changing the URL. Frozen
install must fail before extraction, build hooks or tests, name expected and
actual SHA-256, leave the lock untouched, and install no record for that
distribution.

### G6 — manifest drift is structural

Change each root input in turn (`depends`, build/test dependencies, identity,
`provides`, language/platform support) and assert frozen refusal. Change only
description/tags and assert the lock remains current.

### G7 — identity is complete

Fixtures with the same name/version but different `auth` or `api`, and two
distributions providing one module, must replay the locked selection. A global
store containing the tempting wrong candidate must not affect the result.

### G8 — failed writes are atomic

Begin with a valid lock. Plant a resolver failure, interrupted archive, failed
test, and unwritable destination in turn. The old lock remains byte-identical;
no temporary lockfile remains after a handled failure.

### G9 — cross-engine store compatibility

Install a frozen graph with rakupp into a fresh CURI store. Load the provided
modules with both rakupp and Rakudo. This preserves the installer plan's
existing cross-engine contract; a project environment must not become a
private format by accident.

### G10 — current release gates

The local suite, installer suite, module battery, store checker, documentation
links/examples and perf guard remain green. Lock handling is outside normal
program startup, so any measurable interpreter or `--exe` performance change
is a design error rather than accepted overhead.

Every new gate gets a planted-defect proof in `tools/prove-gates.raku` before it
is called a release gate.

---

## Failure messages

These are part of the feature. Each refusal says what was expected, what was
found, and the next safe command.

```text
locked artifact changed for URI:ver<0.3.7>
  expected sha256 4c1d...
  received sha256 b842...
  source https://...
refusing to extract or install it
```

```text
rakupp.lock covers macos-arm64, this host is linux-x86_64
frozen mode will not resolve a substitute graph
create a lock for this platform (multi-platform locks are not yet supported)
```

```text
locked distribution is unavailable:
  JSON-Fast:ver<0.19.2>:auth<zef:lizmat>
the cache has no verified copy and the locked URL returned 404
frozen mode will not select a newer release
```

Constraint conflicts print both root-to-edge paths. A bare “could not resolve”
is not sufficient for P4.

---

## Non-goals for the first release

- A new SAT/backtracking resolver.
- Hermetic or byte-reproducible native build outputs.
- Locking arbitrary system packages or compiler toolchains.
- Automatically trusting lockfiles from dependencies.
- Garbage-collecting the user's shared store to match one project.
- Silently activating a project-local environment.
- Multi-platform selection inside lock format v1.
- Signed registries or a new package-distribution service.
- Replacing CURI with a rakupp-only repository format.

These are exclusions, not objections. The point of P1/P2 is to establish one
small invariant completely: a decision already made can be recorded and
replayed without making a new decision.

---

## Risks and decisions

### The lock exposes stale or disappearing URLs

That is a real availability problem, not a reason to re-resolve. The local
content-addressed artifact cache and P5 vendoring are the remedies. Integrity
must win over convenience in frozen mode.

### Tests and build hooks are not deterministic

Frozen source can still run a time-, network- or host-sensitive hook. The trace
records hook commands, engine build and platform as it does today. A later
`--frozen --no-build-network` sandbox can narrow this, but v1 says “same source
graph,” not “hermetic build.”

### Test dependencies enlarge production installs

The groups are recorded explicitly. V1 replays the groups locked at creation;
`--production` may be added only once omitting test nodes is proven not to alter
the runtime/build closure. A mode must not reinterpret an existing lock
silently, so the selected groups live in `roots`.

### SHA-256 is not already the store's address

Keep both layers. CURI continues using the identifiers required for
Rakudo/zef compatibility. The lock verifies the raw distribution artifact with
SHA-256 before the existing writer expands it into CURI's per-file layout.

### Lockfile schema evolution

Unknown major `lock-version` values are refused. Additive optional fields may
be ignored only when the schema says they are non-semantic. A migration is an
explicit `rakupp lock migrate`; frozen install never rewrites a lock as a side
effect.

---

## Release boundary

P0-P2 make one release. P3-P5 are separate releases: environment activation,
updates, and vendoring each change a different contract and deserve their own
measurements.

The P2 tag is ready when all of the following hold in one sitting:

- canonical lock goldens pass;
- resolver parity is exact on the fixture corpus;
- two empty-store frozen installs have identical identity and artifact-hash
  manifests;
- the second succeeds with indexes made unavailable;
- every planted source mutation is rejected before execution;
- manifest and platform drift fail with actionable messages;
- the ordinary non-locking installer behavior and all standing release gates
  are unchanged.

The release note can then make the narrow claim at the top without an asterisk:
**commit `rakupp.lock`; `rakupp install --frozen` installs that graph, not
today's graph.**
