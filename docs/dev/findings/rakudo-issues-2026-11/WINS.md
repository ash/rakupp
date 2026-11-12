# Where Raku++ already differs for the better, and the open bugs it shares (2026-11-12)

Generated from [review.tsv](review.tsv) by `harness/fixlist.py`; see [../RAKUDO-ISSUES-2026-11.md](../RAKUDO-ISSUES-2026-11.md).

- **PPWIN** — Raku++ already behaves as the issue expects while Rakudo v2026.09 does not. Each is a reviewer verdict from recorded outputs: worth a regression test in t/ so it stays that way.
- **SHARED** — Raku++ reproduces the bug an open Rakudo issue reports. Fixing one is a chance to be right where Rakudo is not; check the issue thread for the agreed answer first.

### PPWIN (161)

**oo** (32)

- [#1226](https://github.com/rakudo/rakudo/issues/1226) — Rakudo refuses `has @.color[4] = <1 2 3 4>` (not yet implemented); Raku++ gives `[1 2 3 4]` as the issue expects
- [#1288](https://github.com/rakudo/rakudo/issues/1288) *(open)* — Rakudo refuses `has $.foo is rw := Proxy.new...`; Raku++ accepts the attribute bind, which the issue wants to work
- [#1556](https://github.com/rakudo/rakudo/issues/1556) *(open)* — Rakudo prints (<anon>) as the issue complains; Raku++ prints (Int)
- [#1798](https://github.com/rakudo/rakudo/issues/1798) *(open)* — `has &.foo where {True}` with a sub works in Raku++; Rakudo dies Cannot assign to immutable value
- [#2166](https://github.com/rakudo/rakudo/issues/2166) — `my Int:D $foo where * >= 0 = 0; $foo = Nil` Rakudo wrongly accepts and prints (<anon>); Raku++ throws type check failure as issue expects
- [#2199](https://github.com/rakudo/rakudo/issues/2199) — `class :: does Positional { method list { self } }.new.kv` Rakudo hangs; Raku++ returns a Seq
- [#2287](https://github.com/rakudo/rakudo/issues/2287) *(open)* — `say Real.new()` crashes/hangs Rakudo; Raku++ prints 0 without crashing
- [#2334](https://github.com/rakudo/rakudo/issues/2334) *(open)* — `method BUILD (:$.x)` Rakudo accepts silently then breaks; Raku++ refuses at compile time with the helpful virtual-call message
- [#2336](https://github.com/rakudo/rakudo/issues/2336) *(open)* ✓ — `my &op where .signature ~~ :(Int, Int --> Int) = sub ...` dies in Rakudo with "No such method 'signature' for Int"; Raku++ accepts silently
- [#2336](https://github.com/rakudo/rakudo/issues/2336) *(open)* ✓ — `has &.bar where .signature ~~ ...` Rakudo dies "Cannot assign to an immutable value"; Raku++ accepts as expected
- [#2692](https://github.com/rakudo/rakudo/issues/2692) — `B.new.ii.Str` with native uint8 Proxy FETCH runs in Raku++; Rakudo dies No such method Str for BOOTInt
- [#2716](https://github.com/rakudo/rakudo/issues/2716) — `class H is Hash` with BUILD and TWEAK submethods: Raku++ invokes both as the issue expects; Rakudo still skips them
- [#2924](https://github.com/rakudo/rakudo/issues/2924) — `Systemic.^mro`: Rakudo dies "No such method 'mro' for ParametricRoleGroupHOW"; Raku++ returns (Any, Mu) as the issue wants
- [#3197](https://github.com/rakudo/rakudo/issues/3197) — `class Bar is hidden is Foo` with `nextsame` prints Bar then Foo in Raku++ as issue expects; Rakudo prints only Bar
- [#3412](https://github.com/rakudo/rakudo/issues/3412) — attribute composed once: Raku++ prints ok 1 as expected; Rakudo gets 2 (composed twice)
- [#3486](https://github.com/rakudo/rakudo/issues/3486) — `my subset Bar of ::?CLASS` in a role works in Raku++ printing True as the issue expects; Rakudo refuses as not implemented
- [#4260](https://github.com/rakudo/rakudo/issues/4260) *(open)* — `class { submethod TWEAK { Any.map: {say 99} } }.new` Rakudo prints only 2 of 3 99 (issue: should sink); Raku++ prints all three
- [#4260](https://github.com/rakudo/rakudo/issues/4260) *(open)* — same TWEAK sink case: Rakudo 2 lines, Raku++ 3 lines as issue expects
- [#4306](https://github.com/rakudo/rakudo/issues/4306) *(open)* — Role R2 doing R1 using $!attr: Rakudo refuses "Attribute $!attr not declared in role R2"; Raku++ accepts as the issue wants
- [#4801](https://github.com/rakudo/rakudo/issues/4801) — `$obj.^mixin: $role` called from inside a role method works in Raku++; Rakudo dies 'Cannot find method specialize'
- [#4841](https://github.com/rakudo/rakudo/issues/4841) — Role method `returns Array of Str` called via `$.ptp(...)` gives 8 elems in Raku++ as the issue expects; Rakudo still gives 1
- [#4915](https://github.com/rakudo/rakudo/issues/4915) *(open)* — `:(::T, T) ~~ :(::T, T)` gives True in Raku++ as the issue expects; Rakudo dies "No such method 'ACCEPTS' for invocant of type 'T'"
- [#4915](https://github.com/rakudo/rakudo/issues/4915) *(open)* — `:(::T, T) ~~ :(::T, T)` gives True in Raku++ as expected; Rakudo dies with No such method 'ACCEPTS'
- [#4916](https://github.com/rakudo/rakudo/issues/4916) — Raw-invocant `multi method golf(Golfy:U \SELF:)` on role pun: Raku++ prints Golfy.new(values => [42]) as issue expects; Rakudo says immutable type object
- [#4925](https://github.com/rakudo/rakudo/issues/4925) *(open)* — `class A::B { class A::C {...}; class A::C { has $.d } }` compiles in Raku++ as the issue expects; Rakudo dies with stubbed but not defined: A::C
- [#4925](https://github.com/rakudo/rakudo/issues/4925) *(open)* — Stub `class X::Y {...}` then definition inside `class A::B` accepted by Raku++; Rakudo still dies stubbed but not defined: X::Y
- [#4925](https://github.com/rakudo/rakudo/issues/4925) *(open)* — `class A { class A::B {...}; class A::B { has $.c } }` compiles in Raku++; Rakudo dies stubbed but not defined: A::B
- [#5034](https://github.com/rakudo/rakudo/issues/5034) — `my Foo(Any) $foo = 1` with role COERCE and stub method: Rakudo dies pun error; Raku++ accepts as issue expects
- [#5047](https://github.com/rakudo/rakudo/issues/5047) *(open)* — `class Bar does Foo { only method foo(Str) {} }` overriding role multi: Raku++ accepts as issue wants; Rakudo refuses with only-vs-multi error
- [#5803](https://github.com/rakudo/rakudo/issues/5803) — `is ($s.=comb(1=>0,:partial).map({'a'}).join), 'aaaa'` Rakudo dies Type check Seq into Str; Raku++ passes ok 2 as documented sugar expects
- [#5915](https://github.com/rakudo/rakudo/issues/5915) *(open)* — `use v6.*; my class A { has $.a where * > 0 }` compiles in Raku++; Rakudo refuses (needs initializer), which the issue calls a bug
- [#6848](https://github.com/rakudo/rakudo/issues/6848) *(open)* — `package Foo { class Bar::Baz {} }` gives `Foo::Bar.^name` = Foo::Bar in Raku++ (as in Rakudo 2026.08); Rakudo 2026.09 regresses to `Bar`

**regex** (18)

- [#1269](https://github.com/rakudo/rakudo/issues/1269) — Rakudo dies "Quantifier quantifies nothing" on `m:P5/(?<meow>.+)/`; Raku++ matches and captures meow, as the issue asks ("should just work")
- [#1659](https://github.com/rakudo/rakudo/issues/1659) *(open)* — `g.parse('ro')` with `\w+<!after <keywords>>` should match; Raku++ True, Rakudo still False
- [#2076](https://github.com/rakudo/rakudo/issues/2076) *(open)* — `$/.list` hole: Rakudo shows (Mu); Raku++ shows Nil as issue wants
- [#2806](https://github.com/rakudo/rakudo/issues/2806) — `/ <wb>+ /` zero-width quantified terminates in Raku++ without OOM; Rakudo spins until timeout; issue wants an exception
- [#4147](https://github.com/rakudo/rakudo/issues/4147) *(open)* — `/:i /NoMatch/` against Hello World: Raku++ does not match as expected; Rakudo treats leading empty branch as match
- [#4263](https://github.com/rakudo/rakudo/issues/4263) *(open)* — `:ignorecase` with ß/ﬁ multi-char folds: Rakudo gives ｢s｣ ｢ss｣ ｢f｣ ｢fi｣ ｢fid｣; Raku++ gives the issue's expected ｢ss｣ ｢sst｣ ｢fi｣ ｢fid｣ ｢fidd｣
- [#4390](https://github.com/rakudo/rakudo/issues/4390) — `"abc" ~~ /<!space>*/` returns ｢｣ promptly in Raku++; Rakudo loops forever (timeout)
- [#4390](https://github.com/rakudo/rakudo/issues/4390) — `"abc" ~~ /<!space>* d/` returns Nil promptly in Raku++; Rakudo loops forever (timeout)
- [#4468](https://github.com/rakudo/rakudo/issues/4468) *(open)* — `"" ~~ /(.*)*/` hangs in Rakudo (10s timeout); Raku++ finishes immediately
- [#4468](https://github.com/rakudo/rakudo/issues/4468) *(open)* — `"" ~~ /(.?)*/` hangs in Rakudo (10s timeout); Raku++ finishes immediately
- [#4468](https://github.com/rakudo/rakudo/issues/4468) *(open)* — `"" ~~ /<ws>*/` hangs in Rakudo (10s timeout); Raku++ finishes immediately
- [#4782](https://github.com/rakudo/rakudo/issues/4782) — `'abc' ~~ / [z?]+ /`: Rakudo hangs (10s timeout); Raku++ returns at once
- [#5380](https://github.com/rakudo/rakudo/issues/5380) *(open)* — `".[+]" ~~ /^ <ltm-issue> $/` with `[+]  or  [. [ +*%%, ]]` matches in Raku++; Rakudo returns Nil (LTM locks to empty %% match)
- [#5381](https://github.com/rakudo/rakudo/issues/5381) *(open)* — `my token inQuote { \' (.*?) \' }; "'Hello World'" ~~ / <inQuote> /` matches in Raku++ as the issue expects; Rakudo returns Nil
- [#5403](https://github.com/rakudo/rakudo/issues/5403) — `.say for m:g/\d/` prints ｢1｣ ｢2｣ ｢3｣ in Raku++ (legacy behaviour the issue wants); Rakudo 2026.09 prints bare 1 2 3. `dd $_` shows `"1 2 3"` vs Rakudo `$_ = "1 2 3"`
- [#5530](https://github.com/rakudo/rakudo/issues/5530) *(open)* — Regex with `<![\(]>+` quantified lookahead finishes at once in Raku++ (no match); Rakudo hangs past the 10s limit with huge memory, as the issue reports
- [#6087](https://github.com/rakudo/rakudo/issues/6087) *(open)* — `<?after <ab>>` and `<?after <{'ba'}>>` on 'ab ba': Raku++ gives 2,2,2,5 as the issue expects; Rakudo gives the flipped 2,5,5,2
- [#6879](https://github.com/rakudo/rakudo/issues/6879) *(open)* — `"aXb" ~~ m:g/ :r (\w) /` Rakudo 2026.09 dies lang-call VMNull (regression); Raku++ matches and prints a X b

**syntax** (17)

- [#1301](https://github.com/rakudo/rakudo/issues/1301) *(open)* — `constant \s = :();` Rakudo fails with RakuAST FakeSignature error; Raku++ accepts as issue expects
- [#1406](https://github.com/rakudo/rakudo/issues/1406) *(open)* — Rakudo warns Useless use of so on `so run 'false'`; Raku++ gives no warning, which is what the issue wants
- [#2373](https://github.com/rakudo/rakudo/issues/2373) *(open)* — `my $x = 42; .= flip with $x` accepted by Raku++; Rakudo refuses (Preceding context expects a term); issue wants it to DWIM
- [#3587](https://github.com/rakudo/rakudo/issues/3587) *(open)* — Raku++ rejects `my (Str() $x=60 --> Str)` with a parse error; Rakudo silently accepts and drops the return trait, which the issue calls too permissive
- [#3714](https://github.com/rakudo/rakudo/issues/3714) *(open)* — `is looser(&infix:<:>)`: Rakudo dies with undeclared routine; Raku++ accepts it, as the issue wants precedence to be usable
- [#3766](https://github.com/rakudo/rakudo/issues/3766) *(open)* — `class C { has $.a = s; } sub s {}` compiles in Raku++ as the issue expects; Rakudo gives Null regex not allowed error
- [#4081](https://github.com/rakudo/rakudo/issues/4081) *(open)* — spaced chain ending in `.&say` works in Raku++ (prints test); Rakudo refuses 'Malformed postfix call', which the issue calls unwanted
- [#4081](https://github.com/rakudo/rakudo/issues/4081) *(open)* — `.self\n .self.&say` works in Raku++; Rakudo still dies 'Malformed postfix call'
- [#4424](https://github.com/rakudo/rakudo/issues/4424) *(open)* — `5 [&say] 6` prints 56 in both; Rakudo emits bogus useless-use warnings, Raku++ none
- [#4722](https://github.com/rakudo/rakudo/issues/4722) — `say @ary⬆5⬆` with custom postcircumfix: Raku++ prints (3 4 5 6 7) without whitespace, as issue wants; Rakudo fails to parse
- [#4939](https://github.com/rakudo/rakudo/issues/4939) *(open)* — `[ && ]( True, False )` Rakudo dies with confusing No such method CALL-ME on Array; Raku++ accepts spaced reduction as issue suggests
- [#5221](https://github.com/rakudo/rakudo/issues/5221) — `is equiv(&infix:<+=>)` on custom infix:<//=>: Rakudo dies No such method op_props; Raku++ compiles and prints 0
- [#5369](https://github.com/rakudo/rakudo/issues/5369) *(open)* — `.antipairs` newline `.Hash{ 'World!' }` chain: Raku++ accepts as issue wants; Rakudo gives Unexpected block in infix position
- [#5644](https://github.com/rakudo/rakudo/issues/5644) — `$_ = 99; say $_ ~~ Mu:U` prints False in Raku++ as expected; Rakudo 2026.09 still prints True
- [#6763](https://github.com/rakudo/rakudo/issues/6763) — `say try qq:to/MESSAGE/ ... $foo.subst("a", $bar)`: Rakudo says $bar not declared; Raku++ prints xbc as issue expects
- [#6768](https://github.com/rakudo/rakudo/issues/6768) — `say [] ~~ (my $a = "b"); say $a` prints False and b in Raku++ as expected; Rakudo 2026.09 prints True and (Any)
- [#6794](https://github.com/rakudo/rakudo/issues/6794) — `constant &MAIN = sub {}` Rakudo dies No such method meta-object (RakuAST); Raku++ accepts

**lists** (14)

- [#1297](https://github.com/rakudo/rakudo/issues/1297) *(open)* — `@matrix['0';'0'] = True` on shaped array: Rakudo dies "Partially dimensioned views not implemented"; Raku++ accepts as expected
- [#2025](https://github.com/rakudo/rakudo/issues/2025) — `[Z+] @v` with a one-element list-of-lists: Rakudo gives (300); Raku++ gives (100 200) as the issue expects
- [#2122](https://github.com/rakudo/rakudo/issues/2122) *(open)* — `[+] (0, |(1,2...*))` assigned to array: Raku++ prints fine; Rakudo times out
- [#2122](https://github.com/rakudo/rakudo/issues/2122) *(open)* — `my @a = [\+] (0, |(1, 2 ... *))` is lazy in Raku++ (prints hangs); Rakudo times out on the triangle reduce
- [#2267](https://github.com/rakudo/rakudo/issues/2267) *(open)* — `my @a = lazy 1,; my @b = @a.clone; @a[40] = 100; say @b[40]` Rakudo prints 100 (shared cell, the bug); Raku++ prints (Any)
- [#2383](https://github.com/rakudo/rakudo/issues/2383) — `Nil for my $s := "abc".comb[]; say $s`: Rakudo still dies Seq iterator consumed; Raku++ prints (a b c) as the issue wants
- [#2742](https://github.com/rakudo/rakudo/issues/2742) *(open)* — `(0...-∞).is-lazy` Rakudo False (issue calls this wrong for an infinite seq); Raku++ True
- [#3380](https://github.com/rakudo/rakudo/issues/3380) — Infinite gather: both fail `.is-lazy` is True test; Rakudo also fails the no-hang test, Raku++ passes it
- [#3652](https://github.com/rakudo/rakudo/issues/3652) — `@fib ~~ /^9999/` on infinite sequence terminates with Nil in Raku++; Rakudo loops forever
- [#4413](https://github.com/rakudo/rakudo/issues/4413) — `given (^100).hyper.grep:{...}` with `{+$_}` interpolation prints the 25 primes in Raku++; Rakudo dies HyperSeq already consumed
- [#4514](https://github.com/rakudo/rakudo/issues/4514) — `[^100].map({$_ ==> map(&say.assuming("a","b"))})`: Rakudo prints nothing (the reported bug); Raku++ executes the side effects (ab0..ab99)
- [#5068](https://github.com/rakudo/rakudo/issues/5068) — `say (1, IterationEnd, 2)` and gather give `(1 IterationEnd 2)` in Raku++ as the issue expects; Rakudo 2026.09 still truncates to `(1)`
- [#5122](https://github.com/rakudo/rakudo/issues/5122) — `@arr[1^..*;1]` hangs in Rakudo (timeout); Raku++ returns (8 11) as expected
- [#5538](https://github.com/rakudo/rakudo/issues/5538) *(open)* — `(@a Z @b)` with interior Whatever: Rakudo gives ((a 1) (a 2) (c 3)); Raku++ keeps `*` as issue expects

**dispatch** (13)

- [#1594](https://github.com/rakudo/rakudo/issues/1594) — Rakudo still explodes with No such method 'blumber' (better message); Raku++ silently falls to the second multi, matching never-explodes
- [#1947](https://github.com/rakudo/rakudo/issues/1947) *(open)* — `multi sub a (IO(Cool:D) $path) {}; a "foo".IO` runs in Raku++; Rakudo still reports Ambiguous call
- [#2319](https://github.com/rakudo/rakudo/issues/2319) *(open)* — `-> Int() @v {...}(Array[Any].new: ...)`: Rakudo binds oddly (Int to @-param); Raku++ throws a type-check error as the issue asks
- [#2319](https://github.com/rakudo/rakudo/issues/2319) *(open)* — Rakudo binds an Array[Any] to `Int() @v` oddly; Raku++ throws X::TypeCheck::Binding::Parameter, which the issue says it should do
- [#2548](https://github.com/rakudo/rakudo/issues/2548) *(open)* — `multi infix:<~>($a, C) is default {42}; "test" ~ C.new` prints 42 in Raku++ as the issue expects; Rakudo still stringifies
- [#3780](https://github.com/rakudo/rakudo/issues/3780) *(open)* — multi p with where-clause and `do :bar if True`: Rakudo reports misleading `p(bar => True, :foo(42))`; Raku++ reports `p(Pair:D, :foo(Int))` as issue wants
- [#4778](https://github.com/rakudo/rakudo/issues/4778) *(open)* — `multi foo(Int:D(Cool:D) $foo)` called with "1": Raku++ prints 1 as issue expects; Rakudo refuses at compile time
- [#4991](https://github.com/rakudo/rakudo/issues/4991) — `@a ==> foo($c:) ==> say()` feed with sub-form method call works in Raku++ as expected; Rakudo still refuses at compile time
- [#5038](https://github.com/rakudo/rakudo/issues/5038) — `multi method foo(Any :$default!)` with `:42default` prints 2 in Raku++ as expected; Rakudo prints 1
- [#5056](https://github.com/rakudo/rakudo/issues/5056) *(open)* — `multi foo(Hash:D)` vs `foo(Value(Any:D))` with `foo({:foo})`: Rakudo Ambiguous call; Raku++ prints yes as issue expects
- [#5177](https://github.com/rakudo/rakudo/issues/5177) *(open)* — `multi open-xml(IO::Path(FileStr) $src)` fails at runtime with NoMatch in Raku++ (file missing), as expected; Rakudo gives compile-time SORRY
- [#6539](https://github.com/rakudo/rakudo/issues/6539) — `sub to-roman(PosInt() $n is copy)` returns -1 in Raku++ (the 2026.07 result); Rakudo 2026.09 dies Impossible coercion from 'Int' into 'PosInt'
- [#6711](https://github.com/rakudo/rakudo/issues/6711) — `&ln.assuming('ab', *)` on a multi: Rakudo dies no candidate; Raku++ prints "ab kjh jgh" as issue expects

**concurrency** (12)

- [#1200](https://github.com/rakudo/rakudo/issues/1200) — Rakudo dies "Phasers in hyper/race not yet implemented"; Raku++ runs and prints `last` as the issue expects
- [#1612](https://github.com/rakudo/rakudo/issues/1612) *(open)* — Rakudo still blocks Supply ticks until the Seq yields; Raku++ prints Supply lines first then the message then Seq lines, as the issue expects
- [#2008](https://github.com/rakudo/rakudo/issues/2008) — `$c.Seq.hyper(:1batch,:1degree)` prints 42 in Raku++; Rakudo hangs waiting for an extra item
- [#2193](https://github.com/rakudo/rakudo/issues/2193) *(open)* — supply{emit 1}.share then react whenever finishes in Raku++; Rakudo hangs (issue expects live supply, no hang)
- [#2207](https://github.com/rakudo/rakudo/issues/2207) — Proc::Async with bad command: Rakudo leaks Unhandled exception on thread; Raku++ passes the failure to CATCH and prints caught!
- [#2646](https://github.com/rakudo/rakudo/issues/2646) — Raku++ receives both channel items across two react blocks; Rakudo waits on the 10s timeout; but `"$channel"` stringifies as a multi-line field dump instead of Channel<addr>
- [#2834](https://github.com/rakudo/rakudo/issues/2834) — `start { shell ... }` with max_threads=2 completes in Raku++; Rakudo 2026.09 still hangs (timeout)
- [#2931](https://github.com/rakudo/rakudo/issues/2931) *(open)* — `IO::Socket::Async.udp(:broadcast)` Rakudo dies "bad file descriptor"; Raku++ returns an object as issue expects
- [#4654](https://github.com/rakudo/rakudo/issues/4654) — `$*a` inside `await start {...}` in react/whenever Promise.in: Raku++ finds 42 (ok 1); Rakudo still gives Failure (not ok 1)
- [#5478](https://github.com/rakudo/rakudo/issues/5478) *(open)* — `race for ^10 { return 2 if 5 }` inside a sub exits cleanly in Raku++; Rakudo panics "Did not find expected unwind target frame"
- [#5611](https://github.com/rakudo/rakudo/issues/5611) *(open)* — `await start { await start { say $*FOO } }`: Raku++ prints FOO as expected; Rakudo dies Dynamic variable $*FOO not found
- [#5976](https://github.com/rakudo/rakudo/issues/5976) — `my $p1 = start loop {...}` runs in Raku++ (hello and world interleave); Rakudo still prints only world, as the issue reports

**scope** (7)

- [#2542](https://github.com/rakudo/rakudo/issues/2542) *(open)* — `(my int $x = 10)++` and `($x = 10)++` on native int accepted by Raku++ (prints 11, 1); Rakudo still refuses; issue wants lvalue
- [#3169](https://github.com/rakudo/rakudo/issues/3169) *(open)* — `my $a := 1 if 0; say $a.perl` prints Any in Raku++ as expected; Rakudo dies on a VMNull
- [#3220](https://github.com/rakudo/rakudo/issues/3220) *(open)* — `%()` and `{}` both report &?ROUTINE foo in Raku++ (consistent, as issue wants); Rakudo disagrees; snippet has trailing output text
- [#3270](https://github.com/rakudo/rakudo/issues/3270) — `PROCESS::<$v> = 1; $*v.VAR.dynamic` gives True in Raku++ as the issue expects; Rakudo still prints False
- [#5826](https://github.com/rakudo/rakudo/issues/5826) — `multi sub bar` in `start` block captures own `$count` and `$id` per call in Raku++ (1/1); Rakudo prints stale hello from 2! twice
- [#5912](https://github.com/rakudo/rakudo/issues/5912) — `use v6.e.PREVIEW; subset A of Int; my A $a; say $a.VAR.default` Raku++ gives (A) as expected; Rakudo gives (Int)
- [#6053](https://github.com/rakudo/rakudo/issues/6053) — `my ( $p,) := a => 'b'` is accepted by Raku++ as the issue wants; Rakudo still dies "Too few positionals"

**nativecall** (6)

- [#2589](https://github.com/rakudo/rakudo/issues/2589) — `class A is repr('CStruct') { has Str $.s is rw }; A.new(s => 'Hello')`: Rakudo dies "assign requires a concrete object"; Raku++ constructs it as the issue expects
- [#2633](https://github.com/rakudo/rakudo/issues/2633) *(open)* — `explicitly-manage` result in `my Str $foo` works in Raku++; Rakudo prints 8 then fails in nativecast of the Str
- [#2633](https://github.com/rakudo/rakudo/issues/2633) *(open)* — `explicitly-manage($foo)` then nativecast works in Raku++; Rakudo dies in nativecast
- [#2833](https://github.com/rakudo/rakudo/issues/2833) — `is native` with framework path lacking .dylib: Rakudo appends libX.dylib and fails to load; Raku++ loads it and prints 0 as issue expects
- [#3342](https://github.com/rakudo/rakudo/issues/3342) — Infinite CStruct/CArray loop: Rakudo aborts (exit -5, SIGTRAP) as issue reports; Raku++ keeps looping until timeout
- [#4961](https://github.com/rakudo/rakudo/issues/4961) — Empty `class X is repr('CStruct') {}`: Rakudo refuses "has no attributes"; Raku++ accepts as the issue requests

**numeric** (6)

- [#1581](https://github.com/rakudo/rakudo/issues/1581) *(open)* — `(1 ..^ bigRat).pick`: Rakudo returns Nil as the issue complains; Raku++ returns a big integer
- [#1707](https://github.com/rakudo/rakudo/issues/1707) *(open)* — `1 +< -1000000` gives 0 in Raku++ as expected; Rakudo still gives 1
- [#2541](https://github.com/rakudo/rakudo/issues/2541) *(open)* — int16 32767 post-increment: Raku++ prints 32767,32767,-32768 as expected; Rakudo prints -1,-1,0 (redeclared $x in snippet)
- [#3641](https://github.com/rakudo/rakudo/issues/3641) *(open)* — `(-5.0)**(-1)` gives -0.2 in Raku++; Rakudo still prints wrong -1.8 for the first line
- [#4070](https://github.com/rakudo/rakudo/issues/4070) *(open)* — Raku++ runs `uint32 $u is rw` subs fine; Rakudo dies Bytecode validation error decont_u (open issue)
- [#5074](https://github.com/rakudo/rakudo/issues/5074) *(open)* — `srand 1; say (1..100).pick` repeats the same value in Raku++ as expected; Rakudo gives 32 then 53 53 (seed not reset)

**control** (5)

- [#1360](https://github.com/rakudo/rakudo/issues/1360) ✓ — `{ Failure.new for ^5 }()` throws Failed in Raku++ as the issue expects; Rakudo 2026.09 still stays silent
- [#2926](https://github.com/rakudo/rakudo/issues/2926) *(open)* — `do for` with state/NEXT yields (1 2 2 3) in Raku++ as expected; Rakudo gives (3 3 3 3)
- [#3839](https://github.com/rakudo/rakudo/issues/3839) *(open)* — `False ff .take` side effect: Raku++ gives () as issue expects; Rakudo still evaluates RHS giving (1 2 3 4 5)
- [#6912](https://github.com/rakudo/rakudo/issues/6912) *(open)* — `for 1 { (4,5,6).map({say $_}) }` as last statement prints 4 5 6 in Raku++; Rakudo prints nothing (sunk lazy map)
- [#6912](https://github.com/rakudo/rakudo/issues/6912) *(open)* — nested `.map` inside outer map prints inner lines in Raku++; Rakudo prints only outer (inner map never runs)

**strings** (5)

- [#1334](https://github.com/rakudo/rakudo/issues/1334) *(open)* — `say "&[ ]"` Rakudo still dies Missing infix inside []; Raku++ prints the string as the issue expects
- [#1837](https://github.com/rakudo/rakudo/issues/1837) — `(my str @)[1] ~ ""` gives "" in Raku++; Rakudo throws concatenate requires a concrete string, got null
- [#3293](https://github.com/rakudo/rakudo/issues/3293) *(open)* — `'🇺🇸'.encode('utf32')` and `UTF-32` variants work in Raku++; Rakudo 2026.09 still says Unknown string encoding 'utf32'
- [#3893](https://github.com/rakudo/rakudo/issues/3893) *(open)* — `"a/b:c".split("/" or ":")` gives (a b c) in Raku++, honouring the junction like grep as issue wants; Rakudo returns a Junction of lists
- [#6590](https://github.com/rakudo/rakudo/issues/6590) — `say "foo$a"` with a Junction interpolates to any(fooTrue, fooFalse) in Raku++; Rakudo dies 'cannot unbox to native string'

**eval** (4)

- [#4161](https://github.com/rakudo/rakudo/issues/4161) *(open)* — `my int32 $a = 2; EVAL q/say $a/` Rakudo dies Lexical has wrong type; Raku++ prints 2 as expected
- [#4161](https://github.com/rakudo/rakudo/issues/4161) *(open)* — `my num32 $a = 2e0; EVAL q/say $a/` Rakudo dies Unhandled lexical type; Raku++ prints 2 as expected
- [#4161](https://github.com/rakudo/rakudo/issues/4161) *(open)* — `my uint32 $a = 2; EVAL q/say $a/` Rakudo dies Lexical has wrong type; Raku++ prints 2 as expected
- [#5260](https://github.com/rakudo/rakudo/issues/5260) *(open)* — `require $x` with undeclared $x: Raku++ reports Variable '$x' is not declared as the issue wants; Rakudo still shows VMNull .IO error

**exceptions** (3)

- [#1678](https://github.com/rakudo/rakudo/issues/1678) *(open)* — `->UInt @a {}([])` Raku++ says expected Positional[UInt] as the issue wants; Rakudo says UInt
- [#2561](https://github.com/rakudo/rakudo/issues/2561) *(open)* — `sub foo(uint32 $a, UInt $b where ^2)` Raku++ gives the correct constraint-failed message; Rakudo gives misleading 'Lexical $a has a different type'
- [#2725](https://github.com/rakudo/rakudo/issues/2725) *(open)* — `say 42; redo` both die; Rakudo wrongly prefixes ===SORRY!=== (issue says it is runtime); Raku++ prints plain runtime error but no line number

**grammar** (3)

- [#1870](https://github.com/rakudo/rakudo/issues/1870) — token named `CREATE` parses fine in Raku++ as the issue expects; Rakudo still dies Too many positionals
- [#3217](https://github.com/rakudo/rakudo/issues/3217) *(open)* — Raku++ calls no actions from the negative lookahead (prints Nil); Rakudo still calls `keyword` and `before` actions, as the open issue reports
- [#6709](https://github.com/rakudo/rakudo/issues/6709) *(open)* — `token alt { <call>  or  <asg> }` with `<e>* % ','` matches f(1,2) and f(1) in Raku++ as expected; Rakudo LTM still prints no match

**io** (3)

- [#5357](https://github.com/rakudo/rakudo/issues/5357) — `$*IN.close; shell("ls",:!err,:!out)` Rakudo False on macOS (the bug); Raku++ True as expected
- [#5842](https://github.com/rakudo/rakudo/issues/5842) *(open)* — `"foo/../foo/file01.txt".IO.succ` gives file02 in Raku++ as expected; Rakudo returns the path unchanged
- [#6901](https://github.com/rakudo/rakudo/issues/6901) — `IO::Path.slurp` of a 1 MiB file: Rakudo dies Out of range and leaks handles; Raku++ reads 1048576 bytes with no leak

**cli** (2)

- [#4889](https://github.com/rakudo/rakudo/issues/4889) *(open)* — `sub MAIN (:$a where say ...)` Raku++ does not pass Bool::True to the where clause first, as the issue wants; Rakudo does
- [#4889](https://github.com/rakudo/rakudo/issues/4889) *(open)* — `:$a where !.defined || say ...` Raku++ never sees Bool::True (prints nothing); Rakudo prints value: Bool::True

**hashes** (2)

- [#4203](https://github.com/rakudo/rakudo/issues/4203) — `%b{ or  or @a} = 42` after `@a = <a b c>` runs without error in Raku++; Rakudo dies Type Int does not support associative indexing (issue's bug)
- [#4971](https://github.com/rakudo/rakudo/issues/4971) — Assigning `$m<alpha> = 'foo'` on a bound Map: Raku++ dies "Cannot change key in an immutable Map" as issue expects; Rakudo silently assigns

**signatures** (2)

- [#3387](https://github.com/rakudo/rakudo/issues/3387) — `sub count($a,$b){ $foo{$a=>$b}+=1 }` on BagHash: Raku++ gives one key as expected; Rakudo gives duplicate :a("b") key for untyped params
- [#5508](https://github.com/rakudo/rakudo/issues/5508) *(open)* — `sub a(Foo:D %f)` with `my Foo:D %h` binds fine in Raku++ (prints `(my Foo:D %)`); Rakudo dies constraint type check

**attributes** (1)

- [#1798](https://github.com/rakudo/rakudo/issues/1798) *(open)* — `has &.c where {...}` attribute type shows `Mu &!c` in Raku++ not the odd `Callable[<anon>]` Rakudo shows

**backend** (1)

- [#5697](https://github.com/rakudo/rakudo/issues/5697) — `nqp::existskey(Hash.new,"foo")` Rakudo segfaults (exit -11); Raku++ prints 0 as expected

**formatting** (1)

- [#1514](https://github.com/rakudo/rakudo/issues/1514) *(open)* — `sprintf("%lf",10.0)` gives 10.000000 as the issue wants; Rakudo still refuses directive lf

**misc** (1)

- [#1875](https://github.com/rakudo/rakudo/issues/1875) — `pack('C4',[31,32,33,34])` Raku++ gives Buf:0x<1F 20 21 22> as issue expects; Rakudo still gives only Buf:0x<1F>

**modules** (1)

- [#1805](https://github.com/rakudo/rakudo/issues/1805) *(open)* — `require %*ENV<FOO>`: Rakudo dumps the stringified %*ENV in its error; Raku++ reports a clean empty module name error

**perf** (1)

- [#5733](https://github.com/rakudo/rakudo/issues/5733) — run of generated 1M-deep `%x<x>...` file times out in Rakudo; Raku++ finishes with False and 'finished'

**unicode** (1)

- [#1470](https://github.com/rakudo/rakudo/issues/1470) *(open)* — Rakudo gives Malformed UTF-8; Raku++ gives Failed to open file /doesnotexist/...: No such file, as the issue expects


### SHARED (72)

**oo** (17)

- [#1383](https://github.com/rakudo/rakudo/issues/1383) *(open)* — `Array[::T] \a, Array[T] \b` unsupported in both: Rakudo compile error on T, Raku++ runtime type-check failure
- [#1426](https://github.com/rakudo/rakudo/issues/1426) *(open)* — both accept `has Int:D $.count is required` silently; issue notes inconsistency only
- [#1555](https://github.com/rakudo/rakudo/issues/1555) *(open)* — `has ::("Int") $.foo = 42` fails in both (Rakudo type check error; Raku++ no accessor foo)
- [#1555](https://github.com/rakudo/rakudo/issues/1555) *(open)* — `has ::("Int") $.foo .= new: 42` fails in both (Rakudo too-many-positionals; Raku++ no method foo)
- [#2384](https://github.com/rakudo/rakudo/issues/2384) *(open)* — Class method `bar(Str)` accepted for role stub `bar(Int)` in both; open issue wants a compile error
- [#2546](https://github.com/rakudo/rakudo/issues/2546) *(open)* — `role C[$n] does B[$n]`: Rakudo prints (Any) not 5; Raku++ dies Variable $n not declared; neither gives 5
- [#2776](https://github.com/rakudo/rakudo/issues/2776) *(open)* — `1 does T(:x(5))` then `.a.raku`: Rakudo gives 5, Raku++ gives Any; issue expects `:x(5)`, neither does
- [#3519](https://github.com/rakudo/rakudo/issues/3519) *(open)* — `*@foo` param: .positional False and .slurpy True in both; issue wants consistency with *%foo being named
- [#4014](https://github.com/rakudo/rakudo/issues/4014) — `Signature.new(params => (...), count => 3, arity => 2)`: Rakudo gives empty signature, Raku++ ignores count and arity (4/4); expected arity 2 count 3
- [#4503](https://github.com/rakudo/rakudo/issues/4503) *(open)* — Both accept `my method ^m(\a) { 42 }` with no error although it can never be called
- [#4534](https://github.com/rakudo/rakudo/issues/4534) *(open)* — `[:key<keyval> => "val", ...].Capture.raku` stringifies the pair key (with a tab) in both engines; issue expects non-stringified Pair keys
- [#4534](https://github.com/rakudo/rakudo/issues/4534) *(open)* — `%h.Capture.raku` with Any-keyed Pair keys stringifies them with a tab in Raku++ and as "bar\tbaz" in Rakudo; both show the reported bug
- [#5515](https://github.com/rakudo/rakudo/issues/5515) *(open)* — enum with `but MixedInRoleName[Str]` values dies in both (type check of two role instantiations); Raku++ names roles `MixedInRoleNamepun1`
- [#5802](https://github.com/rakudo/rakudo/issues/5802) *(open)* — `sub a(Positional[::T] $a, T $b)` rejected by both with "Invalid typename 'T'"; open issue wants the generic recognised
- [#5815](https://github.com/rakudo/rakudo/issues/5815) *(open)* — `method TWEAK { start die }` unhandled: neither engine reports the Died exception from the sunk Promise; issue expects one
- [#5966](https://github.com/rakudo/rakudo/issues/5966) *(open)* — `role Defaults { ... }; class Site does Defaults` both die: no appropriate parametric role variant; stubbed role cannot be composed
- [#6171](https://github.com/rakudo/rakudo/issues/6171) *(open)* — Raku++ gives False for `Int:D ~~ Int:D`, `Str:D ~~ Str:D(Int)`, `Str:D ~~ Cool:D` and True for `Int:D ~~ Str:D(Int)`; Rakudo dies Impossible coercion on `Int:D ~~ Str(Int:D)`; issue expects all True but the lolwut one

**syntax** (13)

- [#1365](https://github.com/rakudo/rakudo/issues/1365) *(open)* — `::("Int"):H.^name.say` should be an error; Rakudo gives Failure, Raku++ prints Pair
- [#1365](https://github.com/rakudo/rakudo/issues/1365) *(open)* — `::("Int"):D.^name.say` should throw; Rakudo prints Failure, Raku++ prints Pair
- [#1592](https://github.com/rakudo/rakudo/issues/1592) *(open)* — `@a».:<++>` should increment; Rakudo dies with RakuAST type error, Raku++ prints `4` instead of [2, 3, 4]
- [#1938](https://github.com/rakudo/rakudo/issues/1938) *(open)* — `:foo<1/2>.value` gives RatStr in both; issue says it should be Rat
- [#3111](https://github.com/rakudo/rakudo/issues/3111) *(open)* — `sub s() {}; say s/b/d/` fails with Missing required term after infix in both; issue expects it to match
- [#4382](https://github.com/rakudo/rakudo/issues/4382) *(open)* — `0 ==> say` both refuse with Argument to say seems to be malformed; issue says it should work like `say()`
- [#4382](https://github.com/rakudo/rakudo/issues/4382) *(open)* — `0 ==> say;` both refuse with Unsupported use of bare say; issue reports this as a bug
- [#4465](https://github.com/rakudo/rakudo/issues/4465) — `my \n = 3; say 2ⁿ` unsupported in both: Rakudo Confused; Raku++ prints 2 then dies Undefined routine 'ⁿ'
- [#4996](https://github.com/rakudo/rakudo/issues/4996) *(open)* — `$nested .= <dummy>` fails in both (Rakudo No such method postcircumfix:<{ }>, Raku++ parse error); issue expects it to work
- [#5029](https://github.com/rakudo/rakudo/issues/5029) *(open)* — `sub (\ is readonly) { }(1)` is refused by both (Raku++ "Two terms in a row"); issue wants it to parse like the `$` case
- [#5268](https://github.com/rakudo/rakudo/issues/5268) *(open)* — `has $.fun with $*Raku;` accepted without warning in both; issue wants warning or error
- [#6045](https://github.com/rakudo/rakudo/issues/6045) *(open)* — Both give runtime `No such method 'CALL-ME' for invocant of type 'Pair'` for `foo :$a(42)`; issue wants a compile-time complaint
- [#6926](https://github.com/rakudo/rakudo/issues/6926) *(open)* — `"Hello, World!" ==> say;` refused as bare "say" in both; open issue expects it to work like `say()`

**dispatch** (11)

- [#1284](https://github.com/rakudo/rakudo/issues/1284) *(open)* — both silently accept `-> ::($x) $y` with Int() and print 42.2; issue wants error or working
- [#1284](https://github.com/rakudo/rakudo/issues/1284) *(open)* — both silently accept `-> ::($x) $y` and print 42.2; issue wants a type check or error
- [#1474](https://github.com/rakudo/rakudo/issues/1474) *(open)* — `unique(1,2,3,3,:as(<non-Callable>))` silently ignores the bad named arg in both engines; the issue wants an error
- [#1561](https://github.com/rakudo/rakudo/issues/1561) *(open)* — Neither engine calls the `.wrap`per on `postfix:<++>`; no output on both, as the open issue reports
- [#1764](https://github.com/rakudo/rakudo/issues/1764) *(open)* — `proto z($) {*}; multi z($, $) {}` is accepted silently by both; issue wants compile-time failure
- [#2497](https://github.com/rakudo/rakudo/issues/2497) *(open)* — `[[&f]] 1,2,3` with `is assoc<right>` gives -4 in both; issue expects 2
- [#4545](https://github.com/rakudo/rakudo/issues/4545) *(open)* — `proto f returns Int {*}` with multi returning Str: neither engine errors; issue wants return check
- [#4727](https://github.com/rakudo/rakudo/issues/4727) *(open)* — `proto f(Str, or ) {*}; f $b` with Int $b: neither engine throws at runtime, both dd 42
- [#5162](https://github.com/rakudo/rakudo/issues/5162) *(open)* — Both die for adding constrained multi `infix:<+>` in anon class; Rakudo Circularity detected, Raku++ fails with `Default constructor for 'Mu' only takes named arguments` (`::?CLASS.new($r)`)
- [#5289](https://github.com/rakudo/rakudo/issues/5289) *(open)* — trait_mod is on multi candidates: Raku++ prints foo/foo, Rakudo foo/failer; expected foo/stringer, both wrong

**lists** (7)

- [#2133](https://github.com/rakudo/rakudo/issues/2133) *(open)* — `({:42foo},)»{}` Rakudo dies .nodemap single parameter; Raku++ parse error Confused; issue expects ({:foo(42)},)
- [#3597](https://github.com/rakudo/rakudo/issues/3597) *(open)* — `(Inf ^.. Inf).reverse[^10]`: Raku++ prints ten Nil (the reported bug); Rakudo dies Cannot .reverse a lazy list; issue expects Inf values
- [#3599](https://github.com/rakudo/rakudo/issues/3599) *(open)* — `(NaN .. 0).reverse[^10]` gives Nils in both while `(0 .. NaN)[^10]` gives 0..9; open issue says they should agree
- [#3600](https://github.com/rakudo/rakudo/issues/3600) *(open)* — `say (Inf ... Inf)[^10]` both give ten Inf; issue expects sequence to stop after first Inf
- [#3658](https://github.com/rakudo/rakudo/issues/3658) — `@a[-5..∞]:delete` neither deletes: Rakudo dies Index out of range, Raku++ silently leaves [3 4 5]
- [#5254](https://github.com/rakudo/rakudo/issues/5254) *(open)* — `foo(List)` with `@a` gives `[1 0]` (elems vs +) in both; Raku++ `my @b := List; [@b.elems, +@b]` gives `[1 1]`, Rakudo `[1 0]`; type object bound to @ is inconsistent
- [#5907](https://github.com/rakudo/rakudo/issues/5907) *(open)* — `@foo.grep('U', :k)` on a shaped 2;2 array gives (3) in both; issue expects the (1,1) index tuple

**regex** (7)

- [#1924](https://github.com/rakudo/rakudo/issues/1924) *(open)* — `'aaaaaaaaa' ~~ m/ alternation of a+? and a+ /` both give ｢a｣; issue expects the longest ｢aaaaaaaaa｣
- [#1924](https://github.com/rakudo/rakudo/issues/1924) *(open)* — same snippet printing ｢a｣; both engines show the reported LTM frugal quantifier behaviour
- [#2205](https://github.com/rakudo/rakudo/issues/2205) *(open)* — `@<meow>` for single capture gives () in both; issue wants one-item list
- [#2205](https://github.com/rakudo/rakudo/issues/2205) *(open)* — `%<meow>` for single capture gives empty Map in both; issue wants odd-elements error
- [#2206](https://github.com/rakudo/rakudo/issues/2206) *(open)* — `say [$<meow>, @<meow>]` gives `[｢foo｣ ()]` in both; issue reports @<meow> inconsistency
- [#5981](https://github.com/rakudo/rakudo/issues/5981) *(open)* — `$<attrib-val>=[<-[$<quote>]>*]` interpolated in a class: both only print r2; issue expects r1 too
- [#5982](https://github.com/rakudo/rakudo/issues/5982) *(open)* — `$t.match($r1, :$pos)` loop prints Found! once in both; issue expects twice

**scope** (3)

- [#1603](https://github.com/rakudo/rakudo/issues/1603) *(open)* — Both print `(Any)` for `$Foo::z` after `$z := 42` binding, as the open issue reports (no error or value)
- [#4531](https://github.com/rakudo/rakudo/issues/4531) *(open)* — `say my Int:D() $x` prints (Int) in both; open issue expects 0 or an error
- [#4766](https://github.com/rakudo/rakudo/issues/4766) *(open)* — Both accept `Foo::Some::Baz.new` for a `my class` escaping its package; issue says it should die

**strings** (3)

- [#2307](https://github.com/rakudo/rakudo/issues/2307) *(open)* — `"Zoffix".encode.Stringy` gives a Str in both, where the open issue wants self
- [#3509](https://github.com/rakudo/rakudo/issues/3509) *(open)* — Both die `Will not decode invalid ASCII` ignoring `:replacement("0"), :strict(False)`, as the open issue reports
- [#6030](https://github.com/rakudo/rakudo/issues/6030) *(open)* — `'textbook'.comb(4/1 Rat, 2)` returns () in both; issue expects (text book)

**control** (2)

- [#2626](https://github.com/rakudo/rakudo/issues/2626) *(open)* — Both print `Bool::False` for `do when 0==1 {}`; issue expects Empty
- [#2626](https://github.com/rakudo/rakudo/issues/2626) *(open)* — Both give `Bool::False` for failed `when` (issue wants Empty); Raku++ also prints 42 for last `do when 0==0 {42}` where Rakudo prints nothing

**cli** (1)

- [#5741](https://github.com/rakudo/rakudo/issues/5741) *(open)* — GENERATE_USAGE for unknown option: both print usage only, no Did you mean suggestion

**concurrency** (1)

- [#3772](https://github.com/rakudo/rakudo/issues/3772) — `return 42` inside `react whenever` in a sub should return 42; Rakudo panics, Raku++ dies 'Attempt to return outside of any Routine'

**exceptions** (1)

- [#3301](https://github.com/rakudo/rakudo/issues/3301) *(open)* — `sub gg { try EVAL "fail 123" }; gg` still dies in both; issue expects try to contain the failure

**grammar** (1)

- [#3216](https://github.com/rakudo/rakudo/issues/3216) *(open)* — `regex before { . }` in a grammar is accepted silently by both; issue wants a complaint

**io** (1)

- [#2197](https://github.com/rakudo/rakudo/issues/2197) *(open)* — `run ..., :err(.open)` with a read-mode handle fails silently in both, as the issue complains

**junctions** (1)

- [#1456](https://github.com/rakudo/rakudo/issues/1456) *(open)* — none True ~~ Failure and not .handled: both die No such method handled for Bool, as the open issue reports

**nativecall** (1)

- [#4969](https://github.com/rakudo/rakudo/issues/4969) *(open)* — CStruct `HAS int32 @.xs[4] is CArray`: Raku++ gives `(0)` and `$s.xs[7] = 42` raises no error; expected 4 zeros and an out-of-bounds error (Rakudo leaks random data)

**numeric** (1)

- [#5950](https://github.com/rakudo/rakudo/issues/5950) *(open)* — `atan2(1+2i, 3-1i)`: Rakudo hangs; Raku++ returns NaN; issue wants a TypeError-style failure

**signatures** (1)

- [#5929](https://github.com/rakudo/rakudo/issues/5929) *(open)* — `sub bla(Capture (:%hash, :@list, |))` with `\(1,2,:a,:42b)` gives empty hash and list in both; issue reports this

