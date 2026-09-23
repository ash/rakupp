# Grammar, Match, Regex — semantics sheet

Provenance: Rakudo tag `2026.08`, files `src/core.c/Grammar.rakumod` (47
lines), `Match.rakumod` (486), `Cursor.rakumod` (3) and `Regex.rakumod`
(163) read in full, plus the `Capture` methods a Match inherits
(`Capture.rakumod`) and, in `Str.rakumod`, every candidate that takes a
Regex or that a Regex argument falls into: `match` and its helpers,
`subst`, `subst-mutate`, `comb`, `split`, `trans`, `contains`, `index`,
`substr`. Out of scope: the regex syntax itself (Roast S05 asserts it) and
the regex engine; only what a script can observe of the objects and the
parse protocol is recorded. Oracle: Homebrew Rakudo v2026.08 on macOS.
Compared against Raku++ 4.0.1-84-ga4291988 (build-arm64, 2026-09-23).
Format and legend: [README.md](README.md).

Where this sits against the declared spec: Rakudo passes all 11
`S05-grammar` files, all 8 `S05-match`, 7 of 8 `S05-capture`, all 28
`S05-modifier`, both `S05-interpolation`, all 3 `S05-substitution` and all
4 `S05-metachars` files, plus `S32-str/comb.t` and `split.t` (it does not
pass `S32-str/match.t`, `subst.t`, `trans.t` or `S05-nonstrings/basic.t`);
Raku++ passes 5, 5, 1, 13, 0, 3 and 2 of those groups and neither Str
file. The rules below are the object protocol behind those files: what
`.parse` and `.subparse` return and set, how an actions object is driven,
what a Match answers, how captures nest, what `$/` holds where, how a
Regex behaves as a value, and what the Str routines that take a Regex
return. 23 of the 41 items are neither fully documented nor fully asserted
by Roast. Five Rakudo behaviours are recorded as bugs and six as quirks;
step two should not imitate the bugs.

Every probe ran with `alarm 10` and standard input closed, in a fresh
sandbox directory per engine (GM-05 writes two small files there). A
multi-line output has its newlines turned into `|` by the harness, so a
Match gist reads `｢ab｣| 0 => ｢a｣`; the `｢ ｣` brackets are UTF-8 and stable.
Where a probe line exercises neighbouring items, the statement says which
fields matter.

## A. Grammar: the parse protocol

### GM-01  parse: the whole string, Nil, `$/`, and what comes back        D:partial R:yes V:spec
`G.parse($text)` runs `TOP` from position 0 and succeeds only when the match
ends at the last character; otherwise it returns Nil. Success returns the
Match and binds the caller's `$/` to it; failure sets the caller's `$/` to
Nil. The Match is an instance of the grammar itself: `.^name` is `G`, it
smartmatches `G`, `Grammar` and `Match`, its MRO is G, Grammar, Match,
Capture, Cool, Any, Mu, and its `.WHICH` is an `ObjAt` (a Match is not a
value type). `G.new.parse` behaves like `G.parse`; `""` against `a+` is
Nil.
```
grammar G { token TOP { a+ } }; my $m = G.parse("aaa"); say $m.^name, " ", $m.Str, " ", $/.raku, " ", G.parse("aab").raku, " ", $/.raku, " ", G.new.parse("aa").Str, " ", G.parse("").raku, " ", (G.parse("aaa") ~~ Match), " ", (G.parse("aaa") ~~ G), " ", (G.parse("aaa") ~~ Grammar), " ", G.^mro.map(*.^name).join(","), " ", G.parse("aaa").WHICH.^name, " ", G.parse("aaa").^name, " ", $/.^name
# rakudo 2026.08: G aaa Match.new(:orig("aaa"), :from(0), :pos(3)) Nil Nil aa Nil True True True G,Grammar,Match,Capture,Cool,Any,Mu ObjAt G G
```
rakupp 4.0.1-84: differs — the result is a plain `Match` (`.^name` Match, `~~ G` and `~~ Grammar` False) with a `ValueObjAt` WHICH.

### GM-02  subparse never returns Nil; the failed Match                     D:partial R:partial V:quirk
`subparse` returns the first `TOP` match without requiring it to reach the
end (`.Str` "aa", `.to` 2 on "aab"). When `TOP` fails it returns a false
Match: `.Bool` False, `.gist` `#<failed match>`, `.Str` "", `.chars` 0,
`.from` 0, and `.to` and `.pos` NEGATIVE (−3 in 2026.08; the value is an
implementation detail, so test `.Bool`, never the number), which its
`.raku` prints as `:pos(-3)` — the quirk. The caller's `$/` is set to that
failed Match, so `$/` is False but not Nil. `:pos(n)` starts both `parse`
and `subparse` at n (`.from` n, `.prematch` the skipped prefix) and `parse`
still has to reach the end. `subparse("")` against `a+` is a failed Match;
`parse("")` is Nil.
```
grammar G { token TOP { a+ } }; my $m = G.subparse("aab"); say $m.Str, " ", $m.Bool, " ", $m.from, " ", $m.to, " ", $m.pos, " ", $m.^name, " | ", G.subparse("bbb").gist, " ", G.subparse("bbb").Bool, " ", G.subparse("bbb").^name, " ", G.subparse("bbb").from, " ", G.subparse("bbb").to, " ", G.subparse("bbb").pos, " ", G.subparse("bbb").raku, " ", G.subparse("bbb").Str.raku, " ", G.subparse("bbb").chars, " | ", G.subparse("bbaa", :pos(2)).Str, " ", G.subparse("bbaa", :pos(2)).from, " ", G.parse("bbaa", :pos(2)).Str, " ", G.parse("bbaa", :pos(2)).from, " ", G.parse("bbaa", :pos(2)).prematch, " ", G.parse("bbaab", :pos(2)).raku, " ", do { G.subparse("bbb"); $/.Bool ~ " " ~ $/.^name }, " ", do { G.subparse("aab"); $/.Str }, " ", G.subparse("").Bool, " ", G.subparse("").gist, " ", G.parse("").raku
# rakudo 2026.08: aa True 0 2 2 G | #<failed match> False G 0 -3 -3 Match.new(:orig("bbb"), :from(0), :pos(-3)) "" 0 | aa 2 aa 2 bb Nil False G aa False #<failed match> Nil
```
rakupp 4.0.1-84: differs — a failed `subparse` is Nil (so `$/` is Nil and every field after it reads Nil), and `:pos` is not honoured (Nil for every `:pos(2)` field).

### GM-03  :rule and :args                                                 D:partial R:partial V:spec
`:rule<name>` starts at that rule; the result is still an instance of the
grammar. `:args` takes a Capture or anything with a `.Capture` — a
one-element list `("c",)` and `\("x","y")` both work, a bare Str is
`X::Cannot::Capture`; its positionals go to the rule's signature and extra
named arguments in it are ignored; too few or too many positionals
(including any for a `TOP` that takes none) is an `X::AdHoc` arity error.
A missing rule, and a grammar without `TOP`, are `X::Method::NotFound`.
`:pos` combines with `:rule`; `subparse` with `:args` stops early ("cc").
```
grammar G { token TOP { a+ }; token b { b+ }; token n($c) { $c+ }; token two($x, $y) { $x $y } }; say G.parse("bb", :rule<b>).Str, " ", G.parse("bb", :rule<b>).^name, " ", G.parse("ccc", :rule<n>, :args(\("c"))).Str, " ", G.parse("ccc", :rule<n>, :args(("c",))).Str, " ", G.parse("xy", :rule<two>, :args(("x","y"))).Str, " ", G.parse("xy", :rule<two>, :args(\("x","y"))).Str, " ", G.subparse("ccd", :rule<n>, :args(\("c"))).Str, " ", (try G.parse("x", :rule<nope>)) // $!.^name, " ", (try grammar { token a { a } }.parse("a")) // $!.^name, " ", (try G.parse("ccc", :rule<n>)) // $!.^name, " ", (quietly try G.parse("ccc", :rule<n>, :args("c")).Str) // $!.^name, " ", G.parse("bb", :rule("b")).Str, " ", (try G.parse("x", :rule<TOP>, :args(\(1)))) // $!.^name, " ", G.parse("aab", :rule<b>, :pos(2)).Str, " ", (try G.parse("ccc", :rule<n>, :args(\(:c("c")))).raku) // $!.^name, " ", G.parse("ccc", :rule<n>, :args(\("c", :z(1)))).Str
# rakudo 2026.08: bb G ccc ccc xy xy cc X::Method::NotFound X::Method::NotFound X::AdHoc X::Cannot::Capture bb X::AdHoc b X::AdHoc ccc
```
rakupp 4.0.1-84: differs — `:args` is ignored (every rule with a parameter answers Nil), a missing rule and a missing `TOP` answer Nil instead of throwing, and `:pos` is ignored.

### GM-04  The target: any object, `.orig` keeps it, `.target` is the Str    D:partial R:partial V:spec
Any object may be parsed: the rules run against its Str, `.orig` keeps the
original object (an Int stays an Int and `.raku` prints `:orig(12)`
unquoted) and `.target` is the Str. `G.parse(12.5)` against `\d+` is Nil;
a List parses its `.Str` ("1 2", Nil here). A Str type object parses as
Nil (with an "uninitialized value" warning); `Any` is `X::Method::NotFound`,
`Nil` `X::AdHoc`, `Int` `X::Multi::NoMatch`. Numeric methods go through the
matched text: `.Int` 12, `.Numeric` an Int, `+ 1` 13.
```
grammar G { token TOP { \d+ } }; my $m = G.parse(123); say $m.Str, " ", $m.orig.^name, " ", $m.orig, " ", $m.target.^name, " ", $m.target, " ", G.parse(12.5).raku, " ", G.parse("12").orig.^name, " ", (quietly { G.parse(Str) }).raku, " ", (quietly { (try G.parse(Any)) // $!.^name }), " ", G.parse(1e3).Str, " ", (try G.parse((1,2)).raku) // $!.^name, " ", (quietly { (try G.parse(Nil).raku) // $!.^name }), " ", G.subparse(42).orig.^name, " ", (quietly { (try G.parse(Int).raku) // $!.^name }), " ", G.parse(12).Int + 1, " ", G.parse(12).Numeric.^name, " ", G.parse(12).Int.^name, " ", (G.parse(12) + 1), " ", G.parse(12).raku, " ", G.parse(12).gist, " ", G.parse(12).prematch.raku, " ", G.parse(<1 2>).raku
# rakudo 2026.08: 123 Int 123 Str 123 Nil Str Nil X::Method::NotFound 1000 Nil X::AdHoc Int X::Multi::NoMatch 13 Int Int 13 Match.new(:orig(12), :from(0), :pos(2)) ｢12｣ "" Nil
```
rakupp 4.0.1-84: differs — `.orig` is always the Str (`:orig("12")`), and the `Any`, `Nil` and `Int` type objects give Nil instead of throwing.

### GM-05  parsefile                                                        D:yes R:yes V:spec
`parsefile($name, :enc)` slurps the file (a Str or an IO::Path; the
trailing newline is kept) and hands the text to `parse`, passing `:rule`,
`:actions`, `:pos` and any other named argument through; the caller's `$/`
is set, `.orig` is the file text, and a missing file throws `X::AdHoc`.
```
grammar G { token TOP { \w+ \n? } }; "pf.txt".IO.spurt("hello\n"); my $m = G.parsefile("pf.txt"); say $m.Str.raku, " ", $/.Str.raku, " ", $m.orig.raku, " ", $m.^name, " ", G.parsefile("pf.txt", :rule<TOP>).Str.raku, " ", (try G.parsefile("missing.txt")) // $!.^name, " ", G.parsefile("pf.txt", :enc<utf8>).Str.raku, " ", G.parsefile("pf.txt".IO).Str.raku, " ", G.parsefile("pf.txt", :actions(class { method TOP($/) { make "made" } })).made, " ", do { "pf2.txt".IO.spurt("hi there"); G.parsefile("pf2.txt").raku }, " ", $/.raku, " ", G.parsefile("pf2.txt", :pos(3)).Str
# rakudo 2026.08: "hello\n" "hello\n" "hello\n" G "hello\n" X::AdHoc "hello\n" "hello\n" made Nil Nil there
```
rakupp 4.0.1-84: differs — a missing file answers Nil instead of throwing, `:pos` is ignored (Nil), and the result is a plain `Match`.

### GM-06  parse retries into TOP; a token does not backtrack               D:partial R:no V:spec
When `TOP` matches without reaching the end, `parse` asks that match for
another way to match before giving up. Whether that yields anything depends
on the rule's ratchet: `regex TOP { a || aa }` parses "aa" (the first
alternative matched "a", the retry took the second) and `regex TOP { a+? }`
parses "aa", while the same bodies as a `token` give Nil, because a token's
alternatives and quantifiers do not backtrack once matched. `subparse`
never retries ("a" in all four cases). `token TOP { a | aa }` parses "aa"
because `|` picks the longest alternative first, and `token TOP { a* }`
parses "" (a zero-width whole-string match succeeds).
```
grammar G { token TOP { a || aa } }; grammar R { regex TOP { a || aa } }; grammar T { token TOP { a+? } }; grammar U { regex TOP { a+? } }; grammar V { token TOP { a+ a } }; grammar W { regex TOP { a+ a } }; grammar X { token TOP { a | aa } }; grammar Y { token TOP { a* } }; say G.parse("aa").raku, " ", G.subparse("aa").Str, " ", R.parse("aa").Str, " ", R.subparse("aa").Str, " ", T.parse("aa").raku, " ", T.subparse("aa").Str, " ", U.parse("aa").Str, " ", U.subparse("aa").Str, " ", V.parse("aaa").raku, " ", W.parse("aaa").Str, " ", X.parse("aa").Str, " ", X.subparse("aa").Str, " ", Y.parse("aab").raku, " ", Y.subparse("aab").Str, " ", Y.subparse("aab").pos, " ", Y.parse("").Str.raku, " ", Y.parse("").Bool
# rakudo 2026.08: Nil a aa a Nil a aa a Nil aaa aa aa Nil aa 2 "" True
```
rakupp 4.0.1-84: differs — `token TOP { a || aa }` and `token TOP { a+? }` both parse "aa": the retry backtracks into a token.

### GM-07  Attributes and named arguments to parse                          D:partial R:no V:quirk
A grammar may declare attributes. Named arguments to `.parse` other than
`:rule`, `:args`, `:actions` and `:pos` initialise them for that parse:
inside a rule `self.n` sees the value; an unknown name is ignored. An
instance's own attributes are NOT used by `.parse` (`G.new(:n(7)).parse`
sees the default 0), and the returned Match's public attributes are
undefined (`$m.n` is Any, `$m.seen` empty) even when a value was passed —
that is the quirk. `.^attributes` of a grammar lists its own attributes and
then Match's private ones. `G.new` is a true empty Match (gist `｢｣`, Str
"", from 0, pos 0).
```
grammar G { has $.n = 0; has @.seen; token TOP { a { $*r = self.n; self.seen.push("s") } } }; my $*r; G.parse("a", :n(5)); print $*r, " "; G.new(:n(7)).parse("a"); print $*r, " "; G.parse("a"); print $*r, " "; my $m = G.parse("a", :n(3)); say $m.n, " ", $m.seen.raku, " ", $m.^attributes.map(*.name).raku, " ", G.new(:n(1)).n, " ", (try G.parse("a", :nope(1)).Str) // $!.^name, " ", G.new.^name, " ", G.new.Bool, " ", G.new.Str.raku, " ", G.new.orig.raku, " ", G.new.from, " ", G.new.pos, " ", G.new.gist
# rakudo 2026.08: 5 0 0 (Any) [] ("\$!n", "\@!seen", "\$!from", "\$!pos", "\$!to", "\$!shared", "\$!braid", "\$!bstack", "\$!cstack", "\$!regexsub", "\$!restart", "\$!made", "\$!match", "\$!name", "\@!list", "\%!hash").Seq 1 a G True "" "" 0 0 ｢｣
```
rakupp 4.0.1-84: differs — `self` inside a rule's code block is a compile-time `X::Syntax::Self::WithoutObject`, so the line died before running; unmeasured.

### GM-08  A rule is a Regex method; calling it on a Match                  D:no R:no V:spec
Every `token`, `rule` and `regex` of a grammar is a Method of type Regex
with signature `:(G $:: *%_)` (arity 1, count 1) and `.name` the rule
name; `G.^methods` lists them and `&G::TOP` is not a sub (Any). Calling a
rule on the type object, or with a positional (`G.TOP("aaa")`), is an
`X::AdHoc` arity error. Calling it on an instance built with
`G.new(:orig($s))` (optionally `:pos(n)`) runs the rule anchored at that
position and returns the resulting Match, an instance of G: "aaab" gives
`｢aaa｣` with `.pos` 3 (no end anchoring), "baa" a false Match
(`#<failed match>`), and `.made` is Nil. `Match.new(:orig).TOP` is
`X::Method::NotFound`.
```
grammar G { token TOP { a+ }; token b { b } }; say (try G.TOP("aaa").Str) // $!.^name, " ", (try G.TOP.Str) // $!.^name, " ", G.new(:orig("aaa")).TOP.Str, " ", G.new(:orig("aaa")).TOP.^name, " ", G.new(:orig("aaa")).TOP.pos, " ", G.new(:orig("aaab")).TOP.Str, " ", G.new(:orig("baa")).TOP.Bool, " ", G.new(:orig("baa"), :pos(1)).TOP.Str, " ", G.new(:orig("xb")).b.Bool, " ", G.new(:orig("xb"), :pos(1)).b.Str, " ", G.new(:orig("aaa")).TOP.from, " ", G.new(:orig("aaa")).TOP.to, " ", G.new(:orig("aaa")).TOP.orig, " ", G.^methods.map(*.name).grep(/^ TOP | ^ b $/).sort.raku, " ", G.^find_method("TOP").^name, " ", G.^find_method("TOP").signature.raku, " ", G.^find_method("TOP").name, " ", G.^find_method("TOP").arity, " ", G.^find_method("TOP").count, " ", (try &G::TOP.^name) // $!.^name, " ", (try Match.new(:orig("aa")).TOP.raku) // $!.^name, " ", G.new(:orig("aaa")).TOP.gist, " ", G.new(:orig("baa")).TOP.gist, " ", G.new(:orig("aaa")).TOP.made.raku
# rakudo 2026.08: X::AdHoc X::AdHoc aaa G 3 aaa False aa False b 0 3 aaa ("TOP", "b").Seq Regex :(G $:: *%_) TOP 1 1 Any X::Method::NotFound ｢aaa｣ #<failed match> Nil
```
rakupp 4.0.1-84: differs — `G.new(:orig("aaa")).TOP` answers nothing (Nil or empty for every field), `G.^methods` is empty and `^find_method` gives Nil.

