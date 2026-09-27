# Plan: three Roast tracks — Scalar containers, a gather that suspends, parametric roles

*Written 2026-09-27 at `b3119f71`, before any code. Each track is sized for one
session working on its own. This document is what the three sessions share: the
rules all of them follow, then one section per track with its files, what
exists today, the design, the phases and the gates.*

*These are the three mechanisms the one-file-at-a-time sweep
([V5-PLAN.md](V5-PLAN.md), "the parallel campaign's map") keeps running into and
cannot fix with a local patch. Their clusters come from the 2026-09-26 survey
([roast-clusters.tsv](../findings/survey-2026-09-26/roast-clusters.tsv)),
re-measured below against `b3119f71`.*

---

## Where things stand

- **Roast at `b3119f71`:** 1,266 of 1,434 `spectest.data` files fully pass
  (165 partial, 3 without TAP). On the raw checkout (`--all`) it is 1,278 of
  1,464.
- **The three tracks cover 55 of the 168 files still failing**, all 55 on
  `spectest.data` and all passed by Rakudo:

  | Track | Files | Of them, failing ONLY for this track's reason |
  |---|---:|---:|
  | A — Scalar containers | 32 | 24 |
  | B — a gather that suspends, and lazy iteration | 10 | 9 |
  | C — parametric roles as first-class objects | 13 | 12 |

- **How the lists were made.** The survey's clusters (`CONT`, `HOLES` → A;
  `LAZY` → B; `ROLEGRP`, `MOP`, `GENERIC`, and one `COERCE` file → C),
  intersected with the files that still fail at `b3119f71`, then every file
  re-run on its own. The "failing now" column is the non-TODO `not ok` lines of
  that run (subtest lines included), cut short where long.
- **Estimates** are effort guesses, not measurements: A ~2–3 weeks, B ~1.5
  weeks plus a few days for Seq consumption, C ~1.5–2 weeks.

## Rules for all three sessions

- **One worktree and one branch per session.** Three sessions editing
  Interpreter.cpp in one tree break each other's builds (it has happened), and
  a peer's half-done edit shows up as a mystery regression. Branches:
  `track/containers`, `track/gather`, `track/roles`. Configure the worktree's
  own build directory: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
  (macOS arm64: add `-DCMAKE_OSX_ARCHITECTURES=arm64`), then
  `nice cmake --build build -j4 --target rakupp`. Never pipe the build into
  `head`: SIGPIPE kills the link.
- **Where A and B overlap.** Both touch the `map`/`grep`/`first` drivers in
  MethodCallTail.cpp and the `for` loop in Interpreter.cpp. The split: **B owns
  WHEN an element is produced** (laziness, pulling, consumption); **A owns WHAT
  a produced element IS** (a container or a value). Whichever branch lands
  first goes in as it is; the other rebases onto it. C works in role and class
  composition (the `…Role…` functions around `bindRoleParamsInto` in
  Interpreter.cpp, and MethodCallPart2.cpp), away from both.
- **Oracle before semantics.** Probe real Rakudo (2026.08) on the exact snippet
  before deciding what a construct does. Every file listed here passes under
  Rakudo, so the test file is a second oracle, not the only one.
