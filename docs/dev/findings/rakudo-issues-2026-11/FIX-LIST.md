# Fix list — Raku++ against the Rakudo issue tracker (2026-11-12)

Generated from [review.tsv](review.tsv) by `harness/fixlist.py`; the method and the curated summary are in [../RAKUDO-ISSUES-2026-11.md](../RAKUDO-ISSUES-2026-11.md).
Every case where Raku++ crashes or hangs (PPCRASH), lacks something Rakudo has (MISSING), gives a wrong answer (PPBUG), or accepts code Rakudo rightly refuses (LAX), grouped by area.

- **✓** — re-run by hand on 2026-11-12 (the original snippet or a reduction of it) and reproduced.
- No mark — a reviewer verdict from the recorded outputs only; confirm before fixing.
- *(open)* — the Rakudo issue is still open, so Rakudo itself may be wrong; the note says what the issue expects.
- Notes quote both engines as they behaved on Raku++ 5.7.0-111-g56aec789 and Rakudo v2026.09.

### PPCRASH (9)

**perf** (4)

- [#1416](https://github.com/rakudo/rakudo/issues/1416) ✓ — `my $n := 42; for ^30_000_000 { my $ = nqp::if($n,1) }` times out in Raku++ (>10s); Rakudo finishes in ~1.7s
- [#1416](https://github.com/rakudo/rakudo/issues/1416) ✓ — 30M-iteration loop with nqp::if(nqp::isne_i($n,0),1) times out in Raku++; Rakudo ~1.5s
- [#2844](https://github.com/rakudo/rakudo/issues/2844) ✓ — `race for ^100000 {...}` with 100k-element fib arrays: Rakudo finishes in 0.2s; Raku++ exceeds 10s timeout
- [#5835](https://github.com/rakudo/rakudo/issues/5835) *(open)* ✓ — `my $b; $b[1e18]="meow"` Rakudo exits at once with MoarVM panic (issue wants X::OutOfMemory); Raku++ hangs past 10s timeout

**concurrency** (3)

- [#3686](https://github.com/rakudo/rakudo/issues/3686) *(open)* ✓ — Repeated react over two Channels: Raku++ hangs (timeout) where Rakudo finishes (printing the lossy [82 45]); issue expects [100 100]
- [#4853](https://github.com/rakudo/rakudo/issues/4853) ✓ — react with Supply.interval plus Channel.closed: Raku++ never terminates (keeps printing 2) where Rakudo prints 2,5,Data and exits
- [#5868](https://github.com/rakudo/rakudo/issues/5868) *(open)* ✓ — `(^100_000).race.map` calling `Foo.new but role :: { method get-value { $value } }` segfaults in Raku++ (exit -11, no output); Rakudo dies with the reported race error

**oo** (1)

- [#5537](https://github.com/rakudo/rakudo/issues/5537) *(open)* ✓ — user `class Rat is Quadruped` then `Rat.new()` crashes Raku++ (exit -10, no output); Rakudo continues and fails only at `my Rat $r = 2.2`

**regex** (1)

- [#3775](https://github.com/rakudo/rakudo/issues/3775) ✓ — `$regex = /… <$regex> …/` (bare `…` in regex) kills Raku++ silently with exit -10; Rakudo reports "Unrecognized regex metacharacter"


### MISSING (28)

**oo** (5)

- [#2275](https://github.com/rakudo/rakudo/issues/2275) ✓ — `Parameter.twigil` not implemented: No such method 'twigil' in Raku++; Rakudo returns the (empty) twigil
- [#4361](https://github.com/rakudo/rakudo/issues/4361) — `role A[::T] does array[T] { }` dies Invalid typename array in Raku++; Rakudo accepts (prints Alive)
- [#4517](https://github.com/rakudo/rakudo/issues/4517) — `constant e = Array[Array[Int]]();` Raku++ dies No such method CALL-ME for Array[Array[Int]] (coercion type from parametric type); Rakudo accepts
- [#4517](https://github.com/rakudo/rakudo/issues/4517) — `my Array[Array[Int]](Array[Array[Int](Array[Int]())]()) $i` Raku++ parse error expected variable; Rakudo accepts nested coercion types in declaration
- [#4780](https://github.com/rakudo/rakudo/issues/4780) *(open)* ✓ — `&postcircumfix:<⸨ ⸩>.prec("prec")` gives No such method prec for Sub in Raku++; Rakudo has Routine.prec

**rakuast** (5)

- [#5479](https://github.com/rakudo/rakudo/issues/5479) — `q｢grammar G {...}｣.AST.EVAL` Raku++: .AST does not yet build a view for a grammar rule; Rakudo runs fine
- [#5721](https://github.com/rakudo/rakudo/issues/5721) — `"=rakudoc A<Fallback or XXX>".AST.rakudoc.head.paragraphs.head.say` prints just `RakuAST::Doc::Paragraph.new` in Raku++; Rakudo prints the full Doc::Markup with atoms and meta
- [#5798](https://github.com/rakudo/rakudo/issues/5798) — `.AST.say` of a RakuDoc string gives empty `RakuAST::StatementList.new` in Raku++; Rakudo returns the Doc::Block tree
- [#5978](https://github.com/rakudo/rakudo/issues/5978) — `$test.AST.DEPARSE` on a grammar with a rule: Raku++ says .AST does not yet build a view for a grammar rule; Rakudo deparses it
- [#5991](https://github.com/rakudo/rakudo/issues/5991) — `.AST` of a `unit grammar` with `regex x {:my $v; X}` dies "does not yet build a view for a grammar rule" in Raku++; Rakudo returns an AST that deparses fine

**modules** (4)

- [#1513](https://github.com/rakudo/rakudo/issues/1513) ✓ — `use Telemetry` fails in Raku++ (module not found); Rakudo has core Telemetry (and snippet also needs nqp)
- [#3844](https://github.com/rakudo/rakudo/issues/3844) *(open)* ✓ — `use Telemetry` (core module, T<max-rss>) not found by Raku++; Rakudo loads it
- [#5144](https://github.com/rakudo/rakudo/issues/5144) ✓ — `use Telemetry` (core module, `T{"max-rss"}`) cannot be found in Raku++; Rakudo runs and shows RSS growth
- [#5628](https://github.com/rakudo/rakudo/issues/5628) — `use RakuAST::Deparse::Highlight` (shipped with Rakudo) is not found by Raku++; Rakudo highlights `say foo` without error

**io** (3)

- [#1879](https://github.com/rakudo/rakudo/issues/1879) — `IO::Socket::INET.new(:family(PF_INET6))` dies in Raku++ (`Socket family 30 is not supported: only PF_INET`); Rakudo does IPv6 sockets and Async listen on ::1
- [#5359](https://github.com/rakudo/rakudo/issues/5359) *(open)* — `IO::Socket::Async.listen-path` missing in Raku++ (No such method); Rakudo has it
- [#5359](https://github.com/rakudo/rakudo/issues/5359) *(open)* — `IO::Socket::Async.connect-path` missing in Raku++ (No such method); Rakudo has it

**cli** (1)

- [#5546](https://github.com/rakudo/rakudo/issues/5546) *(open)* — `use trace` pragma ignored by Raku++ (no trace lines on stderr); Rakudo prints `2 (-e line 1)` and the statement

**concurrency** (1)

- [#5358](https://github.com/rakudo/rakudo/issues/5358) *(open)* — `IO::Socket::Async.listen-path` not implemented in Raku++ (No such method); Rakudo has it (fails only on socket path permission)

**control** (1)

- [#4351](https://github.com/rakudo/rakudo/issues/4351) *(open)* ✓ — `say FIRST 2` dies 'Undefined routine FIRST' in Raku++; Rakudo prints 2 (FIRST phaser as value)

**dispatch** (1)

- [#4095](https://github.com/rakudo/rakudo/issues/4095) *(open)* — `trait_mod:<returns>(&func, Str)` gives Undefined routine in Raku++; Rakudo has it (issue wants no redeclaration error)

**hashes** (1)

- [#2109](https://github.com/rakudo/rakudo/issues/2109) — `MixHash.new(<a b c>).kxxv`: Raku++ gives No such method 'kxxv'; Rakudo returns a Failure not supported on MixHash

**internal** (1)

- [#1742](https://github.com/rakudo/rakudo/issues/1742) — `use nqp; THROW(nqp::const::CONTROL_TAKE, ...)` gives Undefined routine THROW; nqp-level internals

**lists** (1)

- [#2000](https://github.com/rakudo/rakudo/issues/2000) ✓ — `(%hash, /@path).reduce: &postcircumfix:<{ }>` Raku++: No such method 'reduce' for List

**nativecall** (1)

- [#3698](https://github.com/rakudo/rakudo/issues/3698) *(open)* — `sub addch(int32 $ --> int) is native` Rakudo warns 'returning type erroneous'; Raku++ emits no warning

**pod** (1)

- [#5798](https://github.com/rakudo/rakudo/issues/5798) — `"=begin rakudoc\n=begin MyBlk\nstuff\n=end Myblk...".AST` mismatched =end must error; Raku++ returns empty StatementList with no error

**scope** (1)

- [#2639](https://github.com/rakudo/rakudo/issues/2639) — `CALLERS::('$_')` lookup: Raku++ dies Undefined routine 'CALLERS::'; Rakudo runs (prints $cwd = Any; issue expects foo)

**syntax** (1)

- [#6873](https://github.com/rakudo/rakudo/issues/6873) *(open)* ✓ — `$x ⚛== 1` is a parse error in Raku++ (unexpected operator ==); Rakudo parses it (wrongly as a write); issue wants an atomic comparison


### PPBUG (165)

**syntax** (34)

- [#1264](https://github.com/rakudo/rakudo/issues/1264) ✓ — `sub infix:«"z"»` then `42 z 42`.EVAL dies "Two terms in a row"; Rakudo prints here
- [#1360](https://github.com/rakudo/rakudo/issues/1360) ✓ — `class { method sink {...} } for ^5` is a parse error in Raku++ ("Missing ';' before 'for'"); Rakudo accepts a statement modifier after an anon class block
- [#1381](https://github.com/rakudo/rakudo/issues/1381) *(open)* — Sunk pointy block `quietly -> $a, $b { say $a + $b }` is run by Raku++ and prints 0; Rakudo does not execute it
- [#1532](https://github.com/rakudo/rakudo/issues/1532) *(open)* — `dd [:%::($foo)]` parse error "expected ]" in Raku++; Rakudo compiles it (though yields odd "::($foo)" key)
- [#1562](https://github.com/rakudo/rakudo/issues/1562) *(open)* — `«a\c[NO-BREAK SPACE]bc d»` is a parse error in Raku++ (final ] not found); Rakudo gives ("a bc", "d")
- [#1616](https://github.com/rakudo/rakudo/issues/1616) *(open)* ✓ — `say &[RRRRRRRRZZZ...RRRRRRRRR,]` gives Undefined routine in Raku++; Rakudo returns a Callable block
- [#1616](https://github.com/rakudo/rakudo/issues/1616) *(open)* ✓ — `&[RRRRRRRRZZZ..RRRRSSSSSSSS+]` gives Undefined routine in Raku++; Rakudo builds the callable
- [#1705](https://github.com/rakudo/rakudo/issues/1705) — `say [(elem)] 2334` Rakudo prints True (one-arg reduce); Raku++ prints 2334
- [#2044](https://github.com/rakudo/rakudo/issues/2044) — `.say for :(&s :(), *%_), method (&s :()) {}.signature` Raku++ prints `($ (), *%_)` and `(Mu &s:: $ (), *%_)`; Rakudo prints `(&s:: $ (), *%_)` twice
- [#2078](https://github.com/rakudo/rakudo/issues/2078) *(open)* ✓ — `class { has $.z is rw; }.new.z = 42` is a parse error in Raku++ (unexpected operator '='); Rakudo accepts anonymous class term with `.new.z = 42`
- [#2078](https://github.com/rakudo/rakudo/issues/2078) *(open)* ✓ — `class { has ($.z is rw); }.new.z = 42` is a parse error in Raku++; Rakudo runs it without error
- [#2129](https://github.com/rakudo/rakudo/issues/2129) *(open)* ✓ — `sub foo { return 0.flip or die "bar" }().say` prints `0` in Rakudo, `(Any)` in Raku++
- [#2132](https://github.com/rakudo/rakudo/issues/2132) *(open)* — `()»()` Rakudo accepts silently; Raku++ gives Parse error unexpected '»' in term position
- [#2243](https://github.com/rakudo/rakudo/issues/2243) *(open)* ✓ — `LEAVE { say 42 } if False;` is a parse error (Missing ; before if) in Raku++; Rakudo parses it
- [#2243](https://github.com/rakudo/rakudo/issues/2243) *(open)* ✓ — `LEAVE { blah } if foo;` parse error in Raku++; Rakudo parses and reports only undeclared routines
- [#2336](https://github.com/rakudo/rakudo/issues/2336) *(open)* ✓ — `my (&op:(Int, Int --> Int)) = (sub ...)` is a parse error in Raku++ ("expected )"); Rakudo accepts a signature-constrained &-variable in a declarator list
- [#2368](https://github.com/rakudo/rakudo/issues/2368) — `my $n=3; say ^10 .head:$n` prints (0 1 2) in Raku++; Rakudo prints 0 (colonpair parse)
- [#2422](https://github.com/rakudo/rakudo/issues/2422) *(open)* — `sub postcircumfix:<♥ ♥>( or ) { }; say 42♥* ~ ++$♥` dies "Too many positionals; expected 0 but got 2" in Raku++; `( or )` capture signature should accept anything
- [#2948](https://github.com/rakudo/rakudo/issues/2948) — `my $x:bar<2> = 10; say $x:bar(1+1)` works in Rakudo (10); Raku++ dies Variable '$x' is not declared
- [#3376](https://github.com/rakudo/rakudo/issues/3376) ✓ — `enum All-gods does Norse-gods <Ammon>;` is a parse error in Raku++ ("Missing required term after infix"); Rakudo accepts it
- [#3653](https://github.com/rakudo/rakudo/issues/3653) — `say (*+2).raku` Rakudo prints (*+2).raku; Raku++ prints WhateverCode.new
- [#4119](https://github.com/rakudo/rakudo/issues/4119) *(open)* — `sub infix:<ab>($a,$b) is assoc<chain> {True}; say 1 !ab 2`: Raku++ prints 1 then Undefined routine 'ab'; expected False (negated custom chain op); Rakudo compile error
- [#4128](https://github.com/rakudo/rakudo/issues/4128) *(open)* ✓ — `reduce &[>], 6, 5, 4` and `reduce &infix:«>»` give False in Raku++ (Rakudo True, chain assoc); `[[&infix:«>»]]` dies Unsupported operator
- [#4159](https://github.com/rakudo/rakudo/issues/4159) — `class Foo:ver{VER} {}` is a parse error in Raku++ ("Missing ';' before '{'"); Rakudo accepts a `:ver{...}` colonpair in the class name
- [#4159](https://github.com/rakudo/rakudo/issues/4159) — `class Foo:auth{auth} {}` is a parse error in Raku++ ("Missing ';' before '{'"); Rakudo accepts a `:auth{...}` colonpair
- [#4757](https://github.com/rakudo/rakudo/issues/4757) *(open)* ✓ — `say 2 R- 3 R-4` gives 3 on Raku++ (left-assoc); R- is right-assoc so -1 expected, same as `[R-] (2,3,4)` which gives -1; reduce(&infix:<R->) gives 3 on both (issue expects -1)
- [#5027](https://github.com/rakudo/rakudo/issues/5027) ✓ — `my ( Map \labels,\_) = reduce(-> List \acc_, Int \token_ {...}, ...)` fails to parse in Raku++ ("expected { (got '_')"); Rakudo runs it
- [#5027](https://github.com/rakudo/rakudo/issues/5027) ✓ — Pointy block with sigilless params like `Int \token_` fails to parse in Raku++ ("expected { (got '_')"); Rakudo runs it
- [#5027](https://github.com/rakudo/rakudo/issues/5027) ✓ — `-> Map \labels_, Int \token_ { ... }` parse error "expected { (got '_')" in Raku++; Rakudo runs it and prints the Map
- [#5320](https://github.com/rakudo/rakudo/issues/5320) *(open)* — `dd gather .take given 1 or 2 or 3` gives `(1, 2, 3).Seq` in Raku++ (junction autothreaded); Rakudo gives `(any(1, 2, 3),).Seq`
- [#5320](https://github.com/rakudo/rakudo/issues/5320) *(open)* — `dd gather .take for 1 or 2 or 3` gives `(1, 2, 3).Seq` in Raku++; Rakudo now gives `(any(1, 2, 3),).Seq` (no autothreading in `for`)
- [#5901](https://github.com/rakudo/rakudo/issues/5901) ✓ — `say *>>.fmt("%02x")(42)` is a parse error in Raku++ (unexpected '>>' after `*`); Rakudo prints `(2a)`
- [#6128](https://github.com/rakudo/rakudo/issues/6128) ✓ — `say [RZ] (1,2),(3,4)` Rakudo gives ((3 1) (4 2)); Raku++ dies Undeclared name RZ (R-prefixed Z not supported in reduction)
- [#6128](https://github.com/rakudo/rakudo/issues/6128) ✓ — `say (1,2) RZ (3,4)` Rakudo gives ((3 1) (4 2)); Raku++ dies Undefined routine RZ (R metaop on Z unsupported)

**oo** (27)

- [#1285](https://github.com/rakudo/rakudo/issues/1285) ✓ — `Coercer[Int()].new.c: 42.12` returns 42 in Rakudo; Raku++ dies with type check "expected Int but got Rat" (coercion type in role parameter not applied)
- [#1288](https://github.com/rakudo/rakudo/issues/1288) *(open)* — Proxy STORE block `{ $store = @_[1]² }` bound to `$!foo` sees no `@_[1]` in Raku++ (uninitialized warning, prints 0); Rakudo prints 121
- [#1959](https://github.com/rakudo/rakudo/issues/1959) ✓ — `$a does 'foo'` on a Set: Rakudo mixes in and prints foo; Raku++ dies "Cannot use 'does' operator on a Set, did you mean 'but'?"
- [#1959](https://github.com/rakudo/rakudo/issues/1959) ✓ — `my $a := set(1); $a does 'foo'` works in Rakudo (prints 1); Raku++ dies "Cannot use 'does' operator on a Set"
- [#2078](https://github.com/rakudo/rakudo/issues/2078) *(open)* ✓ — `has ($.z is rw = 42)` loses the default in Raku++ (dd prints Any); Rakudo gives `$!z = 42`
- [#2143](https://github.com/rakudo/rakudo/issues/2143) — `Distribution::Resource.^mro` gives ((Any) (Mu)) in Raku++; Rakudo ((Resource) (Any) (Mu))
- [#2165](https://github.com/rakudo/rakudo/issues/2165) *(open)* ✓ — `enum Chain ( A => "T", G => "C", T => "A")` then `.pick.value` gives 0 in Raku++ (Rakudo gives "C"); also `Metamodel::EnumHOW.add_enum_value` is missing
- [#2174](https://github.com/rakudo/rakudo/issues/2174) ✓ — `class {}.new does Foo` fails with "Undefined routine 'does'" in Raku++; Rakudo runs fine
- [#2307](https://github.com/rakudo/rakudo/issues/2307) *(open)* — `Date.today."Date"()` is not identical to its invocant in Raku++ (`=:=` False) while Rakudo gives True for all six types
- [#2663](https://github.com/rakudo/rakudo/issues/2663) *(open)* ✓ — Mixed-in role private methods: `Puppy.greet`'s `self!bow` calls a role's `!bow` in Raku++ ("<takes a bow>"), Rakudo calls Puppy's own ("Bow wow wow!")
- [#2870](https://github.com/rakudo/rakudo/issues/2870) — `$match.^methods.perl` prints `()` in Raku++: Match.^methods is empty; Rakudo lists methods (then dies on NQPRoutine.raku)
- [#2889](https://github.com/rakudo/rakudo/issues/2889) — `class Foo { method can() {} }.can("foo")`: Raku++ ignores user-defined `can` and returns `(foo)`; Rakudo calls it and dies with Too many positionals
- [#3018](https://github.com/rakudo/rakudo/issues/3018) — `say MY.WHO.WHICH` Rakudo gives PseudoStash/ID; Raku++ gives Hash/2 (MY.WHO is a plain Hash)
- [#3018](https://github.com/rakudo/rakudo/issues/3018) — `{ say MY.WHO.WHICH }` Rakudo gives PseudoStash/ID; Raku++ gives Hash/2
- [#3292](https://github.com/rakudo/rakudo/issues/3292) *(open)* ✓ — `my \foo = HistoryVar.new; foo = 1` dies "Cannot modify an immutable HistoryVar" in Raku++; Rakudo calls the class's STORE method
- [#3784](https://github.com/rakudo/rakudo/issues/3784) — `WithGenericArray[Int:D].new: :array[1,2,3]` prints `WithGenericArraypun1.new(...)` in Raku++; Rakudo prints `WithGenericArray[Int:D].new(...)`
- [#3846](https://github.com/rakudo/rakudo/issues/3846) — `sss { A.new }` with `method sink(-->Nil) { say $*ABC }` prints nothing in Raku++; sink on the block's last value should run and print 123 (Rakudo dies with unfound $*ABC)
- [#3917](https://github.com/rakudo/rakudo/issues/3917) — `say Array ~~ [Real]` Rakudo gives False (type object vs array instance); Raku++ gives True
- [#3976](https://github.com/rakudo/rakudo/issues/3976) ✓ — `multi submethod FOO` lands in .^method_table (methods: (FOO)) in Raku++; Rakudo puts it in .^submethod_table
- [#4463](https://github.com/rakudo/rakudo/issues/4463) ✓ — `$o.b = 42` on `method b is raw { $.a }` mutates the read-only attribute in Raku++ (prints 42); Rakudo leaves it (Any), issue wants Cannot modify immutable error
- [#4543](https://github.com/rakudo/rakudo/issues/4543) ✓ — `&grep does role { method CALL-ME (/) { say 'Got a call' } }; grep(Str)` prints nothing in Raku++; Rakudo prints Got a call
- [#4711](https://github.com/rakudo/rakudo/issues/4711) — `enum <Num>; say Num.raku` Rakudo prints ::Num; Raku++ prints Num
- [#4750](https://github.com/rakudo/rakudo/issues/4750) — subclass `class V is IO::Path` with `multi method new($path) { self.bless; nextwith($path) }` dies "Must specify a non-empty string as a path" at `self.bless` in Raku++; Rakudo continues
- [#5908](https://github.com/rakudo/rakudo/issues/5908) ✓ — `$.b = 666` in a method on non-rw attribute: Rakudo dies "Cannot modify an immutable Int"; Raku++ silently assigns (dd shows 666)
- [#6748](https://github.com/rakudo/rakudo/issues/6748) — `is peek` trait_mod runs late in Raku++: prints BEGIN, CHECK, then trait; Rakudo-legacy order is trait, BEGIN, CHECK (docs attached correctly)

**lists** (17)

- [#1355](https://github.com/rakudo/rakudo/issues/1355) *(open)* ✓ — `-> +l { l }([1,(2,3)]).raku` Raku++ prints (1, (2, 3)); Rakudo keeps itemized inner list (1, $(2, 3)); same for +@l is raw
- [#1398](https://github.com/rakudo/rakudo/issues/1398) ✓ — `sub f (+items) { .sink for items }; f 1..*` exits immediately in Raku++; it should iterate the infinite range forever (Rakudo times out)
- [#1399](https://github.com/rakudo/rakudo/issues/1399) ✓ — `.sink for 1..*` must loop forever (Rakudo times out); Raku++ exits 0 immediately with no output
- [#1447](https://github.com/rakudo/rakudo/issues/1447) ✓ — `say (roundrobin ((1,2,3),(4,5),(6,7,8,9)))` gives `((1) (2) ... (9))` in Raku++; Rakudo gives `((1 4 6) (2 5 7) (3 8) (9))`
- [#1629](https://github.com/rakudo/rakudo/issues/1629) *(open)* ✓ — `<a b c>.combinations: 2.3..^3.6` Rakudo gives ((a b) (a c) (b c) (a b c)); Raku++ omits the 3-element combo (range endpoint Int-ified differently)
- [#1692](https://github.com/rakudo/rakudo/issues/1692) ✓ — `eager Seq.from-loop: {say $i++; last if $i > 5}` dies "Cannot eager a lazy list" in Raku++; Rakudo returns (); expected output 0..5 like loop
- [#1692](https://github.com/rakudo/rakudo/issues/1692) ✓ — `eager Seq.from-loop: {..., last if $i>5}, {1}` gives `(() () () () ())` in Raku++; Rakudo gives `()` since empty slips flatten
- [#1862](https://github.com/rakudo/rakudo/issues/1862) — Capture gist: Raku++ prints `\(1, 2, 3, :a(Bool::True), :b(Bool::True))`; Rakudo prints `:a, :b`
- [#2133](https://github.com/rakudo/rakudo/issues/2133) *(open)* — `dd (<a b>,)»[]` Rakudo (("a", "b"),); Raku++ prints (("a", "b"),).Seq (hyper result is a Seq not List)
- [#2133](https://github.com/rakudo/rakudo/issues/2133) *(open)* — `my @a=42; dd @a»[]` Rakudo (42,); Raku++ prints (42,).Seq
- [#2265](https://github.com/rakudo/rakudo/issues/2265) — `dd ^Inf .elems` prints the Failure then Raku++ also throws X::Cannot::Lazy (exit 1); Rakudo exits 0 after printing it
- [#2485](https://github.com/rakudo/rakudo/issues/2485) ✓ — `<a b c>.head(*.elems - 1)` and `.head({$_.elems - 1})` give `()` in Raku++; Rakudo gives `(a b c)`
- [#3588](https://github.com/rakudo/rakudo/issues/3588) — `(0..10).rotor(1 => -1)` should repeat (0) lazily forever as Rakudo does; Raku++ returns ((0) (1) ... (10))
- [#3881](https://github.com/rakudo/rakudo/issues/3881) *(open)* ✓ — `say { $++ * ($ *= -1) } ...^21` runs on past 20 (to 100 and beyond) in Raku++; Rakudo stops at 20; `... ^21` also does not stop
- [#4205](https://github.com/rakudo/rakudo/issues/4205) — `say @array>>++` on shaped array prints `[1]` in Raku++; Rakudo prints `(1)`
- [#5138](https://github.com/rakudo/rakudo/issues/5138) ✓ — `(1..*).Slip.is-lazy` is False in Raku++; Rakudo and the issue say True
- [#5951](https://github.com/rakudo/rakudo/issues/5951) *(open)* ✓ — `my @result = [»+«] @case` on [[1,2,3],[5,4,3]] gives `[6, 6, 6]` in Raku++ but `[[6, 6, 6],]` in Rakudo; single-element case `[5, 6, 7]` vs `[3]`

**regex** (13)

- [#1551](https://github.com/rakudo/rakudo/issues/1551) ✓ — `'hello' ~~ TR/e/1/` gives False; Rakudo returns h1llo
- [#2206](https://github.com/rakudo/rakudo/issues/2206) *(open)* — `/$<meow>=(\w+)>\s*)+/` captures in Raku++ include trailing space (`｢foo ｣`) and keys `foo `; Rakudo gives `｢foo｣`
- [#2791](https://github.com/rakudo/rakudo/issues/2791) ✓ — grammar `regex bare-key { <key> <!before \S > }` fails at end of string in Raku++ (tests 4-8 not ok); `Chord.subparse(:rule<bare-key>, 'C#')` must succeed as in Rakudo
- [#3215](https://github.com/rakudo/rakudo/issues/3215) — `"w123" ~~ / <?before w>+ . /` gives Nil in Raku++; should match ｢w｣ (zero-width repeat once); Rakudo hangs
- [#3316](https://github.com/rakudo/rakudo/issues/3316) — `:P5` conditional `rx:P5/(B)?(?(1)C)/` wrongly matches "B" in Raku++ (conditional ignored); Rakudo rejects `(?:` as "Quantifier quantifies nothing"
- [#3410](https://github.com/rakudo/rakudo/issues/3410) ✓ — `token { 'a' <?{ say self.pos }> 'a' }` in Raku++ dies X::Syntax::Self::WithoutObject; Rakudo accepts and prints 0 and 5
- [#3775](https://github.com/rakudo/rakudo/issues/3775) ✓ — Multi-line `/ (\w) {} :my $L = ~$0; [｢{｣ <&?BLOCK>? ｢}｣]? {say ...} "$0" /` is a parse error in Raku++ ("couldn't find final ')'"); Rakudo parses it
- [#3775](https://github.com/rakudo/rakudo/issues/3775) ✓ — Regex using ｢{｣ ｢}｣ corner-bracket quotes with `:my $L` fails to parse in Raku++ ("couldn't find final ')'"); Rakudo compiles and runs it
- [#4535](https://github.com/rakudo/rakudo/issues/4535) — `my token jump($offset){<{". ** $offset"}>}; "a1bc" ~~ /^"a"(\d){}<.&jump($/[*-1])>"c"$/` Raku++: parse error "Quantifier quantifies nothing"; Rakudo matches ｢a1bc｣
- [#4752](https://github.com/rakudo/rakudo/issues/4752) ✓ — `'00000' ~~ m{^ 0<same>**2..4}` Rakudo matches with four same captures; Raku++ finds no match (<same> assertion seems not to work)
- [#5799](https://github.com/rakudo/rakudo/issues/5799) ✓ — `"o\c[combining ring above]" ~~ /o\c[combining ring above]/` gives Nil and `/.\c[...]/` matches in Raku++; Rakudo is Nil, Nil, match (NFG grapheme)
- [#6810](https://github.com/rakudo/rakudo/issues/6810) ✓ — `my token FULLRE { ^ <{$re}> }` inside a sub dies "Variable '$re' is not declared" in Raku++; Rakudo captures the enclosing lexical per call

**dispatch** (10)

- [#1476](https://github.com/rakudo/rakudo/issues/1476) — `*+42:foo` Rakudo compiles and only warns; Raku++ dies at runtime "Cannot resolve caller infix:<+>(Whatever:D, Int:D, :foo)"
- [#1561](https://github.com/rakudo/rakudo/issues/1561) *(open)* — `&infix:< or >.wrap: ->  or  { "meow".say }` is not honoured by Raku++ (prints only foo); Rakudo 2026.09 prints meow then foo
- [#1561](https://github.com/rakudo/rakudo/issues/1561) *(open)* — `.wrap` on `&infix:< or >` ignored when `where Int or Num` is used; Raku++ prints only foo, Rakudo meow then foo
- [#2598](https://github.com/rakudo/rakudo/issues/2598) *(open)* — Trait on a `multi` candidate sees the proto in Raku++ (candidates 2, signature `:( or )`, `.multi` True for proto); candidates report `.multi` False; Rakudo passes the candidate itself
- [#3659](https://github.com/rakudo/rakudo/issues/3659) *(open)* — `say :(3i) ~~ 3i` and `:(<3i>) ~~ 3i` give False False in Raku++; Rakudo True True
- [#4553](https://github.com/rakudo/rakudo/issues/4553) ✓ — `&f.assuming(1).signature.params[0].default.defined` is False in Raku++, True in Rakudo (optional param default lost)
- [#4665](https://github.com/rakudo/rakudo/issues/4665) ✓ — `is cached` has no effect in Raku++: pow2 calls:4094 instead of 11 (Rakudo 11), count-to 2002 vs 1001
- [#4666](https://github.com/rakudo/rakudo/issues/4666) ✓ — `sub foo($s) is cached` with a Set argument runs the body on every call in Raku++ (about 100 lines); Rakudo prints six permutations; issue expects one line
- [#5591](https://github.com/rakudo/rakudo/issues/5591) — `:(A: Str $a, Int $b, *%_)` loses its invocant in Raku++ (.raku shows `:(Str $a ...)`), so test 2 ACCEPTS is False; Rakudo True
- [#6860](https://github.com/rakudo/rakudo/issues/6860) ✓ — User `multi infix:<~>(Str() $a, A $b) { "B" }` not used by `say "A: " ~ A`: Raku++ prints `A: `; Rakudo prints B

**numeric** (7)

- [#1855](https://github.com/rakudo/rakudo/issues/1855) ✓ — `class Foo does Rational[IntStr, IntStr] {}; (Foo.new: <1>, <2>).numerator.^name` Raku++ gives Int; Rakudo gives IntStr
- [#2266](https://github.com/rakudo/rakudo/issues/2266) — `dd 1.base: 1` returns a Failure in both, but Raku++ then exits 1 with the exception (Failure not handled by dd) and prints a truncated Failure.new
- [#2266](https://github.com/rakudo/rakudo/issues/2266) — `dd 1.1.base: 1` Failure from dd leaves Raku++ exiting 1 with the exception; Rakudo exits 0 after dumping the Failure
- [#2434](https://github.com/rakudo/rakudo/issues/2434) ✓ — `my num $x = 5e0; dd $x % 0e0` prints the Failure but Raku++ then throws DivideByZero and exits 1; Rakudo returns Failure and exits 0
- [#2434](https://github.com/rakudo/rakudo/issues/2434) ✓ — `dd $x % $y` with num vars: Raku++ dd prints Failure then dies with DivideByZero (exit 1); Rakudo keeps the Failure and exits 0
- [#5053](https://github.com/rakudo/rakudo/issues/5053) — `Num("a")` inside try/`if $!` yields 0 in Raku++ instead of failing; Supply.map prints 0 where Rakudo prints ()
- [#5053](https://github.com/rakudo/rakudo/issues/5053) — `try { Num("a") }; if $! {Empty}` map gives 0 for "a" and "def" in Raku++; Rakudo skips them

**scope** (7)

- [#1944](https://github.com/rakudo/rakudo/issues/1944) — `for CORE::.kv -> $k,$v { put $k }` prints nothing in Raku++ (CORE:: stash empty); Rakudo iterates symbols
- [#1961](https://github.com/rakudo/rakudo/issues/1961) ✓ — `say "count {$++}" for ^3` Rakudo prints count 0 three times (documented trap); Raku++ prints 0,1,2
- [#1974](https://github.com/rakudo/rakudo/issues/1974) ✓ — `start react whenever (my Channel $c .= new) {...}` then `$c.close` fails "Variable $c is not declared"; Rakudo sees $c in the enclosing scope
- [#2362](https://github.com/rakudo/rakudo/issues/2362) *(open)* — `&?BLOCK` inside `for`/`given`/`if` in a sub returns the sub in Raku++ (Rakudo: the block); `.perl` of a sub prints `&for-sub` instead of `sub for-sub { ... }`
- [#4150](https://github.com/rakudo/rakudo/issues/4150) — After `$bar::baz = 33`, `say MY::bar.raku` gives Nil in Rakudo; Raku++ throws X::NoSuchSymbol 'MY::<bar>'
- [#6175](https://github.com/rakudo/rakudo/issues/6175) — `BEGIN my $w = 0; $w += .words.elems; END say $w` prints (Any) in Raku++; Rakudo prints 0 (BEGIN initialiser value lost)
- [#6600](https://github.com/rakudo/rakudo/issues/6600) — trait_mod:<does> willdo phaser pushing to @seen after `my @x := @seen`: Raku++ prints [] (broken sharing); Rakudo prints [42]

**strings** (6)

- [#1460](https://github.com/rakudo/rakudo/issues/1460) ✓ — `qq or %add».sort.map(*.fmt("%s %s")) or ` is not interpolated by Raku++ (prints source); Rakudo interpolates the hyper plus chained method
- [#2119](https://github.com/rakudo/rakudo/issues/2119) *(open)* — `Q:c/\1/` dd prints `"\1"` in Raku++; Rakudo `"\\1"` (backslash kept literally)
- [#2743](https://github.com/rakudo/rakudo/issues/2743) — `"banana".index("ana", -0.9)` throws X::OutOfRange in Raku++; Rakudo returns 1
- [#5147](https://github.com/rakudo/rakudo/issues/5147) *(open)* — `dd qq:v:to/FOO/` static heredoc: Rakudo IntStr.new(42, "42\n"); Raku++ plain Str and dd prints a raw newline instead of \n
- [#5147](https://github.com/rakudo/rakudo/issues/5147) *(open)* — `dd qq:v:to/FOO/` with $a: Rakudo IntStr.new(42, "42\n"); Raku++ gives plain Str (no allomorph, :v ignored)
- [#5184](https://github.com/rakudo/rakudo/issues/5184) *(open)* — `say &infix:<+>.Str` prints `&infix:<+>` in Raku++; Rakudo prints `infix:<+>` (Code.Str gives the bare name, with a coercion warning)

**formatting** (5)

- [#2373](https://github.com/rakudo/rakudo/issues/2373) *(open)* — `dd $x` prints `"24"` in Raku++; Rakudo prints `$x = "24"` with the variable name
- [#3621](https://github.com/rakudo/rakudo/issues/3621) *(open)* ✓ — `my $x='1,2,,3'; $x .= split(','); print $x.raku` prints `Seq.new()` and `("1", "2", "", "3")` without the `$(...)` itemization Rakudo shows
- [#3803](https://github.com/rakudo/rakudo/issues/3803) — `(lazy ^10).raku` prints `$((0, 1, ..., 9).Seq).lazy` in Raku++; Rakudo prints `$((0, 1, ..., 9).lazy.Seq)`; `.raku` of a lazy Seq differs in shape
- [#3885](https://github.com/rakudo/rakudo/issues/3885) — `.raku` of Regex hash prints `rx/'y'/ => rx/1/` in Raku++; Rakudo prints `/'y'/ => /1/` (order of keys aside)
- [#3885](https://github.com/rakudo/rakudo/issues/3885) — `(my Regex %t{Regex}=/'y'/,/1/).raku` Raku++ prints `rx/'y'/ => rx/1/`; Rakudo `/'y'/ => /1/`

**grammar** (5)

- [#1243](https://github.com/rakudo/rakudo/issues/1243) ✓ — `Number.subparse("ABCDE", :pos(4))` ignores :pos and matches ｢ABCDE｣; Rakudo gives ｢E｣
- [#1243](https://github.com/rakudo/rakudo/issues/1243) ✓ — `subparse("ABCDE", :i)` with failing rule prints `Nil`; Rakudo returns failed Match `#<failed match>`
- [#2059](https://github.com/rakudo/rakudo/issues/2059) ✓ — Grammar actions class with `multi method FALLBACK($name where /^md/, $/)`: Rakudo calls FALLBACK ("fallback for ..."); Raku++ never calls it, just prints the parse tree
- [#5782](https://github.com/rakudo/rakudo/issues/5782) *(open)* — `token foo { _ [ <?{False}> // <bar> ] _ }; token bar { <foo> / something }` parse '_something_' via rule bar: Rakudo True; Raku++ False
- [#5782](https://github.com/rakudo/rakudo/issues/5782) *(open)* — Same grammar with leading `{}`: expected True (Rakudo True); Raku++ prints False

**control** (4)

- [#1508](https://github.com/rakudo/rakudo/issues/1508) *(open)* — `dd $_` after `andthen` prints `Meow.new(...)` without the `$x4 =` container name Rakudo shows; writable alias lost
- [#1508](https://github.com/rakudo/rakudo/issues/1508) *(open)* — `$x4 orelse .=new andthen dd $_` prints `Meow.new(...)` in Raku++; Rakudo prints `Meow $x4 = Meow.new(...)` (variable name lost)
- [#4227](https://github.com/rakudo/rakudo/issues/4227) *(open)* — CONTROL block with `when CX::Succeed` after `succeed` in `given` prints nothing in Raku++; Rakudo prints "seen succeed"
- [#4486](https://github.com/rakudo/rakudo/issues/4486) — `try { let $foo = $foo.&{ Nil } }` leaves `$foo` Nil in Raku++; Rakudo restores it to `bar`

**hashes** (4)

- [#2068](https://github.com/rakudo/rakudo/issues/2068) ✓ — `my Int %h = :42foo; %h.map: { .value = 'meows' ... }` does not type-check in Raku++ (hash becomes foo=>meows); Rakudo dies Type check failed for an element of %h
- [#2217](https://github.com/rakudo/rakudo/issues/2217) ✓ — `say ½ ∈ ^2` Rakudo False (setty semantics, as issue wants); Raku++ prints True (range containment)
- [#3377](https://github.com/rakudo/rakudo/issues/3377) — `my Int:D @foo is Set = 1,2,3; say @foo` prints `[1 2 3]` in Raku++; Rakudo gives `Set[Int:D](1 2 3)`
- [#6031](https://github.com/rakudo/rakudo/issues/6031) ✓ — `my %x is Set[Int()] = "42" => 666; %x.keys.head` Rakudo coerces key to 42; Raku++ dies Type check failed for Set key expected Int (same for Bag/Mix)

**modules** (4)

- [#2840](https://github.com/rakudo/rakudo/issues/2840) — `.say for $*REPO.repo-chain` prints `CompUnit::Repository::Installation.new(...)` dumps; Rakudo prints `inst#path` lines
- [#5889](https://github.com/rakudo/rakudo/issues/5889) *(open)* ✓ — `(require Test).WHO.keys` yields nothing in Raku++; Rakudo lists &output, EXPORT etc.
- [#5889](https://github.com/rakudo/rakudo/issues/5889) *(open)* ✓ — `((require Test)).WHO.keys` yields nothing in Raku++; Rakudo lists the package symbols
- [#5889](https://github.com/rakudo/rakudo/issues/5889) *(open)* ✓ — `for (require Test).WHO.keys { .say }` prints nothing in Raku++; Rakudo lists the package symbols (issue's parse error is fixed)

**rakuast** (4)

- [#5589](https://github.com/rakudo/rakudo/issues/5589) *(open)* — `Q:qq/ {$signature1.raku} /.AST` then `.statements[0].expression.signature`: Raku++ gives RakuAST::ApplyPrefix with no .signature; Rakudo yields the signature
- [#5724](https://github.com/rakudo/rakudo/issues/5724) — `.AST.rakudoc` deparse in Raku++ drops `:lang<RakuDoc>` from `=begin code` and prints AST gist as bare `RakuAST::Doc::Block.new`; Rakudo keeps config
- [#6134](https://github.com/rakudo/rakudo/issues/6134) — `BEGIN { RakuAST::Sub.new(...).EVAL }` dies in Raku++ "Use of RakuAST is experimental; please use experimental :rakuast"; Rakudo 2026.09 runs it without the pragma
- [#6137](https://github.com/rakudo/rakudo/issues/6137) — `RakuAST::Sub.new(...).EVAL` Raku++ demands `use experimental :rakuast`; Rakudo 2026.09 runs it and prints foo

**concurrency** (3)

- [#1757](https://github.com/rakudo/rakudo/issues/1757) — `await Promise.new` (never kept) hangs in Rakudo; Raku++ returns immediately and exits 0
- [#2079](https://github.com/rakudo/rakudo/issues/2079) — `await $promise2, $promise1` where promise1 dies never propagates the exception in Raku++ (times out); Rakudo dies with the exception
- [#2609](https://github.com/rakudo/rakudo/issues/2609) *(open)* — `react { whenever $list.Supply { .put }; whenever Promise.in(3) { done } }` with lazy sleeping map: Raku++ prints nothing and exits; expected 1..15 then exit (Rakudo hangs)

**io** (3)

- [#1424](https://github.com/rakudo/rakudo/issues/1424) *(open)* — `$*IN.slurp.say; $*IN .= open; $*IN.eof.say` dies 'Failed to open file' in Raku++; Rakudo prints True
- [#3566](https://github.com/rakudo/rakudo/issues/3566) — `sub MAIN() { say $*ARGFILES.^name }` Raku++ prints IO::Handle; Rakudo prints IO::ArgFiles
- [#4767](https://github.com/rakudo/rakudo/issues/4767) — `$*ARGFILES[0].lines.join("\n").say` with no files: Rakudo runs silently; Raku++ dies 'No such method lines for Any' (index on $*ARGFILES not supported)

**nativecall** (3)

- [#3184](https://github.com/rakudo/rakudo/issues/3184) — native sub returning `is repr('CPointer')` class `xmlNewParserCtxt()` gives an Int in Raku++ ('No such method SetStructuredErrorFunc for Int'); Rakudo runs and prints 0
- [#3687](https://github.com/rakudo/rakudo/issues/3687) — Binding inlined `HAS IntStruct $.a := IntStruct.new(:12first,:11second)` gives 0 0 in Raku++; Rakudo gives 12 11 (also via nativecast to CArray)
- [#4483](https://github.com/rakudo/rakudo/issues/4483) — `Pointer.say` prints `(Pointer)` in Raku++; Rakudo prints `(NativeCall::Types::Pointer)`

**cli** (2)

- [#2532](https://github.com/rakudo/rakudo/issues/2532) *(open)* — Script `unit module My::Main` with `multi sub MAIN() is export` runs MAIN and prints 1 in Raku++; Rakudo prints nothing (MAIN in a module is not the program entry)
- [#5100](https://github.com/rakudo/rakudo/issues/5100) *(open)* — `sub MAIN($a, $b = $a, #= boom\n)`: Rakudo usage lists boom only for <b>; Raku++ also attaches boom to <a>

**exceptions** (2)

- [#2452](https://github.com/rakudo/rakudo/issues/2452) — `dd sub (--> Str) { fail "meows" }()` Rakudo prints Failure and exits 0; Raku++ prints it then throws meows (exit 1)
- [#4083](https://github.com/rakudo/rakudo/issues/4083) — `sub foo($x is rw){}; foo(1); CATCH { .message }` exits 0 silently in Raku++; Rakudo reports X::Parameter::RW message and exits 1

**pod** (2)

- [#4158](https://github.com/rakudo/rakudo/issues/4158) — `C< << >> >` in pod: Rakudo content is `<< >>`; Raku++ splits it into ` <<` and `> >` lines
- [#5258](https://github.com/rakudo/rakudo/issues/5258) — `=begin input` block gives `Pod::Block::Named{:name("input")}` with Para in Raku++; Rakudo 2026.09 gives `Pod::Block::Input`

**datetime** (1)

- [#2097](https://github.com/rakudo/rakudo/issues/2097) ✓ — `DateTime.new('2016-12-31T23:59:60Z').in-timezone: Int(3600/7)` gives 00:08:34+00:08 in Raku++; Rakudo keeps leap second 00:08:60

**misc** (1)

- [#1891](https://github.com/rakudo/rakudo/issues/1891) — `say Signal(0)` prints `0` in Raku++; Rakudo prints `(Signal)`

**test** (1)

- [#2259](https://github.com/rakudo/rakudo/issues/2259) — `isa-ok 42, Int:D` prints description `The object is-a 'Int'` in Raku++; Rakudo says `'Int:D'`


### LAX (93)

**oo** (21)

- [#1288](https://github.com/rakudo/rakudo/issues/1288) *(open)* — `has $.foo is rw := Proxee.new: ...` with undeclared `Proxee` runs and prints 11 in Raku++; Rakudo refuses at compile time
- [#1427](https://github.com/rakudo/rakudo/issues/1427) *(open)* ✓ — `has Int $.count!` is accepted; Rakudo refuses it ("Negation metaoperator not followed by valid infix")
- [#1556](https://github.com/rakudo/rakudo/issues/1556) *(open)* — `my Int $x where .so .= new: 42; say $x.WHAT.new: 42` prints 42 in Raku++; Rakudo dies You cannot create an instance of this type
- [#1557](https://github.com/rakudo/rakudo/issues/1557) *(open)* — `my Int $x where .so; $x.WHAT.new: 42` Rakudo throws "You cannot create an instance of this type (<anon>)"; Raku++ returns 42
- [#1557](https://github.com/rakudo/rakudo/issues/1557) *(open)* — `my Range $x where .so; $x.WHAT.new: 42` Rakudo dies; Raku++ prints Range and 42..0 (type name also not <anon>)
- [#1557](https://github.com/rakudo/rakudo/issues/1557) *(open)* — `(my Range $x where .so).WHAT.new: 1, 2` Rakudo throws cannot create instance; Raku++ creates 1..2
- [#1602](https://github.com/rakudo/rakudo/issues/1602) *(open)* — `constant Y = Z.new` on stubbed class Z: Rakudo gives compile-time error (not composed yet); Raku++ accepts silently
- [#1848](https://github.com/rakudo/rakudo/issues/1848) — After `.^set_name` on an anon class, passing it to `-> Supercalifragilisticexpialidocious {}` fails type check in Rakudo; Raku++ accepts silently
- [#2073](https://github.com/rakudo/rakudo/issues/2073) — `sub (c \v) {...}(A)` with enum value as constraint: Rakudo rejects the enum type object; Raku++ accepts and runs
- [#2073](https://github.com/rakudo/rakudo/issues/2073) — `sub (b \v) {...}(A)` enum type object accepted by Raku++ (prints True); Rakudo dies with constraint check failure
- [#2143](https://github.com/rakudo/rakudo/issues/2143) — `multi method perl(::?CLASS:D:  or c)` outside a class accepted by Raku++; Rakudo: No such symbol ::?CLASS
- [#2236](https://github.com/rakudo/rakudo/issues/2236) *(open)* — `$config.^add_attribute` at runtime then `$attr.set_value` works in Raku++ printing Hello; Rakudo throws no such attribute (issue wants a warning at add_attribute)
- [#2250](https://github.com/rakudo/rakudo/issues/2250) — Role BarRole does FooRole, both with submethod `a`, consumed by class: Rakudo refuses "already has a submethod 'a'"; Raku++ accepts silently
- [#2321](https://github.com/rakudo/rakudo/issues/2321) *(open)* — `role Foo[::T] { method z(::T \x = 42) {} }.new.z` punning without args runs silently in Raku++; Rakudo dies Could not instantiate role 'Foo'
- [#3364](https://github.com/rakudo/rakudo/issues/3364) *(open)* — `has $.val is required where check-param($_)` where check-param returns Nil: Raku++ accepts; Rakudo dies Type check failed in assignment to $!val
- [#3802](https://github.com/rakudo/rakudo/issues/3802) ✓ — `self."!$method-name"()` calls private method !private in Raku++ (prints the value); Rakudo refuses with No such method '!private'
- [#4522](https://github.com/rakudo/rakudo/issues/4522) — `has ($.a is rw is required is totally-made-up)` is accepted in Raku++; Rakudo refuses the unknown trait at compile time
- [#5740](https://github.com/rakudo/rakudo/issues/5740) *(open)* — `True but R(foo => 1)` where R has two public attributes: Rakudo dies "Can only supply an initialization value ... single public attribute"; Raku++ accepts silently
- [#5740](https://github.com/rakudo/rakudo/issues/5740) *(open)* — `True but R(foo => 1, bar => 2)`: Rakudo dies "No such method 'CALL-ME'"; Raku++ accepts silently
- [#6113](https://github.com/rakudo/rakudo/issues/6113) ✓ — `class Foo {has $.a; method test {$.a = 5}}`: Rakudo throws "Cannot modify an immutable 'Any'"; Raku++ silently assigns and prints 5

**syntax** (19)

- [#1365](https://github.com/rakudo/rakudo/issues/1365) *(open)* — `::("Int"):D("x").^name.say` should throw; Rakudo throws No such symbol 'Int:D', Raku++ prints Pair
- [#1476](https://github.com/rakudo/rakudo/issues/1476) — `42:foo` Rakudo refuses "You can't adverb 42" at compile time; Raku++ accepts with only useless-use warnings
- [#1719](https://github.com/rakudo/rakudo/issues/1719) *(open)* — `my \start = 1; 1 ?? start !! 0` accepted by Raku++; Rakudo refuses: !! gobbled by middle expression
- [#1723](https://github.com/rakudo/rakudo/issues/1723) *(open)* — `sub foo( Int(Uint) $num )` with unknown type Uint compiles and runs in Raku++; Rakudo refuses at compile time
- [#1828](https://github.com/rakudo/rakudo/issues/1828) ✓ — `-> < 1/2> {say "in"}(< 1/2>)` Rakudo refuses (Missing block); Raku++ accepts and prints in
- [#2044](https://github.com/rakudo/rakudo/issues/2044) — `sub (&s :()) {}` must be refused (invocant marker only in methods); Raku++ accepts silently
- [#2092](https://github.com/rakudo/rakudo/issues/2092) — `42 .&abs` Rakudo refuses: only basic method calls can be detached; Raku++ accepts silently
- [#2783](https://github.com/rakudo/rakudo/issues/2783) ✓ — `use v6.e` Rakudo refuses: Raku v6.e requires PREVIEW modifier; Raku++ accepts silently
- [#3229](https://github.com/rakudo/rakudo/issues/3229) — `use NativeCall` without semicolon before `sub atoi(...) is native` compiles in Raku++; Rakudo refuses with unknown trait errors
- [#3426](https://github.com/rakudo/rakudo/issues/3426) — `say False:so` is accepted in Raku++ (prints `Falseso => True`); Rakudo refuses "You can't adverb False"
- [#3426](https://github.com/rakudo/rakudo/issues/3426) — `say 42:so` is accepted in Raku++ (prints `42so => True`); Rakudo refuses "You can't adverb 42"
- [#3561](https://github.com/rakudo/rakudo/issues/3561) — `say ++$a .= abs` runs in Raku++ and increments; Rakudo refuses with "Cannot modify an immutable Int"
- [#4107](https://github.com/rakudo/rakudo/issues/4107) *(open)* — `say 1, 2, and 3` Rakudo refuses "Undeclared routine: and"; Raku++ accepts and prints 12
- [#4175](https://github.com/rakudo/rakudo/issues/4175) ✓ — `my $dt = DateTime('...')` without `;` before next line: Rakudo refuses 'Two terms in a row'; Raku++ accepts it (and later() with several units already works)
- [#4429](https://github.com/rakudo/rakudo/issues/4429) — statement missing `;` before `say` on next line is accepted by Raku++; Rakudo: Two terms in a row across lines
- [#4512](https://github.com/rakudo/rakudo/issues/4512) — line `/<?[\s a]> . /    # OUTPUT: ...` with no `;` before next statement accepted by Raku++; Rakudo: Confused
- [#4785](https://github.com/rakudo/rakudo/issues/4785) — `sub infix:<🦋>( or ) is equiv«prefix:<i>» {}` is accepted silently in Raku++; Rakudo refuses (equiv argument fails Callable type check)
- [#5061](https://github.com/rakudo/rakudo/issues/5061) — `my @array[3,3] <== [0..8]` Rakudo refuses "Cannot feed into shaped arrays"; Raku++ accepts silently
- [#6048](https://github.com/rakudo/rakudo/issues/6048) ✓ — line `say (one (3, $bar)).defined` followed by `my $bar;` on next line without `;` is accepted by Raku++; Rakudo: Two terms in a row across lines (it also prints True x3, as wanted)

**dispatch** (10)

- [#1286](https://github.com/rakudo/rakudo/issues/1286) ✓ — `multi f(Int \a, Any \b)` vs `f(Any \a, Int \b)` called f(42,42): Rakudo dies Ambiguous call; Raku++ silently dispatches
- [#1411](https://github.com/rakudo/rakudo/issues/1411) — `--> List:D[Int]` return type: Rakudo refuses "List cannot be parameterized"; Raku++ accepts silently
- [#1426](https://github.com/rakudo/rakudo/issues/1426) *(open)* — `sub ($count! = Int) {}` accepted by Raku++; Rakudo: Cannot put default on required parameter
- [#1426](https://github.com/rakudo/rakudo/issues/1426) *(open)* — `sub ($count is required = Int) {}` accepted by Raku++; Rakudo refuses with default on required parameter
- [#1833](https://github.com/rakudo/rakudo/issues/1833) — `sub f(Empty) {'ok'}; say f(Slip)` Rakudo dies with constraint type check failure; Raku++ prints ok
- [#2320](https://github.com/rakudo/rakudo/issues/2320) — `class { method z { $^a } }` Rakudo refuses placeholder variables in a method at compile time; Raku++ accepts silently
- [#2494](https://github.com/rakudo/rakudo/issues/2494) — `my buf8 $foo; $foo.append(...)` on undefined Buf must die Cannot resolve caller; Raku++ accepts silently
- [#5176](https://github.com/rakudo/rakudo/issues/5176) *(open)* — `multi sub bar(str $s)` called with Str "abc" runs in Raku++ printing voila; Rakudo and issue expect Cannot resolve caller
- [#5845](https://github.com/rakudo/rakudo/issues/5845) — `sub foo(--> Array of Str){my @a="foo"; return @a}` Rakudo dies return type check; Raku++ accepts untyped Array
- [#5845](https://github.com/rakudo/rakudo/issues/5845) — `sub foo(--> Hash of Str)` returning plain Hash: Rakudo dies type check; Raku++ accepts

**scope** (7)

- [#2027](https://github.com/rakudo/rakudo/issues/2027) *(open)* — `my ($) := (1, 2)` silently accepted by Raku++; Rakudo throws Too many positionals
- [#2052](https://github.com/rakudo/rakudo/issues/2052) — `sub class {}; my \point = %(a=>1); class point{"a"}.say` runs in Raku++ printing (point); Rakudo refuses with Redeclaration of symbol 'point'
- [#2243](https://github.com/rakudo/rakudo/issues/2243) *(open)* ✓ — `LEAVE blah if foo;` with undeclared blah/foo runs silently in Raku++; Rakudo reports Undeclared routines at compile time
- [#2739](https://github.com/rakudo/rakudo/issues/2739) — `my T $a is default($val)` with `::T` capture: Rakudo refuses at compile time (default will never bind); Raku++ runs silently
- [#3102](https://github.com/rakudo/rakudo/issues/3102) — `sub MAIN(:$input){ my $input //= 1 }` Rakudo warns redeclaration and dies on readonly assign; Raku++ runs silently
- [#4764](https://github.com/rakudo/rakudo/issues/4764) — regex code block using undeclared `$MAX` compiles and runs in Raku++; Rakudo refuses "Variable '$MAX' is not declared"
- [#5194](https://github.com/rakudo/rakudo/issues/5194) — `($f, $f-next) := ($f-next, $f + $f-next)` accepted by Raku++ and prints 11; Rakudo refuses: Cannot use bind operator with this left-hand side

**concurrency** (6)

- [#1644](https://github.com/rakudo/rakudo/issues/1644) — `Lock.new.protect({})` with a Hash must die with signature mismatch; Raku++ silently accepts
- [#2128](https://github.com/rakudo/rakudo/issues/2128) — `my \w = lines.hyper; @ = w; say w.head` prints Nil in Raku++; Rakudo throws iterator of this HyperSeq already in use
- [#2753](https://github.com/rakudo/rakudo/issues/2753) *(open)* ✓ — `sub { await start { return } }()` must die (return outside Routine); Raku++ exits 0 silently, Rakudo panics
- [#2753](https://github.com/rakudo/rakudo/issues/2753) *(open)* ✓ — `await start { return }` must die 'Attempt to return outside of any Routine'; Raku++ exits 0 silently
- [#2753](https://github.com/rakudo/rakudo/issues/2753) *(open)* ✓ — `sub { start { return } }(); sleep 1` should report unhandled exception; Raku++ silent
- [#5892](https://github.com/rakudo/rakudo/issues/5892) *(open)* — `say await Promise.kept(42).andthen(* + 5)` Rakudo dies Cannot resolve Numeric(Promise:D); Raku++ numifies the Promise and prints 7

**regex** (6)

- [#2624](https://github.com/rakudo/rakudo/issues/2624) — `m:P5/[abc]+/` Rakudo refuses "Adverb P5 not allowed on m"; Raku++ still matches ｢abc｣
- [#3025](https://github.com/rakudo/rakudo/issues/3025) — `my regex foo {.}; foo 'a'` runs silently in Raku++; Rakudo errors No such method '!cursor_start' (regex called as sub)
- [#4527](https://github.com/rakudo/rakudo/issues/4527) *(open)* — `rule bad {'a'\  a}` (unspace inside regex) accepted silently by Raku++; Rakudo: No unspace allowed in regex
- [#4927](https://github.com/rakudo/rakudo/issues/4927) — `"123456" ~~ m/ ^ $<.digit>=(\d)+/` accepted silently by Raku++; Rakudo fails at runtime with bindkey_o VM error (issue wants a compile-time regex error)
- [#4927](https://github.com/rakudo/rakudo/issues/4927) — `m/ ^ <digit>=(\d)+/` accepted silently by Raku++; Rakudo refuses with Unrecognized regex metacharacter =
- [#5978](https://github.com/rakudo/rakudo/issues/5978) — `rule x () {; X}` accepted silently by Raku++; Rakudo: Unrecognized regex metacharacter ;

**nativecall** (5)

- [#1767](https://github.com/rakudo/rakudo/issues/1767) *(open)* — `has long $.x` in a repr(CPointer) class without `use NativeCall` accepted; Rakudo: Type long is not declared
- [#1778](https://github.com/rakudo/rakudo/issues/1778) ✓ — `my CArray[uint8] $ptr .= new;` without `use NativeCall` runs in Raku++; Rakudo refuses: Type 'CArray' is not declared
- [#1778](https://github.com/rakudo/rakudo/issues/1778) ✓ — `my CArray[uint8] $ptr .= new: 0 xx 4096` without `use NativeCall` accepted; Rakudo says Type 'CArray' is not declared
- [#2191](https://github.com/rakudo/rakudo/issues/2191) — `cglobal(LIB,...,Pointer)` accepted without `use NativeCall` in Raku++; Rakudo reports undeclared routines cglobal and Pointer
- [#2673](https://github.com/rakudo/rakudo/issues/2673) *(open)* — `unit class Foo is repr('CStruct')` with no attributes accepted by Raku++; Rakudo refuses with Class Foo has no attributes

**lists** (4)

- [#1657](https://github.com/rakudo/rakudo/issues/1657) — `(3,1,2).kv.first(-> $k, $v {...})` prints 0 in Raku++; Rakudo throws Too few positionals (arity-2 block not fed pairs)
- [#1943](https://github.com/rakudo/rakudo/issues/1943) — `my @a[2;2]; say @a[1,1]` returns a nested slice in Raku++; Rakudo throws partially-dimensioned views NYI
- [#2742](https://github.com/rakudo/rakudo/issues/2742) *(open)* — `(0...∞).chrs` Rakudo dies "Cannot .chrs a lazy list"; Raku++ accepts silently
- [#3656](https://github.com/rakudo/rakudo/issues/3656) *(open)* — `@a[1,-2,0]:delete` with a negative index silently succeeds in Raku++; Rakudo throws Index out of range -2

**exceptions** (3)

- [#1333](https://github.com/rakudo/rakudo/issues/1333) — `cmp-ok 42, "!eqv", 5` Rakudo fails: could not use string as comparator; Raku++ accepts the string and prints ok 1
- [#2492](https://github.com/rakudo/rakudo/issues/2492) — `Foo.?meow.?print: 42` Rakudo dies (print(Nil:U: Int:D) no candidate); Raku++ prints Nil warning and exits 0
- [#3737](https://github.com/rakudo/rakudo/issues/3737) *(open)* — `my ($foo) := prepare` binding too few items runs silently in Raku++; Rakudo dies with a (misleading) too-few-positionals error

**test** (3)

- [#1631](https://github.com/rakudo/rakudo/issues/1631) *(open)* — `ok` is callable without `use Test` in Raku++ (Rakudo: Undeclared routine); also the throttle backpressure test prints not ok
- [#3607](https://github.com/rakudo/rakudo/issues/3607) — `lives-ok` used without `use Test` runs and prints ok in Raku++; Rakudo says Undeclared routine lives-ok
- [#4967](https://github.com/rakudo/rakudo/issues/4967) *(open)* ✓ — `is-deeply ...` without `use Test`: Rakudo dies Undeclared routine is-deeply; Raku++ runs it and prints ok 1

**datetime** (2)

- [#4755](https://github.com/rakudo/rakudo/issues/4755) — `Date.new('2015-02-27').later(:5foo)` and other bogus units: Rakudo throws a type check error; Raku++ accepts silently (issue wants an error)
- [#4783](https://github.com/rakudo/rakudo/issues/4783) — `Date.new('2022-02-12').later(:5hours)` Rakudo throws Cannot use 5 hours as a unit on a Date; Raku++ accepts silently

**strings** (2)

- [#1314](https://github.com/rakudo/rakudo/issues/1314) ✓ — `'x'.substr: /x/, 'x'` Rakudo dies "You cannot use a Regex on 'substr', did you mean 'subst'?"; Raku++ accepts silently
- [#1720](https://github.com/rakudo/rakudo/issues/1720) — `"Long string".substr-rw: 3, {6}` on a literal returns "g stri" in Raku++; Rakudo requires a writeable container

**hashes** (1)

- [#5709](https://github.com/rakudo/rakudo/issues/5709) *(open)* — `{"a" => 1}<a>:k:v` must die 'Unsupported combination of adverbs'; Raku++ accepts silently

**internal** (1)

- [#5788](https://github.com/rakudo/rakudo/issues/5788) *(open)* — Fragment uses `nqp::create` without `use nqp`; Rakudo refuses to compile, Raku++ accepts it silently

**io** (1)

- [#3086](https://github.com/rakudo/rakudo/issues/3086) — `$*IN.nl-in('>')` passes an argument: Rakudo dies "Too many positionals passed; expected 1 argument but got 2"; Raku++ accepts it silently

**modules** (1)

- [#1612](https://github.com/rakudo/rakudo/issues/1612) *(open)* — `todo` and `ok` accepted without `use Test` in Raku++; Rakudo rejects them as undeclared routines

**signatures** (1)

- [#3027](https://github.com/rakudo/rakudo/issues/3027) — `ztask( (:$key, :$value) )` called with a junction-key Pair: Rakudo dies 'Unknown binding error in sub-signature'; Raku++ accepts silently


(KNOWN-PHASER-VALUE, 42 cases in 23 issues: [#1323](https://github.com/rakudo/rakudo/issues/1323), [#1329](https://github.com/rakudo/rakudo/issues/1329), [#1368](https://github.com/rakudo/rakudo/issues/1368), [#1385](https://github.com/rakudo/rakudo/issues/1385), [#1415](https://github.com/rakudo/rakudo/issues/1415), [#1416](https://github.com/rakudo/rakudo/issues/1416), [#1491](https://github.com/rakudo/rakudo/issues/1491), [#1656](https://github.com/rakudo/rakudo/issues/1656), [#1668](https://github.com/rakudo/rakudo/issues/1668), [#1681](https://github.com/rakudo/rakudo/issues/1681), [#1740](https://github.com/rakudo/rakudo/issues/1740), [#1741](https://github.com/rakudo/rakudo/issues/1741), [#1973](https://github.com/rakudo/rakudo/issues/1973), [#2002](https://github.com/rakudo/rakudo/issues/2002), [#2387](https://github.com/rakudo/rakudo/issues/2387), [#2537](https://github.com/rakudo/rakudo/issues/2537), [#2925](https://github.com/rakudo/rakudo/issues/2925), [#2988](https://github.com/rakudo/rakudo/issues/2988), [#3185](https://github.com/rakudo/rakudo/issues/3185), [#3560](https://github.com/rakudo/rakudo/issues/3560), [#3620](https://github.com/rakudo/rakudo/issues/3620), [#4504](https://github.com/rakudo/rakudo/issues/4504), [#5919](https://github.com/rakudo/rakudo/issues/5919))

## From the UNCLEAR re-read (2026-11-14)

The 43 UNCLEAR cases were re-read with their issue threads, which the sweep had
not downloaded, and every snippet was run again on both engines
([unclear-reread.tsv](unclear-reread.tsv); review.tsv and
harness/overrides.tsv carry the new verdicts). The tally: 16 PPBUG, 11 LAX,
5 PPWIN, 3 BOTHOK, 3 NOISE, 1 SHARED, 1 INTERNAL, and 3 still UNCLEAR
(2804.1, 4562.0, 6880.0), whose threads leave the answer open. The
new PPBUG and LAX cases:

### PPBUG, re-read (16)

**control** (3)

- [#1704](https://github.com/rakudo/rakudo/issues/1704) *(open)* — re-read 2026-11-14: Open; zoffix 2018-04-07 links #1571. Rakudo covers 5 of 8 sink cases, Raku++ none; it also skips sinking `1 but S;` and sub return values; `1 for 0` warns at line 0.
- [#5528](https://github.com/rakudo/rakudo/issues/5528) *(open)* — re-read 2026-11-14: vrurg 2024-02-19: PHASER <statement> nesting is legit, the inner phaser attaches to the innermost loop block; lizmat 2024-10-20 agrees. Raku++ never runs the inner LAST.
- [#5528](https://github.com/rakudo/rakudo/issues/5528) *(open)* — re-read 2026-11-14: Same ruling (vrurg 2024-02-19, lizmat 2024-10-20). Raku++ runs the inner FIRST only when the outer LAST fires.

**nativecall** (2)

- [#1767](https://github.com/rakudo/rakudo/issues/1767) *(open)* — re-read 2026-11-14: lizmat 2018-04-24 suggests a compose-time complaint (not done). Raku++ fails sooner: `my p_timespec $t` drops a lowercase type name (type Any); CPointer attrs are accepted too.
- [#3173](https://github.com/rakudo/rakudo/issues/3173) — re-read 2026-11-14: irced 2019-09-11 (closing reply, NativeCall docs): members aren't containers, bind in TWEAK (works on both). Raku++ stores junk for HAS init and lets RO accessors assign.

**numeric** (2)

- [#1596](https://github.com/rakudo/rakudo/issues/1596) — re-read 2026-11-14: ab5tract 2024-05-20 closed: nothing leaks; his variants print <1/2> and Rational.new(1, 2). Raku++ prints 0.5 for all: .raku of a Rat subclass must not use the decimal form.
- [#1872](https://github.com/rakudo/rakudo/issues/1872) — re-read 2026-11-14: lizmat 2020-01-10: DIHWIDT as documented, added :no-trailing-zeroes (97e7cdbd54). Raku++ lacks it: <1/2>.base(10, 20, :no-trailing-zeroes) gives 0.5000... not 0.5. (fixed e2981dc5)

**syntax** (2)

- [#1234](https://github.com/rakudo/rakudo/issues/1234) *(open)* — re-read 2026-11-14: jnthn 2017-11-05 agrees «a\ b» is one word keeping the space; lizmat 2024-10-01: needs a language bump. Raku++'s "ab" fits neither reading.
- [#2467](https://github.com/rakudo/rakudo/issues/2467) *(open)* — re-read 2026-11-14: lizmat 2024-10-08: make sub postfix:<ⁿ> a full citizen with docs and tests. Rakudo exposes it (&postfix:<ⁿ>(2, 3) is 8); Raku++ has no such symbol.

**dispatch** (1)

- [#5717](https://github.com/rakudo/rakudo/issues/5717) *(open)* — re-read 2026-11-14: No comments; the title calls the hang a bug. Raku++ drops a lazy Seq slipped into a capture (*@a gets 0 elems) though |(^Inf) binds lazily.

**exceptions** (1)

- [#3580](https://github.com/rakudo/rakudo/issues/3580) *(open)* — re-read 2026-11-14: Open; users only (2023-08/09). Raku++ blames the if line; Rakudo has a similar off-line bug in variants. Also Raku++ throws at Str < Int where Rakudo returns a Failure.

**lists** (1)

- [#2033](https://github.com/rakudo/rakudo/issues/2033) *(open)* — re-read 2026-11-14: zoffix 2018-07-22 leaves &by vs :&by open. The diff is Code.Str: documented as the name plus a warning; Raku++ gives sub { ... } or &foo silently, so cmp flips.

**oo** (1)

- [#6793](https://github.com/rakudo/rakudo/issues/6793) *(open)* — re-read 2026-11-14: lizmat 2026-11-11 golfed Rakudo's bug to `BEGIN my @` (fine on Raku++). Raku++'s False: infix:<~~> in X~~, Z~~, »~~», [~~] never calls a Callable RHS. (fixed e2981dc5)

**pod** (1)

- [#1244](https://github.com/rakudo/rakudo/issues/1244) — re-read 2026-11-14: Topic (C<> in cells) left open: tbrowder 2019-08-21 not fixed, doable; closed by reporter 2021-09-08; both keep raw text. Diff: Raku++ keeps the trailing row separator.

**regex** (1)

- [#2608](https://github.com/rakudo/rakudo/issues/2608) — re-read 2026-11-14: jnthn 2019-01-25/28 (da6d40adab, 3d581c8d23): a regex value in Bool context matches the $_ captured at closure clone; 6.c stays dynamic. Raku++ uses the caller's $_.

**test** (1)

- [#2258](https://github.com/rakudo/rakudo/issues/2258) — re-read 2026-11-14: lizmat 2024-10-06 closed (open a problem-solving issue if wanted); Rakudo unchanged: istype($var, $type.WHAT). Raku++ autothreads .isa over the Junction.


### LAX, re-read (11)

**syntax** (5)

- [#1234](https://github.com/rakudo/rakudo/issues/1234) *(open)* — re-read 2026-11-14: Lines lack ;: Rakudo refuses, Raku++'s deliberate tolerant split accepts (as #4429.0). Topic is SHARED: jnthn 2017-11-05 wants one word, both engines split.
- [#1503](https://github.com/rakudo/rakudo/issues/1503) — re-read 2026-11-14: jnthn 2018-02-09: `constant Int $foo` is the wrong (ambiguous) syntax; write `my Int constant $foo`, done by zoffix 2018-06-28. Raku++ accepts the wrong form.
- [#1504](https://github.com/rakudo/rakudo/issues/1504) — re-read 2026-11-14: Per #1503 (jnthn 2018-02-09) the type precedes constant; ab5tract 2024-05-16 closed once the `my Foo::Bar constant` form worked (it does on both). Raku++ also takes this order.
- [#1504](https://github.com/rakudo/rakudo/issues/1504) — re-read 2026-11-14: Same as 1504.0 (jnthn 2018-02-09, ab5tract 2024-05-16). The right form works on both engines.
- [#1942](https://github.com/rakudo/rakudo/issues/1942) *(open)* — re-read 2026-11-14: lizmat 2024-10-04 calls RakuAST's map(Seq:D) error an improvement. Raku++ binds a Seq or List to an &-sigil parameter without the Callable check.

**regex** (3)

- [#1894](https://github.com/rakudo/rakudo/issues/1894) — re-read 2026-11-14: jnthn 2018-06-01: <.foo> is always a non-capturing method call, <&foo> the lexical call; moved to doc issue. Raku++ resolves <.t> to the lexical token.
- [#1894](https://github.com/rakudo/rakudo/issues/1894) — re-read 2026-11-14: Same ruling (jnthn 2018-06-01). The grammar parts print the same on both engines.
- [#3119](https://github.com/rakudo/rakudo/issues/3119) — re-read 2026-11-14: jnthn 2020-02-05: to spec; regexes are code objects and are invoked; smartmatch &P::r instead (both give ｢match｣). Raku++ calls r with no args.

**dispatch** (1)

- [#3503](https://github.com/rakudo/rakudo/issues/3503) — re-read 2026-11-14: vrurg 2020-02-21 (reporter): intended behaviour. Raku++ prefers the subclass candidate instead of reporting the ambiguity.

**lists** (1)

- [#6075](https://github.com/rakudo/rakudo/issues/6075) — re-read 2026-11-14: lizmat 2026-05-25 fixed Seq cmp (ee30c34fff); with ; added both print More x4. As written the lines lack ;, which Raku++'s tolerant split accepts (#4429.0).

**strings** (1)

- [#4102](https://github.com/rakudo/rakudo/issues/4102) *(open)* — re-read 2026-11-14: Open feature request (only melezhik 2020-12-10). Raku++ .encode falls back to UTF-8 for any unknown name ("x".encode("nonsense")); .decode refuses. (fixed e2981dc5)