### GM-09  Inheritance, roles, explicit `<ws>`, qualified calls              D:partial R:partial V:spec
`grammar B is A` overrides rules by name and inherits the rest, `TOP`
included; the result is a B and smartmatches A; an actions class works
through the inherited `TOP`. A role can supply rules (`grammar E is A does
R`). A `method x { callsame }` in a subgrammar reaches the parent's rule,
and so does the qualified subrule call `<.I::x>`. An explicit `<ws>` (no
dot) is a named capture: two calls give an Array of two Matches, the first
holding the matched blank.
```
grammar A { token TOP { <x> <y> }; token x { a }; token y { b } }; grammar B is A { token x { c } }; grammar C is A { token TOP { <y> <x> } }; say B.parse("cb").Str, " ", B.parse("ab").raku, " ", C.parse("ba").Str, " ", B.^mro.map(*.^name).join(","), " ", B.parse("cb")<x>.Str, " ", B.parse("cb", :actions(class { method x($/) { make "X" }; method TOP($/) { make $<x>.made } })).made, " ", (B ~~ A), " ", B.parse("cb").^name, " ", (B.parse("cb") ~~ A), " ", do { grammar D is A { token x { <.x2> }; token x2 { z } }; D.parse("zb").Str }, " ", grammar { token TOP { <ws> a <ws> b } }.parse(" a b").Str.raku, " ", grammar { token TOP { <ws> a <ws> b } }.parse(" a b")<ws>.elems, " ", grammar { token TOP { <ws> a <ws> b } }.parse(" a b")<ws>[0].Str.raku, " ", do { role R { token y { q } }; grammar E is A does R { }; E.parse("aq").Str }, " ", do { grammar F is A { method x { callsame } }; (try F.parse("ab").Str) // $!.^name }, " ", do { grammar H is A { token x { <.y> } }; H.parse("bb").Str }, " ", do { grammar I { token TOP { <x> }; token x { a } }; grammar J is I { token x { b | <.I::x> } }; J.parse("a").Str ~ J.parse("b").Str }
# rakudo 2026.08: cb Nil ba B,A,Grammar,Match,Capture,Cool,Any,Mu c X True B True zb " a b" 2 " " aq ab bb ab
```
rakupp 4.0.1-84: differs — `<.I::x>` fails (`J.parse("a")` is Nil), an explicit `<ws>` is not captured (`.elems` 0), and the result is a plain Match (`~~ A` False).

### GM-10  Grammar, Match and a grammar's type object                        D:no R:no V:spec
`Grammar`'s MRO is Grammar, Match, Capture, Cool, Any, Mu. `Grammar.parse`
is an `X::AdHoc` and `Match.parse` `X::Method::NotFound`; an empty
`grammar G {}` parses as `X::Method::NotFound` (no `TOP`).
`Grammar.parsefile` with no argument is an `X::AdHoc` arity error. A
grammar type object gists as `(G2)`, is False and has a `ValueObjAt` WHICH;
two parses of the same text are `eqv` but not `===`. `G2.new` is a true
empty Match: gist `｢｣`, `.raku` `Match.new(:orig(""), :from(0), :pos(0))`,
an `ObjAt` WHICH, `.orig` and `.Str` both "".
```
say Grammar.gist, " ", Grammar.^mro.map(*.^name).join(","), " ", (try Grammar.parse("x")) // $!.^name, " ", (try Match.parse("x")) // $!.^name, " ", grammar G {}.gist, " ", (try G.parse("x")) // $!.^name, " ", grammar G2 { token TOP { x } }.parse("x").raku, " ", G2.WHICH.^name, " ", G2.Bool, " ", G2.raku, " ", G2.new.^name, " ", G2.parse("x").^name, " ", G2.subparse("y").^name, " ", (G2.parse("x") ~~ G2), " ", (G2.parse("x") ~~ Grammar), " ", (G2.parse("x") ~~ Match), " ", G2.parse("x").defined, " ", G2.parse("x").WHICH.^name, " ", (G2.parse("x") === G2.parse("x")), " ", (G2.parse("x") eqv G2.parse("x")), " ", (try Grammar.parsefile.^name) // $!.^name, " ", G2.new.gist, " ", G2.new.raku, " ", G2.new.WHICH.^name, " ", (try G2.new.orig.raku) // $!.^name, " ", (try G2.new.Str.raku) // $!.^name
# rakudo 2026.08: (Grammar) Grammar,Match,Capture,Cool,Any,Mu X::AdHoc X::Method::NotFound (G) X::Method::NotFound Match.new(:orig("x"), :from(0), :pos(1)) ValueObjAt False G2 G2 G2 G2 True True True True ObjAt False True X::AdHoc ｢｣ Match.new(:orig(""), :from(0), :pos(0)) ObjAt "" ""
```
rakupp 4.0.1-84: differs — `Grammar.parse` is `X::Method::NotFound` and the empty grammar answers Nil; a failed subparse is Nil; two parses are `===`; `G2.new` is not a Match at all (gist and raku `G2.new`, no `.orig`, `.Str` an address string).

## B. Actions

### GM-11  The actions protocol                                             D:yes R:yes V:bug
`:actions` takes a class or an instance. After every successful named rule
the engine calls the method of that name on the actions object with the
Match as its only positional and no named arguments (`*%o` is empty); a
method that declares no positional is an `X::AdHoc` arity error; a missing
method is skipped. `make` inside the method, or `$m.make` on the parameter,
stores the payload; `.made` and `.ast` read it; a sub-rule's payload is
reachable as `$<a>.made` in a later action (`TOP` runs last). Without
`:actions` nothing is made (Nil). `subparse` drives actions the same way,
and `:rule<a>` drives only the actions of the rules it runs (no `TOP`, so
`.made` of the result is Nil). The caller's `$/` carries the payload.
`.actions` on the Match returns the object given, class or instance, the
instance keeping its state (`.actions.log` reads what the methods pushed).
With no actions the docs promise `Mu`, but 2026.08 answers `NQPMu`, an
object that is not a Raku `Mu` — the bug; answer `Mu` there.
```
grammar G { token TOP { <a> <b> }; token a { a }; token b { b } }; class A { method TOP($/) { make $<a>.made ~ "+" ~ $<b>.made }; method a($/) { make "A" }; method b($/) { make "B" } }; my $m = G.parse("ab", :actions(A)); say $m.made, " ", $m.ast, " ", $m<a>.made, " ", G.parse("ab", :actions(A.new)).made, " ", G.parse("ab").made.raku, " ", G.parse("ab")<a>.made.raku, " ", do { G.parse("ab", :actions(A)); $/.made }, " ", G.subparse("ab", :actions(A)).made, " ", G.parse("ab", :actions(A), :rule<a>).made, " ", G.parse("ab", :actions(class { method a($/) { make 1 }; method b($/) { make 2 } })).made.raku, " ", G.parse("ab", :actions(class { method a($/) { make 1 }; method b($/) { make 2 } }))<b>.made, " ", G.parse("ab", :actions(class { method a($m) { $m.make("viaparam") } }))<a>.made, " ", (try G.parse("ab", :actions(class { method a { make "noparam" } }))) // $!.^name, " ", G.parse("ab", :actions(class { method a($/, *%o) { make %o.keys.raku } }))<a>.made, " ", G.parse("ab", :actions(class { method a($/) { make $/.Str.uc } }))<a>.made, " ", G.parse("ab", :actions(class { method TOP($/) { make $<a>.made.raku } })).made, " ", G.parse("ab", :actions(class { method a($/) { make "A" }; method TOP($/) { make $<a>.ast } })).made, " ", G.parse("ab", :actions(class { method a($/) { make "A" }; method TOP($/) { make $<b>.made.raku } })).made, " ", G.parse("ab", :actions(class { has @.log; method a($/) { @!log.push("a") } }.new)).actions.log.raku; say $m.actions.^name, " ", G.parse("ab").actions.^name, " ", G.parse("ab", :actions(A.new)).actions.^name
# rakudo 2026.08: A+B A+B A A+B Nil Nil A+B A+B Nil Nil 2 viaparam X::AdHoc ().Seq A Nil A Nil ["a"]|A NQPMu A
```
rakupp 4.0.1-84: differs — every field of the first line matches, but the Match has no `.actions` (`X::Method::NotFound`), so the second line died.

### GM-12  make                                                             D:partial R:partial V:spec
`make` binds a payload to the `$/` of the caller; when that `$/` is not a
Match (nothing matched yet, or the last match failed and left Nil) it
throws `X::Make::MatchRequired` whose `.got` is that value. `make` returns
its argument. A `{ make … }` block inside a rule body runs during the
match; an action method's `make` runs after the rule and overrides it,
while an action method that does not `make` leaves the body's payload in
place. A later `make` replaces an earlier one (the last wins); `make Nil`
leaves `.made` Nil; a list is stored as given; `$/.make(v)` is the method
form and returns `v`. After a successful top-level match, a bare `make`
sets `$/.made`.
```
say (try make(1)) // $!.^name ~ ":" ~ $!.got.raku; grammar G { token TOP { a { make "body" } } }; say G.parse("a").made, " ", G.parse("a", :actions(class { method TOP($/) { make "action" } })).made, " ", G.parse("a", :actions(class { method TOP($/) { } })).made, " ", do { "x" ~~ /x { make 5 }/; $/.made }, " ", do { "x" ~~ /x/; make 7; $/.made }, " ", do { "x" ~~ /x/; (make 9) }, " ", do { "x" ~~ /x/; make(9).^name }, " ", do { "x" ~~ /x/; make Nil; $/.made.raku }, " ", do { "x" ~~ /x/; $/.make(11); $/.made }, " ", do { "x" ~~ /x/; $/.make(11).^name }, " ", do { "x" ~~ /x/; make (1, 2); $/.made.raku }, " ", do { "x" ~~ /y/; (try make 3) // $!.^name ~ ":" ~ $!.got.raku }, " ", do { "x" ~~ /x { make "a" } { make "b" }/; $/.made }
# rakudo 2026.08: X::Make::MatchRequired:Nil|body action body 5 7 9 Int Nil 11 Int (1, 2) X::Make::MatchRequired:Nil b
```
rakupp 4.0.1-84: differs — a bare `make` outside a rule never throws (it returns its argument even with `$/` Nil), and `make 7` after a top-level match leaves `$/.made` Nil.

### GM-13  Which subrule calls fire an action                                D:partial R:partial V:spec
An action fires whenever a NAMED rule completes, whether or not its call
captures: `<.a>` fires `a`; the aliased call `<b=.a>` fires `a` (the rule's
name), not `b`, and stores the capture under `b`; a call inside a lookahead
`<?before <d>>` fires `d` too, so `d` fires twice here; `TOP` fires last.
A lexical regex called as `<&e>` or `<e2=&e>` fires the action named `e`
each time; `<&e>` captures nothing, `<e2=&e>` captures under `e2`. `.caps`
lists the captured names in order and `.chunks` fills the gaps with `~`.
```
my @c; grammar G { token TOP { <.a> <b=.a> <c> <?before <d>> <d> }; token a { a }; token c { c }; token d { d } }; class A { method a($/) { @c.push("a") }; method b($/) { @c.push("b") }; method c($/) { @c.push("c") }; method d($/) { @c.push("d") }; method TOP($/) { @c.push("TOP") } }; my $m = G.parse("aacd", :actions(A)); say @c.raku, " ", $m.keys.sort.raku, " ", $m<b>.Str, " ", $m<d>.Str, " ", $m<a>.raku, " ", $m<c>.^name, " ", $m<c>.from, " ", $m.caps.map(*.key).raku, " ", do { @c = (); my regex e { e }; grammar H { token TOP { <&e> <e2=&e> }; }; H.parse("ee", :actions(class { method e($/) { @c.push("e") }; method e2($/) { @c.push("e2") } })).keys.sort.raku ~ @c.raku }; say $m.chunks.map({ .key ~ "=" ~ .value }).raku
# rakudo 2026.08: ["a", "a", "c", "d", "d", "TOP"] ("b", "c", "d").Seq a d Nil G 2 ("b", "c", "d").Seq ("e2",).Seq["e", "e"]|("~=a", "b=a", "c=c", "d=d").Seq
```
rakupp 4.0.1-84: differs — `<b=.a>` fires no action (`a` fires once), the result is a plain Match, and `.chunks` does not exist (second line died).

### GM-14  Proto tokens                                                     D:yes R:yes V:spec
A `proto token op {*}` dispatches to its `op:sym<…>` candidates by
longest-token matching, declaration order breaking ties (`op:sym<a>`
matching `a` beats `op:sym<b>` matching `'a'`; `op:sym<++>` beats
`op:sym<+>` on "++"); a candidate whose body fails after the longest prefix
(`x <?{ False }>`) falls through to the next. `<sym>` inside a candidate is
a named capture `sym` holding the symbol; a candidate without `<sym>` has
no captures. The action method of the CANDIDATE (`op:sym<+>`) fires; the
proto's own action (`op`) never does. `<op>+` gives an Array of the
candidates' Matches. `:rule<op>` and `:rule("op:sym<+>")` both work (a
missing candidate is `X::Method::NotFound`), and `^methods` lists the proto
and its candidates by their full names. The gist nests: `｢++｣| op => ｢++｣|
sym => ｢++｣`.
```
my @c; grammar G { token TOP { <op>+ }; proto token op {*}; token op:sym<+> { <sym> }; token op:sym<++> { <sym> }; token op:sym<a> { a }; token op:sym<b> { 'a' } }; class A { method op($/) { @c.push("op") }; method op:sym<+>($/) { @c.push("+") }; method op:sym<++>($/) { @c.push("++") }; method op:sym<a>($/) { @c.push("a") }; method op:sym<b>($/) { @c.push("b") } }; my $m = G.parse("++a", :actions(A)); say @c.raku, " ", $m<op>.elems, " ", $m<op>[0]<sym>.Str, " ", $m<op>[0].Str, " ", $m<op>[1].keys.raku, " ", $m<op>[0].keys.raku, " ", $m<op>.^name, " ", $m<op>[0].^name, " ", G.parse("+", :rule<op>).Str, " ", G.parse("+", :rule<op>).keys.raku, " ", G.parse("++", :rule<op>)<sym>.Str, " ", G.parse("++").gist.subst("\n", "|", :g), " ", do { grammar H { token TOP { <op> }; proto token op {*}; token op:sym<x> { x }; token op:sym<y> { xy } }; H.parse("xy")<op>.Str }, " ", do { grammar I { token TOP { <op> }; proto token op {*}; token op:sym<y> { xy }; token op:sym<x> { x } }; I.subparse("xy")<op>.Str }, " ", do { grammar J { token TOP { <op> }; proto token op {*}; token op:sym<x> { x <?{ False }> }; token op:sym<y> { x } }; J.parse("x")<op>.Str }, " ", do { grammar K { token TOP { <op>+ }; proto token op {*}; token op:sym<a> { <sym> }; token op:sym<b> { <sym> } }; K.parse("ab")<op>.map(*<sym>.Str).raku }, " ", G.parse("++").raku
# rakudo 2026.08: ["++", "a"] 2 ++ ++ ().Seq ("sym",).Seq Array G + ("sym",).Seq ++ ｢++｣| op => ｢++｣|  sym => ｢++｣ xy xy x ("a", "b").Seq Match.new(:orig("++"), :from(0), :pos(2), :hash(Map.new((:op([Match.new(:orig("++"), :from(0), :pos(2), :hash(Map.new((:sym(Match.new(:orig("++"), :from(0), :pos(2)))))))])))))
grammar G { token TOP { <op> }; proto token op {*}; token op:sym<+> { <sym> }; token op:sym<-> { <sym> } }; say G.parse("+", :rule("op:sym<+>")).Str, " ", (try G.parse("+", :rule("op:sym<*>"))) // $!.^name, " ", G.parse("-", :rule("op:sym<->")).keys.raku, " ", G.^methods.map(*.name).grep(*.starts-with("op")).sort.raku, " ", G.parse("+")<op>.keys.raku, " ", G.parse("+")<op><sym>.^name
# rakudo 2026.08: + X::Method::NotFound ("sym",).Seq ("op", "op:sym<+>", "op:sym<->").Seq ("sym",).Seq G
```
rakupp 4.0.1-84: differs — `<op>+` gives a List (`.raku` shows `:op((…,))`) rather than an Array, the result is a plain Match, and `:rule("op:sym<+>")` answers Nil while `^methods` lists nothing.

### GM-15  Actions fire on abandoned branches and failed parses; exceptions propagate   D:no R:no V:spec
A sub-rule's action fires as soon as that rule succeeds, even when the
enclosing rule later fails: `parse("ac")` against `<a> b` returns Nil with
`a`'s action fired, and a failed `subparse` likewise. A `||` alternation
fires `a` once per alternative tried (twice for `<a> x || <a> y` on "ay");
a `|` alternation fires only for the alternative chosen; a `regex TOP {
<a>+ a }` on "aaa" fires `a` three times and backtracking does not re-fire
it. An exception thrown in an action method propagates out of `.parse` as
that exception (`X::AdHoc` "boom").
```
my @c; grammar G { token TOP { <a> b }; token a { a } }; class A { method TOP($/) { @c.push("TOP") }; method a($/) { @c.push("a") } }; say G.parse("ac", :actions(A)).raku, " ", @c.raku, " ", do { @c = (); G.parse("ab", :actions(A)); @c.raku }, " ", do { @c = (); G.subparse("ac", :actions(A)).Bool ~ @c.raku }, " ", (try G.parse("ab", :actions(class { method a($/) { die "boom" } }))) // $!.^name ~ ":" ~ $!.message, " ", (try G.parse("ab", :actions(class { method TOP($/) { die "top" } }))) // $!.message, " ", do { @c = (); grammar H { token TOP { <a>+ x | <a>+ y }; token a { a } }; H.parse("aay", :actions(A)); @c.raku }, " ", do { @c = (); grammar I { token TOP { <a> x || <a> y }; token a { a } }; I.parse("ay", :actions(A)); @c.raku }, " ", do { @c = (); grammar J { regex TOP { <a>+ a }; token a { a } }; J.parse("aaa", :actions(A)); @c.raku }
# rakudo 2026.08: Nil ["a"] ["a", "TOP"] False["a"] X::AdHoc:boom top ["a", "a", "TOP"] ["a", "a", "TOP"] ["a", "a", "a", "TOP"]
```
rakupp 4.0.1-84: differs — actions of a parse that fails are not fired (`[]`), `<a> x || <a> y` fires `a` once, and the backtracking case fires it twice.

