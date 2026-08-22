\part{The Speed Campaign}

# Constant Factors

On 2026-08-21 the fastest way to run the `hashfill` kernel built here was to
compile it to a native binary — and that binary, at 113 ms of wall clock, still
lost to the `perl` binary's 82 ms. Our own interpreter was 3.3× behind `perl`
on the same program. Two days later the interpreter led Rakudo on all ten
benchmark kernels, the native binary led `perl` 2.8× on the kernel that started
it, and `sizeof(Value)` had fallen from 344 bytes to 128. This chapter is the
story of those two days: four batches of work, each one an application of the
same two decisions, and each one measured under Chapter 39's rules before it
was believed.

Nothing in the campaign is an algorithm. Every batch attacks a *constant
factor* — the cost per variable read, per hash probe, per value copied, per
assignment executed — and the campaign's tools are the ones this book has
already described: the census before the change, the interleaved A/B with a
control kernel, the falsifier written into the plan, and Roast as the veto.

## The trigger, and the method

The trigger was external: a remark that a Raku program ran "twice slower than
Perl 5". The report named no program, so one was built to probe it —
`hashfill`, a twin pair of files, Raku and Perl, identical line for line. The
Raku side (`tools/bench/hashfill.raku`):

```raku
my %h;
for 1 .. 200_000 -> $i {
    %h{"key$i"} = $i * 2;
}
my $sum = 0;
for %h.values -> $v { $sum += $v }
my $s = '';
for 1 .. 50_000 { $s ~= 'x' }
say $sum, ' ', $s.chars;
```

And the Perl twin, the same work with byte-identical output:

```perl
my %h;
for my $i (1 .. 200_000) {
    $h{"key$i"} = $i * 2;
}
my $sum = 0;
$sum += $_ for values %h;
my $s = '';
$s .= 'x' for 1 .. 50_000;
print "$sum ", length($s), "\n";
```

It is a kernel made of exactly what a scripting language is asked to do all
day — interpolated hash keys, a values sweep, a built-up string — and `perl`
won it in every mode we had.

The response was not to guess. It was to read the Perl 5 sources — five files: `sv.h`,
`hv.h`, `pad.h`, `run.c`, `pp_hot.c` — and write down what thirty years of
interpreter maintenance had settled on, as a ranked findings document —
`docs/dev/findings/engines/PERL5-TECHNIQUES.md` in the repository.
Chapter 41 tells how that one document grew into a nine-engine survey. This
chapter follows the items that got applied, in the order they landed.

The loop each batch ran:

1. **Measure first.** A profile or a census names the cost; the plan document
   records a prediction and a falsifier — what result would prove the idea
   wrong.
2. **Change one layer.**
3. **Interleave the A/B** on the same machine, same day, with control kernels
   that the change cannot touch.
4. **Gate on Roast.** A per-file diff against the pre-change run; jitter is
   distinguished from regression by re-running the flapping file, not by
   optimism.
5. **Re-measure the public numbers** and update BENCHMARKS.md the same day.

## The recurring kernels

