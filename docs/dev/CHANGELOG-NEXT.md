# The next release's CHANGELOG entry, collected as work lands

*Started 2026-10-21 at `main` 3fe87868 (v5.4.1-192). This is not a CHANGELOG
entry yet: [RELEASING.md](RELEASING.md) step 2 writes that at the tag, with
the tag's own gate figures. Here is what has landed and what it measured when
it landed, in the CHANGELOG's own shape, so the entry starts from this page.
Move each item into [CHANGELOG.md](../../CHANGELOG.md) at the tag and delete
it here.*

*Covered so far: the parallel-sessions review and the seven branches that came
out of it, 1ac4abdb..3fe87868
([findings/REVIEW-PARALLEL-2026-10.md](findings/REVIEW-PARALLEL-2026-10.md)
has every item with its evidence, and the ledger of what is open). The 160
commits between v5.4.1 and 1ac4abdb are not in this page yet. Every figure is
instructions retired, base → after, unless it says otherwise; the commits and
the review doc say how each was measured.*

---

### Faster

- **Float code written with decimal literals runs on the fast tiers**
  (a508cc93). A decimal literal is a Rat, and a Rat beside a Num kept the loop
  kernel, the typed subtree and the fast shape off, so code written the way
  most people write it ran ~60× slower than the same line with `e0` literals.
  `$x = $x * 1.0000001 + 0.000000001`: 3,418 → 57 an iteration (`e0`
  literals: 58); under `--cnp` 6,410 → 57, under `--jit` 3,451 → 58.
  `$x = $x * 1.5` 2,209 → 42.
- **Standard input is read a block at a time** (dc76aa01). Every byte of
  `$*IN` was a virtual `getc` call. 32 MB, 400k ASCII lines: `$*IN.lines` and
  `lines()` 40.0G → 2.90G, `-n` 44.1G → 7.0G, a `$*IN.get` loop 45.5G →
  8.5G, `$*IN.slurp` 57.4G → 0.19G (`.IO.lines` of the same file is 4.6G;
  Rakudo reads it from stdin in 3.5G). 27 MB of Latin, Greek, Cyrillic and
  CJK: `$*IN.lines` 43.4G → 11.9G.
- **An `our` costs about what a `my` does** (432a3398, 62b8949f).
  `for ^400_000 { our $n += 1 }` 3.35G → 0.93G and the `-n` idiom
  `-ne 'our $n += .chars; END say $n'` over 400k lines 7.09G → 4.74G, each
  over the two commits, as each measured its own step.
- **Int `div`, `mod`, `%%`, `**`, `gcd`, `lcm`, `min` and `max` on the hot
  path** (a508cc93, 4c78cce6). With the kernels off, `div` 2,745 → 1,144 and
  `**` 3,949 → 1,131 an iteration; the loop and sub kernels take `gcd`
  (2,382 → 81) and `lcm` (2,403 → 89).
- **`1/3` is a small Rat built once** (a508cc93). Int / Int went through
  BigInt division and gcd: with the kernels off `$r += 1/3` 7,836 → 3,562 and
  `my $q = 1/3` in a loop 9,599 → 2,549.
- **A loop kernel's variable may hold a Bool** (a8c61b84). The flag loop
  `$found = True if $i == 500_000` 1,010 → 47 an iteration, `$b = !$b`
  1,346 → 47.
- **BIND-POS of a variable binds its container** (cce315ef). It stored an
  alias that kept the variable's whole frame alive and looked the name up on
  every read. A heap in BinaryHeap 2.0's shape, 2^16 pushes and pops: 47.7G →
  36.5G, peak RSS 200 → 100 MB. Rakudo 2026.09 is still ~3.5× faster on it in
  wall time ([ecosystem/BINARYHEAP.md](ecosystem/BINARYHEAP.md)).
- **`m:c($pos)` and `:pos` over ASCII text are linear in practice**
  (cce315ef): a 5,000-token tokenizer 29.9G → 0.48G. Over non-ASCII text it is
  faster but still quadratic (ledger O7).
- **`.contains`, `.comb` and `.split` with a regex use the pattern cache**
  (cce315ef): `.split(/\s+/)` 100k times 4.4G → 2.3G.

### Fixed: silent wrong answers

- **`×` `÷` `−` `≤` `≥` `≠` are operators of their own** (a968d622). The lexer
  turned them into `*` `/` `-` `<=` `>=` `!=`, so a `multi infix:<×>(V, V)` was
  never consulted (`V.new × V.new` answered the built-in product, `$v ×= …`
  0), a `my &infix:<×>` captured every `*` in its scope (and `$a * $b` in its
  own body recursed until the stack was gone), and `×=` ignored it. A node now
  carries the spelling it was written with.
- **Compiled code calls the program's own operators** (65713c7f, 6fd22b55).
  `--exe` reaches a plain `sub infix:<->`, a `my &infix:<*>`, an
  `&infix:<+>` parameter, a lexical sub and a module's export, and
  `--target=js` calls user operators at all; both ask by the written spelling
  since the merge with a968d622. A comparison chain asks the user's operator
  for every link (`1 <= 2 <= 3` over a user `infix:«<=»`).
