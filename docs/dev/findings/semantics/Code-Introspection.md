# Code, Block, Routine, Signature, Parameter, Attribute, WhateverCode, ForeignCode — semantics sheet

Provenance: Rakudo tag `2026.08`, files `src/core.c/Code.rakumod` (66
lines), `Block.rakumod` (196), `Routine.rakumod` (417),
`Signature.rakumod` (264), `Parameter.rakumod` (689),
`Attribute.rakumod` (295), `WhateverCode.rakumod` (34),
`ForeignCode.rakumod` (207), `traits.rakumod` (626), `Sub.rakumod` (6),
`Method.rakumod` (7), `Submethod.rakumod` (7), `Macro.rakumod` (4), all
read in full on 2026-09-23, plus the `assuming` method in
`core_epilogue.rakumod` and the exception classes this surface throws in
`Exception.rakumod`. What these files declare only as attributes
(`name`, `dispatcher`, `is_dispatcher`, `onlystar`, `yada`, `rw`,
`has_returns`, `set_rw`, the binder itself) was measured, not read. Out
of scope: the metamodel (`.^methods`, `.^can`, `.^parents`), `CallFrame`
and `callframe`, `MAIN` and `USAGE`, `Proxy`, native `is rw` parameters,
macros, `samewith`/`nextcallee`. Oracle: Homebrew Rakudo v2026.08 on
macOS. Compared against Raku++ 4.0.1-84-ga4291988 (build-arm64,
2026-09-23). Format and legend: [README.md](README.md).

Where this sits against the declared spec: Rakudo passes 33 of the 35
`S06-signature` files, 9 of 11 `S06-advanced`, all 5 `S06-currying`,
all 10 `S06-traits`, all 4 `S06-routine-modifiers`, all 8 `S06-other`,
all 9 `S12-introspection` and 12 of 13 `S12-attributes` on this machine,
90 of 95; Raku++ passes 8, 2, 4, 3, 1, 1, 2 and 2, 23 of 95. The rules
below are what those files and real programs lean on: what a code
object answers about itself, what a call does with the arguments it is
given and with the value it returns, how a signature is printed,
compared and bound, what a `where`, a coercion, a slurpy or an `is rw`
does to an argument, how `wrap`, `assuming` and the routine traits
change a routine, and what an attribute says about itself. 24 of the 37
items are neither fully documented nor fully asserted by Roast. Eight
Rakudo behaviours are recorded as bugs and eleven as quirks; step two
should not imitate the bugs.