- **Gates, in this order:**
  1. the track's own files (the tables below);
  2. **a full Roast run, one session at a time.** Concurrent runs on one
     machine fake regressions. `ROAST_TIMEOUT=60 nice build/rakupp
     tools/run-roast.raku -j=3 --list=SCRATCH/new.list`. `--list=` WRITES that
     file, so point it at scratch space, never at a committed list. Take one
     run of the base commit before the first change, and keep its output (the
     `[PASS]`/`[part]` lines). Compare per-file `n/m` against it, not just the
     pass count: a partial file that loses an assertion is a regression;
  3. `t/regression`: 713 files. Compare exit code and `ok`/`not ok` counts per
     file against a build of the base commit (65 files `use Test`, so "exit 0,
     last line PASS" alone misses things);
  4. `build/rakupp t/run.raku`. Its one known failure,
     `nativecall-alias-repr-deref.raku`, fails at `b3119f71` too;
  5. **A and B:** `tools/perf-guard.raku --check` against the base commit's
     binary. A changes the hottest read path in the interpreter and B changes
     every gather, so both must show numbers;
  6. **C:** the battery compile scan from V5-PLAN B0 (`tools/battery-scan.raku`,
     not built yet). Roles are everywhere in the ecosystem. Until the scan
     exists, `use` the battery modules that declare parametric roles, before
     and after.
- **Known noise at `b3119f71`:** `6.c/MISC/bug-coverage-stress.t` test 3 (a
  SIGINT sent to a child process) fails at the base commit as well; timing, not
  code. `S17-channel/basic.t` is flaky under load.
- **Commits:** the user batches them. Stage explicit paths, never `git add -A`.
  Commit when asked.

---

## Track A — Scalar containers

### The problem

rakupp has no container type. A slot holds a Value (Value.h, `enum class VT`
near line 481, `sizeof(Value)` = 128), so two names cannot share one
container. Rakudo semantics that need a shared container are emulated one
construct at a time, and each emulation copies the value back afterwards. What
exists today:

| Mechanism | Where | What it emulates |
|---|---|---|
| `makeSharedCellProxy`, `cellOfProxy`, `makeEnvSlotProxy` | Interpreter.cpp | `:=` between names: a Proxy hash with FETCH/STORE closures around a shared cell |
| `setupRwLinks`, `rwWriteThrough`, `copyOutRw`, `lvalueThroughRw`, `rwDirect` | Interpreter.cpp | `is rw` / `is raw` parameters: one hop of write-through plus a copy-out at return |
| `topicWriteback_` | Interpreter.cpp, MethodCallTail.cpp | `$_` in `map`/`grep`/`first`/`deepmap` aliasing the element |
| `builtinTopicWB_`, `ArgWriter` (`builtinArgWriter_`, `pendingArgWriter_`) | Interpreter.cpp | `*++`, `* += 2` writing their argument back (added in `b3119f71`) |
| `pendingRwSlots_`, `setupRwSlots` | Interpreter.cpp | hyper calls writing elements |
| `pairsAliasSource`, `valuesAliasSource`, `scalarListAlias`, `derefArrayAlias` | Interpreter.cpp | `for %h.pairs`, `for %h.values`, `for @a` writing back |
| `pairValRO`, `exprNamesContainer`, `markPairValueRO` | Value.h, Interpreter.cpp | whether a Pair's value is writable, decided from the value's EXPRESSION |
| `readonly`, `immutableBind` flags | Value.h | read-only binds (`my $b := 3`) |
| `exprYieldsContainer`, `valContained` | Interpreter.cpp | whether a routine hands back a container |

About 400 references in all. Each covers one shape; this track's failing
files are the shapes none of them covers.

### Design: promote on alias

**Not the full Rakudo model**, where every `$` variable and every Array element
is its own heap container. That puts an allocation and an indirection behind
every element, and it runs directly against V6-PLAN P3 and
[REPRESENTATION-PLAN.md](REPRESENTATION-PLAN.md), which want Value smaller.

Instead, a slot is promoted to a **shared cell** only when a second name for the
same container comes into existence:

- `:=` between names; `is rw` and `is raw` parameters; `\x` parameters;
- `$k => $v` and `Pair.new($k, $v)`: the Pair's value already lives behind a
  `shared_ptr<Value>` (`pairValS`), so the pair can share the variable's cell
  directly;
- `(…, $v, …)`, `\(…, $v, …)` and `List.new(…, my $ = 3)`: an element built
  from a container;
- `for`/`map`/`grep`/`first` aliasing, `.kv`/`.pairs`/`.values` views,
  `reverse` and `grep` results, `take-rw`, `return-rw`.

Mechanically:

- **A first-class cell kind** in Value: a `shared_ptr<Value>` in a dedicated VT
  or flag. Not a Proxy hash with closures.
- **Reads decontainerize at a few central sites:** variable lookup, element
  fetch, argument evaluation, method invocant. Each site pays one type check
  when no cell is present.
- **Writes go through the cell in `lvalue()`,** which returns a pointer into
  the cell's Value.
- **Identity:** `=:=` compares cells, and the `.WHICH` of a Pair whose value
  is a container is its cell's address (`whichIsObjAt` in Builtins.cpp). Rakudo
  gives `(foo => $v).WHICH` an object identity and `(foo => 100).WHICH` a value
  identity; `pair.t` test 180 asserts it.
- **What stays a value:** a List element that came from a literal, a Map
  value, a Seq element, a QuantHash count. Those keep refusing writes as they do
  now. QuantHash `.values` is the one special case: its elements are virtual
  containers whose write deletes the key when the new value is 0 (Rakudo hands
  back Proxies). An existing Proxy can serve here.

### Phases

**A1: the cell, `:=`, rw parameters, identity (~1 week).** Expected to flip:
`S03-binding/scalars.t`, `S03-operators/identity.t`, `S03-binding/nested.t`,
`S03-binding/hashes.t`, `S09-multidim/indexing.t`, `S06-traits/is-rw.t`,
`S06-traits/slurpy-is-rw.t`, `S04-statements/for_with_only_one_item.t`,
`S04-statements/once.t`, `S06-routine-modifiers/proxy.t`,
`S32-scalar/undef.t`.

**A2: elements and views (~1 week).** Pair, List, Capture elements, the
iteration views, holes. Expected to flip: `pair.t`, `capture.t`, `list.t`,
`lists.t`, `quanthash.t`, `S32-hash/kv.t`, `S32-hash/map.t`, `S32-list/grep.t`,
`S32-list/reverse.t`, `S32-array/adverbs.t`, `S32-array/create.t`,
`S32-basics/xxPOS.t`, `S04-statements/for.t`, `S04-statement-modifiers/for.t`,
`S04-statements/when.t`, `S32-array/delete.t`, and the container halves of
`S32-list/map.t`, `S32-list/reduce.t`, `S12-attributes/instance.t`,
`S09-typed-arrays/arrays.t`.

**A3: retire the copy-back mechanisms (~0.5–1 week).** Once `is rw`, `for` and
the map drivers bind cells, remove `rwLinks`/`copyOutRw`, `topicWriteback_`,
`builtinTopicWB_`, `ArgWriter`, `pendingRwSlots_` and the Proxy cells, one at a
time, each behind a full gate. This is where perf can come back: the copy-outs
run today on every return from a routine with an `is rw` parameter.

**Backends.** The `--exe` codegen (Codegen.cpp) and the JS backend
(src/codegen/Js.cpp) share the semantics. [TRANSPILE-PLAN.md](TRANSPILE-PLAN.md)
already names slot binding as the one design item left for JS. At minimum, a
construct the backend cannot express must refuse (and fall back), never
silently copy.

**Risk:** the highest of the three. A1 changes every variable read. Land A1
behind the perf gate before A2 grows the surface.

### Files

Harness counts are `b3119f71`'s. "Also" names failures in the file that belong
to another cluster; A alone will not flip those files.

| File | Now | Failing now | Also |
|---|---|---|---|
| S02-types/capture.t | 43/46 | 28 modify Capture positional elements; 29 …associative elements; 35 `\(42)` vs `\($a)` must be `!===` | |
| S02-types/list.t | 76/77 | 75 List.new keeps a passed container (`List.new(1, 2, my $ = 3)`, `$l[2] = 42` lives) | |
| S02-types/lists.t | 28/30 | 1 assigning a list element changes the original variable; 13 list slices as lvalues | |
| S02-types/pair.t | 179/182 | 139 `.value =` changes the variable; 171 `Pair.new("foo", my Int $)` type-checks assignment; 180 clone of a container Pair has its own `.WHICH` | MISC |
| S02-types/quanthash.t | 126/129 | 97, 112, 127 `%qh.values.map({ $_ = 0 })` removes the keys | |
| S03-binding/arrays.t | 45/48 | 39 array assignment creates new containers; 47 `my @a := 1` dies; 48 binding a Failure to an Array | DIAG |
| S03-binding/hashes.t | 36/37 | 30 hash assignment creates new containers | |
| S03-binding/nested.t | 42/43 | 37 binding an element of a structure to another element of it | |
| S03-binding/scalars.t | 30/33 | 2, 9 binding to a literal is X::Bind; 20, 21 a bound read-only sub param stays read-only | DIAG |
| S03-operators/identity.t | 40/46 | 22, 24 rebinding array elements; 27–32 a bound scalar subparam (plain and `is rw`) keeps `=:=` | |
| S04-statement-modifiers/for.t | 30/31 | 30 `for` does not decontainerize | |
| S04-statements/for.t | 109/111 | 100 `for` does not decontainerize; 111 holes written through `for @a[*]` | HOLES |
| S04-statements/for_with_only_one_item.t | 10/12 | 10, 11 `for $single -> $x is rw` | |
| S04-statements/once.t | 23/24 | 24 `once` does not containerize (`my \z := once 42; z = 100` dies) | |
| S04-statements/when.t | 24/26 | 21 `when`, 24 `default` do not strip Scalar containers | |
| S06-routine-modifiers/proxy.t | 24/26 | 24 lvalue sub called the right number of times; 26 a subclass of Proxy | |
| S06-traits/is-rw.t | 7/9 | 6 pairs are mutable; 7 `is rw` unnecessary on objects/references | |
| S06-traits/slurpy-is-rw.t | 1/3 | 2, 3 an `is rw` slurpy writes `@test` and `$test` | |
| S09-multidim/indexing.t | 31/32 | 30 an Int bound into a slot cannot be assigned | |
| S12-attributes/instance.t | 150/152 | 142 Scalar containers respected in attribute initialization | DIAG (141) |
| S32-array/adverbs.t | 604/606 | 605, 606 a container respected by `:exists` / `:delete` | |
| S32-array/create.t | 10/12 | 9 array elements get writable containers; 10 `Array.clone` shares a partial reifier | |
| S32-basics/xxPOS.t | 63/64 | 64 changing an "element" without an ASSIGN-POS | |
| S32-hash/kv.t | 26/27 | 27 `$pair.kv` aliases are rw | |
| S32-hash/map.t | 22/24 | 18 `.WHICH` stays unchanged; 19 a hash value is containerized | |
| S32-list/grep.t | 49/50 | 40 `grep` is rw-like (chained writes land in the source) | LAZY |
| S32-list/map.t | 60/62 | 58 `map` can modify what it iterates | PHASER (62, LAST phaser) |
| S32-list/reduce.t | 22/24 | 23 reduce returns raw containers | MISC (18, chain associativity) |
| S32-scalar/undef.t | 87/91 | 37, 38, 42 undefining a referent / dangling references; 79 undefining a returned array | |
| S32-list/reverse.t | 22/23 | 21 `reverse` returns containers, also for holes | HOLES |
| S32-array/delete.t | 32/34 | 33, 34 `.List`/`.Slip` fill holes with Nil (plain and `is default`) | HOLES |
| S09-typed-arrays/arrays.t | 80/84 | 47 an inner array enforces the type; 79 binding a variable to a typed array checks the type; 80 a deleted element keeps its type inside `.map`; 81 `.gist` shows the real type objects | HOLES, DIAG |

---

## Track B — a gather that suspends, and lazy iteration

### The problem

A `gather` cannot stop at a `take` and resume later. The gather arm of
`evalUnary` (the `u->op == "gather"` branch in Interpreter.cpp) does this
instead:

1. A **probe** runs the block up to 64 takes or 20 ms, whichever comes first.
2. A block that finishes inside the probe is **eager**: its whole body has
   already run. That is why `gather { take $_ for 0..^4; $was-lazy = 0 }.lazy`
   has run to the end before anything asks for an element.
3. A block that hits the cap becomes lazy by **re-running from the start**
   with a doubled cap. A snapshot/restore of the variables it writes
   (`gatherWritesStmt`) keeps the re-runs consistent.
4. Since `b3119f71`, `lazy gather {…}` skips the probe
   (`LazySeqState::declaredLazy`). Its block still runs to completion on the
   first pull.

On top of that, `map`/`grep`/`first`/`Z`/`X`/`.kv`/`.pairs` over a lazy but
finite source build their results eagerly, and a Seq can be iterated twice
(no `X::Seq::Consumed`).

### Design

**B1: the coroutine (~1 week).**

- **A stackful coroutine on the calling thread.** The gather body runs on its
  own stack; `take` switches back to the consumer, and the next pull switches
  in again.
- **Context switch:** a small routine per platform (arm64, x86-64 System V,
  Win64), in the fcontext style, about 50 lines each. `ucontext` is the
  portable fallback; macOS deprecates it, and `swapcontext` makes a signal-mask
  syscall on every switch.
- **Interpreter state:** the per-thread state (`tctx_`, an `ExecContext`) is
  saved and restored on each switch with the `saveCtx`/`loadCtx` pair the GIL
  handoffs already use (`yieldToWorker`).
- **Stacks:** the main interpreter runs on a 1 GiB stack (`onBigStack` in
  Runtime.cpp), and the recursion guard reads the real stack size. Coroutine
  stacks must be reserved with `mmap` and committed lazily, pooled for reuse,
  and the guard must learn a coroutine's bounds.
- **Exceptions:** C++ exceptions must not unwind across stacks. Catch
  everything at the coroutine's entry, switch out, and rethrow in the
  consumer. The same goes for loop control from inside the body, including
  `last`/`next` aimed at a loop outside the gather.
- **What goes away:** the probe, the re-run and the snapshot/restore
  (`gatherWritesStmt`, `gatherDeepCopy`).
- **Keep:** `take`, `take-rw`, the gather frame stack (`pushGatherFrame`,
  `gatherTake`), `StopGatherEx`'s role, and the died-probe behaviour. A body
  that dies raises when it is pulled, not when the gather is written.
- **First version is thread-affine:** a coroutine resumes only on the thread
  that created it. Moving it to another thread is a later question.
- **Backends:** the `--exe` codegen has its own probe-and-double copy
  (Codegen.cpp, the `gather` arm). The JS backend (`gatherExpr` in
  src/codegen/Js.cpp) can use native JS generators.

**B2: lazy drivers (~2–4 days).** `map`, `grep`, `first`, `Z`, `X`, `X~`,
`.kv`, `.pairs` over a source that says `.is-lazy` (declared-lazy or a
suspended gather) pull one element at a time, as they already do over infinite
ranges. `for gather { … } { last }` pulls once.

**B3: Seq consumption and the iterator protocol (a few days).** A Seq is
consumed once (`X::Seq::Consumed`); `.cache` makes it re-readable;
`.count-only` and `.bool-only` answer without consuming; `sink-all`,
`skip-all`, `push-all` on custom iterators; `squish` calls its `:as` and
`:with` callbacks lazily. [semantics/List-Array.md](../findings/semantics/List-Array.md)
(LA-28, LA-29) notes this needs a per-Seq flag shared across copies, plus an
audit of every internal re-read of a Seq.

**Risk:** medium. Stack-switching bugs crash instead of misbehaving. Run the
gather tests under ASan once, and under TSan for the `start`/`hyper` files.
**Perf:** expected to help gather-heavy code (no probe, no re-runs). Measure
it.

### Files

| File | Now | Failing now | Phase |
|---|---|---|---|
| S02-types/lazy-lists.t | 14/27 | 8, 10 `make-lazy-list` runs nothing when assigned or bound; 12 first, 14 grep, 16 map, 17–18 Z, 19 X, 20 X~, 24 kv, 25 pairs are lazy; 23 `for gather {…} { last }` pulls once | B1+B2 |
| S03-metaops/eager-hyper.t | 7/8 | 2 the iterator was lazy and ran the block once | B1 |
| S04-statements/gather.t | 35/39 | 1 not yet gathered; 12 gather is lazy; 15 take with several arguments makes one item each; 27 gather/take keeps sublists | B1 |
| S32-list/seq.t | 34/36 | 24 an EVAL-roundtripped Seq throws when consumed again; 11 sinking a cached Seq does not pull; `.iterator`, `.Slip`, `eqv` on consumed Seqs | B3 |
| S32-list/skip.t | 53/55 | 53 `.skip-all`/`.push-all` on slippy iterators; 54 `Seq.skip` consumes the original; `sink-all` on the Iterate* classes | B3 |
| S32-list/squish.t | 29/31 | 24, 25 `:as`/`:with` callbacks called lazily, once per element | B3 |
| S32-list/tail.t | 56/57 | 53 `tail` uses `.count-only` when implemented (its subtest runs 0 of 4) | B3 |
| S32-array/delete-adverb.t | 220/221 | 219 `:delete` on a lazy Array | B2 |
| S06-signature/slurpy-params.t | 82/86 | 74, 75 a second pass over a Seq dies; 76, 77 `+@args` turns a Seq into a List | B3 |
| 6.c/MISC/bug-coverage.t | 13/17 | `.count-only`/`.bool-only` before and after pulls; `Buf.iterator`; `with`/`andthen` thunk scoping (MISC) | B3 (+MISC) |

`S32-list/grep.t` (Track A) is also tagged LAZY by the survey. Its remaining
failure is the rw half.

---

## Track C — parametric roles as first-class objects

### The problem

A parametric role is not an object of its own. Its parameters are bound per
CLASS: `ClassInfo::roleParamBindings` (Value.h), filled by
`bindRoleParamsInto`, with `pickRoleVariant`/`pickRoleVariantArgs`
choosing among a role's variants and `roleGroupDefault()` standing in for
"the group". The consequences:

- Two instantiations of one role share multi candidates. Even a single
  `does R[Str]` fails `multi method foo(T $t)`.
- A role body is not run per parameterization, so `my T $v .= new` in a role
  body sees an unbound `T` ("No such method 'new' for invocant of type 'T'").
- Classes and packages declared inside a role are not instantiated per
  argument (Rakudo names them `R::G::A[Int]`).
- There is no role-group or curried-role object for `.^mro(:roles)`,
  `.^concretization`, `.^curried_role`, `.WHY` or export to hang on.
- A type capture (`::T`) is not usable as a type in declarations, checks and
  coercions.

### Design

- **A role group object** (the `role R[::T]` declaration, all its variants)
  and a **concretization per argument list** (`R[Int]`), each with its own
  method table and multi candidates, cached per (variant, arguments).
- **Run the role body once per concretization** with the type parameters bound
  lexically. Packages and classes declared in the body are created per
  concretization and named `R::G::A[Int]`. Their type objects answer the
  nominalizables: `T:D`, `T:U`, `T()`, `T:D()` are `Int:D`, `Int:U`,
  `Int(Any)`, `Int:D(Any)` for `T = Int`.
- **Type checks:** `X ~~ R` asks the group, which matches a class doing any
  concretization; `X ~~ R[Int]` asks the concretization. Rakudo's rules from
  `S14-roles/typecheck.t`: a role group matches a parent class of its
  NON-parameterized member, and does not match a parent class of a
  parameterized one.
- **MRO:** roles come after the class that does them, most recent `does`
  first, and a parameterized role appears as its GROUP (which is
  `.^curried_role`). `hides` / `is hidden` propagate from roles. A pun through
  `is Role` shows up as the class plus the role.
- **Type captures:** `sub f(::T $a, T $b)` binds `T` for the rest of the
  signature and the body. It works in declarations (`my T $x`), in type
  checks against a subset, and in coercions (`T()` over a capture).
- **Qualified calls:** `self.R::foo` relaxed to the group; ambiguous relaxed
  resolution throws.

**Risk:** medium-high for the ecosystem, since roles are everywhere. Gate on
the battery scan, or the stand-in listed under the rules above.

### Files

| File | Now | Failing now |
|---|---|---|
| S02-types/generics.t | 0/1 | its one subtest runs 0 of 6: `my T $v .= new` in a role body; `T:D`, `T:U`, `T()`, `T:D()` per concretization; `class A is Array[T]` inside `my package G` in the role, named `R::G::A[Int]`; typed `has @.a is G::A` checks assignment |
| S06-signature/type-capture.t | 15/17 | 7 a captured type in a declaration; 6–8 mismatch, definite, indefinite captures throw; 10 mismatch against a subset; 11, 12 coercion over a type capture; 13 coercion into a subset |
| S11-modules/export.t | 56/57 | 57 a role group is exported |
| S12-methods/qualified.t | 5/7 | 1 relaxed qualified calls to parameterized roles; 3 dispatch in a class private to a role; 4 dispatch to a parent's role; 6 parameterizations and inheritance; 7 relaxed ambiguity throws (in a class and in a role) |
| S14-roles/parameterized-type.t | 7/7, then dies | test 8: "Cannot resolve caller foo(A: Int:D)" — the correct multi from several parametric roles |
| S14-roles/parameterized-basic.t | 51/52 | 52 a role's stubbed `multi method` typed with `::?CLASS`, left unimplemented by the class, dies X::Role::Unimplemented::Multi |
| S14-roles/typecheck.t | 9/11 | 5 a role group matches a parent class of its non-parameterized member; 8 it doesn't match a parent class of a parameterized member |
| S26-documentation/why-both.t | 56/57 | 18 a role group's `.WHY` is its default candidate's |
| S12-class/mro-6e.t | 0/1 | `.^mro(:roles)`, `(:concretizations)`, `.^mro_unhidden`, `.^concretization`, `.^curried_role`; parameterized roles; a parent on a role; rolified MRO |
| 6.c/S12-class/mro-6c.t | 4/5 | 5 hidden classes (subtest runs 0 of 5) |
| S12-methods/fallback.t | 13/14 | 14 a CALL-ME mixed into a proto in a `trait_mod` is called |
| integration/role-composition-vs-attribute.t | 8/10 | 2, 6 a role's private attribute (and an embedded role's) is seen |
| S12-coercion/parameterized.t | 0/2 | coercion into parameterized types: basics, double parameterization (both subtests run 0) |

The last three are MOP/COERCE items that sit in the same code. A session may
leave them for last.

---

## After the tracks

Each track ends with its files passing, all five gates green, and this
document updated: move the track's files to a "landed" line with the commit,
and note anything left over. The remaining ~113 failing files are one-offs
for the file-at-a-time sweep and the V5-PLAN batches.