## C. The Match object

### GM-16  The Match surface                                                D:yes R:partial V:spec
For `"abc123def" ~~ /\d+/`: `.Str` and `~` give the text, `.Bool`/`?`/`so`
True, `.from` 3, `.to` 6, `.pos` 6, `.orig` and `.target` the whole string
(both Str here), `.chars` 3, `.prematch` "abc", `.postmatch` "def"; `.Int`,
`+` and `.Numeric` numify the text (an Int here; a Failure for
non-numeric text, `("abc" ~~ /b/).Int.^name` is Failure); `eq` and `==`
compare through Str and Numeric; `+ 1`, `~ "!"`, `.uc` work as on a Cool.
With no captures `.elems` is 0, `.list` `()`, `.hash` an empty `Map`,
`.keys` empty, `.caps` `()` and `.chunks` one `"~" => text` pair. `.made`
and `.ast` are Nil. `.clone` is a fresh object (`===` False). `.Capture`
is the Match itself; it smartmatches Match, Capture, Cool and `Cursor`,
which is a compile-time alias of Match (`Cursor.^name` is Match, `Cursor
=== Match`). `.WHICH` is an `ObjAt`. `.replace-with($r)` returns
prematch ~ `$r` ~ postmatch, stringifying `$r`. `.CURSOR` does not exist.
```
"abc123def" ~~ /\d+/; say $/.Str, " ", $/.Bool, " ", $/.from, " ", $/.to, " ", $/.pos, " ", $/.orig, " ", $/.target, " ", $/.chars, " ", $/.prematch, " ", $/.postmatch, " ", $/.Int, " ", +$/, " ", ~$/, " ", ($/ eq "123"), " ", ($/ == 123), " ", $/.Numeric.^name, " ", $/.Int.^name, " ", ?$/, " ", (so $/), " ", $/.elems, " ", $/.list.raku, " ", $/.hash.raku, " ", $/.keys.raku, " ", $/.^name, " ", $/.WHICH.^name, " ", $/.made.raku, " ", $/.ast.raku, " ", $/.clone.Str, " ", ($/.clone === $/), " ", $/.Capture.^name, " ", $/.hash.^name, " ", $/.list.^name, " ", $/.orig.^name, " ", $/.target.^name, " ", $/.defined, " ", ($/ ~~ Capture), " ", ($/ ~~ Cool), " ", ($/ ~~ Cursor), " ", Cursor.^name, " ", (Cursor === Match), " ", $/.Str.^name, " ", ($/ + 1), " ", ($/ ~ "!"), " ", $/.uc, " ", $/.chars.^name, " ", ("abc" ~~ /b/).Int.^name, " ", ("abc" ~~ /b/).Numeric.^name, " ", (try $/.CURSOR.^name) // $!.^name; say $/.replace-with("X"), " ", $/.replace-with(42), " ", $/.caps.raku, " ", $/.chunks.raku
# rakudo 2026.08: 123 True 3 6 6 abc123def abc123def 3 abc def 123 123 123 True True Int Int True True 0 () Map.new ().Seq Match ObjAt Nil Nil 123 False Match Map List Str Str True True True True Match True Str 124 123! 123 Int Failure Failure Match|aXc a42c () ("~" => "b",).Seq
```
rakupp 4.0.1-84: differs — `.hash` is a Hash (`{}`), `.WHICH` a `ValueObjAt`, a clone is `===` the original, `Cursor` is a separate type (`~~ Cursor` False, `Cursor === Match` False), and `.replace-with`, `.caps` and `.chunks` do not exist (second line died).

### GM-17  Zero-width matches, Match.new by hand, the type object            D:partial R:partial V:spec
A zero-width match is a true Match with `.Str` "", `.from` = `.to`,
`.chars` 0, gist `｢｣`. `Match.new(:orig, :from, :pos)` builds one by hand
(`:to` is an alias of `:pos`): `.Str` is the substring, `.Bool` is
`pos >= from`, so `:from(2), :pos(1)` is a false Match with gist
`#<failed match>` and Str ""; `Match.new` alone is `orig ""`, from 0, pos 0
and True; `:made`/`:ast` set the payload and `.raku` then carries
`:made(…)`; `.prematch`/`.postmatch` and `.chars` follow the positions; a
non-Str `:orig` is stringified for `.Str`. Two hand-built Matches with
equal fields are `eqv` but not `===`. The type object gists `(Match)`, is
False, and `Match.Str` and `Match.from` are `X::AdHoc`.
```
"abc" ~~ /^/; say $/.Str.raku, " ", $/.Bool, " ", $/.from, " ", $/.to, " ", $/.chars, " ", $/.gist, " ", $/.raku, " | ", Match.new(:orig("abc"), :from(1), :pos(2)).Str, " ", Match.new(:orig("abc"), :from(1), :pos(2)).Bool, " ", Match.new(:orig("abc"), :from(1), :pos(2)).to, " ", Match.new(:orig("abc"), :from(2), :pos(1)).Bool, " ", Match.new(:orig("abc"), :from(2), :pos(1)).Str.raku, " ", Match.new(:orig("abc"), :from(2), :pos(1)).gist, " ", Match.new.raku, " ", Match.new.gist, " ", Match.new.Bool, " ", Match.new(:made(5)).made, " ", Match.new(:ast(6)).made, " ", Match.new(:orig("abc"), :from(1), :pos(2), :made(9)).raku, " ", Match.new(:orig("abc"), :from(0), :pos(3)).prematch.raku, " ", Match.new(:orig("abc"), :from(0), :pos(1)).postmatch, " ", Match.new(:orig(42), :from(0), :pos(2)).Str, " ", Match.new(:orig("ab"), :from(0), :to(1)).Str, " ", Match.new(:orig("ab"), :from(0), :pos(1)).chars, " ", Match.^mro.map(*.^name).join(","), " ", Match.new(:orig("ab"), :from(0), :pos(1)).WHICH.^name, " ", (Match.new(:orig("ab"), :from(0), :pos(1)) eqv Match.new(:orig("ab"), :from(0), :pos(1))), " ", (Match.new(:orig("ab"), :from(0), :pos(1)) === Match.new(:orig("ab"), :from(0), :pos(1))), " ", Match.gist, " ", Match.raku, " ", Match.Bool, " ", (try Match.Str) // $!.^name, " ", (try Match.from) // $!.^name
# rakudo 2026.08: "" True 0 0 0 ｢｣ Match.new(:orig("abc"), :from(0), :pos(0)) | b True 2 False "" #<failed match> Match.new(:orig(""), :from(0), :pos(0)) ｢｣ True 5 6 Match.new(:orig("abc"), :from(1), :pos(2), :made(9)) "" bc 42 a 1 Match,Capture,Cool,Any,Mu ObjAt True False (Match) Match False X::AdHoc X::AdHoc
```
rakupp 4.0.1-84: differs — a hand-built Match with `pos < from` is True with gist `｢｣`, `:made`/`:ast` are ignored (`.made` Nil, `.raku` without `:made`), equal hand-built Matches are `===`, and `Match.Str` on the type object is "" while `.from` is `X::Method::NotFound`.

### GM-18  gist and raku formats; eqv, ===, hash keys                        D:partial R:yes V:spec
`.gist` is `｢text｣` followed, one per line indented by one space per
level, by `key => ｢text｣` for each capture in `.caps` order (position
order, positional indexes as numbers); a quantified capture repeats its key
(`1 => ｢1｣| 1 => ｢2｣`); nested captures indent further (`inner => ｢b(c)｣|
0 => ｢c｣`); an empty quantified capture shows nothing. `.raku` is
`Match.new(:orig(…), :from(n), :pos(n)` plus `:list((…))` when there are
positional captures (an Array where quantified: `[…]`), `:hash(Map.new((…)))`
when there are named ones, and `:made(…)` when made; `.raku.EVAL`
reproduces a Match that is `eqv` and has the same `.raku`. `eqv` compares
orig, from, pos, made, list and hash, so two separate matches of one regex
on one string are `eqv` and `eq` but never `===`, and `(\d)` versus `\d`
on "1" are not `eqv`; a Match smartmatched against a regex matches its
text and gives a new Match. As a hash key a Match stringifies (`"b"`); a
`%h{Any}` object hash keeps the Match.
```
"ab12" ~~ /(a) $<x>=(b) (\d)+/; say $/.gist.subst("\n", "|", :g), " || ", $/.raku, " || ", ($/.raku.EVAL.raku eq $/.raku), " ", ($/.raku.EVAL eqv $/), " ", $/.raku.EVAL.gist.subst("\n","|",:g), " ", $/.raku.EVAL<x>.Str, " ", $/.raku.EVAL[1][0].Str, " ", ($/ eqv $/), " ", ($/ eqv $/.clone), " ", ($/ === $/.clone), " ", do { my $a = $/; "ab12" ~~ /(a) $<x>=(b) (\d)+/; ($a eqv $/) ~ " " ~ ($a === $/) ~ " " ~ ($a eq $/) ~ " " ~ (($a ~~ /(a) $<x>=(b) (\d)+/) === $a) }, " ", do { "1" ~~ /(\d)/; my $a = $/; "1" ~~ /\d/; ($a eqv $/) }, " ", do { "1" ~~ /\d/; my $a = $/; "1" ~~ /\d/; ($a eqv $/) ~ ($a == $/) }, " ", do { my %h; "ab" ~~ /b/; %h{$/} = 1; %h.keys.raku ~ %h.keys[0].^name }, " ", do { "ab" ~~ /b/; my %h{Any}; %h{$/} = 1; %h.keys[0].^name }
# rakudo 2026.08: ｢ab12｣| 0 => ｢a｣| x => ｢b｣| 1 => ｢1｣| 1 => ｢2｣ || Match.new(:orig("ab12"), :from(0), :pos(4), :list((Match.new(:orig("ab12"), :from(0), :pos(1)), [Match.new(:orig("ab12"), :from(2), :pos(3)), Match.new(:orig("ab12"), :from(3), :pos(4))])), :hash(Map.new((:x(Match.new(:orig("ab12"), :from(1), :pos(2))))))) || True True ｢ab12｣| 0 => ｢a｣| x => ｢b｣| 1 => ｢1｣| 1 => ｢2｣ b 1 True True False True False True False False TrueTrue ("b",).SeqStr Match
"a(b(c))" ~~ /a \( $<inner>=[ b \( (c) \) ] \)/; say $/.gist.subst("\n", "|", :g), " || ", $/<inner>.gist.subst("\n","|",:g), " || ", $/.raku, " || ", do { "x" ~~ /(x)/; $/.gist.subst("\n","|",:g) ~ " || " ~ $/.raku }, " || ", do { "xy" ~~ /(x)(y)/; $/.raku }, " || ", do { "xy" ~~ /$<a>=(x) $<b>=(y)/; $/.raku ~ " " ~ $/.gist.subst("\n","|",:g) }, " || ", do { "xx" ~~ /(x)+/; $/.raku ~ " " ~ $/.gist.subst("\n","|",:g) }, " || ", do { "xx" ~~ /$<a>=(x)+/; $/.raku ~ " " ~ $/.gist.subst("\n","|",:g) }, " || ", do { "" ~~ /(x)*/; $/.raku ~ " " ~ $/.gist }
# rakudo 2026.08: ｢a(b(c))｣| inner => ｢b(c)｣| 0 => ｢c｣ || ｢b(c)｣ || Match.new(:orig("a(b(c))"), :from(0), :pos(7), :list((Match.new(:orig("a(b(c))"), :from(4), :pos(5)),)), :hash(Map.new((:inner(Match.new(:orig("a(b(c))"), :from(2), :pos(6))))))) || ｢x｣| 0 => ｢x｣ || Match.new(:orig("x"), :from(0), :pos(1), :list((Match.new(:orig("x"), :from(0), :pos(1)),))) || Match.new(:orig("xy"), :from(0), :pos(2), :list((Match.new(:orig("xy"), :from(0), :pos(1)), Match.new(:orig("xy"), :from(1), :pos(2))))) || Match.new(:orig("xy"), :from(0), :pos(2), :hash(Map.new((:a(Match.new(:orig("xy"), :from(0), :pos(1))),:b(Match.new(:orig("xy"), :from(1), :pos(2))))))) ｢xy｣| a => ｢x｣| b => ｢y｣ || Match.new(:orig("xx"), :from(0), :pos(2), :list(([Match.new(:orig("xx"), :from(0), :pos(1)), Match.new(:orig("xx"), :from(1), :pos(2))],))) ｢xx｣| 0 => ｢x｣| 0 => ｢x｣ || Match.new(:orig("xx"), :from(0), :pos(2), :hash(Map.new((:a([Match.new(:orig("xx"), :from(0), :pos(1)), Match.new(:orig("xx"), :from(1), :pos(2))]))))) ｢xx｣| a => ｢x｣| a => ｢x｣ || Match.new(:orig(""), :from(0), :pos(0), :list(([],))) ｢｣
```
rakupp 4.0.1-84: differs — `===` holds between a Match and its clone and between two separate matches (a value type), and `$<a>=(x)+` gives one Match spanning "xx" instead of an Array (gist `a => ｢xx｣`).

### GM-19  Capture markers `<( )>`: `.to` versus `.pos`, and `.raku`            D:partial R:partial V:quirk
`<(` sets `.from` and `)>` sets `.to`, so `.Str`, `.chars`, `.prematch`,
`.postmatch`, `.replace-with`, `.subst`, `.comb` and a capture inside the
markers all see the marked span ("abc"); `.pos` stays the position where
the whole pattern ended (7, past the trailing `xx`), a `:g` match resumes
from there, and `.raku` prints that `:pos(7)`, so `.raku.EVAL.Str` is
"abcxx", not "abc" — the quirk: a marked Match does not round-trip through
`.raku`. A lone `<(` leaves `.to` = `.pos`; a lone `)>` keeps `.from` 0.
```
"xxabcxx" ~~ /xx <( abc )> xx/; print $/.Str, " ", $/.from, " ", $/.to, " ", $/.pos, " ", $/.prematch, " ", $/.postmatch, " ", $/.chars, " ", $/.raku, " | "; "xxabc" ~~ /xx <( abc/; print $/.Str, " ", $/.from, " ", $/.to, " ", $/.pos, " | "; "abcxx" ~~ /abc )> xx/; print $/.Str, " ", $/.from, " ", $/.to, " ", $/.pos, " | "; "a1b" ~~ /a (\d) <!before x>/; print $0.Str, " | "; say "aXb".match(/a <( X )> b/).gist, " ", do { "xxabcxx" ~~ /xx <( (abc) )> xx/; $0.Str ~ $0.from ~ $0.to }, " ", "xxabcxx".match(/xx <( abc )> xx/, :g).raku, " ", "xxabcxx".subst(/xx <( abc )> xx/, "Y"), " ", "xxabcxx".comb(/xx <( abc )> xx/).raku, " ", "aXbXc".match(/X <( b )> X/).Str, " ", "aXbXc".match(/X <( b )> X/).pos, " ", "aXbXc".match(/X <( b )> X/, :g).elems, " ", "xxabcxx".match(/xx <( abc )> xx/).raku.EVAL.Str; say do { "xxabcxx" ~~ /xx <( abc )> xx/; $/.replace-with("Q") }, " ", "aXb".match(/a <( X )> b/).replace-with("Y")
# rakudo 2026.08: abc 2 5 7 xx xx 3 Match.new(:orig("xxabcxx"), :from(2), :pos(7)) | abc 2 5 5 | abc 0 3 5 | 1 | ｢X｣ abc25 (Match.new(:orig("xxabcxx"), :from(2), :pos(7)),) xxYxx ("abc",).Seq b 4 1 abcxx|xxQxx aYb
```
rakupp 4.0.1-84: differs — `.pos` equals `.to` after a `)>` (5 and 3), so `.raku` round-trips, but `.replace-with` does not exist (second line died).