Every probe ran with `alarm 10`, standard input closed, in a fresh
sandbox directory per engine. No probe output contains a path or an
object address: where an address would print (`Parameter<…>`,
`ForeignCode<…>`, a Block in `.raku`), the probe replaces the digits by
`N` or elides the comment as `#`(…)`. Where a probe line also exercises
neighbouring items, the statement says which fields matter.

## A. Code and Block

### CO-01  arity and count                                          D:yes R:yes V:spec
`.arity` is the number of required positionals, `.count` the maximum
number of positionals accepted: an optional or defaulted positional
adds to `count` only, a named parameter to neither, a positional slurpy
(`*@`, `**@`, `+@`, `+a`) or a capture (`|c`) makes `count` `Inf`, a
`\x` or `&c` parameter counts as one, and an anonymous `$` counts.
`count` is an `Int` unless it is `Inf`, then a `Num`. A method counts
its invocant, so `method m($x)` has arity 2 and count 2. A block with
no signature has arity 0 and count 1 (its implicit `$_`), `{;}` too, and
placeholders set both (`{ $^a + $^b }` is 2); `{ $:x }` is 0 and 0. A
WhateverCode counts its stars. `&say` is 0 and `Inf`; `Str.^lookup("comb")`
(a proto) is 1 and `Inf`. `Signature.arity`/`.count` answer the same.
```
say do { sub a() {}; sub b($x, $y?) {}; sub c($x, $y, *@z) {}; sub d(:$n) {}; sub e($x, **@z) {}; sub f(+@z) {}; sub g(|c) {}; sub h($x = 1) {}; sub i(*%h) {}; sub j(\x) {}; sub k(&c) {}; sub l($x, :$n!) {}; my method m($x) {}; my $w1 = * + 1; my $w2 = * + *; (&a.arity, &a.count, &b.arity, &b.count, &c.arity, &c.count, &d.arity, &d.count, &e.arity, &e.count, &f.arity, &f.count, &g.arity, &g.count, &h.arity, &h.count, &i.arity, &i.count, &j.arity, &j.count, &k.arity, &k.count, &l.arity, &l.count, &a.count.^name, &c.count.^name, &a.arity.^name, { $_ }.arity, { $_ }.count, {;}.arity, {;}.count, { $^a + $^b }.arity, { $:x }.arity, { $:x }.count, (-> $a, $b? {}).arity, (-> $a, $b? {}).count, $w1.arity, $w2.count, &m.arity, &m.count, &say.arity, &say.count, Str.^lookup("comb").arity, Str.^lookup("comb").count, sub ($a, $b, $c?, *@d) {}.arity, sub (:$a!, :$b!) {}.arity, sub ($a, $b) {}.signature.arity, sub ($a, $b) {}.signature.count, sub (*@a, *%h) {}.count, sub (|) {}.count, sub ($a, $b = 1, $c?) {}.arity, sub ($a, $b = 1, $c?) {}.count, sub (\a, |c) {}.arity, sub (\a, |c) {}.count, sub ($a, +@b) {}.count, sub ($a, +@b) {}.arity, sub (&c) {}.count, sub ($, $) {}.arity).join(" ") }
# rakudo 2026.08: 0 0 1 2 2 Inf 0 0 1 Inf 0 Inf 0 Inf 0 1 0 0 1 1 1 1 1 1 Int Num Int 0 1 0 1 2 0 0 1 2 1 2 2 2 0 Inf 1 Inf 2 0 2 2 Inf Inf 1 3 1 Inf Inf 1 1 2
```
rakupp 4.0.1-84: differs in 11 of 58 fields — a defaulted positional is counted as required (`sub ($x = 1)` has arity 1, `($a, $b = 1, $c?)` arity 2), a method does not count its invocant (1 and 1), a block's implicit `$_` and its placeholders set nothing (`{ $_ }.count` 0, `{ $^a + $^b }.arity` 0), and `&say.count`, the `comb` proto and `sub (|)` answer 0 instead of `Inf`.

### CO-02  signature, returns, of, `-->` and the `Callable[T]` mixin    D:partial R:partial V:spec
`.signature` is the Signature object; `.returns` and `.of` on a
Block or Routine are its return constraint, `Mu` when none was given;
`returns Int` and `of Int` after the parameter list mean exactly
`--> Int` and print as `--> Int` in `.raku`. `Block.of` and `Code.of`
on the type objects are `Mu`; `Routine.returns` on the type object is
`X::Parameter::InvalidConcreteness`. `.signature.params` is a `List` of
`Parameter`. A constant return (`--> 42`) makes `.returns` that value,
prints as `:( --> 42)`, and the call returns the constant whatever the
body does; `--> Nil` makes every call return `Nil`. `-> () --> Int {}`
has one anonymous parameter with an empty sub-signature, spelled
`:($ () --> Int)`. `&say.signature` is `:(|)`, `Str.^lookup("comb")`'s is
the proto's `:(Cool $:: |)` (the `$::` spelling is CO-19's bug).
Giving both `-->` and `returns` is a compile-time `X::Redeclaration`;
`returns Int of Str` is `X::NotParametric` (composed with `X::Comp`). A
routine with a return constraint `does Callable[T]` and its type prints
as `Sub+{Callable[Str]}`; one without matches `Callable[Str]` False.
`.signature.has_returns` tells whether a constraint was declared.
```
say do { sub a(Int $x, Str $y --> Str) { $y }; sub b($x) { $x }; sub c($x) returns Int { $x }; sub d($x) of Int { $x }; (&a.signature.raku, &a.signature.gist, &a.returns.^name, &a.of.^name, &b.returns.^name, &b.of.^name, &c.returns.^name, &c.signature.raku, &d.of.^name, &d.signature.raku, { $_ }.of.^name, { $_ }.returns.^name, {;}.signature.raku, { $_ }.signature.raku, -> $a { }.signature.gist, Block.of.^name, (try Code.of.^name) // $!.^name, (try Routine.returns.^name) // $!.^name, &a.signature.params.elems, &a.signature.params.^name, &a.signature.params[0].^name, &a.signature.^name, Sub.of.^name, (-> --> Int {}).of.^name, (-> () --> Int {}).signature.raku, sub (--> 42) {}.returns.raku, sub (--> 42) {}.signature.raku, sub (--> 42) { my $x = 1 }(), sub (--> Nil) { my $x = 1 }().raku, &say.signature.raku, &say.gist, Str.^lookup("comb").signature.raku, (try EVAL 'sub z(--> Int) returns Str { }') // $!.^name, (try EVAL 'sub z2() returns Int of Str { }') // $!.^name, (&a ~~ Callable[Str]), (&b ~~ Callable[Str]), &a.^name, &a.WHAT.gist, (&a ~~ Callable), &a.signature.returns.^name, (try &a.signature.has_returns) // $!.^name, (try &b.signature.has_returns) // $!.^name).join(" ") }
# rakudo 2026.08: :(Int $x, Str $y --> Str) (Int $x, Str $y --> Str) Str Str Mu Mu Int :($x --> Int) Int :($x --> Int) Mu Mu :(;; $_? is raw = OUTER::<$_>) :(;; $_? is raw = OUTER::<$_>) ($a) Mu Mu X::Parameter::InvalidConcreteness 2 List Parameter Signature Mu Int :($ () --> Int) Int :( --> 42) 42 Nil :(|) &say :(Cool $:: |) X::Redeclaration X::NotParametric+{X::Comp} True False Sub+{Callable[Str]} (Sub+{Callable[Str]}) True Str True False
```
rakupp 4.0.1-84: differs — `Routine.returns` on the type object is `X::Method::NotFound`; `-> () --> Int` prints `:($ --> Int)`; `--> 42` is lost (`.returns` `Mu`, `.raku` `:()`, the call returns the body's value) and `--> Nil` returns the body's value too; `&say.signature.raku` and the proto's are `:()`; the two redeclarations compile silently; there is no `Callable[T]` mixin (`&a.^name` is `Sub`, `~~ Callable[Str]` False); `has_returns` is missing. The two "Sub object coerced to string" warnings are the two EVAL fields returning their routine instead of failing.

### CO-03  The return type check: X::TypeCheck::Return                D:partial R:yes V:spec
A value that fails `-->`/`returns` throws `X::TypeCheck::Return` (an
`X::TypeCheck`, `.operation` "returning", `.got` the value, `.expected`
the constraint) — whether it falls off the end, comes from `return`, from
a block or pointy called inside the routine, or from a `return` inside
such a block; a List (`"a", "b"`, `return 1, 2`, `(5,)`) fails an `Int`
constraint too. Exempt: `Nil` (falling off the end, `return`, or
`return Nil` all give `Nil`), a `Failure` (returned as-is), and a type
object of the right type (`Int` passes `--> Int`, but not `--> Int:D`,
and `Any`/`Str` fail `--> Int`). A mixin keeps the base type (`42 but
"x"` passes `--> Int`). `return 5` inside `sub (--> Nil)` is refused at
compile time (`X::Comp::AdHoc`); a bare `return` there gives `Nil`.
```
say do { sub a(--> Int) { "x" }; sub b(--> Int) { 42 }; sub c(--> Int:D) { Int }; sub d(--> Int) { return "s" }; sub e(--> Nil) { my $x = 42 }; sub f(--> Int) { Nil }; sub g() returns Int { "x" }; sub h(--> Int) { fail "oops" }; sub j(--> Str) { my $x = 5; $x }; sub k(--> Int) { 42.Str }; sub l(--> Int) { "a", "b" }; sub m(--> Int) { return 1, 2 }; sub n(--> Int) { 42 but "x" }; sub o(--> Int) { my Int $i = 5; $i }; sub p(--> Int) { (5,) }; ((try a()) // $!.^name, b(), (try c()) // $!.^name, (try d()) // $!.^name, e().raku, f().raku, (try g()) // $!.^name, h().^name, (try j()) // $!.^name, (try k()) // $!.^name, (try -> --> Int { "x" }()) // $!.^name, (try l()) // $!.^name, (try m()) // $!.^name, n(), o(), (try p()) // $!.^name, (try sub (--> Int) { 5 }()) // $!.^name, (try sub (--> Int) { Any }()) // $!.^name, (try sub (--> Int:D) { Any }()) // $!.^name, (try sub (--> Int) { Str }()) // $!.^name, (try sub (--> Int) { Failure.new("f") }.^name) // $!.^name, (try sub (--> Int) { -> { "s" }() }()) // $!.^name, (try sub (--> Int) { { return "s" }(); 1 }()) // $!.^name, (try EVAL 'sub (--> Nil) { return 5 }') // $!.^name, sub (--> Nil) { return }().raku, (try sub (--> Int) { return }().raku) // $!.^name, (try sub (--> Int) { return Nil }().raku) // $!.^name, sub (--> Int) { return 7 }(), (try sub (--> Int) { 42 but "x" }.^name) // $!.^name, (try -> Int $x --> Int { $x }.(5)) // $!.^name, (try (-> --> Int { "s" }).()) // $!.^name).join(" ") }
# rakudo 2026.08: X::TypeCheck::Return 42 X::TypeCheck::Return X::TypeCheck::Return Nil Nil X::TypeCheck::Return Failure X::TypeCheck::Return X::TypeCheck::Return X::TypeCheck::Return X::TypeCheck::Return X::TypeCheck::Return x 5 X::TypeCheck::Return 5 X::TypeCheck::Return X::TypeCheck::Return X::TypeCheck::Return Sub+{Callable[Int]} X::TypeCheck::Return X::TypeCheck::Return X::Comp::AdHoc Nil Nil Nil 7 Sub+{Callable[Int]} 5 X::TypeCheck::Return
say do { sub a(--> Int) { "x" }; sub b(--> Int:D) { Int }; try a(); my $e = $!; try b(); my $e2 = $!; ($e.^name, $e.got.raku, $e.expected.^name, ($e ~~ X::TypeCheck), $e2.^name, $e2.got.raku, $e2.expected.^name, $e.message, (try $e.operation) // "no-operation").join(" ") }
# rakudo 2026.08: X::TypeCheck::Return "x" Int True X::TypeCheck::Return Int Int:D Type check failed for return value; expected Int but got Str ("x") returning
```
rakupp 4.0.1-84: differs — `Int` passes `--> Int:D`, and `Any`, `Str` and a type object pass `--> Int` (all answer `Nil` instead of throwing), `return 5` under `--> Nil` compiles, there is no `Callable[Int]` mixin, and the second probe died: after a statement-form `try a();` `$!` is not set, so `$e.got` is a call on `Any`.

### CO-04  name, gist, raku, Str, package, file, line, identity          D:partial R:partial V:spec
`.name` is the declared name and `""` for an anonymous sub, block or
pointy; `anon sub nm` keeps its name without installing it. `.gist` of
a named routine is `&name`, of an anonymous sub `sub { }`; `Method.gist`
and `Submethod.gist` are the bare name. `.raku` is the declarator, the
name, the signature (omitted when empty) and a `{ #`(WHICH) ... }`
body; a block's `.raku` is `-> <params> { #`(WHICH) ... }` and shows the
implicit `;; $_? is raw = OUTER::<$_>`. `.Str` is the name, with a
"coerced to string" `warn` (a CX::Warn, resumable). `.package` of a
mainline sub is `GLOBAL`, of a method its class; a Block has no
`.package` (`X::Method::NotFound`). `.file` is the compile unit's name
(`-e` here) or `SETTING::src/core.c/…` for the setting, `.line` an Int.
`.WHICH` is an `ObjAt`: routines compare by identity, and a nested sub
is a fresh closure on every call of its outer (`o() === o()` is False)
while `.static_id` (an Int) is the same for all clones. `.outer` is the
enclosing code object. A plain sub answers `.multi` `0`, `.is_dispatcher`
False, `.yada` False, `.onlystar` False.
```
say do { sub foo($x) { }; my $anon = sub { }; my $b = { 1 }; my $p = -> $x { }; my method m() {}; class Foo { method bar {}; submethod baz {} }; my @w; CONTROL { when CX::Warn { @w.push(.message); .resume } }; (&foo.name.raku, $anon.name.raku, $b.name.raku, $p.name.raku, (anon sub nm() { }).name, &foo.gist, $anon.gist, &foo.raku.subst(/'#`(' <-[)]>* ')'/, '#`(…)'), $anon.raku.subst(/'#`(' <-[)]>* ')'/, '#`(…)'), $b.raku.subst(/'#`(' <-[)]>* ')'/, '#`(…)'), $p.raku.subst(/'#`(' <-[)]>* ')'/, '#`(…)'), &foo.Str, $anon.Str.raku, &foo.package.^name, Foo.^lookup('bar').package.^name, Foo.^lookup('bar').gist, Foo.^lookup('baz').gist, Foo.^lookup('bar').name, Foo.^lookup('bar').raku.subst(/'#`(' <-[)]>* ')'/, '#`(…)'), Foo.^lookup('baz').^name, &m.^name, &m.gist.raku, &m.name, &foo.file, &foo.line, $b.line, &say.file, &say.line.^name, @w.elems, @w[0], &foo.^name, $b.^name, $p.^name, &foo.WHICH.^name, (&foo ~~ Callable), (&foo ~~ Code), ($b ~~ Routine), $b.defined, Sub.defined, do { sub o { my sub i { }; &i }; (o() === o()) ~ (&foo === &foo) }, (try $b.package.^name) // $!.^name, (try $b.multi) // $!.^name, &foo.multi, &foo.is_dispatcher, &foo.yada, &foo.onlystar, Foo.^lookup('bar').file, Foo.^lookup('bar').line, Foo.^lookup('baz').package.^name, (try &foo.static_id.^name) // $!.^name, (try &foo.outer.^name) // $!.^name, (try $b.outer.^name) // $!.^name, do { sub o2 { my sub i2 { }; &i2 }; (try o2().static_id == o2().static_id) // $!.^name }).join(" ") }
# rakudo 2026.08: "foo" "" "" "" nm &foo sub { } sub foo ($x) { #`(…) ... } sub { #`(…) ... } -> ;; $_? is raw = OUTER::<$_> { #`(…) ... } -> $x { #`(…) ... } foo "" GLOBAL Foo bar baz bar method bar (Foo $:: *%_) { #`(…) ... } Submethod Method "m" m -e 1 1 SETTING::src/core.c/io_operators.rakumod Int 2 Sub object coerced to string (please use .gist or .raku to do that) Sub Block Block ObjAt True True False True False FalseTrue X::Method::NotFound X::Method::NotFound 0 False False False -e 1 Foo Int Block Block True
```
rakupp 4.0.1-84: differs — the line died at `&foo.onlystar` (`X::Method::NotFound`); the earlier fields are unmeasured by this line, but CO-10 and CO-14 show `.multi` answering `False` and `.file` answering `SETTING::src/core.c/` for user code.

### CO-05  Code.ACCEPTS, smartmatch against code, and cando               D:partial R:partial V:quirk
`Code.new`, `Block.new`, `Sub.new`, `Method.new` and `WhateverCode.new`
throw `X::Cannot::New` (`.class` the type). `$topic ~~ $code` calls the
code with the topic when its `count` is non-zero and with no arguments
when it is zero, and the smartmatch is the call's result **boolified**
(`5 ~~ { 0 }` is False, `~~ { "s" }` True, `~~ { Nil }` False), while
`.ACCEPTS` returns the raw result (`{ 0 }.ACCEPTS(5)` is `0`). Too few
arguments for the block is `X::AdHoc` at the call; an exception inside
the block propagates. `Code.cando(Capture)` answers `(self,)` when the
Capture binds and `()` otherwise; on a Block it takes only a Capture
(positional arguments are `X::AdHoc`). Quirk: a **Routine**'s `cando`
ignores an unexpected named argument (`sub ($x) { }.cando(\(1, :n))` is
1) although the same Capture does not bind to `:($a)` (CO-23) and the
call itself dies. `5 ~~ Code` and `~~ Callable` are False; a block is a
Block and a Callable but not a Sub; `4 ~~ * > 3` and `5 ~~ *` are not
smartmatches at all but WhateverCodes (CO-35), so a `*`-expression must
be parenthesized: `4 ~~ (* > 3)`.
```
say do { my $c1 = { $^a }; my $ti = -> Int $x { }; my $s0 = sub () { }; my $s1 = sub ($x) { }; my $sn = sub ($x, :$n) { }; (do { try Code.new; my $e = $!; $e.^name ~ ":" ~ ((try $e.class.^name) // "NA") }, (try Block.new) // $!.^name, (try Sub.new) // $!.^name, (try Method.new) // $!.^name, (try WhateverCode.new) // $!.^name, (5 ~~ { $_ > 3 }), (2 ~~ { $_ > 3 }), (5 ~~ sub () { "x" }), (5 ~~ sub () { 0 }), (5 ~~ -> $a { $a == 5 }), (try (5 ~~ -> $a, $b { True })) // $!.^name, (5 ~~ *.is-prime), (4 ~~ (* > 3)), ("abc" ~~ *.chars), (0 ~~ { $_ }), $c1.cando(\(1)).elems, $c1.cando(\(1, 2)).elems, $c1.cando(\(1))[0].^name, $ti.cando(\("s")).elems, $ti.cando(\(1)).elems, $c1.cando(\(1)).^name, (try $c1.cando(1, 2)) // $!.^name, $s1.cando(\(1, :n)).elems, $sn.cando(\(1, :n)).elems, (5 ~~ { 0 }), ((1, 2) ~~ { .elems }), (Nil ~~ { $_ }).raku, { $_ > 3 }.ACCEPTS(5), { 0 }.ACCEPTS(5), { 0 }.ACCEPTS(5).^name, (5 ~~ { 0 }).^name, (5 ~~ { "s" }), ($c1.cando(\(1))[0] === $c1), $s0.cando(\()).elems, $s0.cando(\(1)).elems, (try (5 ~~ sub ($a, $b) { })) // $!.^name, (5 ~~ Code), (5 ~~ Callable), ({;} ~~ Callable), ({;} ~~ Block), (sub { } ~~ Block), (sub { } ~~ Sub), ({;} ~~ Sub), (try (5 ~~ { die "in-block" })) // $!.^name, ((1, 2, 3) ~~ { .elems == 3 }), (5 ~~ sub ($x) { $x + 1 }), (5 ~~ sub ($x) { $x + 1 }).^name, (5 ~~ { 0 }).raku, (5 ~~ { Nil }).raku, (5 ~~ { "" }).raku, (4 ~~ * > 3).WHAT.gist, (5 ~~ *).WHAT.gist).join(" ") }
# rakudo 2026.08: X::Cannot::New:Code X::Cannot::New X::Cannot::New X::Cannot::New X::Cannot::New True False True False True X::AdHoc True True True False 1 0 Block 0 1 List X::AdHoc 1 1 False True Bool::False True 0 Int Bool True True 1 0 X::AdHoc False False True True True True False X::AdHoc True True Bool Bool::False Bool::False Bool::False (WhateverCode) (WhateverCode)
```
rakupp 4.0.1-84: differs — there is no `X::Cannot::New` (`X::Method::NotFound`, without `.class`); a pointy with two parameters smartmatches 5 as True instead of `X::AdHoc`; `cando(1, 2)` on a Block returns the block itself; `sub ($x) { }.cando(\(1, :n))` is 0; `sub { } ~~ Block` is False (CO-06); `5 ~~ sub ($a, $b) { }` is False instead of dying.

### CO-06  The type hierarchy and the `&`-sigil container                D:partial R:partial V:spec
`Code is Any does Callable`; `Block is Code`; `Routine is Block`; `Sub`,
`Method`, `Submethod`, `Macro` are Routines; `Regex is Method`;
`WhateverCode is Code`; `ForeignCode is Any does Callable` (not a
Code); `Signature`, `Parameter`, `Attribute` are plain `Any`s; `Callable`
is a parametric role and `Callable[Int]` a Callable. A routine matches
`Callable[T]` only when it has a return constraint of a matching type
(`--> Int:D` matches `Callable[Int]`; no constraint matches nothing).
An `&`-sigiled variable is typed `Callable` (`my &c; &c.raku` is
`Callable`), assigning a non-Callable is `X::TypeCheck::Assignment`, and
`my Int &c` refuses a routine whose return type is not Int and accepts
`sub (--> Int)`. The signature-constraint form `my &c:(Int)` is a
compile-time `X::Syntax::Adverb`. A block's own methods (`{;}.^methods`)
are `Method`s and `Submethod`s (some with `is-nodal`/`is-pure`
mixins).
```
say (Code.^mro.map(*.^name).raku, Block.^mro.map(*.^name).raku, Routine.^mro.map(*.^name).raku, Sub.^mro.map(*.^name).raku, Method.^mro.map(*.^name).raku, Submethod.^mro.map(*.^name).raku, Macro.^mro.map(*.^name).raku, Regex.^mro.map(*.^name).raku, WhateverCode.^mro.map(*.^name).raku, ForeignCode.^mro.map(*.^name).raku, Signature.^mro.map(*.^name).raku, Parameter.^mro.map(*.^name).raku, Attribute.^mro.map(*.^name).raku, Callable.^name, (Code ~~ Callable), (Signature ~~ Callable), (Regex ~~ Method), (Regex ~~ Routine), (Macro ~~ Routine), (ForeignCode ~~ Code), (ForeignCode ~~ Callable), (Callable[Int] ~~ Callable), Callable[Int].^name, (sub (--> Int) { } ~~ Callable[Int]), (sub (--> Int) { } ~~ Callable[Str]), (sub { } ~~ Callable[Int]), (sub (--> Int:D) { } ~~ Callable[Int]), (&say ~~ Callable[Int]), do { my &c = sub { }; &c.^name }, (try EVAL 'my &c = 5; &c') // $!.^name, do { my &c; &c.^name ~ &c.raku }, do { my Int &c; (try &c = sub (--> Str) { }) // $!.^name }, do { my Int &c; &c = sub (--> Int) { }; &c.^name }, (try EVAL 'my &c:(Int); 1') // $!.^name, Code.DEFINITE, Sub.^parents.map(*.^name).raku, &say.^name, &say.^mro.map(*.^name).raku, (try Code.^roles.map(*.^name).raku) // $!.^name, (try Callable.HOW.^name) // $!.^name, Code.HOW.^name, (try {;}.^methods.map(*.^name).unique.sort.raku) // $!.^name).join(" ")
# rakudo 2026.08: ("Code", "Any", "Mu").Seq ("Block", "Code", "Any", "Mu").Seq ("Routine", "Block", "Code", "Any", "Mu").Seq ("Sub", "Routine", "Block", "Code", "Any", "Mu").Seq ("Method", "Routine", "Block", "Code", "Any", "Mu").Seq ("Submethod", "Routine", "Block", "Code", "Any", "Mu").Seq ("Macro", "Routine", "Block", "Code", "Any", "Mu").Seq ("Regex", "Method", "Routine", "Block", "Code", "Any", "Mu").Seq ("WhateverCode", "Code", "Any", "Mu").Seq ("ForeignCode", "Any", "Mu").Seq ("Signature", "Any", "Mu").Seq ("Parameter", "Any", "Mu").Seq ("Attribute", "Any", "Mu").Seq Callable True False True True True False True True Callable[Int] True False False True False Sub X::TypeCheck::Assignment CallableCallable X::TypeCheck::Assignment Sub+{Callable[Int]} X::Syntax::Adverb False ("Routine", "Block", "Code").Seq Sub ("Sub", "Routine", "Block", "Code", "Any", "Mu").Seq ("Callable",).Seq Perl6::Metamodel::ParametricRoleGroupHOW Perl6::Metamodel::ClassHOW ("Method", "Method+\{is-nodal}", "Method+\{is-pure}", "Submethod").Seq
```
rakupp 4.0.1-84: differs — every `.^mro` is `(Any, Mu)`, `Code ~~ Callable`, `Regex ~~ Method`, `Macro ~~ Routine` and `ForeignCode ~~ Callable` are False, `Callable[Int]` matches no routine, `my &c = 5` is accepted, `my &c` prints `AnyAny`, `my Int &c` accepts a `--> Str` sub, `my &c:(Int)` is `X::Syntax::Confused`, `Sub.^parents` is empty, `.^roles` is missing, `Callable.HOW` is a `ClassHOW`, and `{;}.^methods` is empty.

### CO-07  Blocks: the implicit `$_`, placeholders, `@_`/`%_`, &?BLOCK and &?ROUTINE   D:yes R:yes V:quirk
A block with no signature has `:(;; $_? is raw = OUTER::<$_>)`: one
optional raw positional after the `;;` marker, defaulting to the outer
`$_` (so `{ $_ }()` sees the caller's topic and `{ $_ }("in")` the
argument); two arguments are `X::AdHoc`. `$^a $^b` placeholders make
required positionals in **alphabetical** order of the name (`{ $^b ~ $^a
}` is `:($a, $b)`), `$:x` placeholders make **required** nameds in order
of appearance (`:(:$y!, :$x!)`), `@_` adds `*@_` and `%_` adds `*%_`
(also in a sub), an explicit placeholder removes the implicit `$_`
(`{ $_ ~ $^a }` is `:($a)`), and a repeated placeholder is one
parameter. A placeholder in a block or routine that has a signature is
a compile-time `X::Signature::Placeholder`; in a method it is refused
(`X::Comp::AdHoc`); `sub f { $^a }` works. `&?BLOCK` is the innermost
block, `&?ROUTINE` the innermost routine (an anonymous sub's name is
`""`); `&?ROUTINE` outside any routine is a compile-time
`X::Undeclared::Symbols`. Quirk: the `$_` parameter's `.default` is the
`Code` type object (invoking it is `X::AdHoc`) although `.raku` prints
`= OUTER::<$_>`; its `.raw` is True, `.optional` True, `.multi-invocant`
False.
```
say do { my $b = { $_ * 2 }; my $p = { $^b ~ $^a }; my $n = { $:y ~ $:x }; my $m = { $^a ~ $:z }; my $c1 = { $^a }; ({;}.signature.raku, $b.signature.raku, $p.signature.raku, $n.signature.raku, $m.signature.raku, $b(21), $p("x", "y"), $n(:x<a>, :y<b>), $m("q", :z<r>), { $_ }.arity, { $_ }.count, { $^a; $^c; $^b }.signature.raku, (try $c1()) // $!.^name, (try $p("only")) // $!.^name, do { $_ = "outer"; {;}() // "Nil" }, do { $_ = "outer"; { $_ }() }, do { $_ = "outer"; { $_ }("in") }, (try ({ &?BLOCK.^name })()) // $!.^name, (try ({ &?BLOCK.signature.raku })()) // $!.^name, (try (sub { &?ROUTINE.name.raku })()) // $!.^name, do { sub foo { { &?ROUTINE.name }() }; (try foo()) // $!.^name }, do { sub bar { { &?BLOCK.^name }() }; (try bar()) // $!.^name }, (try EVAL '{ &?ROUTINE.^name }()') // $!.^name, $c1.WHAT.gist, (try EVAL 'sub ($x) { $^y }') // $!.^name, (try EVAL '-> $x { $^y }') // $!.^name, { $_ }.signature.params[0].name, { $_ }.signature.params[0].default.^name, { $_ }.signature.params[0].raw, {;}.signature.params[0].optional, {;}.signature.params[0].multi-invocant, sub { @_ }.signature.raku, sub { %_ }.signature.raku, { @_; %_ }.signature.raku, { $^b; @_ }.signature.raku, sub { @_.elems }(1, 2, 3), { %_.keys }(:a).raku, sub { $^a }.signature.raku, (try EVAL '{ $^a; $:a }.signature.raku') // $!.^name, (try ({ $_ }).signature.params[0].default.().raku) // $!.^name, $c1.signature.params[0].raku, { $:n }.signature.params[0].raku, (try EVAL 'sub f { $^a }; f(1)') // $!.^name, $c1.signature.params[0].usage-name, $c1.name.raku, sub { $^a }.name.raku, {;}.signature.params[0].raku, (-> { }).signature.raku, (-> { }).arity, (-> { }).count, ({ $_ }.count.^name), (try EVAL '{ $_ }("a", "b")') // $!.^name, (try EVAL 'class Z { method m { $^a } }; Z.m(1)') // $!.^name, { $^a; $^a }.arity, { $^b; $^b; $^a }.signature.raku, { @_[0] }(7), { $^a + @_[0] }(1, 2), sub { $:x }.signature.raku, -> $x { }.signature.params[0].raku, -> \x { }.signature.params[0].raku, { $_ ~ $^a }.signature.raku, (try EVAL '-> { $^a }') // $!.^name, { $^a }.signature.params[0].positional, { $:a }.signature.params[0].named).join(" ") }
# rakudo 2026.08: :(;; $_? is raw = OUTER::<$_>) :(;; $_? is raw = OUTER::<$_>) :($a, $b) :(:$y!, :$x!) :($a, :$z!) 42 yx ba qr 0 1 :($a, $b, $c) X::AdHoc X::AdHoc Nil outer in Block :(;; $_? is raw = OUTER::<$_>) "" foo Block X::Undeclared::Symbols (Block) X::Signature::Placeholder X::Signature::Placeholder $_ Code True True False :(*@_) :(*%_) :(*@_, *%_) :($b, *@_) 3 ("a",).Seq :($a) :($a) X::AdHoc Mu $a Mu :$n! 1 a "" "" Mu $_? is raw = OUTER::<$_> :() 0 0 Int X::AdHoc X::Comp::AdHoc 1 :($a, $b) 7 3 :(:$x!) Mu $x Mu \x :($a) X::Signature::Placeholder True True
```
rakupp 4.0.1-84: differs — the line's stdout was pushed past the harness's six-line window by rakupp's own warnings ("Sub object coerced to string", "Block object coerced to string", "Use of Nil in string context"), which Rakudo does not emit here; the values are unmeasured by this line.

### CO-08  Phaser introspection, and what the phasers do                D:no R:no V:spec
`.has-phasers` is True when the block declares any phaser (a `CATCH`
or `CONTROL` is not a phaser); `.has-loop-phasers` when it has `FIRST`,
`NEXT` or `LAST`; `.has-phaser($name)` per kind, and a `KEEP` or `UNDO`
counts as a `LEAVE` for `has-phaser('LEAVE')` while `.phasers('LEAVE')`
does not list them. `.phasers($name)` is a `List` of the phaser
closures (`Code` objects, callable by hand), `()` for an unknown kind;
`ENTER` phasers are listed and run in declaration order, `LEAVE`
phasers in reverse declaration order (the order they run). A `FIRST` in
a non-loop block never runs. `KEEP` runs when the block's value is
defined, `UNDO` when it is a `Failure` or `Nil`. A false `PRE` or
`POST` is `X::Phaser::PrePost`. `.raku` of a block, pointy, anonymous
sub or method prints as in CO-04 (an anonymous method is `method <anon>
(Mu $:: *%_) …`, its gist `<anon>`); `sub { ... }` prints as `sub { ... }`,
`.yada` is True for `...`, `!!!` and `???` bodies and calling a `...` or
`!!!` body is `X::StubCode`. A `proto` prints `proto sub pr (|) {*}`
with `.onlystar` True. `fail` inside a block that has no enclosing
routine throws (`X::AdHoc`) instead of returning a Failure.
```
say do { my $x = 0; my $b = { LEAVE $x++; 42 }; my $c = { 42 }; my $d = { ENTER $x++; LEAVE $x++; LEAVE $x++; 42 }; my $e = { FIRST $x++; 42 }; my $f = { KEEP $x++; UNDO $x++; 3 }; proto pr(|) {*}; multi pr() { }; ($b.has-phasers, $c.has-phasers, $b.has-loop-phasers, $e.has-loop-phasers, $b.has-phaser('LEAVE'), $b.has-phaser('ENTER'), $b.phasers('LEAVE').elems, $b.phasers('ENTER').elems, $d.phasers('LEAVE').elems, $d.phasers('ENTER').elems, $d.phasers('LEAVE').^name, $d.phasers('LEAVE')[0].^name, $c.phasers('LEAVE').raku, $d(), $x, $c.raku.subst(/'#`(' <-[)]>* ')'/, '#`(…)'), (-> $y, :$n { }).raku.subst(/'#`(' <-[)]>* ')'/, '#`(…)'), (-> $y, :$n { }).gist.subst(/'#`(' <-[)]>* ')'/, '#`(…)'), $c.gist.subst(/'#`(' <-[)]>* ')'/, '#`(…)'), sub { }.gist, sub { }.raku.subst(/'#`(' <-[)]>* ')'/, '#`(…)'), sub ($y) { }.raku.subst(/'#`(' <-[)]>* ')'/, '#`(…)'), (method () { }).raku.subst(/'#`(' <-[)]>* ')'/, '#`(…)'), (method () { }).gist.raku, (method ($y) { }).signature.raku, (sub { ... }).raku.subst(/'#`(' <-[)]>* ')'/, '#`(…)'), (sub { ... }).yada, (sub { 1 }).yada, &pr.raku, &pr.onlystar, &pr.yada, &pr.gist, (-> { }).WHY.raku, (-> { }).has-phasers.^name, $f.phasers('LEAVE').elems, $f.phasers('KEEP').elems, $f.has-phaser('LEAVE'), { NEXT $x++; LAST $x++ }.has-loop-phasers, { PRE 1; POST 1; 5 }.phasers('POST').elems, $b.has-phaser('NEXT'), $b.phasers('nope').raku, (my $w = * + 1).has-phasers, $d.phasers('LEAVE').map({ .() }).raku, $x, { $x++; CATCH { default { } } }.has-phasers, { $x++; CONTROL { default { } } }.phasers('LEAVE').elems, (sub { LEAVE $x++ }).has-phasers, (sub { LEAVE $x++ }).phasers('LEAVE').elems, (try (sub { ... }).()) // $!.^name, sub { !!! }.yada, sub { ??? }.yada, (try sub { !!! }.()) // $!.^name).join(" ") }
# rakudo 2026.08: True False False True True False 1 0 2 1 List Code () 42 5 -> ;; $_? is raw = OUTER::<$_> { #`(…) ... } -> $y, :$n { #`(…) ... } -> $y, :$n { #`(…) ... } -> ;; $_? is raw = OUTER::<$_> { #`(…) ... } sub { } sub { #`(…) ... } sub ($y) { #`(…) ... } method <anon> (Mu $:: *%_) { #`(…) ... } "<anon>" :(Mu $:: $y, *%_) sub { ... } True False proto sub pr (|) {*} True False &pr Nil Bool 0 1 True True 1 False () False (3, 4).Seq 5 False 0 True 1 X::StubCode True True X::StubCode
say do { my @o; my $b = { LEAVE @o.push("leave"); ENTER @o.push("enter"); FIRST @o.push("first"); "body" }; @o.push($b()); @o.push($b.phasers('ENTER').elems, $b.phasers('LEAVE').elems, $b.phasers('FIRST').elems, $b.has-loop-phasers); my $x = 0; my $c = { ENTER $x++; ENTER $x += 10; $x }; @o.push($c()); @o.push($c.phasers('ENTER').map({ .() }).raku); @o.push($x); my $d = { LEAVE $x = 100; LEAVE $x = 200; $x }; $d(); @o.push($x); @o.push($d.phasers('LEAVE').map({ .() }).raku); @o.push($x); my $e = -> { KEEP @o.push("keep"); UNDO @o.push("undo"); 1 }; $e(); my $f = -> { KEEP @o.push("keep2"); UNDO @o.push("undo2"); Failure.new("f") }; $f().defined; my $g = -> { KEEP @o.push("keep3"); UNDO @o.push("undo3"); Nil }; $g(); @o.push($e.phasers('KEEP').elems, $e.phasers('UNDO').elems, $e.phasers('LEAVE').elems, $e.has-phaser('KEEP')); @o.push((try (sub { PRE @o.push("pre"); 1 }).()) // $!.^name); @o.push((try (sub { PRE 0; 1 }).()) // $!.^name); @o.push((try (sub { POST 0; 1 }).()) // $!.^name); @o.push((sub { PRE 1; POST 1; 5 }).phasers('PRE').elems, (sub { PRE 1; POST 1; 5 }).phasers('POST').elems, (sub { PRE 1; POST 1; 5 }).has-phasers); my $h = -> { fail "h" }; @o.push((try $h().defined) // $!.^name); @o.push((sub { fail "s" })().^name); @o.join(" ") }
# rakudo 2026.08: enter leave body 1 1 1 True 11 (11, 22).Seq 22 100 (100, 100).Seq 100 keep undo2 undo3 1 1 0 True pre 1 X::Phaser::PrePost X::Phaser::PrePost 1 1 True X::AdHoc Failure
```
rakupp 4.0.1-84: differs — `has-phasers`, `has-loop-phasers`, `has-phaser` and `phasers` are `X::Method::NotFound`, so both lines died at their first field.

### CO-09  What `return` returns from                                    D:yes R:yes V:quirk
`return` leaves the innermost **Routine**; a block, pointy, `map`/`grep`/
`first` callback, `for` body, `do`, `given`, `if`, `loop`, `while`,
`andthen` thunk, `gather` block, `NEXT`/`LEAVE` phaser or an `EVAL`'d
string inside it are all transparent (`(1..3).first({ return "first" })`
returns "first" from the sub; a nested sub returns only from itself).
`5 ==> return` returns 5. A `return` in a block that has no routine on
its dynamic call chain is `X::ControlFlow::Return`, with
`.out-of-dynamic-scope` False at top level and True when the block
escaped its routine and is called later. `return` with no value gives
`Nil`, `return 1, 2` a List. A sub's value is decontainerized (`sub { my
$x = 5 }().VAR` is `Int`) but — quirk — `return my $x = 5` hands back
the `Scalar`, as does `return-rw` and any `is rw` sub; a Block returns
its containers as they are. `.leave` is `X::NYI`.
```
say do { sub f() { (1, 2, 3).map({ return 42 if $_ == 2; $_ }).eager; "end" }; sub g() { for 1..3 { return $_ * 10 if $_ == 2 }; "end" }; sub h() { my $b = { return 7 }; $b(); "after" }; sub i() { -> { return 8 }(); "after" }; sub j() { return }; sub k() { return 1, 2 }; sub l() { my $b = { return 9 }; $b }; (f(), g(), h(), i(), j().raku, k().raku, k().^name, (try EVAL '{ return 1 }(); "no"') // $!.^name, (try EVAL 'return 5') // $!.^name, (try &f.leave) // $!.^name, sub () { 42 }(), sub () { my $a = 1; 2 }(), sub () { }().raku, sub () { my $x = 5 }(), (sub () { my $x = 5 }()).VAR.^name, sub () { return my $x = 5 }().VAR.^name, sub () is rw { return my $x = 5 }().VAR.^name, sub () is rw { my $x = 5 }().VAR.^name, (try l()()) // $!.^name, (try EVAL 'my $b = { return 3 }; sub m { $b() }; m()') // $!.^name, do { sub n { my $b = { return 3 }; $b }; (try n()()) // $!.^name }, sub () { return-rw my $x = 1 }().VAR.^name, (try EVAL 'sub o { { return 1 }() }; o()') // $!.^name, sub () { (1..3).first({ return "first" }); "no" }(), sub () { (1..3).grep({ return "grep" }).eager; "no" }(), sub () { [1].map({ return "map" }).Str; "no" }(), sub () { for 1 { NEXT return "next" } }(), sub () { for 1 { LEAVE return "leave" } }(), sub () { 5 ==> return }(), (try EVAL 'sub (--> Nil) { return 5 }') // $!.^name, (try EVAL '{ return 1 }(); "no"') // $!.out-of-dynamic-scope, (try l()()) // $!.out-of-dynamic-scope, sub () { my $r = (1..5).map({ return "mapped" if $_ == 3; $_ }).eager; $r }(), sub () { my @a = gather { take 1; return "from-gather" }; @a[0] }(), sub () { sub inner { return "inner" }; inner(); "outer" }(), sub () { my &c = -> { return "pointy" }; c(); "after-pointy" }(), sub () { EVAL 'return "from-eval"'; "after-eval" }(), sub () { (try EVAL 'return "from-eval2"') // $!.^name }(), sub () { my $x = do { return "from-do" }; "after-do" }(), sub () { given 1 { return "from-given" }; "after-given" }(), sub () { if 1 { return "from-if" }; "after-if" }(), sub () { loop { return "from-loop" } }(), sub () { while 1 { return "from-while" } }(), sub () { 1 andthen return "andthen"; "no" }()).join(" ") }
# rakudo 2026.08: 42 20 7 8 Nil (1, 2) List X::ControlFlow::Return X::ControlFlow::Return X::NYI 42 2 Nil 5 Int Scalar Scalar Scalar X::ControlFlow::Return X::ControlFlow::Return X::ControlFlow::Return Scalar 1 first grep map next leave 5 X::Comp::AdHoc False True mapped from-gather outer pointy from-eval from-eval2 from-do from-given from-if from-loop from-while andthen
```
rakupp 4.0.1-84: differs — the line died at `5 ==> return` ("Too many positionals"); rerun without that field it answers `.leave` `X::Method::NotFound`, `return my $x = 5` and `return-rw` give `Nil` for `.VAR.^name`, a mainline block called from a sub returns from that sub instead of `X::ControlFlow::Return`, and `LEAVE return "leave"` returns `Nil`; the other fields match.

## B. Routine

### CO-10  multi, candidates, is_dispatcher, dispatcher, cando            D:partial R:partial V:bug
A proto (the thing `&f` names) has `.is_dispatcher` True, `.candidates`
a List of the candidates in declaration order, `.onlystar` True for
`{*}`, an auto-generated signature `:(;; Mu |)` (arity 0, count Inf),
`.raku` `proto sub f (;; Mu |) {*}` and gist `&f`; each candidate has
`.multi` True, `.dispatcher` the proto, `.raku` `multi sub f (Int $x) …`.
`.cando(Capture)` (or `.cando(1, 2)` with plain arguments) lists the
candidates that would bind, narrowest first: a `where`-constrained
candidate before the plain one of the same type, the `Any` one last;
`.cando(\())` is `()`. `.candidates(:!local)` is a **lazy** Seq (its
`.elems` is `X::Cannot::Lazy`) that also descends into wrapped
routines; `:with-proto` puts the proto first. Bug: `.multi` on a proto
or a plain sub answers `0`, not `False` as its docs state, and
`.dispatcher` of a non-multi is an `NQPMu` that answers `.^name`
"NQPMu", `.defined` 0 and `X::Method::NotFound` for `.raku`. There is
no `.is-dispatcher` (hyphen).
```
say do { multi f(Int $x) { "Int" }; multi f(Str $x) { "Str" }; multi f($x) { "Any" }; multi f(Int $x where * > 10) { "big" }; sub g($x) { }; (&f.multi, &f.is_dispatcher, &f.candidates.elems, &f.candidates.^name, &f.candidates.map(*.multi).raku, &f.candidates.map(*.is_dispatcher).raku, &f.candidates[0].dispatcher.^name, &f.candidates[0].dispatcher.is_dispatcher, &f.candidates[0].dispatcher.name, &g.multi, &g.is_dispatcher, &g.candidates.elems, &g.candidates[0].name, &f.cando(\(5)).elems, &f.cando(\(5)).map(*.signature.gist).raku, &f.cando(\(50)).map(*.signature.gist).raku, &f.cando(\("s")).map(*.signature.gist).raku, &f.cando(\(1.5)).map(*.signature.gist).raku, &f.cando(\()).elems, &f.cando(\(1, 2)).elems, &f.cando(5).elems, &f.cando(\(5)).^name, &g.cando(\(1)).elems, &g.cando(\()).elems, &g.cando(\(1))[0] === &g, &f.cando(\(5))[0].multi, &f.candidates(:local).elems, &f.gist, &f.raku, &f.candidates[0].raku.subst(/'#`(' <-[)]>* ')'/, '#`(…)'), &f.candidates[0].gist, &f.name, &f.candidates[0].name, &f.onlystar, &f.signature.raku, &f.arity, &f.count, &f.candidates[0].arity, &f.candidates.map(*.signature.gist).raku, f(50), f(5), f("s"), f(1.5), &f.candidates[3].signature.params[0].constraint_list.elems, &g.candidates.^name, &g.candidates[0].^name, &f.^name, &f.candidates[0].^name, (try &f.dispatcher.raku) // $!.^name, (try &f.dispatcher.defined) // $!.^name, (try &g.dispatcher.raku) // $!.^name, (try &g.dispatcher.defined) // $!.^name, (try &g.dispatcher.^name) // $!.^name, (try &f.candidates(:!local).elems) // $!.^name, (try &f.candidates(:!local).head(10).elems) // $!.^name, (try &f.candidates(:!local, :with-proto).head(10).elems) // $!.^name, (try &f.candidates(:!local, :with-proto).^name) // $!.^name, (try &f.candidates(:!local, :with-proto).is-lazy) // $!.^name, (try &f.candidates(:!local, :with-proto)[0].is_dispatcher) // $!.^name, (try &f.is-dispatcher) // $!.^name).join(" ") }
# rakudo 2026.08: 0 True 4 List (Bool::True, Bool::True, Bool::True, Bool::True).Seq (Bool::False, Bool::False, Bool::False, Bool::False).Seq Sub True f 0 False 1 g 2 ("(Int \$x)", "(\$x)").Seq ("(Int \$x where \{ ... })", "(Int \$x)", "(\$x)").Seq ("(Str \$x)", "(\$x)").Seq ("(\$x)",).Seq 0 0 2 List 1 0 True True 4 &f proto sub f (;; Mu |) {*} multi sub f (Int $x) { #`(…) ... } &f f f True :(;; Mu |) 0 Inf 1 ("(Int \$x)", "(Str \$x)", "(\$x)", "(Int \$x where \{ ... })").Seq big Int Str Any 1 List Sub Sub Sub X::Method::NotFound 0 X::Method::NotFound 0 NQPMu X::Cannot::Lazy 4 5 Seq True True X::Method::NotFound
```
rakupp 4.0.1-84: differs — a candidate's `.dispatcher` is `Mu`, so the line died at `.is_dispatcher` on it; CO-11 shows `.candidates[0].dispatcher.gist` `(Mu)` and `only q; &q.multi` `False`.

### CO-11  Dispatch failures and the narrowness rules                     D:partial R:partial V:quirk
No candidate binds: `X::Multi::NoMatch` with `.dispatcher` (the proto)
and `.capture` (the arguments, an unexpected named included); a tie:
`X::Multi::Ambiguous` with `.ambiguous` (the tied candidates) and
`.capture`. Two candidates with the same signature are Ambiguous at
the call, not a redeclaration; `sub`+`multi` of one name is
`X::Redeclaration`; a proto whose only candidates cannot match the
literal arguments is refused at compile time (`X::TypeCheck::Argument`).
Calling a candidate directly is a plain `X::TypeCheck::Binding::Parameter`.
Narrowness: `Int` beats `Numeric`, `Any` beats `Mu`, a `where` beats
the plain candidate of the same type regardless of declaration order,
`is default` breaks a tie (two defaults are Ambiguous), a plain
positional beats one with an optional or a slurpy, an optional named
neither narrows nor widens (the first declared wins), a required named
candidate is skipped when the named is absent, `@a` beats `$x` for a
List or Array. Quirks: `Int:D` against `Int` for a defined Int is
**Ambiguous** (only the type object is unambiguous), a coercion
candidate `Str(Int)` against `Int` is Ambiguous, and a `{*}`-only proto's
own parameter types are not enforced (`proto s(Int $x) {*}` accepts a
Str). Arity mistakes on an `only` sub are `X::AdHoc`.
```
say do { multi f(Int $x) { }; multi f(Str $x) { }; multi g(Int $x, Any $y) { "a" }; multi g(Any $x, Int $y) { "b" }; my $r = 1.5; my @none; my @two = 1, 2; ((try f($r)) // $!.^name, (try g(1, 1)) // $!.^name, (try f(|@none)) // $!.^name, (try f(|@two)) // $!.^name, (try f(1, |{:n})) // $!.^name, (try sub ($x) { }(1, :n)) // $!.^name, (try sub ($x) { }(|@none)) // $!.^name, (try sub ($x) { }(|@two)) // $!.^name, g(1, "s"), g("s", 1), (try &f($r)) // $!.^name, (try &f.candidates[0]($r)) // $!.^name, (try &f.candidates[0]("s")) // $!.^name, &f.cando(\(1.5)).elems, &g.cando(\(1, 1)).elems, &g.cando(\(1, 1)).map(*.signature.gist).raku, &f.candidates[0].dispatcher.gist, (try EVAL 'multi h(Int $x) { 1 }; multi h(Int $x) { 2 }; h(1)') // $!.^name, (try EVAL 'sub o(Int $x) { 1 }; multi o(Str $x) { 2 }; 1') // $!.^name, (try EVAL 'multi p(Int $x) { 1 }; sub p(Str $x) { 2 }; 1') // $!.^name, (try EVAL 'only q($x) { 1 }; &q.multi.raku') // $!.^name, (try EVAL 'proto r(|) {*}; r(1)') // $!.^name, (try EVAL 'proto s(Int $x) {*}; multi s($x) { "s" }; my $v = "str"; s($v)') // $!.^name, (try EVAL 'proto t($x) { "proto:" ~ {*} }; multi t($x) { "cand" }; t(1)') // $!.^name, (try EVAL 'proto u(|) {*}; multi u(Int $x) { "int" }; my $v = "s"; u($v)') // $!.^name, (try EVAL 'multi v(Int $x) { "int" }; multi v(Int $x) is default { "def" }; v(1)') // $!.^name, (try EVAL 'multi w(Int $x) is default { "d1" }; multi w(Int $x) is default { "d2" }; w(1)') // $!.^name, (try EVAL 'multi x1(Int $x where * > 5) { "big" }; multi x1(Int $x) { "int" }; x1(3) ~ x1(9)') // $!.^name, (try EVAL 'multi x2(Int $x) { "int" }; multi x2(Int $x where * > 5) { "big" }; x2(3) ~ x2(9)') // $!.^name, (try EVAL 'multi x3(Int:D $x) { "d" }; multi x3(Int $x) { "u" }; x3(3) ~ x3(Int)') // $!.^name, (try EVAL 'multi x4($x, $y?) { "opt" }; multi x4($x) { "one" }; x4(1)') // $!.^name, (try EVAL 'multi x5($x, *@r) { "slurpy" }; multi x5($x) { "one" }; x5(1)') // $!.^name, (try EVAL 'multi x6($x, :$n) { "named" }; multi x6($x) { "plain" }; x6(1) ~ x6(1, :n)') // $!.^name, (try EVAL 'multi x7($x, :$n!) { "named" }; multi x7($x) { "plain" }; x7(1) ~ x7(1, :n)') // $!.^name, (try EVAL 'multi x8(Int $x) { "int" }; multi x8(Numeric $x) { "num" }; x8(1) ~ x8(1.5)') // $!.^name, (try EVAL 'multi x9(Any $x) { "any" }; multi x9(Mu $x) { "mu" }; x9(1) ~ x9(Mu)') // $!.^name, (try EVAL 'multi xa(@a) { "arr" }; multi xa($x) { "sca" }; xa([1]) ~ xa(1) ~ xa((1,2))') // $!.^name, (try EVAL 'multi xb(Str(Int) $x) { "coerce" }; multi xb(Int $x) { "int" }; xb(1)') // $!.^name).join(" ") }
# rakudo 2026.08: X::Multi::NoMatch X::Multi::Ambiguous X::Multi::NoMatch X::Multi::NoMatch X::Multi::NoMatch X::AdHoc X::AdHoc X::AdHoc a b X::Multi::NoMatch X::TypeCheck::Binding::Parameter X::TypeCheck::Binding::Parameter 0 2 ("(Int \$x, \$y)", "(\$x, Int \$y)").Seq &f X::Multi::Ambiguous X::Redeclaration X::Redeclaration 0 X::TypeCheck::Argument+{X::Comp} s proto:cand X::Multi::NoMatch def X::Multi::Ambiguous intbig intbig X::Multi::Ambiguous one one namednamed plainnamed intnum anymu arrscaarr X::Multi::Ambiguous
say do { multi f(Int $x) { }; multi f(Str $x) { }; multi g(Int $x, Any $y) { "a" }; multi g(Any $x, Int $y) { "b" }; my $r = 1.5; my @o; try f($r); my $e1 = $!; @o.push($e1.^name, (try $e1.dispatcher.name) // "NA", (try $e1.capture.raku) // "NA", (try $e1.dispatcher.is_dispatcher) // "NA"); try g(1, 1); my $e2 = $!; @o.push($e2.^name, (try $e2.dispatcher.name) // "NA", (try $e2.ambiguous.elems) // "NA", (try $e2.capture.raku) // "NA", (try $e2.ambiguous[0].signature.gist) // "NA"); try f(1, |{:n}); my $e3 = $!; @o.push($e3.^name, (try $e3.capture.raku) // "NA"); try &f.candidates[0]($r); my $e4 = $!; @o.push($e4.^name, (try $e4.symbol) // "NA"); @o.join(" ") }
# rakudo 2026.08: X::Multi::NoMatch f \(1.5) True X::Multi::Ambiguous g 2 \(1, 1) (Int $x, $y) X::Multi::NoMatch \(1, :n(Bool::True)) X::TypeCheck::Binding::Parameter $x
```
rakupp 4.0.1-84: differs — no ambiguity is ever detected (the first candidate wins: `g(1, 1)` "a", two defaults "d1", `Int:D`/`Int` "du"), an `only` sub silently ignores wrong arity and unexpected nameds (`Nil`), `is default` is ignored, the optional-positional candidate wins over the plain one, `Mu` cannot take `Mu`, the empty proto is a run-time `X::Multi::NoMatch`, and `X::Multi::NoMatch` carries no `.dispatcher`/`.capture`.

### CO-12  wrap, unwrap, WrapHandle, callsame and friends inside a wrapper   D:partial R:yes V:quirk
`.wrap(&wrapper)` installs the wrapper outermost (the newest wrapper
runs first) and returns a `Routine::WrapHandle` (gist `(WrapHandle)`,
`.raku` `Routine::WrapHandle.new`); the routine's type becomes
`Sub+{Routine::Wrapped}`, `.is-wrapped` True, and its name, signature,
arity and candidates are unchanged. Inside the wrapper `callsame`
calls the next layer with the same arguments, `callwith` with new ones,
`nextsame`/`nextwith` do the same but never return to the wrapper. A
wrapper that does not call on replaces the routine. `.unwrap($handle)`
returns `Empty` and `$handle.restore` True once, then False; unwrapping
twice, with a foreign object, or a handle already restored is
`X::Routine::Unwrap`. Setting routines can be wrapped. Quirk: `nextsame`
and `nextwith` in a **pointy-block** wrapper throw
`X::ControlFlow::Return` (`.out-of-dynamic-scope` False) because they
return from an enclosing routine the block does not have; `callsame`
and `callwith` work there, and a `sub` wrapper takes all four. `.soft` is
`Nil`.
```
say do { sub f($x) { "f($x)" }; my @o; my $h = &f.wrap(-> $x { "w1<" ~ callsame() ~ ">" }); @o.push(f(1)); @o.push($h.^name); my $h2 = &f.wrap(-> $x { "w2<" ~ callwith($x + 1) ~ ">" }); @o.push(f(1)); @o.push(&f.unwrap($h).raku); @o.push(f(1)); @o.push($h2.restore); @o.push(f(1)); @o.push($h2.restore); @o.push((try &f.unwrap($h)) // $!.^name); @o.push((try &f.unwrap(42)) // $!.^name); @o.push((try &f.unwrap("x")) // $!.^name); my $h3 = &f.wrap(sub ($x) { nextsame }); @o.push(f(2)); &f.unwrap($h3); my $h4 = &f.wrap(-> $x { "no-call" }); @o.push(f(3)); @o.push(&f.candidates.elems); &f.unwrap($h4); my $h5 = &f.wrap(sub ($x) { my $r = callsame; $r ~ "!" }); @o.push(f(4)); @o.push(&f.name); @o.push(&f.signature.raku); @o.push(&f.^name); @o.push(&f.WHAT.gist); @o.push($h.can('restore').so); @o.push($h.restore); @o.push($h5.^name, $h5.WHAT.gist, $h5.raku.substr(0, 25)); @o.push(&f.wrap(sub ($x) { nextwith($x * 10) }).^name); @o.push(f(5)); @o.push(&f.arity, &f.count, &f.multi); @o.push((try &say.wrap(-> |c { callsame }).^name) // $!.^name); my $h6 = &f.wrap(-> $x { nextsame }); @o.push((try f(6)) // $!.^name); &f.unwrap($h6); my $h7 = &f.wrap(-> $x { nextwith($x) }); @o.push((try f(7)) // $!.^name ~ ":" ~ $!.out-of-dynamic-scope); &f.unwrap($h7); my $h8 = &f.wrap(sub ($x) { my $r = nextsame; "after-nextsame" }); @o.push(f(8)); &f.unwrap($h8); @o.push(&f.is-wrapped); @o.push(&f.soft.raku); @o.push(sub () { }.is-wrapped); @o.push(&f.is-wrapped.^name); @o.join(" ") }
# rakudo 2026.08: w1<f(1)> Routine::WrapHandle w2<w1<f(2)>> Empty w2<f(2)> True f(1) False X::Routine::Unwrap X::Routine::Unwrap X::Routine::Unwrap f(2) no-call 1 f(4)! f :($x) Sub+{Routine::Wrapped} (Sub+{Wrapped}) True False Routine::WrapHandle (WrapHandle) Routine::WrapHandle.new Routine::WrapHandle f(50)! 1 1 0 Routine::WrapHandle X::ControlFlow::Return X::ControlFlow::Return:False f(80)! True Nil False Bool
```
rakupp 4.0.1-84: differs — the line died at `.out-of-dynamic-scope`; without that field it answers: the handle is a `WrapHandle` whose `.raku` is a hash dump, `unwrap` returns the routine instead of `Empty`, `restore` returns the routine instead of True/False and a second restore or a foreign handle does not throw, the wrapped routine stays a plain `Sub`, `.is-wrapped` and `.soft` are missing; the call results (`w1<f(1)>`, `w2<w1<f(2)>>`, `f(50)!`, `f(80)!`) and the pointy-block `X::ControlFlow::Return` match.

### CO-13  Wrapping methods, multis and candidates; stacking             D:partial R:yes V:spec
A method wrapper receives the invocant first (`-> $self, $x`). Wrapping
a multi's proto wraps the whole dispatch (every candidate is reached
through it); wrapping one candidate affects that candidate only and
leaves the proto's `.is-wrapped` False. A recursive routine passes
through its wrapper at every level. Two wrappers stack newest-first
(`callsame() ~ "a"` then `~ "b"` gives `xab`); restoring the inner one
leaves the outer (`xb`); a restored handle answers False afterwards.
`callsame` inside a wrapper called through another wrapper reaches the
next layer with the arguments that layer received. `return` in a sub
wrapper ends the call. A wrapper whose signature does not fit the call
is installed anyway and fails at call time (`X::AdHoc`).
```
say do { class C { method m($x) { "m($x)" }; multi method n(Int $x) { "nInt" }; multi method n(Str $x) { "nStr" } }; my @o; my $h = C.^lookup('m').wrap(-> $self, $x { "W<" ~ callsame() ~ ">" }); @o.push(C.new.m(1)); C.^lookup('m').unwrap($h); @o.push(C.new.m(1)); my $h2 = C.^lookup('n').wrap(-> $self, $x { "P<" ~ callsame() ~ ">" }); @o.push(C.new.n(1)); @o.push(C.new.n("s")); @o.push(C.^lookup('n').is_dispatcher); C.^lookup('n').unwrap($h2); @o.push(C.new.n(1)); my $h3 = C.^lookup('n').candidates[0].wrap(-> $self, $x { "C<" ~ callsame() ~ ">" }); @o.push(C.new.n(1)); @o.push(C.new.n("s")); my $depth = 0; sub r($n) { $depth++; $n > 0 ?? r($n - 1) !! "done" }; my $h4 = &r.wrap(-> $n { "[" ~ callsame() ~ "]" }); @o.push(r(2)); @o.push($depth); &r.unwrap($h4); sub wrapped-twice($x) { $x }; my $ha = &wrapped-twice.wrap(-> $x { callsame() ~ "a" }); my $hb = &wrapped-twice.wrap(-> $x { callsame() ~ "b" }); @o.push(wrapped-twice("x")); $ha.restore; @o.push(wrapped-twice("x")); @o.push($ha.restore); @o.push($hb.restore); @o.push(wrapped-twice("x")); @o.push($hb.restore); sub cw($x) { "cw($x)" }; &cw.wrap(-> $x { callwith($x + 1) }); @o.push(cw(1)); @o.push(&cw.wrap(-> $x { callsame }).^name); @o.push(cw(1)); sub ret($x) { "ret" }; &ret.wrap(sub ($x) { return "early"; callsame }); @o.push(ret(1)); sub sig($x) { "sig" }; @o.push((try &sig.wrap(-> $x, $y { callsame }).^name) // $!.^name); @o.push((try sig(1)) // $!.^name); sub proto-w($x) { "pw" }; my $hp = &proto-w.wrap(-> $x { callsame() ~ "1" }); my $hq = &proto-w.wrap(-> $x { callsame() ~ "2" }); @o.push(proto-w(1)); $hp.restore; @o.push(proto-w(1)); @o.push(C.^lookup('m').is-wrapped, C.^lookup('n').is-wrapped, C.^lookup('n').candidates[0].is-wrapped, &wrapped-twice.is-wrapped); @o.join(" ") }
# rakudo 2026.08: W<m(1)> m(1) P<nInt> P<nStr> True nInt C<nInt> nStr [[[done]]] 3 xab xb False True x False cw(2) Routine::WrapHandle cw(2) early Routine::WrapHandle X::AdHoc pw12 pw2 False False True False
```
rakupp 4.0.1-84: differs — the line died at `.is-wrapped`; without it every call result matches, but `restore` returns the routine instead of True/False, the handle type is `WrapHandle`, and the arity-mismatched wrapper is skipped (`sig(1)` answers `sig`) instead of dying.

### CO-14  assuming                                                      D:partial R:yes V:quirk
`.assuming(…)` returns a new `Sub` named `assumed.<name>` (gist
`&assumed.f`, `.raku` `sub assumed.f ($b, $c) …`, package GLOBAL, line
1, `.multi` 0) whose signature is the original's minus the primed
parameters: positionals are primed left to right, `*` skips one, a
named primed by value becomes that parameter's default (`:(:$n = "X")`),
a slurpy stays. Priming checks types (`X::TypeCheck::Binding::Parameter`
at priming time), refuses too many positionals or an unknown named
(`X::AdHoc`), refuses a non-container for `is rw` (`X::Parameter::RW`),
coerces a coercion parameter and fixes a `::T` capture — but does **not**
run a `where` (the primed sub is built and fails when called).
Quirk: a variable primed into a positional is captured as its
**container**, so later assignments and pushes are seen by the primed
sub (`$v = 9` after priming gives `9-2-3`). Blocks, WhateverCodes and
methods (with `*` for the invocant) can be primed; `assuming()` with
nothing is a copy with the full signature.
```
say do { sub f($a, $b, $c) { "$a-$b-$c" }; sub g($x, :$n = "N") { "$x:$n" }; sub h(*@a) { @a.join(",") }; my &p = &f.assuming(1); my &q = &f.assuming(*, 2); my &r = &f.assuming(*, *, 3); my &s = &g.assuming(:n<X>); my &t = &g.assuming(9); my $blk = { $^a + $^b }; my $wc = * + *; (p(2, 3), q(1, 3), r(1, 2), s(5), t(), t(:n<Y>), &p.signature.raku, &q.signature.raku, &r.signature.raku, &s.signature.raku, &t.signature.raku, &p.name.raku, &p.^name, &p.arity, &p.count, &q.arity, &f.assuming(1, 2, 3)(), &h.assuming(1, 2)(3), &h.assuming(1, 2).signature.raku, (try &f.assuming(1, 2, 3, 4).^name) // $!.^name, (try &f.assuming(1, 2, 3, 4)()) // $!.^name, $blk.assuming(10)(5), $wc.assuming(1)(2), Str.^lookup('comb').assuming(*, /\w/)("a b").raku, sub (Int $x, $y) { "$x$y" }.assuming(1).signature.raku, (try sub (Int $x, $y) { }.assuming("s").^name) // $!.^name, (try sub (Int $x, $y) { }.assuming("s")(1)) // $!.^name, &f.assuming(1).assuming(2)(3), do { my @a = 1, 2; my &u = &h.assuming(@a); @a.push(3); u() }, do { my $v = 1; my &u = &f.assuming($v, 2); $v = 9; u(3) }, &g.assuming(1, :n<A>).signature.raku, &f.assuming(*, 2).name.raku, &f.assuming(*, 2).gist, &p.file, &p.raku.subst(/'#`(' <-[)]>* ')'/, '#`(…)'), &f.assuming().signature.raku, &f.assuming()(7, 8, 9), (try &g.assuming(:zz<1>).^name) // $!.^name, (try &g.assuming(:zz<1>)(1)) // $!.^name, sub (*%h) { %h.keys.sort.join }.assuming(:a)(:b), sub ($x, *@r) { "$x|@r[]" }.assuming(1, 2)(3, 4), sub ($x?) { $x.raku }.assuming()(), sub ($x?) { $x.raku }.assuming(5)(), sub ($x = 3) { $x }.assuming(*)(), sub ($x = 3) { $x }.assuming(*).signature.raku, $blk.assuming(10).signature.raku, $wc.assuming(1).signature.raku, &q.name.raku, &s.name.raku, &p.multi, &p.package.^name, &p.line, do { my $z = 5; sub ($x is rw) { $x++ }.assuming($z)(); $z }, (try sub ($x is rw) { $x++ }.assuming(5)()) // $!.^name, sub (Int() $x) { $x.raku }.assuming("4")(), sub (::T $x, T $y) { T.^name }.assuming(1)(2), (try sub (::T $x, T $y) { T.^name }.assuming(1)("s")) // $!.^name, sub (:$n!) { $n }.assuming(:n(1))(), (try sub (:$n!) { $n }.assuming()()) // $!.^name, sub ($x, $y) { "$x$y" }.assuming(*, *)(1, 2), sub ($x, $y) { "$x$y" }.assuming(*, *).signature.raku, sub (@a) { @a.elems }.assuming([1, 2])(), sub (%h) { %h.elems }.assuming({a => 1})(), sub (&c) { c() }.assuming({ 42 })(), sub ($x where * > 2) { $x }.assuming(5)(), (try sub ($x where * > 2) { $x }.assuming(1).^name) // $!.^name, (try sub ($x where * > 2) { $x }.assuming(1)()) // $!.^name, sub ($x where * > 2) { $x }.assuming(5).signature.raku, sub (Int $x where * > 2) { $x }.assuming(*).signature.raku).join(" ") }
# rakudo 2026.08: 1-2-3 1-2-3 1-2-3 5:X 9:N 9:Y :($b, $c) :($a, $c) :($a, $b) :($x, :$n = "X") :(:$n = "N") "assumed.f" Sub 2 2 2 1-2-3 1,2,3 :(*@a) X::AdHoc X::AdHoc 15 3 ("a", "b").Seq :($y) X::TypeCheck::Binding::Parameter X::TypeCheck::Binding::Parameter 1-2-3 1,2,3 9-2-3 :(:$n = "A") "assumed.f" &assumed.f sub assumed.f ($b, $c) { #`(…) ... } :($a, $b, $c) 7-8-9 X::AdHoc X::AdHoc ab 1|2 3 4 Any 5 3 :($x = 3) :($b) :($whatevercode_arg_2 is raw) "assumed.f" "assumed.g" 0 GLOBAL 1 6 X::Parameter::RW 4 Int X::TypeCheck::Binding::Parameter 1 X::AdHoc 12 :($x, $y) 2 1 42 5 Sub X::TypeCheck::Binding::Parameter :() :(Int $x where { ... })
```
rakupp 4.0.1-84: differs — the primed sub has no name (`""`, gist `sub { ... }`, `.file` `SETTING::src/core.c/`, `.raku` `sub { ... }`, `.line` 0, `.multi` False), a primed named prints as `:(:$n = Code.new)`, a primed scalar is captured by value (`1-2-3`), an `is rw` parameter primed with a container does not write through it and a literal primed into it is accepted, a block's or WhateverCode's primed signature is `:()`, and a missing required named is `X::Parameter::RequiredNamed` instead of `X::AdHoc`.

### CO-15  is rw and is raw on routines, return-rw                        D:yes R:yes V:spec
A sub returns a decontainerized value (`sub { $v }().VAR` is `Int`,
assigning to the call is `X::Assignment::RO`); `is rw` and `is raw`
make the call return the container (assignable, autovivifying through
`%h<a><b>` and `@a[0]`); `.rw` is True for both traits. `return` inside
an `is rw` sub hands back a **read-only** container (assigning is
`X::AdHoc`); `return-rw` keeps it writable, also in a sub without the
trait. An `is rw` sub that returns a literal is still `X::Assignment::RO`
at the assignment. A method takes `is rw` the same way. A Block or
pointy returns its containers untouched. `.raku` does not show the
trait; `.is-rw` does not exist.
```
say do { my $v = 1; sub a() is rw { $v }; sub b() { $v }; sub c() is rw { return $v }; sub d() is rw { return-rw $v }; my @o; a() = 2; @o.push($v); @o.push((try { b() = 3; "ok" }) // $!.^name); @o.push((try { c() = 4; "ok" }) // $!.^name); d() = 5; @o.push($v); @o.push(&a.rw, &b.rw, &c.rw, &a.rw.^name); sub e() is raw { $v }; e() = 6; @o.push($v, &e.rw); sub f() { return-rw $v }; @o.push((try { f() = 7; "ok" }) // $!.^name); @o.push($v); my %h; sub w(\thing, *@keys) is rw { my $c := thing; $c := $c{$_} for @keys; $c }; w(%h, "a", "b") = "viv"; @o.push(%h<a><b>); @o.push(&a.raku.subst(/'#`(' <-[)]>* ')'/, '#`(…)')); @o.push(&a.signature.raku); sub g() is rw { 42 }; @o.push((try { g() = 1; "ok" }) // $!.^name); @o.push(a().VAR.^name, b().VAR.^name, e().VAR.^name, f().VAR.^name); my $m = (method () is rw { $v }); @o.push($m.rw); @o.push(({ $v })().VAR.^name); @o.push((-> { $v })().VAR.^name); @o.push((sub { $v })().VAR.^name); @o.push((sub () is rw { $v })().VAR.^name); @o.push((sub () is rw { return $v })().VAR.^name); @o.push((sub () is rw { return-rw $v })().VAR.^name); @o.push((sub () { return-rw $v })().VAR.^name); @o.push((sub () is raw { $v })().VAR.^name); @o.push((try { (sub () is rw { my $x = 1; return-rw $x })() = 9; "ok" }) // $!.^name); my @arr = 1, 2; sub el() is rw { @arr[0] }; el() = 8; @o.push(@arr.raku); sub hh() is rw { %h<new> }; hh() = "auto"; @o.push(%h<new>); @o.push((try &a.is-rw) // $!.^name); @o.push((-> { $v }).VAR.^name); @o.join(" ") }
# rakudo 2026.08: 2 X::Assignment::RO X::AdHoc 5 True False True Bool 6 True ok 7 viv sub a { #`(…) ... } :() X::Assignment::RO Scalar Int Scalar Scalar True Scalar Scalar Int Scalar Scalar Scalar Scalar Scalar ok [8, 2] auto X::Method::NotFound Block
```
rakupp 4.0.1-84: differs — no call returns a container (`.VAR.^name` is `Int` throughout), yet `c() = 4` (plain `return` under `is rw`) and `g() = 1` (a literal) are accepted silently, `return-rw` of a fresh scalar is `X::Assignment::RO`, and autovivifying through an `is rw` sub leaves `%h<a><b>` undefined (the "Use of uninitialized value" warning).

### CO-16  is pure, is default, is hidden-from-backtrace, is implementation-detail, is nodal, is test-assertion, unknown traits   D:partial R:partial V:spec
Each of these traits mixes a role into the routine (`Sub+{is-pure}`,
`Sub+{is-nodal}+{is-pure}` when stacked) that adds a predicate method:
`is-pure`, `is-hidden-from-backtrace`, `nodal`, `default`,
`is-hidden-from-USAGE`, `is-test-assertion` (the last needs `use Test`,
type `Sub+{Test::is-test-assertion}`); without the trait the method is
`X::Method::NotFound`, except `is-implementation-detail`, which every
Code answers (False). `is default` wins a dispatch tie; `is raw` sets
`.rw`; `is onlystar` sets `.onlystar`; `is inlinable` is accepted. An
unknown trait is a compile-time `X::Comp::Trait::Unknown` with
`.type` "is", `.subtype` the name and `.declaring` "sub", "a
parameter", "an attribute" or " variable" (with that leading space);
`is cached` without the experimental pragma is `X::Experimental`. A
hidden routine's frame is still in `.backtrace` (`.is-hidden` True on
it) but is left out of the rendered backtrace (`.Str`) and kept by
`.full`.
```
say do { sub a() is pure { 1 }; sub b() is hidden-from-backtrace { die "x" }; sub bb() { die "y" }; sub c() is implementation-detail { 1 }; sub d() is nodal { 1 }; sub f() { 1 }; multi g() is default { "d" }; multi g() { "nd" }; ((try &c.is-implementation-detail) // $!.^name, (try &f.is-implementation-detail) // $!.^name, g(), do { multi h(Int $x) is default { "d" }; multi h(Int $x) { "nd" }; (try h(1)) // $!.^name }, do { multi i(Int $x) { "narrow" }; multi i($x) is default { "def" }; i(1) }, do { multi hh(Int $x) { "1" }; multi hh(Int $x) { "2" }; (try hh(1)) // $!.^name }, do { try b(); $!.backtrace.map(*.subname).grep(* eq "b").elems }, do { try bb(); $!.backtrace.map(*.subname).grep(* eq "bb").elems }, (try EVAL 'sub z() is bogus { }') // $!.^name ~ ":" ~ $!.subtype ~ ":" ~ $!.declaring ~ ":" ~ $!.type, (try EVAL 'sub z() is cached { }') // $!.^name, &a.gist, &c.gist, &a.raku.subst(/'#`(' <-[)]>* ')'/, '#`(…)'), (try EVAL 'my $x is bogus = 1') // $!.^name ~ ":" ~ $!.declaring, (try EVAL 'sub z2($x is bogus) { }') // $!.^name ~ ":" ~ $!.declaring ~ ":" ~ $!.subtype, (try EVAL 'class Z { has $.x is bogus }') // $!.^name ~ ":" ~ $!.declaring, (try EVAL 'sub z4() is nodal is pure { 1 }; &z4.^name') // $!.^name, &a.^name, &c.^name, &b.^name, &d.^name, &g.candidates[0].^name, (try &a.is-pure) // $!.^name, (try &f.is-pure) // $!.^name, (try &b.is-hidden-from-backtrace) // $!.^name, (try &f.is-hidden-from-backtrace) // $!.^name, (try &d.nodal) // $!.^name, (try &f.nodal) // $!.^name, (try &g.candidates[0].default) // $!.^name, (try &g.candidates[1].default) // $!.^name, (try &g.candidates[0].default.^name) // $!.^name, (try EVAL 'sub z5() is pure { 1 }; z5(); 1') // $!.^name, (try EVAL 'sub z6() is nodal { 1 }; z6(); 1') // $!.^name, (try EVAL 'sub z7() is hidden-from-USAGE { 1 }; &z7.is-hidden-from-USAGE') // $!.^name, (try EVAL 'sub z8() is raw { 1 }; &z8.rw') // $!.^name, (try EVAL 'sub z9() is onlystar { 1 }; &z9.onlystar') // $!.^name, (try EVAL 'sub z10() is inlinable { 1 }; 1') // $!.^name, (try EVAL 'sub z11() is DEPRECATED is pure { 1 }; &z11.^name') // $!.^name).join(" ") }
# rakudo 2026.08: True False d d narrow X::Multi::Ambiguous 1 1 X::Comp::Trait::Unknown:bogus:sub:is X::Experimental &a &c sub a { #`(…) ... } X::Comp::Trait::Unknown: variable X::Comp::Trait::Unknown:a parameter:bogus X::Comp::Trait::Unknown:an attribute Sub+{is-nodal}+{is-pure} Sub+{is-pure} Sub+{is-implementation-detail} Sub+{is-hidden-from-backtrace} Sub+{is-nodal} Sub+{<anon|1>} True X::Method::NotFound True X::Method::NotFound True X::Method::NotFound True X::Method::NotFound Bool 1 1 True True True 1 Sub+{is-DEPRECATED}+{is-pure}
use Test; sub t() is test-assertion { 1 }; sub u() { 1 }; say (&t.is-test-assertion, (try &u.is-test-assertion) // $!.^name, &t.^name, (try EVAL 'sub v() is test-assertion { 1 }; 1') // $!.^name).join(" ")
# rakudo 2026.08: True X::Method::NotFound Sub+{Test::is-test-assertion} 1
say do { sub b() is hidden-from-backtrace { die "x" }; sub bb() { die "y" }; try b(); my $e1 = $!; try bb(); my $e2 = $!; ($e1.backtrace.Str.contains("in sub b "), $e2.backtrace.Str.contains("in sub bb "), $e1.backtrace.map(*.subname).grep(* eq "b").elems, $e1.backtrace.map(*.is-hidden).raku, $e1.backtrace.full.Str.contains("in sub b ")).join(" ") }
# rakudo 2026.08: False True 1 (Bool::False, Bool::False, Bool::True, Bool::False, Bool::False, Bool::False).Seq True
```
rakupp 4.0.1-84: differs — no trait mixes anything in (every `.^name` stays `Sub`, every predicate is `X::Method::NotFound`, `is-implementation-detail` included), unknown traits and `is cached` compile silently (the EVALs return their routine, hence the "coerced to string" warnings), `is test-assertion` is missing, and the hidden frame is still rendered.

### CO-17  is DEPRECATED                                                 D:yes R:no V:spec
`is DEPRECATED("new")` mixes in `is-DEPRECATED` (type
`Sub+{is-DEPRECATED}`), adds an `ENTER` phaser (`.has-phasers` True,
`.phasers('ENTER').elems` 1) and gives the routine a `.DEPRECATED`
method answering the alternative, "something else" when none was
given; a plain sub has no such method. Calls work; at process exit a
report is printed to standard error: "Saw N occurrences of deprecated
code.", a rule of `=`, then per routine "Sub old (from GLOBAL) seen at:"
with file and line and "Please use new instead." (the probe shows the
report's first lines).
```
say do { sub old() is DEPRECATED("new") { 42 }; sub old2() is DEPRECATED { 1 }; (old(), &old.^name, &old.has-phasers, &old.phasers('ENTER').elems, old2(), old(), (try &old.DEPRECATED) // $!.^name, (try &old2.DEPRECATED) // $!.^name, (try (sub () { }).DEPRECATED) // $!.^name).join(" ") }
# rakudo 2026.08: 42 Sub+{is-DEPRECATED} True 1 1 42 new something else X::Method::NotFound|Saw 2 occurrences of deprecated code.|================================================================================|Sub old (from GLOBAL) seen at:|  -e, line 1|Please use new instead.
```
rakupp 4.0.1-84: differs — the line died at `.has-phasers`; no report is printed and `.DEPRECATED` is missing.

### CO-18  is export                                                     D:partial R:partial V:spec
`is export` on a routine, `our sub`, `our` variable, constant, class,
role or enum installs the symbol in the package's `EXPORT::DEFAULT` and
`EXPORT::ALL`; `is export(:tag)` in `EXPORT::tag` and `ALL` only;
`(:DEFAULT, :other)` in both; a multi exports its proto (one symbol,
`.is_dispatcher` True). `EXPORT::` lists the tag packages; an exported
`my` sub is still not reachable as `M::a`. An enum exports its values
too; a role exports as its short name. A non-Pair argument (`is
export(42)`) is a compile-time `X::AdHoc`. Two exported subs of one name
in a module are `X::Redeclaration`; importing the same name from two
modules is `X::Export::NameClash`; `import M` takes `DEFAULT` only (a
`:t`-tagged sub is then undeclared), `import M :t` and `:ALL` take it, a
tag the module does not define is a compile-time `X::Import::NoSuchTag`
(also for `:MANDATORY` exports asked by another tag).
```
say do { my @o; module M { sub a() is export { "a" }; sub b() is export(:tag) { "b" }; sub c() is export(:DEFAULT, :other) { "c" }; sub d() { "d" }; our sub e() is export { "e" }; sub f() is export(:ALL) { "f" }; sub g() is export(:tag, :tag2) { "g" }; multi mm(Int) is export { "mmInt" }; multi mm(Str) is export { "mmStr" } }; @o.push(M::EXPORT::DEFAULT::.keys.sort.raku, M::EXPORT::ALL::.keys.sort.raku, M::EXPORT::tag::.keys.sort.raku, M::EXPORT::other::.keys.sort.raku, M::EXPORT::tag2::.keys.sort.raku, M::EXPORT::.keys.sort.raku, (try EVAL 'M::a()') // $!.^name, M::e(), M::EXPORT::DEFAULT::<&a>(), (try EVAL 'module M2 { sub z() is export(42) { } }') // $!.^name, M::EXPORT::DEFAULT::<&mm>.is_dispatcher, M::EXPORT::DEFAULT::<&mm>(1), M::EXPORT::DEFAULT::<&mm>.candidates.elems, M::EXPORT::DEFAULT::<&mm>.package.^name, M::EXPORT::DEFAULT::<&a>.package.^name, M::EXPORT::DEFAULT::<&a> === M::EXPORT::ALL::<&a>, (try EVAL 'module M3 { sub z3() is export { 1 }; sub z3() is export { 2 } }; 1') // $!.^name, (try EVAL 'module M4 { sub z4() is export { 1 } }; module M5 { sub z4() is export { 2 } }; import M4; import M5; z4()') // $!.^name, (try EVAL 'module M6 { sub z6() is export { 6 } }; import M6; z6()') // $!.^name, (try EVAL 'module M7 { sub z7() is export(:t) { 7 } }; import M7; z7()') // $!.^name, (try EVAL 'module M8 { sub z8() is export(:t) { 8 } }; import M8 :t; z8()') // $!.^name, (try EVAL 'module M9 { sub z9() is export(:t) { 9 } }; import M9 :ALL; z9()') // $!.^name, (try EVAL 'module M10 { sub z10() is export(:MANDATORY) { 10 } }; import M10 :t; z10()') // $!.^name, (try EVAL 'module M11 { sub z11() is export(:DEFAULT) { 11 } }; import M11 :DEFAULT; z11()') // $!.^name, (try EVAL 'module M12 { sub z12() is export { 12 } }; import M12 :nope; z12()') // $!.^name); @o.join(" ") }
# rakudo 2026.08: ("\&a", "\&c", "\&e", "\&mm").Seq ("\&a", "\&b", "\&c", "\&e", "\&f", "\&g", "\&mm").Seq ("\&b", "\&g").Seq ("\&c",).Seq ("\&g",).Seq ("ALL", "DEFAULT", "other", "tag", "tag2").Seq X::AdHoc e a X::AdHoc+{X::Comp} True mmInt 2 M M True X::Redeclaration X::Export::NameClash 6 X::Undeclared::Symbols 8 9 X::Import::NoSuchTag+{X::Comp} 11 X::Import::NoSuchTag+{X::Comp}
say do { my @o; @o.push((try EVAL 'module N { our $v is export = 5; constant K is export = 7; class KL is export { }; enum EN is export <e1 e2>; role RO is export { }; our sub s() is export { "s" } }; (N::EXPORT::DEFAULT::.keys.sort.raku, N::EXPORT::DEFAULT::<EN>.raku, N::EXPORT::DEFAULT::<e1>.raku, N::EXPORT::DEFAULT::<KL>.raku, N::EXPORT::DEFAULT::<RO>.raku, N::EXPORT::DEFAULT::<K>, N::EXPORT::DEFAULT::<$v>, N::EXPORT::DEFAULT::<&s>()).join(" ")') // $!.^name); @o.join(" ") }
# rakudo 2026.08: ("\$v", "\&s", "EN", "K", "KL", "RO", "e1", "e2").Seq EN EN::e1 N::KL N::RO 7 5 s
```
rakupp 4.0.1-84: differs — the first line died at `M::EXPORT::DEFAULT::<&a>()` ("Cannot invoke non-Callable value of type Any": the EXPORT packages are not populated), and in the second `N::EXPORT::DEFAULT::<EN>` is parsed as a placeholder (`X::Placeholder::Mainline`); measured separately, every import succeeds regardless of tag and no clash is detected.

### CO-19  Methods: invocant, `*%_`, gist, and how their signatures print   D:partial R:partial V:bug
A method's signature has the invocant as its first parameter
(`.invocant` True, name `""`, type the class, `:D`/`:U` from the
smiley, or `Mu` for an anonymous method) and an implicit `*%_` last
(`.slurpy`, `.named`, `named_names` `()`), so arity and count include
the invocant; a submethod likewise; `Method.gist` and `Submethod.gist`
are the bare name. Unknown nameds are accepted by a method (into
`%_`) and refused by a sub (`X::AdHoc`); too many positionals are
`X::AdHoc`. `cando` needs the invocant in the Capture and honours `:D`.
A `:` invocant marker in a sub is a compile-time
`X::Syntax::Signature::InvocantNotAllowed`, two markers
`X::Syntax::Signature::InvocantMarker`; calling a `:D` method on the
type object is `X::Parameter::InvalidConcreteness`. Bug: `.raku` and
`.gist` print the invocant with **two** colons — `:(C $:: $x, *%_)`,
`:($s:: $x, *%_)`, `(Cool $:: |)` — a spelling the docs give with one
colon and that does not re-parse (`X::Syntax::Signature::InvocantMarker`);
the parameter's own `.raku` is `C $:`.
```
say do { class C { method m($x) { }; method n(C:D: $x) { }; method o(::?CLASS:U $c: ) { }; submethod s() { }; method p(*%_) { }; method q(:$a) { } }; my method free(Int:D: $x) { self + $x }; (C.^lookup('m').signature.raku, C.^lookup('m').signature.gist, C.^lookup('n').signature.raku, C.^lookup('o').signature.raku, C.^lookup('s').signature.raku, C.^lookup('p').signature.raku, C.^lookup('q').signature.raku, C.^lookup('m').signature.params.elems, C.^lookup('m').signature.params[0].invocant, C.^lookup('m').signature.params[0].name.raku, C.^lookup('m').signature.params[0].type.^name, C.^lookup('n').signature.params[0].type.^name, C.^lookup('n').signature.params[0].modifier, C.^lookup('m').signature.params[*-1].named, C.^lookup('m').signature.params[*-1].slurpy, C.^lookup('m').signature.params[*-1].name, C.^lookup('m').signature.params[*-1].named_names.raku, C.^lookup('m').arity, C.^lookup('m').count, C.^lookup('m').gist, C.^lookup('s').gist, C.^lookup('m').^name, C.^lookup('s').^name, C.^lookup('m').name, C.^lookup('m').package.^name, &free.signature.raku, &free.gist, 5.&free(2), (try C.new.m(1, :extra)) // "extra-ok", (try C.new.m(1, 2)) // $!.^name, (try sub ($x) { }(1, :extra)) // $!.^name, C.^lookup('m').cando(\(C, 1)).elems, C.^lookup('m').cando(\(1)).elems, C.^lookup('n').cando(\(C, 1)).elems, C.^lookup('n').cando(\(C.new, 1)).elems, C.^lookup('m').multi, C.^lookup('m').yada, C.^lookup('m').signature.params[0].multi-invocant, C.^lookup('m').signature.params[1].multi-invocant, C.^lookup('m').signature.params[0].positional, C.^lookup('m').signature.params[0].usage-name.raku, C.^lookup('m').signature.params[0].sigil, C.^lookup('o').signature.params[0].name, C.^lookup('o').signature.params[0].modifier, C.^lookup('o').signature.params[0].type.^name, (method ($s: $x) { }).signature.raku, (method ($x) { }).signature.raku, (try EVAL '(sub ($s: $x) { }).signature.raku') // $!.^name, (try EVAL 'class Z { method m($a: $b: $c) { } }') // $!.^name, (try C.^lookup('n').(C, 1)) // $!.^name, C.^lookup('m').(C, 1).raku, C.^lookup('m').signature.params[1].name, C.^lookup('m').signature.params[0].raku, C.^lookup('m').signature.params[*-1].raku, C.^lookup('n').signature.params[0].raku, (method ($s: $x) { }).signature.params[0].raku, C.^lookup('q').signature.params[*-1].raku, Str.^lookup('comb').signature.gist, Str.^lookup('comb').candidates[0].signature.raku, (try EVAL 'class Z2 { submethod s($a: ) { } }; Z2.^lookup("s").signature.raku') // $!.^name, (try C.^lookup('n').cando(\(1))) // $!.^name, C.^lookup('p').signature.params.elems, (try C.new.p(1)) // $!.^name, C.new.p(:a, :b).raku, (try C.new.q(:b)) // "q-extra-ok", C.new.q(:a).raku, (try EVAL ':(Int $:: $x).raku') // $!.^name, (try EVAL ':(Int $: $x).raku') // $!.^name).join(" ") }
# rakudo 2026.08: :(C $:: $x, *%_) (C $:: $x, *%_) :(C:D $:: $x, *%_) :(C:U $c:: *%_) :(C $:: *%_) :(C $:: *%_) :(C $:: :$a, *%_) 3 True "" C C :D True True %_ () 2 2 m s Method Submethod m C :(Int:D $:: $x, *%_) free 7 extra-ok X::AdHoc X::AdHoc 1 0 0 1 0 False True True True "" $ $c :U C :($s:: $x, *%_) :(Mu $:: $x, *%_) X::Syntax::Signature::InvocantNotAllowed X::Syntax::Signature::InvocantMarker X::Parameter::InvalidConcreteness Nil $x C $: *%_ C:D $: $s: *%_ (Cool $:: |) :(Cool:D $:: *%_ --> Seq:D) :($a:: *%_)  2 X::AdHoc Nil q-extra-ok Nil X::Syntax::Signature::InvocantMarker :(Int $:: $x)
```
rakupp 4.0.1-84: differs — the invocant and `*%_` are absent from method signatures (`:($x)`, arity 1), `Method.gist` is `&m`, a submethod's `.^name` is `Method`, a method accepts two positionals and a sub an unknown named (`Nil`), `cando` ignores `:D`, `.multi` is False, both invocant-marker errors compile silently, calling a `:D` method on the type object returns `Nil`, and a parameter's `.raku` is a hash dump.

### CO-20  Operator properties: precedence, associative, thunky, iffy, prec, reducer, and is equiv/tighter/looser/assoc   D:partial R:partial V:bug
A Routine answers `.precedence` (a level string: `+` is `t=`, `*` `u=`,
prefix `-` `v=`, postfix `++` `x=`, `..` and `cmp` have `associative`
`non`, `,` `list`, `=` and `**` `right`), `.associative`, `.thunky`
(`""` or `.t` for `&&`), `.iffy` (True for `==`, `||`, `~~`), `.prec` (a
Hash with `prec`, `assoc`, `dba`; `.prec<iffy>` is `Any` for `+`, 1 for
`==`) and `.reducer` (`METAOP_REDUCE_LEFT`, `_LIST` for `,`, `_CHAIN` for
`==`, `_RIGHT` for right-associative). A user infix without traits is
`t=` with `associative` `""` and reduces left; `is equiv(&op)` or `is
equiv<op>` copies a level (`ee` then binds like `+`), `is tighter`/`is
looser` make a level just above or below (`t@=`, `t:=`; `1 pp 2 * 3` is
`(1+6)`, `1 ll 2 + 3` is `[1 5]`), `is assoc<right|non>` sets
associativity (`non` makes `1 nn 2 nn 3` a compile-time
`X::Syntax::NonAssociative`); `[rr]` reduces right. Mixing two
operators of one level takes the left one's associativity. `is
tighter<+>` on a non-operator is a compile-time `X::AdHoc`; `is
equiv(&say)` is accepted. A non-operator sub and a block answer
`precedence` `""` and `prec` `{}`. Bug: `.prec("prec")` throws
`X::TypeCheck::Return` (the one-argument form returns a Str against
its own `Hash:D` constraint).
```
say do { sub infix:<mm>($a, $b) is equiv(&infix:<*>) { "$a*$b" }; sub infix:<pp>($a, $b) is tighter(&infix:<+>) { "($a+$b)" }; sub infix:<ll>($a, $b) is looser(&infix:<+>) { "[$a $b]" }; sub infix:<rr>($a, $b) is assoc<right> { "($a r $b)" }; sub infix:<nn>($a, $b) is assoc<non> { }; sub infix:<zz>($a, $b) { "($a z $b)" }; sub infix:<ee>($a, $b) is equiv<+> { "$a e $b" }; (EVAL('1 pp 2 * 3'), EVAL('1 ll 2 + 3'), EVAL('1 rr 2 rr 3'), EVAL('1 zz 2 zz 3'), EVAL('2 mm 3 ~ "!"'), (try EVAL('1 nn 2 nn 3')) // $!.^name, EVAL('1 ee 2 * 3'), (try EVAL 'sub infix:<qq>($a, $b) is equiv(&say) { }; 1') // $!.^name, (try EVAL 'sub notop($a, $b) is tighter<+> { }; 1') // $!.^name, EVAL('[rr] 1, 2, 3'), EVAL('[zz] 1, 2, 3'), EVAL('[mm] 2, 3'), EVAL('1 rr 2 zz 3'), EVAL('1 zz 2 rr 3'), (try EVAL('1 pp 2 ll 3')) // $!.^name, (try &infix:<+>.precedence) // $!.^name, (try &infix:<+>.associative) // $!.^name, (try &infix:<*>.precedence) // $!.^name, (try &infix:<mm>.precedence) // $!.^name, (try &infix:<pp>.precedence) // $!.^name, (try &infix:<ll>.precedence) // $!.^name, (try &infix:<ee>.precedence) // $!.^name, (try &infix:<+>.thunky.raku) // $!.^name, (try &infix:<&&>.thunky) // $!.^name, (try &infix:<+>.iffy) // $!.^name, (try &infix:<==>.iffy) // $!.^name, (try &infix:<||>.iffy) // $!.^name, (try &infix:<+>.prec.^name) // $!.^name, (try &infix:<+>.prec<prec>) // $!.^name, (try &infix:<+>.prec<assoc>) // $!.^name, (try &infix:<+>.prec("prec")) // $!.^name, (try &infix:<rr>.associative) // $!.^name, (try &infix:<zz>.associative.raku) // $!.^name, (try &infix:<zz>.precedence) // $!.^name, (try &infix:<+>.reducer.name) // $!.^name, (try &infix:<,>.reducer.name) // $!.^name, (try &infix:<==>.reducer.name) // $!.^name, (try &infix:<rr>.reducer.name) // $!.^name, (try &infix:<**>.associative) // $!.^name, (try (sub () {}).precedence.raku) // $!.^name, (try ({;}).precedence.raku) // $!.^name, (try ({;}).prec.raku) // $!.^name, (try &say.prec.raku) // $!.^name, (try &say.iffy.raku) // $!.^name, (try &prefix:<->.precedence) // $!.^name, (try &postfix:<++>.precedence) // $!.^name, (try &infix:<~~>.iffy) // $!.^name, (try &infix:<nn>.associative) // $!.^name, (try &infix:<,>.associative) // $!.^name, (try &infix:<cmp>.associative) // $!.^name, (try &infix:<..>.associative) // $!.^name, (try &infix:<=>.associative) // $!.^name, (try &infix:<x>.associative) // $!.^name, (try &infix:<+>.prec.keys.sort.raku) // $!.^name, (try &infix:<+>.prec<iffy>.raku) // $!.^name, (try &infix:<==>.prec<iffy>.raku) // $!.^name, (try &infix:<+>.prec<thunky>.raku) // $!.^name, (try &infix:<&&>.prec<thunky>.raku) // $!.^name, (try &infix:<+>.prec<dba>.raku) // $!.^name, (try &infix:<+>.precedence.^name) // $!.^name, (try &infix:<mm>.reducer.name) // $!.^name, (try &infix:<rr>.prec<assoc>) // $!.^name, (try &infix:<ll>.prec<prec>) // $!.^name, (try &infix:<+>.prec("prec")) // $!.message.substr(0, 55)).join(" ") }
# rakudo 2026.08: (1+6) [1 5] (1 r (2 r 3)) ((1 z 2) z 3) 2*3! X::Syntax::NonAssociative 1 e 6 1 X::AdHoc+{X::Comp} (1 r (2 r 3)) ((1 z 2) z 3) 2*3 ((1 r 2) z 3) (1 z (2 r 3)) [(1+2) 3] t= left u= u= t@= t:= t= "" .t False True True Hash t= left X::TypeCheck::Return right "" t= METAOP_REDUCE_LEFT METAOP_REDUCE_LIST METAOP_REDUCE_CHAIN METAOP_REDUCE_RIGHT right "" "" {} {} Bool::False v= x= True non list non non right left ("assoc", "dba", "prec").Seq Any 1 Any ".t" "additive" Str METAOP_REDUCE_LEFT right t:= Type check failed for return value; expected Hash:D but
```
rakupp 4.0.1-84: differs — `is looser` is ignored (`1 ll 2 + 3` parses as `(1 ll 2) + 3` and the line died numifying `[1 2]`); none of the property methods exist.

## C. Signature

### CO-21  How a Signature prints: `.raku` and `.gist`                    D:partial R:partial V:bug
`.raku` is `:(` … `)`, `.gist` the same without the colon. Rules: a
parameter's type is printed unless it is the elided default (`Mu` for a
standalone signature, `Any` for a routine's), so `:($x)` and `:(Mu $x)`
both print `:($x)` while `:(Any $x)` keeps `Any`; an anonymous typed
scalar is `Int $` in `.raku` and `Int` in `.gist`; `Int:D`/`Int:U`
keep the smiley, `Int:_` loses it; a coercion prints its source
(`Int(Any) $x`, `Str:D(Any):D $`); a `where` is `where { ... }` even for
`where 42` or `where 1..3`, while a bare literal (`:(42)`, `:("s")`)
prints the literal; a literal default prints (`$z = 1`, `Str :$s = "d"`)
but any other default is `Code.new`; `is rw`, `is copy`, `is item`
print, `is raw` prints only on a sigiled parameter (`\x is raw` is
`\x`); `\c`, `|c`, `+a` keep their marks; `::T $x` prints with two
spaces (`::T  $x`); `--> Int` prints `:( --> Int)` and `:($x --> Int:D)`;
`;;` and `;; $x?` print; a signature constraint prints `&c:(Int $)`; a
sub-signature `@a ($f, *@r)`; nested aliases `:x(:y($z))`; `:(Mu)` is
`:($)`. Bug: two spellings do not re-parse — `Int:D(Any):D` (CO-22's
last field, `X::MultipleTypeSmiley`) and `Code.new`.
```
say do { my @s = :($x), :($x?), :($x = 1), :(:$n), :(:$n!), :(*@a), :(**@a), :(+@a), :(+a), :(&c), :(\c), :(|c), :(Int:D $x), :(Int $x), :(Int:U $x), :(@a ($f, *@r)), :($x where { $_ > 1 }), :($x where 42), :(42), :("s"), :(:n($x)), :($, $), :(Int, Str), :(*%h), :(:@a), :(::T $x), :(::T Numeric $x, T $y), :($x is rw), :($x is copy), :($x is raw), :(\x is raw), :(@x is item), :(%x is item), :($ is rw), :(Int $x = 5, Str :$s = "d"), :($a, $b?, *@c, :$d, *%e), :(--> Int), :($x --> Int:D), :(--> 42), :($x, :$y, *%_), :(Int() $x), :(Int(Str) $x), :(Int(Str) $x, Str() $y), :($x, +@r); @s.map(*.raku).join(" ") }
# rakudo 2026.08: :($x) :($x?) :($x = 1) :(:$n) :(:$n!) :(*@a) :(**@a) :(+@a) :(+a) :(&c) :(\c) :(|c) :(Int:D $x) :(Int $x) :(Int:U $x) :(@a ($f, *@r)) :($x where { ... }) :($x where { ... }) :(42) :("s") :(:n($x)) :($, $) :(Int $, Str $) :(*%h) :(:@a) :(::T  $x) :(::T Numeric $x, T $y) :($x is rw) :($x is copy) :($x is raw) :(\x) :(@x is item) :(%x is item) :($ is rw) :(Int $x = 5, Str :$s = "d") :($a, $b?, *@c, :$d, *%e) :( --> Int) :($x --> Int:D) :( --> 42) :($x, :$y, *%_) :(Int(Any) $x) :(Int(Str) $x) :(Int(Str) $x, Str(Any) $y) :($x, +@r)
say do { my @s = :(Int @a), :(Int %h), :(Str &c), :(&c:(Int)), :($a;; $b), :(;; $x?), :(Positional $p), :($a, $b, :$c is copy), :($x where Int), :(Int $x where * > 1), :(Any $x), :(Mu $x), :(Mu), :(), :($x, $y = $x), :(:$x = $*PID), :(Int:_ $x), :(Str:D()), :(@), :(%), :(&), :(*@), :(**@), :(+@), :(*%), :(|), :(:x(:$y)), :(:x(:y($z))), :($x where 1..3), :(:$n where 1..3), :(%h is copy), :(&c is raw), :(Int:D), :(Int:U), :(Int:D()); @s.map(*.raku).join(" ") }
# rakudo 2026.08: :(Int @a) :(Int %h) :(Str &c) :(&c:(Int $)) :($a;; $b) :(;; $x?) :(Positional $p) :($a, $b, :$c is copy) :($x where { ... }) :(Int $x where { ... }) :(Any $x) :($x) :($) :() :($x, $y = Code.new) :(:$x = Code.new) :(Int $x) :(Str:D(Any):D $) :(@) :(%) :(&) :(*@) :(**@) :(+@) :(*%) :(|) :(:x(:$y)) :(:x(:y($z))) :($x where { ... }) :(:$n where { ... }) :(%h is copy) :(&c is raw) :(Int:D $) :(Int:U $) :(Int:D(Any):D $)
say do { my @s = :(Str $x = "a" ~ "b"), :(:$x = 1 + 1), :(\x = 5), :(@a = [1]), :(%h = {}), :(&c = { 1 }); @s.map(*.raku).join(" ") }
# rakudo 2026.08: :(Str $x = Code.new) :(:$x = Code.new) :(\x = 5) :(@a = Code.new) :(%h = Code.new) :(&c = Code.new)
say do { my @s = :($x), :($x?), :($x = 1), :(:$n), :(:$n!), :(*@a), :(**@a), :(+@a), :(+a), :(&c), :(\c), :(|c), :(Int:D $x), :(Int $x), :(@a ($f, *@r)), :($x where { $_ > 1 }), :(42), :(:n($x)), :($, $), :(Int, Str), :(*%h); @s.map(*.gist).join(" ") }
# rakudo 2026.08: ($x) ($x?) ($x = 1) (:$n) (:$n!) (*@a) (**@a) (+@a) (+a) (&c) (\c) (|c) (Int:D $x) (Int $x) (@a ($f, *@r)) ($x where { ... }) (42) (:n($x)) ($, $) (Int, Str) (*%h)
say do { my @s = :(::T $x), :($x is rw), :($x is copy), :($x is raw), :(\x is raw), :(@x is item), :(--> Int), :(Int() $x), :(Int(Str) $x), :(&c:(Int)), :($a;; $b), :(Mu), :(), :(Str:D()), :(Int, Int $x), :(Int:D, Int:D $y), :(Any $x, Any), :(Mu $x, Mu), :(@, %, &), :(:x(:$y)); @s.map(*.gist).join(" ") }
# rakudo 2026.08: (::T  $x) ($x is rw) ($x is copy) ($x is raw) (\x) (@x is item) ( --> Int) (Int(Any) $x) (Int(Str) $x) (&c:(Int $)) ($a;; $b) ($) () (Str:D(Any):D) (Int, Int $x) (Int:D, Int:D $y) (Any $x, Any) ($x, $) (@, %, &) (:x(:$y))
```
rakupp 4.0.1-84: differs — `\c` prints `c`, `\x is raw` prints `x`, sub-signatures and the `is raw`/`is item` traits are dropped (`(@a)`, `(@x)`), a bare literal prints `($)`, `::T` prints `T`, coercions print their target only, `&c:(Int)` prints `&c`, `;;` prints `,`, `:(Mu)` prints `(Mu)`, `:x(:$y)` prints `:x($y)`, and the second line is a parse error (`:(;; $x?)`).

### CO-22  Signatures the compiler refuses, and attributive parameters   D:partial R:yes V:spec
Compile-time refusals, by type: `$!x`/`$.x` outside a class
`X::Syntax::NoSelf`; `$?x` `X::Parameter::Twigil` (`$*x` and `@*a` are
allowed); an unknown parameter trait `X::Comp::Trait::Unknown`; `is
item` on a `$` parameter `X::Comp::Trait::Invalid`, `is rw` on an
optional or `@` parameter `X::Trait::Invalid` / `X::Comp::AdHoc`; a
name twice, a type capture twice, or `:$x!, :$x` `X::Redeclaration`; two
nameds sharing an alias `X::Signature::NameClash`; a required after an
optional, a positional after a slurpy or named, a parameter after a
capture `X::Parameter::WrongOrder`; a typed or coercing slurpy
`X::Parameter::TypedSlurpy`; two types `X::Parameter::MultipleTypeConstraints`;
a trait after the default `X::Parameter::AfterDefault`; a default of the
wrong type `X::Parameter::Default::TypeCheck` (`Int:D $x = Int` passes);
an unknown type `X::Parameter::InvalidType`. Allowed: `($a, |c)`, two
positional slurpies, `*@a, *%h` in either order, `$x?, :$y!`, `::T $x, T
$y, T @z`, `\x is item`, `Int ::T $x` (printed `::T Int $x`). In a
method, `$.x` binds the accessor and `$!x` the attribute (writing it:
`method m($!x)` sets the attribute); `BUILD(:$!x)` has name `$!x`,
twigil `!`, usage-name `x`, sigil `$`.
```
say do { ((try EVAL ':($!x).raku') // $!.^name, (try EVAL ':($.x).raku') // $!.^name, (try EVAL ':(@*a).raku') // $!.^name, (try EVAL ':($x is default(1)).raku') // $!.^name, (try EVAL ':($*x).raku') // $!.^name, (try EVAL ':($?x).raku') // $!.^name, (try EVAL 'class A { has $.x; method m($.x) { } }; A.^lookup("m").signature.raku') // $!.^name, (try EVAL 'class B { has $!x; method m($!x) { } }; B.^lookup("m").signature.raku') // $!.^name, (try EVAL 'class B2 { has $!x; method m($!x) { $!x }; method get { $!x } }; my $b = B2.new; $b.m(5); $b.get') // $!.^name, (try EVAL 'class B3 { has $.x; submethod BUILD(:$!x) { } }; B3.^lookup("BUILD").signature.params[1].name ~ B3.^lookup("BUILD").signature.params[1].twigil ~ B3.^lookup("BUILD").signature.params[1].usage-name ~ B3.^lookup("BUILD").signature.params[1].sigil') // $!.^name, (try EVAL 'class B4 { has @.x; submethod BUILD(:@!x) { } }; B4.^lookup("BUILD").signature.raku') // $!.^name, (try EVAL ':(:$x!, :$x).raku') // $!.^name, (try EVAL ':($x, $x).raku') // $!.^name, (try EVAL ':(:$x, :x($y)).raku') // $!.^name, (try EVAL ':(*@a, $b).raku') // $!.^name, (try EVAL ':($a?, $b).raku') // $!.^name, (try EVAL ':(:$a, $b).raku') // $!.^name, (try EVAL ':(Int *@a).raku') // $!.^name, (try EVAL ':(Int *%h).raku') // $!.^name, (try EVAL ':(Int Str $x).raku') // $!.^name, (try EVAL ':($x = 1 is copy).raku') // $!.^name, (try EVAL ':(Int $x = "s").raku') // $!.^name, (try EVAL ':(|c, $x).raku') // $!.^name, (try EVAL ':($a, |c).raku') // $!.^name, (try EVAL ':(*@a, *@b).raku') // $!.^name, (try EVAL ':(*@a, *%h).raku') // $!.^name, (try EVAL ':(*%h, *@a).raku') // $!.^name, (try EVAL ':($x?, :$y!).raku') // $!.^name, (try EVAL ':(::T $x, T $y, T @z).raku') // $!.^name, (try EVAL ':(Nonesuch $x).raku') // $!.^name, (try EVAL ':($x is item).raku') // $!.^name, (try EVAL ':(\x is item).raku') // $!.^name, (try EVAL ':($x is rw is copy).raku') // $!.^name, (try EVAL ':(Int() *@a).raku') // $!.^name, (try EVAL ':(:$x is rw = 1).raku') // $!.^name, (try EVAL ':(Int:D $x is rw = 5).raku') // $!.^name, (try EVAL ':(Int:D $x = Int).raku') // $!.^name, (try EVAL ':($a, $b, $a).raku') // $!.^name, (try EVAL ':(:$a, :$a).raku') // $!.^name, (try EVAL ':(::T $x, ::T $y).raku') // $!.^name, (try EVAL ':(::T).raku') // $!.^name, (try EVAL ':(::T $).raku') // $!.^name, (try EVAL ':(Int ::T $x).raku') // $!.^name, (try EVAL ':(::T Int $x).raku') // $!.^name, (try EVAL ':(@x is rw).raku') // $!.^name, (try EVAL ':(::T  $x).raku') // $!.^name, (try EVAL ':(Int:D(Any):D $x).raku') // $!.^name).join(" ") }
# rakudo 2026.08: X::Syntax::NoSelf X::Syntax::NoSelf :(@*a) X::Comp::Trait::Unknown :($*x) X::Parameter::Twigil :(A $:: $.x, *%_) :(B $:: $!x, *%_) 5 $!x!x$ :(B4 $:: :@!x, *%_) X::Redeclaration X::Redeclaration X::Signature::NameClash X::Parameter::WrongOrder X::Parameter::WrongOrder X::Parameter::WrongOrder X::Parameter::TypedSlurpy X::Parameter::TypedSlurpy X::Parameter::MultipleTypeConstraints X::Parameter::AfterDefault X::Parameter::Default::TypeCheck X::Parameter::WrongOrder :($a, |c) :(*@a, *@b) :(*@a, *%h) :(*%h, *@a) :($x?, :$y!) :(::T  $x, T $y, T @z) X::Parameter::InvalidType X::Comp::Trait::Invalid :(\x is item) :($x is rw) X::Parameter::TypedSlurpy X::Trait::Invalid+{X::Comp} X::Trait::Invalid+{X::Comp} :(Int:D $x = Int) X::Redeclaration X::Redeclaration X::Redeclaration :(::T  $) :(::T  $) :(::T Int $x) :(::T Int $x) X::Comp::AdHoc :(::T  $x) X::MultipleTypeSmiley
```
rakupp 4.0.1-84: differs — nearly all of these compile: `$!x`/`$.x` outside a class, `$?x`, duplicate names, `is default(1)`, `\x is item` (as `x`), `$x is item`, `is rw is copy` (as `is copy`), `Int:D(Any):D` (as `D $x`), and `Nonesuch $x`; the wrong-order, typed-slurpy and two-type cases become `:()` (the parameters silently dropped); `BUILD(:$!x)` has no `.twigil`.

### CO-23  Binding a Capture: Signature.ACCEPTS(Capture) and `~~`         D:yes R:partial V:quirk
`\(…) ~~ :(…)` (and `.ACCEPTS`) answers whether the Capture would bind:
the positional count must fit arity..count, a required named must be
present, an unexpected named refuses, types, `:D`/`:U`, `where`,
`is rw` (a literal fails, a container passes), sub-signatures and
`|c` are all checked, `Any` and `Nil` fail an `Int` parameter, `\()`
binds `:()` and `:($a?)` while `\(1)` does not bind `:()`. A
non-Capture topic is converted with `.Capture` first — a List, Array,
Set or Hash bind their elements — and a topic whose `.Capture` throws
(an Int) simply answers False. Quirk: a coercion parameter accepts an
argument its coercion will reject (`\("x") ~~ :(Int() $a)` True) because
the failed coercion binds as a Failure (CO-30). `:($x).Capture` is
`X::Cannot::Capture`; `.arity`/`.count`/`.params`/`.returns` are as for
Code (CO-01); `.WHICH` is an `ObjAt`; `:().elems` is 1.
```
say do { (:($x, $y?).arity, :($x, $y?).count, :(*@a).count, :(:$n).count, :(|c).count, :(|c).arity, :(**@a).count, :(+@a).count, :(--> Int).returns.^name, :($x).returns.^name, :($x).returns.raku, :(--> 42).returns.raku, :($x).params.elems, :($x).params.^name, (try :($x).Capture) // $!.^name, (\(1, 2) ~~ :($a, $b)), (\(1) ~~ :($a, $b)), (\(1, 2, 3) ~~ :($a, $b)), (\(1, 2, 3) ~~ :($a, *@b)), (\(1, :n) ~~ :($a, :$n)), (\(1, :n) ~~ :($a)), (\(1) ~~ :(Int $a)), (\("s") ~~ :(Int $a)), (\(1) ~~ :($a where * > 5)), (\(9) ~~ :($a where * > 5)), (\(:a(1)) ~~ :(:$a!)), (\() ~~ :(:$a!)), (\() ~~ :(:$a)), ((1, 2) ~~ :($a, $b)), (<a b c d> ~~ :(Int $a)), (try (42 ~~ :(Int))) // $!.^name, (set(<a b>) ~~ :(:$a, :$b)), ({ :a(1) } ~~ :(:$a)), ([1, 2] ~~ :($a, $b)), (:($a, $b).ACCEPTS(\(1, 2))), :($x).ACCEPTS(\(1)).^name, (\(1, 2) ~~ :($a, $b, $c?)), (\(1) ~~ :($a, $b = 2)), (\(1, 2, 3) ~~ :($a, **@b)), (\(1, 2) ~~ :(+@a)), (\(:x(1)) ~~ :(*%h)), (\(1, :x(1)) ~~ :($a, *%h)), (\(1, :x(1)) ~~ :($a)), (\(Int) ~~ :(Int:D $a)), (\(1) ~~ :(Int:U $a)), (\(1) ~~ :(\a)), (\(1, 2) ~~ :(|c)), (\("s") ~~ :(Int() $a)), (\("x") ~~ :(Int() $a)), (\([1, 2]) ~~ :(@a ($x, $y))), (\([1]) ~~ :(@a ($x, $y))), (\(1) ~~ :($a where Int)), (\(1) ~~ :($a is rw)), (\(my $v = 1) ~~ :($a is rw)), (\(Any) ~~ :(Int $a)), (\(Nil) ~~ :(Int $a)), (\(1) ~~ :()), (\() ~~ :()), (\() ~~ :($a?)), (\(1, 2) ~~ :($a, $b, :$c!)), (\(1, :c) ~~ :($a, :$c!)), (:($x).WHICH.^name), (:($x) ~~ Signature), (:($x) ~~ Callable), (:($x).defined), (:($x).Bool), (:().Bool), (:().elems)).join(" ") }
# rakudo 2026.08: 1 2 Inf 0 Inf 0 Inf Inf Int Mu Mu 42 1 List X::Cannot::Capture True False False True True False True False False True True False True True False False True True True True Bool True True True True True True False False False True True True True True False True False True False False False True True False True ObjAt True False True True True 1
```
rakupp 4.0.1-84: differs in 15 of 68 fields — an unexpected named, a failed `where`, a missing required named, a literal for `is rw`, a too-short sub-signature and the `:D`/`:U` smileys all bind (True), a Set or Hash topic does not, `\() ~~ :(:$a)` is False, `:(--> 42).returns` is `Mu`, `.WHICH` is a `ValueObjAt`, and `:().elems` is 4.

### CO-24  Signature against Signature: `~~` and `eqv`                    D:partial R:partial V:spec
`:(A) ~~ :(B)` is True when everything B accepts, A accepts: each of
A's positionals must be accepted by the corresponding B parameter
(nominal type only, so `Int $n ~~ Any` True and `Any ~~ Int` False;
`Int:D ~~ Int` True, `Int ~~ Int:D` False), a `where` or literal on
either side never matches (`:(42) ~~ :($ where 42)` False, but `:(42) ~~
:(42)` True), B may have extra optionals but not extra requireds, a
slurpy on the right absorbs (`:($a) ~~ :(*@a)` True), `|c` on the right
absorbs everything, nameds match by name (`:(:$a!) ~~ :(:$a)` True, the
reverse False; `:(:$a) ~~ :(*%h)` True), `@a` and `Positional $b` are
interchangeable, `&c ~~ Callable $b`, `\a`/`$a` interchangeable, `is
rw` on the left only, `is copy` ignored, `is item` on the left only, and
the return constraints must be identical (`:(--> Int) ~~ :()` False).
`eqv` is stricter: same types (not `Int` vs `Int:D`), same flags
(optional, rw, raw), same names (`:$a`/`:$b` differ, `:$a`/`:a($b)` are
equal), same returns, and a `where` is never `eqv` to anything; `===`
on two literals is False.
```
say do { ((:($a, $b) ~~ :($foo, $bar, $baz?)), (:($foo, $bar, $baz?) ~~ :($a, $b)), (:(Int $n) ~~ :(Str)), (:(Int $n) ~~ :(Any)), (:(Any $n) ~~ :(Int)), (:(42) ~~ :($ where 42)), (:(42) ~~ :(42)), (:($x) ~~ :($y)), (:($x --> Int) ~~ :($y)), (:($x --> Int) ~~ :($y --> Int)), (:(Int $x) eqv :(Int $y)), (:(Int $x) eqv :(Int $x)), (:($x) eqv :($x?)), (:(:$a) eqv :(:$a)), (:(:$a) eqv :(:$b)), (:(--> Int) eqv :(--> Str)), (:($x where 1) eqv :($x where 1)), (:($x) eqv :($x --> Int)), (:(Int $x) eqv :(Int:D $x)), (:($a, $b) === :($a, $b)), (:(*@a) ~~ :($a)), (:($a) ~~ :(*@a)), (:(:$a) ~~ :($a)), (:($a) ~~ :(:$a)), (:(:$a) ~~ :(*%h)), (:(*%h) ~~ :(:$a)), (:(Int $a, Str $b) ~~ :(Any $a, Any $b)), (:(|c) ~~ :($a)), (:($a) ~~ :(|c)), (:($a, $b) ~~ :($a)), (:($a) ~~ :($a, $b)), (:(:$a!) ~~ :(:$a)), (:(:$a) ~~ :(:$a!)), (:($x) eqv :(\x)), (:(Int $x) ~~ :(Int $x)), (:($a where Int) ~~ :($b where Int)), (:(Int $a) ~~ :($b where Int)), (:(Int $a) ~~ :(Int:D $b)), (:(Int:D $a) ~~ :(Int $b)), (:($a is rw) ~~ :($b)), (:($a) ~~ :($b is rw)), (:($a is copy) ~~ :($b)), (:(@a) ~~ :($b)), (:($a) ~~ :(@b)), (:(@a) ~~ :(@b)), (:(@a) ~~ :(Positional $b)), (:(Positional $a) ~~ :(@b)), (:(&c) ~~ :($b)), (:(&c) ~~ :(Callable $b)), (:(\a) ~~ :($b)), (:($a) ~~ :(\b)), (:($a, :$n) ~~ :($b)), (:($a) ~~ :($b, :$n)), (:($a) ~~ :($b, :$n!)), (:(:$n!) ~~ :($b, :$n)), (:(--> Int) ~~ :(--> Any)), (:(--> Int) ~~ :()), (:(@a is item) ~~ :(@b)), (:(@a) ~~ :(@b is item)), (:(@a is item) eqv :(@b is item)), (:(@a is item) eqv :(@a)), (:(:$a) eqv :(:a($b))), (:(:$a) ~~ :(:a($b))), (:(*@a, *%h) eqv :(*@b, *%i)), (:(::T $x) eqv :(::U $y)), (:(::T $x) ~~ :(::U $y))).join(" ") }
# rakudo 2026.08: True False False True False False True True False True True True False True False False False False False False False True False False True False True False True False False True False False True False False False True True False True True False True True True True True True True False True False False False False True False True False True True True True True
```
rakupp 4.0.1-84: differs in 22 of 66 fields — the nominal check is symmetric (`:(Int $n) ~~ :(Str)` True), `where`s and `:D` compare equal, `eqv` on `Int $x` vs `Int $y` is False, and the returns/nameds rules are mostly True where Rakudo says False.

### CO-25  Signature.new                                                 D:partial R:no V:bug
`Signature.new(:params, :returns, :arity, :count)` builds a signature
whose `.raku` shows every type (`Any $x`) and an explicit `--> Mu`; with
no arguments arity and count are 0 and the raku `:( --> Mu)`; `arity`
defaults to the number of parameters (a named counts), `count` to the
arity even for a slurpy (`Inf` must be passed, as a `Num`); `params`,
`returns`, `gist` answer. Two such signatures are not `eqv` to each
other nor to a literal (`:($x)`'s parameters are typed `Mu`), and `~~`
against a literal is False in both directions. Quirk: an Int `count`
or a non-Int `arity` silently falls through to `Mu.new`, leaving
`.count` the `Num` type object, `.raku` `:()` and arity 0. Bug: a
runtime-built Signature cannot bind a Capture — `\(1) ~~ Signature.new(
…)` and even `\() ~~ Signature.new` throw `X::AdHoc`.
```
say do { (Signature.new(params => (Parameter.new(name => '$x'),)).raku, Signature.new(params => (Parameter.new(name => '$x'), Parameter.new(name => '$y?')), returns => Int).raku, Signature.new.arity, Signature.new.count, Signature.new.raku, Signature.new(params => (Parameter.new(name => '$x'),)).arity, Signature.new(params => (Parameter.new(name => '*@a'),), count => Inf).count, Signature.new(params => (Parameter.new(name => '*@a'),)).count, Signature.new(params => (Parameter.new(name => '*@a'),), arity => 0, count => Inf).raku, Signature.new(params => (Parameter.new(name => '$x'),)).params[0].name, Signature.new(params => (Parameter.new(name => '$x', type => Int),)).gist, Signature.new(returns => Str).raku, Signature.new(returns => Str).returns.^name, Signature.new.returns.^name, Signature.new.params.raku, (Signature.new eqv Signature.new), (Signature.new(params => (Parameter.new(name => '$x'),)) eqv :($x)), Signature.new(params => (Parameter.new(name => ':$n'),)).raku, Signature.new(params => (Parameter.new(name => ':$n'),)).count, (:($x) eqv Signature.new(params => (Parameter.new(name => '$x', type => Any),))), Signature.new(params => (Parameter.new(name => '$x', type => Any),)).raku, (Signature.new(params => (Parameter.new(name => '$x', type => Int),)) eqv :(Int $x)), (Signature.new(params => (Parameter.new(name => '$x', type => Int),)) ~~ :(Int $x)), (:(Int $x) ~~ Signature.new(params => (Parameter.new(name => '$x', type => Int),))), Signature.new(params => (Parameter.new(name => '$x', type => Int),), arity => 1, count => 1e0).raku, (try Signature.new(params => (), count => 1).count.raku) // $!.^name, (try Signature.new(params => (), count => 1).raku) // $!.^name, (try Signature.new(params => (), arity => 1.0).arity.raku) // $!.^name, (try Signature.new(params => (), count => 1).arity.raku) // $!.^name, (try Signature.new(params => (), count => 1).^name) // $!.^name, (try (\(1) ~~ Signature.new(params => (Parameter.new(name => '$x'),)))) // $!.^name, (try (\() ~~ Signature.new)) // $!.^name, (try (\(1) ~~ Signature.new)) // $!.^name, (try (\("s") ~~ Signature.new(params => (Parameter.new(name => '$x', type => Int),)))) // $!.^name).join(" ") }
# rakudo 2026.08: :(Any $x --> Mu) :(Any $x, Any $y? --> Int) 0 0 :( --> Mu) 1 Inf 1 :(*@a --> Mu) $x (Int $x --> Mu) :( --> Str) Str Mu () False False :(Any :$n --> Mu) 1 False :(Any $x --> Mu) False False False :(Int $x --> Mu) Num :() 0 0 Signature X::AdHoc X::AdHoc X::AdHoc X::AdHoc
```
rakupp 4.0.1-84: differs — the raku shows no `Any`, a slurpy's count is 1 not `Inf` but `eqv`/`~~` against literals hold (True), an Int `count` gives count 0 and `:( --> Mu)`, and Capture binding works (True, True, False, False).

### CO-26  Parameter.new                                                 D:partial R:no V:bug
`Parameter.new(:name, :type, :default, :where, :is-copy, :is-raw,
:is-rw, :is-item, :named, :optional, :mandatory, :invocant,
:multi-invocant, :sub-signature)` reads sigil, twigil, `:` prefix,
`*`/`**` slurpy marks, `|`/`\` and a trailing `?`/`!` from the name
(`':n($x)'` gives alias `n`), prints the result as CO-21 does, and
makes a default imply optional (a literal or a block default both
answer a `Block` from `.default`, called for the value). A type
instance is replaced by its type; a type given as `Int:D` keeps the
smiley in `.type.^name` but `.modifier` stays `""`; `Int()` prints
`Int(Any) $x`; `:is-copy` with `:is-rw` prints `is rw`; `:named` on a
`$x` name makes `:$x`; `:invocant` prints `$x:`; a name without sigil is
accepted and prints doubled (`xx`); a typed slurpy is `X::AdHoc`, a
non-Str name `X::TypeCheck::Binding::Parameter`. Such a parameter is
`eqv` to another built the same way but not to `:($x).params[0]`. Bug:
the `+@a` and `+a` prefixes throw `X::OutOfRange` from the constructor.
```
say do { (Parameter.new(name => '$x').raku, Parameter.new(name => ':$n!').raku, Parameter.new(name => '*@a').raku, Parameter.new(name => '**@a').raku, Parameter.new(name => '|c').raku, Parameter.new(name => '\c').raku, Parameter.new(name => '$x', type => Int).raku, Parameter.new(name => '$x', type => Int, :is-rw).raku, Parameter.new(name => '$x', default => 42).raku, Parameter.new(name => '$x?').optional, Parameter.new(name => ':$n').optional, Parameter.new(name => ':$n!').optional, Parameter.new(name => 'x').raku, Parameter.new(name => ':n($x)').named_names.raku, Parameter.new(name => '$x', where => { $_ > 1 }).raku, Parameter.new(:is-copy).raku, Parameter.new.raku, Parameter.new.name.raku, Parameter.new.type.^name, (try Parameter.new(name => '*@a', type => Int).raku) // $!.^name, Parameter.new(name => '$x', :named).raku, Parameter.new(name => '$x', :optional).raku, Parameter.new(name => '$x', :named, :mandatory).raku, Parameter.new(name => '$x', :invocant).raku, Parameter.new(name => '$x', :!multi-invocant).raku, Parameter.new(name => '$x', :is-raw).raku, Parameter.new(name => '@x', :is-item).raku, Parameter.new(name => '@x', type => Int).raku, Parameter.new(name => '@x', :sub-signature(:($a))).raku, Parameter.new(name => '$x', type => Int:D).raku, Parameter.new(name => '$x', type => Int:D).modifier.raku, Parameter.new(name => '$x', type => 42).type.^name, (try Parameter.new(name => '$x)').raku) // $!.^name, Parameter.new(name => '$!x').twigil, Parameter.new(name => '$.x').twigil, Parameter.new(name => '$*x').twigil, Parameter.new(name => '$x', where => 42).raku, Parameter.new(name => '$x', where => 42).constraint_list.raku, Parameter.new(name => ':$n', default => 1).raku, Parameter.new(name => '$x', :optional, default => 1).raku, (try Parameter.new(name => '+@a').raku) // $!.^name, (try Parameter.new(name => '+a').raku) // $!.^name, Parameter.new(name => '$x', default => 42).default.^name, Parameter.new(name => '$x', default => { 42 }).default.^name, Parameter.new(name => '$x', default => 42).default.(), Parameter.new(name => '$x', default => { 42 }).default.(), Parameter.new(name => '$x', default => 42).optional, Parameter.new(name => ':$n').named_names.raku, Parameter.new(name => ':$n').named, Parameter.new(name => '$x', :named).named_names.raku, Parameter.new(name => '$x').sigil, Parameter.new(name => '\c').sigil, Parameter.new(name => '\c').name, Parameter.new(name => '|c').sigil, Parameter.new(name => '|c').name, Parameter.new(name => '|c').capture, Parameter.new(name => '*@a').slurpy, Parameter.new(name => '*@a').prefix, Parameter.new(name => '**@a').prefix, Parameter.new(name => '*%h').named, Parameter.new(name => '*%h').slurpy, Parameter.new(name => '$x', type => Int:D).type.^name, Parameter.new(name => '$x', type => Int()).raku, Parameter.new(name => '$x', :is-copy, :is-rw).raku, Parameter.new(name => '$x').WHICH.^name, (Parameter.new(name => '$x') eqv Parameter.new(name => '$x')), (Parameter.new(name => '$x') eqv :($x).params[0]), (Parameter.new(name => '$x', type => Any) eqv :($x).params[0]), Parameter.new(name => '$x', type => Any).raku, (try Parameter.new(name => 42)) // $!.^name).join(" ") }
# rakudo 2026.08: $x :$n! *@a **@a |c \c Int $x Int $x is rw $x = 42 True True False xx ("n",) $x where { ... } $ is copy $ "" Any X::AdHoc :$x $x? :$x! $x: $x $x is raw @x is item @x @x ($a) Int:D $x "" Int X::AdHoc ! . * $x where { ... } (42,) :$n = 1 $x = 1 X::OutOfRange X::OutOfRange Block Block 42 42 True ("n",) True ("x",) $ \ c | c True True * ** True True Int:D Int(Any) $x $x is rw ObjAt True False False $x X::TypeCheck::Binding::Parameter
```
rakupp 4.0.1-84: differs — the line died at `.twigil` (`X::Method::NotFound`).

## D. Parameter

### CO-27  What a Parameter answers                                      D:yes R:yes V:spec
Per parameter: `.name` is the variable with sigil and twigil (`$x`,
`@a`, `%!x`), but `d` for `+d`, `f` for `\f`, `g` for `|g` and `""` for
an anonymous one; `.usage-name` strips sigil and twigil; `.sigil` is
`$ @ % &`, `\` for a sigilless or `+d` parameter, `|` for a capture;
`.twigil` `""`, `!` or `.`; `.prefix` `*`, `**`, `+` or `""`; `.suffix`
`?` for an optional positional without default, `!` for a required
named, else `""`; `.modifier` `:D`, `:U` or `""`; `.type` `Mu` when
untyped, `Positional`/`Associative`/`Callable` for `@ % &`, `Int(Any)` for
`Int()`, `Positional[Int]` for `Int @x11`; `.named` for a named or
`*%`, `.positional` for a positional that is not a slurpy (a slurpy
positional and `|g` are neither); `.optional` for `?`, a default or a
plain named; `.slurpy`; `.capture`; `.raw` for `\`, `|`, `+d` and `is
raw`; `.rw`; `.copy`; `.readonly` False for raw, rw or copy;
`.invocant`; `.multi-invocant` True; `.named_names` (`("ali",)` for
`:ali($p)`, innermost first for a nested alias); `.type_captures`
`("T",)`; `.default` a `Block` for a literal default (`$z = 1`, `:$w =
5`) and the `Code` type object both when there is none and for a
non-literal default such as `$x7 = $*PID` (whose `.optional` is still
True); `.sub_signature` the `Signature` type object when none;
`.constraint_list.elems` 1 for a `where`; `.constraints` a `Junction`;
`.is-item`, `.onearg` (`+d`), `.untyped` (Mu without constraints). The
`.raku` of each is on the fourth line.
```
say do { my @p = (:($x, $y?, $z = 1, :$n, :$m!), :(*@a), :(**@b), :(+@c), :(+d), :(&e), :(\f), :(|g)).map(*.params).flat; @p.map({ (.name, .usage-name, .sigil, .twigil, .prefix, .suffix, .modifier, .type.^name, .named ?? "N" !! "P", .positional ?? "p" !! "-", .optional ?? "o" !! "r", .slurpy ?? "s" !! "-", .capture ?? "c" !! "-", .raw ?? "R" !! "-", .rw ?? "W" !! "-", .copy ?? "C" !! "-", .readonly ?? "ro" !! "rw", .invocant ?? "i" !! "-", .multi-invocant ?? "m" !! "-", .named_names.raku, .type_captures.raku, .default.^name, .sub_signature.^name, .constraint_list.elems, .constraints.^name).join(",") }).join(" | ") }
# rakudo 2026.08: $x,x,$,,,,,Mu,P,p,r,-,-,-,-,-,ro,-,m,(),(),Code,Signature,0,Junction | $y,y,$,,,?,,Mu,P,p,o,-,-,-,-,-,ro,-,m,(),(),Code,Signature,0,Junction | $z,z,$,,,,,Mu,P,p,o,-,-,-,-,-,ro,-,m,(),(),Block,Signature,0,Junction | $n,n,$,,,,,Mu,N,-,o,-,-,-,-,-,ro,-,m,("n",),(),Code,Signature,0,Junction | $m,m,$,,,!,,Mu,N,-,r,-,-,-,-,-,ro,-,m,("m",),(),Code,Signature,0,Junction | @a,a,@,,*,,,Positional,P,-,r,s,-,-,-,-,ro,-,m,(),(),Code,Signature,0,Junction | @b,b,@,,**,,,Positional,P,-,r,s,-,-,-,-,ro,-,m,(),(),Code,Signature,0,Junction | @c,c,@,,+,,,Positional,P,-,r,s,-,-,-,-,ro,-,m,(),(),Code,Signature,0,Junction | d,d,\,,+,,,Mu,P,-,r,s,-,R,-,-,rw,-,m,(),(),Code,Signature,0,Junction | &e,e,&,,,,,Callable,P,p,r,-,-,-,-,-,ro,-,m,(),(),Code,Signature,0,Junction | f,f,\,,,,,Mu,P,p,r,-,-,R,-,-,rw,-,m,(),(),Code,Signature,0,Junction | g,g,|,,,,,Mu,P,-,r,-,c,R,-,-,rw,-,m,(),(),Code,Signature,0,Junction
say do { my @p = (:(Int:D $h), :(Str $i is rw), :($j is copy), :($k is raw), :(@l is item), :(Int() $o), :(:ali($p)), :(:$q where * > 1), :(::T $r), :(@s ($t, *@u)), :($), :(Int)).map(*.params).flat; @p.map({ (.name, .usage-name, .sigil, .twigil, .prefix, .suffix, .modifier, .type.^name, .named ?? "N" !! "P", .positional ?? "p" !! "-", .optional ?? "o" !! "r", .slurpy ?? "s" !! "-", .capture ?? "c" !! "-", .raw ?? "R" !! "-", .rw ?? "W" !! "-", .copy ?? "C" !! "-", .readonly ?? "ro" !! "rw", .invocant ?? "i" !! "-", .multi-invocant ?? "m" !! "-", .named_names.raku, .type_captures.raku, .default.^name, .sub_signature.^name, .constraint_list.elems, .constraints.^name).join(",") }).join(" | ") }
# rakudo 2026.08: $h,h,$,,,,:D,Int,P,p,r,-,-,-,-,-,ro,-,m,(),(),Code,Signature,0,Junction | $i,i,$,,,,,Str,P,p,r,-,-,-,W,-,rw,-,m,(),(),Code,Signature,0,Junction | $j,j,$,,,,,Mu,P,p,r,-,-,-,-,C,rw,-,m,(),(),Code,Signature,0,Junction | $k,k,$,,,,,Mu,P,p,r,-,-,R,-,-,rw,-,m,(),(),Code,Signature,0,Junction | @l,l,@,,,,,Positional,P,p,r,-,-,-,-,-,ro,-,m,(),(),Code,Signature,0,Junction | $o,o,$,,,,,Int(Any),P,p,r,-,-,-,-,-,ro,-,m,(),(),Code,Signature,0,Junction | $p,p,$,,,,,Mu,N,-,o,-,-,-,-,-,ro,-,m,("ali",),(),Code,Signature,0,Junction | $q,q,$,,,,,Mu,N,-,o,-,-,-,-,-,ro,-,m,("q",),(),Code,Signature,1,Junction | $r,r,$,,,,,Mu,P,p,r,-,-,-,-,-,ro,-,m,(),("T",),Code,Signature,0,Junction | @s,s,@,,,,,Positional,P,p,r,-,-,-,-,-,ro,-,m,(),(),Code,Signature,0,Junction | ,,$,,,,,Mu,P,p,r,-,-,-,-,-,ro,-,m,(),(),Code,Signature,0,Junction | ,,$,,,,,Int,P,p,r,-,-,-,-,-,ro,-,m,(),(),Code,Signature,0,Junction
say do { my @p = (:(*%v), :(:$w = 5), :($x3 where 1..3), :(Int(Str) $x4), :($x5 where { $_ > 1 }), :(Int:U $x6), :($x7 = $*PID), :(@x8), :(%x9), :(&x10), :(Int @x11), :(:$x12!), :(%x13 is item), :(:x14(:$y14))).map(*.params).flat; @p.map({ (.name, .usage-name, .sigil, .twigil, .prefix, .suffix, .modifier, .type.^name, .named ?? "N" !! "P", .positional ?? "p" !! "-", .optional ?? "o" !! "r", .slurpy ?? "s" !! "-", .capture ?? "c" !! "-", .raw ?? "R" !! "-", .rw ?? "W" !! "-", .copy ?? "C" !! "-", .readonly ?? "ro" !! "rw", .invocant ?? "i" !! "-", .multi-invocant ?? "m" !! "-", .named_names.raku, .type_captures.raku, .default.^name, .sub_signature.^name, .constraint_list.elems, .constraints.^name).join(",") }).join(" | ") }
# rakudo 2026.08: %v,v,%,,*,,,Associative,N,-,r,s,-,-,-,-,ro,-,m,(),(),Code,Signature,0,Junction | $w,w,$,,,,,Mu,N,-,o,-,-,-,-,-,ro,-,m,("w",),(),Block,Signature,0,Junction | $x3,x3,$,,,,,Mu,P,p,r,-,-,-,-,-,ro,-,m,(),(),Code,Signature,1,Junction | $x4,x4,$,,,,,Int(Str),P,p,r,-,-,-,-,-,ro,-,m,(),(),Code,Signature,0,Junction | $x5,x5,$,,,,,Mu,P,p,r,-,-,-,-,-,ro,-,m,(),(),Code,Signature,1,Junction | $x6,x6,$,,,,:U,Int,P,p,r,-,-,-,-,-,ro,-,m,(),(),Code,Signature,0,Junction | $x7,x7,$,,,,,Mu,P,p,o,-,-,-,-,-,ro,-,m,(),(),Code,Signature,0,Junction | @x8,x8,@,,,,,Positional,P,p,r,-,-,-,-,-,ro,-,m,(),(),Code,Signature,0,Junction | %x9,x9,%,,,,,Associative,P,p,r,-,-,-,-,-,ro,-,m,(),(),Code,Signature,0,Junction | &x10,x10,&,,,,,Callable,P,p,r,-,-,-,-,-,ro,-,m,(),(),Code,Signature,0,Junction | @x11,x11,@,,,,,Positional[Int],P,p,r,-,-,-,-,-,ro,-,m,(),(),Code,Signature,0,Junction | $x12,x12,$,,,!,,Mu,N,-,r,-,-,-,-,-,ro,-,m,("x12",),(),Code,Signature,0,Junction | %x13,x13,%,,,,,Associative,P,p,r,-,-,-,-,-,ro,-,m,(),(),Code,Signature,0,Junction | $y14,y14,$,,,,,Mu,N,-,o,-,-,-,-,-,ro,-,m,("y14", "x14"),(),Code,Signature,0,Junction
say do { my @p = (:($x, $y?, $z = 1, :$n, :$m!), :(*@a), :(**@b), :(+@c), :(+d), :(&e), :(\f), :(|g), :(Int:D $h), :(Str $i is rw), :($j is copy), :($k is raw), :(@l is item), :(Int() $o), :(:ali($p)), :(:$q where * > 1), :(::T $r), :(@s ($t, *@u)), :($), :(Int), :(*%v), :(:$w = 5), :($x3 where 1..3), :(Int(Str) $x4), :($x5 where { $_ > 1 }), :(Int:U $x6), :($x7 = $*PID), :(@x8), :(%x9), :(&x10), :(Int @x11), :(:$x12!), :(%x13 is item), :(:x14(:$y14))).map(*.params).flat; @p.map(*.raku).join(" | ") }
# rakudo 2026.08: Mu $x | Mu $y? | Mu $z = 1 | Mu :$n | Mu :$m! | *@a | **@b | +@c | Mu +d | &e | Mu \f | Mu |g | Int:D $h | Str $i is rw | Mu $j is copy | Mu $k is raw | @l is item | Int(Any) $o | Mu :ali($p) | Mu :$q where { ... } | ::T Mu $r | @s ($t, *@u) | Mu $ | Int $ | *%v | Mu :$w = 5 | Mu $x3 where { ... } | Int(Str) $x4 | Mu $x5 where { ... } | Int:U $x6 | Mu $x7 = Code.new | @x8 | %x9 | &x10 | Int @x11 | Mu :$x12! | %x13 is item | Mu :x14(:$y14)
say do { my @p = (:(@l is item), :(%x13 is item), :(+d), :(\f), :(|g), :($), :(*@a)).map(*.params).flat; @p.map({ ((try .is-item) // "NA", (try .onearg) // "NA", (try .untyped) // "NA").join(",") }).join(" | ") }
# rakudo 2026.08: True,False,False | True,False,False | False,True,True | False,False,True | False,False,True | False,False,True | False,False,False
```
rakupp 4.0.1-84: differs — `.twigil` is missing (both matrix lines died there), a parameter's `.raku` is a hash dump, and `is-item`, `onearg`, `untyped` are missing.

### CO-28  Parameter.ACCEPTS, eqv, default, constraints, sub_signature    D:partial R:partial V:quirk
`$p ~~ $q` (Parameter against Parameter) is True when `$q` is at
least as specific: same or narrower nominal type (`Int ~~ Any` True),
`:D` on the right only if on the left, `is rw` on the left only,
optional/slurpy on the right only if on the left, same named names
(a right-side alias set must be a subset), equal sub-signatures, and
constraints compare only when both are literal values — a `where`
block on either side is never accepted, even against itself. `eqv`
needs identical type, flags, named names and, for constraints, equal
literals; `$x` is not `eqv` `\x`. `.default` is the `Code` type object
when none, else a `Block` returning the value (`{ 1 + 1 }` computed at
each call). `.named_names` lists aliases outermost first (`:a(:b($c))`
gives `("b", "a")`, name `$c`). Quirk: `where 1..3` and `where 42`
are stored as a wrapping `Block`, so two parameters written `where 42`
neither `~~` nor `eqv` each other, while the bare literal `:(42)` stores
`42` and does. `.sub_signature` is `:($b)` for `@a ($b)` and the
`Signature` type object for a signature constraint, which lives in
`.signature_constraint` (`:(Int $)`); `.coerce_type` is the target
(`Mu` when not coercive), `.nominal_type` the base type, `.coercive` 1
or 0; `.Str` is the default object form `Parameter<…>`, `.gist` the
`.raku`.
```
say do { sub P($s) { $s.params[0] }; (P(:(Int $x)) ~~ P(:(Int $y)), P(:(Int $x)) ~~ P(:(Str $y)), P(:(Any $x)) ~~ P(:(Int $y)), P(:(Int $x)) ~~ P(:(Any $y)), P(:(Int:D $x)) ~~ P(:(Int $y)), P(:(Int $x)) ~~ P(:(Int:D $y)), P(:($x?)) ~~ P(:($y)), P(:($x)) ~~ P(:($y?)), P(:($x is rw)) ~~ P(:($y)), P(:($x)) ~~ P(:($y is rw)), P(:(*@a)) ~~ P(:($y)), P(:($x)) ~~ P(:(*@a)), P(:(:$a)) ~~ P(:(:$a)), P(:(:$a)) ~~ P(:(:$b)), P(:(:$a)) ~~ P(:($b)), P(:($x where 1)) ~~ P(:($y where 1)), P(:(42)) ~~ P(:(42)), P(:(42)) ~~ P(:(43)), P(:(42)) ~~ P(:($x)), P(:($x)) ~~ P(:(42)), P(:(Int $x)) eqv P(:(Int $y)), P(:(Int $x)) eqv P(:(Str $y)), P(:($x)) eqv P(:($x?)), P(:(:$a)) eqv P(:(:$a)), P(:(:$a)) eqv P(:(:$b)), P(:($x where 1)) eqv P(:($x where 1)), P(:(42)) eqv P(:(42)), P(:(42)) eqv P(:(43)), P(:($x)) eqv P(:(\x)), P(:(@a ($b))) eqv P(:(@a ($c))), P(:(Int() $x)).type.^name, P(:(Int:D $x)).type.^name, P(:($x = 42)).default.(), P(:($x = 1 + 1)).default.(), P(:($x = 1 + 1)).default.^name, P(:($x = 42)).default.^name, P(:($x)).default.raku, P(:(:$n)).default.raku, P(:($x?)).default.raku, P(:(&c:(Int))).raku, P(:(Int $x)).of.^name, P(:($x)).WHICH.^name, P(:($x)).gist.subst(/\d+/, "N"), P(:(Int $x)).Str.subst(/\d+/, "N"), P(:($x where 1..3)).constraint_list.map(*.^name).raku, P(:($x where { 1 })).constraint_list[0].^name, P(:($x where 1..3)).constraints.^name, P(:($x where 42)).constraint_list.map(*.^name).raku, P(:(42)).constraint_list.raku, P(:(42)).constraints.raku, P(:($x)).constraints.raku, P(:($x)).constraint_list.raku, P(:(Int $x = 5)).raku, P(:(:$n)).named_names.raku, P(:(:a(:b($c)))).named_names.raku, P(:(:a(:b($c)))).name, P(:(:a(:b($c)))).raku, P(:(*%h)).named_names.raku, P(:(*%h)).named, P(:(*%h)).positional, P(:(*@a)).positional, P(:(|c)).positional, P(:(|c)).named, P(:(\x)).name, P(:(\x)).sigil, P(:(\x)).raw, P(:($)).name.raku, P(:($)).sigil, P(:(@)).sigil, P(:(Int)).sigil, P(:(Int)).type.^name, P(:($x)).type.^name, P(:(::T $x)).type.^name, P(:(::T $x)).type_captures.raku, P(:(&c:(Int))).sub_signature.raku, P(:(@a ($b))).sub_signature.raku, P(:(@a ($b))).raku, P(:(:$n)).suffix.raku, P(:(:$n!)).suffix, P(:($x?)).suffix, P(:($x = 1)).suffix.raku, P(:($x)).suffix.raku, P(:(*@a)).prefix, P(:(**@a)).prefix, P(:(+@a)).prefix, P(:(*%h)).prefix, P(:($x)).prefix.raku, P(:(Int:D $x)).modifier, P(:(Int:U $x)).modifier, P(:(Int:_ $x)).modifier.raku, P(:(Int $x)).modifier.raku, (try P(:(&c:(Int))).signature_constraint.raku) // $!.^name, (try P(:($x)).signature_constraint.raku) // $!.^name, (try P(:(Int() $x)).coerce_type.^name) // $!.^name, (try P(:(Int(Str) $x)).coerce_type.^name) // $!.^name, (try P(:(Int $x)).coerce_type.^name) // $!.^name, (try P(:(Int() $x)).nominal_type.^name) // $!.^name, (try P(:(Int:D $x)).nominal_type.^name) // $!.^name, (try P(:(Int() $x)).coercive) // $!.^name, (try P(:($x)).untyped) // $!.^name, (try P(:(Mu $x)).untyped) // $!.^name, (try P(:(Int $x)).untyped) // $!.^name, (try P(:(Mu $x where 1)).untyped) // $!.^name).join(" ") }
# rakudo 2026.08: True False False True True False False True True False False False True False False False True False True False True False False True False False True False False True Int(Any) Int 42 2 Code Block Code Code Code &c:(Int $) Int ObjAt Mu $x Parameter<N> ("Block",).Seq Block Junction ("Block",).Seq (42,) all(42) all() () Int $x = 5 ("n",) ("b", "a") $c Mu :a(:b($c)) () True False False False False x \ True "" $ @ $ Int Mu Mu ("T",) Signature :($b) @a ($b) "" ! ? "" "" * ** + * "" :D :U "" "" :(Int $) Signature Int Int Mu Int Int 1 True True False False
```
rakupp 4.0.1-84: differs — the line died at `.constraint_list` (`X::Method::NotFound`).

### CO-29  Binding traits at run time: is rw, is copy, is raw, `\`, slurpies   D:yes R:yes V:spec
`is rw` needs a writable container: a variable, an anonymous `my Int
$`, an Array, a typed container of the right type; a literal, `Nil`, a
type object, a List or a Hash is `X::Parameter::RW` (`.got` the value,
`.symbol` the name), and a typed rw parameter refuses a container of
another type (`X::TypeCheck::Binding::Parameter`); a named `:$n! is rw`
behaves the same. Through a typed `is rw`, `is copy` or `is raw`
parameter an assignment is re-checked against the parameter's type.
`is copy` gives a fresh `Scalar`; a plain `$x` bound to a variable
keeps that variable's `Scalar` (`.VAR` Scalar) and is read-only
(`X::Assignment::RO`); bound to a literal it is the bare value; `\x` and
`is raw` pass the argument as is (assigning through a literal is
`X::AdHoc`). Type failures are `X::TypeCheck::Binding::Parameter`
(`.got`, `.expected`, `.symbol`, `.operation` "binding", `.parameter`,
`.constraint` True for a `where`, then `.expected` is the where block);
`:D`/`:U` failures `X::Parameter::InvalidConcreteness` (`.expected` and
`.got` are type **names**, `.routine`, `.param` `<anon>` for an invocant,
`.should-be-concrete`, `.param-is-invocant`); an optional `where` parameter
left out runs its `where` against the type object (`X::Numeric::Uninitialized`
here); arity and named mistakes on a sub are `X::AdHoc`. Slurpies:
`*@a` flattens (`[1, 2], 3` gives 3 elements, a Range 3), `**@a` keeps
each argument as one (`[1, 2]` gives 1), `+@a` flattens a single
Iterable argument only (`(1, 2)` 2, `(1, 2), 3` 2, `1` 1); `*@a`,
`**@a`, `+@a` are `Array`s, `*@a is raw` and `+a` a `List` (an Array
argument stays an Array for `+a`); elements of a slurpy are `Scalar`s,
`*%h` a `Hash`, `|c` a `Capture`. `@a` bound to a List is read-only,
bound to an Array writes through, `is copy` copies.
```
say do { sub rw($x is rw) { $x++ }; sub cp($x is copy) { $x++; $x }; sub rw2(\x) { x = 9 }; sub ro($x) { $x = 1 }; sub raw($x is raw) { $x = 7 }; sub ty(Int $x) { }; sub wh($x where * > 5) { }; sub df(Int:D $x) { }; sub du(Int:U $x) { }; sub op(Int $x? where { $_ > 5 }) { }; my $v = 1; my $str = "s"; my $one = 1; my $it = Int; my $five = 5; my \nl = (Nil,); my @o; rw($v); @o.push($v); @o.push((try rw(5)) // $!.^name); @o.push((try rw(my Int $)) // "ok"); @o.push(cp($v), $v); rw2($v); @o.push($v); @o.push((try rw2(1)) // $!.^name); @o.push((try ro($v)) // $!.^name); raw($v); @o.push($v); @o.push((try raw(1)) // $!.^name); @o.push((try ty($str)) // $!.^name); @o.push((try wh($one)) // $!.^name); @o.push((try df($it)) // $!.^name); @o.push((try du($five)) // $!.^name); @o.push((try op()) // $!.^name); @o.push((try ty(Int)) // "typeobj-ok"); @o.push((try ty(|nl)) // $!.^name); @o.push((try rw(Nil)) // $!.^name); @o.push((try (sub (Int $x is rw) { $x = "s" })($v)) // $!.^name); @o.push((try (sub (Int $x is copy) { $x = "s" })($v)) // $!.^name); @o.push((try (sub (Int $x is raw) { $x = "s" })($v)) // $!.^name); @o.push((try (sub ($x is rw) { })(my @a)) // "arr-ok"); @o.push((try (sub ($x is rw) { })((1, 2))) // $!.^name); @o.push((try (sub ($x is rw) { })(Any)) // $!.^name); @o.push((try (sub ($x is rw) { })(my $u)) // "undef-ok"); @o.push((try (sub ($x is rw) { $x = 5 })(my %h)) // $!.^name); @o.push((try (sub ($x is rw) { })(my Int $ti = 1)) // "typed-ok"); @o.push((try (sub (Int $x is rw) { })(my $any = 1)) // "any-cont-ok"); @o.push((try (sub (Int $x is rw) { })(my Str $s = "a")) // $!.^name); @o.push((try (sub (:$n! is rw) { $n = 2 })(:n(my $nn = 1)) // $nn) // $!.^name); @o.push((try (sub (:$n! is rw) { $n = 2 })(:n(1))) // $!.^name); @o.push((try (sub (*@a is raw) { @a[0].VAR.^name })(my $q2 = 1)) // $!.^name); @o.push((try (sub (*@a) { @a[0].VAR.^name })(my $q3 = 1)) // $!.^name); @o.push((try (sub (+@a) { @a.^name })(1, 2)) // $!.^name); @o.push((try (sub (+a) { a.^name })(1, 2)) // $!.^name); @o.push((try (sub (+a) { a.^name })([1, 2])) // $!.^name); @o.push((try (sub (+@a) { @a.elems })([1, 2])) // $!.^name); @o.push((try (sub (*@a) { @a.elems })([1, 2])) // $!.^name); @o.push((try (sub (**@a) { @a.elems })([1, 2])) // $!.^name); @o.push((try (sub (**@a) { @a.elems })([1, 2], 3)) // $!.^name); @o.push((try (sub (*@a) { @a.elems })([1, 2], 3)) // $!.^name); @o.push((try (sub (*@a) { @a.elems })((1, 2), 3)) // $!.^name); @o.push((try (sub (+@a) { @a.elems })((1, 2), 3)) // $!.^name); @o.push((try (sub (+@a) { @a.elems })((1, 2))) // $!.^name); @o.push((try (sub (+@a) { @a.elems })(1)) // $!.^name); @o.push((try (sub (+@a) { @a.elems })()) // $!.^name); @o.push((try (sub (*@a) { @a.elems })(1..3)) // $!.^name); @o.push((try (sub (**@a) { @a.elems })(1..3)) // $!.^name); @o.push((try (sub (+@a) { @a.elems })(1..3)) // $!.^name); @o.push((try (sub (*@a) { @a.^name })(1)) // $!.^name); @o.push((try (sub (**@a) { @a.^name })(1)) // $!.^name); @o.push((try (sub (+@a) { @a.^name })(1)) // $!.^name); @o.push((try (sub (*@a is raw) { @a.^name })(1)) // $!.^name); @o.push((try (sub (*%h) { %h.^name })(:a)) // $!.^name); @o.push((try (sub (|c) { c.^name })(1)) // $!.^name); @o.push((try (sub (*@a) { @a[0].VAR.^name })((my $q4 = 1,)) ) // $!.^name); @o.push((try (sub ($x is copy) { $x.VAR.^name })(1)) // $!.^name); @o.push((try (sub ($x) { $x.VAR.^name })(1)) // $!.^name); @o.push((try (sub ($x) { $x.VAR.^name })(my $q5 = 1)) // $!.^name); @o.push((try (sub (\x) { x.VAR.^name })(1)) // $!.^name); @o.push((try (sub (\x) { x.VAR.^name })(my $q6 = 1)) // $!.^name); @o.push((try (sub ($x is raw) { $x.VAR.^name })(1)) // $!.^name); @o.push((try (sub (@a) { @a.VAR.^name })([1])) // $!.^name); @o.push((try (sub (@a) { @a[0] = 9 })((1,))) // $!.^name); @o.push((try (sub (@a) { @a[0] = 9; @a.raku })([1])) // $!.^name); @o.push((try (sub (@a is copy) { @a[0] = 9; @a.raku })((1,))) // $!.^name); @o.push((try (sub (%h) { %h<a> = 9; %h.raku })({a => 1})) // $!.^name); @o.join(" ") }
# rakudo 2026.08: 2 X::Parameter::RW 0 3 2 9 X::Assignment::RO X::AdHoc 7 X::AdHoc X::TypeCheck::Binding::Parameter X::TypeCheck::Binding::Parameter X::Parameter::InvalidConcreteness X::Parameter::InvalidConcreteness X::Numeric::Uninitialized typeobj-ok X::TypeCheck::Binding::Parameter X::Parameter::RW s X::TypeCheck::Binding::Parameter X::TypeCheck::Binding::Parameter arr-ok X::Parameter::RW X::Parameter::RW undef-ok X::Parameter::RW typed-ok any-cont-ok X::TypeCheck::Binding::Parameter 2 X::Parameter::RW Scalar Scalar Array List List 2 2 1 2 3 3 2 2 1 0 3 1 3 Array Array Array List Hash Capture Scalar Scalar Scalar Scalar Int Scalar Int Array X::Assignment::RO [9] [9] {:a(9)}
say do { sub ty(Int $x) { }; sub wh($x where * > 5) { }; sub df(Int:D $x) { }; sub du(Int:U $x) { }; sub op(Int $x? where { $_ > 5 }) { }; sub rw($x is rw) { }; class C { method m(C:D:) { } }; my $str = "s"; my $one = 1; my $it = Int; my $five = 5; my $ty = Str; my @o; try rw(5); my $e1 = $!; @o.push($e1.^name, (try $e1.got.raku) // "NA", (try $e1.symbol) // "NA"); try ty($str); my $e2 = $!; @o.push($e2.^name, $e2.got.raku, $e2.expected.^name, (try $e2.symbol) // "NA", ($e2 ~~ X::TypeCheck::Binding), ($e2 ~~ X::TypeCheck), (try $e2.operation) // "NA", (try $e2.parameter.name) // "NA", (try $e2.constraint.raku) // "NA", (try $e2.what.raku) // "NA"); try wh($one); my $e3 = $!; @o.push($e3.^name, (try $e3.constraint) // "NA", $e3.expected.^name, $e3.got); try df($it); my $e4 = $!; @o.push($e4.^name, (try $e4.expected.^name) // "NA", (try $e4.got.^name) // "NA", (try $e4.routine) // "NA", (try $e4.param) // "NA", (try $e4.should-be-concrete) // "NA", (try $e4.param-is-invocant) // "NA"); try du($five); my $e5 = $!; @o.push($e5.^name, (try $e5.should-be-concrete) // "NA"); try op(); my $e6 = $!; @o.push($e6.^name, (try $e6.omitted.raku) // "NA"); try C.m; my $e7 = $!; @o.push($e7.^name, (try $e7.param-is-invocant) // "NA", (try $e7.routine) // "NA", (try $e7.param.raku) // "NA"); try (sub (Int $x) { })($ty); @o.push($!.^name, $!.got.raku); @o.push((try (sub ($x) { })()) // $!.^name, (try (sub ($x) { })(1, 2)) // $!.^name, (try (sub (:$n!) { })()) // $!.^name, (try (sub ($x) { })(1, :n)) // $!.^name, (try (sub ($x) { })(:n)) // $!.^name, (try (sub ($x, $y) { })(1)) // $!.^name); @o.join(" ") }
# rakudo 2026.08: X::Parameter::RW 5 $x X::TypeCheck::Binding::Parameter "s" Int $x True True binding $x Bool Str X::TypeCheck::Binding::Parameter True WhateverCode 1 X::Parameter::InvalidConcreteness Str Str df $x True False X::Parameter::InvalidConcreteness False X::Numeric::Uninitialized NA X::Parameter::InvalidConcreteness True m "<anon>" X::TypeCheck::Binding::Parameter Str X::AdHoc X::AdHoc X::AdHoc X::AdHoc X::AdHoc X::AdHoc
```
rakupp 4.0.1-84: differs — `Nil`, a List, `Any`, a Hash and a literal named all bind to `is rw` (no `X::Parameter::RW`), a plain `$x` bound to a variable is `X::AdHoc` on assignment instead of `X::Assignment::RO` and shows `.VAR` `Int`, the omitted-optional `where` is `X::TypeCheck::Binding::Parameter`, slurpy elements are `Int` not `Scalar`, `+@a`/`+a` give `Array` not `List`, `@a` bound to a List is writable, and the exception attributes are missing (the second line died at `.got`).

### CO-30  Coercion types `Int()`, `Int(Str)`, `Int:D()`                 D:partial R:partial V:quirk
`Int() $x` accepts any argument whose type is not already Int and
calls its `.Int`: `"42"` gives 42, `4.7` 4, `"0x10"` 16, `" 7 "` 7, `""`
0, `"1e3"` 1000, `1.5e0` 1, `[1]` 1, an object with an `Int` method its
result; an Int, an `IntStr` and the `Int` type object pass untouched; a
`Bool` passes as a Bool (it is an Int). `Int(Str)` accepts only a Str
or an Int (a Rat is `X::TypeCheck::Binding::Parameter`). The bound value
is a plain value (`.VAR` Int) even from a container. Failures: a Str
type object `X::AdHoc`, `1i` `X::Numeric::Real`, an object without
`.Int` `X::Multi::NoMatch`, an `.Int` that returns a non-Int
`X::Coerce::Impossible`; `Any` and `Nil` coerce to 0 with a warning.
Quirks: a coercion that **fails** (`"x"`, `Inf`, `"Inf"`) does not throw
but binds the `Failure` (`.defined` False, `.exception` `X::Str::Numeric`
or `X::Numeric::CannotConvert`), and a `where` on such a parameter then
throws that exception; `Int() $x is rw` binds a container but assigning
through it is `X::AdHoc` (the coerced value has no container). A
default or a named coerces too, `Int() @a` does not coerce elements
(`X::TypeCheck::Binding::Parameter`), `Int:D()` prints `Int:D(Any):D`
(type `Int:D(Any)`, modifier `:D`) and refuses type objects with
`X::Parameter::InvalidConcreteness`.
```
say do { sub a(Int() $x) { $x.^name ~ ":" ~ $x.raku }; sub af(Int() $x) { $x.^name }; sub b(Int(Str) $x) { $x.^name ~ ":" ~ $x.raku }; sub c(Str() $x) { $x.raku }; sub d(Int() $x is rw) { $x = 1 }; my $str = "x"; my $ty = Str; my $it = Int; (a("42"), a(4.7), a(True), af($str), b("42"), (try b(4.7)) // $!.^name, (try b(4)) // $!.^name, c(42), c(1.5), a(Int), (try a($ty)) // $!.^name, (try b($ty)) // $!.^name, &a.signature.params[0].type.^name, &a.signature.params[0].raku, &b.signature.params[0].raku, &a.signature.raku, &a.signature.gist, (\("42") ~~ :(Int() $x)), (\("x") ~~ :(Int() $x)), (try d(my $s = "5")) // $!.^name, sub (Int() $x, $y = $x) { $y.^name }("3"), a("1/2"), a("0x10"), a(" 7 "), (try a("")) // $!.^name, a("1e3"), (try a(1i)) // $!.^name, sub (Int() $x is copy) { $x++ }("4"), (sub (Numeric() $x) { $x.^name })("1.5"), (sub (Str(Int) $x) { $x.raku })(5), (try (sub (Str(Int) $x) { "ok" })("s")) // $!.^name, (sub (Int() $x) { $x })(my $c = "9"), (sub (Int() $x) { $x.VAR.^name })(my $c2 = "9"), (sub (Int() $x) { $x.VAR.^name })(9), (sub (Int() $x) { $x.VAR.^name })("9"), (sub (Int:D() $x) { $x.raku })("9"), (try (sub (Int:D() $x) { $x.raku })($it)) // $!.^name, (try (sub (Int:D() $x) { $x.raku })($ty)) // $!.^name, :(Int:D() $x).raku, :(Int:D() $x).params[0].type.^name, :(Int:D() $x).params[0].modifier, (sub (Int() $x = "5") { $x.raku })(), (sub (Int() :$n) { $n.raku })(:n<3>), (sub (Int() :$n) { $n.raku })(), (try (sub (Int() $x where * > 3) { $x })("5")) // $!.^name, (try (sub (Int() $x where * > 3) { $x })("2")) // $!.^name, (try (sub (Int() @a) { @a.raku })(["1", 2])) // $!.^name, (try (sub (Int() $x) { $x.raku })(1.5e0)) // $!.^name, af(Inf), af("Inf"), (sub (Str() $x) { $x.raku })([1, 2]), (sub (Str() $x) { $x.raku })(Str), (try (sub (Numeric(Cool) $x) { $x.raku })([1])) // $!.^name, (try a([1])) // $!.^name, (try (sub (Int() $x) { $x })(class { method Int { 77 } }.new)) // $!.^name, (try (sub (Int() $x) { $x })(class { }.new)) // $!.^name, (try (sub (Int() $x) { $x })(class { method Int { "notint" } }.new)) // $!.^name, do { my $f = af("x"); $f.^name }, do { my $f = (sub (Int() $x) { $x })("x"); $f.so; $f.exception.^name }, do { my $f = (sub (Int() $x) { $x })(Inf); $f.so; $f.exception.^name }, (sub (Int() $x) { $x.defined })("x"), (try (sub (Int() $x where { $_ > 3 }) { $x })("x")) // $!.^name, (try d(my $s2 = "5")) // $!.message).join(" ") }
# rakudo 2026.08: Int:42 Int:4 Bool:Bool::True Failure Int:42 X::TypeCheck::Binding::Parameter Int:4 "42" "1.5" Int:Int X::AdHoc X::AdHoc Int(Any) Int(Any) $x Int(Str) $x :(Int(Any) $x) (Int(Any) $x) True True X::AdHoc Int Int:0 Int:16 Int:7 Int:0 Int:1000 X::Numeric::Real 4 Rat "5" ok 9 Int Int Int 9 X::Parameter::InvalidConcreteness X::Parameter::InvalidConcreteness :(Int:D(Any):D $x) Int:D(Any) :D 5 IntStr.new(3, "3") Int 5 X::TypeCheck::Binding::Parameter X::TypeCheck::Binding::Parameter 1 Failure Failure "1 2" Str 1 Int:1 77 X::Multi::NoMatch X::Coerce::Impossible Str X::Str::Numeric X::Numeric::CannotConvert False X::Str::Numeric Cannot assign to an immutable value
say do { sub a(Int() $x) { $x.^name ~ ":" ~ $x.raku }; my @w; CONTROL { when CX::Warn { @w.push(.message); .resume } }; (a(Any), a(Nil), @w.elems, @w.raku).join(" ") }
# rakudo 2026.08: Int:0 Int:0 2 ["Use of uninitialized value of type Any in numeric context", "Use of Nil in numeric context"]
```
rakupp 4.0.1-84: differs — `Int(Str)` coerces a Rat, a Str type object coerces to 0 (no `X::AdHoc`), `1i` is `X::Coerce::Impossible`, an object without `.Int` gives 0 and a non-Int `.Int` is bound as is, `is rw` assigns silently, `is copy` returns `Nil`, the bound value keeps a `Scalar`, `Int() @a` coerces elements, `Int:D()` prints `:(Int:D $x)` and accepts a Str type object as 0, and `Any`/`Nil` coerce without warning.

### CO-31  is item                                                       D:no R:no V:spec
`is item` is allowed on `@`, `%` and sigilless parameters only (`$x is
item` is a compile-time `X::Comp::Trait::Invalid`); `.is-item` is True
and `.raku` shows it. It does not change binding: `@x is item` takes a
List, Array, Seq or Range exactly as `@x` does and refuses an Int the
same way; slurpies with it flatten as without; the bound `@a` is an
`Array` either way. It is a **dispatch** tie-breaker: an itemized
argument (`$[1, 2]`, `$@arr`, `${…}`, `$%h`) picks the `is item`
candidate, a non-itemized one (`[1, 2]`, `(1, 2)`, `@arr`, `{…}`) the
plain candidate.
```
say do { multi f(@a is item) { "item" }; multi f(@a) { "plain" }; multi g(%h is item) { "item" }; multi g(%h) { "plain" }; sub b(@x is item) { @x.^name }; sub bb(@x) { @x.^name }; sub h(%h is item) { %h.^name }; my @arr = 1, 2; my %hh = a => 1; my $one = 1; my $r = 1..2; my $sq = (1, 2).Seq; (f([1, 2]), f($[1, 2]), f((1, 2)), f(@arr), f($@arr), f((1, 2).Seq), g({a => 1}), g(${a => 1}), g(%hh), g($%hh), (sub (*@a is item) { @a.raku })([1, 2], [3, 4]), (sub (*@a) { @a.raku })([1, 2], [3, 4]), (sub (*@a is item) { @a.raku })($[1, 2], $[3, 4]), (sub (*@a) { @a.raku })($[1, 2], $[3, 4]), (sub (+@a is item) { @a.raku })($[1, 2]), (sub (+@a) { @a.raku })($[1, 2]), (sub (+@a is item) { @a.raku })([1, 2]), (sub (+@a) { @a.raku })([1, 2]), (sub (+@a is item) { @a.raku })((1, 2)), (sub (**@a is item) { @a.raku })($[1, 2], [3]), (sub (**@a) { @a.raku })($[1, 2], [3]), (sub (@a is item) { @a.VAR.^name })($[1, 2]), (sub (@a) { @a.VAR.^name })($[1, 2]), (sub (*@a is item) { @a[0].VAR.^name })([1, 2]), (sub (*@a) { @a[0].VAR.^name })([1, 2]), (sub (*@a is item) { @a[0].VAR.^name })($[1, 2]), (sub (*@a) { @a[0].VAR.^name })($[1, 2]), (try b((1, 2))) // $!.^name, (try b([1, 2])) // $!.^name, (try b($one)) // $!.^name, (try b(@arr)) // $!.^name, (try b($[1, 2])) // $!.^name, (try b($sq)) // $!.^name, (try b($r)) // $!.^name, (try bb($one)) // $!.^name, (try bb($sq)) // $!.^name, (try bb($r)) // $!.^name, (try h({a => 1})) // $!.^name, (try h($one)) // $!.^name, (try h(%(a => 1))) // $!.^name, :(@x is item).params[0].raku, :(@x is item).raku, (try :(@x is item).params[0].is-item) // $!.^name, (try :(@x).params[0].is-item) // $!.^name, (try EVAL ':($a is item)') // $!.^name).join(" ") }
# rakudo 2026.08: plain item plain plain item plain plain item plain item [1, 2, 3, 4] [1, 2, 3, 4] [[1, 2], [3, 4]] [[1, 2], [3, 4]] [[1, 2],] [[1, 2],] [1, 2] [1, 2] [1, 2] [[1, 2], [3]] [[1, 2], [3]] Array Array Scalar Scalar Scalar Scalar List Array X::TypeCheck::Binding::Parameter Array Array List Range X::TypeCheck::Binding::Parameter List Range Hash X::TypeCheck::Binding::Parameter Hash @x is item :(@x is item) True False X::Comp::Trait::Invalid
```
rakupp 4.0.1-84: differs — the `is item` candidate wins for every argument, `@x` accepts an Int, a Seq stays a Seq and a Range becomes an Array on binding, `.VAR` of the bound array is `Int`/`Array`, `.is-item` is missing and `$x is item` compiles.

## E. Attribute

### CO-32  What an Attribute answers                                     D:partial R:partial V:spec
Per attribute of `.^attributes(:local)`: `.name` is always the private
form (`$!b` for `has $.b`), `.type` `Mu` when untyped, `Positional`/
`Associative`/`Callable` for `@ % &`, a native `int`, `Int:D` kept;
`.has_accessor` for `.` attributes; `.rw` only with `is rw`, `.readonly`
its inverse; `.package` the class; `.gist` `Type $!name`, `.Str` the
name; `.^name` `Attribute`, or `Attribute+{is-DEPRECATED}` with `is
DEPRECATED` and then `.DEPRECATED` is the message; `.required` `Mu`,
`1` for `is required`, the message for `is required("…")`; `.is_built`
True for public attributes and `is built` privates, False for a plain
private and `is built(False)`; `.is_bound` only for `is built(:bind)`;
`.build` `Mu` without initializer, the literal for `= 5`, a `Method`
for an expression; `.container` the container prototype (`Any`, `Int`,
`Array`, `Hash`, `Callable`, `int`); `.inlined` 0; `.WHY` Nil.
```
say do { class C { has $!a; has $.b; has Int $.c is rw; has @.d; has %.e; has &.f; has Str $.g is required; has $.h is required("a reason"); has $!i is built; has $.j is built(False); has $.k = 5; has Int $.l is default(7) is rw; has $.m is DEPRECATED("n"); has $!n is built(:bind); has int $.o; has Int:D $.p is required; has $.q = 1 + 1 }; sub T(&c) { (try c()) // "NA" }; my @a = C.^attributes(:local); @a.map({ (.name, .type.^name, .has_accessor ?? "acc" !! "-", .rw ?? "rw" !! "ro", .readonly ?? "RO" !! "-", .package.^name, .gist, .Str, .^name, .has_accessor.^name, .rw.^name, T({ .required.raku }), T({ .required.^name }), T({ .is_built ?? "B" !! "-" }), T({ .is_bound ?? "bind" !! "-" }), T({ .?DEPRECATED // "-" }), T({ .build.^name }), T({ .container.^name }), T({ .inlined.raku }), T({ .WHY.raku })).join(",") }).join(" | ") }
# rakudo 2026.08: $!a,Mu,-,ro,RO,C,Mu $!a,$!a,Attribute,Bool,Bool,Mu,Mu,-,-,-,Mu,Any,0,Nil | $!b,Mu,acc,ro,RO,C,Mu $!b,$!b,Attribute,Bool,Bool,Mu,Mu,B,-,-,Mu,Any,0,Nil | $!c,Int,acc,rw,-,C,Int $!c,$!c,Attribute,Bool,Bool,Mu,Mu,B,-,-,Mu,Int,0,Nil | @!d,Positional,acc,ro,RO,C,Positional @!d,@!d,Attribute,Bool,Bool,Mu,Mu,B,-,-,Mu,Array,0,Nil | %!e,Associative,acc,ro,RO,C,Associative %!e,%!e,Attribute,Bool,Bool,Mu,Mu,B,-,-,Mu,Hash,0,Nil | &!f,Callable,acc,ro,RO,C,Callable &!f,&!f,Attribute,Bool,Bool,Mu,Mu,B,-,-,Mu,Callable,0,Nil | $!g,Str,acc,ro,RO,C,Str $!g,$!g,Attribute,Bool,Bool,1,Int,B,-,-,Mu,Str,0,Nil | $!h,Mu,acc,ro,RO,C,Mu $!h,$!h,Attribute,Bool,Bool,"a reason",Str,B,-,-,Mu,Any,0,Nil | $!i,Mu,-,ro,RO,C,Mu $!i,$!i,Attribute,Bool,Bool,Mu,Mu,B,-,-,Mu,Any,0,Nil | $!j,Mu,acc,ro,RO,C,Mu $!j,$!j,Attribute,Bool,Bool,Mu,Mu,-,-,-,Mu,Any,0,Nil | $!k,Mu,acc,ro,RO,C,Mu $!k,$!k,Attribute,Bool,Bool,Mu,Mu,B,-,-,Int,Any,0,Nil | $!l,Int,acc,rw,-,C,Int $!l,$!l,Attribute,Bool,Bool,Mu,Mu,B,-,-,Mu,Int,0,Nil | $!m,Mu,acc,ro,RO,C,Mu $!m,$!m,Attribute+{is-DEPRECATED},Bool,Bool,Mu,Mu,B,-,n,Mu,Any,0,Nil | $!n,Mu,-,ro,RO,C,Mu $!n,$!n,Attribute,Bool,Bool,Mu,Mu,B,bind,-,Mu,Any,0,Nil | $!o,int,acc,ro,RO,C,int $!o,$!o,Attribute,Bool,Bool,Mu,Mu,B,-,-,Mu,int,0,Nil | $!p,Int:D,acc,ro,RO,C,Int:D $!p,$!p,Attribute,Bool,Bool,1,Int,B,-,-,Mu,Int,0,Nil | $!q,Mu,acc,ro,RO,C,Mu $!q,$!q,Attribute,Bool,Bool,Mu,Mu,B,-,-,Method,Any,0,Nil
```
rakupp 4.0.1-84: differs — `.gist` omits the type, `.required`, `.is_bound` and `.inlined` are missing, `.build` is `Any`, `.container` is the Attribute itself, `&.f` is typed `Mu` and `Int:D` prints `Int`.

### CO-33  is required, is built, is rw, is default; attribute order        D:partial R:partial V:quirk
A missing `is required` attribute makes `.new` and `.bless` throw
`X::Attribute::Required` (an `X::MOP`; `.name` `$!a`, `.why` 1 or the
message); a type object or `Nil` passed for it counts as provided.
`is built` lets `.new` set a private attribute, `is built(False)` stops
`.new` from setting a public one, `is built(:bind)` binds the value
(`.VAR` is the value's type, not `Scalar`). An `is rw` accessor is
assignable and type-checked (`X::TypeCheck::Assignment`), a plain one
is `X::Assignment::RO`. `is default(7)` with `= 9` starts at 9 and
reverts to 7 when `Nil` is assigned or passed; `is default("s")` on an
Int attribute is a compile-time `X::TypeCheck::Attribute::Default`. An
unknown attribute trait is `X::Comp::Trait::Unknown` declaring "an
attribute". `.^attributes` lists a class's own attributes in declaration
order, a subclass's own **before** the parent's, a class's own before a
role's; `(:local)` is the class's own; on an instance the same list;
`.raku`/`.gist` of an object list the `is built` attributes. Quirk: an
attribute read from the **role** (`R.^attributes`) has `.package`
`$?CLASS`; through the class it is the class.
```
say do { class C { has $.a is required; has $.b is required("a reason"); has $!c is built; has $.d is built(False); has $!e is built(:bind); has $.f is rw; has $.g; has Int $.h is default(7) is rw = 9; method c { $!c }; method e { $!e.VAR.^name } }; my @o; @o.push((try C.new(:b(1))) // $!.^name ~ ":" ~ $!.name); @o.push((try C.new(:a(1))) // $!.^name ~ ":" ~ $!.name); @o.push((try C.bless(:b(1))) // $!.^name); my $c = C.new(:a(1), :b(2), :c(3), :d(4), :e(5)); @o.push($c.c, $c.d.raku, $c.e); $c.f = 6; @o.push($c.f); @o.push((try { $c.g = 7; "ok" }) // $!.^name); @o.push($c.h); $c.h = Nil; @o.push($c.h); @o.push((try { $c.h = "s"; "ok" }) // $!.^name); @o.push(C.^attributes.map(*.name).raku); @o.push(C.^attributes(:local).map(*.name).raku); class D is C { has $.z; has $.y }; @o.push(D.^attributes.map(*.name).raku); @o.push(D.^attributes(:local).map(*.name).raku); role R { has $.r1; has $.r2 }; class E does R { has $.e1 }; @o.push(E.^attributes.map(*.name).raku); @o.push(E.^attributes[0].package.^name); @o.push(D.^attributes[0].package.^name); @o.push((try EVAL 'class Q { has $.x is bogus }') // $!.^name ~ ":" ~ $!.subtype ~ ":" ~ $!.declaring); @o.push((try EVAL 'class Q2 { has Int $.x is default("s") }') // $!.^name); @o.push(C.new(:a(Int), :b(1)).a.raku); @o.push((try C.new(:a(1), :b(Nil)).b.raku) // $!.^name); @o.push(R.^attributes.map(*.name).raku); @o.push(R.^attributes[0].package.^name); @o.push($c.^attributes.map(*.name).elems); @o.push((try C.new(:a(1), :b(2), :f(9)).f) // $!.^name); @o.push(C.new(:a(1), :b(2), :h(Nil)).h); @o.push(C.new(:a(1), :b(2), :g(Nil)).g.raku); @o.push((try C.new(:a(1), :b(2), :h("s")).h) // $!.^name); @o.push(C.new(:a(1), :b(2)).raku); @o.push(C.new(:a(1), :b(2)).gist); class F { has $.x; has $!y = 2; has $.z is built(False) = 3; method y { $!y } }; @o.push(F.new(:x(1), :y(9), :z(8)).raku, F.new.y); @o.push(D.^attributes(:local)[0].name, D.^attributes(:local)[0].package.^name); class G { has $.a; has $.b; has $.c }; @o.push(G.^attributes.map(*.name).raku); @o.push(G.^attributes.^name); @o.push((try C.new(:b(1))) // $!.why.raku); @o.push((try C.new(:a(1))) // $!.why.raku); @o.push((try C.new(:b(1))) // ($! ~~ X::MOP)); @o.push(C.^attributes[0].required.raku, C.^attributes[1].required.raku, C.^attributes[5].required.raku); @o.join(" ") }
# rakudo 2026.08: X::Attribute::Required:$!a X::Attribute::Required:$!b X::Attribute::Required 3 Any Int 6 X::Assignment::RO 9 7 X::TypeCheck::Assignment ("\$!a", "\$!b", "\$!c", "\$!d", "\$!e", "\$!f", "\$!g", "\$!h").Seq ("\$!a", "\$!b", "\$!c", "\$!d", "\$!e", "\$!f", "\$!g", "\$!h").Seq ("\$!z", "\$!y", "\$!a", "\$!b", "\$!c", "\$!d", "\$!e", "\$!f", "\$!g", "\$!h").Seq ("\$!z", "\$!y").Seq ("\$!e1", "\$!r1", "\$!r2").Seq E D X::Comp::Trait::Unknown:bogus:an attribute X::TypeCheck::Attribute::Default Int Any ("\$!r1", "\$!r2").Seq $?CLASS 8 9 7 Any X::TypeCheck::Assignment C.new(a => 1, b => 2, c => Any, e => Any, f => Any, g => Any, h => 9) C.new(a => 1, b => 2, c => Any, e => Any, f => Any, g => Any, h => 9) F.new(x => 1) 2 $!z D ("\$!a", "\$!b", "\$!c").Seq List 1 "a reason" True 1 "a reason" Mu
```
rakupp 4.0.1-84: differs — the line died at `.required` (`X::Method::NotFound`).

### CO-34  get_value, set_value, natives, Attribute.new                   D:partial R:partial V:spec
`.get_value($obj)` reads the slot (a native is boxed: `Int`, `Num`,
`Str`), `.set_value($obj, $v)` binds it without any type check (a Str
into an `Int`-typed slot, an Int into an `@` slot) and returns the
value; a native slot refuses a wrong kind with `X::AdHoc`; `get_value`
on the type object or on an object of another class is `X::AdHoc`.
`Attribute.new` needs `:name`, `:type` and `:package` (else `X::AdHoc`;
via an instance too); `:has_accessor` implies `.is_built` (passing
`:is_built(0)` with it is ignored), `:rw` sets `.rw`, `.required` is
`Mu`, `.container` the type object, `.WHICH` an `ObjAt`; a name without
sigil (`q`) is accepted.
```
say do { class C { has $!a = 5; has int $!n = 3; has Str $.s = "x"; has @!l = 1, 2; has num $!f = 1.5e0; has str $!t = "q"; method a { $!a } }; my $c = C.new; my $A = C.^attributes[0]; my $N = C.^attributes[1]; my $S = C.^attributes[2]; my $L = C.^attributes[3]; my $F = C.^attributes[4]; my $T = C.^attributes[5]; ($A.get_value($c), $A.set_value($c, 42), $c.a, $N.get_value($c), $N.set_value($c, 9), $N.get_value($c), $N.type.^name, $N.gist, $S.get_value($c), $L.get_value($c).raku, $L.type.^name, $L.gist, $A.get_value($c).VAR.^name, (try $A.set_value($c, "s")) // "no-typecheck", $c.a, (try $S.set_value($c, 42)) // $!.^name, $S.get_value($c).raku, $c.s.raku, (try $A.get_value(C)) // $!.^name, (try $A.get_value(42)) // $!.^name, C.^attributes[0].get_value(C.new), $c.^attributes.map(*.name).raku, C.^attributes.^name, C.^attributes.elems, (try C.^attributes[0].new) // $!.^name, $F.get_value($c), $F.type.^name, $T.get_value($c), $T.type.^name, $N.get_value($c).^name, $F.get_value($c).^name, $T.get_value($c).^name, (try $N.set_value($c, "s")) // $!.^name, $N.get_value($c), $L.set_value($c, [7]).raku, $L.get_value($c).raku, $L.set_value($c, 42).raku, (try $L.get_value($c).raku) // $!.^name, C.^attributes[0].get_value($c).^name, $A.name, $A.type.^name, (try $A.container.^name) // $!.^name, (try $A.container.raku) // $!.^name, (try $S.container.raku) // $!.^name, (try $L.container.raku) // $!.^name).join(" ") }
# rakudo 2026.08: 5 42 42 3 9 9 int int $!n x [1, 2] Positional Positional @!l Int s s 42 42 42 X::AdHoc X::AdHoc 5 ("\$!a", "\$!n", "\$!s", "\@!l", "\$!f", "\$!t").Seq List 6 X::AdHoc 1.5 num q str Int Num Str X::AdHoc 9 [7] [7] 42 42 Str $!a Mu Any Any Str []
say do { class C { }; (Attribute.new(:name<$!q>, :type(Int), :package(C)).name, Attribute.new(:name<$!q>, :type(Int), :package(C)).type.^name, Attribute.new(:name<$!q>, :type(Int), :package(C)).has_accessor, Attribute.new(:name<$!q>, :type(Int), :package(C), :has_accessor).has_accessor, Attribute.new(:name<$!q>, :type(Int), :package(C)).gist, Attribute.new(:name<$!q>, :type(Int), :package(C)).rw, Attribute.new(:name<$!q>, :type(Int), :package(C)).package.^name, (try Attribute.new) // $!.^name, (try Attribute.new(:name<$!q>)) // $!.^name, (try Attribute.new(:name<$!q>, :type(Int))) // $!.^name, (try Attribute.new(:name<$!q>, :type(Int), :package(C)).required.raku) // $!.^name, (try Attribute.new(:name<$!q>, :type(Int), :package(C)).is_built.raku) // $!.^name, (try Attribute.new(:name<$!q>, :type(Int), :package(C), :has_accessor).is_built.raku) // $!.^name, (try Attribute.new(:name<$!q>, :type(Int), :package(C), :has_accessor, :is_built(0)).is_built.raku) // $!.^name, (try Attribute.new(:name<$!q>, :type(Int), :package(C)).readonly) // $!.^name, (try Attribute.new(:name<$!q>, :type(Int), :package(C), :rw).rw) // $!.^name, (try Attribute.new(:name<$!q>, :type(Int), :package(C)).container.raku) // $!.^name, (try Attribute.new(:name<$!q>, :type(Int), :package(C)).WHICH.^name) // $!.^name, (try Attribute.new(:name<$!q>, :type(Int), :package(C)).Str) // $!.^name, (try Attribute.new(:name<q>, :type(Int), :package(C)).name) // $!.^name, (try Attribute.new(:name<q>, :type(Int), :package(C)).gist) // $!.^name).join(" ") }
# rakudo 2026.08: $!q Int False True Int $!q False C X::AdHoc X::AdHoc X::AdHoc Mu Bool::False Bool::True Bool::True True False Int ObjAt $!q q Int q
```
rakupp 4.0.1-84: differs — `get_value` on the type object or a foreign object and `Attribute.new` via an instance are `X::Method::NotFound`, a Str goes into a native `int` slot, `.container` is the Attribute, `.gist` omits the type, `Attribute.new` with missing arguments returns `""`, `.required` is missing.

## F. WhateverCode, subscripts, ForeignCode

### CO-35  WhateverCode                                                  D:partial R:partial V:bug
`* + 1`, `*.abs`, `*[0]`, `* + *` are WhateverCodes (a Code, not a
Block: `.has-phasers` False, no `.phasers`, no `.multi`); `*` alone is a
`Whatever`. Arity and count are the number of stars, a wrong count is
`X::AdHoc`; `.raku` and `.gist` are `WhateverCode.new`, `.name` `""`,
`.Str` `""` with the coercion warning, the signature `:(;; $whatevercode_arg_N
is raw)` numbered by a process-wide counter, `.line` 1, `.of`/`.returns`
`Mu`, `.ACCEPTS` the raw result (`6`) and `~~` boolified, `.cando`
1/0, `.assuming` works. **Currying**: a method call on a `*`-term is
itself curried — `(* + 1).arity`, `.^name`, `.ACCEPTS(-2)`, `.WHICH`,
`.raku`, `.gist` all give a new WhateverCode (stringifying it warns and
prints `""`), so introspection needs the code in a variable first; only
`.WHAT` and `.HOW` are not curried. On the right of `~~`, a bare `*` or
an operator expression (`5 ~~ *`, `4 ~~ * > 3`) curries the **whole
smartmatch** into a WhateverCode, while `*.method` does not (`"s" ~~
*.uc` is a Bool) and `5 ~~ (* + 1) > 3` is `True > 3`, False. Bug: `.file`
answers a null Str that `.raku`, `.Bool` and `~` refuse with `X::AdHoc`
(`.defined` True, `.^name` Str).
```
say do { my $w1 = * + 1; my $w2 = * + *; my $wa = *.abs; my $w0 = *; my $wi = *[0]; my $w3 = * + * + *; my $wm = * * 2; my $wg = * > 2; my $ws = *.so; my $we = * == 5; my $wu = *.uc; ($w1.^name, $w2.^name, $wa.^name, $w0.^name, $w1.arity, $w1.count, $w2.arity, $w2.count, $wa.arity, $wa.count, $w1(4), $w2(1, 2), $wa(-3), (try $w1()) // $!.^name, (try $w1(1, 2)) // $!.^name, (try $w2(1)) // $!.^name, (try $w2(1, 2, 3)) // $!.^name, $w1.raku, $w2.raku, $wa.raku, $w1.signature.raku, $w2.signature.raku, $wa.signature.raku, $w0.raku, $w0.WHAT.gist, $w1.WHAT.gist, ($w1 ~~ Callable), ($w1 ~~ Code), ($w1 ~~ Block), $w1.ACCEPTS(5), $w1.ACCEPTS(5).^name, $wg.ACCEPTS(1), ($w1 ~~ WhateverCode), $w3.count, $w1.name.raku, $w1.gist, $w1.line, (try $w1.file.raku) // $!.^name, (try $w1.file.defined) // $!.^name, (try $w1.file.^name) // $!.^name, $wi.^name, $wi([9])).join(" ") }
# rakudo 2026.08: WhateverCode WhateverCode WhateverCode Whatever 1 1 2 2 1 1 5 3 3 X::AdHoc X::AdHoc X::AdHoc X::AdHoc WhateverCode.new WhateverCode.new WhateverCode.new :(;; $whatevercode_arg_1 is raw) :(;; $whatevercode_arg_2 is raw, $whatevercode_arg_3 is raw) :(;; $whatevercode_arg_4 is raw) * (Whatever) (WhateverCode) True True False 6 Int False True 3 "" WhateverCode.new 1 X::AdHoc True Str WhateverCode 9
say do { my $w1 = * + 1; my $w2 = * + *; my $wa = *.abs; my $wm = * * 2; my $ws = *.so; my $we = * == 5; my $wu = *.uc; ($w1.signature.params[0].name, $w1.signature.params[0].raw, $w2.signature.params.map(*.name).raku, $wa.signature.params[0].name, $w1.signature.arity, $w1.signature.count.^name, $w1.count.^name, $w1.of.^name, $w1.returns.^name, $wm.cando(\(1)).elems, $wm.cando(\(1, 2)).elems, (5 ~~ $w1), (0 ~~ $w1).raku, (-1 ~~ $w1), (5 ~~ $ws), ("" ~~ $ws), $ws.ACCEPTS(""), $we.ACCEPTS(5), ("a" ~~ $wu), ($w1.assuming(1))(), (try $w1.has-phasers) // $!.^name, (try $w1.has-loop-phasers) // $!.^name, (try $w1.multi.^name) // $!.^name, $w1.Str.raku, (try $w1.phasers('LEAVE').raku) // $!.^name).join(" ") }
# rakudo 2026.08: WhateverCode object coerced to string (please use .gist or .raku to do that)|$whatevercode_arg_1 True ("\$whatevercode_arg_2", "\$whatevercode_arg_3").Seq $whatevercode_arg_4 1 Int Int Mu Mu 1 0 True Bool::True False True False False True True 2 False False X::Method::NotFound "" X::Method::NotFound|  in block  at -e line 1
say do { my $w1 = * + 1; my $wm = * * 2; ((try (* + 1).arity.^name) // $!.^name, (* + 1).arity.WHAT.gist, (try ((* + 1).arity)(5)) // $!.^name, (*.abs).WHAT.gist, (try ((*.abs).arity).(-5)) // $!.^name, (*).WHAT.gist, (*.WHAT).^name, ((* + 1).raku)(3), $w1.signature.params[0].usage-name, do { my @a = (* + 1, * + 2); @a.map({ .(10) }).raku }, do { my @a = (* + 1).arity, (* + 2).count; @a.map(*.^name).raku }, ($wm ∘ $w1)(3), $wm.assuming(3)(), ((* + 1).gist)(1), ((*.abs).gist)(-2), (*.abs).ACCEPTS(-2).WHAT.gist, (4 ~~ (* > 3)), (2 ~~ (* > 3)), (* + 1).WHAT.gist, (* + 1).^name.WHAT.gist, (* + 1).arity.arity.WHAT.gist, (4 ~~ * > 3).WHAT.gist, (5 ~~ (* + 1) > 3), (5 ~~ *).WHAT.gist, ("s" ~~ *.uc).WHAT.gist, (* + 1).WHICH.^name, (* + 1).HOW.^name).join(" ") }
# rakudo 2026.08: WhateverCode object coerced to string (please use .gist or .raku to do that)| (WhateverCode) X::Method::NotFound (WhateverCode) X::Method::NotFound (Whatever) Whatever 4 whatevercode_arg_1 (11, 12).Seq ("WhateverCode", "WhateverCode").Seq 8 6 2 2 (WhateverCode) True False (WhateverCode) (WhateverCode) (WhateverCode) (WhateverCode) False (WhateverCode) (Bool)  Perl6::Metamodel::ClassHOW|  in block  at -e line 1|WhateverCode object coerced to string (please use .gist or .raku to do that)|  in block  at -e line 1
```
rakupp 4.0.1-84: differs — a wrong argument count is accepted (1 2 1 3), the signatures are `:()`, `.line` 0 and `.file` `SETTING::src/core.c/`, `$w1.signature.params[0].name` is Nil, `cando(\(1, 2))` is 1, `.Str` is `"sub { ... }"`, and method calls on a `*`-term are not curried (`(* + 1).arity.^name` is `WhateverCode`, `.^name.WHAT` `(Str)`, `.WHICH.^name` `WhateverCode`).

### CO-36  Code in a positional subscript                                D:partial R:partial V:quirk
In `@a[…]` a WhateverCode, Block or Sub is called with the element
count once per parameter (`* - 1` gets 3, `* - *` and `{ $^a - $^b }` get
3, 3, a four-parameter block four 3s) and the result is the index; `*`
alone is every element; a negative result is a `Failure`
`X::OutOfRange`, `[*+0]` past the end `Nil`; a list of them or a range
of them (`[(* - 1, * - 2)]`, `[1 .. * - 1]`, `[0 ..^ * - 1]`, `[* - 1 ..
*]`) slices, `[* - 1 .. * + 1]` pads with `Nil`, `[^*]` is all; `:exists`,
`:k`, `:p`, `:v` take the computed index and `@m[* - 1] = 9` assigns.
Quirks: `[* .. *]` is not a WhateverCode but the Range `-Inf..Inf` and
dies (`X::Numeric::CannotConvert`), `[* - 1 ... *]` is `X::AdHoc`, and a
**hash** subscript does not call the code: `%h{* - 1}` uses its
stringification as the key (with the coercion warning) and answers
`Any`; `%h{*}` is all values.
```
say do { sub R($r) { $r ~~ Failure ?? "Failure:" ~ $r.exception.^name !! $r.raku }; ((1,2,3)[* - 1], (1,2,3)[* - *], (1,2,3)[{ $_ - 1 }], (1,2,3)[{ $^a - $^b }], (1,2,3)[*].raku, (1,2,3)[*.pred], R((1,2,3)[* - 5]), (1,2,3)[-> $a, $b, $c { 0 }], (1,2,3)[*/2], (1,2,3)[* div 2], (1,2,3)[(* - 1, * - 2)].raku, (1,2,3)[^*].raku, (1,2,3)[*-1, *-2].raku, (1,2,3)[* - 1 .. *].raku, (try (1,2,3)[* - 1 ... *].raku) // $!.^name, (1,2,3)[{ $_ }].raku, (1,2,3)[{ 0 }], (1,2,3)[*+0].raku, (1,2,3)[*-3], R((1,2,3)[*-4]), (1,2,3)[* - 1].^name, (1,2,3)[{ ($_ - 1) xx 2 }].raku, (try (1,2,3)[-> $a, $b, $c, $d { 0 }]) // $!.^name, (1,2,3)[sub ($n) { $n - 1 }], ((1,2,3)[* - 1]:exists), ((1,2,3)[*-9]:exists), ((1,2,3)[*-1]:k), ((1,2,3)[* - 1]:p), ((1,2,3)[* - *]:p), [1,2,3][* - 1], (my @arr = 1,2,3)[* - 1], "abc".comb[* - 1], (1..3)[* - 1], (1,2,3)[*-1].WHAT.gist, (1,2,3)[* - 1, * - 2, * - 3].raku, (try (1,2,3)[* .. *].raku) // $!.^name, (1,2,3)[1 .. * - 1].raku, (1,2,3)[0 ..^ * - 1].raku, (try (1,2,3)[* - 1 .. * + 1].raku) // $!.^name, do { my @m = 1,2,3; @m[* - 1] = 9; @m.raku }, do { my @m = 1,2,3; @m[*] = 7, 8; @m.raku }, do { my $w = "quiet"; CONTROL { when CX::Warn { $w = "warned"; .resume } }; my %h = a => 1; %h{* - 1}.raku ~ ":" ~ $w }, (try do { my %h = a => 1; %h{*}.raku }) // $!.^name, (1,2,3)[* - 1]:v, (1,2,3)[{ $_ - 1 }]:k, (1,2,3)[* - 1, * - 3]:exists).join(" ") }
# rakudo 2026.08: 3 1 3 1 (1, 2, 3) 3 Failure:X::OutOfRange 1 2 2 (3, 2) (1, 2, 3) (3, 2) (3,) X::AdHoc Nil 1 Nil 1 Failure:X::OutOfRange Int (3, 3) 1 3 True False 2 2	3 0	1 3 3 c 3 (Int) (3, 2, 1) X::Numeric::CannotConvert (2, 3) (1, 2) (3, Nil, Nil) [1, 2, 9] [7, 8, Any] Any:warned (1,) 3 2 True True
```
rakupp 4.0.1-84: differs — `[* - *]` is empty, `[{ ($_ - 1) xx 2 }]` and `[{ $_ - 1 }]:k` give 1 and 0, `[* - 1 ... *]` is `()`, `[* .. *]` is `(3,)`, and the hash subscript is quiet.

### CO-37  ForeignCode and a native sub                                  D:partial R:no V:bug
`ForeignCode` is a Callable that is not a Code, marked implementation
detail (`.^is-implementation-detail` 1, 0 for `Int`); `ForeignCode.new`
works; instances appear among a routine's own methods (`&f.^methods`
holds `ForeignCode`, `Method`, `Method+{is-implementation-detail}`,
`Submethod`). An instance answers `.name` `<anon>`, `.arity` 0, `.count`
`Inf` (also on the type object), `.signature` `:(|)` (on the type object
`X::Parameter::InvalidConcreteness`), `.has-phasers` False, `.raku`
`ForeignCode.new`, `.WHICH` an `ObjAt`, and has no `.file`, `.line`,
`.multi`, `.package`. Bug: `.gist` is `ForeignCode.new` and `.Str`
`ForeignCode<addr>` where its docs promise the name. Setting operators
are `Sub+{is-pure}` with `.file` `SETTING::src/core.c/Numeric.rakumod`;
`&say` has 3 candidates. A `NativeCall` sub is a
`Sub+{Callable[NativeCall::Types::size_t]}+{NativeCall::Native[…]}` with
signature `:(Str $ --> NativeCall::Types::size_t)`, `.returns`
`NativeCall::Types::size_t`, `cando` by type, and a wrong argument type
at the call is `X::AdHoc`.
```
say do { sub f() { }; my @m = &f.^methods.grep({ $_ ~~ ForeignCode }); (ForeignCode.^name, (ForeignCode ~~ Callable), (ForeignCode ~~ Code), @m.elems > 0, (try ForeignCode.^is-implementation-detail) // $!.^name, (try Int.^is-implementation-detail) // $!.^name, ForeignCode.DEFINITE, (try ForeignCode.new.^name) // $!.^name, &f.^methods.map(*.^name).unique.sort.raku, &say.^name, &say.candidates.elems, &infix:<+>.^name, &infix:<+>.file, &infix:<+>.line.^name, &say.file, &say.candidates[0].file, (try ForeignCode.signature) // $!.^name, (try ForeignCode.arity) // $!.^name, (try ForeignCode.count) // $!.^name, (@m ?? @m[0].name !! "none"), (@m ?? @m[0].^name !! "none"), (@m ?? (@m[0] ~~ Callable) !! "none"), (try @m[0].arity) // $!.^name, (try @m[0].count) // $!.^name, (try @m[0].signature.raku) // $!.^name, (try @m[0].gist) // $!.^name, (try @m[0].Str.subst(/\d+/, "N")) // $!.^name, (try @m[0].raku.substr(0, 16)) // $!.^name, (try @m[0].has-phasers) // $!.^name, (try @m[0].file) // $!.^name, @m.map(*.name).unique.sort.raku, (@m ?? @m[0].name.raku !! "none"), (try @m[0].WHICH.^name) // $!.^name, (try @m[0].defined) // $!.^name, (try @m[0].multi) // $!.^name, (try @m[0].package) // $!.^name, (try @m[0].line) // $!.^name).join(" ") }
# rakudo 2026.08: ForeignCode True False True 1 0 False ForeignCode ("ForeignCode", "Method", "Method+\{is-implementation-detail}", "Submethod").Seq Sub 3 Sub+{is-pure} SETTING::src/core.c/Numeric.rakumod Int SETTING::src/core.c/io_operators.rakumod SETTING::src/core.c/io_operators.rakumod X::Parameter::InvalidConcreteness 0 Inf <anon> ForeignCode True 0 Inf :(|) ForeignCode.new ForeignCode<N> ForeignCode.new False X::Method::NotFound ("<anon>",).Seq "<anon>" ObjAt True X::Method::NotFound X::Method::NotFound X::Method::NotFound
use NativeCall; sub strlen(Str --> size_t) is native { * }; my $one = 1; say (&strlen.^name, &strlen.name, strlen("abcd"), &strlen.signature.raku, &strlen.arity, &strlen.count, &strlen.gist, (&strlen ~~ Sub), (&strlen ~~ Callable), &strlen.multi, &strlen.is_dispatcher, &strlen.package.^name, &strlen.file.subst(/.*'/'/, ''), &strlen.line, &strlen.signature.params[0].raku, &strlen.returns.^name, &strlen.of.^name, (&strlen ~~ Callable[NativeCall::Types::size_t]), &strlen.raku.subst(/'#`(' <-[)]>* ')'/, '#`(…)'), &strlen.yada, &strlen.candidates.elems, &strlen.cando(\("x")).elems, &strlen.cando(\(1)).elems, (try strlen($one)) // $!.^name).join(" ")
# rakudo 2026.08: Sub+{Callable[NativeCall::Types::size_t]}+{NativeCall::Native[Sub+{Callable[NativeCall::Types::size_t]},Str]} strlen 4 :(Str $ --> NativeCall::Types::size_t) 1 1 &strlen True True 0 False GLOBAL -e 1 Str $ NativeCall::Types::size_t NativeCall::Types::size_t True sub strlen (Str $ --> NativeCall::Types::size_t) { #`(…) ... } False 1 1 0 X::AdHoc
```
rakupp 4.0.1-84: differs — no ForeignCode instances exist (`&f.^methods` is empty, `ForeignCode ~~ Callable` False, `.arity`/`.count`/`.^is-implementation-detail` missing), `&say` has 1 candidate, setting files print as `SETTING::src/core.c/`, and the native sub is a plain `Sub` with `:(Str --> size_t)`, `~~ Callable[…]` False and no type check on the call (`strlen(1)` is 1).

## Counts

| | items |
|---|---|
| total | 37 |
| not fully stated by docs (D:yes) nor asserted by Roast (R:yes) | 24 |
| Rakudo bugs (do not imitate) | 8 — CO-10 `.multi` answers 0 and `.dispatcher` is an NQPMu, CO-19 the `$::` invocant spelling, CO-20 `.prec("prec")` fails its own return check, CO-21 `Int:D(Any):D` and `Code.new` do not re-parse, CO-25 a runtime Signature cannot bind a Capture, CO-26 `Parameter.new(name => '+@a')` throws X::OutOfRange, CO-35 `.file` of a WhateverCode is a null Str, CO-37 ForeignCode gist/Str against its docs |
| quirks (recorded, step two decides) | 11 — CO-05 `cando` ignores an unexpected named, CO-07 the `$_` parameter's default is a type object, CO-09 `return` keeps the container, CO-11 `Int:D` vs `Int` and coercion candidates are Ambiguous and an onlystar proto's types are unchecked, CO-12 `nextsame` in a pointy-block wrapper, CO-14 `assumed.f` and container capture, CO-23 a failing coercion still binds, CO-28 `where` literals are wrapped in Blocks, CO-30 a failed coercion binds a Failure and `Int() $x is rw` cannot assign, CO-33 a role attribute's package is `$?CLASS`, CO-36 `[* .. *]` is `-Inf..Inf` and a hash subscript stringifies the code |
| rakupp 4.0.1-84 differs | 37 |
| rakupp 4.0.1-84 matches | 0 |

Recurring rakupp gaps, for step two, largest first. **Missing
introspection methods kill whole lines**: `has-phasers`/`phasers`
(CO-08, CO-17), `twigil` (CO-26, CO-27), `constraint_list` (CO-28),
`required`/`is_bound`/`inlined` (CO-32, CO-33, CO-34), `is-wrapped`/`soft`
(CO-12, CO-13), `onlystar` (CO-04), `is_dispatcher` on a candidate's
`.dispatcher` (CO-10), `is-item`/`onearg`/`untyped`, `coerce_type`,
`nominal_type`, `signature_constraint`, `has_returns`, `is-pure` and
the other trait predicates, `is-test-assertion`, `DEPRECATED`, the
operator properties. **Exceptions lack their attributes**:
`X::TypeCheck::Return.operation`, `X::TypeCheck::Binding::Parameter.got`,
`X::Parameter::RW.got`, `X::Multi::NoMatch.dispatcher/.capture`,
`X::ControlFlow::Return.out-of-dynamic-scope`, `X::Cannot::New.class`,
and a statement-form `try` does not set `$!`. **The binder is lenient**:
wrong arity and unexpected nameds pass silently, `is rw` takes
literals, `:D`/`:U` are not checked on binding or in `cando`, `--> Int:D`
and type objects pass the return check, `Int(Str)` coerces a Rat, and
no ambiguity is ever reported. **Printing**: a Parameter's `.raku` is a
hash dump; a Signature's drops sub-signatures, `is raw`/`is item`,
literals, `::T`, coercion sources, `&c:(…)` and `;;`; a routine's
`.raku` is `sub { ... }`; the invocant and `*%_` are absent from method
signatures; `.file` of user code is `SETTING::src/core.c/`. **Type
hierarchy**: every code type's `.^mro` is `(Any, Mu)`, no `Callable[T]`
mixin exists, and no trait mixes a role in. **Containers**: no call
returns a container (`is rw`, `is raw`, `return-rw`, a Block), slurpy
elements are bare values, `is copy` gives no fresh Scalar, and a
primed `is rw` argument is not written through. **Currying**: method
calls on a `*`-term are not curried. Smaller: the arity of a defaulted
positional, the invocant not counted, `Int:D()` spelling, phaser lists,
export tags and clashes, `is default`, `is item` dispatch, the DEPRECATED
report, `[* .. *]`.

## Method (how this sheet was produced)

As for [Supply.md](Supply.md), three probe rounds plus ten isolation
runs. The probe harness is `run.sh` (one probe per line, `bash`, a
fresh sandbox per engine and probe, `alarm 10`, `head -6`, `tr '\n'
'|'`), `tsv.sh` and a field-by-field differ. Traps met, in the order
they cost time: a method called on a `*`-expression is **curried**
(`(* + 1).arity` is a WhateverCode), so every WhateverCode was put in a
variable first; `try { $^a }` and `try {;}` parse as a try-**block**, so
such calls were written `(try ({ $^a })())`; `4 ~~ * > 3` curries the
whole smartmatch; a `nextsame` in a pointy-block wrapper throws; a
wrapper that calls its own wrapped routine by name recurses forever
(the alarm killed it); a required parameter after a slurpy, `is item`
on a `$`, `is rw` on an optional or `@` parameter, a typed slurpy, `:$n
is rw` are all compile-time errors that kill the line; Rakudo's
compile-time "will never work" check rejects `ty("s")`, `bb(1)`,
`strlen(1)` and `ty(Nil)` for a named sub, so bad arguments were passed
through variables or a `|(Nil,)` slip; `fail` in a block with no
routine throws; `sub (--> Nil) { return 5 }` is a compile error;
`$b.package` on a Block is a method-not-found; `.dispatcher` of a
non-multi is an NQPMu whose `.raku` dies even inside `try` in one
position; `start { return }` is a MoarVM panic (not recorded); `(1,2,3)[*
.. *]` dies; `Parameter.new(name => '+@a')` dies; `%h{* - 1}` warns
(captured with `CONTROL`); `List[* - 1]` dies; a literal `1; 2` or
`LEAVE 1` warns "useless use"; a `where { 1 }` block's `.raku`, a
`Parameter.Str` and a `ForeignCode.Str` carry object addresses (replaced
by `N` or mapped to `.^name`); a Seq of `.phasers('LEAVE')` calls that
return containers (`$x = 100`) is read at join time, which masked the
phaser order until distinct counters were used; the zsh `echo` used to
display outputs eats `\f` and `\x` (the files were intact; `printf
'%s'` was used from then on); rakupp's own warnings can push a line's
result past `head -6` (CO-07); and an `import` inside an EVAL installs
into the shared outer scope, so import probes need unique names. D
flags from `doc/Type/{Code,Block,Routine,Signature,Parameter,Attribute,
WhateverCode,ForeignCode,Sub,Method,Submethod,Callable}.rakudoc`,
`doc/Type/X/TypeCheck/Return.rakudoc` and
`doc/Language/{functions,signatures,traits,objects,mop,subscripts}.rakudoc`;
R flags from the eight Roast directories named in the second paragraph,
read by grep for each feature, with `S03-smartmatch`, `S06-multi`,
`S09-subscript`, `S10-packages` and `S02-types/whatever.t` noted as
outside the area where they cover an item.
