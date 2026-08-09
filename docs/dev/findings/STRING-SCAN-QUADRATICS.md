# Per-character string ops, and what the ASCII fast path actually cost

Found 9 August 2026, working the v3.0.1 release, by asking why JSON::Fast
needed 13.9 s to parse a 421 KB document that Rakudo parses in 51 ms. §1–§3
shipped in v3.0.1. §5, §5a and §6 were the follow-up pass later the same day,
which closed the two gaps v3.0.1 left open (the uncached `.substr`/`.index`
sites, and a perf gate that could not see string work at all) and took another
22% off the parse. Nothing here is open now except what §4 and §6 agree is
architecture.

The reason it is worth writing down is that the defect was **invisible in the
shape of the code**. Every site looked like an optimisation, was commented as
an optimisation, and was one — for a single call. What none of them survived
was being called once per character, which is exactly how a tokenizer written
in Raku uses them.

## 1. The pattern — FIXED (v3.0.1)

Raku string positions are grapheme positions. rakupp stores a Str as UTF-8
bytes, so an op like `nqp::ordat($text, $pos)` has to establish that a byte
index is a codepoint index before it can index bytes. Each op did that by
scanning:

```cpp
size_t run = asciiRun(s, (size_t)i + 1);      // O(i), every call
if (run == (size_t)i + 1 || run == s.size())
    return Value::integer((unsigned char)s[i]);
```

That is O(position) per character examined, so scanning a string of length n
costs O(n²). The scan is cheap per byte — eight bytes a word — which is
precisely why it survived review: the constant is small and the complexity is
wrong.

The fix is not a faster scan but *not repeating it*. Both properties are pure
functions of immutable text, so with strings promoted to a shared immutable
body (`StrBody` in [Value.h](../../../src/Value.h)) the answer is cached on
the body and computed once per string:

- `cowAllAscii` — every byte < 0x80, so a byte index is a codepoint index
- `cowByteIsGraphemeIndex` — the above and no CR (CR LF is the one ASCII
  sequence that clusters, GB3), so a byte index is a *grapheme* index
- `cowGraphemeCount` — `.chars`

Sites converted: `nqp::ordat`, `eqat`, `substr`, `chars`, `index`,
`findnotcclass`, `iscclass`, and `.chars`/`rtBChars`.

**`findnotcclass` was the expensive one and was found last.** JSON::Fast calls
it once per string token to find the end of a word-character run; it called
`allAscii(s)` on the *whole document* each time. 12,001 calls over a 208 KB
document is 2.5 GB of scanning — on its own still quadratic, after the first
four sites had been fixed and the parse already looked linear-ish. It was only
caught by asking why the measured gap to Rakudo was bigger than the dispatch
cost explained.

The lesson worth keeping: **fixing four of seven sites of a quadratic leaves a
quadratic.** The scaling table, not the improvement factor, is what tells you
whether you are done — 255 → 777 → 3,245 → 13,216 ms is ×4 per doubling, and
102 → 190 → 381 → 764 ms is ×2.

## 2. Strings were copied by value — FIXED (v3.0.1)

Underneath the scans, `Value` held its Str as a `std::string` **by value**, and
`Value` is copied on every argument pass, operand evaluation and list element.
A long string was memcpy'd once per operation — the same O(length)-per-op
shape, from a different direction. Measured, 20k operations on a 200 KB string:

| | before | after |
|---|---:|---:|
| pass the string to a sub, no work done | 142 ms | 24 ms |
| `nqp::ordat` on a global | 79 ms | 11 ms |

`CowStr` keeps short strings inline (std::string's own small-buffer
optimisation already makes those copies free) and promotes from 64 bytes up
into a shared body, so copying is a refcount bump. Promotion is **eager**, at
construction, not lazy-on-first-copy: rakupp runs work in parallel, and a lazy
promotion would have to mutate the source from a const copy constructor, which
two threads copying the same Value would race on. Eager promotion means a
const `CowStr` is never written to.

Cost: `sizeof(Value)` went 376 → 392 bytes. The perf gate passed and Roast came
out marginally up, so it is paid for, but it is a real 4% tax on every Value.

## 3. `nqp::strtocodes` normalized ASCII — FIXED (v3.0.1)

It ran a full `uniNormalize` pass for the requested form even when the text was
pure ASCII, which every normalization form leaves unchanged. 4 µs per call
against Rakudo's ~0, once per escaped string. Now gated on `cowAllAscii`, and
the target array is `reserve`d.

## 4. What is left, and why it is not a bug — OPEN

After all of the above, rakupp is ~15× slower than Rakudo on this workload.
That residue is the architecture, measured directly rather than assumed:

| | per loop iteration | per AST node / op |
|---|---:|---:|
| rakupp | 0.46 µs | ~115 ns |
| Rakudo | 0.02 µs | ~5 ns |

MoarVM compiles to bytecode and JITs hot loops; rakupp walks the AST, so every
`nqp::push_i` is a node visit, a switch dispatch, argument evaluation into a
`ValueList` and a `Value` construction. ~23× is what that costs, and it
accounts for the whole remaining gap. Closing it is a bytecode VM, not an
optimisation, and nothing in this file should be read as suggesting otherwise.

One hypothesis tested and **rejected**: that the 392-byte `Value` was the cost,
since a native `Uni` buffer is a `std::vector<Value>` where MoarVM uses a
packed int32 array. Measured, a loop doing `nqp::push_i` costs the same as the
same loop doing no array work at all (0.45 vs 0.47 µs/iter) — the vector
amortises growth and the fat struct is noise next to dispatch. A packed
native-int array representation is still worth having for memory, but it is not
where the time goes.

## 4a. What the speedup exposed: `Supply.wait` never blocked — FIXED (v3.0.1)

Worth recording because of *how* it was found. Re-measuring the module battery
after the work above, `Log::Async` dropped from 15/17 to 13/17, and
`t/13-remove-tap.rakutest` printed the memorable

```
# expected: ["one", "three"]
#      got: ["one", "three"]
```

Two identical gists comparing unequal reads as an equality bug, and that was
the first (wrong) diagnosis. It was a race: the list was still being filled.
Log::Async's `done` is

```raku
method done() {
    start { sleep 0.1; $.source.done };
    $.source.Supply.wait;
}
```

and `Supply.wait` returned `True` immediately for *every* Supply — the
`m == "wait"` case sat next to `done`/`close`/`quit` in the trivially-true
list. Measured: 0 ms against Rakudo's 317 ms on a supplier completed after
300 ms. So any code using `wait` as a barrier was never synchronised, and
whether it worked came down to interpreter speed. The old binary won that race
in this test; a faster main thread lost it.

A live Supply (`$supplier.Supply`) carries `["supplier"]`, and `done` records
`done_state` under the supplier's stripe, so `wait` now polls that under the
same lock with `sleepYield` between checks (which releases the GIL, so the
emitter can actually run). `quit` records `quit_state` for the same reason —
otherwise a quit supply would block `wait` forever.

Results: `wait` blocks 304 ms where Rakudo blocks 317; Log::Async goes to
17/17, better than the 15/17 it managed before any of this work — which lifts
the distribution bar from 47/59 to 48/59; and Roast gained a fully-passing
file (593 -> 594, S17 +1). The lesson is that a
performance change is a **concurrency** change: it reorders every race in the
system, and the module battery caught what the unit suites did not.

Two things left open here, neither blocking:

- **`wait` does not rethrow on quit.** Rakudo throws the quit exception out of
  `wait`; we return `True`. Not hanging was the important part; matching the
  throw is a small, separate change that was not worth making under release
  pressure.
- **`Log::Async` `t/14-frame.rakutest` is flaky in parallel mode** — 6/6, 5/6,
  4/6 or 1/6 across runs, on the *old* binary as well, and stable under
  `RAKUPP_GIL=1`. Pre-existing, unrelated to the above, and looks like
  `callframe` under worker threads.

## 5. Where else this pattern lives — the `.substr`/`.index` sites, FIXED (2026-08-09)

The suspicion recorded here was right, and understated. `MethodCallPart3.cpp`
did call `byteIsGraphemeIndex` uncached at the `substr` and `index`/`rindex`
sites — but each of those lines was preceded by an `inv.toStr()`, which **copies
the whole invocant**. So the per-call cost was O(length) twice over, on the
methods a scanning loop calls per character.

Measured with the ×4 test this file recommends — 5,000 `.substr($n, 1)` calls
against strings of 50 KB, 100 KB, 200 KB and 400 KB:

| string length | before | after |
|---|---:|---:|
| 50 KB | 29 ms | 5 ms |
| 100 KB | 44 ms | 5 ms |
| 200 KB | 74 ms | 5 ms |
| 400 KB | 135 ms | 5 ms |

The tell is not the ratio but the shape: the *same* 5,000 operations cost more
on a longer string, which is the signature of per-call O(length) work. After the
fix the column is flat, which is what "the string's length no longer enters the
per-call cost" looks like.

Both sites now read the invocant's `CowStr` in place and take the verdict off
the shared body. Two caveats are in the code and worth repeating: `substr-rw`
keeps the snapshot copy (its write-back may reach the same storage), and a
Str-valued **enum** stringifies to its key, so `inv.s` is only interchangeable
with `inv.toStr()` when `enumName` is empty. That second one was a live bug in
the first cut of the fix.

