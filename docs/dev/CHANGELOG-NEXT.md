# The next release's CHANGELOG entry, collected as work lands

*Emptied at the v5.8.0 tag: everything collected here is in
[CHANGELOG.md](../../CHANGELOG.md). This is not a CHANGELOG entry yet:
[RELEASING.md](RELEASING.md) step 2 writes that at the tag, with the tag's
own gate figures. Add each item as it lands, in the CHANGELOG's own shape —
what changed, what it measured and how, with the commit — so the entry starts
from this page. Move each item into CHANGELOG.md at the tag and delete it
here.*

*Covered so far: 041789fd, 147d0b95, 7fc5b00b, b4f6e472, 00c30ed1, b980a365,
4f507a0f, 401e19e8, cee2cd42, 29f64dcc, bdb022c4, 38cb1d1e, a64cd841,
91033936, 2987aab9, b66ccda3, 6eef76fd, ecaee71c, 24256603, 521d34ae,
a0437c10, 703198f5, cb91f67b, 2416bbe5, 8c513f23, 585a8ee1, 133560ad,
ad10ad81, 3db5cae5, c109e8ec, c47aa1ea, cfbda86a, ccda30e9, 1c4da191,
22520fae, eca56b5a, 1ba950a5, 7dc4809d, 9bb82a6c, 57b07f65, 1f205847,
6b709af5, 31206e76, a2fcf5bb, e9aaf6b3, 9e693ab0, edc8bf39, 1ea5c984,
2137a05d, d45f616e, 9e12d938, 70ee4e8b, 6fcd9181, f102e0f5, b1040540,
d77e1185, 953adf0a, 73357985, 9badbec4, b590c1ec, f9028851, fdaa60ec,
f8b5555c, ce72763f, bfd530f9, eb482a9f, 7e5a643e, 6b8c1d1a, 41150ad1,
2eafbb23, bec86076, ea466f67, ef2e778b, 112a0cdb, a247fad2, 9a6eeb85,
251842c2, fcc6d577, 3a3fc21e, 62ada605, 9f94cac7, 2fe0aeeb, 1f3377ea,
8f7ac1df, 6de9d27b, ce54f928, d3059bf5, f1491b3e, 06212d97, e89cc6a7,
a213ccad, 02b24803, 9e948ff3, e60a6163, 657626c7, 13902c52, a397987e,
e2981dc5, c5273f1e, 7df13701.*

---

### New