### GM-20  `$/` after a failed match, and which Str routines set it          D:partial R:partial V:spec
A failed `~~` sets the caller's `$/` to Nil: `$/.^name` is Nil, `.Bool` and
`so` False, `.from` Nil, `.Str` and `~` "" (a "Use of Nil in string
context" warning each), `+` 0 (numeric-context warning), `.defined` False,
`$/.list` is `(Nil,)`, `$/.hash` `{}`, `$0`, `$<a>` and `.made` Nil, `.gist`
Nil. `!~~` sets `$/` the same way (Nil on no match, the Match on a match).
`.match` with a Regex sets `$/` (Nil, or `()` itemized for `:g` with no
match), so does `.match` with a Str pattern and `.subst` with a Regex (Nil
when nothing matched); `.subst` with a Str matcher, `.comb`, `.split` and
`.contains` leave `$/` alone.
```
my @w; CONTROL { when CX::Warn { @w.push(.message.substr(0, 30)); .resume } }; "abc" ~~ /b/; "abc" ~~ /x/; say $/.^name, " ", $/.Bool, " ", (so $/), " ", $/.from.raku, " ", $/.Str.raku, " ", (~$/).raku, " ", +$/, " ", $/.defined, " ", ($/ ~~ Match), " ", $/.list.raku, " ", $/.hash.raku, " ", $0.raku, " ", $<a>.raku, " ", $/.made.raku, " ", ("abc" ~~ /x/).raku, " ", ("abc" ~~ /x/).^name, " ", ("abc" !~~ /x/), " ", $/.raku, " ", ("abc" !~~ /b/), " ", $/.Str, " ", do { "abc" ~~ /x/; $/.gist }, " ", @w.raku, " ", do { "abc" ~~ /b/; "abc".match(/x/); $/.raku }, " ", do { "abc" ~~ /b/; "abc".match(/x/, :g); $/.raku }, " ", do { "abc" ~~ /b/; "abc".subst(/x/, "y"); $/.raku }, " ", do { "abc" ~~ /b/; "abc".subst("x", "y"); $/.Str }, " ", do { "abc" ~~ /b/; "abc".comb(/x/); $/.Str }, " ", do { "abc" ~~ /b/; "abc".split(/x/); $/.Str }, " ", do { "abc" ~~ /b/; "abc".contains(/x/); $/.Str }, " ", do { "abc" ~~ /b/; "abc".match("x"); $/.raku }, " ", do { "abc" ~~ /b/; "abc".match("c"); $/.Str }
# rakudo 2026.08: Nil False False Nil "" "" 0 False False (Nil,) {} Nil Nil Nil Nil Nil True Nil False b Nil ["Use of Nil in string context", "Use of Nil in string context", "Use of Nil in numeric context"] Nil $( ) Nil b b b b Nil c
```
rakupp 4.0.1-84: differs — `$0` on a Nil `$/` is Any, only one warning is issued, and `.match` with a Str pattern does not touch `$/`.

## D. Captures

### GM-21  Positional captures and quantifiers                              D:partial R:yes V:quirk
An unquantified `(…)` is one Match; any quantifier on it — `+`, `*`, `**2`,
`**1`, `**1..2`, `**1..1`, `**0..1`, `**1..*`, `+?`, `??`, `+ % ","`, and a
group `[(…)]*` or `[(…)(…)]+` — makes the capture an Array of Matches
(empty for zero repetitions), while `?` alone keeps a single Match or Nil.
`.elems` counts the capture slots, present or not, so `(a)(X)?(b)` has 3
with `$1` Nil. In `$/.list.raku` an absent slot prints as `Mu` although
`$1` reads Nil — the quirk; read it as Nil.
```
"123" ~~ /(\d)/; print $0.^name, " ", $/.list.elems, " ", $/.elems, " | "; "123" ~~ /(\d)+/; print $0.^name, " ", $0.elems, " ", $0.raku, " ", $0[0].^name, " | "; "123" ~~ /[(\d)]*/; print $0.^name, " ", $0.elems, " | "; "12" ~~ /(\d)**2/; print $0.^name, " ", $0.elems, " | "; "1,2,3" ~~ /(\d)+ % ","/; print $0.^name, " ", $0.elems, " ", $0.join("|"), " | "; "a" ~~ /(a)?/; print $0.^name, " | "; "" ~~ /(a)?/; print $0.raku, " ", $/.list.elems, " ", $/.elems, " | "; "b" ~~ /(a)*b/; print $0.^name, " ", $0.elems, " ", $/.list.elems, " | "; "12" ~~ /(\d)**1/; print $0.^name, " | "; "12" ~~ /(\d)**1..2/; print $0.^name, " ", $0.elems, " | "; "1" ~~ /(\d)**1..1/; print $0.^name, " | "; "aXb" ~~ /(a)(X)?(b)/; print $1.^name, " ", $2.Str, " ", $/.elems, " | "; "ab" ~~ /(a)(X)?(b)/; print $1.raku, " ", $2.Str, " ", $/.elems, " ", $/.list.raku, " | "; "12" ~~ /(\d)(\d)+/; print $0.^name, " ", $1.^name, " ", $1.elems, " ", $/.elems, " | "; "1" ~~ /(\d)**0..1/; print $0.^name, " | "; "1" ~~ /(\d)**1..*/; print $0.^name, " | "; "1" ~~ /(\d)+?/; print $0.^name, " | "; "1" ~~ /(\d)??/; print $0.^name, " | "; "1" ~~ /(\d) ** 1/; say $0.^name, " ", do { "ab" ~~ /[(a)(b)]+/; $0.^name ~ $1.^name ~ $0.elems }
# rakudo 2026.08: Match 1 1 | Array 3 [Match.new(:orig("123"), :from(0), :pos(1)), Match.new(:orig("123"), :from(1), :pos(2)), Match.new(:orig("123"), :from(2), :pos(3))] Match | Array 3 | Array 2 | Array 3 1|2|3 | Match | Nil 0 0 | Array 0 1 | Array | Array 2 | Array | Match b 3 | Nil b 3 (Match.new(:orig("ab"), :from(0), :pos(1)), Mu, Match.new(:orig("ab"), :from(1), :pos(2))) | Match Array 1 2 | Array | Array | Array | Nil | Array ArrayArray1
```
rakupp 4.0.1-84: matches on every field but the quirk one, where the absent slot prints as Nil; keep that.

### GM-22  Captures in alternations; absent captures; no captures            D:yes R:yes V:spec
Each alternative of `|` and `||` numbers its captures from the group's
first index, so `(a)|(b)` has one slot and `(a)|(b)(c)` two (`$1` Nil and
`:exists` False when the first alternative won); a quantified group of
alternatives gathers into one Array (`[(a)|(b)]+` is an Array of 2).
With no captures at all `$0`, `$/[0]` and `$/[5]` are Nil, `.elems` and
`.list.elems` 0, `:exists` False; a negative index is a Failure.
```
"b" ~~ /(a)|(b)/; print $0.Str, " ", $/.list.elems, " ", $1.raku, " | "; "bc" ~~ /(a)|(b)(c)/; print $0.Str, " ", $1.Str, " ", $/.elems, " | "; "a" ~~ /(a)|(b)(c)/; print $0.Str, " ", $1.raku, " ", $/.list.elems, " ", $/.elems, " ", ($/[1]:exists), " ", ($/[0]:exists), " | "; "ab" ~~ /(a)[(b)|(c)]/; print $1.Str, " ", $/.elems, " | "; "ac" ~~ /(a)[(b)|(c)]/; print $1.Str, " | "; "x" ~~ /x/; print $0.raku, " ", $/[0].raku, " ", $/.list.elems, " ", $/.elems, " ", ($/[0]:exists), " ", $/[5].raku, " ", do { my $i = -1; $/[$i].^name }, " | "; "ab" ~~ /(a)||(b)/; print $0.Str, " | "; "b" ~~ /(a)||(b)/; print $0.Str, " ", $/.list.elems, " | "; "ab" ~~ /[(a)|(b)]+/; print $0.^name, " ", $0.elems, " ", $0.join(","), " ", $/.elems, " | "; "ab" ~~ /(a)(b)|(c)/; print $/.elems, " ", $1.Str, " | "; "c" ~~ /(a)(b)|(c)/; print $/.elems, " ", $1.raku, " ", $/.list.raku, " | "; "ac" ~~ /(a)[(b)||(c)]/; say $1.Str, " ", $/.elems, " ", do { "1a" ~~ /(\d)(a)|(\w)/; $0.Str ~ $1.Str ~ $/.elems }, " ", do { "1" ~~ /(\d)(a)|(\w)/; $0.Str ~ $1.raku ~ $/.elems }
# rakudo 2026.08: b 1 Nil | b c 2 | a Nil 1 1 False True | b 2 | c | Nil Nil 0 0 False Nil Failure | a | b 1 | Array 2 a,b 1 | 2 b | 1 Nil (Match.new(:orig("c"), :from(0), :pos(1)),) | c 2 1a2 1Nil1
```
rakupp 4.0.1-84: differs — an absent capture reads Any instead of Nil, and `:exists` on it answers Nil (with a warning) instead of False.

### GM-23  Named captures                                                   D:partial R:yes V:spec
`<a>` once is a Match; `<a>+`, and two separate calls `<a> <a>`, give an
Array; `<a>?` is a Match or Nil; `<a>*` with none is `[]`. A missing name
reads Nil with `:exists` False. `$<x>=(a)` and `$<x>=[b]` name a group (the
parentheses do not also number it: `.elems` 0); `$<x>=(a)+` is an Array.
`<a=.alpha>` and `<b=.alpha>` store under the alias. A name used in both
alternatives is one Match. `.hash` is a Map of the named captures, `.list`
the positional ones; `.keys`, `.values`, `.pairs`, `.kv` and `.antipairs`
give the positional entries first, then the named ones in hash order;
`.caps` and `.chunks` are in position order; `.elems` counts positionals
only, `+$/.hash` the named ones.
```
my regex a { a }; "aa" ~~ /<a>+/; print $<a>.^name, " ", $<a>.elems, " ", $<a>[0].Str, " ", $<a>[0].^name, " | "; "aa" ~~ /<a> <a>/; print $<a>.^name, " ", $<a>.elems, " | "; "a" ~~ /<a>/; print $<a>.^name, " ", $<b>.raku, " ", ($/<b>:exists), " ", ($/<a>:exists), " ", $/.hash.^name, " ", $/.hash.keys.raku, " | "; "a" ~~ /<a>?/; print $<a>.^name, " | "; "" ~~ /<a>?/; print $<a>.raku, " ", $/.hash.raku, " ", ($/<a>:exists), " | "; "" ~~ /<a>*/; print $<a>.raku, " ", $<a>.elems, " | "; "ab" ~~ /$<x>=(a) $<y>=[b]/; print $<x>.Str, " ", $<y>.Str, " ", $<x>.^name, " ", $/.keys.sort.raku, " ", $/.elems, " | "; "ab" ~~ /$<x>=(a)+ b/; print $<x>.^name, " ", $<x>.elems, " | "; "aa" ~~ /<a=.alpha> <b=.alpha>/; print $/.keys.sort.raku, " ", $<a>.Str, " | "; "a" ~~ /<a> | <a> b/; print $<a>.^name, " | "; "ab" ~~ /<a> b | <a>/; print $<a>.^name, " | "; "a1b2" ~~ /(a) $<n>=(\d) (b) $<m>=(\d)/; say $/.keys.head(2).raku, " ", $/.keys.sort.raku, " ", $/.values.head(2).map(*.Str).raku, " ", $/.pairs.head(2).map({ .key ~ "=" ~ .value }).raku, " ", $/.kv.head(4).map(*.Str).raku, " ", $/.caps.map({ .key ~ "=" ~ .value }).raku, " ", $/.Str, " ", $/.hash.keys.sort.raku, " ", $/.list.elems, " ", $/.elems, " ", +$/.hash, " ", $/.antipairs.elems; say $/.chunks.map({ .key ~ "=" ~ .value }).raku
# rakudo 2026.08: Array 2 a Match | Array 2 | Match Nil False True Map ("a",).Seq | Match | Nil Map.new False | [] 0 | a b Match ("x", "y").Seq 0 | Array 1 | ("a", "b").Seq a | Match | Match | (0, 1).Seq (0, 1, "m", "n").Seq ("a", "b").Seq ("0=a", "1=b").Seq ("0", "a", "1", "b").Seq ("0=a", "n=1", "1=b", "m=2").Seq a1b2 ("m", "n").Seq 2 2 2 4|("0=a", "n=1", "1=b", "m=2").Seq
```
rakupp 4.0.1-84: differs — quantified and repeated named captures are Lists, not Arrays; `$<x>=(a)+` is one Match with `.elems` 0; `.hash` is a Hash; a missing name's `:exists` is Nil with a warning; `.antipairs` counts 2 instead of 4; `.chunks` does not exist (second line died).

### GM-24  Captures inside lookarounds are discarded                          D:no R:no V:quirk
Nothing captured inside `<?before …>`, `<!before …>` or `<?after …>` reaches
the result: `(\d)`, `$<d>=\d` and `<d=.digit>` inside a lookahead leave
`.elems` 0 and `.keys` empty. The non-`?` form `<before (\d) >` is an
ordinary named subrule call: `$<before>` is a zero-width Match at the
lookahead position (from 1 to 1, Str "") with no captures of its own. Note
the space before `>`: `(\d)>` would read `)>` as the capture-end marker.
```
say ("a1" ~~ /a <?before \d>/).Str, " ", ("a1" ~~ /a <?before (\d) >/).raku, " ", ("a1" ~~ /a <?before $<d>=\d >/)<d>.raku, " ", ("a1" ~~ /a <?before [(\d)] >/).raku, " ", ("a1" ~~ /a <before (\d) >/).raku, " ", ("a1" ~~ /a <before (\d) >/)<before>.raku, " ", ("a1" ~~ /a <before (\d) >/)<before>[0].raku, " ", ("a1" ~~ /a <?before $<d>=(\d) >/)<d>.raku, " ", ("a1" ~~ /a <?before <d=.digit> >/)<d>.raku, " ", ("ab" ~~ /(a) <?after (a) > b/).elems, " ", ("ab" ~~ /(a) <!before (x) > b/).elems, " ", ("ab" ~~ /(a) <?[b]>/).to, " ", ("a1" ~~ /a <?before \d>/).to, " ", ("a1" ~~ /a <before \d>/).Str, " ", ("a1" ~~ /a <before \d>/)<before>.Str.raku, " ", ("a1" ~~ /a <before \d>/)<before>.from, " ", ("a1" ~~ /a <?before (\d) >/).elems, " ", ("a1" ~~ /a <?before (\d) >/).keys.raku
# rakudo 2026.08: a Match.new(:orig("a1"), :from(0), :pos(1)) Nil Match.new(:orig("a1"), :from(0), :pos(1)) Match.new(:orig("a1"), :from(0), :pos(1), :hash(Map.new((:before(Match.new(:orig("a1"), :from(1), :pos(1))))))) Match.new(:orig("a1"), :from(1), :pos(1)) Nil Nil Nil 1 1 1 1 a "" 1 0 ().Seq
```
rakupp 4.0.1-84: differs — the named-subrule form stores the key with the argument text (`:before (\d)`) and `$<before>` reads Nil.

### GM-25  Inside a code block: `$/`, `$¢`, `$0`, `:my`                       D:yes R:yes V:spec
Inside `{ … }` in a regex, `$/` and `$¢` are the same object (`===` True),
the match in progress: `.from` is the start, `.pos` and `.to` the current
position, `.Str` the text so far, `.orig`/`.target`/`.prematch`/`.postmatch`
answer for that partial span, `.Bool` True, `.made` Nil, and `$0`/`$/[n]`,
`.list.elems`, `.gist` and `.raku` show the captures completed so far
(`.raku` includes them). Inside a named regex `$/` is that regex's own
match (from 0 there). `<r>` is visible as `$<r>` in a block that follows it.
`:my $y = 5;` declares a variable visible in the following blocks. Outside
any regex `$¢` is Nil.
```
my $*x; "ab" ~~ /(a) { $*x = $0.Str ~ ":" ~ $/.pos ~ ":" ~ $¢.pos ~ ":" ~ $¢.^name ~ ":" ~ $/.from ~ ":" ~ $¢.from ~ ":" ~ $/.Str.raku ~ ":" ~ ($/ === $¢) ~ ":" ~ $/.^name ~ ":" ~ $/.to } b/; say ~$*x, " ", $¢.raku, " ", do { "ab" ~~ /:my $y = 5; (a) { $*x = $y } b/; ~$*x }, " ", do { "ab" ~~ /a { $*x = $¢.orig ~ $¢.target ~ $¢.Str.raku } b/; ~$*x }, " ", do { "abc" ~~ /a (b) { $*x = $/.gist } c/; $*x.subst("\n", "|", :g) }, " ", do { my regex r { (\w) { $*x = $/.Str ~ $/.from } }; "q" ~~ /<r>/; ~$*x }, " ", do { "ab" ~~ /(a) (b) { $*x = $/.list.elems ~ $1.Str }/; ~$*x }, " ", do { "ab" ~~ /(a) { $*x = $/.raku } b/; ~$*x }, " ", do { "ab" ~~ /a { $*x = $/.Bool } b/; ~$*x }, " ", do { "ab" ~~ /a { $*x = $/.made.raku } b/; ~$*x }, " ", do { "ab" ~~ /(a) { $*x = $/.list.elems } (b) { $*x ~= $/.list.elems }/; ~$*x }, " ", do { "ab" ~~ /(a) { $*x = $/[0].Str } (b) { $*x ~= $/[1].Str }/; ~$*x }, " ", do { my regex r { (\w) }; "q" ~~ /<r> { $*x = $<r>[0].Str ~ $<r>.^name }/; ~$*x }, " ", do { "ab" ~~ /(a) { $*x = $/.gist.subst("\n","|",:g) } b/; ~$*x }, " ", do { "ab" ~~ /a { $*x = $/.orig ~ $/.target } b/; ~$*x }, " ", do { "ab" ~~ /a { $*x = $/.prematch.raku ~ $/.postmatch.raku } b/; ~$*x }; say do { "abc" ~~ /a { } b { $*x = $/.chunks.elems } c/; ~$*x }
# rakudo 2026.08: a:1:1:Match:0:0:"a":True:Match:1 Nil 5 abab"a" ｢ab｣| 0 => ｢b｣ q0 2b Match.new(:orig("ab"), :from(0), :pos(1), :list((Match.new(:orig("ab"), :from(0), :pos(1)),))) True Nil 12 ab qMatch ｢a｣| 0 => ｢a｣ abab """b"|1
```
rakupp 4.0.1-84: differs — every field of the first line matches; `.chunks` does not exist, so the second line died.

## E. `$/`, smartmatch and the Regex value

### GM-26  `$/` is one per routine; blocks share it                           D:partial R:partial V:spec
Every sub and method has its own `$/`: a match inside a sub does not touch
the caller's, and a sub that matches nothing returns Nil for `$/`. Bare
blocks, `for`, pointy blocks, `map` blocks, `if`, a stored `{ }` block,
`given`/`when` and `do` all share the enclosing routine's `$/`, so a match
inside them is visible after them, and a `when /t/` that succeeds leaves
`$/` set after the `given`. Inside a `for` over an `m:g` list `$/` is that
list (`.^name` List) while `$_` is each Match. `my $/` inside a block
shadows the outer one. `andthen` and `&&` see the match just made.
```
grammar G { token TOP { a+ } }; sub f { G.parse("aaa"); $/.Str }; sub g { "xyz" ~~ /y/; $/.Str }; "zzz" ~~ /z/; my $r = f(); my $s = g(); print $r, " ", $s, " ", $/.Str, " "; { "abc" ~~ /b/ }; print $/.Str, " "; for 1 { "abc" ~~ /c/ }; print $/.Str, " "; -> { "abc" ~~ /a/ }(); print $/.Str, " "; (1,).map({ "def" ~~ /e/ }).eager; print $/.Str, " "; if "ghi" ~~ /h/ { print $/.Str, " " }; print $/.Str, " "; my $b = { "jkl" ~~ /k/ }; $b(); print $/.Str, " "; my &c = sub { "mno" ~~ /n/ }; c(); print $/.Str, " "; class C { method m { "pqr" ~~ /q/ } }; C.m; print $/.Str, " "; given "stu" { when /t/ { print $/.Str, " " } }; print $/.Str, " "; given "vwx" { when /w/ { } }; print $/.Str, " "; my @l; for "a1b2".match(/\d/, :g) { @l.push($_.Str ~ $/.^name ~ $/.elems) }; print @l.raku, " "; my $z = ("q" ~~ /q/ andthen $/.Str); print $z, " "; my $y = do { "r" ~~ /r/; $/.Str }; print $y, " ", $/.Str, " "; my $w = "s" ~~ /s/ && $/.Str; print $w, " "; sub h { $/ } ; "t" ~~ /t/; print h().raku, " "; sub i { "u" ~~ /u/; $/ }; say i().Str, " ", $/.Str, " ", do { my $/; "v" ~~ /v/; $/.Str }, " ", $/.Str
# rakudo 2026.08: aaa y z b c a e h h k k k t t w ["1List2", "2List2"] q r r s Nil u t v t
```
rakupp 4.0.1-84: differs — a sub without a match of its own returns the caller's `$/` instead of Nil.

### GM-27  Smartmatching against a Regex: Junction, List, Hash, type objects, a Match on the right   D:partial R:partial V:bug
`$x ~~ /re/` returns the Match or Nil and sets `$/`. A Junction topic
autothreads into a Junction of results (`any(Match, Nil)`, True under `so`
when any matched). A List or Array topic matches the ELEMENTS in turn and
returns the first element's Match (`.orig` is that element, `(1, 2) ~~ /2/`
has `:orig(2)`), Nil when none matches; a Hash topic tries its keys. An Int
topic matches its Str and keeps the Int as `.orig`. `Regex.ACCEPTS`,
`~~ Regex`, `~~ Method` and `~~ Callable` hold for a regex. A concrete
MATCH on the right is returned as is, whatever the topic (`5 ~~ $m` is
`$m`), and `!~~` gives its `.not`; a Junction topic against a Match is
boolified. A type-object topic (`Str`, `Any`) gives Nil with an
"uninitialized value" warning and `Nil ~~ /b/` Nil with a Nil warning,
although a candidate exists to answer False for an undefined topic — the
bug: answer False, no warning. `rx ~~ rx` is Nil.
```
say (quietly { ("abc" ~~ /b/).^name }), " ", (any("abc", "xyz") ~~ /b/).^name, " ", (any("abc", "xyz") ~~ /b/).raku, " ", (so any("abc","xyz") ~~ /b/), " ", (so all("abc","xyz") ~~ /b/), " ", (<abc xyz bcd> ~~ /b/).Str, " ", (<abc xyz bcd> ~~ /b/).^name, " ", (<xyz> ~~ /b/).raku, " ", ({ abc => 1 } ~~ /b/).Str, " ", ({ xyz => 1 } ~~ /b/).raku, " ", (quietly { (Str ~~ /b/).raku }), " ", (quietly { (Nil ~~ /b/).raku }), " ", (quietly { (Any ~~ /b/).raku }), " ", (42 ~~ /4/).Str, " ", (42 ~~ /4/).orig.^name, " ", (rx/b/ ~~ Regex), " ", (rx/b/ ~~ Method), " ", (rx/b/ ~~ Callable), " ", ("abc" ~~ /b/ ~~ Match), " ", do { my $m = "abc" ~~ /b/; ("zzz" ~~ $m).Str }, " ", do { my $m = "abc" ~~ /b/; ("zzz" ~~ $m) === $m }, " ", do { my $m = "abc" ~~ /b/; ("zzz" !~~ $m) }, " ", rx/b/.ACCEPTS("abc").Str, " ", rx/b/.ACCEPTS("xyz").raku, " ", (quietly { rx/b/.ACCEPTS(Str).raku }), " ", (quietly { (rx/b/ ~~ rx/b/).raku }), " ", do { my @a = "abc", "xbx"; (@a ~~ /b/).from }, " ", do { my @a = "abc"; ("x" ~~ /b/); (@a ~~ /b/); $/.Str }, " ", do { "b" ~~ /(b)/; "b" ~~ /b/; $0.raku }, " ", do { my @a = <abc xbx>; (@a ~~ /b/).orig }, " ", ((1, 2) ~~ /2/).raku, " ", (<1 2> ~~ /2/).Str, " ", do { my $m = "abc" ~~ /b/; ("zzz" ~~ $m).Str ~ (5 ~~ $m).Str ~ ($m.ACCEPTS("q") === $m) }, " ", ("abc" ~~ /b/).ACCEPTS("zzz").^name, " ", do { my @a = <abc xbx>; (@a ~~ /x(b)/).from }, " ", do { my %h = (abc => 1); (%h ~~ /b/).orig }, " ", (("abc", "xbx") ~~ /b/).orig, " ", ((1, 2) ~~ /1\s2/).raku, " ", (any("b") ~~ /b/).^name, " ", (any("b") ~~ /b/).Str, " ", do { my $m = "abc" ~~ /b/; (any("a") ~~ $m).raku }, " ", do { my $m = "abc" ~~ /b/; (so any("a") ~~ $m) }, " ", do { my @a = <xbx abc>; (@a ~~ /b/).orig }, " ", do { my @a = <xx abc>; ("q" ~~ /q/); (@a ~~ /z/).raku ~ $/.raku }
# rakudo 2026.08: Match Junction any(Match.new(:orig("abc"), :from(1), :pos(2)), Nil) True False b Match Nil b Nil Nil Nil Nil 4 Int True True True True b True False b Nil Nil Nil 1 b Nil abc Match.new(:orig(2), :from(0), :pos(1)) 2 bbTrue Match 0 abc abc Nil Junction any(b) Bool::True True xbx NilNil
```
rakupp 4.0.1-84: differs — a List or Hash topic is stringified and matched as one string (`.orig` "1 2", a Hash's key/value text), so `(1, 2) ~~ /1\s2/` matches; an Int topic's `.orig` is a Str; `rx/b/.ACCEPTS(Str)` returns a Match; a Junction topic against a Match is not boolified.

### GM-28  The Regex value: Bool against `$_`, gist/raku, name, callable      D:partial R:partial V:spec
A regex in Boolean context (`so`, `?`, `??`, `if`) matches `$_` and sets
`$/`; with `$_` undefined it is False and `m//` gives Nil. `.gist` and
`.raku` are the source text verbatim, adverbs included (`rx:i/ab/`,
`rx/ a.b /`), and a named regex shows its declaration (`regex r { \d+ }`,
`token t { \w }`); `.Str` is "" with a "Regex object coerced to string"
warning. `.^name` is Regex, `.name` "" for an anonymous one and the name
otherwise, `.signature` `:(Mu $:: *%_)`, arity and count 1. A regex is
callable only with a Match as its argument, and then matches ANCHORED at
that Match's `:pos` and returns the resulting Match (`.Bool` False when it
does not match there); a Str argument is `X::Method::NotFound`, no argument
an `X::AdHoc` arity error. `Regex.new` is `X::Cannot::New`. `m/b/` matches
at once and yields a Match or Nil; `rx/b/` and `/b/` in term position are
the Regex. `<&r>` is non-capturing, `<r=&t>` captures under `r`. Two
regexes are `eqv` but never `===` (an `ObjAt` WHICH).
```
$_ = "abc"; print (so /b/), " ", (?/x/), " ", (/b/ ?? "y" !! "n"), " ", (do if /c/ { "if" }), " ", $/.Str, " ", rx/a.b/.gist, " ", rx/a.b/.raku, " ", (quietly { /a.b/.Str.raku }), " ", rx:i/ab/.raku, " ", rx/ab/.^name, " ", rx/ab/.name.raku, " | "; my regex r { \d+ }; my token t { \w }; print &r.name, " ", &r.^name, " ", &t.^name, " ", &r.gist, " ", &r.raku, " ", ("a12" ~~ &r).Str, " ", ("a12" ~~ /<r>/)<r>.Str, " ", (try &r("a12").Str) // $!.^name, " ", (try &r(Match.new(:orig("a12"), :pos(1))).Str) // $!.^name, " ", (try Regex.new) // $!.^name, " ", (try Regex.new(:source("a"))) // $!.^name, " | "; $_ = "abc"; my $m = m/b/; print $m.^name, " ", $m.Str, " ", rx/b/.^name, " ", (m/b/).^name, " ", (m/x/).raku, " "; $_ = Any; print (so /b/), " ", (quietly { (m/b/).raku }), " ", (quietly { (Str ~~ /b/).raku }), " | "; my $y = /b/; say $y.^name, " ", ("abc" ~~ $y).Str, " ", (/b/).^name, " ", ("abc".match($y)).Str, " ", (try $y("abc").^name) // $!.^name, " ", $y(Match.new(:orig("abc"), :pos(0))).Str.raku, " ", $y(Match.new(:orig("abc"), :pos(0))).^name, " ", $y(Match.new(:orig("abc"), :pos(0))).Bool, " ", $y(Match.new(:orig("abc"), :pos(1))).Str, " ", $y(Match.new(:orig("abc"), :pos(1))).Bool, " ", (rx/b/ eqv rx/b/), " ", (rx/b/ === rx/b/), " ", rx/b/.WHICH.^name, " ", rx/(b)/.gist, " ", rx/ a.b /.raku.raku, " ", ("a12" ~~ /<&r>/).Str, " ", ("a12" ~~ /<&r>/).keys.raku, " ", ("a12" ~~ /<r=&t>/).keys.raku, " ", (try &r()) // $!.^name, " ", (rx/b/ ~~ Str), " ", &t.gist, " ", (quietly { (rx/b/ ~~ rx/b/).raku }), " ", do { my @w; CONTROL { when CX::Warn { @w.push(.message.substr(0, 22)); .resume } }; rx/b/.Str.raku ~ @w.raku }; say rx/ab/.signature.raku, " ", &r.signature.raku, " ", rx/ab/.arity, " ", rx/ab/.count, " ", &r.arity, " ", &r.count
# rakudo 2026.08: True False y if c rx/a.b/ rx/a.b/ "" rx:i/ab/ Regex "" | r Regex Regex regex r { \d+ } regex r { \d+ } 12 12 X::Method::NotFound 12 X::Cannot::New X::Cannot::New | Match b Regex Match Nil False Nil Nil | Regex b Regex b X::Method::NotFound "" Match False b True True False ObjAt rx/(b)/ "rx/ a.b /" 12 ().Seq ("r",).Seq X::AdHoc False token t { \w } Nil ""["Regex object coerced t"]|:(Mu $:: *%_) :(Mu $:: *%_) 1 1 1 1
```
rakupp 4.0.1-84: differs — `.Str` is the source without a warning, `.raku` moves the adverb inside (`rx/:i ab/`), `.name` is "Regex" and a named regex gists as `&r`; a Str argument is accepted and the call scans instead of anchoring; `Regex.new` is not refused; `rx/b/ === rx/b/` is True (`ValueObjAt`); `rx ~~ rx` is a Match; `.signature`, `.arity` and `.count` do not exist (second line died).