Straight-line effect, 20,000 calls over a 200 KB string: `.substr($n,1)`
296 → 22 ms, `.index` 359 → 91 ms.

## 5a. The guard could not see any of this — FIXED (2026-08-09)

v3.0.1 left a note that the perf-guard baseline was v1.5.1's and had no string
kernel, "which is why a change of this size in the string representation could
pass it unmeasured". That was exactly right, and it is why §5 survived a
release: all four kernels were Int-and-Array work, so a build could make string
operations six times slower and the gate would call it green.

Three kernels added, each guarding a distinct thing:

| kernel | what it catches | v3.0.1 | now |
|---|---|---:|---:|
| `strscan` | per-call O(length) work in a string method | 2883.0 ms | 221.6 ms |
| `strpass` | a regression in CowStr promotion/sharing | 184.3 ms | 153.8 ms |
| `subcall` | per-call work that belongs on the AST | 375.3 ms | 281.1 ms |

Their first baseline is the number measured the day they landed, not the last
release's — recording v3.0.1's `strscan` would have left the gate open to
readmitting the very regression it was added to stop. The reasoning is written
into `tools/perf-baseline.raku` beside the numbers.

The general check, cheap to run against any candidate: time the operation over
inputs of n, 2n, 4n and 8n. A ×2 per doubling is fine; a ×4 is this bug.

Every program used to measure anything in this file is kept, with the procedure
and the two ways it is easy to fool yourself, in
[tools/bench/diagnose/](../../../tools/bench/diagnose/README.md).

## 6. The per-call and per-op tax — REDUCED (2026-08-09)

§4 concluded that the residual gap to Rakudo is architecture. That is still true
of most of it, but "most" was doing more work in that sentence than it should
have: profiling the same JSON::Fast parse again found ordinary waste worth 22%,
none of it in the parser and none of it needing an architecture change.

The pattern, four times over, was **a static property of the AST recomputed on
every call**. The codebase already had the tool for this (`DecidedOnce<T>`, and
the comment in `hoistExprDecls` describing precisely this bug) — these sites had
simply not been looked at:

- **The binder's fast path excluded `is rw`.** The only thing the slow path did
  differently for an rw scalar was leave `readonly` clear, but excluding it sent
  every `(str $t, int $p is rw)` signature down a path that allocates a
  `ValueList`, a `std::map` and a `std::set` before binding anything. That
  signature is `nom-ws`, called 73,603 times in a 278 KB parse. Whether a
  signature qualifies is now decided once and kept on the parameter list.
- **`Value::natWidthOfType` ran per typed parameter per call** — and it
  `substr`s the type name, so it *allocated* each time. Cached on the `Param`.
- **`typeCheckBind` did four keyed lookups per bind** (two hashes, a prefix
  chain, a 17-element `std::set`) to decide whether the type name was
  enforceable. Cached, but only the positive answer: a name that is unresolvable
  now can resolve later, so the negative must not stick.
- **The arity pre-check and the inline-`CATCH` scan** both walked the signature
  and the whole statement list per call. Both are static; both are now decided
  once on the `Callable`.
- **`hoistExprDecls` built two `std::function`s per call** — two heap
  allocations before any walking — on exactly the bodies where its existing
  cache cannot short-circuit. Self-recursive lambdas instead.

And one that was not a cached-property bug but the same shape of waste:

- **Every nqp op built a fresh `ValueList` for its arguments**, so an nqp-heavy
  program paid a malloc and a free per op — roughly 1.5M of each on a 278 KB
  document. The buffers now come from a per-thread, depth-indexed pool on
  `ExecContext` (a `std::deque`, because an argument's own evaluation re-enters
  the function and a growing `vector` would reallocate under the outer frame's
  live reference). Contents are still cleared on exit, so argument lifetimes are
  unchanged. Allocation fell from 23.6% of the profile to 14.0%.

Measured end to end, `from-json` on the same 278 KB document: **597 → 464 ms**,
against Rakudo's 34 ms. So the gap closes from ~17.6× to ~13.6×, and §4's claim
survives in a narrower form: what is left really is the AST walk. The top line
of the profile is now `Value`'s own copy constructor and destructor at ~10%,
which is `sizeof(Value) == 392` and not something an optimisation removes.

Two candidates measured and NOT taken, recorded so they are not re-proposed
without numbers: LTO on the runtime (slower than `-O2`, and 10× the link time),
and blaming `Value`'s size for array work (§4's rejected hypothesis still
holds — the fat struct is noise next to dispatch *there*; it is the copying on
the argument path that shows up, not the size in containers).