- **`.AST` of a grammar rule, and DEPARSE of it as Rakudo renders it**
  (657626c7) (rakudo#5479, #5978, #5991). It said "`.AST` does not yet build
  a view for a grammar rule", unless the rule's whole body was one quoted
  literal. The view now parses the rule's text into Rakudo's `Regex::`
  nodes: literals and quotes, character classes, anchors, groups and
  captures, named and look-around assertions, enumerated classes, `:my`,
  code blocks, modifiers, quantifiers with their backtrack suffixes and
  separators, and the four branchings. Anything else is refused by name.
  DEPARSE gives Rakudo 2026.09's text for 33 probe bodies. A `unit` package
  deparses as one, a package's parts come back in source order, and a
  rule's `#|`/`#=` docs are its WHY. `t/regression/rakuast-grammar-rule-view.raku`.
- **`nqp::time_n`** (a397987e): the seconds since the epoch as a Num, which
  GLib::Timeout reads; it died "Undefined routine 'nqp::time_n'".
  `t/regression/nqp-time-n.raku`.

- **`use Telemetry`** (fcc6d577) (rakudo#1513, #3844, #5144): Rakudo's core
  module for measuring a program, ported to plain Raku and carried by the
  binary. `T<max-rss>`, `snap`, `snapper`, `periods` and `report` give
  Rakudo's columns and report. It was "Could not find Telemetry".
  `t/regression/telemetry-module.raku`.
- **`use trace`** (3a3fc21e) (rakudo#5546): each statement is announced on
  $*ERR as it runs, numbered and placed as Rakudo announces it.
  `t/regression/use-trace-pragma.raku`.
- **`Routine.prec`, `.precedence`, `.associative`, `.thunky`, `.iffy`**
  (bec86076) (rakudo#4780): an operator's precedence properties, Rakudo's
  for the built-ins and as `is equiv`/`tighter`/`looser`/`assoc`/`prec`
  make them for a declared one. `t/regression/routine-prec.raku`.
- **`IO::Socket::Async.listen-path` and `.connect-path`** (9a6eeb85)
  (rakudo#5358, #5359): UNIX-domain sockets.
  `t/regression/async-socket-unix-path.raku`.
- **IO::Socket::INET over IPv6** (a247fad2) (rakudo#1879): `:family(PF_INET6)`
  died "only PF_INET". `t/regression/socket-inet-ipv6.raku`.
- **The EnumHOW protocol** (2eafbb23) (rakudo#2165): `E.HOW.add_enum_value(E,
  key => value)`, `enum_values`, `enum_value_list`, `enum_from_value`.
  `t/regression/enum-how-add-enum-value.raku`.

### Faster

- **A sequence whose generator outgrows a double, and big-Int addition**
  (02b24803) (rakudo#2844). `1, 1, * + * ... *` to 40k terms took 0.92 s,
  against Rakudo's 0.07. Each step numified the last element for a step the
  closure does not use, through the decimal string, and an Int past 1e315 is
  now ±Inf without one. BigInt addition carries with a compare and allocates
  once: 40k terms 0.03 s, and 200k Fibonacci steps in a loop 2.46 → 0.71 s
  (Rakudo 0.79). `t/regression/sequence-generator-over-huge-ints.raku`.
- **`my $ = …` and a ternary on the right of a declaration** (9e948ff3)
  (rakudo#1416). An anonymous `my $` missed the declaration lane and the loop
  kernel a named one takes. `for ^3_000_000 { my $ = 1 }` went from 0.48 s to
  0.003 s. `my $x = COND ?? A !! B` over variables and literals takes the
  lane: `my $ = $n ?? 1 !! Nil` 0.54 → 0.21 s (Rakudo 0.22).
  `t/regression/anonymous-my-and-ternary-on-declaration-lane.raku`.

### Fixed: silent wrong answers

- **Three from re-reading the UNCLEAR issue threads** (e2981dc5):
  - `(1, 2) X~~ -> $ { True }`, `Z~~`, `»~~»`, `[~~]` and `infix:<~~>`
    called never called a Callable matcher, and answered False
    (rakudo#6793).
  - `"中".encode("gbk")` handed back UTF-8 bytes for any name it did not
    know. It dies X::Encoding::Unknown, as Rakudo does (rakudo#4102).
  - `.base(10, 20, :no-trailing-zeroes)` ignored the flag; a Rat now drops
    its trailing zeroes (rakudo#1872).

  `t/regression/unclear-reread-fixes.raku`.
- **`--exe`: `(1, 2) X~~ *.so` matches each element** (7df13701), as the
  interpreter does, where compiled code curried the whole expression into one
  WhateverCode. The codegen's smartmatch rule named only `~~` and `!~~`.

- **A phaser used as a value runs at its phase** (b980a365) (rakudo#4351, and
  the 23 issues that time themselves with `now - ENTER now`). What was
  wrong:
  - `say "body"; my $x = INIT say "init"` said body first, and so did
    BEGIN, CHECK and ENTER, in block and expression form;
  - `say now - INIT now` printed about −0.00003;
  - `CHECK <expr>` and `FIRST <expr>` were "Undefined routine";
  - `FIRST { … }` used as a value in a loop ran on every iteration;
  - an INIT parameter default ran on every call.

  Now BEGIN, CHECK and INIT run before the code around them, the top level
  included; ENTER runs at each entry of its block; FIRST runs at a loop's
  first iteration, and every iteration reads its value. At the top level of
  a program or an EVAL, the FIRSTs and then the ENTERs run on entry, as
  Rakudo runs them: EVAL'd code ran even a statement-form ENTER in place. A
  program that uses a value phaser bundles with the interpreter under
  `--exe`, which had compiled it in place. Every snippet of those issues
  now prints a positive time, except one that calls `nqp::time_n`.
  `t/regression/value-phasers-run-at-their-phase.raku`.
- **A set or bag operator op-assigned to an undefined target starts empty**
  (00c30ed1). `my $s; $s ∪= 0` was Set.new(Any, 0), where Rakudo gives
  Set.new(0): the undefined target joined in as a value. So did `(-)=`,
  `⊖=` and `⊎=`. The target now starts from set() or bag(), and so does a
  reduce over nothing: `[∪] ()` and `[⊎] ()` were Any.
  `t/regression/set-op-assign-starts-empty.raku`.

- **`is cached` caches** (cee2cd42) (rakudo#4665, rakudo#4666). The trait
  was accepted and did nothing: `pow2(10)`, recursing twice per level, made
  2,047 calls where Rakudo makes 11. A call is keyed on the gist of its
  Capture, as Rakudo keys it, and every closure of one declaration shares one
  cache. `samewith` goes through the cache too (Rakudo's skips it), and a Set
  argument is one key whatever its order. Under `--exe` such a program
  bundles with the interpreter.
  `t/regression/is-cached-answers-a-seen-call.raku`.
- **`self!name` reaches the private method of the package it is written in**
  (91033936) (rakudo#2663). With `(Puppy.new but Violinist) but Diva`,
  Puppy's own `greet` bowed as the Diva, and so did the Violinist's
  `fiddle`. A class's method now reaches its class's `!name`, and a role's
  method the class the role was composed into, as in Rakudo. A compiled
  `$o!Pkg::name` had died "No such private method".
  `t/regression/private-call-resolves-in-its-package.raku`.
- **The program's `multi infix:<op>` meets its own classes' type objects**
  (bdb022c4) (rakudo#6860). With `multi infix:<~>(Str() $a, A $b)`,
  `"A: " ~ A` printed "A: " where Rakudo prints the candidate's answer; an
  instance already met it. `t/regression/user-infix-takes-a-type-object.raku`.
- **A typed hash's Pairs check what `.value` is given** (a64cd841)
  (rakudo#2068): `my Int %h = :42foo; %h.map: { .value = 'meows' }` stored
  the Str. `t/regression/typed-hash-pair-value-checks-type.raku`.
- **`.assuming` keeps a remaining parameter's `.default`** (29f64dcc,
  38cb1d1e) (rakudo#4553): it was the Code type object, where Rakudo answers a
  block that gives the default. Under `--exe` a routine whose defaults the
  program reads with `.default` runs in the interpreter, since a compiled
  signature carries only the default's text.
  `t/regression/assuming-residual-default.raku`.
- **A `multi submethod` is a submethod** (2987aab9) (rakudo#3976): it was in
  `.^method_table` and missing from `.^submethod_table`, and a plain
  submethod was in both. `t/regression/submethod-table-and-method-table.raku`.
- **R reverses an operator's associativity as well as its operands**
  (521d34ae) (rakudo#4757): `2 R- 3 R- 4` grouped from the left and gave 3,
  where Rakudo groups `R-` from the right and gives −1, as `[R-] 2, 3, 4`
  does. `t/regression/r-metaop-flips-associativity.raku`.
- **R composes over a zip or a cross** (8c513f23, 585a8ee1) (rakudo#6128,
  rakudo#1616): `(1,2) RZ+ (10,20)` read the `+` as a prefix of the right
  operand and gave ((2 1)), where Rakudo gives (11 22). `(1,2) RZ (3,4)`
  died "Undefined routine 'RZ'", `[RZ] (1,2),(3,4)` was an array holding
  the name, and `&[RZ,]` and `ZR-` were refused.
  `t/regression/r-metaop-over-zip-and-cross.raku`.
- **`reduce` with a built-in comparison chains, as `[op]` does** (a0437c10,
  703198f5) (rakudo#4128): `reduce &[>], 6, 5, 4` folded `(6 > 5) > 4` and
  gave False, where Rakudo answers `6 > 5 > 4`, True.
  `t/regression/reduce-with-chaining-operator.raku`.
- **A set relation reduces as a chain** (2416bbe5): `[⊆] ()` was Any and
  `[∈] (1,)` was 1, where both are True; three operands test each adjacent
  pair. `t/regression/set-predicate-reduce-chains.raku`.
- **Set membership reads a Range as the Set of what it iterates** (cb91f67b)
  (rakudo#2217): `½ ∈ ^2` was True, because the Range was asked whether ½
  lay between its ends, and `"aa" ∈ "a".."c"` was True by string order. Over
  an endless range `(elem)` now dies X::Cannot::Lazy, as in Rakudo; it
  answered True. `t/regression/range-is-the-set-of-its-elements.raku`.
- **`roundrobin` follows the single-argument rule** (133560ad)
  (rakudo#1447): `roundrobin ((1, 2, 3), (4, 5), (6, 7, 8, 9))` took its one
  argument as one list and answered nine rounds of one.
  `t/regression/roundrobin-single-argument.raku`.
- **A Callable count to `.head`, `.tail` and `.skip` counts from the end**
  (ad10ad81) (rakudo#2485): it was called with the element count, so
  `<a b c>.head(*.elems - 1)` was () where Rakudo takes all three. `*-N`
  answered the same either way.
  `t/regression/head-tail-callable-count-from-end.raku`.
- **`.combinations` reads a Rat range of sizes as Rakudo does** (3db5cae5)
  (rakudo#1629):
  `<a b c>.combinations(2.3..^3.6)` dropped (a b c).
  `t/regression/combinations-rat-range-sizes.raku`.
- **`EXPR for 1..*` runs as long as the range does** (c109e8ec)
  (rakudo#1399, rakudo#1398): the statement modifier stopped after 10,000
  iterations, so `.sink for 1..*` returned at once.
  `t/regression/modifier-for-over-endless-range.raku`.
- **`[»op«]` over one operand is the inner operator's one-argument form**
  (cfbda86a) (rakudo#5951): `[»+«] [[1,2],]` handed back [1 2] where Rakudo
  numifies it to 2, and `infix:<~>(<a b>)` was "a b" where Rakudo joins it
  to "ab". `t/regression/hyper-reduce-one-operand.raku`.
- **A lone Array bound through a `+` slurpy keeps its elements' containers**
  (ccda30e9) (rakudo#1355): `-> +l { l }([1, (2, 3)])` was `(1, (2, 3))`,
  where Rakudo keeps `(1, $(2, 3))`.
  `t/regression/plus-slurpy-keeps-array-containers.raku`.
- **`.=` into a `$` variable itemizes what it stores, as `=` does**
  (1c4da191) (rakudo#3621): `$x .= split(',')` held a bare Seq, so `.raku`
  lost its `$( )` and `for $x` walked the elements.
  `t/regression/dot-assign-itemizes-into-scalar.raku`.
- **A compiled reduction over one Array takes its elements one level deep**
  (1ba950a5): in a `--exe` binary `[+] [[1,2],[3,4]]` was 10 where it is 4,
  `[»+«] @a` summed every number, and `[Z]` of a matrix zipped one flat
  row. The interpreter was right. `t/regression/reduce-lone-array-operand.raku`.
- **A hyper operator's answer takes its left operand's container** (57b07f65)
  (rakudo#5951): `my @r = [»+«] @a` held the summed row's elements, where
  Rakudo holds the row as one item, `[[5, 7, 9],]`, and `$[1,2] »+« $[3,4]`
  was `[4, 6]` where Rakudo answers `$[4, 6]`.
  `t/regression/hyper-reduce-one-operand.raku`.
- **A compiled hyper operator is the interpreter's** (9bb82a6c): in a `--exe`
  binary `%h »+« %g` was `(2,)` where it is `{:a(3)}`, and every array answer
  came back a List. `t/regression/hyper-reduce-one-operand.raku`.
- **`$x ~~ TR///` answers the transliterated Str** (6b709af5) (rakudo#1551):
  `'hello' ~~ TR/e/1/` was False, the result smartmatched against 'hello',
  where Rakudo answers "h1llo". A tr/// StrDistance's `.gist` and `.raku`
  are `StrDistance.new(before => …, after => …)`, as Rakudo's.
  `t/regression/tr-smartmatch-answers-the-string.raku`.
- **`.parse` and `.subparse` start at `:pos`** (31206e76) (rakudo#1243):
  `G.subparse("ABCDE", :pos(4))` matched all five letters. A failed
  `.subparse` answers a failed Match, `#<failed match>`, where it was Any.
  `t/regression/subparse-pos-and-failed-match.raku`.
- **A bounded quantifier repeats a zero-width item** (a2fcf5bb)
  (rakudo#4752): `'00000' ~~ m{^ 0 <same> ** 2..4}` never matched; Rakudo
  matches with four `same` captures.
  `t/regression/bounded-quantifier-zero-width-item.raku`.
- **A grammar's parse target is its first positional argument** (e9aaf6b3)
  (rakudo#2791): `Chord.subparse(:rule<bare-key>, 'C#')` parsed the `:rule`
  Pair, so the issue's `<!before \S>` rules never matched. An undefined
  target dies, as Rakudo's does.
  `t/regression/grammar-parse-named-before-target.raku`.
- **An actions class's FALLBACK is called, and `ws` actions fire**
  (2137a05d) (rakudo#2059): FALLBACK was never called, `rule TOP { a b }`
  never fired `method ws`, and `<.q>` over `token q { 'q' }` never fired
  `method q`. `t/regression/grammar-actions-fallback-and-ws.raku`.
- **`\c[NAME]` in a regex takes the name in any case** (d45f616e)
  (rakudo#5799): the lower-case name resolved to nothing, so
  `/o\c[combining ring above]/` failed against "o̊" and
  `/.\c[combining ring above]/` matched it.
  `t/regression/regex-char-names-any-case.raku`.
- **Every routine is its own `.WHICH`** (9e693ab0): two closures of one
  `sub { … }` had one identity, so a Set of them held one element.
  `t/regression/my-token-closes-over-its-frame.raku`.
- **`return` takes its value as a list operator** (f9028851) (rakudo#2129):
  `return 0 or 5` returned 5; it is `(return 0) or 5`, and answers 0.
  `t/regression/return-is-a-list-operator.raku`.
- **An operator named by a quoted word is that word** (ce72763f)
  (rakudo#1264): `sub infix:«"z"» (…)` declared an operator named `"z"`,
  quotes and all, so `42 z 42` gave 42 and the operator was invisible to
  EVAL. `t/regression/operator-name-quoted-guillemet-word.raku`.
- **An item in a `has (…)` list keeps its own traits** (f102e0f5)
  (rakudo#2078): `has ($.z is rw = 42)` declared a read-only `$.z` with no
  default. `t/regression/has-list-item-traits.raku`.
- **A gather block's last statement is sunk** (b590c1ec):
  `gather { take 1; Failure.new }` handed back [1], where Rakudo throws, and
  `gather { Sinker.new }` took nothing.
  `t/regression/for-as-block-value-is-not-sunk.raku`.
- **A sigilless name in a list assignment holds a list as it is** (d77e1185)
  (rakudo#5027): `my (\a, \b) = (1, 2), 3` made a `$(1, 2)`.
  `t/regression/sigilless-names-token-and-destructure.raku`.
- **A hyper method call interpolates in a string** (6b8c1d1a) (rakudo#1460):
  `"%add».sort.map(*.fmt("%s %s"))"` stayed literal text.
  `t/regression/interpolated-hyper-method-chain.raku`.
- **An anonymous state variable in a string's `{ … }` is fresh each time**
  (7e5a643e) (rakudo#1961): `"count {$++}" for ^3` counted 0 1 2, where
  Rakudo prints count 0 three times. `t/regression/string-block-anonymous-state.raku`.
- **A leap second stays one in a timezone of odd seconds** (bfd530f9)
  (rakudo#2097): `DateTime.new('2016-12-31T23:59:60Z').in-timezone(514)` gave
  00:08:34, where Rakudo keeps the second, 00:08:60.
  `t/regression/leap-second-in-timezone.raku`.
- **A built-in exception's `.raku` is Rakudo's** (eb482a9f) (rakudo#2434): it
  showed a `message => …` attribute, which is a method in Rakudo, and
  X::Numeric::DivideByZero's attributes in the order they were thrown.
  `t/regression/dd-failure-not-sunk-and-exception-raku.raku`.
- **`CALLERS::<$_>` reads a calling routine's `$_` under `--exe` too**
  (62ada605): a compiled program answered Any, where the interpreted one
  answered the routine's `$_`. A program that reads its callers' lexicals
  keeps its routines interpreted. `t/regression/callers-call-form.raku`.
- **`return not $x` returns the negation** (e89cc6a7): it returned Nil since
  f9028851, with `not $x` left as a statement of its own. Found with
  `GLOBAL::<x>:delete`, whose `:delete` was a sunk Pair that deleted nothing,
  and `$x or last LOOP`, which left the innermost loop instead of LOOP.
  `t/regression/strict-separation-false-positives.raku`.
- **A picked member of a Str-valued enum keeps its value** (2eafbb23)
  (rakudo#2165): `enum Chain (A => "T", …); Chain.pick.value` was 0, as was
  every `.roll`. `t/regression/enum-how-add-enum-value.raku`.
- **X::Method::NotFound and X::Numeric::DivideByZero made with `.new` have
  their message** (251842c2): `.message` was Any, and a second `.new` of one
  died "Default constructor … only takes named arguments".
  `t/regression/exception-new-message.raku`.
### Fixed: crashes

- **A regex value that names itself is a recursive pattern, not a crash**
  (041789fd) (rakudo#3775). `my $r; $r = / a <$r>? /` matches "aaa" in
  Rakudo; in Raku++ every recursive spelling took SIGBUS before a character
  was matched: `<$r>`, `<x=$r>`, a bare `$r`, and two values naming each
  other. Expanding `<$r>` pasted $r's source, and its own `<$r>` pasted it
  again. Now a value's reappearance inside its own expansion is called when
  the match reaches it, as Rakudo resolves it. A rule that calls itself
  before consuming anything (`/ <$r> a /`, `token TOP { <TOP> a }`) cannot
  end: Rakudo hangs, and Raku++ throws X::Recursion where it took SIGBUS.
  `t/regression/regex-value-naming-itself.raku`.
- **An index no machine could reach is refused, not grown into**
  (147d0b95) (rakudo#5835). `my $b; $b[1e18] = "meow"` grew the array a slot
  at a time until memory ran out: eight gigabytes in six seconds. So did
  `.ASSIGN-POS`, `.BIND-POS`, a slice, a `:=` bind and a nested `[0][i]`.
  Rakudo refuses each with "Unable to allocate an array of N elements"
  (X::AdHoc), and Raku++ now does too. It refuses when the slots alone would
  outweigh the machine's memory, so an ordinary large array still grows.
  `t/regression/huge-array-index-refused.raku`.
- **A user class may be called Rat** (7fc5b00b) (rakudo#5537).
  `class Rat is Quadruped { … }; Rat.new` took SIGBUS. Whether a class does
  Rational was also asked by its name, and for a class called Rat the core
  Rat answered. Its `.new` then boxed a `Rat.new` that was itself.
  `t/regression/user-class-named-rat.raku`.
- **Two ways a `react` never ended** (b4f6e472) (rakudo#4853, rakudo#3686).
  - `whenever $ch.closed { done }` never fired when the channel closed
    after the react started. Closing kept `.closed` for `await`, but never
    ran the continuations a react listens through. The reader behind
    `whenever $ch` took the last value of a closed channel without keeping
    `.closed` at all.
  - A `whenever $promise` body waits until no other body of its react runs,
    and that reader counted as running for as long as it polled. So beside
    an empty channel, the promise's body never ran.

  The issue's own program, a react re-entered once per value until a
  promise says stop, now reads all 100 values from each channel. That is
  what rakudo#3686 asks for: Rakudo 2026.09 loses values there and Raku++
  used to hang. `t/regression/react-channel-closed-and-turns.raku`.
- **A generator sequence stops at an excluded endpoint however it is read**
  (7dc4809d) (rakudo#3881): `say { $++ * ($ *= -1) } ...^ 21` never ended.
  The 21 was dropped and the walk went on for `.gist`, `.join` and an array
  assignment, though `.elems` and indexing stopped. A Range on the right of a
  lone generator is now a list, its first element the endpoint and the rest
  following, as in Rakudo: `{ $++ } ... ^5` is (0 1 2 3 4).
  `t/regression/sequence-generator-exclusive-endpoint.raku`.

### Behaves like Rakudo

- **The `RakuAST::` names need no pragma** (e60a6163) (rakudo#6134, #6137).
  Rakudo 2026.08 refused them under 6.d without `use experimental
  :rakuast`, and Raku++ followed; 2026.09 refuses nothing. `RakuAST::Sub.new(…).EVAL`
  runs, and `::('RakuAST::Node')` is the type where it was a Failure. The
  pragma is accepted and changes nothing. `t/regression/rakuast-needs-no-pragma.raku`.
- **Error wording the closed issues settled** (13902c52):
  - `42.abs += 42` says "Cannot modify an immutable Int (42)", naming the
    method's answer, where it said "Target is not assignable" (rakudo#1987).
  - `MixHash.grab` answers a Failure, ".grab is not supported on a MixHash"
    (rakudo#2109).
  - A subset return type names the value it refuses: "got Rat (1.8)"
    (rakudo#2595).
  - `(Inf .. 5) / 0` says "divide Inf by zero"; the empty Range printed as
    nothing (rakudo#3598).
  - A busy port says "address already in use (address: localhost:11111)",
    an X::AdHoc (rakudo#1266).

  `t/regression/error-messages-rakudo-settled.raku`.

- **`$.x` in a method is `$(self.x)`** (4f507a0f) (rakudo#5908, rakudo#6113,
  rakudo#4463). Through a read-only accessor, `$.b = 666` refuses with
  "Cannot modify an immutable Int (42)", and `$.n++`, `$.n += 1` or an
  autovivifying `$.h<k> = 1` change only a copy, as in Rakudo. All of them had
  written the attribute. `is rw`, the class's `is rw` and a user method that
  is rw still write. Assigning from outside (`$o.b = 5`) names the value in
  its refusal too. `t/regression/dot-attr-through-read-only-accessor.raku`.
- **Two multi candidates that tie are ambiguous with sigilless, `is raw` or
  `is copy` parameters too** (401e19e8) (rakudo#1286): `multi f(Int \a, Any
  \b)` beside `multi f(Any \a, Int \b)` quietly took the first for
  `f(42, 42)`, where Rakudo dies X::Multi::Ambiguous.
  `t/regression/multi-tie-sigilless-raw-copy.raku`.
- **Assigning to a sigilless name bound to an object with STORE calls it**
  (b66ccda3) (rakudo#3292): `my \foo = HistoryVar.new; foo = 1` died "Cannot
  modify an immutable HistoryVar". `t/regression/sigilless-object-store.raku`.
- **A multi's `where` check reads its defaults in the candidate's scope**
  (6eef76fd): Data::Dump's every `Dump(…)` was X::Multi::NoMatch since
  v5.3.0. Checking an unsupplied named's `where` evaluated the defaults before
  it in the caller's scope, where Data::Dump's module-level `my %defaults` is
  not. Found by the module battery.
  `t/regression/multi-where-default-in-candidate-scope.raku`.
- **`try EXPR` is a frame of its own, as in Rakudo** (ecaee71c): Log::Async's
  `trace` died "No such method 'file' for invocant of type 'Any'". It drops
  the `try`'s frame as the exception's own and reads the caller from the
  next, and the statement form had none. The short printout now leaves out
  the frames of `try`, `do` and `quietly` blocks, as Rakudo's does; a warning
  still names its block. Found by the module battery.
  `t/regression/try-statement-is-a-block-frame.raku`.
- **`&postcircumfix:<[ ]>` and `&postcircumfix:<{ }>` are routines**
  (22520fae) (rakudo#2000): both were Any, so `&postcircumfix:<{ }>(%h, 'a')`
  died "Undefined routine". Named arguments are the adverbs (`:exists`,
  `:delete`, `:kv`), and a third operand is assigned, as Rakudo's assign
  candidate does. `t/regression/postcircumfix-routines-by-name.raku`.
- **`eager` runs an unconditioned `Seq.from-loop` to its `last`** (c47aa1ea)
  (rakudo#1692): `eager Seq.from-loop: { $i++; last if $i > 5; $i }` died
  "Cannot eager a lazy list". A body whose value is Empty adds nothing.
  `t/regression/eager-from-loop-runs-to-last.raku`.
- **An Attribute with a role mixed in reaches the role's attributes**
  (24256603): AttrX::Mooish's trait died "No such method
  'get_attribute_for_usage' for invocant of type 'Attribute'" on its first
  option. `.^get_attribute_for_usage('$!name')` answers the role's
  attribute, and its `.get_value` and `.set_value` read and write the slot.
  Past this, AttrX::Mooish needs Rakudo's MOP internals. Found by the module
  battery. `t/regression/mixed-in-attribute-meta-access.raku`.
- **The reduce metaop over a built-in operator's Callable folds it**
  (1f205847) (rakudo#4128): `[[&infix:«>»]] 6, 5, 4` died "Unsupported
  operator", where Rakudo folds ((6 > 5) > 4) and answers False.
  `t/regression/reduce-with-chaining-operator.raku`.
- **A compiled program calls a built-in infix by its name** (eca56b5a):
  `infix:<~>("a", "b")` died "Undefined routine 'infix:<~>'" in a `--exe`
  binary. It is the operator, as `&infix:<~>` is.
  `t/regression/hyper-reduce-one-operand.raku`.
- **A `my token` returned from a sub closes over its call** (9e693ab0)
  (rakudo#6810): `<{$re}>` inside it died "Variable '$re' is not declared"
  once the sub had returned `&FULLRE`. Each call's routine is its own, by
  `===` too; Rakudo 2026.09 shares one between the calls.
  `t/regression/my-token-closes-over-its-frame.raku`.
- **`self` in a standalone token's code is its cursor** (edc8bf39)
  (rakudo#3410): `token { 'a' <?{ say self.pos }> 'a' }` died "'self' used
  where no object is available". `t/regression/self-in-token-code-block.raku`.
- **A ｢…｣ quote in a regex holds any bracket, and `<&?BLOCK>` recurses**
  (1ea5c984) (rakudo#3775): `[｢{｣ <&?BLOCK>? ｢}｣]?` died "Unable to parse
  regex; couldn't find final ')'", and `<&?BLOCK>` matched nothing.
  `t/regression/regex-corner-quotes-and-block-recursion.raku`.
- **An anonymous class that opens a statement is a term like any other**
  (70ee4e8b, 9badbec4) (rakudo#1360, rakudo#2078): `class { … } for ^5` died
  "Missing ';' before 'for'", and `class { … }.new.z = 42` "unexpected
  operator in term position".
  `t/regression/anon-class-statement-takes-infix-and-modifier.raku`.
- **A `for` that is its block's value does not sink its iterations**
  (6fcd9181) (rakudo#1360): `{ Failure.new for ^5 }()` threw, where Rakudo
  answers Nil; compiled code agrees.
  `t/regression/for-as-block-value-is-not-sunk.raku`.
- **A block phaser takes a statement modifier** (b1040540) (rakudo#2243):
  `LEAVE { say 42 } if False;` was refused at parse time. The phaser stays
  declared, as a declaration does. `t/regression/phaser-block-takes-statement-modifier.raku`.
- **A name that starts with `token`, `rule` or `regex` is a name** (d77e1185)
  (rakudo#5027): `-> \labels_, \token_ { … }` failed to parse, `token_ {`
  read as a regex declaration.
  `t/regression/sigilless-names-token-and-destructure.raku`.
- **A `&` item in a declaration list takes a signature** (953adf0a)
  (rakudo#2336): `my (&op:(Int, Int --> Int)) = …` failed to parse, and
  `my (&op) = …; op(2, 3)` was "Undeclared routine".
  `t/regression/declaration-list-routine-items.raku`.
- **`enum E does R <a b>` parses** (73357985) (rakudo#3376), and an enum's
  archetypes say it is composalizable, as Rakudo's 1/0 integers.
  `t/regression/enum-does-role-and-archetypes.raku`.
- **`say *>>.fmt("%02x")(42)` parses** (fdaa60ec) (rakudo#5901): a tight
  hyper method call on `*` after a list operator is a WhateverCode.
  `t/regression/whatever-hyper-method-after-listop.raku`.
- **Every `;` segment of a parenthesised list takes a statement modifier**
  (f8b5555c): `($n++; last if $n > 3) for 1..*` was "Two terms in a row".
  `t/regression/paren-segment-statement-modifier.raku`.
- **A declaration called at once takes a postfix chain** (f9028851):
  `sub foo { 7 }().say` printed (Any). `t/regression/return-is-a-list-operator.raku`.
- **`dd` answers Nil** (eb482a9f) (rakudo#2434): `dd $x % 0e0` printed the
  Failure and then threw it, because dd handed its argument back to be sunk.
  `t/regression/dd-failure-not-sunk-and-exception-raku.raku`.
- **`Parameter.twigil`** (41150ad1) (rakudo#2275), and `.sigil` of a sigilless
  `\g` is `\` and of a capture `|c` is `|` (they were `g` and `c`).
  `t/regression/parameter-twigil-and-sigil.raku`.
- **The atomic family is complete** (ea466f67) (rakudo#6873): `⚛−=` with a
  Unicode minus parsed as nothing, and `&prefix:<⚛>`, `&prefix:<++⚛>`,
  `&postfix:<⚛++>` and their kin were Any. `$x ⚛== 1`, which Rakudo reads as
  a write, is refused with a pointer to `⚛$x == 1`.
  `t/regression/atomic-callables-and-unicode-minus.raku`.
- **`CALLERS::('$_')`, `CALLER::(…)`, `M::(…)`** (ef2e778b) (rakudo#2639):
  they called a sub named `CALLERS::`. CALLERS reaches a routine's own `$_`,
  not a `given` or `for` topic, as in Rakudo.
  `t/regression/callers-call-form.raku`.
- **`role A[::T] does array[T]` declares** (112a0cdb) (rakudo#4361): it died
  "Invalid typename 'array'". `t/regression/role-does-native-array-generic.raku`.
- **`name{…}` subscripts what the call answers** (fcc6d577): `T{"max-rss"}`
  passed the block to T. `name {…}`, with a space, still passes it.
  `t/regression/brace-subscript-after-routine-call.raku`.
- **Code Rakudo refuses is refused** (R10; the battery scan against v5.8.0 is
  green, 2,775 module files):
  - Test's and NativeCall's routines with no `use` of the module (1f3377ea)
    (rakudo#1631, #3607, #4967, #1612, #2191). A module the program `use`s
    that may re-export them keeps them, and so does a caller's `use` for an
    EVAL.
  - `use v6.e` without `.PREVIEW` (9f94cac7) (rakudo#2783).
  - `is cached` without `use experimental :cached` (2fe0aeeb).
  - `has $.x!`, `sub ($!x)` outside a method, `-> < 1/2>`, a default on a
    required positional, a placeholder in a method, and a CStruct with no
    attributes (8f7ac1df) (rakudo#1427, #1828, #1426, #2320, #2673).
  - A Regex given to `substr`, `self."!$name"()`, `Lock.new.protect({})`,
    `(0...∞).chrs` (6de9d27b) (rakudo#3802, #1644, #2742), and a `return` in
    a mainline `start` block (ce54f928) (rakudo#2753).
  - An unquoted `,`, `=`, `-`, backtick or non-word symbol in a regex
    (d3059bf5) (rakudo#3775, #4927).
  - A phaser's name called like a routine, `FIRST($x)` (f1491b3e).
  - `42:foo` and `say 1, 2, and 3` (06212d97) (rakudo#1476, #3426, #4107).

  The tests are `t/regression/use-v6e-needs-preview.raku`,
  `is-cached-needs-experimental.raku`,
  `test-and-nativecall-routines-need-use.raku`,
  `declarations-rakudo-refuses.raku`, `calls-rakudo-refuses.raku`,
  `return-in-mainline-start-block.raku`,
  `regex-unquoted-symbols-refused.raku`,
  `phaser-name-called-like-routine.raku` and
  `adverb-on-literal-and-word-infix-after-comma.raku`.
- **`method !q { … }` declares `!q`** (6de9d27b): the `q {…}` lexed as a
  quote, so the method never existed.
### Platforms and packaging

### Tools

- **battery-scan, battery-use-smoke and cpp-build-check run again**
  (a213ccad): each passed `:env` a List of Pairs, which no longer binds to
  `:%env`, so the battery scan died on its first file.

### Left open