### GM-29  Interpolation: `$v`, `@a`, `<$v>`, `<@a>`, `<{ }>`, `<?{ }>`          D:partial R:yes V:spec
`$v` interpolates literally (metacharacters inert, spaces significant even
under `:s`); `:i` applies to it. `@a` is a literal alternation that takes
the LONGEST matching element whatever the order; `[@a]` and a following
atom behave the same. `<$v>` and `<{ code }>` interpolate as regex source
(`"a+"` quantifies, `'\d'` is a class); a Regex value in `$r`, `<$r>`,
`@r`/`<@r>` or `<{ }>` is run as a subrule, and captures INSIDE an
interpolated regex do not become captures of the outer match (`.elems`
0), unless aliased (`<z=$z>`). `<?{ … }>` and `<!{ … }>` gate on a Boolean
and see `$0`. `<{ Nil }>` is `X::Multi::NoMatch`; an empty `@e` matches
nothing (Nil).
```
my $v = "a.c"; my @a = <ab abc a>; print ("abc" ~~ /$v/).raku, " ", ("a.c" ~~ /$v/).Str, " ", ("abcd" ~~ /@a/).Str, " ", ("abcd" ~~ /<@a>/).Str, " ", ("abcd" ~~ /<{ "ab" ~ "c" }>/).Str, " ", ("a.c" ~~ /<$v>/).Str, " ", ("abc" ~~ /<$v>/).Str, " ", ("ab" ~~ /(a) <?{ $0 eq "a" }> b/).Str, " ", ("ab" ~~ /(a) <?{ $0 eq "x" }> b/).raku, " ", ("ab" ~~ /(a) <!{ $0 eq "x" }> b/).Str, " ", ("AbC" ~~ /:i $v/).raku, " ", do { my $w = "abc"; ("ABC" ~~ /:i $w/).Str }, " | "; my $r = rx/b+/; my @r = rx/a/, rx/ab/; say ("abbc" ~~ /<$r>/).Str, " ", ("abbc" ~~ /$r/).Str, " ", ("abc" ~~ /<@r>/).Str, " ", ("abc" ~~ /@r/).Str, " ", ("ab" ~~ /<{ rx/a.b/ }>/).raku, " ", ("ab" ~~ /<{ rx/ab/ }>/).Str, " ", ("a1" ~~ /a <{ '\d' }>/).Str, " ", (try ("ab" ~~ /a <{ Nil }> b/).raku) // $!.^name, " ", do { my @e; (try ("ab" ~~ /@e/).raku) // $!.^name }, " ", do { my $u = "b"; ("abc" ~~ /a $u c/).Str }, " ", do { my @m = "b", "bc"; ("abcd" ~~ /a @m d/).Str }, " ", do { my @m = "b", "bc"; ("abcd" ~~ /a [@m] d/).Str }, " ", do { my $q = "a b"; ("a b" ~~ /$q/).Str.raku }, " ", do { my $q = "a b"; ("ab" ~~ /:s $q/).raku }, " ", do { my $q = "a+"; ("aaa" ~~ /<$q>/).Str }, " ", do { my $q = "a+"; ("aaa" ~~ /$q/).raku }, " ", do { my @aa = <a b>; ("xb" ~~ /<@aa>/).Str }, " ", do { my @m = "b", "bc"; ("abcd" ~~ /a @m/).Str }, " ", do { my @m = "b", "bc"; ("abcd" ~~ /a [ @m ] d/).Str }, " ", do { my $n = 5; ("a5" ~~ /a $n/).Str }, " ", do { my @x = <c b>; ("abc" ~~ /a @x/).Str }, " ", do { my $q = "\\d"; ("a1" ~~ /a $q/).raku ~ ("a1" ~~ /a <$q>/).Str }, " ", do { my @m = "ab", "a"; ("abc" ~~ /@m/).Str }, " ", do { my @m = "ab", "a"; ("abc" ~~ /@m c/).Str }, " ", do { my @m = "a", "ab"; ("abc" ~~ /@m/).Str }, " ", do { my @m = rx/a+/, rx/ab/; ("aab" ~~ /@m/).Str }, " ", do { my @m = rx/a+/, rx/aab/; ("aab" ~~ /@m/).Str }, " ", do { my $z = rx/(b)/; ("abc" ~~ /a $z/).elems ~ ("abc" ~~ /a <$z>/).elems }, " ", do { my $z = rx/(b)/; ("abc" ~~ /a <z=$z>/).keys.raku }
# rakudo 2026.08: Nil a.c abc abc abc a.c abc ab Nil ab Nil ABC | bb bb ab ab Nil ab a1 X::Multi::NoMatch Nil abc abcd abcd "a b" Nil aaa Nil b abc abcd a5 ab Nila1 ab abc ab aa aab 00 ("z",).Seq
```
rakupp 4.0.1-84: differs — a capture inside an interpolated regex is kept (`.elems` 1), and `<{ Nil }>` is `X::AdHoc`.

### GM-30  Hash interpolation                                               D:partial R:partial V:quirk
A bare `%h` in a regex is a compile-time `X::Syntax::Reserved`. `<%h>`
compiles but matched nothing in every form tried (values True, "", or a
Regex; keys a prefix of the text): Nil every time — the quirk. Do not rely
on it.
```
my %h = (abc => rx/d/, ab => rx/x/); say ("abcd" ~~ /<%h>/).raku, " ", ("abcd" ~~ /<%h>/).keys.raku, " ", do { my %g = (ab => True, a => True); ("abcd" ~~ /<%g>/).raku }, " ", do { my %g = (ab => "", a => ""); ("abcd" ~~ /<%g>/).raku }, " ", do { my %g = (a => rx/b/); ("abcd" ~~ /<%g>/).raku }, " ", (try EVAL('my %g; "a" ~~ /%g/')) // $!.^name, " ", do { my %g = (a => rx/b/); ("abcd" ~~ /<%g>/)<a>.raku }
# rakudo 2026.08: Nil () Nil Nil Nil X::Syntax::Reserved Nil
```
rakupp 4.0.1-84: differs — `<%h>` matches zero-width and stores a capture named `%h`, and a bare `%h` is not refused.

