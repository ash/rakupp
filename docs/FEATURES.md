# Raku++ — Supported Features

A from-scratch C++17 interpreter for the Raku language, tested against the
[Roast](https://github.com/Raku/roast) suite. This document inventories what
works today, grouped by theme. **~** marks partial support; gaps are noted per section.

See [REFERENCE.md](REFERENCE.md) for an exhaustive lookup sheet (every operator, subroutine, and method with a verified example), [COOKBOOK.md](COOKBOOK.md) for a cookbook of runnable snippets (each verified against `rakupp`), [examples/](../examples/) for complete example programs, and [showcase/](../showcase/) for mid-size showcase programs.

Roast standing: measured per individual test, **~89% of all declared tests pass** (193,513 / ~215,896, counting tests in files that abort before running); on the stricter file bar, **576 / 1,462 fully pass (~39%)** (671 partial, 212 no-TAP, 11 timeout). See [COUNTING.md](COUNTING.md) for how these are defined. (The declared denominator grows as parse fixes land: files that previously died before announcing a plan now declare their real, often larger, dynamic plans.)

## Language versions (6.c / 6.d / 6.e)

Raku++ **defaults to Raku 6.d**, matching Rakudo (6.e is still `PREVIEW`, opt-in). The
version pragma selects a revision, and `$*RAKU.version` reflects it:

```raku
say $*RAKU.version;                       # 6.d   (the default)
use v6.e.PREVIEW;  say $*RAKU.version;    # 6.e
use v6.c;          say $*RAKU.version;    # 6.c
```

In practice Raku++ is **largely version-agnostic**: the revision changes essentially
one runtime behaviour — `sqrt` (and `.sqrt`) of a negative returns a `Complex` under
6.e but `NaN` under 6.c/6.d. Everything else in the language behaves the same across
revisions.

**6.e-specific syntax that works today:** the **`Format` type** — `q:o/…/` and
`q:format/…/` build a callable `sprintf` template (`my $f = q:o/%5s/; $f("foo")` →
`"  foo"`). Raku++ accepts it in *any* version rather than gating it to
`use v6.e.PREVIEW` — a deliberate leniency (`:o`/`:format` aren't valid adverbs in
6.c/6.d anyway, so it never mis-parses older code). (Features like sub-signature
destructuring and multiple inheritance also work but aren't 6.e-specific — they're
part of the common language and are listed in their own sections.)

**6.e features not yet implemented:** `.are` / `.snip` / `.snitch`, multi-dimensional
subscripts and hyperslices (`@a[$a;$b;$c]:delete`, `%h{**}`), pseudo-packages
(`SETTING::`, `::<$x>`), parametric roles (`my role R[::T]`), role versioning
(`role R:ver<…>`), and nested sigilless destructuring (`my (\a, (\b, \c))`).

## Lexical & Literals
- Int (arbitrary precision / bignum), Num, Rat, **Complex** (`3+4i`); FatRat ~
- Radix `0x` `0o` `0b` `0d` and `:N<…>` (`:16<ff>`, `:36<aZ>`), `_` separators (strictly between digits — `1__0`/`100_` are errors), exponent `1e5`; string→number coercion parses all of these plus Complex (`+"1+2i"`) and the Unicode minus `−`
- Leading-dot fractions `.5`, imaginary `4i`, `∞`/`Inf`/`NaN`, superscripts `²`
- Strings: `'…'` `"…"` `q//` `qq//` `Q//`, heredocs `q:to/END/`
- Unicode string quotes `‘’ “” ｢｣`, qw `<…>` and guillemet `«…»`
- Escapes `\n \t \x[…] \o[…]` and `\c[NAME]` (Unicode character names)
- Unicode identifiers (combining marks, Latin-Extended, Greek, fullwidth, letterlike)
- **Gaps:** Unicode digit literals (`໑໐`)

## Operators
- Arithmetic `+ - * / % %% div mod ** gcd lcm`; prefix `- + ~ ? !`
- Comparison `== != < > <= >= <=> eq ne lt gt le ge cmp leg before after`, chained
- Logical `&& || // ^^ and or xor not`, `andthen` / `orelse`
- Identity / smartmatch `=== !== eqv ~~ !~~` (types, values, ranges, hashes, regex, junctions, object identity)
- Ranges `.. ..^ ^.. ^..^` — integer, string (`'a'..'z'`), and **fractional** (`1.1 .. 3.1`, `-1.5 ..^ 3` step by 1 keeping the fraction); sequence `…` (arithmetic/geometric/closure generators, ascending & descending), junctions `| & ^`
- Reduce `[+]`, zip `Z`, cross `X`, hyper `>>op>>` / `«op»`
- Contextualizers `$( ) @( ) %( ) $[ ]`, ternary `?? !!`, assignment `= := += …`
- Divisibility `%%` and negated `!%%` (both return `Bool`)
- Bitwise / boolean: numeric `+& +| +^`, string `~& ~| ~^`, boolean `?& ?| ?^`
- Function composition `∘` / `o` (`(f ∘ g)(x)` == `f(g(x))`); feed operators `==>` / `<==`
- Compound assignment for word operators (`$x x= 3`, `$n gcd= 12`) and reverse-metaop reduce (`[R~]`, `[R-]`)
- User-defined operators — all six categories: `infix`/`prefix`/`postfix`/`term`/`circumfix`/`postcircumfix` (`sub infix:<…>`, `sub postfix:<!>` → `5!`, `sub circumfix:<⟦ ⟧>`, `sub term:<TAU>`)
- Meta-operators over user-defined operators: `[myop]` reduce, `>>myop<<` hyper, `Z§`/`X§` zip/cross, `$x myop= y` meta-assignment
- Whatever-currying: infix `* + 1`, prefix `~* -* +*`, postcircumfix `*.<key>` `*[i]`, subscript `@a[*-1]` `@a[*]`
- Precedence/associativity traits on custom operators: `is tighter(&infix:<+>)` / `is looser(…)` / `is equiv(…)` / `is assoc<left|right|non>`
- **Gaps:** other negated metaops; word-form of a user op in a meta-op (`Zpl`)

## Control Flow
- `if/elsif/else`, `unless`, `while/until`, `for`, C-style `loop`, `repeat`
- `given/when/default`, `with/without`
- Statement modifiers (`if unless while until for given when with`), including chained (`X if A for B`)
- `last/next/redo` (incl. labeled: `LABEL: for … { last LABEL }`), `gather/take` (lazy — an infinite `gather { loop { take … } }` yields on demand), `do`
- `FIRST` loop phaser (runs once, before the first iteration; `last` inside it breaks the loop)
- **Gaps:** `NEXT`/`LAST` loop phaser ordering vs `LEAVE`

## Subs, Signatures & Dispatch
- `sub`, `multi`/`proto` dispatch (by type, arity, `where`, literal, `:D`/`:U` smileys)
- Params: positional, named, optional `?`, slurpy `*@`/`*%` (single-argument rule), defaults
- `is rw` / `is copy`, sigilless `\a`, capture `|c`, return-type `-->`, type-capture `::T`
- Sub-signature destructuring: `[$a,$b]` / `($a,$b)` unpack a list arg, `|c($x,$y)` a capture, `*[$a,$b]` a slurpy, and a *named* `@a [$first, *@rest]` (binds `@a` and unpacks it) — with `multi` dispatch on the destructured arity
- Coercion-type containers: `my Int(Str) $n = '42'` coerces the value to the target type
- Anonymous subs, closures, placeholder params `$^a`, sub/block as argument
- Redispatch: `callsame`/`callwith`/`nextsame`/`nextwith`/`samewith`; routine `.wrap`/`.unwrap` (wrapper `callsame`s to the original)
- **Gaps:** `is rw` inside a sub-signature, `-> [$a,$b]` pointy destructure

## Objects, Classes, Roles
- `class`/`role`/`grammar`, attributes `has $.x`/`$!x` (+ defaults), accessors
- `method`/`multi method`/`submethod`, `self`, single inheritance `is`, `does`
- `BUILD`, default `.new`, `bless`, enums
- Metamodel: `.^name .^methods .^mro .^parents .^roles .^add_method .^find_method .^parameterize` (`Set.^parameterize(Str)` is `Set[Str]`), `.WHAT .WHICH .HOW`
- `augment`/`supersede` reopen a type — user classes **and** built-ins (`augment class Int {…}` reaches `3.method`); `does`/`but` runtime role mixins
- Inheritance errors: `class A is A` (self), `class B is Undeclared` → compile-time throws
- **Gaps:** `Metamodel::*` construction, submethod-not-inherited

## Regexes & Grammars
- `/…/`, `m//`, `s///`; char classes `\d \w \s`, `<[…]> <-[…]> <+[…]>`
- Unicode property classes: general category short + long (`<:Nd>`/`<:L>`/`<:UppercaseLetter>`), `<:LC>`/`<:Assigned>`, **blocks** `<:InArabic>` (real 16.0 table), and **binary props** `<:Math>`/`<:Alphabetic>`/`<:Soft_Dotted>`/`<:White_Space>`/`<:Other_*>` (DerivedCoreProperties + PropList); negated `<:!…>` and inverted `<-:…>`
- Script classes `<:Latin> <:Syriac> <:Canadian_Aboriginal> …` (real 16.0 Scripts.txt) and `Bidi_Class` `<:bc<L>>`/`<:bc<EN>>`; zero-width property assertions `<?:prop>`/`<!:prop>`
- Anchors `^ $ ^^`, quantifiers `* + ? ** {n..m}`, alternation `|`/`||`, **conjunction `A & B` / `A && B`** (all terms match at the same position; the match spans the last term), groups, named captures
- Repeated named captures under a quantifier collate into a list (`@<content>`, `$<x>[1]`)
- **Capture aliases**: numbered `$7=(…)` (sets the capture index, auto-numbering resumes at N+1), named `$<x>=(…)`, list-valued `@<x>=(…)` (each occurrence → an Array element), hash-valued `%<x>=(…)` (matched strings become Hash keys)
- Subrules, grammars (`token`/`rule`/`regex`, `.parse`/`.subparse`/`.parsefile`), `$/ $0…`, actions
- Capture interpolation `"$0/$1"`, callable `.subst` (`.subst(/…/, *.uc)`), non-mutating `S///`
- `Match` accessors: `.from`/`.to`, `.orig`, `.prematch`/`.postmatch`, `.made`/`.ast`
- Match adverbs `:g(lobal) :ex(haustive)` (every match at every position and length) `:x :nth :p :c :i :samecase/:ii :sigspace :samespace/:ss :ignoremark/:m :samemark/:mm`, ordinals `:2nd`, assignment forms `s[…] = … / OP= …`, `$var`/`$^a`/`@a` interpolation in pattern & replacement
- **Gaps:** backtracking control, `:ratchet`; `<:Block(…)>` block-by-name and nested sub-rule capture aliases

## Unicode (generated from UCD/UCA 17.0 — see [UNICODE.md](UNICODE.md))
- Normalization **NFC / NFD / NFKC / NFKD** (+ Hangul), `Uni`/`NFC`/`NFD` types; `Uni.new(…).Str` is NFC (NFG semantics) — all `nf*-*.t` + `mass-equality.t` pass
- Grapheme clusters (full UAX #29 incl. **GB9c** Indic conjuncts, emoji ZWJ/skin-tones, flags 🇦🇧): `.chars`/`.comb`/`.flip` — `GraphemeBreakTest-*` and `emoji-test.t` (3,825) fully pass
- **UCA collation** — `unicmp`/`coll` from DUCET 17.0 (contractions incl. discontiguous matching, implicit weights): all 8,271 conformance tests pass
- Character names both directions — `\c[NAME]`, `uniname` — incl. control aliases and algorithmic CJK/Tangut/Nushu/**Hangul syllable** names; numeric values `unival`/`univals` as exact Rats, incl. Unihan numerals (`千` = 1000)
- Regex properties: general category (short `<:L>`/`<:Nd>` and long forms), **scripts** `<:Latin>`/`<:Script<Greek>>` (real Scripts.txt), **blocks** `<:InArabic>`, **bidi** `<:bc<L>>`, binary props (`<:Math>`, `<:Soft_Dotted>`, …); negated/inverted `<:!P>`/`<-:P>`
- **Gaps:** full case folding (`ß.fc`, final sigma), `:ignorecase`/`:ignoremark` on non-ASCII, `samemark`, `.collate`/`.sort` not yet UCA-routed

## Data Types & Built-ins
- Array, List/Seq, Hash, Map, Pair, Range, Set/Bag/Mix (+Hash variants), Junction, IO::Path, Proc, Promise
- String: `chars codes uc lc tc fc wordcase samecase index rindex substr split comb subst trans words lines flip trim starts-with~ ends-with~ contains sprintf ords chrs ord chr` (`.trans` supports `a..z` ranges); `undefine($x)` resets a container
- List: `map grep sort reverse join first reduce produce sum min max elems push pop shift unshift splice keys values kv pairs antipairs invert unique repeated squish classify categorize rotor batch permutations combinations rotate flat head tail skip pick roll tree`; `slip(…)` spreads into the enclosing list; `.grep` smartmatches Type/Regex/value; `.head`/`.tail`/`.skip` take `*`/`*-N`/`Inf`; `.min`/`.max` skip undefined elements; list methods on scalars (`5.map`, `42.grep`); `roundrobin`. `.classify`/`.categorize` accept a Callable, Hash, or Array mapper and an `:into(%h)` target; `.tree` builds a nested view (`.tree(N)` depth-limited, `.tree(&c1,&c2,…)` per-level closures)
- **Shaped arrays**: `my @a[3]` / `my @a[2;2]` / `Array.new(:shape(2;2))` — fixed-dimension, `.shape`, row-major fill with structural validation (out-of-range or over-full assignment throws `X::OutOfRange`/`X::Assignment::*`), size-changing ops (`push`/`pop`/…) throw `X::IllegalOnFixedDimensionArray`, multi-dim `@a[i;j]` + `AT/EXISTS/ASSIGN-POS(i,j)`, leaf-walking `keys`/`values`/`kv`/`pairs`/`antipairs`/`iterator`/`flat` with index tuples
- Negative list index is out of range (a Failure, no Python-style wraparound — `@a[*-1]` is the from-the-end form); an unhandled Failure propagates through further indexing (`@a[-1][0]`)
- **Iterator protocol** (S07): `.iterator` on any collection with `pull-one`/`push-all`/`push-exactly`/`skip-at-least`/`count-only`/… and the `IterationEnd` sentinel; **lazy infinite sources** — `^Inf`/`1..Inf` with `.head`/`.skip`/`.grep`/`.first`/`.map` composing lazily (`(^Inf).grep(*.is-prime).head(5)`)
- Hash: `push`/`append` (accumulate values under a key), `kv keys values pairs invert antipairs`
- Math: `abs sqrt floor ceiling round sign exp log log10 log2` + full trig, `polymod`, `base`, `rand` / `.rand`, constants `pi tau e`

## I/O, System, Concurrency
- `open`/FileHandle (`.lines .get .slurp .print .say .put .getc .readchars .close`; `close`/`getc` sub forms), `dir`, `make-temp-file`
- `IO::Path` tests & stat: `.e .f .d .r .w .x` (real `access()`), `.s` (size), `.z` (empty), `.l` (symlink), `.mode` (octal string), `.modified/.changed/.accessed` (distinct-precision Instants), `.chmod`; filetest-adverb smartmatch `$path.IO ~~ :e/:d/:f/:r/:w/:x/:s/:z/:l`; `chmod`/`unlink`/`mkdir` subs
- `say`/`print`/`put`/`note` (and their method forms) honour a user-overridden dynamic `$*OUT`/`$*ERR` — assigning `my $*OUT = $mock-handle` reroutes output through its `.print`
- `run` → standard `Proc` (`.out.slurp .exitcode .so`; `+$proc` is the exit status); bidirectional `run(:in,:out)` (`.in.spurt` / `.out.slurp`, e.g. piping through `pandoc`); `shell(CMD)` runs `CMD` through `/bin/sh -c` (redirections/pipes work)
- Line-processing: `-n` / `-p` command-line switches (awk/perl line loops over `$*ARGFILES`); `$*IN`/`$*ARGFILES` reading (`.lines .get .words .slurp`, bare `lines()`/`get()`/`words()`)
- **Concurrency (real `std::thread`s + a GIL, CPython-style; blocking ops release the lock: `sleep`/`await` let tasks interleave in time (sleep-sort actually sorts), and external-process waits (`run`/`shell`) run in genuine parallel wall-clock — N concurrent `run('sleep','1')` finish in ~1s, not N s. Optional true CPU parallelism via `RAKUPP_PARALLEL=1`: worker threads run interpreter compute concurrently — per-thread registers/stacks are thread-local, the symbol tables freeze once concurrency engages, and `Lock`/`Semaphore` become real; ~3× on 8 workers, 0 Roast regressions, ThreadSanitizer-clean. Default (flag off) keeps the GIL. `start EXPR` correctly thunks EXPR to the worker):**
  - `Promise` — `start` (kept, or **broken** if the block dies), `await` (blocks, rethrows the cause), manual `Promise.new`/`.keep`/`.break`/`.vow`, `.result`/`.status` (a real `PromiseStatus` enum)/`.cause`/`.Bool`, deferred `.then`, `Promise.anyof`/`.allof` (with `X::Promise::Combinator`), `Proc::Async`
  - `Supply` (`from-list` — args are values, `[..]` items stay unflattened / `tap`/`act`/`map`/`grep`/`first`/`do`/`grab`/`unique`/`squish` (incl. `:as`/`:with`)/`head`/`tail`/`skip`/`reverse`/`rotate`/`sort`/`min`/`max` (running extremes, `&mapper`)/`minmax` (running Range)/`produce` (scan)/`reduce`/`zip` (`:with`)/`merge`/`list`/`wait`/…), `Tap.close`, live `Supplier` (`.emit`/`.done`/`.quit`) with combinators as a per-value transform tap-chain (`grep`/`map`/`head`/`first`/`skip`/`unique`/`squish`), a real `react` event loop / `whenever` (incl. `react whenever …`) / `supply { emit … }`
  - Containers: `Proxy.new(:FETCH/:STORE)` (reads/writes run your code), `is rw` / `return-rw`, `$_` rw-aliased to array elements in `for` loops
  - `Channel` (`send`/`receive`/`poll`/`close`/`fail`/`closed`, `X::Channel::*`), `Thread` (`.start`/`.join`, `is-initial-thread`, `$*THREAD`), `Lock`/`Semaphore` (`.protect`/`.acquire`/`.release`), `sleep`
  - `atomicint` containers and the `⚛` operators (`$x⚛++`, `⚛$x`, `$x ⚛= v`) + `atomic-fetch`/`-fetch-inc`/`-fetch-dec`/`-fetch-add`/`atomic-assign` (correct under the GIL; true lock-free atomics only under `RAKUPP_PARALLEL`)
- **NativeCall**: `sub … is native {*}` / `is native('lib')` / `is symbol('n')` calls a C function via `dlsym`. Integer/pointer args (`Str`→`char*`, the `int`/`uint`/`size_t`/`bool`/`Pointer` family) **and** floating-point args (`num`/`num32`/`num64`), with an integer/pointer/`Str`/float/void return — arguments coerce to their declared type (`sqrt(4)` works). Covers all of libc's integer functions and the whole `<math.h>` surface (`strlen`, `getenv`, `sqrt`, `pow`, …). No `libffi`, so **mixed int/float args, C structs, `CArray`, and callbacks** are unsupported (mixed args give a clear error).
- `$*CWD $*EXECUTABLE $*ARGS $*RAKU/$*PERL` (`.compiler.name` = "Raku++", backend "cpp"), `$*DISTRO $*KERNEL $*VM $*THREAD $*SCHEDULER`
- **Gaps:** true CPU parallelism is opt-in (`RAKUPP_PARALLEL`), off by default; real wall-clock timers (`Promise.in`/`.at` are capped), `cas`, stream-retokenizing Supply combinators (`split`/`comb`/`words`/`lines`)

## Phasers, Modules, Exceptions, Special Vars, Testing
- Phasers: `BEGIN CHECK INIT END` (top-level ordering), `ENTER/LEAVE` (block entry/exit), `FIRST` (once per loop), `CATCH`; `BEGIN`/`ENTER` usable in value position
- `state` variables (persistent), modules `use`/`need`/`no`, `use lib <expr>`, sub hoisting, `EVAL`
- **POD DOM**: `$=pod` / `@=pod` as `Pod::Block` objects (`Pod::Block::Named`/`::Para`/`::Code`/`::Comment`, `Pod::Heading`, `Pod::Item`) with `.name`/`.contents`/`.level` — delimited/paragraph/abbreviated blocks, nesting, indent-based code blocks; plus `$=finish`
- Exceptions: `die`/`try`/`CATCH`, `throws-like`, `X::*` (partial), resumable via `.resume` inside a `CATCH`; `fail`/`Failure.new` carry an exception (`.exception`), report undefined for `//`
- Special vars: `$_ $/ $! @*ARGS $?LINE $?FILE`
- Test API: `plan ok nok is isnt is-deeply like unlike cmp-ok isa-ok is-approx dies-ok lives-ok throws-like eval-lives-ok subtest skip todo pass flunk diag done-testing "plan skip-all"`