- **Text decoded from a file, `$*IN`, a pipe or a socket is NFC** (1918e400,
  5e474d7d), as Rakudo's decoders make it: `e\x[301]` read from a file was 3
  codes, not `eq` to `"\x[e9]"`, and missed as a hash key.
- **Characters, not codepoints or bytes** (1918e400, 5e474d7d, cce315ef):
  `.split('')`; `<space>` and `<cntrl>` take a CR LF whole; sprintf's `%s`
  width and precision; `Match.raku`'s offsets; `.indices` and
  `.contains(/rx/)` positions; `.getc` and `.readchars`; `.trans` with a
  Regex key after a CR LF or a combining mark; a bare `m:c` after non-ASCII
  text (it found nothing).
- **A sub has its own `$_` on every call** (cce315ef). From its second call
  on, a sub with a signature runs in a faster lane that defined none, so
  `sub p($v) { $_ = $v }` wrote the caller's `$_`, and an `s///` in such a sub
  rewrote the caller's topic. An `EVAL` in the body, and a `$_` in a
  parameter's default, read the caller's too.
- **A `.wrap` on a closure's routine is honoured by the integer kernels**
  (cce315ef). Closures of one body share a compiled kernel, which called the
  first closure's routines: a second closure's wrapped `my sub g` was skipped.
- **`div`, `mod`, `gcd`, `lcm` and `+&` `+|` `+^` over a Num of 2**63 or more**
  (cce315ef): `1e20 div 7` was 1317624576693539401, `1e20 +& 255` 255,
  `1e20 gcd 10` 1.
- **Objects meet their own operators and order the same way every run**
  (6b080a07, a0fd6a69, d0a85b2b). `Z+`, `X+`, `R+` and `[+]` consult a user
  `multi infix:<+>` and numify through `.Numeric` (`[+] W, W, W` was 0); so do
  `.sum` and a junction's eigenstates. `cmp`, `sort`, `min` and `max` order
  objects as Rakudo does, where they went by address and changed from run to
  run.
- **`our` is one container, its package's** (432a3398, 8174a3c3, cff43a59,
  77741ce6, 9288492c). An `our` in a loop body or a sub lost its value on
  re-entry; a method's `our $k` read GLOBAL's rather than its class's; a typed
  or defaulted `our` lost its type and default when re-run; a module's `my`s
  and unexported `our`s became the importing program's variables;
  `GLOBAL::<$x>:exists` and `GLOBAL::{…}` answered from the wrong table.
- **A Rat that spills to Num is the nearest double** (477bf8d0): one spill in
  four came out an ulp off.
- **`--target=js` binds a sigilless parameter as one argument** (ab34f1e6):
  `sub f(\b) { b² }` was 0.
- **A core enum member or a type name before an infix is a term** (c92b7789):
  `Less < More, More > Same` and `C < 2, 3 > 2` are two comparisons, and
  `Less R+ More` reverses the `+`.
- **A binary handle refuses text reads, and a socket's `.recv($n)` counts
  characters** (f01c2ef2).
- **`@a.BIND-POS($i, $v)` keeps `$v`'s container after `$v := $w`**
  (cce315ef): the element followed the name.

### Fixed: crashes

- **`our` declarations on worker threads** (6ba94541): the five programs in
  t/regression/our-on-worker-threads.raku died in 50–100% of their runs.
- **`1 +< $n` and `1 +> $n` with `$n` = -2**63** (cce315ef) recursed until
  SIGBUS; Rakudo answers 1, and so do we.
- **`-2**63 div -1` and `-2**63 % -1` in the code `--exe` and `--jit` share**
  (cce315ef) were undefined behaviour in C++ (SIGFPE on x86-64).

### Left open

The ledger in
[findings/REVIEW-PARALLEL-2026-10.md](findings/REVIEW-PARALLEL-2026-10.md),
ranked by what a user sees. The ones a release note may want to name: text
against Perl is still far behind (`.trans` 94×, output 19×, regex captures
14×; [plans/TEXT-PLAN.md](plans/TEXT-PLAN.md)); `m:c` over non-ASCII text is
still quadratic (O7); a compiled program's `~` joins do not renormalize (O37,
older than this round); `subcall` costs +1.7% for the `$_` fix (O25).

### Gates at 3fe87868 (not the release's)

Against a build of 1ac4abdb: t/run.raku 1744/1744 (1709 at the base; the 35
new checks are this round's 35 new regression files); Roast the same 1424 fully passing
files, 220,845 of 220,846 declared assertions, with `S17-procasync/stress.t`
timing out under load on both and `S07-hyperrace/basics.t` losing one
assertion in one run (it passed 33 reruns, and the stdin and `our` branches
saw the same flake on their own binaries); t/jit/run.raku and t/exe/run.raku
no worse than the base (their only differences are tests that print a timing
inside an `ok` line, and two compiled-code failures the base has too);
perf-guard's 24 kernels within noise of the base in wall time on a loaded
box; the four that read slowest there (`objnew`, `mainnext`, `substrloop`,
`subcall`) and `method` and `fib`, re-measured in instructions, within
−1.0%…+0.2% of it but for `subcall` (+1.7%, O25).
The JS output was not run: no `node` on the machine.
