# Missing modules install themselves — plan

*Written 2026-10-14 at `0820ef24`, before any code. Goal: `rakupp app.raku`
runs a program whose `use` names a module that is not installed. The engine
fetches the distribution from the ecosystem, installs it into the ordinary
store and carries on, with no option to type and no step before the run. The
idea was "Inline script dependencies" in
[CLI-BORROW-PLAN.md](CLI-BORROW-PLAN.md), parked there as "a semantic decision
(a new header format, network at run time), not just a flag". This file makes
that decision: **no header format at all.** The `use` lines are the
declaration, and the network is reached only under the rules below.*

**The number a stranger can re-measure:**

```bash
rakupp t/deps/run.raku                                  # the gate, network-free
HOME=$(mktemp -d) rakupp -e 'use JSON::Fast; say to-json [1]'
```

The gate runs against a fixture index and must pass every check. It must also
fail on `0820ef24`, which proves its checks can fail. The second line is the
claim in one command. On an empty home it prints
`installed JSON::Fast:ver<…>:auth<zef:timo>` on stderr and `[1]` on stdout.
The same line run a second time prints only `[1]` and makes no network
request.

---

## Why this one

The newcomer's path today is: install the engine, learn that modules come
from an installer, install the modules, then run. The second and third steps
are where people leave. Python's `uv run` (PEP 723), Deno's remote imports and
Bun's auto-install each removed those steps, and each spread because of it.

Raku++ already owns every piece except the wiring:

- **The installer is in the binary.** `rakupp install` runs
  `tools/install.raku`, compiled in at build time (`src/main.cpp:2590`). A lone
  `rakupp` executable has it, and it needs no zef and no Rakudo.
- **It already reads a program's dependencies.** `rakupp install app.raku`
  runs `program-wants` / `source-uses` (`tools/install.raku:1308`). They read
  every `use` and `need` with its adverbs, skip Pod and heredocs, honour
  `use lib`, and deliberately skip `require`.
- **The store keeps every version.** Installing a newer version adds an entry
  beside the old one ([MODULES-PLAN.md](MODULES-PLAN.md)). Identical files are
  stored once, writers take `repo.lock`, and zef and Rakudo read the same
  `~/.raku`. A program's dependencies can therefore go into the ordinary store
  with nothing displaced and nothing duplicated. A per-script environment, the
  thing `uv` has to build, is not needed here.
- **The miss has one place.** Every failed `use` ends at one throw,
  `Interpreter::loadModuleImpl` (`src/InterpreterModules.cpp:1792`). It comes
  after `-I` / `lib/`, after every store, and after the modules bundled in the
  binary, and the function already distinguishes `requireForm`.

Rakudo cannot easily follow, because its module loading and its installer
(zef) are separate programs. Nothing here changes the language: the script
contains only ordinary `use` lines, so it also runs under Rakudo once someone
has run `zef install`.

---

## The contract

### What the user sees

```console
$ cat app.raku
use JSON::Fast;
say to-json %(ok => True), :!pretty;

$ rakupp app.raku
installed JSON::Fast:ver<0.21.0>:auth<zef:timo> for app.raku
{"ok":true}

$ rakupp app.raku
{"ok":true}
```

- **Standard output is the program's alone.** Everything the fetch says goes
  to stderr. `rakupp app.raku > out` writes the same bytes to `out` on the
  first run and on every later one.
- **One line per distribution installed**, naming its full identity, auth
  included. The user always sees where the code came from. `-q` does not drop
  this line: it reports a change to the user's store, not chatter about a mode.
- **Nothing else changes.** A module that is already installed, found under
  `-I` or `lib/`, or bundled in the binary loads exactly as it does today,
  without touching the index.

### The five rules