### GM-31  token, regex, rule: ratchet, sigspace and `<ws>`                   D:yes R:yes V:spec
A plain `/…/` and a `regex` backtrack (`a+ a` matches "aaa"); `:r`,
`rx:ratchet`, and a `token` do not (Nil), and a `token` called as a subrule
keeps its ratchet even from a backtracking caller (`<tk2> a` is Nil) while a
`regex` subrule backtracks (`<rg> a` matches). Under `:r`, `a+!` re-enables
backtracking for that quantifier, `[ a+ || a ] a` and `a*? a` do not
backtrack into the group ("a" for the frugal case), and `[ a | aa ] a`
takes the longest alternative first. `rule`, `:s`, `rx:s`, `rx:sigspace`
and `:s` inside a `token` insert `<.ws>` after every atom including the
last, so a rule matches trailing blanks and `^<u>$` on "a  b " succeeds;
spaces inside a `token` are ignored. `<ws>` matches any run of blanks and
also zero width, but fails between two word characters (`ab`, `a1`, `1 1`
and `é é` decide by word-ness, `a_a` fails) and succeeds beside a
non-word character (`a+b`) and at either end. `a? b` under `:s` fails on
"ab" because `<.ws>` sits between `a?` and `b`.
```
say ("aaa" ~~ /a+ a/).Str, " ", ("aaa" ~~ /:r a+ a/).raku, " ", ("aaa" ~~ rx:ratchet/a+ a/).raku, " ", do { my token t { a+ a }; ("aaa" ~~ /<t>/).raku }, " ", do { my regex r { a+ a }; ("aaa" ~~ /<r>/).Str }, " ", do { my rule u { a b }; (("a b" ~~ /<u>/).Str.raku, ("ab" ~~ /<u>/).raku, ("a  b " ~~ /<u>/).Str.raku, ("a  b " ~~ /^<u>$/).Str.raku).join(",") }, " ", ("a b" ~~ /:s a b/).Str.raku, " ", ("ab" ~~ /:s a b/).raku, " ", ("a b" ~~ rx:s/a b/).Str.raku, " ", ("a b" ~~ rx:sigspace/a b/).Str.raku, " ", ("a  b" ~~ /a <ws> b/)<ws>.Str.raku, " ", ("ab" ~~ /a <ws> b/).raku, " ", ("a+b" ~~ /a <ws> '+' <ws> b/).Str, " ", (" a" ~~ /<ws> a/).Str.raku, " ", ("a " ~~ /a <ws>/).Str.raku, " ", ("a b" ~~ /a <.ws> b/).keys.raku, " ", do { my rule v { a+ b }; ("aa b" ~~ /^<v>$/).Str.raku }, " ", do { my rule w { 'a' 'b' }; ("a b" ~~ /^<w>$/).Str.raku }, " ", do { my token x { :s a b }; ("a b" ~~ /^<x>$/).Str.raku }, " ", do { my rule y { a? b }; ("b" ~~ /^<y>$/).Str.raku }, " ", ("a b" ~~ /:s a  b/).Str.raku, " ", ("a b" ~~ /:s 'a' 'b'/).Str.raku, " ", ("a b" ~~ /:s [a] [b]/).Str.raku, " ", ("ab" ~~ /:s ab/).Str.raku, " ", ("a b" ~~ /:s ab/).raku, " ", ("+ a" ~~ /'+' <ws> a/).Str.raku, " ", ("+a" ~~ /'+' <ws> a/).Str.raku, " ", ("a1" ~~ /a <ws> 1/).raku, " ", ("a 1" ~~ /a <ws> 1/).Str.raku, " ", ("1 1" ~~ /1 <ws> 1/).Str.raku, " ", ("é é" ~~ /é <ws> é/).Str.raku, " ", ("aa" ~~ /^ a <ws> a $/).raku, " ", ("a_a" ~~ /a <ws> "_"/).raku, " ", do { my rule z { ^ a b $ }; ("a b " ~~ /<z>/).Str.raku }, " ", do { my rule z2 { a b }; (" a b" ~~ /<z2>/).raku }, " ", ("ab" ~~ /:s a? b/).raku, " ", ("a b" ~~ /:s a?b/).raku, " ", do { my rule rl { <alpha>+ }; ("a b" ~~ /^<rl>$/).raku }, " ", do { my rule rl2 { <alpha>+ }; ("ab" ~~ /^<rl2>$/).Str.raku }, " ", do { my token tk { a 'b' }; ("ab" ~~ /<tk>/).Str }, " ", do { my regex rg { a+ }; ("aaa" ~~ /<rg> a/).Str }, " ", do { my token tk2 { a+ }; ("aaa" ~~ /<tk2> a/).raku }, " ", ("aaa" ~~ /:r [a+] a/).raku, " ", ("aaa" ~~ /:r a+: a/).raku, " ", ("aaa" ~~ /a+: a/).raku, " ", ("aaa" ~~ /:r a+! a/).raku, " ", ("aaa" ~~ /:r [ a+ || a ] a/).raku, " ", ("aaa" ~~ /:r [ a || aa ] a/).raku, " ", ("aaa" ~~ /:r [ a | aa ] a/).raku, " ", ("aaa" ~~ /:r a*? a/).raku
# rakudo 2026.08: aaa Nil Nil Nil aaa "a b",Nil,"a  b ","a  b " "a b" Nil "a b" "a b" "  " Nil a+b " a" "a " ().Seq "aa b" "a b" "a b" "b" "a b" "a b" "a b" "ab" Nil "+ a" "+a" Nil "a 1" "1 1" "é é" Nil Nil "a b " Match.new(:orig(" a b"), :from(1), :pos(4), :hash(Map.new((:z2(Match.new(:orig(" a b"), :from(1), :pos(4))))))) Nil Match.new(:orig("a b"), :from(2), :pos(3)) Nil "ab" ab aaa Nil Nil Nil Nil Match.new(:orig("aaa"), :from(0), :pos(3)) Nil Match.new(:orig("aaa"), :from(0), :pos(2)) Match.new(:orig("aaa"), :from(0), :pos(3)) Match.new(:orig("aaa"), :from(0), :pos(1))
```
rakupp 4.0.1-84: differs on three fields — a `regex` subrule called from a backtracking regex does not backtrack (`<rg> a` is ""), `a+!` under `:r` stays ratcheted (Nil), and `[ a+ || a ] a` under `:r` backtracks into the group (a Match); the 47 other fields match.

## F. Str.match and its adverbs

### GM-32  :g and :x                                                        D:partial R:yes V:spec
`:g` returns a List of Matches, `()` (a List, False, 0 elems) when nothing
matched, and sets `$/` to that List; each element has the full string as
`.orig` and its own `.prematch`/`.postmatch`; a zero-width regex advances
one character per match, so `x*` on "abc" matches 4 times (positions 0 to
3) and `a*` on "aaa" twice ("aaa" then "" at the end). `:x(n)` takes the
first n matches and answers `()` when fewer exist; `:x(0)` and a negative
count are `()`; `:x(m..n)` takes up to n and answers `()` when fewer than
m, `:x(m..*)` at least m, `:x(0..1)` at most one and `()` is still the
answer when nothing matched; `:x(*)` and `:x(Inf)` are all; a non-Int
Numeric is truncated. Anything else (`:x("a")`) is a Failure
`X::Str::Match::x` with `.got`, and `$/` is Nil. The lists are eager.
```
say "a1b2c3".match(/\d/, :g).^name, " ", "a1b2c3".match(/\d/, :g).raku, " ", "abc".match(/\d/, :g).raku, " ", "abc".match(/\d/, :g).^name, " ", "abc".match(/\d/, :g).Bool, " ", "abc".match(/\d/, :g).elems, " ", "a1b2c3".match(/\d/, :x(2)).raku, " ", "a1b2c3".match(/\d/, :x(2..3)).elems, " ", "a1b2c3".match(/\d/, :x(0)).raku, " ", "a1b2c3".match(/\d/, :x(*)).elems, " ", "a1b2c3".match(/\d/, :x(5)).raku, " ", "a1b2c3".match(/\d/, :x(2..5)).elems, " ", "a1b2c3".match(/\d/, :x(4..5)).raku, " ", "a1b2c3".match(/\d/, :x(Inf)).elems, " ", "a1b2c3".match(/\d/, :x(1)).^name, " ", "a1b2c3".match(/\d/, :x(1)).raku, " ", "a1b2c3".match(/\d/, :x(2.7)).elems, " ", do { "a1b2".match(/\d/, :g); $/.^name ~ $/.elems }, " ", "aaa".match(/a*/, :g).raku, " ", "abc".match(/x*/, :g).elems, " ", "abc".match(/x*/, :g).map(*.from).raku, " ", "a1b2c3".match(/\d/, :x(2)).^name, " ", "a1b2c3".match(/\d/, :g, :x(2)).elems, " ", "a1b2c3".match(/\d/, :x(2)).is-lazy, " ", "a1b2c3".match(/\d/, :g).is-lazy, " ", "a1b2c3".match(/\d/, :g)[1].prematch, " ", "a1b2c3".match(/\d/, :g)[1].postmatch, " ", "a1b2c3".match(/\d/, :g)[1].orig, " ", "a1b2c3".match(/(\d)/, :g)[2][0].Str, " ", "a1b2c3".match(/\d/, :x(1..*)).elems, " ", "abc".match(/\d/, :x(1..*)).raku, " ", "a1b2c3".match(/\d/, :x(0..1)).elems, " ", "abc".match(/\d/, :x(0..1)).raku, " ", "abc".match(/\d/, :x(0)).raku, " ", "a1".match(/\d/, :x(-1)).raku, " ", (try { my $f = "a1".match(/\d/, :x("a")); $f.so; $f.^name ~ ":" ~ $f.exception.^name ~ ":" ~ $f.exception.got.raku }) // $!.^name, " ", (try { my $f = "a1".match(/\d/, :x("a")); $f.so; $/.raku }) // $!.^name
# rakudo 2026.08: List (Match.new(:orig("a1b2c3"), :from(1), :pos(2)), Match.new(:orig("a1b2c3"), :from(3), :pos(4)), Match.new(:orig("a1b2c3"), :from(5), :pos(6))) () List False 0 (Match.new(:orig("a1b2c3"), :from(1), :pos(2)), Match.new(:orig("a1b2c3"), :from(3), :pos(4))) 3 () 3 () 3 () 3 List (Match.new(:orig("a1b2c3"), :from(1), :pos(2)),) 2 List2 (Match.new(:orig("aaa"), :from(0), :pos(3)), Match.new(:orig("aaa"), :from(3), :pos(3))) 4 (0, 1, 2, 3).Seq List 2 False False a1b c3 a1b2c3 3 3 () 1 () () () X::Str::Match::x X::Str::Match::x
```
rakupp 4.0.1-84: matches on every field, including the `X::Str::Match::x` failure.

### GM-33  :nth, :st/:nd/:rd/:th, and the :nth+:x bug                        D:yes R:yes V:bug
`:nth(n)` (aliases `:st`, `:nd`, `:rd`, `:th`, `:1st`, `:2nd`, …) returns
the n-th match or Nil past the end; a non-Int Numeric is truncated; `Inf`
and `*` mean the last; `*-1` the one before it, and `*-k` beyond the first
is Nil; a Callable is called for n. An Iterable (`2..3`, `(1,3)`, `2..*`)
returns a List of the numbered matches (empty for an empty or exhausted
range) and sets `$/` to it. `:nth(0)`, a negative n, a range starting at
0, and a non-increasing list (`(3,1)`, `(2,2)`) throw `X::AdHoc`, the last
two lazily when the list is reified. `:nth(n), :g` is the single n-th
match. With `:x` on top, an Iterable `:nth` is filtered to `:x` elements
(`:nth(2..3), :x(1)` is a List of one), but a SINGLE `:nth(2), :x(1)`
answers `()` although the second match exists — the bug: answer the Match
(or a one-element List) there, never `()`.
```
say "a1b2c3".match(/\d/, :nth(2)).Str, " ", "a1b2c3".match(/\d/, :nth(5)).raku, " ", "a1b2c3".match(/\d/, :nth(2..3)).raku, " ", "a1b2c3".match(/\d/, :nth(2..9)).elems, " ", "a1b2c3".match(/\d/, :nth(1,3)).raku, " ", "a1b2c3".match(/\d/, :1st).Str, " ", "a1b2c3".match(/\d/, :2nd).Str, " ", "a1b2c3".match(/\d/, :3rd).Str, " ", "a1b2c3".match(/\d/, :4th).raku, " ", "a1b2c3".match(/\d/, :st(2)).Str, " ", "a1b2c3".match(/\d/, :nth(Inf)).Str, " ", "a1b2c3".match(/\d/, :nth(2.9)).Str, " ", "abc".match(/\d/, :nth(2)).raku, " ", "abc".match(/\d/, :nth(2..3)).raku, " ", do { "a1b2".match(/\d/, :nth(2)); $/.Str }, " ", do { "a1b2".match(/\d/, :nth(1,2)); $/.^name ~ $/.elems }, " ", "a1b2c3".match(/\d/, :nth(2..1)).raku, " ", "a1b2c3".match(/\d/, :nth(2..*)).elems, " ", "a1b2c3".match(/\d/, :nth(2..3)).^name, " ", "a1b2c3".match(/\d/, :nth(1..*)).elems, " ", "a1b2c3".match(/\d/, :nth(2), :g).raku, " ", "a1b2c3".match(/\d/, :nth(2, 3), :x(1)).raku, " ", "a1b2c3".match(/\d/, :nth(2..3), :x(1)).raku, " ", "a1b2c3".match(/\d/, :nth(1..3), :x(2)).elems, " ", "a1b2c3".match(/\d/, :nth(2), :x(1)).raku; say "a1b2c3".match(/\d/, :nth(*)).Str, " ", "a1b2c3".match(/\d/, :nth(*-1)).Str, " ", "a1b2c3".match(/\d/, :nth(*-2)).Str, " ", "a1b2c3".match(/\d/, :nth(*-3)).raku, " ", "a1b2c3".match(/\d/, :nth(*-5)).raku, " ", "abc".match(/\d/, :nth(*)).raku, " ", "a1b2c3".match(/\d/, :nth({ 2 })).Str
# rakudo 2026.08: 2 Nil (Match.new(:orig("a1b2c3"), :from(3), :pos(4)), Match.new(:orig("a1b2c3"), :from(5), :pos(6))) 2 (Match.new(:orig("a1b2c3"), :from(1), :pos(2)), Match.new(:orig("a1b2c3"), :from(5), :pos(6))) 1 2 3 Nil 2 3 2 Nil () 2 List2 () 2 List 3 Match.new(:orig("a1b2c3"), :from(3), :pos(4)) (Match.new(:orig("a1b2c3"), :from(3), :pos(4)),) (Match.new(:orig("a1b2c3"), :from(3), :pos(4)),) 2 ()|3 2 1 Nil Nil Nil 2
say (try "a1b2c3".match(/\d/, :nth(0))) // $!.^name, " ", (try "a1b2c3".match(/\d/, :nth(0)).raku) // $!.message, " ", (try "a1b2c3".match(/\d/, :nth(-1)).raku) // $!.^name, " ", (try "a1b2c3".match(/\d/, :nth(0..2)).raku) // $!.^name, " ", (try "a1b2c3".match(/\d/, :nth(3,1)).raku) // $!.^name, " ", (try "a1b2c3".match(/\d/, :nth(2,2)).raku) // $!.^name, " ", (try "a1b2c3".match(/\d/, :nth(3,1)).raku) // $!.message
# rakudo 2026.08: X::AdHoc Attempt to retrieve before :1st match -- :nth(0) X::AdHoc X::AdHoc X::AdHoc X::AdHoc Attempt to fetch match #1 after #3
```
rakupp 4.0.1-84: differs — a `:nth` past the end is `()` instead of Nil, `:nth(Inf)` is empty, `:nth(2), :x(1)` is the Match (the sane answer), and `:nth(*)`, `:nth(*-1)` and `:nth({ 2 })` die with "out of order", a die that escapes `try` (second line of the first probe died there); the refusal cases all throw as Rakudo does.

### GM-34  :c, :p, :ov, :ex, :as, a Str pattern, and :x(Nil)                 D:partial R:partial V:bug
`:c(n)` (`:continue`) starts scanning at n, `:p(n)` (`:pos`) anchors the
match at n (Nil unless the regex matches exactly there); both accept a
position equal to the length for a zero-width regex (`$`); past the end is
Nil; a negative `:c` is Nil; `:c(Nil)` and `:p(Nil)` mean 0. `:ov`
(`:overlap`) lists one match per start position, `:ex` (`:exhaustive`)
every match at every position, both eager Lists and `()` when nothing
matches. `:as(Str)` returns the matched text instead of the Match (Nil on
no match); any other `:as` value returns the Match. A Str or Cool pattern
matches literally (`"a.c".match(".")` finds the dot, `.match(1)` works)
and `.orig` is the string. `:g(False)`, `:!g`, `:ov(False)` and
`:nth(Nil)` behave as absent. `:x(Nil)` is an `X::AdHoc` arity error — the
bug: treat an undefined `:x` as absent, as the other adverbs are.
```
say "aXbXc".match(/X/, :c(2)).from, " ", "aXbXc".match(/X/, :c(5)).raku, " ", "aXbXc".match(/X/, :p(1)).from, " ", "aXbXc".match(/X/, :p(2)).raku, " ", "aXbXc".match(/X/, :p(3)).from, " ", "aXbXc".match(/X/, :c(9)).raku, " ", "aXbXc".match(/X/, :p(9)).raku, " ", (try "aXbXc".match(/X/, :c(-1)).raku) // $!.^name, " ", "aaa".match(/a+/, :ov).raku, " ", "aaa".match(/a+/, :ex).raku, " ", "aaa".match(/a+/, :ex).elems, " ", "aaa".match(/a+/, :ov).elems, " ", "abc".match(/x/, :ov).raku, " ", "abc".match(/x/, :ex).raku, " ", "a1b2".match(/\d/, :as(Str)).raku, " ", "a1b2".match(/\d/, :as(Str)).^name, " ", "a1b2".match(/\d/, :as(Int)).^name, " ", "abc".match(/\d/, :as(Str)).raku, " ", "abc".match("b").Str, " ", "abc".match("b").^name, " ", "a.c".match(".").from, " ", "abc".match(".").raku, " ", "aXbXc".match(/X/, :continue(2)).from, " ", "aXbXc".match(/X/, :pos(1)).from, " ", "aXbXc".match(/X/, :global).elems, " ", "aaa".match(/a+/, :overlap).elems, " ", "aaa".match(/a+/, :exhaustive).elems, " ", "aXbXc".match(/X/, :c(4)).raku, " ", "aXbXc".match(/X/, :c(3)).from, " ", "ab".match(/$/, :c(2)).from, " ", "ab".match(/$/, :p(2)).from, " ", "abc".match(/b/, :c(0)).from, " ", "abc".match(/b/, :p(0)).raku, " ", (try "a1".match(1).Str) // $!.^name, " ", (try "a1".match(1).orig.^name) // $!.^name, " ", (try "aXbXc".match(/X/, :x(Nil)).^name) // $!.^name
# rakudo 2026.08: 3 Nil 1 Nil 3 Nil Nil Nil (Match.new(:orig("aaa"), :from(0), :pos(3)), Match.new(:orig("aaa"), :from(1), :pos(3)), Match.new(:orig("aaa"), :from(2), :pos(3))) (Match.new(:orig("aaa"), :from(0), :pos(3)), Match.new(:orig("aaa"), :from(0), :pos(2)), Match.new(:orig("aaa"), :from(0), :pos(1)), Match.new(:orig("aaa"), :from(1), :pos(3)), Match.new(:orig("aaa"), :from(1), :pos(2)), Match.new(:orig("aaa"), :from(2), :pos(3))) 6 3 () () "1" Str Match Nil b Match 1 Nil 3 1 2 3 6 Nil 3 2 2 1 Nil 1 Str X::AdHoc
```
rakupp 4.0.1-84: differs — `:ov` and `:ex` return a single Match (`.elems` 0), `:as(Str)` returns the Match, the long names `:overlap`/`:exhaustive` count 0, `.match(1)` is `X::Method::NotFound`, and `:x(Nil)` is `X::Str::Match::x`.

