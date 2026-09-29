# Dispatch probes — what the tree-walker pays per node, and what each fix buys

*Measured 2026-09-29 on the benchmark machine (M3, Darwin 27.0.0, Apple clang,
Release `build-arm64`), base binary `5.0.1-14-geb48f46c`. One finding became an
engine change, `Block::entryWork`, described at the end of section 3. The question: where
could a 1.2–2× come from at the DISPATCH level (not `--cnp`, not calls), for
five candidate ideas — (1) closure compilation, (2) quickening, (3) a borrowed
result protocol, (4) superinstructions, (5) per-node checks.*

**Reproduce:**

```bash
# the ladder (one core, ~10 s)
c++ -std=c++20 -O2 -DNDEBUG -w -Isrc -Iinclude tools/dispatch-probe.cpp \
    build-arm64/librakupp_{rt,parse,ucd_names,ucd_coll,ucd_props}.a -liconv -o /tmp/dp && /tmp/dp 7
# engine binaries, interleaved (base first; each later one gets a % against it)
build-arm64/rakupp tools/dispatch-probe/ab.raku --reps=7 BASE VARIANT...
```

The engine variants in section 3 were temporary compile-time switches in
Interpreter.cpp, measured and then removed. What each one skipped is described
in its row, so it can be rebuilt as a patch.

Kernels (`tools/dispatch-probe/kernels.raku`, each in its own sub):
k1 `while $i < $n { $s = $s + $i; $i = $i + 1 }`, k2 `$x = ($x * 31 + $i) % 1000003`,
k3 `$s = $s + @a[$j]`, k4 a method call per iteration, k5 `fib(27)`.

---

## 1. The engine today: half the time is not dispatch

| kernel | engine | L1 lean switch, same `Value` protocol |
|---|---:|---:|
| k1 | 150 ns/iter | 71 |
| k2 | 224 | 104 |
| k3 | 243 | 91 |

L1 is the leanest possible walker that still returns a `Value` by value from
every node and dispatches every operator through `applyArith(std::string)`.
The engine pays **2–2.7×** that. `sample` on k1 says where. The fractions
below are of the main thread's samples. A further 2,181 samples were idle
worker threads waiting on a lock (`__ulock_wait`), and are left out.

| share | what |
|---:|---|
| **~19%** | thread-local storage: `__tls_init` + `_tlv_get_addr`, i.e. `tctx_` accesses — from runLoopBody, the phaser runners, evalBinary's operand lambda, evalAssign, tryCondBool, exec |
| **~11%** | per-iteration block entry/exit on a body with no phasers: `runLeavePhasers`, `runEnterPhasers`, `hoistSubs`, `isBlockPhaser` |
| ~4% | `___chkstk_darwin` — stack frames over a page (applyArith, evalBinary, exec, eval) |
| ~4% | `std::string` copies/compares (`Value::operator=` copying a string field, memcmp, strlen) |
| ~4% | `applyArith(std::string)` dispatch |

Those four overhead rows add up to about 40% of the loop, before any change
to how nodes are dispatched.

## 2. The ladder (C++ probe, real `Value` + the engine's rt helpers)

ns per iteration, best of 7, interleaved; every rung reproduces the checksum.

| kernel | L1 switch | L2 quickened | L3 closures | L4 borrowed | L5 fused | C++ int64 |
|---|---:|---:|---:|---:|---:|---:|
| k1 | 71.0 | 55.1 | 37.4 | 32.2 | 9.0 | 0.27 |
| k2 | 103.8 | 74.3 | 47.1 | 54.3 | 25.4 | 3.45 |
| k3 | 91.2 | 71.0 | 54.1 | 49.0 | 20.2 | 0.41 |

- **L1 → L2, quickening (item 2): −22…−29%.** The node kind is rewritten once
  to a per-operator kind; the case calls `rtAdd`/`rtLtB` instead of the string
  dispatch.
- **L2 → L3, closure compilation (item 1): −24…−37%.** L2 is already specialised
  per operator, so this step is the switch alone: one giant frame against one
  small frame per handler. This is the largest single dispatch-level step.
- **L3 → L4, borrowed results (item 3): −14% k1, −9% k3, but +15% k2.** Reading
  a variable operand by reference wins. Handing every computed operand a
  scratch `Value` loses: two 128-byte temporaries per node cost more than the
  copies they replace. Borrow the LEAVES only.
- **L4 → L5, superinstructions (item 4): 2.1–3.6×.** A binary node whose operands
  are variables or literals runs an int64 lane with the overflow check and
  stores an Int in place. This is the UNBOX/`--cnp` idea applied per node rather than per loop,
  and by far the biggest rung.

## 3. Engine variants (interleaved A/B, best of 7)