Two harnesses supply the names this chapter keeps using. `perf-guard`
(Chapter 39's regression gate) times interpreter-only one-liners; the
`tools/bench` set times full programs in every mode, against Rakudo and — for
`hashfill` — against `perl`. Four of the guard kernels are short enough to
show whole:

```raku
# fib
sub fib($n) { $n < 2 ?? $n !! fib($n-1) + fib($n-2) }; say fib(29);
# asg
my $x = 0; for ^2_000_000 { $x = $x + 1 }; say $x;
# loopsum
my $t = 0; for 1 .. 1_000_000 { $t += $_ }; say $t;
# hash
my %c; for 1 .. 100_000 { %c{$_ % 1_000}++ }; say %c.elems;
```

The rest, in a line each:

| kernel | what the program does |
|---|---|
| `strscan` | per-character `.substr` over a 200 KB string, 200k calls — catches any op that re-scans or copies the invocant per call |
| `strpass` | a 200 KB string passed to a sub 200k times — catches lost `CowStr` sharing (a memcpy per call when broken) |
| `subcall` | a typed `is rw` signature called 200k times — catches per-call binder work that belongs on the AST |
| `strcat` | build a string by repeated `~=` |
| `streq` | a million `eq` comparisons in a hot condition |
| `regex` | one simple pattern matched many times |
| `sortnums` | sort 50,000 pseudo-random integers |
| `arrayops` | a `grep`/`map`/`sum` pipeline over a range |
| `bigint` | factorial(5000) as a running product — machine words overflow almost immediately |

The guard kernels were chosen adversarially — each stresses the path a batch
is most likely to slow down — and `strscan`/`strpass`/`subcall` exist
precisely because an earlier release changed the string representation and
the then-current guard, all Int-and-Array work, could not see it.

## Batch one: the hash payload

The hash payload behind every `%h` was `std::map<std::string, Value>` — a
red-black tree. Every lookup walked O(log n) nodes doing a full string
comparison at each, every node carried a then-344-byte `Value`, and the
`hashfill` profile showed the cost plainly as `memcmp` samples under
`rtIndexRef`.

Perl has not paid that price since the nineties: a key's hash is computed
once and stored beside it, lookups compare the stored hash before they touch
a single byte, and the table is an open-addressed array. `ValueHash`
(src/ValueHash.h) is that design under two contracts the interpreter already
relied on:

- **Reference stability.** Autovivification hands out `Value&` into the
  payload and keeps it across further inserts. Entries therefore live in a
  deque — `push_back` never moves elements — and deletion only marks. A
  churning hash carries tombstones; that is the price of stable references,
  and Perl pays the same one with its lazy deletes.
- **Iteration shape.** The interpreter iterates `pair<const string, Value>`
  in insertion order, skipping the dead.

The subtlety was not the table; it was the *ordering audit*. `std::map` had
been silently sorting, and rendering sites got sorted `.keys` for free. The
audit came to exactly three test files, and two of them were gifts: a pair of
assertions that had only ever passed by coincidence, and one real latent bug —
`Mix.total` summed fractional weights in a `double`, so its rounding depended
on iteration order. It now sums exactly through the numeric tower, which is
also Rakudo's answer. A data-structure swap found an arithmetic bug; that is
what the ordering audit was for.

| kernel | `std::map` | `ValueHash` | |
|---|---:|---:|---:|
| hashfill, interp (user) | 0.26 s | 0.21 s | −19% |
| hashfill, `--exe -O3` (wall) | 0.103 s | 0.078 s | −24% |
| 500k lookups, 100k-key hash | 0.41 s | 0.33 s | −20% |
| bench `hash` kernel | 0.06 s | 0.04 s | −30% |

The compiled binary crossed `perl` on `hashfill` with this batch — 78 ms
against `perl`'s 82.

## Batch two: the census and the cold block

Chapter 8 ends by admitting that `Value`'s size "is the thing to attack first
if the design is ever revisited". The revisit began with a census, not a
redesign: a build flag (`RAKUPP_PTR_CENSUS`) that made every `Value`
destructor record which of its eleven pointer fields were actually set. The
corpus was chosen to be broad on purpose — the local test suite plus a
feature-targeted Roast slice, some 30 million destructions — and pointedly
*not* the benchmark kernels, which would have been a blinkered witness
(`hashfill` barely creates the fields in question). The verdict:

| live pointer fields | destructions |
|---|---:|
| none | 25,583,692 |
| `arr` | 4,268,636 |
| `big` | 539,483 |
| `ratN ratD` | 466,950 |
| `obj` | 181,374 |
| `code` | 67,662 |

25.6 million of 30 million values carried none of the eleven pointers — and
the half-million BigInts and Rat pairs are as much a part of the design as
the zeros, because they are the values the change could have hurt.

The top row licensed the cold block: `im`, the `BigInt` slots, the Rat pair,
the shape vector, the range fields, `ofType` and friends — about 148 inline
bytes — moved behind one lazily allocated, copy-on-write
`shared_ptr<ValueExt>`. Reads go through an accessor that never allocates
(`xr()`, returning a shared empty block at namespace scope — a
function-local static's thread-safe guard, run 256 times per regex byteset
build, had already cost one 28% regression and taught that lesson); writes
clone a shared block first. `sizeof(Value)`: 344 → 208.

The second half applied the same census to the five payload pointers — `arr`,
`hash`, `code`, `pairVal`, `obj`. They were mutually exclusive on every live
value except the Match family, which legitimately carries positionals, nameds
and `.made` at once. So the five collapsed into **one** slot plus a kind
byte, and Match got a combined body behind the same slot. The kind byte is
the design point worth keeping: a handful of sites convert a value in place —
tag first, payload after — and a self-describing slot keeps that legal
without proving a global invariant over the whole interpreter.
`sizeof(Value)`: 208 → 128.

One census blind spot surfaced, and it is the honest footnote to the method:
`is default` containers carried their element default in `pairVal`
*alongside* a payload — a co-occurrence the census never sampled, because the
programs it ran never did it. The census bounds what typical programs do,
not what the language allows; the field moved to the cold block and the
lesson moved here.

| effect (batches together) | |
|---|---:|
| sortnums | −26% then −17% |
| arrayops | −20% then −14% |
| hashfill (interp) | −15% then −13% |
| hashfill (`--exe`) | — then −20% |
| JSON::Fast interpreted parse | −8% then −13% |
| hashfill peak RSS | −39% then −32% |

A plain Int copy, eleven pointer checks and a string construction in August's
first week, is now two pointer checks and an inline `CowStr`.

Who pays in the new layout, and how that was checked. A value that *uses* a
cold field pays one small allocation when the block is first written and one
pointer hop per read — while every **copy** of it drops from a 344-byte
memcpy to a 128-byte one with fewer refcount touches. Copying is what the
profile said dominates, so even the penalised types should net flat or
ahead — and the suite contains the adversarial case to test that: `bigint`,
whose every hot value keeps its payload behind the block, improved with the
rest (the −3…−13% band) rather than regressing, and the grammar parse —
Match values genuinely carry payloads — measured flat. The wins were also
broadest *away* from `hashfill` (sortnums −26% against hashfill's −15%),
which is the distribution you expect when a change targets the
representation rather than a benchmark. The residual exposure is narrow and
nameable: a program that mass-creates short-lived Rats or Ranges and reads
each once pays the allocation without harvesting the copy savings. No
current kernel isolates that shape — which is an argument for adding one to
the guard, not for the old layout.

## Batch three: pads, and the lever the falsifier exposed

Until this batch every variable lived in an `unordered_map<string, Value>`
on an `Env`, and every read hashed the name at each level of the parent
chain. Perl resolves a `my` to an integer offset at compile time; a read is
one indexed load. The port is not literal, because `Env` does three jobs at
once — lexical scope, closure environment (a block captures the
`shared_ptr<Env>` chain), and the dynamic-lookup spine for `CALLER::` and
`$*dyn` — and any pad design has to keep all three working through the same
object.

The design that landed: a `PadLayout` per pad *owner*, keyed by **body**
rather than by `Callable` (`.assuming` wrappers share the AST body, and slot
annotations live on shared AST nodes, so slot assignment must be a function
of the body alone); a fixed-size `pad` vector on owning frames; and a
liveness bitmask, because a slot exists from frame entry but must answer
lookups only after its `my` executed — declaration is an event in the
scope's timeline, and the mask is that event made cheap.

The bug of the batch deserves its page. The first design tracked the current
pad frame in a register, maintained RAII-style by `run()` and the call path.
It survived every targeted edge test — shadowing, closures, EVAL, `temp`,
recursion — and then the Forth showcase said "stack underflow". The minimal
reproduction:

```raku
sub f($n is rw, $d) { $d > 0 ?? f($n, $d - 1) !! ($n = 42) }
```

The `is rw` write-through evaluates the caller's argument expression after
re-pointing the current scope to the caller — but not the tracked pad
register. In a recursive sub, the caller's `$n` is the *same AST node* as the
callee's; its annotation validated against the innermost frame's layout, and
the write landed on the wrong activation. The interpreter has about 109
places that temporarily re-point the current scope; every one was this bug
waiting. The fix was to **stop tracking and start deriving**: the pad frame
is the nearest layout-carrying ancestor of the current scope, computed at
use. A register is a claim about global state that every call site must
maintain; a derivation is a per-use proof from state the site already holds.
Both RAII guards were deleted, and all 109 sites became correct at once.

Then the plan's own falsifier fired. The prediction was at least 10% on the
assignment kernels from pads alone; the first A/B measured `asg` *flat*. A
`sample` of the loop answered better than theory: the remaining samples were
not in name lookup at all — they were in the per-iteration topic insert and
the iteration-scope machinery. Pads had already removed the lookups; the
kernel's cost was the scope itself, two million times. That is Perl's other
`foreach` lesson: the loop variable aliases one pad cell for the whole loop.
The equivalent here: when a static scan proves the body cannot define
anything into the iteration scope, the loop keeps one scope and overwrites
the topic in place — and capture safety stays dynamic, because a closure
taking the scope bumps its use count and the next iteration forks it.

| kernel | pre | after | |
|---|---:|---:|---:|
| perf-guard asg | 545.1 ms | 406.7 ms | −25% |
| perf-guard loopsum | 226.5 ms | 149.5 ms | −34% |
| perf-guard hash | 34.0 ms | 26.0 ms | −24% |
| perf-guard fib | 575.1 ms | 546.0 ms | −5% |
| bench hashfill (interp) | 184.0 ms | 155.2 ms | −16% |
| bench strcat | 17.2 ms | 13.1 ms | −24% |

The pads infrastructure was not wasted by the falsifier — the flat-loop
lever lands *on top of it*, and the next batch builds on the same frame.

## Batch four: the price of an assignment

With lookups and scopes cheap, the while-shaped kernels named the next cost:
the store. Six one-line loop bodies priced an interpreter iteration's
anatomy, 2 million iterations each, best of three:

| loop body | ns/iter | the increment buys |
|---|---:|---|
| `{ }` | 65 | the loop floor: body dispatch, safe point, topic |
| `{ $x }` | 102 | +37 — one pad read, with the sink copy |
| `{ $x = 1 }` | 173 | +108 — the assignment ceremony for a constant store |
| `{ $x = $x + 1 }` | 201 | +28 — the specialised add itself |
| `{ $x += 1 }` | 127 | the compound path — proof a leaner lane exists |
| `{ $p = $p + 4; $n = $n + 1 }` (while) | 389 | condition eval plus two full assigns |

The arithmetic costs 28 ns; the act of *storing* its result costs 108. The
assignment ceremony cost four times the arithmetic inside it.

Perl's answer is TARG: every op owns a preallocated result slot in the pad.
Translated literally that is wrong for a value-returning tree-walk — but the
diagnosis translates perfectly, and it became a decided-once **simple-assign
lane**: a store into a slot that was declared untyped and unconstrained skips
the full container ceremony. The eligibility rule carries a lesson of its
own: a typed `my Int $x` leaves *no mark on its slot's Value* — the
constraint lives in the scope's default table — so the lane's licence had to
be a per-slot bit computed from the declaration at layout-build time, not
anything inspectable at the store site.

The follow-up slices each carry a moral in miniature:

- **`op=` joined the lane.** The general compound path allocated a substring
  of the operator name and ran a cascade of string compares — on every
  `+=`. The lane classifies the operator once and stores an op class on the
  node: the cost of *re-deriving per evaluation*, removed by deciding once.
- **Native ints stopped bailing** on a rule read out of the engine itself:
  only uppercase type names register an assignment check, so a lowercase
  declared type is lane-eligible *by the engine's own definition* — parity
  by construction beats parity by testing.
- **The binder stopped string-matching types.** Its fast-accept caches what
  is a property of the name (its accept class) and re-checks per call only
  what can change underneath (whether a user subset now shadows the name —
  two hash counts). Fast-accept only, never fast-reject, so every error
  message stays byte-identical with the full matcher's.
- **Conditions answer as bool** — with the first iteration deliberately
  taking the slow path, so Chapter 19's shape verdict is decided by the code
  that owns it and the fast path only ever reads it. Layers stay in their
  lanes.

Combined: subcall −19.5%, strpass −19.5%, strscan −12%, loopsum −8%, the
`my int` while-shape −45% in isolation; a closing slice gave parameters the
same slot annotations variables have, worth another −4% on fib and subcall.

## Where it landed

The re-measured standing on 2026-08-22, same machine as every earlier
revision of BENCHMARKS.md. The interpreter against Rakudo, all ten kernels:

| kernel | interp | Rakudo | faster |
|---|---:|---:|---|
| strcat | 9.7 ms | 181.1 ms | 18.7× |
| hash | 18.2 ms | 223.0 ms | 12.3× |
| bigint | 31.5 ms | 251.8 ms | 8.0× |
| sortnums | 34.1 ms | 249.4 ms | 7.3× |
| regex | 39.7 ms | 278.9 ms | 7.0× |
| arrayops | 64.9 ms | 280.6 ms | 4.3× |
| hashfill | 112.9 ms | 455.4 ms | 4.0× |
| loopsum | 107.6 ms | 259.7 ms | 2.4× |
| fib | 394.1 ms | 453.6 ms | 1.2× |
| streq | 248.4 ms | 284.3 ms | 1.1× |

`streq` was the last holdout and crossed with the assignment lane; `fib` had
crossed the same day. Both had been Rakudo's for the whole life of the
benchmarks file, both are tiny-body kernels where a JIT should be at its
best, and both margins are thin — the file reads them as "level, our side",
which is the honest reading. Compiling widens everything again: `--exe` puts
`fib` 5.3× ahead and `streq` 13.9×.

Against `perl`, on the kernel that started the campaign:

| engine | hashfill |
|---|---:|
| Raku++ `--exe` | 37.4 ms |
| Perl 5 | 104.0 ms |
| Raku++ interp | 112.9 ms |
| Rakudo | 455.4 ms |

The interpreter, 3.3× behind `perl` two days earlier, is within 10% of it.
(The absolute numbers differ from the batch-one day's — 78 against 82 —
because this harness times all four engines in one run, startup included,
best of six; the batch-one figures were warm wall-clock. Ratios, not
milliseconds, are the comparable thing across the chapter.)

And the costs are stated with the wins: startup grew 0.3–0.6 ms — laying
out a pad is a fixed per-process cost — and long-lived churning hashes
carry tombstones. Both were prices worth paying; neither is hidden.

And the leftover list is part of the result. The loop floor and the
per-node `eval` return protocol are untouched — they are the opening
argument for a threaded execution loop, which is a design document away, not
a weekend. The 128-byte `Value` still carries its `CowStr` inline; the
head/body endgame is deliberately coupled to the container refactor. The
campaign stopped where the next step stopped being a constant factor and
started being an architecture.

## The two decisions

Every batch above is one of two decisions, made at a new layer:

1. **Pay per compile, not per use.** Slot numbers instead of name hashes;
   op classes instead of string cascades; accept classes instead of type
   matching; stored key hashes instead of re-hashed probes.
2. **Stop carrying per value what only some values need.** The cold block;
   the payload slot; tags as interned handles; the topic written in place.

Perl's speed is mostly these two decisions applied everywhere for thirty
years. The campaign compressed the applying; the deciding had been done for
us, and reading it cost two days. What else the other engines had already
decided — and which of it this codebase had independently decided the same
way — is the next chapter.