### GM-35  Adverb combinations                                              D:no R:no V:spec
`:c`/`:p` combine with `:g` (matches from that point; `:p(2), :g` is `()`
when nothing matches at 2), with `:x` (a List) and with each other (`:p`
wins over `:c`). `:ov, :x(2)` takes two overlapping matches; `:ex, :nth(2)`
is the second exhaustive match; `:ov, :g` and `:ex, :ov` are the overlap
and exhaustive lists (`:ex` wins); `:as(Str)` combines with `:g` (a List of
Strs) and `:nth` (a Str); `:g, :nth(2)` is the single second match;
`:x(2), :c(2)` counts from position 2. `:c(Nil)`/`:p(Nil)` are 0;
`:ov(False)`, `:g(False)`, `:!g` and `:nth(Nil)` are absent.
```
say "aXbXc".match(/X/, :c(2), :g).elems, " ", "aXbXc".match(/X/, :p(1), :g).elems, " ", "aXbXc".match(/X/, :p(2), :g).raku, " ", "aaa".match(/a+/, :ov, :x(2)).elems, " ", "aaa".match(/a+/, :ex, :nth(2)).Str, " ", "a1b2".match(/\d/, :as(Str), :g).raku, " ", "a1b2".match(/\d/, :c(1), :p(1)).raku, " ", "aXbXc".match(/X/, :c(2), :p(1)).from, " ", "aXbXc".match(/X/, :p(3), :c(1)).from, " ", "aaa".match(/a+/, :ov, :g).elems, " ", "aaa".match(/a+/, :ex, :ov).elems, " ", "aXbXc".match(/X/, :c(2), :x(1)).raku, " ", "a1b2".match(/\d/, :as(Str), :nth(2)).raku, " ", "a1b2c3".match(/\d/, :g, :nth(2)).raku, " ", "a1b2c3".match(/\d/, :x(2), :c(2)).elems, " ", "a1b2c3".match(/\d/, :ov, :ex).elems, " ", "aXbXc".match(/X/, :c(Nil)).from, " ", "aXbXc".match(/X/, :p(Nil)).from, " ", "aXbXc".match(/X/, :ov(False)).^name, " ", (try "aXbXc".match(/X/, :g(False)).^name) // $!.^name, " ", (try "aXbXc".match(/X/, :!g).^name) // $!.^name, " ", (try "aXbXc".match(/X/, :nth(Nil)).^name) // $!.^name
# rakudo 2026.08: 1 2 () 2 aa ("1", "2") Match.new(:orig("a1b2"), :from(1), :pos(2)) 1 3 3 6 (Match.new(:orig("aXbXc"), :from(3), :pos(4)),) "2" Match.new(:orig("a1b2c3"), :from(3), :pos(4)) 2 3 Nil 1 Match Match Match Match
```
rakupp 4.0.1-84: differs — `:ov, :x(2)` is 0, `:ex, :nth(2)` "aaa", `:as(Str), :g` a Match, `:p`+`:c` and `:ov, :g`/`:ex, :ov` wrong, `:as(Str), :nth(2)` a Match, `:ov, :ex` 0, and `:g(False)`/`:!g` are refused with `X::Syntax::Regex::Adverb`.

### GM-36  The m// form and its adverbs; list context                        D:yes R:yes V:spec
`m:g/…/` against `$_` is a List of Matches (`()` and False when none), so
`for m:g/…/` iterates the Matches, `my @m = m:g/…/` fills an Array and
`.elems` counts; `m:x(2)`, `m:nth(2)`, `m:2nd`, `m:st(2)`, `m:c(3)`,
`m:continue(3)`, `m:p(1)`, `m:pos(3)` (Nil when not matching there),
`m:ov`, `m:overlap`, `m:ex`, `m:exhaustive`, `m:global`, `m:i` and `m:s`
mean what the method adverbs mean, and adverbs stack (`m:x(2):g`,
`m:g:i`). `m:P5` and `m:Perl5` take Perl 5 syntax. `m//` evaluates at once:
the Match is fixed even if `$_` changes afterwards; on failure `m//` is Nil
and False, and `m:g` empty.
```
$_ = "a1b2c3"; say (m:g/\d/).^name, " ", (m:g/\d/).elems, " ", (m:g/\d/).join(","), " ", (m:x(2)/\d/).elems, " ", (m:nth(2)/\d/).Str, " ", (m:2nd/\d/).Str, " ", (m:c(3)/\d/).Str, " ", (m:p(1)/\d/).Str, " ", (m:p(0)/\d/).raku, " ", (m:ov/\d\D?/).elems, " ", (m:ex/\d\D?/).elems, " ", (m:i/B/).Str, " ", (m:s/b 2/).raku, " ", (m:g/x/).raku, " ", (m:g/x/).^name, " ", (so m:g/x/), " ", (m:exhaustive/\d/).elems, " ", (m:global/\d/).elems, " ", (m:continue(3)/\d/).Str, " ", (m:pos(3)/\d/).Str, " ", (m:overlap/\d/).elems, " ", (m:st(2)/\d/).Str, " ", do { my @m = m:g/\d/; @m.elems ~ @m[0].^name }, " ", do { my $c = 0; for m:g/\d/ { $c++ }; $c }, " ", do { my @s; for m:g/\d/ -> $m { @s.push($m.from) }; @s.raku }, " ", do { my $x = m:g/\d/; $x.^name }, " ", (m:g/(\d)/)[1][0].Str, " ", (m:g/\d/)[0].prematch, " ", (m:g/\d/)[1].postmatch, " ", (m:nth(2)/\d/).prematch, " ", (try (m:x(2):nth(1)/\d/).raku) // $!.^name, " ", (m:x(2):g/\d/).elems, " ", (m:P5/\d+/).Str, " ", (m:Perl5/(\d)/)[0].Str, " ", do { $_ = "abc"; (m:P5/a.c/).Str }, " ", do { $_ = "a1b2c3"; (m:g:i/B/).elems }, " ", do { $_ = "a1"; my $m = m/\d/; $_ = "b2"; $m.Str }, " ", do { $_ = "abc"; (m/x/).^name ~ (m/x/).Bool }, " ", do { $_ = "abc"; my $c = 0; for m:g/x/ { $c++ }; $c }, " ", do { $_ = "abc"; (m:g/x/).elems }, " ", do { $_ = "abc"; my @m = m:g/x/; @m.elems }
# rakudo 2026.08: List 3 1,2,3 2 2 2 2 1 Nil 3 5 b Nil () List False 3 3 2 2 3 2 3Match 3 [1, 3, 5] List 2 a c3 a1b () 2 1 1 abc 1 1 NilFalse 0 0 0
```
rakupp 4.0.1-84: differs — `m:x(2)` yields 1, `m:c(3)`, `m:p(1)`, `m:continue(3)`, `m:pos(3)` and `m:st(2)` yield nothing, `m:x(2):g` is empty and `m:x(2):nth(1)` Nil.

## G. subst, comb, split, trans and the rest

### GM-37  .subst                                                            D:partial R:yes V:spec
`.subst($m, $r)` returns a new Str and leaves the invocant alone; with no
match it returns the invocant unchanged (the same object). With a Regex it
sets the caller's `$/` to the Match (a List under `:g`, Nil when nothing
matched); a Str matcher leaves `$/` alone. A Callable replacement is
called per match with the Match as `$_` and as its argument (a
zero-arity one is called without it) and sees `$/`, `$0`, `$<n>`; `$¢` is
Nil there; a non-Str replacement is stringified (42, Nil as "") and the
default is "". `:nth`, `:x`, `:c`, `:p` select as in `.match` (`:nth(5)`
past the end and `:x(3)` with fewer matches change nothing; `:nth(1..2)`
and `:x(2..3)` replace the listed ones; `:nth(2), :g` the second only).
`:ov` and `:ex` throw `X::Str::Subst::Adverb` (`.name` "ov", `.got`
True); `:ov(False)` is fine. `:ii`/`:samecase` copies the case of the
matched text onto the replacement, `:ss`/`:samespace` copies its
whitespace; neither implies `:i` or `:s` in the method form, so
`"Hello".subst(/h/, "j", :ii)` changes nothing.
```
my $s = "a1b2"; say $s.subst(/\d/, "X"), " ", $s, " ", $s.subst(/\d/, "X", :g), " ", $s.subst(/\d/, "X").^name, " ", $s.subst(/x/, "X"), " ", ($s.subst(/x/, "X") === $s), " ", do { $s.subst(/\d/, "X"); $/.Str }, " ", do { $s.subst(/\d/, "X", :g); $/.^name ~ $/.elems }, " ", do { "q" ~~ /q/; $s.subst(/x/, "X"); $/.raku }, " ", do { "q" ~~ /q/; $s.subst("x", "X"); $/.Str }, " ", $s.subst(/(\d)/, { "<$0>" }), " ", $s.subst(/(\d)/, { "<$0>" }, :g), " ", $s.subst(/\d/, -> $m { $m.Str + 1 }, :g), " ", $s.subst(/\d/, { $/.Str * 2 }, :g), " ", $s.subst(/\d/, *.Str.succ, :g), " ", $s.subst(/\d/, "X", :nth(2)), " ", $s.subst(/\d/, "X", :x(1)), " ", $s.subst(/\d/, "X", :nth(5)), " ", $s.subst(/\d/, "X", :x(3)), " ", $s.subst(/\d/, "X", :nth(2), :g), " ", $s.subst(/\d/, "X", :nth(1..2)), " ", $s.subst(/\d/, "X", :c(2)), " ", $s.subst(/\d/, "X", :p(1)), " ", $s.subst(/\d/, "X", :p(0)), " ", "Hello".subst(/h/, "j", :ii), " ", "Hello".subst(/:i h/, "j", :ii), " ", "HELLO".subst(/:i hel/, "jum", :samecase), " ", "a  b".subst(/:s a b/, "x y", :ss), " ", "a  b".subst(/:s a b/, "x y", :samespace), " ", "abc".subst("b", "X"), " ", "abcb".subst("b", "X", :g), " ", "abcb".subst("b", "X", :nth(2)), " ", "abc".subst("x", "X"), " ", "aaa".subst(/a/, "b", :x(2)), " ", "a1b2".subst(/\d/, 42, :g), " ", (quietly { "a1b2".subst(/\d/, Nil, :g) }), " ", "a1b2".subst(/\d/), " ", "a1b2".subst(/\d/, :g), " ", "abc".subst(/b/, { $/.from }), " ", "abc".subst(/b/, { $¢.^name }), " ", "a1b2".subst(/\d/, "X", :x(2..3)), " ", "a1b2".subst(/\d/, "X", :x(1..2)), " ", "ABC".subst(/:i b/, "x", :ii), " ", "abc".subst(/b/, "X", :samecase), " ", "a1b2".subst(/\d/, { $/ + 1 }, :g), " ", "a1b2".subst(/(\d)/, { $0 + 1 }, :g), " ", "a1".subst(/\d/, { $_ }), " ", do { my @s; "a1b2".subst(/\d/, { @s.push($/.from); "" }, :g); @s.raku }, " ", (try $s.subst(/\d/, "X", :ov(False))) // $!.^name, " ", (try $s.subst(/\d/, "X", :ov)) // $!.^name, " ", (try $s.subst(/\d/, "X", :ex)) // $!.^name, " ", (try $s.subst(/\d/, "X", :ov)) // ($!.?name // "-") ~ ":" ~ ($!.?got // "-")
# rakudo 2026.08: aXb2 a1b2 aXbX Str a1b2 True 1 List2 Nil q a<1>b2 a<1>b<2> a2b3 a2b4 a2b3 a1bX aXb2 a1b2 a1b2 a1bX aXbX a1bX aXb2 a1b2 Hello Jello JUMLO x  y x  y aXc aXcX abcX abc bba a42b42 ab ab2 ab a1c aNilc aXbX aXbX AXC axc a2b3 a2b3 a1 [1, 3] aXb2 X::Str::Subst::Adverb X::Str::Subst::Adverb ov:True
```
rakupp 4.0.1-84: differs — `:ii` implies `:i` in the method form ("Jello"), `$¢` inside the replacement is Any, `:ov`/`:ex` throw `X::Syntax::Regex::Adverb` without `.name`/`.got`, and `:ov(False)` is refused.

### GM-38  s///, S///, subst-mutate                                           D:partial R:partial V:spec
`$t ~~ s/…/…/` mutates `$t` and returns the Match, or `Bool::False`
(`.^name` Bool) when nothing matched; `s:g///` returns the List of
Matches, `()` when none; `$/` is set the same way (Nil after a failure).
The replacement side is a double-quoted string: `$0`, `{ … }` and
`$/.from()` method calls interpolate; the `s[…] = expr` form evaluates an
expression per match. `S///` returns the new string, leaves the topic and
sets `$/`; it is applied to `$_` (use `given`, not `~~`). `s///` on a
literal or a bound Str is `X::Assignment::RO`. `s:ii///` implies `:i`,
`s:s///` sigspace; `s:nth(2)`, `s:x(2)`, `s:2nd` select; `s:ov///` is a
compile-time `X::Syntax::Regex::Adverb`. `.subst-mutate` mutates and
returns the Match, a List under `:g`, or Nil / `()` when nothing matched
(assigning that Nil to a scalar reads Any); a Str matcher still yields a
Match.
```
my $s = "a1b2"; my $r = ($s ~~ s/\d/X/); say $r.^name, " ", $r.raku, " ", $s, " ", do { my $t = "a1b2"; my $r2 = ($t ~~ s:g/\d/X/); $r2.^name ~ " " ~ $r2.elems ~ " " ~ $t }, " ", do { my $t = "abc"; my $r3 = ($t ~~ s/\d/X/); $r3.raku ~ " " ~ $t ~ " " ~ $r3.^name ~ " " ~ $r3.Bool }, " ", do { my $t = "abc"; my $r4 = ($t ~~ s:g/\d/X/); $r4.raku ~ " " ~ $r4.^name ~ " " ~ $r4.Bool ~ " " ~ $t }, " ", do { my $t = "a1b2"; $t ~~ s/(\d)/<$0>/; $t ~ " " ~ $/.Str }, " ", do { my $t = "a1b2"; $t ~~ s:g/(\d)/<$0>/; $t ~ " " ~ $/.^name ~ $/.elems }, " ", do { my $t = "a1b2"; $t ~~ s/\d/{ $/.Str + 10 }/; $t }, " ", do { my $t = "a1b2"; $t ~~ s[\d] = "Y"; $t }, " ", do { my $t = "a1b2"; $t ~~ s:g[\d] = $/.Str ~ "!"; $t }, " ", do { my $t = "a1b2"; my $u = S/\d/Z/ given $t; $u ~ " " ~ $t ~ " " ~ $u.^name }, " ", do { $_ = "a1b2"; my $u = S:g/\d/Z/; $u ~ " " ~ $_ }, " ", do { $_ = "a1b2"; s/\d/Q/; "" ~ $_ }, " ", do { $_ = "a1b2"; my $v = s:g/\d/Q/; $v.^name ~ $v.elems ~ $_ }, " ", do { my $t = "a1b2"; my $m = $t.subst-mutate(/\d/, "M"); $m.^name ~ " " ~ $m.Str ~ " " ~ $t }, " ", do { my $t = "a1b2"; my $m = $t.subst-mutate(/\d/, "M", :g); $m.^name ~ " " ~ $m.elems ~ " " ~ $t }, " ", do { my $t = "abc"; ($t.subst-mutate(/\d/, "M")).raku ~ " " ~ $t }, " ", do { my $t = "abc"; ($t.subst-mutate(/\d/, "M", :g)).raku ~ " " ~ $t }, " ", do { my $t = "abc"; $t.subst-mutate("b", "M"); $t }, " ", do { my $t = "abc"; my $m = $t.subst-mutate("b", "M"); $m.^name }, " ", do { my $t = "abc"; $t ~~ s:nth(2)/\w/x/; $t }, " ", do { my $t = "abc"; $t ~~ s:x(2)/\w/x/; $t }, " ", do { my $t = "abc"; $t ~~ s:2nd/\w/x/; $t }, " ", do { my $t = "abc"; $t ~~ s:ii/B/z/; $t }, " ", do { my $t = "Abc"; $t ~~ s:ii:i/a/z/; $t }, " ", (try { "abc" ~~ s/b/x/; "ok" }) // $!.^name, " ", (try { my $t := "abc"; $t ~~ s/b/x/; "ok" }) // $!.^name, " ", do { my $t = "abc"; my $r5 = $t ~~ s/b/x/; $r5.Str ~ $r5.from }, " ", do { my $t = "abc"; $t ~~ s/(b)/$0$0/; $t }, " ", do { my $t = "abc"; $t ~~ s/b/$/.from()/; $t }, " ", do { my $t = "abc"; $t ~~ s:s/b c/X/; $t }, " ", do { my $t = "a b c"; $t ~~ s:s/b c/X/; $t }, " ", do { my $t = "abc"; ($t ~~ s/x/y/).^name ~ $t }, " ", do { my $t = "abc"; ($t ~~ s/x/y/).raku }, " ", do { my $t = "abc"; ($t ~~ s:g/x/y/).raku }, " ", (try EVAL('my $t = "abc"; $t ~~ s:ov/b/x/; $t')) // $!.^name, " ", do { my $t = "abc"; "q" ~~ /q/; $t ~~ s/x/y/; $/.raku }, " ", do { my $t = "a1b2"; my $n = ($t ~~ s:g/\d/X/).elems; $n }, " ", do { my $t = "abc"; "q" ~~ /q/; $t ~~ s/b/y/; $/.Str }
# rakudo 2026.08: Match Match.new(:orig("a1b2"), :from(1), :pos(2)) aXb2 List 2 aXbX Bool::False abc Bool False $( ) List False abc a<1>b2 1 a<1>b<2> List2 a11b2 aYb2 a1!b2! aZb2 a1b2 Str aZbZ a1b2 aQb2 List2aQbQ Match 1 aMb2 List 2 aMbM Nil abc () abc aMc Match axc xxc axc azc Zbc X::Assignment::RO X::Assignment::RO b1 abbc a1c abc a X Boolabc Bool::False () X::Syntax::Regex::Adverb Nil 2 b
```
rakupp 4.0.1-84: differs — a failed `s///` returns something whose `.raku` is Any rather than `Bool::False`, `s/b/x/` on a literal succeeds, and `$/.from()` in the replacement is not interpolated (`ab.from()c`).