| variant | k1 | k2 | k3 | k4 | k5 |
|---|---:|---:|---:|---:|---:|
| QUICKEN — plain pad read skips the special-name ladder; plain binary op skips evalBinary's metaop ladder and uses the inline single-char applyArith | −1.6% | −2.8% | +0.4% | −2.3% | −2.6% |
| NOCHECKS — no `pendingSubscripts_` scan, no ParStripe on a read (item 5 as first posed) | −0.1% | −1.2% | −1.9% | −3.4% | −0.6% |
| QUICKEN + NOCHECKS | −3.1% | −3.5% | +0.2% | −3.2% | −2.8% |
| **FLATBLOCK** — a loop body already proved flat (`Block::flatLoop`) skips the CATCH/sub/decl scans and the ENTER/LEAVE runners | **−10.5%** | **−9.0%** | **−10.6%** | **−6.9%** | — |

The two checks named in item 5 are noise-level. The overheads the profile
found are the ones that matter. FLATBLOCK is a probe: it skips
`runLeavePhasers`' `leaveReturned` reset, and it relies on the flat verdict
covering temps, which a real change must prove.

**TLS, priced alone** (C++ probe, one non-inlined access):
`thread_local ExecContext` as today 1.86 ns; a trivial `thread_local` pointer
1.33 ns; an `ExecContext&` passed in 0.80 ns. At ~19% of k1's samples, passing
or caching the context in the hot functions is worth an estimated **~10%**.
That estimate is not measured in the engine: `tctx_` has 1,288 uses in
Interpreter.cpp alone.

### Taken to the engine: `Block::entryWork`

The FLATBLOCK probe became a real change on the same branch. It is not tied
to loop flatness, and it applies to every block. `blockEntryWork` scans a
block's OWN statements once and caches five bits: 1 = a CATCH/CONTROL,
2 = PRE/ENTER/FIRST, 4 = LEAVE/KEEP/UNDO/POST, 8 = a named sub or type to
hoist, and 16 = the last statement might not be the block's value.
`execBlock` skips each scan or runner whose bit is clear.

- Bit 16 is set wider than the scan it gates, for any trailing phaser block,
  CATCH or named sub, because `isBlockPhaser` answers an INIT from run-time
  state.
- An exit with no LEAVE-family phaser still does the two things
  `runLeavePhasers` did without one: it resets `leaveReturned` and drains the
  block's `temp` restores. `drainTempRestores` is now shared by both paths.
- `hoistExprDecls` is no longer called for a block whose `hoistNeed` is
  already 0. The function checked that cache itself, but only after it had
  been called.

Interleaved, best of 9, against the base binary; FLATBLOCK is the probe:

| kernel | base | entryWork | FLATBLOCK |
|---|---:|---:|---:|
| k1 | 298.1 ms | **−11.0%** | −11.9% |
| k2 | 438.2 | **−9.9%** | −8.8% |
| k3 | 462.4 | **−11.1%** | −9.6% |
| k4 | 255.3 | **−6.1%** | −6.2% |
| k5 | 148.2 | −1.6% | −0.1% |

Roast unchanged: 1,424 of 1,424 files, 218,420 assertions without skip/todo.
raku-corpus (1,812 diffable programs, base vs this build): 1,696 MATCH on
both, and the same verdict for every program except two that are
nondeterministic on both builds (`bool.pl` prints `Bool.pick`; `channel9.pl`
is thread timing). Module battery (the t/ suites of all 59 dists, 438 files,
sandboxed, base vs this build): identical per file in exit code, `ok` count
and `not ok` count. 296 files pass on both; the 142 that do not fail the same
way on both.
A `temp`/LEAVE/KEEP/UNDO/CATCH/trailing-sub/ENTER-value program prints the same
on this build, the base, and Rakudo. Routine bodies (`fib`) are not covered:
they run their phasers from the call path on a statement vector, not
through `execBlock`.

## 4. What this says about 1.2–2×

- **~1.2× is available without changing the architecture**, from the overheads
  the profile found: the flat-body skip (measured −7…−11%), passing the context
  instead of reading TLS (~10%, estimated), and trimming the page-sized frames
  and string copies (~4% each at most). Each is local and gated by existing
  decided-once facts.
- **2× needs the ladder's two big steps in the engine.** Closure compilation of
  the hot node kinds, with the `switch` kept as the fallback, is the L2 → L3
  step. Typed leaf superinstructions with in-place Int stores are L4 → L5.
  Quickening (L1 → L2) is the natural first half of that same work: it decides
  per node which handler to install.
- **Calls do not move.** k4 and k5 change by 3–7% under every variant. Their
  cost is in call setup (DISPATCH-PERF-PLAN), not node dispatch.

## 5. Side finding: a `given`/`when` block loses the pads

The same k1 loop in the mainline or in a sub: 300 ms. Inside `given $k { when
'k1' { … } }`: **989 ms, 3.3×**. The variables declared in the `when` block
apparently fall back to map lookups (PADS-PLAN v1 owners are the mainline and
routine bodies). Not investigated further here.