1. **Exact index match only.** The missed name is looked up in the cached zef
   index by the installer's own `candidates()`: an exact distribution name,
   or an entry in a distribution's `provides`. REA is not asked about the
   missed name: it is 18 MB, and a name only REA has is an old release a
   program is unlikely to mean (`resolve()` still reaches it for a dependency
   pinned to one). A name in
   neither fails as it does today. Line 1 of the error stays byte for byte
   `Could not find X in:` ([TRACER.md](../../guide/TRACER.md)'s rule), the
   exception stays `X::CompUnit::UnsatisfiedDependency`, and one line is added
   below the search path: `(not in the ecosystem index either)`.
2. **`use` and `need`, never `require`.** `try require Foo` is how Raku code
   asks whether an optional module is present, so installing it there would
   change what the program does. Calls through the `CompUnit::Repository` API
   (`src/Builtins.cpp:7266`, `:7504`) are left alone for the same reason.
3. **The typo guard.** Fetching a name runs code the user did not write, so a
   misspelt `use` is the real risk. The fetch is refused if a far more depended-on
   module name is a typo's distance away. The rule and its measurement are
   below. The refusal names
   the neighbour: `did you mean JSON::Fast? (to install JSON::Mask itself,
   pin it: use JSON::Mask:auth<…>)`. An explicit `:auth<…>` or `:ver<…>` on the
   `use` counts as proof of intent and passes the guard. The source says what
   is meant, so no option is needed.
4. **No module test suites.** These installs run as `--no-test`. Running a
   suite takes from seconds to minutes, and only 1,019 of 2,547 distributions
   pass their own suites here today. With the test gate on, half the ecosystem
   could never be fetched this way. `rakupp install Foo` keeps its test gate.
5. **An off switch, not an on switch.** `RAKUPP_DEPS=0` restores today's
   behaviour exactly, for CI and production setups that want a missing module
   to stay an error.

### Where it is on, and where it is off

| context | fetches? | why |
|---|---|---|
| `rakupp FILE`, `-e`, stdin, `-n`/`-p` | **yes** | the feature |
| the REPL, `--repl-after` | **yes** | `use Foo` at a prompt is the same request |
| `EVAL 'use Foo'` | **yes** | a `use`, compiled when the EVAL runs |
| `--jupyter` | **yes** | a notebook cell is a program |
| `--mcp` | **yes**, see open question 3 | `--mcp` already gives an agent shell-level trust ([MCP.md](../../guide/integrations/MCP.md)) |
| `--exe`, `--bundle`, `--aot` | **at compile time** | the fetched modules are embedded (D4) |
| a binary those modes produced | **no** | a shipped binary never reaches the network on its own |
| `require`, `CompUnit::Repository` calls | **no** | rule 2 |
| `-c`, `--lint`, `--lsp` | — | they never load modules (`rakupp -c -e 'use NoSuchThing'` says `Syntax OK`) |
| `--sandbox` | **no** | the sandbox refuses processes and sockets |
| `RAKUPP_DEPS=0` | **no** | rule 5 |
| children of `rakupp install` / `rakupp test` (suites, build hooks) | **no** | a suite that `use`s an undeclared dependency must fail as under Rakudo. Otherwise the ecosystem sweep's numbers inflate |
| `librakupp` embedding, the bindings | **no** unless the host asks | a host program decides what its process does: `RkConfig.deps` |
| Raku.js / WASM | **no** | no processes and no store |

---

## How it works

### The fetch is a child process

`loadModuleImpl` cannot run the installer in-process: there is one
interpreter per process ([EMBEDDING.md](../../guide/EMBEDDING.md)), and a miss
happens partway through compiling the program. At the throw site, after the
pragma check, the engine instead runs itself:

```
$EXECUTABLE install --auto --for=<program path or -e> <missed identity>
```

with the environment `RAKUPP_DEPS=0`, so that nothing the child loads can
recurse. The child's stdout is copied to the parent's stderr. If the child
succeeds, the lookup is retried exactly once. If it fails, the original
exception is thrown with the child's one-line reason added.

**One child per run, not one per module.** Given `--for=FILE`, the installer
runs `program-wants` over the file and installs every direct `use` of the
program that is not yet satisfied, dependencies first. The first miss
therefore brings in the whole program, and its modules' own dependencies
arrive through `resolve`. A second child is needed only for a name the text
scan could not see, such as a `use` inside a string handed to `EVAL`.

### `install --auto`, the installer's side

`--auto` is an internal mode, not part of the documented surface. It is
`--no-test --quiet` plus:

- the typo guard (rule 3), computed for the requested names only. Comparing
  one name against the 484 module names with five or more dependents is
  instant; it is the all-pairs survey below that took 32 s;
- distinct exit codes (*not in any index*, *refused by the guard*, *network
  or store failure*), so the parent can say which happened;
- a refresh on a miss. A name absent from an index cache older than an hour
  refreshes the index once, which catches a module published today. Today's
  24-hour TTL (`$CACHE-TTL`, `tools/install.raku:56`) otherwise stands;
- an offline stamp. A failed network attempt writes
  `~/.raku/rakupp-install/offline`, and for ten minutes after that every
  `--auto` answers from the cache alone. A machine without network then pays
  the curl timeout once, not once per process: a Roast run starts 1,425
  processes. The fetches in this mode use `--connect-timeout 5`;
- the pin record (D3).

### Pin records: the same versions next time

The first run resolves and records what it chose. A later run loads exactly
that, even after a newer version has been installed for some other program.

- **Where:** `~/.raku/rakupp-install/scripts/<sha1 of the absolute path>.json`,
  beside the installer's existing cache (`cache-dir`, `tools/install.raku:313`).
  It holds the path, each `use` as written, and the dist identity chosen for
  it and for every distribution in its closure.
- **Keyed by path, not by content.** A content hash would re-resolve
  everything whenever any line changed, and silently move versions on an
  unrelated edit. With a path key, only a `use` that is new or changed is
  resolved. Moving the script resolves again, which is the honest answer for
  a different file.
- **The loader reads it.** `pickInstalledDist` takes the newest installed
  version that satisfies the `use` (`src/InterpreterModules.cpp:1729`). With a
  record present for the main program, a pinned name loads its pinned dist
  id, process-wide, so the closure holds too. An edited `use Foo:ver<0.22>`
  that the pin no longer satisfies is resolved again, and the pin is
  rewritten.
- **An uninstalled pin** should be fetched again: the identity is exact, so
  there is nothing to choose. Not built yet; today that name loads the newest
  installed version (Where it stands).
- Programs from `-e`, stdin and the REPL have no path and get no record.
- This is deliberately narrower than [LOCKFILE-PLAN.md](LOCKFILE-PLAN.md):
  there is no file in the project, no archive digests and no `--frozen`. If
  that plan lands, a script's record is the natural seed for its
  `rakupp.lock`.

### Compiling

`--exe`, `--bundle` and `--aot` call the same fetch for the program's wants
before `collectModuleGraph` (`src/main.cpp:1378`, `:1667`). The binary then
embeds the modules, and one script becomes one file that needs nothing at run
time. With `--standalone`, a fetch that fails is a build error, as a module
that cannot be embedded already is.

---

## What the probes found

All four were run on 2026-10-14 against the cached zef index (8,211 entries,
9,698 module names), on an Apple M3. The probe scripts were scratch files;
D0 turns the first two into gate checks.

**Roast does not collide.** Every `use` / `need` in Roast's `.t` files was
checked against the index, excluding modules Roast ships under `packages/`.
The single hit was English prose ("Can use Unicode properties",
`S15-nfg/regex.t:29`). The missing-module tests use names such as
`ThisDoesNotExistAtAll` (`S32-exceptions/misc.t:148`), which no index has, so
rule 1 leaves them byte-identical with no network access.

**The typo guard refuses 4 of 9,698 names.** Dependents are counted once per
dependent distribution *name*. The first probe counted index entries, so a
distribution with 40 releases voted 40 times: JSON::Fast came out at 1,651
dependents, but 220 distributions depend on it. That inflated count also made
the first rule (5 dependents and 10 times the asked name's own) look right
when it was not. With honest counts it missed `Pod::To::MarkDown`, which then
installed 41 distributions in a test run. A looser bar (5 and 5 times) blocked
25 names, including real modules (`Digest::SHA1` is not a typo of
`Digest::SHA`). The rule that shipped, in `typo-neighbour`
(`tools/install.raku`):

- compare lowercased; allow a distance of 1 for a name under 8 characters,
  2 otherwise;
- a neighbour provided by the asked name's own distribution is no hazard:
  either spelling installs the same thing (`LLM::Function` is a module of
  LLM::Functions, and `DBDish` of DBIish);
- a case-only difference is the strongest signal: the neighbour needs 3
  dependents and 3 times the asked name's own;
- any other needs 5 and 10 times, and the asked name is credited with its
  providing distribution's standing too (`Cro::TCP`, a module of the
  well-used Cro::Core, is not a typo of `Cro::HTTP`);
- core names and empty ones (malformed `depends` entries give `""` eight
  dependents) are never neighbours.

On the 2026-10-14 index that refuses exactly `Dist::META` (near Test::META,
97 dependents against 0), `File::file` (File::Find, 24 against 1),
`JSON::Mask` (JSON::Fast, 220 against 0) and `Pod::To::MarkDown`
(Pod::To::Markdown, 5 against 0). Each still installs with `:auth<…>` on
the `use`, and the refusal prints that spelling. Checking one name against
the whole index takes about 0.3 s, paid only on a fetch.

**269 module names are published under more than one auth.** Almost all are
transfers: an author's account rename (`zef:p6steve` → `zef:librasteve`), or
a move to `zef:raku-community-modules`. The installer resolves a name to the
exact dist name first, then the newest version, whatever the auth
(`candidates()`, `tools/install.raku:578`), and zef does the same. `--auto`
keeps that rule, so that `rakupp app.raku` and `rakupp install app.raku` can
never pick different code. The stderr line names the auth, and open question 1
asks whether to go further.

**What a first run costs.** Resolving with a warm index takes 0.13 s
(`install --dry-run JSON::Fast`). A real `--no-test` install into an empty
store took 0.85 s for JSON::Fast (one distribution) and 14.7 s for Cro::HTTP,
whose 21 distributions are fetched one after another. The cold index is
10 MB. D6 fetches archives in parallel.

---

## Phases

### D0 — the gate first

`t/deps/run.raku`, network-free. Like `t/install/run.raku`, it uses
`RAKUPP_INSTALL_INDEX` pointing at a fixture index and locally built archives,
with a temporary `HOME`, so no check touches the user's store. Checks:

- a script `use`ing a fixture module runs; stdout is the program's alone, and
  stderr has exactly one `installed …` line;
- the second run prints nothing on stderr, and with the index file removed it
  still runs, so it made no lookup;
- a name in no index fails with line 1 byte-identical to `0820ef24`'s output,
  `X::CompUnit::UnsatisfiedDependency` intact;
- `try require Fixture::Mod` stays `Nil`, and nothing is installed;
- a fixture typo pair is refused with the neighbour named; the same `use`
  with `:auth<…>` installs;
- `RAKUPP_DEPS=0`, `--sandbox`, and a child of `rakupp test` each leave the
  miss fatal;
- a pin holds: install v2 of a fixture beside the pinned v1, and the script
  still loads v1;
- the offline stamp: with an unreachable index URL, the second process does
  not wait for a timeout.

On `0820ef24` every "installs" check fails, and every "stays fatal" check
passes.

### D1 — `install --auto`

All of the installer's side above, in `tools/install.raku`: the guard, the
exit codes, the refresh on a miss, the offline stamp and short timeouts, and
`--for=FILE`. `child-env` sets `RAKUPP_DEPS=0` for suites and build hooks.
It can be tested by itself, before the engine calls it.

### D2 — the engine calls it

`loadModuleImpl`'s throw site, behind the off-list in the table. The child
runs through the existing spawn helpers, with stdout routed to stderr. A
`RAKUPP_DEPS` knob goes in the help text's Environment block. The exception
message gains its one added line.

### D3 — pin records

The record writer in the installer, the reader in `pickInstalledDist`, and
re-resolving an edited `use`.

### D4 — compiling

Fetch before `collectModuleGraph` in the three compile modes. `--standalone`
makes a failed fetch an error. A compiled binary never fetches: the
runtime library `--exe` links has no installer, and the knob is forced off
there as well.

### D5 — hosts

The switch (`g_depsFetch`) is off unless `main()` turns it on for an
interpreter run, so `librakupp` hosts, compiled binaries and Raku.js never
fetch without anyone deciding so. Still to build: `RkConfig.deps`, so a host
can opt in, exposed by the bindings when they next move. `--mcp` and `--jupyter` follow the table. `tools/mcp-smoke.raku`
and `tools/jupyter-smoke.raku` get one check each.

### D6 — docs and speed

[MODULES.md](../../guide/MODULES.md) gets a section that leads its page.
[CLI.md](../../guide/CLI.md) gets the knob; the README's Modules section gets
one paragraph and the zero-setup example; a faq entry covers "where did this
module come from". The CHANGELOG gets an entry. CLI-BORROW-PLAN's entry moves
here. Archives are fetched in parallel, so Cro::HTTP's 14.7 s is the number to
beat.

---

## Gates, every batch

- `t/deps/run.raku` and `t/install/run.raku`.
- `t/run.raku`.
- `tools/run-roast.raku`: the lists must be identical to `0820ef24`'s. The
  probe says no Roast name reaches the index, and the gate holds that.
- The module battery and any ecosystem sweep run with `RAKUPP_DEPS=0`.
  A distribution's pass rate must measure its own declared dependencies, not
  this feature.
- `tools/mcp-smoke.raku`, `tools/jupyter-smoke.raku`, `tools/embed-smoke.raku`.
- A MinGW `-fsyntax-only` pass over every touched `.cpp`. Windows spawns the
  child differently, and no local build sees that branch.

---

## Open questions for the maintainer

1. **Auth on a name with several publishers.** Keep the installer's rule
   (exact name, newest version, any auth; matches zef and `rakupp install`).
   Or, in `--auto` only, prefer the auth with the most dependents, which
   defends against someone publishing a higher version of a popular name under
   a new account, at the cost of the two commands disagreeing. The plan takes
   the first; the data is above.
2. **Untested installs in the shared store.** A module fetched this way never
   ran its suite. Should `rakupp install --list` mark it, and should a later
   `rakupp install Foo` run the suite it skipped?
3. **`--mcp` default.** On, as the table says, because an agent writing
   `use JSON::Fast` is the common case. Or off, because an agent's typo is
   less visible than a person's. `--mcp --sandbox` is off either way.
4. **The knob's name.** `RAKUPP_DEPS=0` is short. `RAKUPP_AUTO_INSTALL=0` says
   more.

---

## Where it stands

*2026-10-14:* D0–D4 are built, and D6's guides.

- **The gate:** `t/deps/run.raku`, 42 checks, all pass. On `7ae228d3` (the
  plan's commit, before any code) it gave 18 ok and 24 failed: every
  "fetches" check failed and every "stays fatal" check passed. It runs in
  CI beside the installer gate.
- **Other gates on the same build:** `t/install/run.raku` 159/159;
  `t/run.raku` 1,626/1,626; Roast (checkout `cf28a323c`) 1,425 of 1,425
  files and 219,207/219,207 tests without skip and todo, with a file list
  identical to v5.4.1's; `mcp-smoke`, `jupyter-smoke` and `embed-smoke`
  pass; the source budget is OK. No MinGW compiler on this machine, so the
  Windows syntax pass is CI's.
- **Where the code is:** `install --auto` in `tools/install.raku`
  (`auto-index`, `typo-neighbour`, `auto-install`, `write-pins`);
  `child-env` gives suites and hooks `RAKUPP_DEPS=0`. In the engine,
  `depsAsk` runs the child (`src/InterpreterModules.cpp`), the store search
  in `loadModuleImpl` became a lambda that runs again after a fetch, and
  `depsPinFor` / `pickPinnedDist` read and apply the pins.
  `collectModuleGraph` fetches at the compile-time skip, for `use` and
  `need` only. `UseStmt.authReq` keeps `:auth<…>` (AST cache v45).
- **The docs:** MODULES.md leads with the feature; CLI.md (the installer
  section and the environment) and the modules FAQ cover it, and `--help`
  lists `RAKUPP_DEPS`. The README and the CHANGELOG wait for the release
  that ships it.

**Open:**

- **D5:** `RkConfig.deps` for embedding hosts.
- **D6:** fetch archives in parallel. Cro::HTTP's 14.7 s for 21
  distributions is the number to beat.
- **Pins on Windows:** the engine keys the record by `_fullpath`, the
  installer by `IO::Path.resolve`. If the two spell the path differently
  (case, slashes), the script falls back to the newest version, which is
  harmless but not pinned. A Windows check belongs in the gate once one can
  run there.
- **A pinned version that was uninstalled** loads the newest installed one
  rather than fetching the pinned one again, as the plan first said.
- The four open questions above still stand.
