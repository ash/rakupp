# The next release's CHANGELOG entry, collected as work lands

*Emptied at the v5.5.0 tag: everything collected here is in
[CHANGELOG.md](../../CHANGELOG.md). This is not a CHANGELOG entry yet:
[RELEASING.md](RELEASING.md) step 2 writes that at the tag, with the tag's
own gate figures. Add each item as it lands, in the CHANGELOG's own shape —
what changed, what it measured and how, with the commit — so the entry starts
from this page. Move each item into CHANGELOG.md at the tag and delete it
here.*

*Covered so far: d7c6d9d5, 3e97932e and e30e2827 (FLOAT-HEAVY-ISSUES);
e24f05f1 (NUM-UNBOX-PLAN P0); 8644846a; 74806a6d; 93f9a4af and c7a12943;
9d48b3b5, 184c423c, 72e0c5fb and 316e5855 (FatRoast); 07775af3
(FORTRAN-LESSONS 1a); e614993a; de2e08d4; c95d9332; d0ba733b
(FORTRAN-LESSONS 1b); 5e5d80ec; 4405241a (FORTRAN-LESSONS 2); 769ed705;
85d56001; 960e8aed; 7fcca8a8; 273863ac; 90bc6e23; 84add3b1; 08a17d21;
12af490d (RACE-DETECTOR-PLAN P0-P4); 42f79e98, b9108e1d, 0b9950b1, 500c29e2,
1b182b8e, 828e52d5, 4d1c7cc3, 80ea9030, d972abe7, 166334a9 and 29b10450;
5d31d4a2 and aff5032b (issue #202); 5d0e7ce9 and 170802d4 (issue #203);
cb7933f3 (issue #204); cb5b360a, d5184116 and 50aebf54.*

---

### Faster

From docs/dev/findings/FLOAT-HEAVY-ISSUES-2026-10-22.md, issues 9, 11, 16, 18,
19 and 20. Figures are ns per call of that file's probe scripts, best of 3,
against a build of v5.5.0 in the same sitting, `--exe -O` unless they say
interpreted.

- **A lexical `my sub` is called directly** (d7c6d9d5). Every call, its own
  recursive ones included, found `&name` in the runtime env and went through
  callCallable; now a call site in its scope reaches the body through a weak
  handle, and takes the old path once that scope has gone. A recursion of
  2**10 leaves 210.6 → 42.5 µs (a top-level sub: 39.9).
- **Native types compile at the untyped code's speed** (d7c6d9d5). `+ - * /` on
  natives have a helper per operator instead of rtNativeArith's dispatch on
  the operator's text, a typed parameter that already conforms is not moved
  or looked up, and a native store compares no type names: `(num $a, num $b
  --> num)` 29.1 → 10.0 (untyped 9.7), `(Num $a, Num $b --> Num)` 22.3 →
  9.7, `num < num` 19.5 → 6.2, `(int $m --> int)` 10.6 → 5.5, a `num` float
  loop 28.5 → 9.5 (untyped 8.7).
- **`.elems`, `.defined` and a one-argument `.push` are answered at the call
  site** (d7c6d9d5), by the same tests methodCall's own first arms make:
  `@a.elems` 24.9 → 8.0, `$x.defined` 9.7 → 6.4, `my @a`, three `.push` and
  return 89.8 → 54.1.
- **A sub with a plain `@` or `%` parameter takes the direct call**
  (d7c6d9d5): `sub (@x)` 16.8 → 3.9 (`sub ($x)`: 3.8). Summing 10 Nums with
  `for @a -> $x` 168.6 → 148.8, with `while` and `@a[$i]` 170.6 → 155.2.
- **Prefix `-` has a Num and machine-Int arm in both engines** (d7c6d9d5): `-$x`
  12.9 → 5.2 compiled (`0e0 - $x`: 6.5), 143.5 → 128.6 interpreted (126.9).
- **A top-level `constant` holding a machine Int is a literal to the int
  kernels** (d7c6d9d5), compiled and interpreted: two constant references in a
  sub 9.2 → 1.5 compiled, 213.9 → 5.7 interpreted (two literals: 1.7 / 5.5).
- **A sub typed `num`, `int`, `Num`, `Int` or `Str` takes the interpreter's
  lean call path** (3e97932e; issue 17), when each argument already is of its
  parameter's type, and an argument that is not goes the general way. Against
  d7c6d9d5, interpreted: `(num $a, num $b --> num)` 387.1 → 152.6 (untyped
  144.8), `(Num $a, Num $b --> Num)` 360.8 → 148.3, `num < num` 245.2 → 143.1.
- **A loop kernel takes `int` and `num` variables** (3e97932e; issue 17), at
  full width: it bails where a result leaves int64, and the native path then
  wraps. Interpreted, the issue-15 float loops 1,024 → 292 and 2,332 → 581
  (untyped 282 and 407), and perf-guard's strscan, which counts in a `my
  int`, 96.4 → 6.0 ms.
- **`for ^$n -> int $i` and `-> Int $i` run as `-> $i` does** (07775af3;
  FORTRAN-LESSONS-2026-10-15 item 1a). A typed loop variable bound each
  iteration through the general binder and reached no kernel; a lone `int`,
  `int64` or `Int` parameter over an integer Range now takes the counted
  path, the loop kernels and the `--cnp` site, keeping what the binding
  gave it (read-only, and a native `int` wraps). Against a87a6b73, best of
  3: `$s = $s + $i` over 10M, interpreted, 1.211 → 0.018 s with `-> int
  $i` (untyped 0.017); a hash-increment body no kernel takes, 1M, 0.217 →
  0.023 s (untyped 0.020); the findings file's `--cnp` dot loop over 10M
  `num`s 1.860 → 0.144 s (untyped 0.143).
- **`--exe -O` gives `for ^$n -> int $i` and `-> Int $i` the unboxed loop
  lane** (c95d9332), as it gives `-> $i`; the typed loop ran boxed through
  the typed binder. `$s = $s + $i` in a sub over 10M, against a87a6b73, best
  of 3: 60.6 → 2.2 ms (untyped 2.2).
- **`--exe -O` gives a `my int` / `my num` declared in a loop header the
  unboxed loop lane** (d0ba733b; FORTRAN-LESSONS-2026-10-15 item 1b). `loop
  (my int $i = 0; …)` ran boxed, because the name outlives the loop and the
  lane's store-back lost the native's width; it now rebuilds the native, so
  `$i` after the loop still wraps. The findings file's `sub f(int $nn -->
  num)` over 10M, against a87a6b73, best of 3: 240 → 5.1 ms (untyped 5.1).
- **A packed `int` / `num` array keeps its words under a computed key**
  (5e5d80ec). `@a[$i + 1]`, read or stored, unpacked the array for good in
  the interpreter, so a `--cnp` loop entered after it read boxed Values. The
  key is still evaluated once.
- **`--cnp` reads and writes a packed array's word with no call** (4405241a;
  FORTRAN-LESSONS-2026-10-15 item 2), its fixed facts checked once as the
  register is loaded. Against a87a6b73, best of 3, 10M iterations: the
  findings file's dot loop 140.9 → 58.6 ms, one read 80.7 → 45.7 ms (no
  read: 42.2), axpy 178.5 → 64.5 ms, a three-point stencil over 1M 103.8 →
  33.9 ms, and its flat 200×200 matmul 419.0 → 60.4 ms (with 5e5d80ec).
- **`my num @c = @b` copies a packed array's words** (7fcca8a8) and leaves
  @b packed; it read @b's Values, which unpacked it for good. Against
  a87a6b73, best of 3, 10M elements under `--cnp`: the findings file's
  axpy-loop, which starts that way, 403.3 → 64.7 ms, and reading @b after the
  copy 258.6 → 44.9 ms.

The next nine entries measure `--exe -O` against the build before each commit,
best of 5 interleaved, on one program of three routines (a recursion of
about 500 calls over four native parameters updating file-scope tables at
its leaves, a loop over one array, and an integer routine), its arrays
plain before 500c29e2 and native from it, unless they say otherwise.

- **A bundled program's routine that steps or stores into an outer variable
  stays native** (42f79e98). `$n++`, `$n = $n + 1` and the like under -O, and
  any store into a native outer, named a C++ local a routine of a bundled
  program has not got, and the routine ran interpreted; the recursion ran
  2.3× faster.
- **Native `%`, `div`, and a `num` stored into an `int`, are inline in
  compiled code** (42f79e98): `%` and `div` built a string from the operator
  on every call, and the store took the general conversion. An Int or Str
  subscript key makes no call. A routine of `int` locals doing integer
  arithmetic on a `num` argument: 2,659 → 1,459 instructions a call.
- **A compiled native store of a native `+ - *` skips the store's checks**
  (b9108e1d) when the value can only be a fresh number of the target's kind:
  that routine 1,459 → 1,317 instructions a call (untyped 1,294).
- **A sub whose parameters are all full-width `int` / `num` compiles to a
  plain C++ function over int64_t and double** (0b9950b1), beside its Value
  function; a call of another such sub is a direct C++ call. A statement
  whose native operation could answer other than a number keeps the boxed
  form. The recursion 110.3 → 59.8 µs a call (plain `--exe` 220.5 → 64.0),
  the integer routine 134.5 → 75.0 ns; untyped code retires the same
  instructions.
- **`my num @a` / `my int @a` compile under `--exe`** (500c29e2): a typed `@`
  declaration bundled the whole program with the interpreter. A full-width
  native array is built and filled as the interpreter does (one routine for
  both engines now), keeps its words, and an element read or store uses them
  while the array is packed. The recursion over native arrays 371.7 → 33.3 µs
  a call, the loop 6.64 → 1.07 µs, the integer routine 580.8 → 72.0 ns; the
  binary 29.91G → 3.06G instructions. A `num @x` parameter still bundles.
  Every compiled store into an element (`=`, `op=`, `++`) converts, wraps or
  refuses as the interpreter's does (80ea9030): until then one the word could
  not take went to the element's place, which took a Str, a Rat, a Failure or
  an Int too wide, kept a Bool, and did not wrap a step.
- **A native routine reads and writes a native array's element as its word**
  (1b182b8e), and a `/` with a Num on either side divides as a double, behind
  the runtime tests that make that the answer (the array packed, the index in
  range, a divisor not zero); otherwise the statement runs boxed, as before.
  A routine ending in a loop gets its native function. The recursion 33.5 →
  14.9 µs a call, the loop 1.10 → 0.64 µs; the binary 3.06G → 1.48G
  instructions. The same program over plain arrays, and untyped, retire the
  same instructions as before.
- **A native routine's read of a file-scope native and its element tests are
  inline, and it makes no `$_` it does not read** (828e52d5): the read's
  conversion sat in the same function as its test, which clang called out of
  line, and every call built and destroyed the routine's own `$_`. The binary
  1.500G → 1.225G instructions.
- **A native loop checks its arrays' facts once, on entry** (4d1c7cc3): what
  an element's test asked at every access (packed, of its kind, no default,
  no worker running) is asked as the loop starts and again after anything
  boxed or a native call runs in it, and an element's test is its index
  against the length kept. While a worker runs, the per-access tests answer,
  as before. The recursion 12,892 → 8,604 ns a call, the binary 1.215G →
  0.851G instructions.
- **A top-level `loop` or `while` over native arrays, or calling a native
  routine, compiles as a native routine's loop does** (d972abe7): native
  locals for its header's `my int`, the facts on entry, direct calls. A
  statement in it that runs boxed may declare nothing, and a program with a
  CATCH, LEAVE or END at its top level keeps its loops as they were. The
  loop 589 → 55.2 ns a call, the integer routine 75.0 → 14.6 ns (over plain
  arrays 58.0 → 14.2), the binary 0.853G → 0.767G instructions.

### New

- **`--detect-races` reports data races between threads** (12af490d;
  docs/dev/plans/RACE-DETECTOR-PLAN.md). `rakupp --detect-races prog.raku`,
  or `RAKUPP_RACE=1`, watches scalar variables, array elements and lengths,
  hash entries and keys, and attributes. It reports each pair of accesses
  from two threads, at least one a write, that no `await`, `Lock`,
  `Channel`, `Supply` or `⚛` orders. Each report goes to stderr once, when
  the program ends, with both sites, the threads, a backtrace of the later
  access and every name the location was reached by; a run that raced
  exits 66. It is a FastTrack happens-before detector, so it also reports
  races that gave the right answer. The flag is refused with the compilers,
  `--target=js`, `--jit` and `--cnp`, and it turns the loop kernels off.
  Detector off, the hooks cost within about ±0.7% instructions and cycles
  on the benchmarks measured. t/race-detect/ is its gate (134 checks at 20
  runs each). tools/race-sweep.raku runs files plain and under it: Roast
  S17 and four distributions reported 11 races, each one in the test, none
  the detector's own mistake
  (docs/dev/findings/RACE-SWEEP-2026-10-25.md). guide/CLI.md, "Finding data
  races", and faq/threads.md have the user's side.
- **The Jupyter kernel reads Jupyter::Kernel's cell magics** (5d0e7ce9;
  issue #203), so that kernel's notebooks run unchanged. `#% html`,
  `#% markdown` (`md`), `#% latex`, `#% latex(ENV)` and `#% js` publish the
  cell's value under that MIME type. `#% > TYPE` holds what the cell prints
  and publishes it as one display when the cell ends, in order with any
  `jupyter-display` call. `#% javascript` sends the cell to the frontend
  unevaluated. `%% bash` runs it in bash with its output streaming, and a
  non-zero exit fails the cell. `%% run FILE` evaluates FILE's code, then
  the cell's, as one cell. `%% always prepend|append|show|clear` wraps every
  Raku cell. A `#%` line that is not a magic stays a comment, and an unknown
  `%%` line is an error that lists the magics. tools/jupyter-smoke.raku has
  22 new checks, 17 of which fail on the previous binary.
  guide/integrations/JUPYTER.md, "Cell magics", has the user's side.
- **A cell value that begins with `<svg` renders as an image, and one that
  begins with `<img` as HTML** (170802d4; issue #203). This is
  Jupyter::Kernel's one guess about a value no magic typed. Only the first
  four characters are read, case aside, so any other value is text as
  before, and a cell with a type magic is never guessed. Five gate checks.
- **`--jupyter-install` warns when it replaces another kernel** (cb7933f3;
  issue #204). `raku` is Jupyter::Kernel's kernelspec name too, so the
  default install takes the Rakudo kernel's place, and it used to do so
  silently. The warning goes to stderr, under `-q` too, names the program
  the old kernel.json launched, and offers `--name=rakupp` to keep both; a
  reinstall over the same binary stays quiet. JUPYTER.md, "It replaces
  another Raku kernel", also gives
  `raku-jupyter-kernel --generate-config --force` to hand the name back.

### Fixed: silent wrong answers

- **A routine compiled in a bundled program judges a store into a native
  array's element as the interpreter does** (166334a9). Compiled on its own,
  with no view of the program's declarations, it wrote the element's place:
  a Str, a Rat or a Failure into an `int` or `num` array, and an Int into a
  `num` one, were stored, `@w[$i] += 2.5` left 2.5, a Bool stayed one, `++`
  at int64's top and an `int8` past its range did not wrap. Each module's
  routines now know its native `@` names, of any width, and a store into one
  asks the array whether it is native before taking the native path.
- **A compiled store into a native array's element through a parameter, a
  block's parameter, a `$` variable or an attribute is judged too**
  (29b10450): `sub f(@a) { @a[0] = 2.5 }` given an `int` array stored 2.5,
  and so did `$x[0] = 2.5` with `$x` holding one. Such a store asks the array
  at run time; one into an array declared plain never asks, and programs
  storing only into those compile to the same C++ as before.
- **Compiled `try` consumes a live Failure** (42f79e98), as interpreted: a block
  that returned one handed the Failure back and left `$!` unset; now `try`
  answers Nil, sets `$!` and marks the Failure handled.
- **A Failure already handled passes through `try`** (42f79e98), as its value
  with `$!` Nil, as in Rakudo 2026.09, in both engines; it was consumed like a
  live one.
- **Compiled `-$x` on 0e0 is -0** (d7c6d9d5), as interpreted and in Rakudo; it
  was `0 - $x`, which is +0. An object's `-$o` asks its `.Numeric`, not its
  `.Bridge`, as the interpreter does.
- **A List kept from a literal is written through in compiled code**
  (d7c6d9d5). `f(($a, 2))` with `sub f($x) { $x[0] = 5 }` writes $a. Three ways
  it did not: bundled for it, the program's routines were still native, and
  stored into the List's item instead of through it (a second write then died
  "Cannot modify an immutable List"); a native program handing such a List to
  a module's routine built it of values (the call now bundles the program);
  and a module routine keeping one did the same (it stays interpreted).
- **`fail` in a compiled routine returns its Failure** (d7c6d9d5). The builtin
  decides by the interpreter's routine frame, which compiled code never has,
  so every compiled sub that failed died with the exception: `q(-1) //
  "failed"` printed it and exited 1, with or without -O. In a block closure
  it is aimed at the routine's frame, as `return` is, and while a compiled
  `try` runs it still dies, so `(try q(-1)) // "caught: $!"` catches it.
- **`Num.Rat($epsilon)` walks as Rakudo's does** (d7c6d9d5): `1e-300.Rat(0)` was
  0/1 (Rakudo: 1/10**300), a fractional part below 1e-18 stopped a convergent
  early, and b/d past 2**53 was rounded twice. Where Rakudo's walk never ends
  (`7.01492021304424e-06.Rat(0)` grows Ints until memory runs out) this one
  answers the double's exact value. `.Rat(0)` is not the exact value in
  either engine: `0.1e0.Rat(0)` is 1/10, as in Rakudo, which the findings
  file had wrong. 13,222 (input, epsilon) pairs agree with Rakudo 2026.09.

- **A `$` store in compiled code resets on Nil and checks a declared type**
  (3e97932e), as `=` does interpreted: `my $r = try die "x"` left Nil (Any),
  `my @a = Nil` was `[]` (`[Any]`), `my Int $x; $x = "a"` stored the Str and
  `$x = Nil` the Nil. In a bundle, a module-mode routine's store into one of
  the interpreter's variables asks its declaration.
- **`-O` answers as plain `--exe` and the interpreter do** (3e97932e): `~` and
  an interpolation join two Strs NFC (`"e" ~ "\x[301]"` was two code points);
  `my ($a, $b) = $j` iterates the List in `$j` (it bound it to `$a`) and a
  Hash gives its pairs; `!=` between an unsigned native and a negative signed
  one is False, as in Rakudo; `my $bound := $q` is no longer compiled as a
  copy (it bundles).
- **Instant and Duration arithmetic answer Rakudo's types** (3e97932e):
  `-$duration` and `abs $duration` are Durations (were Rats), `0 - $duration`
  and `7 % $duration` Nums (were Durations), `-$instant` a Num; compiled, no
  operator kept the tag (`$d - 0` was a Rat).
- **`.push($s)` and `.unshift($s)` slip a Slip held in `$s`** (3e97932e), as
  Rakudo does; the `unshift` sub is the method (it answered the new length,
  slipped nothing and checked no element type).
- **`my ($k, $v) = %h` takes the pairs** (3e97932e); `$k` held the Hash.
- **`fail` in a block that another routine runs returns from the routine it
  was written in** (3e97932e), as a `return` does, in both engines: `sub q {
  ap({ fail "z" }); … }` returned the Failure from `ap`, and `q` sank it and
  died.
- **`f(($a, 2))` writes `$a` from a sub's second call on** (3e97932e): the
  interpreter's lean call path dropped what keeps the List's containers, and
  `$x[0] = 5` died "Cannot modify an immutable List".

- **A compiled routine applies its `--> T`** (e30e2827): `sub r(--> Int) {
  "x" }` answered "x" with or without -O, `--> Str()` did not coerce and
  `--> Int:D` passed Int. Every way out is checked, in top-level, lexical and
  anonymous subs, multi candidates, methods and pointy blocks, as the
  interpreter checks it; a value of a core type passes inline, at no cost
  measurable against the unchecked call (`--> Num` -O: 10.6 → 10.3 ns).
- **A write through a kept List into a typed variable is checked**
  (e30e2827), in both engines: `my Int $a; sub f($x) { $x[0] = "s" };
  f(($a, 2))` stored the Str, and a Nil there stayed Nil instead of
  resetting to the default.

- **A compiled loop's Num division by zero fails** (e24f05f1), as interpreted
  and in Rakudo, with X::Numeric::DivideByZero. The unboxed loop lane, on with
  `-O` and without, divided doubles and answered Inf: `while … { $t = 1e0 /
  $x }` with `$x` 0e0 returned Inf, and `$s = $s + 1e0 / $x` ran on where it
  dies. The lane bails on a zero divisor now, -0e0 and an Int 0 included.
- **A compiled loop of Nums alone leaves its lane correctly** (e24f05f1). A
  lane with no Int slot had no landing for a bail, so a Num `**` that
  underflows ended the loop with the values it held at that point: `$t = $x
  ** ($k + 1e0)` with `$x` 1e-200 answered 1e-200, where Rakudo gives an
  X::Numeric::Underflow Failure.
- **A `<name>` that calls a `my token` matches in .comb, .split and
  .contains** (8644846a). Compiled code declares its tokens where .comb did
  not look, so with -O and without `'abq xyzq aq'.comb(/ <?after <two>>
  'q' /)` (`my token two { 'ab' | 'xyz' }`) answered three q's and
  `.comb(/<two>/)` twelve empty strings. .split and .contains missed one in
  both engines: `'zzz'.contains(/<two>/)` was True, `'zz'.split(/<two>/)`
  cut between every character. Rakudo 2026.09 agrees but on the
  lookbehinds, where it matches the subrule against the reversed text.
- **A compiled regex literal is the Regex where its value is taken**
  (74806a6d), natively with -O and without, as interpreted: a Pair's key
  (`'a b c'.trans(/\s/ => '_')` changed nothing), a ternary's branch
  (`.grep(True ?? /c/ !! /a/)` kept everything, `return True ?? /b/ !! /q/`
  was Nil) and `/x/ xx 2` (two Nils); `m//` is the match (`my $m = m/c/`
  stored the Regex). A regex asked for its truth in a compiled loop matches
  the loop's topic: `for <xa yb ab> { say $_ if /a/ && /b/ }` printed xa and
  yb, judged against the program's `$_`.
- **A `for` over a Range with a BigInt end walks its real integers**
  (e614993a), interpreted and natively with -O and without. The counted loops
  walked the saturated int64 fields beside the carried ends: `for 2**64 ..
  2**64 + 2 -> $i` ran once, at 9223372036854775807 (Rakudo: three times from
  2**64), `9223372036854775806 .. 9223372036854775808` stopped one short, and
  `-2**64 - 1 .. -2**64` ran once at int64's min. `2**64 .. *` carried no
  start at all, so its .raku said 9223372036854775807..Inf; it walks lazily
  now, until its `last`.
- **`++` and `--` in a `--cnp` kernel store .succ and .pred** (de2e08d4), as
  interpreted. Anything but a machine number was stepped by `+ 1`: `my $b =
  False; $b++` counted to 3 where the interpreter and Rakudo answer True,
  `$s++` on "a" died "Cannot convert string to number", and `@flags[$i]++`
  made a Bool element an Int. A FatRat whose parts fit two words ran on the
  Rat lane, so `$fr * 3 + 0` answered a Rat; it stays boxed now.
- **A subscript's key is evaluated once** (769ed705), as in Rakudo. A key
  that was no machine Int was evaluated by evalIndex's fast arm and again by
  the general path: with `my @a = 10, 20, 30`, `@a[$i++ + 0.5]` read 20 and
  left $i at 2 (Rakudo: 10, and 1), `@a[($i++, 1)]` read (20 20), and
  `@a[key()]` called key() twice.
- **A Failure used as a subscript throws what it carries** (85d56001), as in
  Rakudo, where it indexed as the 2 its hash holds: with `my $f = 1 div 0`,
  `@a[$f] = 5` stored into @a[2], `@a[$f]:exists` was True and `%h{$f} = 5`
  made a key of the Failure's fields. Slices, junctions, `++`, `:delete`,
  native arrays and the AT-POS / *-KEY methods throw too, interpreted,
  compiled and in a `--cnp` kernel.
- **A store into a native array's element converts or refuses as Rakudo's
  does** (960e8aed). An `int` element held True, 1.5 or 2**70 as it came, an
  int8 one 300 and a uint8 one -1, `@a.push(1)` put an Int into a num array,
  and `@a[0] += 1.5` left 3.5. True is 1 now, 300 into an int8 is 44, and a
  Rat dies with the X::TypeCheck or unbox message Rakudo gives for that store
  (`=`, `+=`, a slice, push, append, splice). A store answers what it was
  handed, `(@u8[$i] = -1)` is -1, and a slice an `array[int]` of those. 103
  probes of the forms agree with Rakudo 2026.09. Bools keep a packed array
  packed: `@a[$i] = $i %% 3` under `--cnp` takes 8.1 ms over 1M, from 30.9
  on a87a6b73 (best of 3).
- **A store through an alias of a native array's element converts or
  refuses as Rakudo's does** (273863ac). `my $x := @n[1]; $x = 1.5`, `for @n
  { $_ = 1.5 }`, `given @n[0] { … }`, an `is rw` or `\a` parameter, a `.kv`
  value, `(@n[0], @n[1]) = …` and `@a[0] := @n[0]` took what they were
  given, so an int8 held 300 and an int array 1.5, and `@n[0] ||= Nil`
  stored Nil. True is 1 now, 300 into an int8 is 44, and 1.5, "7", Nil or a
  big Int dies as an unbox, as `@n[0] += 1.5` already did. A native
  variable refuses a Str as an unbox too, and `$i ||= Nil`, `$i .= Str` and
  the Any a `$` holds for Nil are refused, as Rakudo refuses them. Through
  an alias of any array's element, `$x ||= 5` now stores. An interpreted rw
  loop over a native array pays a few nanoseconds an iteration for it
  (`for @ints { }` over 2M: 69.4 → 76.9 ms against 74895cce).
- **C's `cond ? a : b` is a compile error, as in Rakudo** (93f9a4af). A lone
  `?` ended the expression and `? a` began a new statement, so
  `my $s = $n > 2 ? "many" : "few"` set `$s` to True; other shapes died of
  "Malformed radix number" or "expected )". It is now Rakudo's X::Obsolete,
  "Unsupported use of ? and : for the ternary conditional operator".
- **A store reaches the container an element is bound to** (84add3b1).
  After `@b[1] := my $c`, `@b[1] = 9` left $c as (Any): the bind looked $c up
  before the declaration made it. `$x := my $c` was the same, and
  `@b[1] := my $c = 4` died on the store. A slice (`@b[1, 2] = …`,
  `%h<a b> = …`) or list assignment (`($c, @b[1]) = …`) replaced a bound
  element instead of storing into it, and `.VAR` of one was its value's type.
  22 shapes agree with Rakudo 2026.09; a list assignment into a Proxy now
  runs its STORE.
- **A routine named after a block prefix gets its arguments** (08a17d21).
  `sub start($x) { $x + 1 }; say start(5)` said 1: the prefix handed the sub
  its thunk as a Block, and `start(2, 3)` died. `do(5)`, `try(5)`,
  `quietly(5)` and `once(5)` never called the routine, and `gather(5)` was
  empty. Glued to its paren, such a name is now the routine's call (`react`
  and `supply` too), while `start { … }` and `start 5` stay the prefix. A
  routine declared below its call counts, which also fixes `say repeat(5);
  sub repeat($x) {…}`, a parse error before.

- **A package's `our` variable reads through its Stash** (5d31d4a2; issue
  #202). `Circle::{'$pi'}` answered (Any) where `$Circle::pi` and
  `Circle::<$pi>` answered 3.14, and so did a computed key, `.AT-KEY`,
  `.pairs`, `.values` and a Stash held in a variable: `.WHO` copied the
  variable's cell holder, which reads as nothing, into the Stash. The Stash
  holds the variable's container now, as Rakudo's does, so
  `Circle::{'$pi'} = 3` assigns to `$Circle::pi`. Compiled, `Circle::.keys`
  was (): the native bareword lookup made a type object named `Circle::`
  where the interpreter answers the package's stash.
- **A module's importer takes its variables, constants and enums by tag**
  (aff5032b; issue #202). Only a routine's `is export(:tag)` was read: a
  plain `use` brought in `our $tv is export(:vt)` and `constant TK is
  export(:ct)`, `use M :vt` brought in M's DEFAULT variables, constants and
  enum members too, and `need M` its constants and enum members. A constant
  or enum a `unit module` declared without exporting it reached the
  importer all the same (`use NC; say ha`). Every declaration keeps its
  tags now (AST cache v51), a load imports what the module exported by
  tag, and `require M <C $x>` by name. An exported `our` variable is its
  package's one cell, which the importer is bound to. `import G` brings in
  G's exported variables and constants, and a `sub EXPORT` handing over a
  Stash's variable binds its container (roast's gh2979.t). The orbits and
  fourier showcases used four constants their libraries never exported;
  those are `is export` now, and both showcases run under Rakudo too.

### Fixed: crashes

- **A worker thread's teardown no longer races the cycle collector**
  (e30e2827). A worker gave its count back at the end of its body, before its
  closure, ExecContext and frame pools were let go, and a collection on the
  main thread could walk containers that teardown was freeing: Roast's
  S07-hyperrace/basics.t segfaulted about one run in six. 300 runs, none.
- **`-O` compiles a `--> Int` sub of no parameters that is an int kernel**
  (e30e2827); its kernel guard named an `__a` its direct overload has not got.
- **Compiled `$failure.handled = True` works** (42f79e98); it died "Target is
  not assignable".
- **Workers auto-extending one array by index no longer realloc its buffer
  at once** (90bc6e23). Four workers storing `@r[$w * 10_000 + $_]` into an
  empty array died 8/8 in parallel mode (SIGABRT, SIGBUS, SIGSEGV); the
  growth now takes push's stripe, and t/stress/ub-indexed-grow.raku stages it.
  2/40 still crash: see Left open.
- **Compound assignment to a shared container no longer crashes**
  (12af490d). `$s ~= "a"` from two threads appended into one buffer at once,
  a double free on every run at 3,000 appends each. The other `OP=` arms
  computed from the slot and stored back unstriped: a Rat or a big Int
  `+=`, `,=`, `~= $obj`, a typed or subset variable, `Z~=`. Every arm now
  computes from the slot or a copy taken under its stripe and stores under
  it, and the generated accessor's fast reads take the stripe too. Of 48
  shapes, each run by two threads on one container, 33 crashed before and
  none do now. t/stress/ub-opassign.raku.
- **`push @a, …`, `pop @a` and `shift @a` from two threads no longer crash**
  (12af490d). The sub forms changed the array in place without the stripe
  the methods take: 3 runs in 6 crashed. t/stress/ub-array-push.raku has a
  sub-form phase, which crashed every run before.

### Fixed: what FatRoast found

FatRoast's fattened Roast (strings-v1.mixed, names-v1.mixed, ops-v1; Roast
da425eb92) through tools/run-fatroast.raku, rakupp against Rakudo 2026.09
run the same way. Every fattened file Rakudo passes now passes here; fully
passing, before → after (Rakudo): strings-v1 537 → 539 (507), names-v1 933 →
951 (933), ops-v1 428 → 428 (418; infinity.t gained, one S17 file lost to
the 10 s limit under -j load in each run). What is left fails on Rakudo too:
the v1 transforms broke it, and their v2 (the last paragraph) does not.

- **A variable named beyond ASCII interpolates into a pattern and a
  replacement** (9d48b3b5): `/$é/`, `/@𝐯/`, `/"$ч"/`, `<$甲>`, `/$*ж/`,
  `s/q/$ч/`, `%ü<k>` and `@é[1]` in a replacement. Each scanner read `$é` as
  `$` and a literal `é`.
- **A twigil takes a name in any script** (9d48b3b5): `has $.ч` made no
  accessor, `$!ч` was "Two terms in a row", `$*ч` not assignable, `$^ч` and
  `$:ч` routines.
- **A type named beyond ASCII is a type to the parser's guesses** (9d48b3b5):
  `Éx:D` was the call `Éx(:D)`; `Ро[Int]`, `Éx .new`, `new Еx:` and
  `handles Бэк` likewise.
- **`万` and the other CJK ideographs with a numeric value are letters**
  (9d48b3b5), as in Rakudo: `my class 万 {}` was "Undefined routine 'class'" and
  `sub 万` the number 10000.
- **A name that starts with a quote word and goes on beyond ASCII is that
  name** (9d48b3b5): `class Q̲x` made every later `Q { … }` a type name, and
  `Q̲x.^name` a Q quote; `?&е` was the infix `?&`.
- **`.samecase`, `s:ii` and `:ignoremark` work by grapheme in any script**
  (9d48b3b5): a combining mark took a pattern position, `s:ii` was ASCII-only,
  `m:m/ w̲x /` never matched and `:i:m` folded ASCII only.
- **EVAL refuses an undeclared variable before an undeclared routine**
  (9d48b3b5), as Rakudo does: `EVAL '{ my $v = 1 }; nope $v'` is X::Undeclared.
- **`my Int $x = ∞`'s exception carries Inf** (9d48b3b5), and says so; `.value`
  was Any.
- **`/:my $x = 1; a/` compiles** (9d48b3b5); a leading `:my` was taken for an
  adverb and the `;` refused.
- **`--highlight` reads the Unicode quotes, hyper operators, word lists
  after a listop and in `<<…>>`/`«…»`, the Whatever `*`, ranges, shifts,
  `q(…)` calls, a routine named `s`, `:sym<…>` token names and radix
  numbers** (184c423c). FatRoast places its edits by these spans: `“…”`,
  `‘…’` and `｢…｣` were no strings, `is <a b>` no word list, `>>+<<` began a
  `< … >` word list that swapped strings and code to the end of the line,
  and a `"…"` ended at a quote inside its `{…}` block. Fattened again with
  this build, the v1 files `rakupp -c` refuses while their plain form
  compiles go 61 → 55, 49 → 48 and 8 → 7; FatRoast's v2 transforms bring
  each to 0.
- **`val('NaN+Inf\i')`, `val('Inf\i')` and `val('NaN\i')` are ComplexStr**
  (72e0c5fb), as in Rakudo; they were Str, refused for carrying no digit. And
  a word-list word reads Inf and NaN as val() does: `<0+NaNi>` was a Complex
  where Rakudo keeps a Str.
- **A Complex and a plain Str order as strings** (72e0c5fb): `"abc" cmp
  <0+0i>` was Same; and `1 before <1+0i>` follows its own `cmp`.
- **`IntStr.new` and the other allomorph constructors check what they bind**
  (72e0c5fb): `IntStr.new("x", "y")` made an IntStr of two Strs, where Rakudo
  dies.
- **`.indices` reads a Cool number as its Str** (72e0c5fb): `422.indices(2)`
  was "No such method". Roast's S32-str/indices.t died there and had been
  counted passing; see the next entry.
- **run-roast counts a run that printed no plan as no pass** (316e5855): a file
  that died before its `done-testing`, every test so far passing, was
  PASS. Raku++'s S32-str/indices.t was a false `[PASS] 18/18` in every run of
  Roast, and Rakudo's fattened S32-str/val.t `[PASS] 93/93`.

FatRoast's v2 transforms (strings-v2, names-v2, ops-v2, made in the FatRoast
repo beside these fixes) leave alone what v1 broke on every engine. Fully
passing under the corrected harness, Raku++ / Rakudo 2026.09, v1 → v2:
strings 538 / 507 of 1413 → 675 / 648 of 1414; names 951 / 933 of 1181 →
1022 / 1010 of 1166; ops 429 / 418 of 446 → 422 / 420 of 422. Plain Roast:
1419 / 1396 of 1419. In none of the seven does Rakudo pass a file Raku++
does not.

### Behaves like Rakudo

- **An undeclared routine in the main program is a compile-time report with
  its line** (c7a12943): `say length("Riga")` printed a run-time "Undefined
  routine 'length'" at line 0, and now prints Rakudo's ===SORRY!===
  "Undeclared routine: length used at line 1. Did you mean 'elems', 'chars',
  'codes'?", every such name at once, with Rakudo's fixed hints (`local`,
  `foreach`, `quit`, a phaser in lower case, …). `len` is no longer "Did you
  mean 'leg'?".
- **A source read live but finite is not lazy** (12af490d). `Channel.list`,
  a handle's `.lines` (standard input included), a process's output and a
  `Seq.from-loop` with a condition answered `.is-lazy` True, and an Array
  assigned from one stayed lazy; Rakudo says False and reads them to the
  end. A `for` still walks them one item at a time, and `lazy` still
  declares one lazy. t/regression/live-finite-sources-not-lazy.raku agrees
  with Rakudo 2026.09.

- **A module that exports nothing has no EXPORT package** (5d31d4a2; issue
  #202). Every loaded module named `Mod::EXPORT`, so `Circle::.keys` was
  ($pi EXPORT) where Rakudo says ($pi). Only a unit that declares something
  `is export` (a routine, variable, constant, type, enum or subset) or an
  `EXPORT::` package of its own has one now; a subset's `is export` is
  recorded for it (AST cache v50).
  t/regression/issue-202-stash-our-variable-lookup.raku agrees with Rakudo
  2026.09.
- **EXPORT holds every exported symbol, under each of its tags** (aff5032b;
  issue #202). `Mod::EXPORT::DEFAULT` and `::ALL` listed only routines, a
  tagged routine sat under DEFAULT, and no tag package of its own existed.
  A symbol is in each tag it names (DEFAULT when none) and ALL now, the tag
  packages are listed in `Mod::EXPORT`, and a variable's entry is its
  container. A package declared in the program, a class, and every package
  around an exporting one have an EXPORT, as Rakudo installs a symbol in
  the EXPORT of each package it is declared within.
- **A package's Stash lists its subsets and modules, and `Foo::<$x>` takes
  every adverb** (aff5032b; issue #202). `Foo::<$x>:p` was the value and a
  Pair `p => True`; `:k`, `:v`, `:kv`, `:!p`, a slice and `:delete` work as
  on `Foo::{…}`, and `:delete` removes the symbol from the package
  (`M::<$x>:delete` was a parse error).
  t/regression/export-tags-and-package-stashes.raku agrees with Rakudo
  2026.09.
- **A file required by its path is that file, and its `sub MAIN` runs**
  (cb5b360a). `require "/abs/Build.pm6"` loaded nothing and said nothing:
  the path was looked up as a module name. `require "path"` and `require
  Stub:file(path)` now load the file named (an absolute path as it stands, a
  relative one under each search-path directory) and run its `sub MAIN` on
  @*ARGS after its mainline, once; a `use` of it still does not, and a
  missing absolute path is X::NotFoundInRepository. `rakupp install` runs a
  `Build.*` hook with zef's own command, which requires it by path, so a hook
  that is only a `sub MAIN` script runs (Timezones::ZoneInfo's died with
  "Undeclared name 'Build'").
- **`:4m, :5s` is two pairs** (cb5b360a): after the digits of a `:4m` pair
  the lexer opened an `m,…,` match (`rx,` too), "Malformed radix number".
- **`samewith` works in a routine that is not a multi** (cb5b360a), calling
  the routine it is written in again with the new arguments, as Rakudo
  does; it died with X::NoDispatcher.
  Timezones::ZoneInfo 0.5.0 now builds, passes its suite and installs. The
  three t/regression files (require-path-loads-file-runs-main,
  colonpair-digits-regex-key, samewith-only-routine) agree with Rakudo
  2026.09.
- **A module may extend the core DateTime in place, and `rakupp install
  DateTime::Timezones` installs** (d5184116). `CORE::DateTime` is the
  setting's DateTime, as a term and as a parameter type, whatever the scope
  has made of the short name; `class DateTime is DateTime` in a module is a
  subclass of the core one (only `class A is A`, with nothing outside, is
  refused); `.wrap` of a core method's object
  (`DateTime.^find_method('in-timezone').wrap(…)`) wraps it for every
  DateTime, where it was accepted and ignored, and `callsame` reaches the
  core method; a sub called with a method call's invocant first (a
  `.^add_fallback` method, `$d.&up`) takes the invocant's container, so an
  `is rw` parameter assigns the caller's variable and a multi picks its
  `is rw` candidate only for one (`3.&m`, `my \d = DateTime.new(…)` take the
  other). A class deriving DateTime keeps its class through `±` a Duration
  (`$dt - $day` was a Num), and through `.later`, `.in-timezone`, `.clone`
  and kin; a difference is a Duration, `$dt + 60` an Instant, and
  `DateTime but R` keeps its role. DateTime::Timezones 0.4.2 installs with
  its suite green, and a program using it prints what Rakudo prints from the
  same store. t/regression/core-class-extended-in-place.raku agrees with
  Rakudo 2026.09.
- **What was found beside it** (50aebf54), each the same on v5.5.0 before.
  An engine object kept in a hash (a DateTime, Date, Promise, Channel, Lock,
  …) is ONE element: `my @a = DateTime.new(0)` held eight Pairs, a Lock
  none, and `.elems` was 8; a Set, Bag or Mix in a `$` parameter or loop
  variable is one item. A DateTime against a plain number is its Instant:
  `$d + 60` is Instant:70 (it moved the date), `$d * 2` 20 (it was 0), `$d
  == 10` True; a Duration still moves it. `sub f(CORE::Int $x)` checks its
  argument, at compile time too, and its `.signature` is `:(Int $x)`
  (Param.typeIsSetting, AST cache v52). `&?ROUTINE` is the routine a block is
  written in, after it returns and from a `start` block as well (it named
  whichever routine ran the block), and `samewith` calls that one. `my \x =
  @a.first`, `$x := @a.tail`, `for @a.head(2) { $_ = 7 }` and
  `@a.grep(…).first` reach the elements' own containers. A top-level `class
  DateTime is CORE::DateTime`, the core class's own name outside a package,
  is refused as X::NYI where it crashed. The five t/regression files
  (engine-objects-list-as-one, datetime-number-is-instant,
  core-qualified-parameter-type, routine-is-lexical,
  array-ends-are-containers) agree with Rakudo 2026.09.

### Left open

- Two types of one name are still one here (50aebf54): a top-level class of
  a core class's own name that inherits it is refused as X::NYI (Rakudo
  accepts it; in a package it works), a top-level class of a core name that
  does not inherit replaces the core type, and two `my class A` in two
  blocks are one class. Types are known by their names throughout.
- `%h.values.head` hands back a value, where Rakudo's is the hash entry's
  container (an Array's `.first`/`.head`/`.tail` do now); a Block's `.gist`
  is `sub { ... }` where Rakudo writes `-> { #`(Block|…) ... }`.

- A store into an array element can still land in a buffer a sibling worker's
  growth has just moved, in a program that races the two (90bc6e23): an
  in-bounds store racing a push crashes about a quarter of runs, and
  ub-indexed-grow's parallel leg is on t/stress's `%known-flaky`. Two fixes
  were prototyped and measured in docs/dev/experiments/STALE-ELEMENT-POINTER.md.
- Issues 2, 4 and 14 still bundle the mainline; 10 (a `use`d module's sub
  costs ~6× compiled) and 21 (no array-element loop lane) are unchanged.
- Found and open, the same on v5.5.0: `sub f(int $x) { $x * 2**62 }`
  answers a BigInt where Rakudo wraps; a `--> num` routine returning a Str
  hands it back where Rakudo dies; a `fail` in a block written at the
  mainline returns from the routine that runs it (Rakudo dies); and
  t/jit/run.raku's tier-up cases calls, elements, inline, methods and
  natives enter no kernel.
- t/exe/fuzz.raku's 20 `*-shadow` programs, which failed at parse time on
  v5.5.0, were not valid Raku (Rakudo refuses them too); the generator now
  writes valid ones, and all 232 programs agree.
- A Failure that builds a Range (`my $r = $f .. 2; @a[$r]`) is still 2
  before any subscript sees it, and one that has thrown is not marked
  handled, so using it again rethrows its exception where Rakudo dies with
  another (85d56001).
- A native element reached through `map` (`@n.map({ $_ = 1.5 })`) or a
  `.pairs` value takes the value as it comes, and so does a native `str`
  variable (`my str $s; $s = 5`). `for @n[0 .. 1]` aliases the elements
  where Rakudo's Range slice of a native array is a copy. `$_ += 1` at
  int64's max through an alias wraps, as a native variable does in Rakudo,
  where Rakudo's element refuses it as too wide. A native element reads as
  a boxed Int: `@n[0] + @n[0]` at 2**62 does not wrap, and `f(@n[0])` picks
  an `Int` candidate over an `int` one (273863ac). Rakudo's literal-index
  `(@u[0] = 2**64 - 1)` answers -1; Raku++ answers 2**64 - 1, as Rakudo does
  under a computed key (960e8aed).
- A `--> int` call's result is a boxed Int to the native arithmetic around it,
  as a native element is: `$s = $s + f($x)` into a `my int $s` at int64's max,
  and `my int $t = @n[0] + @n[1]`, die "Cannot unbox 64 bit wide bigint" where
  Rakudo wraps, interpreted and compiled alike. `@n[0, 1]` on a native array
  is a native array where Rakudo's is a List.
- Reading a top-level loop's header variable where its C++ declaration is
  not in scope fails the `--exe` build with a C++ error (`v_si` undeclared):
  a routine (`sub show { say $i }` beside `loop (my int $i = 0; …)`), or a
  top-level CATCH. The same before these changes (8c570a98).
- Found with issue #202, the same on b85a84ec: a class or subset a module
  declares is reachable from its importer by its short name, exported or
  not and whatever tags were asked for (`use NC; say Hid.^name` answers
  NC::Hid where Rakudo refuses the name). `Mod::EXPORT.^name` is
  `Mod::EXPORT` where Rakudo's is `EXPORT`, and `UNIT::EXPORT` is empty. A
  module that fills `EXPORT::DEFAULT::{…}` in a BEGIN without declaring the
  package writes a global EXPORT::DEFAULT, where Rakudo resolves the name
  inside the module's package. Two packages of one file exporting the same
  name are not X::Export::NameClash.