### GM-39  .comb with a Regex                                               D:yes R:yes V:spec
`.comb(/re/)` is an eager Seq of the matched texts (`<( )>` respected);
`:match` gives the Match objects with their captures; a limit takes the
first n (0 or negative gives an empty Seq, `*`/`Inf` all, a Rat truncates);
no match is an empty Seq; a zero-width regex yields one "" per position
(4 for "abc"); `a*` on "aaa" gives "aaa" and "". `$/` is left alone.
```
say "a1b22c".comb(/\d+/).raku, " ", "a1b22c".comb(/\d+/).^name, " ", "a1b22c".comb(/\d+/, :match).raku, " ", "a1b22c".comb(/\d+/, :match)[0].^name, " ", "a1b22c".comb(/(\d)(\d)/, :match)[0][1].Str, " ", "a1b22c".comb(/(\d)(\d)/).raku, " ", "a1b22c".comb(/\d/, 2).raku, " ", "a1b22c".comb(/\d/, :match, 2).elems, " ", "abc".comb(/\d/).raku, " ", "abc".comb(/\d/, :match).raku, " ", "abc".comb(/\d/).elems, " ", "abc".comb(/x*/).raku, " ", "abc".comb(/x*/).elems, " ", "aaa".comb(/a*/).raku, " ", "a1b22c".comb(/\d/, 0).raku, " ", "a1b22c".comb(/\d/, *).elems, " ", "a1b22c".comb(/\d/, Inf).elems, " ", "xxabcxx".comb(/x <( \w )> x/).raku, " ", "a1b2".comb(/\d/, :!match).raku, " ", "a1b2".comb(/\d/, :match).is-lazy, " ", do { "q" ~~ /q/; "a1b2".comb(/\d/).eager; $/.Str }, " ", "a1b2".comb(/(\d)/, :match).map(*[0].Str).raku, " ", "a1b2".comb(/\d/).is-lazy, " ", "a1b22c".comb(/\d/, 2, :match).elems, " ", "a1b22c".comb(/\d/, -1).raku, " ", "a1b22c".comb(/\d/, 2.5).raku, " ", "ab".comb(/./, :match).map(*.pos).raku, " ", "aXbXc".comb(/X <( \w/).raku, " ", "a1b2".comb(rx/\d/).raku, " ", "a1b2".comb(/\d/).map(*.^name).raku
# rakudo 2026.08: ("1", "22").Seq Seq (Match.new(:orig("a1b22c"), :from(1), :pos(2)), Match.new(:orig("a1b22c"), :from(3), :pos(5))).Seq Match 2 ("22",).Seq ("1", "2").Seq 2 ().Seq ().Seq 0 ("", "", "", "").Seq 4 ("aaa", "").Seq ().Seq 3 3 ().Seq ("1", "2").Seq False q ("1", "2").Seq False 2 ().Seq ("1", "2").Seq (1, 2).Seq ("b", "c").Seq ("1", "2").Seq ("Str", "Str").Seq
```
rakupp 4.0.1-84: differs — `:match` returns a List rather than a Seq (`.raku` without `.Seq`, `()` for no match); every other field matches.

### GM-40  .split with a Regex                                              D:yes R:yes V:spec
`.split(/re/)` is an eager Seq of the pieces between matches, captures not
included (unlike `:v`); leading and trailing empty pieces are kept
(`"1a2"` gives "", "a", ""), `:skip-empty` drops them; no match gives the
whole string; a zero-width regex splits between every character with ""
at both ends; `^`, `$` and a lookahead split at their positions. `:v`
interleaves the Match objects (with captures, `.from` in the whole
string), `:k` interleaves 0 (the delimiter index), `:kv` both, `:p`
`0 => Match`; two of `:v`/`:k`/`:kv`/`:p` together are `X::Adverb`. A
limit gives at most n pieces (1 is the whole string, 0 or negative an
empty Seq, `*`/`Inf` all, a Rat truncates), `:end` counts from the end. A
List of needles (regexes or strings) splits on all of them, and `:v` then
gives the matched TEXT, not a Match.
```
say "a1b2c".split(/\d/).raku, " ", "a1b2c".split(/\d/).^name, " ", "a1b2c".split(/\d/, :v).raku, " ", "a1b2c".split(/\d/, :v)[1].^name, " ", "a1b2c".split(/\d/, :k).raku, " ", "a1b2c".split(/\d/, :kv).raku, " ", "a1b2c".split(/\d/, :p).raku, " ", "a1b2c".split(/\d/, :p)[1].^name, " ", "a1b2c".split(/\d/, :p)[1].key, " ", "a1b2c".split(/(\d)/).raku, " ", "a1b2c".split(/(\d)/, :v)[1][0].Str, " ", "a1b2c".split(/\d/, 2).raku, " ", "a1b2c".split(/\d/, 1).raku, " ", "a1b2c".split(/\d/, 0).raku, " ", "a1b2c".split(/\d/, 2, :v).raku, " ", "1a2".split(/\d/).raku, " ", "1a2".split(/\d/, :skip-empty).raku, " ", "1a2".split(/\d/, :skip-empty, :v).raku, " ", "abc".split(/x/).raku, " ", "abc".split(/x/, :v).raku, " ", "abc".split(/x*/).raku, " ", (try "abc".split(/x/, :v, :k).raku) // $!.^name, " ", "a1b2c".split(/\d/, 2, :end).raku, " ", "a b".split(/\s/, :v).raku, " ", "xxabcxx".split(/x <( \w )> x/).raku, " ", "a1b2".split(/\d/, :kv, :skip-empty).raku, " ", "a1b".split(/\d/, :v).map(*.^name).raku, " ", "a1b".split(/\d/, :p).map(*.^name).raku, " ", "a1b".split(/\d/, :k).map(*.^name).raku, " ", "a1b".split(/\d/, :v)[1].from, " ", "a,b;c".split((/","/, /";"/)).raku, " ", "a,b;c".split((/","/, ";")).raku, " ", "a,b".split((/","/,), :v)[1].^name, " ", "abc".split(/b/, :v).elems, " ", "abc".split(/^/).raku, " ", "abc".split(/$/).raku, " ", "abc".split(/<?before b>/).raku, " ", "a1b2c".split(/\d/, -1).raku, " ", "a1b2c".split(/\d/, *).raku, " ", "a1b2c".split(/\d/, Inf).elems, " ", "a1b2c".split(/\d/, 2.7).elems, " ", "a1b2c".split(/\d/).is-lazy
# rakudo 2026.08: ("a", "b", "c").Seq Seq ("a", Match.new(:orig("a1b2c"), :from(1), :pos(2)), "b", Match.new(:orig("a1b2c"), :from(3), :pos(4)), "c").Seq Match ("a", 0, "b", 0, "c").Seq ("a", 0, Match.new(:orig("a1b2c"), :from(1), :pos(2)), "b", 0, Match.new(:orig("a1b2c"), :from(3), :pos(4)), "c").Seq ("a", 0 => Match.new(:orig("a1b2c"), :from(1), :pos(2)), "b", 0 => Match.new(:orig("a1b2c"), :from(3), :pos(4)), "c").Seq Pair 0 ("a", "b", "c").Seq 1 ("a", "b2c").Seq ("a1b2c",).Seq ().Seq ("a", Match.new(:orig("a1b2c"), :from(1), :pos(2)), "b2c").Seq ("", "a", "").Seq ("a",).Seq (Match.new(:orig("1a2"), :from(0), :pos(1)), "a", Match.new(:orig("1a2"), :from(2), :pos(3))).Seq ("abc",).Seq ("abc",).Seq ("", "a", "b", "c", "").Seq X::Adverb ("a1b", "c").Seq ("a", Match.new(:orig("a b"), :from(1), :pos(2)), "b").Seq ("xxabcxx",).Seq ("a", 0, Match.new(:orig("a1b2"), :from(1), :pos(2)), "b", 0, Match.new(:orig("a1b2"), :from(3), :pos(4))).Seq ("Str", "Match", "Str").Seq ("Str", "Pair", "Str").Seq ("Str", "Int", "Str").Seq 1 ("a", "b", "c").Seq ("a", "b", "c").Seq Str 3 ("", "abc").Seq ("abc", "").Seq ("a", "bc").Seq ().Seq ("a", "b", "c").Seq 3 2 False
```
rakupp 4.0.1-84: differs — a `:v` Match's `.orig` is the delimiter text, not the whole string; `:v, :k` together are not refused; `:end` is ignored.

### GM-41  trans, contains, index and the rest with a Regex                  D:partial R:partial V:bug
`.trans(/re/ => $s)` replaces every match with `$s` (any length, "" to
delete); with a Callable value the callable is called per match with `$/`
set (and the caller's `$/` is left on the last match); a List of Regex keys
pairs with a List of values; `:d` has no effect on regex keys; a non-Str,
non-Regex key (`[1,2]`, the `Regex` type object) is silently ignored, and
a Cool value is stringified. `.contains(/re/)` is a Bool; `.contains(/re/,
$pos)` starts at `$pos`, is False when `$pos` is not below the length even
for `^`/`$`, and a negative `$pos` is a Failure `X::OutOfRange`. `.index`,
`.rindex`, `.indices`, `.starts-with`, `.ends-with` and `.contains(Regex)`
(the type object) with a Regex are `X::Multi::NoMatch`. `.substr(/re/, 1)`
and `.substr(/re/)` die `X::Method::NotFound` (a Callable candidate calls
the regex with an Int) although a candidate meant to say "use subst"
exists — the bug: refuse a Regex to `substr` with a clear `X::AdHoc`. A
Match, being a Cool, answers `.index("b")` on its text.
```
say "a1b2".trans(/\d/ => "#"), " ", "a1b2".trans(/\d/ => "##"), " ", "a1b2".trans(/\d/ => { $/.Str * 2 }), " ", "a1b2".trans(/\d/ => { "<" ~ $/ ~ ">" }), " ", "a1b2".trans([/\d/, /a/] => ["#", "A"]), " ", "a1b2".trans((/\d/ => "#", "a" => "A")), " ", "a1b2".trans(/(\d)/ => { $0 + 1 }), " ", "a1b2".trans(/\d/ => ""), " ", "abc".trans(/x/ => "y"), " ", "a1b2".trans(/\d+/ => "#"), " ", "a11b2".trans(/\d/ => "#", :d), " ", (try "abc".trans([1,2] => "x")) // $!.^name, " ", (try "abc".trans((Regex) => "x")) // $!.^name, " ", "abc".contains(/b/), " ", "abc".contains(/x/), " ", "abc".contains(/b/, 1), " ", "abc".contains(/b/, 2), " ", "abc".contains(/b/, 3), " ", "abc".contains(/b/, 99), " ", do { my $f = "abc".contains(/b/, -1); $f.so; $f.^name ~ ":" ~ $f.exception.^name }, " ", "abc".contains(/^/, 3), " ", "abc".contains(/$/, 3), " ", "abc".contains(/./, 3), " ", "abc".contains(/b/, 0), " ", ("abc" ~~ /b/).index("b"), " ", "abc".contains(rx/b/), " ", "abc".contains(/B/), " ", "abc".contains(/:i B/), " ", ("abc".contains(/b/)).^name, " ", "a1b2".trans(/\d/ => "#", "a" => "A"), " ", "aaa".trans(/a/ => "bc"), " ", "a1b2".trans(/\d/ => 9), " ", "a1b2".trans(/\d/ => ["x", "y"]), " ", do { "q" ~~ /q/; "abc".trans(/b/ => { $/.Str }); $/.Str }, " ", (try "abc".index(/b/)) // $!.^name, " ", (try "abc".rindex(/b/)) // $!.^name, " ", (try "abc".indices(/b/)) // $!.^name, " ", (try "abc".starts-with(/a/)) // $!.^name, " ", (try "abc".ends-with(/c/)) // $!.^name, " ", (try "abc".substr(/b/, 1)) // $!.^name ~ ":" ~ $!.message.substr(0, 24), " ", (try "abc".substr(/b/)) // $!.^name, " ", (try "abc".index(/b/, 0)) // $!.^name, " ", (try "abc".contains(Regex)) // $!.^name
# rakudo 2026.08: a#b# a##b## a2b4 a<1>b<2> A#b# A#b# a2b3 ab abc a#b# a##b# abc abc True False True False False False Failure:X::OutOfRange False False False True 0 True False True Bool A#b# bcbcbc a9b9 axbx b X::Multi::NoMatch X::Multi::NoMatch X::Multi::NoMatch X::Multi::NoMatch X::Multi::NoMatch X::Method::NotFound:No such method '!cursor_ X::Method::NotFound X::Multi::NoMatch X::Multi::NoMatch
```
rakupp 4.0.1-84: differs — a Callable value in `trans` is inserted as text, a List of Regex keys is not applied, `.contains(/^/, 3)` is True, `.index`/`.rindex`/`.indices`/`.starts-with`/`.ends-with`/`.substr` accept a Regex (1, 1, `(1)`, True, True, "a"), and `.contains(Regex)` is `X::TypeCheck::Binding::Parameter`.

## Counts

| | items |
|---|---|
| total | 41 |
| not fully stated by docs (D:yes) nor asserted by Roast (R:yes) | 23 |
| Rakudo bugs (do not imitate) | 5 — GM-11 `.actions` answers `NQPMu` where `Mu` is promised, GM-27 a type-object topic gives Nil with a warning instead of False, GM-33 `:nth(n), :x(1)` answers `()` for an existing match, GM-34 `:x(Nil)` is an arity error, GM-41 `substr` with a Regex dies in a shadowed candidate |
| quirks (recorded, step two decides) | 6 — GM-02 the negative `to`/`pos` of a failed subparse, GM-07 the returned Match's attributes are undefined, GM-19 `.raku` of a `<( )>` match does not round-trip, GM-21 an absent capture prints as `Mu` in `.list`, GM-24 captures inside lookarounds are discarded, GM-30 `<%h>` matches nothing |
| rakupp 4.0.1-84 differs | 39 |
| rakupp 4.0.1-84 matches | 2 — GM-21 (all but the `Mu` quirk field), GM-32 |

Recurring rakupp gaps, for step two. The result of a parse is a plain
`Match`, never an instance of the grammar, and a Match is a value type
(`ValueObjAt`, `===` between clones and between separate matches); a
failed `subparse` is Nil instead of a false Match; `:pos` and `:args` to
`parse` are ignored; a missing rule, a missing `TOP`, a missing file and a
type-object target answer Nil where Rakudo throws; `.orig` is always the
Str. A grammar's rules are not callable methods (`G.new(:orig).TOP`,
`^methods`), `self` in a rule's code block is a compile error, `<.I::x>`
and an explicit `<ws>` capture fail. The Match surface lacks
`.replace-with`, `.caps`, `.chunks` and `.actions`; `.hash` is a Hash,
`Cursor` is a separate type, `Match.new` ignores `:made`/`:ast` and
accepts `pos < from` as True; `$/<missing>:exists` and `$0` on a Nil `$/`
answer Nil/Any; quantified named captures are Lists and `$<x>=(a)+` one
Match. Actions: `<b=.a>` fires nothing, actions of failed or abandoned
branches are not fired, `make` never throws `X::Make::MatchRequired` and
a top-level `make` is lost. The Regex value stringifies to its source,
`.raku` moves adverbs inside, `.name` is "Regex", it is callable with a
Str and scans instead of anchoring, `Regex.new` is accepted, and
`.signature`/`.arity`/`.count` are missing; a List or Hash topic is
stringified before matching; `<%h>` and a bare `%h` are accepted. Under
`:r`, `a+!` and `[ … || … ]` and a `regex` subrule get backtracking wrong
in three directions. In `.match`: `:ov`/`:ex` return one Match, `:as(Str)`
a Match, the long adverb names count 0, `:nth(*)`/`*-1`/a Callable die
"out of order" and the die escapes `try`, a `:nth` past the end is `()`,
`:g(False)` is refused, and every combination in GM-35 is off; in the
`m//` form `:x`, `:c`, `:p` and `:st` are ignored. `.subst(:ii)` implies
`:i`, `$¢` in a replacement is Any, `:ov`/`:ex` throw the wrong type, a
failed `s///` is not `Bool::False`, `s///` on a literal succeeds, method
calls in a replacement are not interpolated; `.comb(:match)` and split's
`:v` build Lists whose Matches have the piece as `.orig`; `:end` and the
`:v`/`:k` refusal are missing; `trans` with a Callable inserts its text,
and `.index`/`.rindex`/`.indices`/`.starts-with`/`.ends-with`/`.substr`
accept a Regex that Rakudo refuses.

## Method (how this sheet was produced)

As for [Supply.md](Supply.md): the four source files were read in full,
the Str candidates by name, each behaviour was turned into a one-line
probe, and every probe ran through both engines in a fresh sandbox under
`alarm 10`; eleven rounds. Traps met, for the next sheet: `)>` is the
capture-end marker, so `<?before (\d)>` does not compile — write `(\d) >`;
a one-line grammar or class needs `;` between its rule and method
declarations ("Strange text after block"); `%h` bare in a regex is a
compile-time reserved-syntax error and a space inside `rx/a b/` a
sigspace worry that lands in the output, both killing the line; `say a,
b, c` evaluates every argument before printing, so a field that dies on
rakupp erases the whole line — the risky field went into a second `say`
(never `print`: `print Nil` warns where `say Nil` prints Nil); `do { …;
$*x }` and `do { …; $_ }` return the CONTAINER, so every such field showed
the last assignment — return `~$*x`; a `class { has @.log }` passed as a
type object dies when a method touches the attribute (pass `.new`); an
action method must declare its positional; `Regex => "x"` is a named
argument; `:args("c")` is `X::Cannot::Capture`; named captures come out
of `.keys`/`.pairs` in hash order, so sort them; a `do { $s.subst-mutate(…) }`
whose value is Nil reads Any once assigned; rakupp's `:nth(*)` die
escapes `try`, so those forms went on a second line; rakupp's exception
for `:ov` has no `.name`, so attributes are read with `.?name`. D flags
from `doc/Type/{Grammar,Match,Regex,Str}.rakudoc` and
`doc/Language/{grammars,regexes,grammar_tutorial}.rakudoc`; R flags from
`S05-grammar/*.t`, `S05-match/*.t`, `S05-capture/*.t`, `S05-modifier/*.t`,
`S05-interpolation/*.t`, `S05-substitution/*.t`, `S05-nonstrings/basic.t`
and `S32-str/{comb,split,subst,match,trans}.t`.
