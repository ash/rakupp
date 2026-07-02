# Raku++ — Supported Features

A from-scratch C++17 interpreter for the Raku language, tested against the
[Roast](https://github.com/Raku/roast) suite. This document inventories what
works today, grouped by theme. **~** marks partial support; gaps are noted per section.

See [EXAMPLES.md](EXAMPLES.md) for a cookbook of runnable snippets (each verified against `rakupp`).

Roast standing: **185 / 1,464 files fully pass (~13%)**; 508 partial, 752 no-TAP, 19 timeout. (Among files that run, 96,059 / 111,360 reached assertions pass — a correctness signal, not a coverage figure; see [ROAST.md](ROAST.md).)

## Lexical & Literals
- Int (arbitrary precision / bignum), Num, Rat, **Complex** (`3+4i`); FatRat ~
- Radix `0x` `0o` `0b`, `_` separators, exponent `1e5`
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
- Ranges `.. ..^ ^.. ^..^`, sequence `…`, junctions `| & ^`
- Reduce `[+]`, zip `Z`, cross `X`, hyper `>>op>>` / `«op»`
- Contextualizers `$( ) @( ) %( ) $[ ]`, ternary `?? !!`, assignment `= := += …`
- Divisibility `%%` and negated `!%%` (both return `Bool`)
- **Gaps:** other negated metaops, custom operator definitions (`sub infix:<…>`)

## Control Flow
- `if/elsif/else`, `unless`, `while/until`, `for`, C-style `loop`, `repeat`
- `given/when/default`, `with/without`
- Statement modifiers (`if unless while until for given when with`), including chained (`X if A for B`)
- `last/next/redo`, `gather/take`, `do`
- **Gaps:** loop labels (`LABEL: for … { last LABEL }`)

## Subs, Signatures & Dispatch
- `sub`, `multi`/`proto` dispatch (by type, arity, `where`, literal, `:D`/`:U` smileys)
- Params: positional, named, optional `?`, slurpy `*@`/`*%` (single-argument rule), defaults
- `is rw` / `is copy`, sigilless `\a`, capture `|c`, return-type `-->`, type-capture `::T`
- Anonymous subs, closures, placeholder params `$^a`, sub/block as argument
- **Gaps:** `callsame`/`nextsame`, sub-signature destructuring binding

## Objects, Classes, Roles
- `class`/`role`/`grammar`, attributes `has $.x`/`$!x` (+ defaults), accessors
- `method`/`multi method`/`submethod`, `self`, single inheritance `is`, `does`
- `BUILD`, default `.new`, `bless`, enums
- Metamodel: `.^name .^methods .^add_method .^find_method`, `.WHAT .WHICH`
- **Gaps:** `.HOW` metaobjects, `Metamodel::*`, subset error-detection

## Regexes & Grammars
- `/…/`, `m//`, `s///`; char classes `\d \w \s`, `<[…]> <-[…]> <+[…]>`
- Unicode property classes `<:Nd> <:L> <:Alpha>` (general categories) and `<:!…>` negation
- Script classes `<:Latin> <:Greek> <:Cyrillic> <:Han> …` (approximate, block-based)
- Anchors `^ $ ^^`, quantifiers `* + ? ** {n..m}`, alternation, groups, named captures
- Subrules, grammars (`token`/`rule`/`regex`, `.parse`/`.subparse`/`.parsefile`), `$/ $0…`, actions
- Capture interpolation `"$0/$1"`, callable `.subst` (`.subst(/…/, *.uc)`), non-mutating `S///`
- **Gaps:** named-capture storage (`$<name>=(…)`), replacement `$0`-interp in `s///`, backtracking control, `:ratchet`

## Unicode (generated from UCD 15.1)
- Normalization **NFC / NFD / NFKC / NFKD** (+ Hangul), `Uni` type
- Grapheme-correct `.chars` (UAX #29: emoji, flags 🇦🇧, ZWJ sequences, Hangul)
- Character names `\c[NAME]`, `uniname`; numeric values `unival`/`univals`
- General category + script (`.uniprop`, `.uniprop('Script')`, `uniprop($c,'Script')`, `<:cat>`/`<:Script>` in regex)
- **Gaps:** `uniprop` binary props, exact UCD scripts (current scripts are block-approximate), `unimatch`, full NFG grapheme-break suite

## Data Types & Built-ins
- Array, List/Seq, Hash, Map, Pair, Range, Set/Bag/Mix (+Hash variants), Junction, IO::Path, Proc, Promise
- String: `chars codes uc lc tc index rindex substr split comb subst words lines flip trim starts-with~ ends-with~ contains sprintf`
- List: `map grep sort reverse join first reduce sum min max elems push pop shift unshift keys values kv pairs antipairs invert unique repeated squish permutations combinations rotate flat head tail`
- Math: `abs sqrt floor ceiling round sign exp log log10 log2` + full trig, constants `pi tau e`

## I/O, System, Concurrency
- `open`/FileHandle (`.lines .get .slurp .print .say .close`), `dir`, `make-temp-file`
- `run` → standard `Proc` (`.out.slurp .exitcode .so`); `Proc::Async`, `Promise`, `await` (synchronous)
- `$*CWD $*EXECUTABLE $*ARGS $*RAKU/$*PERL` (`.compiler.name` = "Raku++", backend "cpp")

## Phasers, Modules, Exceptions, Special Vars, Testing
- Phasers: `BEGIN CHECK INIT END` (top-level ordering), `ENTER/LEAVE` (block entry/exit), `CATCH`
- `state` variables (persistent), modules `use`/`need`/`no`, `use lib <expr>`, `$=finish` POD, sub hoisting, `EVAL`
- Exceptions: `die`/`try`/`CATCH`, `throws-like`, `X::*` (partial)
- Special vars: `$_ $/ $! @*ARGS $?LINE $?FILE`
- Test API: `plan ok nok is isnt is-deeply like unlike cmp-ok isa-ok is-approx dies-ok lives-ok throws-like eval-lives-ok subtest skip todo pass flunk diag done-testing "plan skip-all"`
