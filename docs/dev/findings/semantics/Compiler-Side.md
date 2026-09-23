# The compiler side: precedence, quote adverbs, sink context — semantics sheet

Provenance: Rakudo tag `2026.08`, files `src/Perl6/Grammar.nqp` (5,967
lines: the precedence table, `EXPR`, the quote tokens and the Q
sub-grammar), `src/Perl6/Actions.nqp` (12,186: the wanted/unwanted sink
analysis and the heredoc trimming), `src/Perl6/Optimizer.nqp` (4,745: the
"Useless use" sites), `src/core.c/Failure.rakumod` (196), `Proc.rakumod`
(255) and `Seq.rakumod` (224) for their `sink` methods, read on
2026-09-23. `src/Raku/Grammar.nqp` (6,766) and
`src/Raku/ast/operator-properties.rakumod` (872) were read to compare the
RakuAST frontend. The tag runs the legacy frontend (`Perl6::Grammar`) by
default; `src/main.nqp` selects the RakuAST one only when `RAKUDO_RAKUAST`
is set in the environment, which it was not, and `$*RAKU.compiler.version`
answered `v2026.08`. Observable differences between the two frontends on
this sheet's ground: the RakuAST table puts `temp`/`let` on a named-unary
level of their own between junctive-or and structural (the legacy one puts
them with `++`/`--`), and it knows a `:o`/`:format` quote adverb that the
legacy frontend rejects (CP-23); the associativity and level of every other
operator measured here are the same in both. Oracle: Homebrew Rakudo
v2026.08 on macOS. Compared against Raku++ 4.0.1-84-ga4291988
(build-arm64, 2026-09-20). Format and legend: [README.md](README.md).

Where this sits against the declared spec: of the 43 Roast files behind
these three areas (16 in `S03-operators`: `precedence.t`, `assign.t`,
`short-circuit.t`, `ternary.t`, `andthen.t`, `notandthen.t`, `orelse.t`,
`so.t`, `not.t`, `minmax.t`, `flip-flop.t`, `repeat.t`,
`context-forcers.t`, `inplace.t`, `comparison.t`, `autoincrement.t`; 13
in `S02-literals`: `quoting.t`, `heredocs.t`, `quoting-unicode.t`,
`misc-interpolation.t`, `string-interpolation.t`, `adverbs.t`,
`listquote.t`, `listquote-whitespace.t`, `array-interpolation.t`,
`hash-interpolation.t`, `sub-calls.t`, `char-by-name.t`,
`char-by-number.t`; 14 in `S04`: `statements/sink.t`, `do.t`, `if.t`,
`for.t`, `while.t`, `loop.t`, `given.t`, `try.t`, `return.t`, `lazy.t`,
`unless.t`, `exceptions/fail.t`, `blocks-and-statements/temp.t` and
`let.t`), Rakudo passes all 43 on this machine and Raku++ passes 8
(`so.t`, `not.t`, `autoincrement.t`, `listquote-whitespace.t`,
`array-interpolation.t`, `loop.t`, `lazy.t`, `unless.t`). The rules below
are the ones those files and real programs lean on: which of two
neighbouring operators takes the operand, what a same-level mix is allowed
to mean, what a quote construct's adverbs and delimiters do to its text,
and what happens to a value nobody uses. 8 of the 34 items are neither
fully documented nor fully asserted by Roast. One Rakudo behaviour is
recorded as a bug and eight as quirks; step two should not imitate the
bug.

Every probe is one line, run with `alarm 25` and standard input closed, in
a fresh sandbox directory per engine. A newline in an output is shown as
`|`. Compile-time questions go through `EVAL` inside `try` and print the
exception type; compile-time warnings are read from a child process's
standard error (CP-28, CP-29), so that a `WARNINGS` block does not
decorate the line.

## A. The precedence table

### CP-01  The levels, top to bottom, and each level's associativity     D:partial R:yes V:spec
From tightest to loosest: method postfix (`.meth .[] .{} .<> ()`), then
autoincrement `++ --`, exponentiation `**` (right), symbolic unary
`! + - ~ ? | ^ +^ ~^ ?^ //` and the spaced dotty infix `.`, multiplicative
`* / % %% div mod gcd lcm +& +< +> ~& ~< ~> ?&` (left), additive
`+ - +| +^ ~| ~^ ?| ?^` (left), replication `x xx` (left), concatenation
`~` (left), junctive and `&` (list), junctive or `| ^` (list), structural
`.. ..^ ^.. ^..^ <=> leg cmp unicmp coll but does` (non-associative),
chaining `== != < <= > >= eq ne lt le gt ge === eqv =:= ~~ !~~ before
after (elem) …` (chain), tight and `&&` (left), tight or `|| //` (left)
with `^^ min max` (list) on the same level, conditional `?? !!` and the
`ff` family (right), item assignment `= => += x= …` (right), loose unary
`so not`, comma `, :` (list), list infix `Z X … minmax` (list), list
prefix (list assignment `=`, `[+]`, `any`), loose and `and` (left) with
`andthen notandthen` (list) on the same level, loose or `or` (left) with
`xor orelse` (list), and the sequencer `==> <==`. So `1 ~ 2 * 3` is `16`,
`2 x 2 + 3` is `22222`, `2 x 2 ~ 3` is `223`, `1 + 1 & 2 + 2` is
`all(2, 4)` and `"a" ~ 1 | 2` is `any("a1", 2)`. The docs' table lists
concatenation and tight-and as `list` and the whole tight-or, loose-and and
loose-or rows as `list`; the measured mixes (CP-07, CP-09) show the
left/list split within a level, which is what a same-level mix parses or
refuses by.
```
say (1 ~ 2 * 3), " ", (2 x 2 + 3), " ", (2 x 2 ~ 3), " ", (1 x 2 xx 3).raku, " ", (1 + 2 ~ 3), " ", ("a" ~ 1 + 2), " ", (2 ** 3 x 2), " ", (4 xx 2 ~ "a"), " ", (1, 2 xx 2).raku, " ", ("ab" x 2 xx 2).raku, " ", (1 + 2 x 2), " ", (2 x 2 + 3).^name, " ", (1 xx 2 + 1).raku, " ", ("a" ~ "b" x 2), " ", ("a" x 2 ~ "b" x 2), " ", (1 ~ 2 + 3 ~ 4), " ", (2 + 3 ~ 4 + 5), " ", (10 - 2 - 3), " ", (2 ** 2 x 2), " ", (1 + 1 & 2 + 2).raku, " ", ("a" ~ 1 | 2).raku, " ", (1 + 2 == 3), " ", (1 ~ 2 == 12), " ", (2 x 3 == 222), " ", (1 xx 2 == 2), " ", (8 / 2 / 2), " ", (2 ** 3 ** 0), " ", (7 mod 3 * 2), " ", (1 +< 2 + 1), " ", (1 +| 2 +& 3), " ", (5 +& 3 +| 8)
# rakudo 2026.08: 16 22222 223 ("11", "11", "11").Seq 33 a3 88 4 4a (1, (2, 2).Seq) ("abab", "abab").Seq 33 Str (1, 1, 1).Seq abb aabb 154 59 5 44 all(2, 4) any("a1", 2) True True True True 2 2 2 5 3 9
```
rakupp 4.0.1-84: differs in two fields — `1 + 1 & 2 + 2` gives `all(4, 4)` and `"a" ~ 1 | 2` gives `any("a1", "a2")`: the junctive operators bind tighter than `+` and `~` there.

### CP-02  Exponentiation against unary minus and the dotty infix       D:yes R:yes V:spec
`**` binds tighter than prefix `-`, so `-2**2` is `-4` and `-2 ** -2` is
`-0.25`; it is right-associative (`2**3**2` is 512, `2 ** 0.5 ** 2` is
`2 ** 0.25`). A method postfix binds tighter than everything, including
prefix `-` (`-1.abs` is `-1`, `-$x.abs` with `$x = -5` is `-5`) and `**`
(`2 ** 3.Str` is an Int 8). The spaced form `. abs` is the dotty infix,
one level looser than `**`, applied left to right with the unary minus:
`-2 ** 2 . abs` is 4 and `-1 * -1 . abs` is -1. `!0 * 2` is 2 and
`~2**4` is a Str.
```
say (-2**2), " ", ((-2)**2), " ", (2**3**2), " ", ((2**3)**2), " ", (1-2-3), " ", (2**-1), " ", (-1.abs), " ", do { my $x = -5; -$x.abs }, " ", (~2**4).^name, " ", (!0 * 2), " ", (2 ** 2 ** 3), " ", (try EVAL '-2 ** 2 . abs') // $!.^name, " ", (try EVAL '-1 * -1 . abs') // $!.^name, " ", (10 / 2 * 5), " ", (2 * 3 % 4), " ", (7 % 3 * 2), " ", (2 - 2 div 2), " ", (2 ** 2 * 3), " ", (2 * 2 ** 3), " ", (-2 ** -2), " ", (2 ** 0.5 ** 2), " ", (-3 ** 2 ** 0), " ", (1 - -1), " ", (+"3" + 1), " ", (-"3" + 1), " ", (2 ** 3.Str).^name, " ", (- - 1)
# rakudo 2026.08: -4 4 512 64 -4 0.5 -1 -5 Str 2 256 4 -1 25 2 2 1 12 16 -0.25 1.189207115002721 -3 2 4 -2 Int 1
```
rakupp 4.0.1-84: differs in one field — `-2 ** 2 . abs` gives -4 (the spaced `.` binds like a method postfix).

### CP-03  Autoincrement: non-associative, container-bound, ordered      D:partial R:yes V:spec
`++$l++` is a compile-time `X::Syntax::NonAssociative`; `$i++++`,
`(++$i)++`, `(-$x)++` and `++$i.abs` are run-time `X::Multi::NoMatch`
(the postfix needs a container; parentheses and a method call give a
value). `++` binds tighter than `**` and prefix `-` (`$i++ ** 2` with
`$i = 1` is 1, `++$i ** 2` is 4, `-$x++` is -1). Operands are bound as
containers and read when the operator runs, so `$i + $i++` with `$i = 1`
is 3 (the left `$i` is read after the postfix ran) while `$i++ + $i` is
also 3, `$i++ + ++$i` is 4 and `++$i + ++$i` is 5 (the left prefix result
is read before the right one runs). `$n++` on an undefined scalar gives 1,
`$n--` gives -1; Str increments (`"az"` to `"ba"`); hash and array
elements autovivify.
```
say do { my $i = 1; my $r = $i++ + ++$i; "$r $i" }, " ", do { my $i = 5; my $r = ++$i * 2; "$r $i" }, " ", (try EVAL 'my $l = 42; ++$l++') // $!.^name, " ", do { my $i = 1; $i++ ** 2 }, " ", do { my $x = 1; my $r = -$x++; "$r $x" }, " ", do { my $s = "az"; $s++; $s }, " ", do { my $n; $n++; $n }, " ", do { my $n; $n--; $n }, " ", do { my $i = 1; my $r = ++$i + $i++; "$r $i" }, " ", (try EVAL 'my $i = 1; (++$i)++; $i') // $!.^name, " ", (try EVAL 'my $i = 1; $i++++') // $!.^name, " ", do { my $i = 3; my $r = $i-- - --$i; "$r $i" }, " ", do { my @a = 1; @a[0]++; @a.raku }, " ", do { my %h; %h<k>++; %h.raku }, " ", do { my $i = 1; $i++.raku ~ $i }, " ", (try EVAL 'my $x = 2; (-$x)++') // $!.^name, " ", (try EVAL 'my $i = 1; ++$i.abs') // $!.^name, " ", do { my $i = 1; my $r = ++$i ** 2; "$r $i" }, " ", do { my $i = 1; my $r = $i++ ** 2; "$r $i" }, " ", do { my $i = 2; my $r = -$i ** 2; "$r $i" }, " ", do { my $i = 2; my $r = 2 ** $i++; "$r $i" }, " ", do { my $i = 1; my $r = $i++ + $i++; "$r $i" }, " ", do { my $i = 1; my $r = ++$i + ++$i; "$r $i" }, " ", do { my $i = 1; my $r = $i + $i++; "$r $i" }, " ", do { my $i = 1; my $r = $i++ + $i; "$r $i" }
# rakudo 2026.08: 4 3 12 6 X::Syntax::NonAssociative 1 -1 2 ba 1 -1 4 3 X::Multi::NoMatch X::Multi::NoMatch 2 1 [2] {:k(1)} 12 X::Multi::NoMatch X::Multi::NoMatch 4 2 1 2 -4 2 4 3 3 3 5 3 3 2 3 2
```
rakupp 4.0.1-84: differs in seven fields — `++$l++` (43), `$i++++` (1) and `(++$i)++` are accepted, `(-$x)++` and `++$i.abs` throw `X::Assignment::RO`, and `$i + $i++` is 2 (the left operand is read before the postfix runs).

### CP-04  Symbolic unary and loose unary                                D:yes R:yes V:spec
`! ? ~ + - ^ | +^ ?^` bind tighter than every infix but `**` and the
method postfix: `!1 ~~ 2` is `False ~~ 2`, `!1 == 1` is `False == 1`,
`~1 + 2` is `"1" + 2` (an Int 3), `^3 + 1` is `(^3) + 1` (the Range
`1..^4`), `?2*2` is 2, `!"" + 1` is 2, `-1 ** 2` is -1 and `!1 ** 2` is
False. `so` and `not` are loose unary: looser than item assignment and
every comparison but tighter than the comma, so `so $x = 42` assigns
first, `not 1 ~~ 2` is `not (1 ~~ 2)`, `not 0 + 1` is `not 1`,
`(not 1, 42)[1]` is 42 and `(so 0, 1)` is `(False, 1)`. `not(0) + 1`
with parentheses is a call, `(not 0) + 1`. `|(1,2)` is a Slip.
```
say (!1 ~~ 2), " ", (not 1 ~~ 2), " ", do { my $x; (so $x = 42) ~ " " ~ $x }, " ", (not 1 and 0).raku, " ", do { my $x = 1; (!$x.defined) }, " ", (?2*2), " ", (^3 + 1).raku, " ", (~1 + 2), " ", (~1 + 2).^name, " ", (not(0) + 1), " ", (not 0 + 1).raku, " ", ((not 1, 42)[1]), " ", (!"" + 1), " ", (?!0), " ", (!!1), " ", (!1 == 1), " ", (not 1 == 1), " ", (so 1 == 2), " ", (+^1), " ", (?^True), " ", (|(1,2)).raku, " ", [1, |(2,3)].raku, " ", (so 0, 1).raku, " ", (not 0, 1).raku, " ", (-1 ** 2), " ", (!1 ** 2), " ", (~1 ** 2), " ", (?0 ** 2).raku, " ", (- 2 ** 2), " ", (so 1 ~~ 2), " ", ((so 1) ~~ 2), " ", (!1 ~~ 2).^name
# rakudo 2026.08: False True True 42 Bool::False False 2 1..^4 3 Int 2 Bool::False 42 2 True True False False False -2 False slip(1, 2) [1, 2, 3] (Bool::False, 1) (Bool::True, 1) -1 False 1 Bool::False -4 False False Bool
```
rakupp 4.0.1-84: matches.

### CP-05  Junctive and structural: same-op lists, non-associative rows D:partial R:yes V:quirk
`&`, `|` and `^` are list-associative but only among the same operator:
`1 & 2 & 3` is one `all`, `1 | 2 ^ 3`, `1 & 2 (&) 3` and `1 | 2 (|) 3`
are compile-time `X::Syntax::NonListAssociative`. `&` is tighter than
`|`/`^` (`1 & 2 ^ 3` is `one(all(1, 2), 3)`). The structural level is
non-associative: `1 .. 2 .. 3`, `1 <=> 2 <=> 3`, `1 <=> 2 leg 3`,
`1 leg 2 cmp 3`, `1 .. 2 cmp 3` and `1 but 2 does 3` are
`X::Syntax::NonAssociative`. Structural binds tighter than chaining and
looser than junctives and arithmetic: `1 == 3 <=> 2` is `1 == More`
(True), `1 .. 2 + 3` is `1..5`, `1 | 3 <=> 2` is `any(Less, More)`,
`0 < 2 <=> 1 < 2` is True. The quirk: a chain of two `~~` is not
reliable — `1 ~~ 1 .. 3 ~~ Bool` is True while `1 ~~ Int ~~ Bool` is
False (the second form type-checks its middle operand against `Bool`, the
first does not); do not chain `~~`.
```
say ((1 & 2 | 3) == 3).raku, " ", (try EVAL '1 | 2 ^ 3') // $!.^name, " ", (try EVAL '1 .. 2 .. 3') // $!.^name, " ", (try EVAL '1 <=> 2 <=> 3') // $!.^name, " ", (try EVAL '1 <=> 2 leg 3') // $!.^name, " ", (try EVAL '1 but 2 does 3') // $!.^name, " ", (try EVAL '1 .. 2 cmp 3') // $!.^name, " ", (1 == 3 <=> 2), " ", (1..3, 4).raku, " ", (1 | 3 <=> 2).raku, " ", (0 < 2 <=> 1 < 2), " ", (1 <=> 2 == Less), " ", (2 + 2 | 4 - 1).raku, " ", (2 ~ 2 | 4 ~ 1).raku, " ", (1 & 2 ^ 3).raku, " ", (1 & 2 & 3).raku, " ", (1 | 2 | 3).raku, " ", (1 ^ 2 ^ 3).raku, " ", (try EVAL '1 & 2 (&) 3') // $!.^name, " ", (1 .. 2 + 3).raku, " ", (1 + 1 .. 5).raku, " ", ((1..3) cmp (1..4)).raku, " ", (1 .. 5 == 5), " ", (try EVAL '1 leg 2 cmp 3') // $!.^name, " ", (2 <=> 1 ~~ More), " ", ("a" cmp "b" eq "Less"), " ", (1 ~~ 1..3), " ", (1 ~~ 1 .. 3 ~~ Bool), " ", (1 | 2 == 2).raku, " ", (1 | 2 ~~ 2), " ", ((1 & 2) < 3), " ", (try EVAL '1 | 2 (|) 3') // $!.^name
# rakudo 2026.08: any(all(Bool::False, Bool::False), Bool::True) X::Syntax::NonListAssociative X::Syntax::NonAssociative X::Syntax::NonAssociative X::Syntax::NonAssociative X::Syntax::NonAssociative X::Syntax::NonAssociative True (1..3, 4) any(Order::Less, Order::More) True True any(4, 3) any("22", "41") one(all(1, 2), 3) all(1, 2, 3) any(1, 2, 3) one(1, 2, 3) X::Syntax::NonListAssociative 1..5 2..5 Order::Less True X::Syntax::NonAssociative True True True True any(Bool::False, Bool::True) True all(True, True) X::Syntax::NonListAssociative
```
rakupp 4.0.1-84: differs in eleven of 32 fields — `1 | 2 ^ 3`, `1 & 2 (&) 3` and `1 | 2 (|) 3` are accepted (`one(any(1, 2), 3)`, `all(Set(), Set())`, `any(Set(1 3), Set(2 3))`), the non-associative row parses left to right (`1 .. 2 .. 3` is `0..3`, `1 <=> 2 leg 3` is `More`, `1 leg 2 cmp 3` and `1 .. 2 cmp 3` are `Less`), `1 but 2 does 3` is a run-time `X::AdHoc`, `1 == 3 <=> 2` is `Less`, and the junctives bind tighter than `+` and `~` (`2 + 2 | 4 - 1` is `any(3, 3)`, `2 ~ 2 | 4 ~ 1` is `any("221", "241")`).

### CP-06  Chaining, the `!` and `R` metaoperators                       D:yes R:yes V:spec
`a op b op c` is `(a op b) && (b op c)` with the middle operand evaluated
once: `1 < 2 < 3`, `1 < 3 > 2`, `1 == 1 == 1`, `"a" lt "b" lt "c"`,
`5 > 3 > 1 > 0` and `1 == 1 != 2` are True; `1 < 2 > 3` is False; the
chain stops at the first False (`1 > g() < 3` calls `g` once and gives
False). The traps: `2 == 2 == True` is `2 == True`, False; `1 < 2 == True`
is `2 == True`, False; `1 == 1 eq "True"` is `1 eq "True"`, False;
`1 == 1.0 eq "1"` is True. `&&` is looser than `==` (`1 == 1 && 2` is 2)
while `&` is tighter (`1 == 1 & 2` is `all(True, False)`). `!` negates a
chaining operator and keeps the chain (`1 !== 1 == 1` is False,
`1 !> 2 !< 3` is True, `1 !eqv 2`, `1 !~~ Str`); on a non-iffy operator it
is a compile-time `X::Syntax::CannotMeta` (`1 !+ 2`, `1 !<=> 2`), and
`1 !!= 2` is `X::Syntax::Confused`. `R` swaps the operands and keeps the
level (`1 R- 2 * 3` is 5, `2 Rx "a"` is `aa`, `1 R.. 2` is `2..1`) and
reverses the associativity, so `1 R< 2 R< 3` is True. `=:=` chains like the
rest.
```
say (1 == 1 == 1), " ", (1 < 2 < 3), " ", (1 < 3 > 2), " ", (1 < 2 > 3), " ", (2 == 2 == True), " ", (0 == 1 ~~ Bool), " ", ((0 == 1) ~~ Bool), " ", (1 !== 2), " ", (1 !eqv 2), " ", (1 !~~ Str), " ", (1 !== 1 == 1), " ", do { my $n = 0; sub f { $n++; 2 }; (1 < f() < 3) ~ " " ~ $n }, " ", do { my $n = 0; sub g { $n++; 5 }; (1 > g() < 3) ~ " " ~ $n }, " ", ("a" lt "b" lt "c"), " ", (1 === 1 === 1), " ", (1 == 1.0 eq "1"), " ", (try EVAL '1 !+ 2') // $!.^name, " ", (try EVAL '1 !!= 2') // $!.^name, " ", (1 R< 2), " ", (1 R- 3), " ", (1 R- 2 * 3), " ", (2 Rx "a"), " ", (1 < 2 == True), " ", (1 < 2 == 1), " ", (5 > 3 > 1 > 0), " ", (1 == 1 != 2), " ", (1 !< 2), " ", (1 !> 2 !< 3), " ", (try EVAL '1 !<=> 2') // $!.^name, " ", (1 =:= 1), " ", do { my $a = 1; my $b := $a; ($a =:= $b =:= $a) }, " ", ("a" before "b" before "c"), " ", (1 == 1 eq "True"), " ", (try EVAL '1 !== 2 !== 3') // $!.^name, " ", (1 == 1 && 2), " ", (1 == 1 & 2).raku, " ", (1 R< 2 R< 3), " ", (try EVAL '1 R.. 2') // $!.^name, " ", (try (1 R.. 2).raku) // $!.^name, " ", (2 R** 3), " ", (1 ~~ Int ~~ Bool), " ", ("1" == 1 eq "1")
# rakudo 2026.08: True True True False False False True True True True False True 1 False 1 True True True X::Syntax::CannotMeta X::Syntax::Confused False 2 5 aa False False True True False False X::Syntax::CannotMeta True True True False True 2 all(Bool::True, Bool::False) True 2..1 2..1 9 False True
```
rakupp 4.0.1-84: differs in eight fields — `0 == 1 ~~ Bool` and `1 ~~ Int ~~ Bool` are True (each link is `(previous result) ~~ right`), `1 !+ 2` and `1 !<=> 2` are `X::Syntax::Confused` instead of `X::Syntax::CannotMeta`, `$a =:= $b =:= $a` on bound aliases is False, `1 R< 2 R< 3` is False, and `R..` is `X::NYI`.

### CP-07  Tight and, tight or, defined-or, xor, min/max                 D:partial R:partial V:spec
`&&` is tighter than `||`/`//` (`0 || 1 && 0` is 0, `1 // 2 || 3 && 4` is
1, `0 && 2 || 3` is 3). On the tight-or level `||` and `//` are
left-associative and `^^`, `min`, `max` are list-associative among
themselves only; the rule for a same-level mix is: after a left-associative
operator any operator of the level may follow (`1 || 2 // 3` is 1,
`Nil // 2 || 3` is 2, `1 ^^ 2 || 3` is 3), but a list-associative operator
may only be followed by itself (`1 min 2 max 3`, `0 || 2 ^^ 3`,
`0 // 2 ^^ 3` and `1 || 2 min 3` are `X::Syntax::NonListAssociative`).
`min`/`max` are looser than every arithmetic and string operator
(`3 min 2 + 5` is 3, `1 max 2 ** 2` is 4, `"a" ~ 1 min 2` compares `"a1"`
with 2 and gives 2, `2 min 1 == 1` is `2 min True`, which is True). `^^`
of one true operand is that operand, of two or more true ones Nil, of none
the last operand. `&&`, `||` and `//` return the deciding operand and do
not evaluate the rest; `//` on a Failure marks it handled and takes the
right side.
```
say (0 || 1 && 0), " ", ((0 || 1) && 0), " ", (1 // 2 || 3 && 4), " ", (try EVAL '1 || 2 // 3') // $!.^name, " ", (try EVAL 'Nil // 2 || 3') // $!.^name, " ", (try EVAL '1 min 2 max 3') // $!.^name, " ", (try EVAL '1 max 2 max 3') // $!.^name, " ", (try EVAL '1 ^^ 2 || 3') // $!.^name, " ", (try EVAL '0 || 2 ^^ 3') // $!.^name, " ", (try EVAL '0 // 2 ^^ 3') // $!.^name, " ", (try EVAL '1 || 2 min 3') // $!.^name, " ", (try EVAL '0 && 2 || 3') // $!.^name, " ", (1 && 0 ?? 2 !! 3), " ", (0 || 1 ?? 2 !! 3), " ", (3 min 2 + 5), " ", (1 max 2 ** 2), " ", (0 ^^ 42), " ", (1 ^^ 42).raku, " ", (0 ^^ 0).raku, " ", (0 ^^ False ^^ ''), " ", (1 ^^ 2 ^^ 3).raku, " ", (Any // 0 || 5), " ", (1 && 2 && 3), " ", (0 && 2).raku, " ", (Nil || 0).raku, " ", (Nil // Nil).raku, " ", (Any || Nil).raku, " ", ("" || 0).raku, " ", (2 min 1 == 1).raku, " ", ("a" ~ 1 min 2), " ", (1 min 2 min 3), " ", (Failure.new("x") // 5), " ", do { my $c = 0; (0 && $c++); (1 || $c++); (5 // $c++); $c }, " ", (0 ^^ 0 ^^ 3), " ", (1 ^^ 0 ^^ 3).raku, " ", (0 xor 1 xor 0), " ", (1 && 2 || 3), " ", (0 || 0 && 3).raku, " ", (Any && 1).raku
# rakudo 2026.08: 0 0 1 1 2 X::Syntax::NonListAssociative 3 3 X::Syntax::NonListAssociative X::Syntax::NonListAssociative X::Syntax::NonListAssociative 3 3 2 3 4 42 Nil 0  Nil 5 3 0 0 Nil Nil 0 Bool::True 2 1 5 0 3 Nil 1 2 0 Any
```
rakupp 4.0.1-84: differs in six fields — `1 min 2 max 3` (3), `0 || 2 ^^ 3` (Nil), `0 // 2 ^^ 3` (3) and `1 || 2 min 3` (1) are accepted, and `min` binds tighter than `+` and `~` (`3 min 2 + 5` is 7, `"a" ~ 1 min 2` is `a1`).

### CP-08  The ternary: right-associative, loosest of the tight ops        D:yes R:yes V:spec
`?? !!` nests from the right without parentheses
(`1 ?? 2 !! 3 ?? 4 !! 5` is 2, `0 ?? 2 !! 3 ?? 4 !! 5` is 4,
`1 ?? 0 ?? 3 !! 4 !! 5` is 4, `1 ?? 1 ?? 2 !! 3 !! 4` is 2); only the
chosen branch runs. Both branches and the condition take a whole
tight-or-level expression: `1 ?? 2 !! 3 + 10` is 2 and `0 ?? … !! 3 + 10`
is 13, `1 + 1 ?? 2 !! 3` is 2, `0 ?? "a" !! "b" ~ "c"` is `bc`,
`1 ?? 2 !! 3 x 2` is 2 and `0 ?? 2 !! 3 x 2` is `33`, `1 ?? 2 !! 3 min 1`
is 2, `Nil // 1 ?? 2 !! 3` is 2 and `so 1 ?? 0 !! 1` is False. Looser
operators bind around it: `my $a = 0 ?? "yes" !! "no"` assigns `no`,
`0 and 1 ?? 2 !! 3` is 0, `4 or 5 ?? 6 !! 7` is 4, `(1 ?? 2 !! 3, 4)` is
`(2, 4)`, `1 ?? 2 !! 3 andthen 9` is 9. An assignment inside a branch is
a compile-time `X::Syntax::ConditionalOperator::PrecedenceTooLoose`
(`$a ?? $a = 42 !! $a = 43`, `$a ?? $a += 42 !! 43`, and also
`1 ?? 2,3 !! 4,5`); `1 ?? 3 :: 2` and `1 ?? 3 : 2` are
`X::Syntax::ConditionalOperator::SecondPartInvalid`; a missing `!!` is
`X::Syntax::Confused`; `1 ?? 2 !! 3 = 4` parses and fails at run time with
`X::Assignment::RO`.
```
say (1 ?? 2 !! 3 ?? 4 !! 5), " ", (0 ?? 2 !! 3 ?? 4 !! 5), " ", (1 ?? 0 ?? 3 !! 4 !! 5), " ", (0 ?? 2 !! 0 ?? 4 !! 5), " ", (try EVAL 'my $a; $a ?? $a = 42 !! $a = 43') // $!.^name, " ", (try EVAL 'my $a; $a ?? $a += 42 !! 43') // $!.^name, " ", (try EVAL '1 ?? 2,3 !! 4,5') // $!.^name, " ", (try EVAL '1 ?? 2') // $!.^name, " ", (try EVAL '1 ?? 3 :: 2') // $!.^name, " ", (try EVAL '1 ?? 3 : 2') // $!.^name, " ", do { my $a = 0 ?? "yes" !! "no"; $a }, " ", (Nil // 1 ?? 2 !! 3), " ", (0 and 1 ?? 2 !! 3).raku, " ", (4 or 5 ?? 6 !! 7), " ", do { my @s; 1 ?? @s.push(1) !! @s.push(2); @s.raku }, " ", do { my $x; 1 ?? ($x = 2) !! ($x = 3); $x }, " ", (1 ?? 2 !! 3 + 10), " ", (0 ?? 2 !! 3 + 10), " ", (1 + 1 ?? 2 !! 3), " ", (1 ?? "a" !! "b" ~ "c"), " ", (0 ?? "a" !! "b" ~ "c"), " ", (1 ?? 2 !! 3 == 3), " ", (0 ?? 2 !! 3 == 3), " ", (so 1 ?? 0 !! 1), " ", (not 0 ?? 1 !! 2), " ", (try EVAL 'my $x = 1 ?? 2 !! 3; $x') // $!.^name, " ", (1 ?? 2 !! 3, 4).raku, " ", (0 ?? 2 !! 3 // 4), " ", (1 ?? Nil !! 3).raku, " ", (0 ?? 2 !! Nil // 9), " ", (1 ?? 2 !! 3 ?? 4 !! 5 ?? 6 !! 7), " ", (0 ?? 2 !! 0 ?? 4 !! 0 ?? 6 !! 7), " ", (1 ?? 1 ?? 2 !! 3 !! 4), " ", (1 ?? 2 !! 3 andthen 9), " ", (1 ?? 2 !! 3 x 2), " ", (0 ?? 2 !! 3 x 2), " ", (1 ?? "a" !! "b" x 2), " ", (1 ?? 2 !! 3 min 1), " ", (try EVAL '1 ?? 2 !! 3 = 4') // $!.^name
# rakudo 2026.08: 2 4 4 5 X::Syntax::ConditionalOperator::PrecedenceTooLoose X::Syntax::ConditionalOperator::PrecedenceTooLoose X::Syntax::ConditionalOperator::PrecedenceTooLoose X::Syntax::Confused X::Syntax::ConditionalOperator::SecondPartInvalid X::Syntax::ConditionalOperator::SecondPartInvalid no 2 0 4 [1] 2 2 13 2 a bc 2 True False False 2 (2, 4) 3 Nil 9 2 7 2 9 2 33 a 2 X::Assignment::RO
```
rakupp 4.0.1-84: differs in six fields — an assignment inside a branch is accepted (`43`, `85`), `1 ?? 2,3 !! 4,5` gives `((2 3) 5)`, the `::`/`:` forms and the missing `!!` are all `X::Syntax::Confused`, and `1 ?? 2 !! 3 = 4` is a silent 2.

### CP-09  andthen, notandthen, orelse against and, or, xor              D:yes R:yes V:spec
`andthen` and `orelse` test definedness, not truth, and thunk their right
side: `0 andthen 1` is 1, `1 orelse 0` is 1, `Int andthen ($t = 1)` leaves
`$t` untouched. `andthen` returns its first undefined operand, and an
undefined left operand yields `Empty`, not the operand (`Any andthen 2` is
`Empty`, `1 andthen Any` is `Any`, `Empty andthen 42` is `Empty`);
`notandthen` is the reverse (`Int notandthen 42` is 42,
`1 notandthen 42` is `Empty`). The right side sees the left value as
`$_` (`5 andthen $_ * 2 andthen $_ + 1` is 11, `"x" andthen "$_$_"` is
`xx`). `orelse` binds `$_` to the left value and marks a Failure handled
(`f() orelse $_.handled` is True). `andthen`/`notandthen` sit on the
loose-and level, tighter than `orelse`/`or`/`xor`
(`Nil andthen 'foo' orelse Nil orelse 'bar'` is `bar`,
`1 andthen 2 or 3` is 2, `Any andthen 2 orelse 3` is 3); `and`/`or` are
left-associative, `andthen`/`notandthen`/`xor`/`orelse` list-associative,
with the CP-07 mixing rule: `1 xor 2 or 3` and `1 orelse 2 or 3` parse
(a left-associative operator may follow a list one), while `and … andthen`,
`andthen … notandthen`, `andthen … and`, `or … orelse`, `or … xor`,
`orelse … xor`, `Int notandthen a() andthen b()` and
`Any andthen a() notandthen b()` are `X::Syntax::NonListAssociative`;
`andthen … orelse` crosses levels and parses.
`a() xor b() xor c()` with three true operands is Nil.
```
say (1 andthen 2), " ", (0 andthen 1), " ", (Any andthen 2).raku, " ", (Empty andthen 42).raku, " ", (1 andthen Any).raku, " ", (Any orelse 2), " ", (1 orelse 0), " ", (Any orelse Int orelse 3), " ", (Nil andthen 'foo' orelse Nil orelse 'bar'), " ", (0 andthen 1 ?? 2 !! 3), " ", do { my $t = 0; Int andthen ($t = 1); $t }, " ", do { my $t = 0; 1 orelse ($t = 1); $t }, " ", (5 andthen $_ * 2), " ", (5 andthen $_ * 2 andthen $_ + 1), " ", do { sub f { fail "boo" }; (f() orelse $_.^name ~ ":" ~ $_.handled ~ ":" ~ $_.exception.message) }, " ", (1 andthen 2 andthen 3), " ", (Str andthen .uc orelse "foo"), " ", (1 and 2 or 3), " ", (0 and 2 or 3), " ", (1 or 2 and 3), " ", (try EVAL 'sub a { 1 }; sub b { 2 }; sub c { 3 }; a() and b() andthen c()') // $!.^name, " ", (try EVAL 'sub a { 1 }; sub b { 2 }; sub c { 3 }; a() andthen b() notandthen c()') // $!.^name, " ", (try EVAL 'sub a { 1 }; sub b { 2 }; sub c { 3 }; a() or b() orelse c()') // $!.^name, " ", (try EVAL 'sub a { 1 }; sub b { 2 }; sub c { 3 }; a() or b() xor c()') // $!.^name, " ", do { my ($a,$b,$c) = 1,2,3; $a xor $b or $c }, " ", (try EVAL 'sub a { 1 }; sub b { 2 }; sub c { 3 }; a() andthen b() orelse c()') // $!.^name, " ", (1 andthen 2 or 3), " ", (Nil orelse "d").raku, " ", do { my $s = 0; (Nil andthen $s++); $s }, " ", ("x" andthen "$_$_"), " ", (Any andthen 2 orelse 3), " ", (1 andthen Nil orelse 4), " ", (0 orelse 9), " ", ((1, 2) andthen $_.elems), " ", (5 andthen .Str.^name), " ", (try EVAL 'sub a { 1 }; sub b { 2 }; sub c { 3 }; a() xor b() xor c()') // $!.^name, " ", (1 and 0 or 3), " ", (0 or 0 and 3).raku, " ", do { my ($a,$b,$c) = 1,2,3; $a orelse $b or $c }, " ", (Nil orelse $_.raku), " ", do { sub g { fail "z" }; my $r = (g() orelse "h:" ~ $_.handled); $r }, " ", do { sub g { fail "z" }; my $r = g() // "def"; $r }, " ", (1 andthen 2 andthen Nil).raku, " ", (Nil andthen 2 andthen 3).raku, " ", (try EVAL 'sub a { 1 }; sub b { 2 }; sub c { 3 }; a() andthen b() and c()') // $!.^name, " ", (try EVAL 'sub a { 1 }; sub b { 2 }; sub c { 3 }; a() orelse b() xor c()') // $!.^name, " ", (try (Int notandthen 42.Int)) // $!.^name, " ", (try (1 notandthen 42.Int).raku) // $!.^name, " ", (try (Empty notandthen 42.Int)) // $!.^name, " ", (try EVAL 'sub a { Any }; sub b { 2 }; (a() andthen 1) notandthen b()') // $!.^name, " ", (try EVAL 'sub a { 1 }; sub b { 2 }; Int notandthen a() andthen b()') // $!.^name, " ", (try EVAL 'sub a { 1 }; sub b { 2 }; Any andthen a() notandthen b()') // $!.^name
# rakudo 2026.08: 2 1 Empty Empty Any 2 1 3 bar 2 0 0 10 11 Failure:True:boo 3 foo 2 3 1 X::Syntax::NonListAssociative X::Syntax::NonListAssociative X::Syntax::NonListAssociative X::Syntax::NonListAssociative 3 2 2 "d" 0 xx 3 4 0 2 Str Any 3 0 1 Nil h:True def Nil Empty 3 X::Syntax::NonListAssociative 42 Empty 42 2 X::Syntax::NonListAssociative X::Syntax::NonListAssociative
```
rakupp 4.0.1-84: differs in thirteen fields — an undefined left operand of `andthen` yields itself (`Any`) rather than `Empty`, `orelse` does not mark the Failure handled, every same-level mix is accepted, `notandthen` does not exist (`X::Undeclared::Symbols`), and `1 andthen 2 andthen Nil` / `Nil andthen 2 andthen 3` give `Nil`.

### CP-10  Assignment: item vs list, the metaoperators, `::=`             D:yes R:yes V:spec
`=` is item assignment when the target is a scalar and list assignment
otherwise; the choice is by the sigil of the target, so `@a[0] = 1, 2` and
`%h<a> = 1, 2` are list assignments storing `(1, 2)` in the element,
`my $x = 1, 2` stores 1 (the 2 is sunk, CP-29). Item assignment is tighter
than the comma and looser than `?? !!` (`$x = 1 ?? 2 !! 3` stores 2,
`$x += 1 ?? 10 !! 20` adds 10) and than the list infixes
(`my $a = (1, 3) X (2, 4)` stores `(1, 3)`, CP-29); list assignment is
looser than the comma and than `Z`/`X` (`my @d = 1,3 Z 2,4` is
`[(1, 2), (3, 4)]`). Assignment is right-associative
(`my $y = $x = 3`, `$x = my $y = 2`) and returns the container
(`my $y = ($x = 5) + 1`). The assignment metaoperators `.= x= //= min=
max= ~= **=` take a whole tight-level expression on the right
(`$x x= 1 + 1` doubles, `$x min= 3 max 4` stores 4, `$c ~= "b" ~ "c"`);
`.=` is on the method level (`($s .= uc ?? 1 !! 2)` tests the result).
`::=` is `X::Comp::NYI`; `:=` binds (`my $x := 5`).
```
say do { my @a = 1, 2; @a.raku }, " ", do { my $x; my $y = $x = 3; "$x $y" }, " ", do { my %h = a => 1, b => 2; %h.elems }, " ", do { my $p = a => 1; $p.^name }, " ", do { my @d = 1,3 Z 2,4; @d.raku }, " ", do { my $s = "ab"; $s .= uc; $s }, " ", do { my $s = "ab"; $s x= 2; $s }, " ", do { my $u; $u //= 5; $u //= 6; $u }, " ", do { my $m = 5; $m min= 3; $m max= 4; $m }, " ", do { my $c = "a"; $c ~= "b" ~ "c"; $c }, " ", do { my $x = 1; $x += 1 ?? 10 !! 20; $x }, " ", do { my $x = 0; $x = 1 ?? 2 !! 3; $x }, " ", (try EVAL 'my $x ::= 1; $x') // $!.^name, " ", do { my $x := 5; $x }, " ", do { my @a; @a[0] = 1, 2; @a.raku }, " ", do { my %h; %h<a> = 1, 2; %h.raku }, " ", do { my $x = 1; $x = $x + 1 * 2; $x }, " ", do { my ($a, $b) = 1, 2; "$a $b" }, " ", do { my ($a, $b) = (1, 2), 3; "$a $b" }, " ", do { my $a = my $b = 4; "$a $b" }, " ", do { my $x = 1; $x x= 1 + 1; $x }, " ", do { my $x = 5; $x min= 3 max 4; $x }, " ", do { my $s = "a"; ($s .= uc ?? 1 !! 2) ~ $s }, " ", do { my @a = 1; @a = 2, 3 ?? 4 !! 5; @a.raku }, " ", do { my $x = 1; my $y = 2; ($x, $y) = ($y, $x); "$x $y" }, " ", do { my @a = 1, 2 Z 3, 4; @a.elems }, " ", do { my ($a, @b) = 1, 2, 3; "$a @b[]" }, " ", do { my $x = 1; $x ~= 2 x 2; $x }, " ", do { my $x = 2; $x **= 3 ** 1; $x }, " ", do { my $x = 1; my $y = ($x = 5) + 1; "$x $y" }, " ", do { my @a = 1; @a = 1, 2 Z 3, 4; @a.raku }, " ", do { my @a; @a = (1, 2) X (3, 4); @a.elems }, " ", do { my $x; $x = (1, 2); $x.raku }, " ", do { my @a = 1 => 2, 3 => 4; @a.elems }, " ", do { my %h = 1 => 2, 3 => 4; %h.elems }, " ", do { my $x = 1 => 2 => 3; $x.value.^name }, " ", do { my @a = (1, 2), (3, 4); @a.elems }, " ", do { my @a = [1, 2], [3, 4]; @a.elems }, " ", do { my @a; @a = 1; @a.raku }, " ", do { my $x = 1; $x = 2 == 2; $x }, " ", do { my $x = 1; $x = 2 ~~ 2; $x }, " ", do { my $x; $x = 1 && 0; $x }, " ", do { my $x = 1; $x = 3 xx 2; $x.raku }, " ", do { my $x = 1; $x = my $y = 2; "$x $y" }, " ", do { my $x = 1; $x =:= $x }, " ", do { my ($x, $y); $x = $y = 3; "$x $y" }
# rakudo 2026.08: [1, 2] 3 3 2 Pair [(1, 2), (3, 4)] AB abab 5 4 abc 11 2 X::Comp::NYI 5 [(1, 2),] {:a($(1, 2))} 3 1 2 1 2 3 4 4 11 4 1A [2, 4] 2 1 2 1 2 3 122 8 5 6 [(1, 3), (2, 4)] 4 $(1, 2) 2 2 Pair 2 2 [1] True True 0 $((3, 3).Seq) 2 2 True 3 3
```
rakupp 4.0.1-84: differs in one field — `::=` is `X::Syntax::Confused` instead of `X::Comp::NYI`.

### CP-11  Comma, the list infixes, pairs                                  D:yes R:yes V:spec
The comma is tighter than `Z`, `X`, `...` and `minmax`, which are
list-associative among the same operator only: `1, 2 Z 3, 4` is
`((1, 3), (2, 4))`, `1, 2 ... 5, 6` is `(1, 2) ... (5, 6)` (which reaches
6), `1, 2 Z 3, 4 X 5, 6`, `4 X+> 1...2` and `1, 2 ... 3 Z 4, 5 ... 6`
are `X::Syntax::NonListAssociative`, `1, 2 Z 3, 4 Z 5, 6` is one
three-way zip and `1 X 2 X 3` one cross. `=>` is on the item-assignment
level and right-associative: `1 => 2 => 3` has key 1 and a Pair value,
`a => 1 + 1` is `:a(2)`, `1 => 2 ?? 3 !! 4` is `1 => 3` while
`1 ?? 2 !! 3 => 4` is `2 => 4`, and `1 => 2, 3` is a two-element list.
`any`, `[+]`, `flat` are list prefixes and take everything after them
(`any flat 1, 2 Z 3, 4` is `any(1, 3, 2, 4)`, `[+] 1, 2, 3 X 2` sums the
three cross pairs by count, 6). `1, 2 X ()` and `() Z 1, 2` are empty;
`(1, 2) Z (3, 4), (5, 6)` zips the first list with the two-element list of
lists.
```
say (1, 2 + 3).raku, " ", ((1, 2) X (3, 4)).raku, " ", (1, 2 Z 3, 4).raku, " ", (try EVAL '1, 2 Z 3, 4 X 5, 6') // $!.^name, " ", (try EVAL '4 X+> 1...2') // $!.^name, " ", (1, 2 ... 5).raku, " ", (1, 2 ... 5, 6).raku, " ", (any flat 1, 2 Z 3, 4).raku, " ", (1..2 X 3..4).raku, " ", ((1, 5) minmax (0, 2)).raku, " ", ([+] 1, 2, 3), " ", ([+] 1, 2, 3 X 2).raku, " ", (1, 2 Z+ 3, 4).raku, " ", (1 X 2 X 3).raku, " ", (1, 2 Z 3, 4 Z 5, 6).raku, " ", (1 => 2 => 3).raku, " ", (1 => 2 => 3).key.^name, " ", (1, 2 xx 2).raku, " ", ((1, 2) xx 2).raku, " ", (1 .. 3 X 1 .. 2).elems, " ", (1, 2 X~ 3, 4).raku, " ", (1 => 2, 3 => 4).raku, " ", (a => 1 + 1).raku, " ", ("a" => 1 ~ 2).raku, " ", (1 ... 3, 4 ... 6).raku, " ", (<a b> X <c d>).elems, " ", (1, 2 minmax 3).raku, " ", (1, 2 X 3, 4).elems, " ", (1 => 2 ?? 3 !! 4).raku, " ", (1 ?? 2 !! 3 => 4).raku, " ", (1 => 2 => 3).value.raku, " ", (1 => 2 => 3).key, " ", (1, 2 Z (3, 4)).raku, " ", (any 1, 2).raku, " ", (any 1, 2 == 2).raku, " ", (try EVAL '(1, 2 ... 3 Z 4, 5 ... 6).elems') // $!.^name, " ", (1, 2 X+ 3, 4).raku, " ", (1, 2 X 3, 4).raku, " ", (1, 2 Z 3).raku, " ", (1, 2 X ()).raku, " ", (() Z 1, 2).raku, " ", (1 X 2).raku, " ", (1 Z 2).raku, " ", ((1, 2) Z (3, 4), (5, 6)).raku, " ", ((1, 2) X (3, 4), (5, 6)).raku, " ", (1, 2 ... 3).raku, " ", (1, 2 ... 3, 4).elems, " ", (1 => 2, 3).raku, " ", (1, 2 => 3).raku, " ", (a => 1, b => 2).elems, " ", (a => (1, 2)).raku, " ", (a => 1, 2).raku, " ", (1 => 2 X 3).raku, " ", (1 X 2 => 3).raku
# rakudo 2026.08: (1, 5) ((1, 3), (1, 4), (2, 3), (2, 4)).Seq ((1, 3), (2, 4)).Seq X::Syntax::NonListAssociative X::Syntax::NonListAssociative (1, 2, 3, 4, 5).Seq (1, 2, 3, 4, 5, 6).Seq any(1, 3, 2, 4) ((1, 3), (1, 4), (2, 3), (2, 4)).Seq 0..5 6 6 (4, 6).Seq ((1, 2, 3),).Seq ((1, 3, 5), (2, 4, 6)).Seq 1 => 2 => 3 Int (1, (2, 2).Seq) ((1, 2), (1, 2)).Seq 6 ("13", "14", "23", "24").Seq (1 => 2, 3 => 4) :a(2) :a("12") (1, 2, 3, 4, 5, 6).Seq 4 1..3 4 1 => 3 2 => 4 2 => 3 1 ((1, 3), (2, 4)).Seq any(1, 2) any(1, Bool::True) X::Syntax::NonListAssociative (4, 5, 5, 6).Seq ((1, 3), (1, 4), (2, 3), (2, 4)).Seq ((1, 3),).Seq ().Seq ().Seq ((1, 2),).Seq ((1, 2),).Seq ((1, (3, 4)), (2, (5, 6))).Seq ((1, (3, 4)), (1, (5, 6)), (2, (3, 4)), (2, (5, 6))).Seq (1, 2, 3).Seq 4 (1 => 2, 3) (1, 2 => 3) 2 :a((1, 2)) (:a(1), 2) ((1 => 2, 3),).Seq ((1, 2 => 3),).Seq
```
rakupp 4.0.1-84: differs in five fields — `1, 2 Z 3, 4 X 5, 6` and `4 X+> 1...2` are accepted, `[+] 1, 2, 3 X 2` is 12, `1 ?? 2 !! 3 => 4` is 2 (the pair binds tighter than the ternary), and `(1, 2 ... 3 Z 4, 5 ... 6).elems` is 1000000.

### CP-12  Reduction: associativity, chains, identities, one operand       D:yes R:yes V:spec
`[op]` folds by the operator's associativity: `[**] 2,3,2` is 512 (from
the right), `[-] 1,2,3` is -4, `[R-] 1,2,3` is 0 (reversed and
right-folded). A chaining operator reduces as a chain (`[<] 1,2,3` True,
`[<] 1,3,2` False, `[!=] 1,2,1` True, `[<] 1,2,2` False). A
non-associative operator (`..`, `..^`, `<=>`, `cmp`, `but`) is a
compile-time `X::Syntax::CannotMeta`, and so is `[=]`. Empty lists give
the identity: `[+]` 0, `[*]` 1, `[~]` `""`, `[<]`/`[==]`/`[&&]` True,
`[||]` False, `[min]` Inf, `[max]` -Inf, `[**]` 1, `[-]` 0, `[,]` `()`,
`[/]` a Failure `X::NoZeroArgMeaning`. One operand is returned unchanged
by every operator except the chains (True) and `[=>]` (`X::AdHoc`).
The triangle `[\op]` yields every partial result as a Seq
(`[\+] 1,2,3` is `(1, 3, 6)`, `[\**] 2,3,2` is `(2, 9, 512)`,
`[\<] 1,3,2` is `(True, True, False)`, `[\R-] 1,2,3` is `(3, 1, 0)`).
`[X]`/`[Z]` of lists cross and zip them; `[+] (1,2), (3,4)` adds the
counts (4); `[+] 1..3` and `[+] [1,2]` flatten the single argument.
```
say ([**] 2,3,2), " ", ([-] 1,2,3), " ", ([<] 1,2,3), " ", ([<] 1,3,2), " ", ([==] 1,1,1), " ", (try EVAL '[..] 1,2,3') // $!.^name, " ", (try EVAL '[<=>] 1,2') // $!.^name, " ", (try EVAL '[but] 1,2') // $!.^name, " ", ([R-] 1,2,3), " ", ([\+] 1,2,3).raku, " ", ([\-] 1,2,3).raku, " ", ([max] 1,3,2), " ", ([||] 0, 0, 3), " ", ([&&] 1, 2, 0), " ", ([//] Nil, Any, 5), " ", ([~] <a b c>), " ", ([+] ()), " ", ([*] ()), " ", ([<] ()), " ", ([<] 1), " ", ([-] 1), " ", ([X] (1,2),(3,4)).raku, " ", ([min] ()).raku, " ", ([max] ()).raku, " ", ([~] ()).raku, " ", ([==] ()), " ", ([&&] ()), " ", ([||] ()).raku, " ", ([,] 1,2,3).raku, " ", ([=>] 1,2,3).raku, " ", ([<=] 1,1,2), " ", ([R**] 2,3,2), " ", ([Z] (1,2),(3,4)).raku, " ", ([eqv] 1,1,1), " ", ([!=] 1,2,1), " ", ([**] ()), " ", ([-] ()), " ", ([/] ()).raku, " ", ([x] "a", 2, 2), " ", ([xx] 1, 2, 2).raku, " ", ([\**] 2,3,2).raku, " ", ([R~] <a b c>), " ", ([**] 2,2,3), " ", ([-] 1,2,3,4), " ", ([\<] 1,2,3).raku, " ", ([\<] 1,3,2).raku, " ", ([\R-] 1,2,3).raku, " ", ([<] 1,2,2), " ", ([<=] 1,2,2), " ", (try ([=>] 1).raku) // $!.^name, " ", ([,] 1).raku, " ", ([,] ()).raku, " ", ([&&] 1), " ", ([**] 4), " ", ([R-] 1, 2), " ", ([R-] 5), " ", ([\R-] 1, 2, 3).elems, " ", (try EVAL '[..^] 1, 2') // $!.^name, " ", (try EVAL '[cmp] 1, 2') // $!.^name, " ", (try EVAL '[=] 1, 2') // $!.^name, " ", ([\==] 1,1,2).raku, " ", ([\,] 1,2).raku, " ", ([\~] <a b c>).raku, " ", ([\max] 1,3,2).raku, " ", ([\||] 0,1,0).raku, " ", ([andthen] 1,2,3), " ", ([orelse] Nil,Any,3), " ", ([~~] 1, Int, Any), " ", ([&&] 1, 2, 3), " ", ([R-] 1,2,3,4), " ", ([\-] ()).raku, " ", ([\+] 1).raku, " ", ([<] 2,1), " ", ([+] 1..3), " ", ([+] [1,2]), " ", ([+] (1,2), (3,4)), " ", ([Z] 1, 2).raku
# rakudo 2026.08: 512 -4 True False True X::Syntax::CannotMeta X::Syntax::CannotMeta X::Syntax::CannotMeta 0 (1, 3, 6).Seq (1, -1, -4).Seq 3 3 0 5 abc 0 1 True True 1 ((1, 3), (1, 4), (2, 3), (2, 4)).Seq Inf -Inf "" True True Bool::False (1, 2, 3) 1 => 2 => 3 True 512 ((1, 3), (2, 4)).Seq True True 1 0 Failure.new(exception => X::NoZeroArgMeaning.new(name => "infix:</>"), backtrace => Backtrace.new) aaaa ((1, 1).Seq, (1, 1).Seq).Seq (2, 9, 512).Seq cba 256 -8 (Bool::True, Bool::True, Bool::True).Seq (Bool::True, Bool::True, Bool::False).Seq (3, 1, 0).Seq False True X::AdHoc (1,) () 1 4 1 5 3 X::Syntax::CannotMeta X::Syntax::CannotMeta X::Assignment::RO (Bool::True, Bool::True, Bool::False).Seq ((1,), (1, 2)).Seq ("a", "ab", "abc").Seq (1, 3, 3).Seq (0, 1, 1).Seq 3 3 True 3 -2 ().Seq (1,).Seq False 6 3 4 ((1, 2),).Seq
```
rakupp 4.0.1-84: differs in ten fields — the non-associative operators reduce (`[..]`/`[..^]`/`[but]` are `X::NYI`, `[<=>]`/`[cmp]` give `Less`), `[/] ()` is 1, `[=>] 1` is 1, `[xx] 1, 2, 2` and `[\||] 0,1,0` come back as Lists instead of Seqs, and `[+] (1,2), (3,4)` is 10.

### CP-13  Hyper operators: shape rules and precedence                    D:yes R:yes V:quirk
A hyper infix keeps the precedence of its base operator
(`(1,2,3) >>+<< (1,2,3) >>*<< (2,2,2)` is `(3, 6, 9)`,
`(1,2) >>**<< (2,2) >>+<< (1,1)` is `(2, 5)`,
`(1,2,3) >>+<< (1,2,3) == 6` compares the count, `(1,2) >>+<< 5 xx 2`
is `((1,2) >>+<< 5) xx 2` and dies) and recurses into nested lists
(`((1,2),(3,4)) >>+<< ((10,20),(30,40))`). The pointy side must match the
other side's length; a blunt side (`>>op>>`, `<<op<<`, `<<op>>`) is
repeated to the pointed side's length (`(1,2,3) >>+>> (1,2)` is
`(2, 4, 4)`, `(1,2,3) <<+<< (1,2)` is `(2, 4)`, `(1,2) <<+>> (1,2,3)`
is `(2, 4, 4)`); a scalar is a one-element list (`(1,2,3) >>+>> 1`,
`1 >>+<< 2` is 3, `(1,2) >>+<< (10,)` and `1 >>+>> (1,2)` are
`X::HyperOp::NonDWIM`). Two empty lists give `()`; an empty blunt side
against a pointed one is `()`, an empty pointed side against a blunt one
is `X::HyperOp::NonDWIM`. Hashes hyper by key: `>>+<<` keeps the
intersection, `>>+>>` the left keys, `<<+>>` the union. The result is a
List, or an Array when the left side is an Array; `-<<` and `>>.meth`
hyper the unary and the method (`(1,-2)>>.abs`). The quirk: `>>.^name` is
not a hyper method call — `(1,2)>>.^name` answers `"List"`, the list's
own metaobject name.
```
say ((1,2,3) >>+<< (10,20,30)).raku, " ", ((1,2,3) >>+>> (1,2)).raku, " ", ((1,2,3) <<+<< (1,2)).raku, " ", ((1,2) <<+>> (1,2,3)).raku, " ", (try ((1,2,3) >>+<< (1,2)).raku) // $!.^name, " ", (try ((1,2) >>+<< (1,2,3)).raku) // $!.^name, " ", ((1,2,3) >>+>> 1).raku, " ", (try ((1,2,3) >>+<< 1).raku) // $!.^name, " ", (((1,2),(3,4)) >>+<< ((10,20),(30,40))).raku, " ", (() >>+<< ()).raku, " ", ([1,2] >>+<< [3,4]).raku, " ", (-<< (1,2)).raku, " ", ((1,-2)>>.abs).raku, " ", (1 >>+<< 2).raku, " ", ((1,2,3) >>+<< (1,2,3) >>*<< (2,2,2)).raku, " ", ((1,2) >>~<< <a b>).raku, " ", ((1,2,3) «+» (1,2)).raku, " ", ({a => 1, b => 2} >>+<< {a => 10, c => 5}).sort.raku, " ", ({a => 1, b => 2} >>+>> {a => 10, c => 5}).sort.raku, " ", ({a => 1, b => 2} <<+>> {a => 10, c => 5}).sort.raku, " ", (try (1 >>+>> (1,2)).raku) // $!.^name, " ", ((1,2) >>+<< (1,2)).^name, " ", ([1,2] >>+<< [3,4]).^name, " ", ((1,(2,3)) >>+>> 1).raku, " ", (try ((1,2) >>+<< (1,(2,3))).raku) // $!.^name, " ", ((1,2,3) »+« (3,2,1)).raku, " ", (try ((1,2) <<+>> ()).raku) // $!.^name, " ", (try ((1,2) >>+<< ()).raku) // $!.^name, " ", (try ((1,2) >>+>> ()).raku) // $!.^name, " ", ((1,2,3) >>+<< (1,2,3) == 6).raku, " ", (1 + (1,2) >>*>> 2).raku, " ", ((1,2) >>**<< (2,2) >>+<< (1,1)).raku, " ", (<a b>>>.uc).raku, " ", ((1,2)>>.^name).raku, " ", ({a => 1}>>.Str).raku, " ", ((1, (2, 3))>>.Str).raku, " ", ([[1,2],[3]]>>.elems).raku, " ", (try ((1,2,3) >>+<< (1,2)).raku) // $!.message.subst(/\s+/, " ", :g), " ", ((1,2) <<+<< ()).raku, " ", (() <<+>> (1,2)).raku, " ", (try ((1,2) >>+<< 5 xx 2).raku) // $!.^name, " ", ((1,2) >>+>> (10,)).raku, " ", (try ((1,2) >>+<< (10,)).^name) // $!.^name, " ", ((1,2) >>%%<< (1,3)).raku, " ", ((1,2) >>==<< (1,3)).raku, " ", ((1,2) >>+<< (1,2) >>==<< (2,4)).raku, " ", ((1, 2) >>+<< (1, 2)).is-lazy, " ", ((1..3) >>+>> 1).raku, " ", ((1,2) >>+<< (1,2)).WHAT.raku, " ", (my @h = (1,2) >>+<< (1,2)).raku, " ", ((1,2) >>+<< [1,2]).raku, " ", ([1,2] >>+<< (1,2)).raku, " ", ((1,2).Seq >>+<< (1,2)).^name, " ", ((1,2) >>+<< (1,2).Seq).^name
# rakudo 2026.08: (11, 22, 33) (2, 4, 4) (2, 4) (2, 4, 4) X::HyperOp::NonDWIM X::HyperOp::NonDWIM (2, 3, 4) X::HyperOp::NonDWIM ((11, 22), (33, 44)) () [4, 6] (-1, -2) (1, 2) 3 (3, 6, 9) ("1a", "2b") (2, 4, 4) (:a(11), :b(2), :c(5)).Seq (:a(11), :b(2)).Seq (:a(11),).Seq X::HyperOp::NonDWIM List Array (2, (3, 4)) X::HyperOp::NonDWIM (4, 4, 4) () X::HyperOp::NonDWIM () Bool::False 3 (2, 5) ("A", "B") "List" {:a("1")} ("1", $("2", "3")) (2, 1) Lists on either side of non-dwimmy hyperop of infix:<+> are not of the same length while recursing left: 3 elements, right: 2 elements () () X::HyperOp::NonDWIM (11, 12) X::HyperOp::NonDWIM (Bool::True, Bool::False) (Bool::True, Bool::False) (Bool::True, Bool::True) False (2, 3, 4) List [2, 4] (2, 4) [2, 4] List List
```
rakupp 4.0.1-84: differs in three fields — `(1,2)>>.^name` is `("Int", "Int")`, `(1, (2, 3))>>.Str` does not itemize the inner list, and the NonDWIM message has no "while recursing" detail.

### CP-14  The method-postfix level and the space rules of postfixes      D:partial R:partial V:spec
A postfix must touch its term: `@a .[0]` and `%h .<a>` are
`X::Syntax::Malformed`, `@a [0]`, `@a[0] [1]` and `-@a [0]` are
`X::Syntax::Missing` (a term where an infix is expected), `say(1) (2)` is
`X::Syntax::Confused`; the unspace forms `@a\ .[0]`, `@a\.[0]`,
`%h\ .<a>`, `$s\ .uc` bind the postfix, and so does the dotted form
`@a.[1]`, `%h.<a>`, `%h.{"a"}`, `@a.[0..1]`. A space after the dot is
allowed (`$s. uc`). A spaced dot before a method name on a term is the
dotty infix at the symbolic-unary level: `1..3 .elems` is `1..(3 .elems)`,
`1 + 2 .Str` is `1 + "2"` (Int), `"ab" .uc` and `$s .uc` are `AB`,
`2 .Str.chars` is 1. `f (1), 2` calls `f` with two arguments (2 elems),
`f(1), 2` is a two-element list; `f [1]` passes the array to `f`
(`X::AdHoc` "too many positionals" when `f` takes none). `5.&f` and
`5.&f("z")` call a sub as a method; `"3".Int ** 2` and `2 ** "3".Int`
bind the postfix first; `$x.abs++` is `X::Multi::NoMatch`.
```
say (-1.abs), " ", (2.Str x 2), " ", (try EVAL 'my @a = 1,2; @a .[0]') // $!.^name, " ", (try EVAL 'my @a = 1,2; @a\ .[0]') // $!.^name, " ", (try EVAL 'my @a = 1,2; @a\.[0]') // $!.^name, " ", (try EVAL 'my @a = 1,2; @a [0]') // $!.^name, " ", (try EVAL 'my %h = a => 1; %h .<a>') // $!.^name, " ", (try EVAL 'my %h = a => 1; %h\ .<a>') // $!.^name, " ", do { my @a = 5,6; @a.[1] ~ " " ~ @a.[0] }, " ", do { my %h = a => 1; %h.<a> ~ %h.{"a"} ~ %h<a> }, " ", do { sub f(*@a) { @a.elems }; f (1), 2 }, " ", do { sub f(*@a) { @a.elems }; (f(1), 2).raku }, " ", do { sub f($x) { $x * 2 }; 5.&f }, " ", ("3".Int ** 2), " ", (2 ** "3".Int), " ", (1.5.Int), " ", do { my @a = 1,2; -@a.elems }, " ", (try EVAL 'my $x = 3; $x.abs++') // $!.^name, " ", (2.5.Int.Str ~ 1), " ", (1..3 .elems).raku, " ", (try EVAL '2 .Str.chars') // $!.^name, " ", (try EVAL '(1 + 2 .Str).^name') // $!.^name, " ", (try EVAL '"ab" .uc') // $!.^name, " ", (try EVAL 'my $s = "ab"; $s .uc') // $!.^name, " ", (try EVAL 'my $s = "ab"; $s\ .uc') // $!.^name, " ", (try EVAL 'my $s = "ab"; $s. uc') // $!.^name, " ", do { my @a = 1,2; @a[0]++; -@a[0] }, " ", (try EVAL '!1.Str') // $!.^name, " ", do { sub f($x, $y) { $x ~ $y }; 5.&f("z") }, " ", (try EVAL 'my @a = 1,2; @a[ 0 ]') // $!.^name, " ", (try EVAL 'my @a = 1,2; @a[0] [1]') // $!.^name, " ", (try EVAL 'say(1) (2)') // $!.^name, " ", do { my @a = 1,2; @a.[0..1].raku }, " ", do { my @a = 1,2; @a.elems.Str ~ @a.[1] }, " ", (try EVAL 'my @a = 1,2; -@a [0]') // $!.^name, " ", (try EVAL 'sub f { 1 }; f [1]') // $!.^name, " ", (try EVAL 'sub f($x) { $x }; f [1]') // $!.^name
# rakudo 2026.08: -1 22 X::Syntax::Malformed 1 1 X::Syntax::Missing X::Syntax::Malformed 1 6 5 111 2 (1, 2) 10 9 8 1 -2 X::Multi::NoMatch 21 1..1 1 Int AB AB AB AB -2 False 5z 1 X::Syntax::Missing X::Syntax::Confused (1, 2) 22 X::Syntax::Missing X::AdHoc [1]
```
rakupp 4.0.1-84: differs in eight fields — `@a .[0]` and `%h .<a>` are accepted, the four `X::Syntax::Missing` cases and `say(1) (2)` are `X::Syntax::Confused`, `$x.abs++` is `X::Assignment::RO`, and `f [1]` with a no-argument `f` gives 1.

### CP-15  Binding, `=:=`, `temp` and `let`                               D:partial R:partial V:quirk
`:=` aliases containers (`$a =:= $b` True after `my $b := $a`, writes
through; `=:=` chains) while `=` copies; `my $y := $x + 1` binds a bare
Int (`.VAR.^name` is `Int`) and assigning to it is `X::AdHoc`; `my ($x,
$y) := (1, 2)` destructures. `temp` and `let` are on the autoincrement
level, tighter than `=` and `~`: `temp $x = 2 ~ "z"` localizes then
assigns; `temp 42` is `X::Localizer::NoContainer`; `(temp $x)` yields the
container. `temp` restores at scope exit, also after `die`, also for an
Array, a hash element and a double `temp`. Because the do block returns
its last expression's container, `do { temp $x = 2; $x }` is already
restored when read (`1 1`). `let` restores when the routine leaves by
`die`, `fail` or with an undefined value (`Nil`, `Any`, a Failure) and
keeps the change for a defined value that the caller uses (`my $r = t11`
keeps 2 for a routine ending in `1`). The quirk: when the call is sunk,
a routine ending in `1` or `0` restores (`t2; $x` is 1) while one ending
in `""`, `False`, `"0"`, `()`, `Nil` or an explicit `return 0`/`return 1`
keeps the change; Roast (`let.t`) asserts only the used-value cases, and
step two should follow those.
```
say do { my $a = 1; my $b := $a; my $c = $a; my $r1 = $a =:= $b; my $r2 = $a =:= $c; $b = 2; my $r3 = $a; $c = 3; "$r1 $r2 $r3 $a" }, " ", do { my $x = 1; my $in = do { temp $x = 2; $x }; "$in $x" }, " ", do { my $x = 1; sub t1 { temp $x; $x = 5; Nil }; t1; $x }, " ", do { my $x = 1; try { let $x = 2; die }; $x }, " ", do { my $x = 1; sub t2 { let $x = 2; 1 }; t2; $x }, " ", do { my $x = 1; try { let $x = 2; fail "x" }; $x }, " ", do { my $x = 1; sub t3 { let $x = 2; Nil }; t3; $x }, " ", do { my $x = 1; sub t4 { let $x = 2; 0 }; t4; $x }, " ", do { my $a = 1; my $b := $a; my $c := $b; ($a =:= $b =:= $c) }, " ", do { my @a = 1,2; my @b := @a; @b.push(3); @a.raku }, " ", do { my ($x, $y) := (1, 2); "$x $y" }, " ", do { my $x = 5; my $y := $x + 1; $y.VAR.^name }, " ", (try EVAL 'my $x := 1; $x = 2') // $!.^name, " ", do { my $x = 1; temp $x = 2 ~ "z"; $x }, " ", do { my $x = 1; my $in = do { temp $x = 2; temp $x = 3; $x }; "$in $x" }, " ", do { my @a = 1,2; my $in = do { temp @a; @a.push(3); @a.elems }; "$in " ~ @a.elems }, " ", do { my %h = a => 1; my $in = do { temp %h<a> = 9; %h<a> }; "$in " ~ %h<a> }, " ", do { my $x = 1; my $r = (temp $x); $x = 7; $r }, " ", (try EVAL 'temp 42') // $!.^name, " ", do { my $x = 1; sub t5 { temp $x; temp $x; $x = 3; Nil }; t5; $x }, " ", do { my $x = 1; my $y = 1; my $in = do { temp $x = temp $y = 2; "$x$y" }; "$in $x$y" }, " ", do { my $x = 1; sub t6 { let $x = 2; "" }; t6; $x }, " ", do { my $x = 1; sub t7 { let $x = 2; False }; t7; $x }, " ", do { my $x = 1; sub t8 { let $x = 2; (0, 1) }; t8; $x }, " ", do { my $x = 1; sub t9 { let $x = 2; Failure.new }; my $f = t9; $f.so; $x }, " ", do { my $x = 1; sub t10 { let $x = 2; Any }; t10; $x }, " ", do { my $x = 1; sub f { let $x = 2; return 0 }; f; $x }, " ", do { my $x = 1; sub g { let $x = 2; return 1 }; g; $x }, " ", do { my $x = 1; try { temp $x = 2; die }; $x }, " ", do { my $x = 1; sub t11 { let $x = 2; 1 }; my $r = t11; "$r $x" }, " ", do { my $x = 1; sub t12 { let $x = 2; "0" }; t12; $x }, " ", do { my $x = 1; sub t13 { let $x = 2; () }; t13; $x }, " ", do { my $x = 1; sub t14 { let $x = 2; Nil }; my $r = t14; $x }, " ", do { my $x = 1; sub t15 { let $x = 2; $x }; my $r = t15; "$r $x" }, " ", do { my @a = 1; sub t16 { let @a = 5, 6; 0 }; t16; @a.raku }, " ", do { my $x = 1; my $y := $x; $y = 9; $x }, " ", do { my $x = 1; my $y = $x; $y = 9; $x }, " ", do { my @a = 1; my @b = @a; @b[0] = 9; @a.raku }, " ", do { my @a = 1; my @b := @a; @b[0] = 9; @a.raku }, " ", do { my $x = 1; { temp $x = 2; }; $x }
# rakudo 2026.08: True False 2 2 1 1 1 1 2 1 1 2 True [1, 2, 3] 1 2 Int X::AdHoc 1 1 1 3 2 1 1 1 X::Localizer::NoContainer 1 22 11 2 2 2 1 1 2 2 1 1 2 2 2 1 2 2 [5, 6] 9 1 [1] [9] 1
```
rakupp 4.0.1-84: differs in nineteen fields — `=:=` is False for bound aliases (also chained), a do block returns the value not the container (`2 1`, `3 1`, `9 1`, `2z`), `my $y := $x + 1` reports `Scalar` and accepts assignment, `temp 42` is `X::Assignment::RO`, and `let` never restores except after `die` (a `fail`, `Nil`, `Any`, a Failure and every sunk case keep 2).

### CP-16  Flip-flop and feeds                                             D:yes R:yes V:spec
`ff` starts when its left side is true for the current `$_`, tests the
right side on the same element, and yields the 1-based count of elements
in the current run (Nil while off): `(1..4).map({ $_ == 1 ff $_ == 2 })`
is `(1, 2, Nil, Nil)`, `$_ == 2 ff $_ == 2` is one element, `fff` waits
for the next element (`$_ == 2 fff $_ == 2` runs to the end). `^ff`
excludes the start, `ff^` the end, `^ff^` both (the count still starts at
the excluded start: `1 ^ff^ 3` over 1..4 yields `(Nil, 2, Nil, Nil)`). A
non-Bool operand is smartmatched against `$_` (`/b/ ff /d/`; `"a" ff …`
never starts; `*` always matches). Both sides are evaluated on every
element (a sub used on both sides is called twice per element). `ff`
sits on the conditional level: `&&`, `||`, `?? !!` and every comparison
bind inside it (`$_ > 1 && $_ < 3 ff $_ == 5`,
`$_ == 2 ff $_ == 4 || $_ == 5`, `$_ == 2 ff $_ == 4 ?? "y" !! "n"` never
ends), `andthen`/`or` bind outside; `ff` is right-associative. The feed
operators are the loosest of all: `1 or 2 ==> @r` feeds `(1 or 2)`,
`my @r = (1, 2) ==> map {…}` assigns before feeding, `<b a> ==> sort ==>
@r` needs `sort()` (`X::Syntax::InfixInTermPosition`), a feed into a
literal is `X::Comp::AdHoc`; `<==` reads right to left.
```
say (1..6).grep({ $_ == 2 ff $_ == 4 }).raku, " ", (1..6).grep({ $_ == 2 ^ff $_ == 4 }).raku, " ", (1..6).grep({ $_ == 2 ff^ $_ == 4 }).raku, " ", (1..6).grep({ $_ == 2 ^ff^ $_ == 4 }).raku, " ", (1..6).grep({ $_ == 2 ff $_ == 2 }).raku, " ", (1..6).grep({ $_ == 2 fff $_ == 2 }).raku, " ", (1..6).grep({ $_ %% 2 ff * }).raku, " ", (1..6).grep({ * ff $_ == 3 }).raku, " ", (1..3).grep({ True ff False }).raku, " ", ("a".."e").grep({ /b/ ff /d/ }).raku, " ", (1..6).map({ $_ == 2 ff $_ == 4 }).raku, " ", (1..4).map({ $_ == 2 fff $_ == 3 }).raku, " ", (1..6).grep({ $_ > 1 && $_ < 3 ff $_ == 5 }).raku, " ", (1..6).grep({ $_ == 2 ff $_ == 4 || $_ == 5 }).raku, " ", do { my @r; <a b c> ==> map { .uc } ==> @r; @r.raku }, " ", do { my @r <== map { .uc } <== <a b c>; @r.raku }, " ", do { my @r; 1, 2 ==> @r; @r.raku }, " ", do { my @r; <x y> ==> sort() ==> @r; @r.raku }, " ", do { my @r; <b a> ==> sort() ==> map({ $_ x 2 }) ==> @r; @r.raku }, " ", (try EVAL 'my @r; 1 ==> @r ==> 2') // $!.^name, " ", (1..6).map({ $_ == 2 ff^ $_ == 4 }).raku, " ", (1..5).map({ ($_ == 2 ff $_ == 4) ?? "y" !! "n" }).join, " ", (1..6).grep({ $_ == 2 ff $_ == 4 ff $_ == 6 }).raku, " ", (1..6).grep({ $_ == 2 fff * }).raku, " ", (1..6).grep({ ($_ == 2 ff $_ == 3) or $_ == 6 }).raku, " ", do { my @r; 1 or 2 ==> @r; @r.raku }, " ", do { my @r; (1, 2) ==> map({ $_ * 10 }) ==> @r; @r.raku }, " ", (try EVAL 'my @r; <b a> ==> sort ==> @r; @r.raku') // $!.^name, " ", (1..6).map({ ($_ == 2 ff $_ == 4 ?? "y" !! "n").raku }).join(","), " ", (1..6).map({ $_ == 2 ff $_ == 4 andthen "y" }).join, " ", (1..4).map({ ($_ == 1 ff $_ == 2).raku }).join(","), " ", (1..4).map({ ($_ == 1 ^ff^ $_ == 3).raku }).join(","), " ", (1..4).map({ ("a" ff $_ == 2).raku }).join(","), " ", (1..4).map({ (($_ == 2) fff ($_ == 3)).raku }).join(","), " ", do { my @s; sub f($v) { @s.push($v); $v == 2 }; (1..4).grep({ f($_) ff f($_) }).raku ~ @s.raku }, " ", do { my @r; <c a b> ==> sort() ==> reverse() ==> @r; @r.raku }, " ", do { my @r; (1, 2, 3) ==> grep({ $_ > 1 }) ==> @r; @r.raku }, " ", do { my @r = (1, 2) ==> map { $_ + 1 }; @r.raku }
# rakudo 2026.08: (2, 3, 4).Seq (3, 4).Seq (2, 3).Seq (3,).Seq (2,).Seq (2, 3, 4, 5, 6).Seq (2, 3, 4, 5, 6).Seq (1, 2, 3, 4, 5, 6).Seq (1, 2, 3).Seq ("b", "c", "d").Seq (Nil, 1, 2, 3, Nil, Nil).Seq (Nil, 1, 2, Nil).Seq (2, 3, 4, 5).Seq (2, 3, 4).Seq ["A", "B", "C"] ["A", "B", "C"] [1, 2] ["x", "y"] ["aa", "bb"] X::Comp::AdHoc (Nil, 1, 2, Nil, Nil, Nil).Seq nyyyn (2, 3, 4, 5, 6).Seq (2, 3, 4, 5, 6).Seq (2, 3, 6).Seq [1] [10, 20] X::Syntax::InfixInTermPosition Nil,1,2,3,4,5 yyy 1,2,Nil,Nil Nil,2,Nil,Nil Nil,Nil,Nil,Nil Nil,1,2,Nil (2,).Seq[1, 1, 2, 2, 3, 3, 4, 4] ["c", "b", "a"] [2, 3] [1, 2]
```
rakupp 4.0.1-84: differs in seven fields and three warnings — `* ff $_ == 3` and `2 ff 4 ff 6` yield nothing, the ternary binds outside `ff`, a bare `sort` in a feed is accepted, a feed into a literal is `X::Assignment::RO`, the right side of `ff` runs only while on, and joining Nils warns "Use of Nil in string context".

### CP-17  `=~=` and the comparison-with-Order family                     D:yes R:partial V:spec
`=~=` is relative to the larger magnitude with `$*TOLERANCE` (1e-15 by
default, dynamically rebindable): `1 =~= 1 + 1e-16` True, `1 =~= 1.1`
False, `1e10 =~= 1e10 + 1` True, `1e-20 =~= 2e-20` False; when one side
is 0 the difference is compared with the tolerance directly (`0 =~= 1e-16`
True, `1e-15 =~= 0` False); `Inf =~= Inf` True, `NaN =~= NaN` False; it
chains. `cmp` compares numerically when both sides are numeric, as
strings otherwise (`"10" cmp 9` and `10 cmp "9"` are Less, `2 cmp 10`
Less, `"2" cmp "10"` More, `<10> cmp 9` More, `<10> leg 9` Less,
`1.0 cmp 1` Same); `leg` always as strings; `<=>` numifies and throws
`X::Str::Numeric` on a non-numeric string; `Inf <=> NaN` and
`NaN <=> NaN` are Nil; `unicmp` and `coll` order by Unicode
collation (`"a" unicmp "B"` Less, `"é" unicmp "f"` Less where `cmp` says
More). Lists compare element-wise. `Less < More`, `Less == -1`,
`1 cmp 2 == Less`, `1 <=> 2 === Less` are True.
```
say (1 =~= 1 + 1e-16), " ", (1 =~= 1.1), " ", $*TOLERANCE, " ", do { my $*TOLERANCE = 0.5; (1 =~= 1.4) ~ " " ~ (1 =~= 1.6) }, " ", (0 =~= 1e-16), " ", (0 =~= 0), " ", (1e10 =~= 1e10 + 1), " ", (1e-20 =~= 2e-20), " ", (1 =~= 1.0000000001), " ", (0 =~= 1e-14), " ", (1 <=> 2), " ", ("a" leg "B"), " ", ("a" cmp "B"), " ", (1 cmp "1"), " ", ("10" cmp 9), " ", (10 cmp "9"), " ", ("a" unicmp "B"), " ", ("a" coll "B"), " ", (1 <=> "2"), " ", (try ("a" <=> 1)) // $!.^name, " ", (2 cmp 10), " ", ("2" cmp "10"), " ", ("2" leg "10"), " ", (1.0 cmp 1), " ", (Inf <=> NaN).raku, " ", (NaN <=> NaN).raku, " ", ("é" unicmp "e"), " ", ("é" cmp "f"), " ", ("é" unicmp "f"), " ", (1 <=> 1e0), " ", (<10> cmp 9), " ", (<10> leg 9), " ", (Less < More), " ", (Less == -1), " ", (1 cmp 2 == Less), " ", ("a" cmp "b" ~~ Less), " ", (1 <=> 2).^name, " ", (1 ≅ 1), " ", (1 == 1 =~= 1), " ", ((1,2) cmp (1,3)), " ", ((1,2) <=> (1,3)), " ", (("a", "b") cmp ("a", "c")), " ", (1e0 =~= 1), " ", (0e0 =~= -0e0), " ", (1e-16 =~= 0), " ", (1e-15 =~= 0), " ", (1e-14 =~= 0), " ", (Inf =~= Inf), " ", (NaN =~= NaN), " ", (1/3 =~= 0.333333333333333), " ", (Less cmp More), " ", (1 <=> 2 === Less)
# rakudo 2026.08: True False 1e-15 True True True True False False False False Less More More Same Less Less Less Less Less X::Str::Numeric Less More More Same Nil Nil More More Less Same More Less True True True True Order True True Less Same Less True True True False False True False True Less True
```
rakupp 4.0.1-84: matches.

### CP-18  `x` and `xx`: laziness, thunking, edge counts                  D:yes R:yes V:spec
`xx` returns a Seq (`(1 xx 3).raku` is `(1, 1, 1).Seq`), infinite and
lazy with `*` or `Inf` (`is-lazy` True, indexable, `head`); a finite one
is not lazy; `.elems` of an infinite one is `X::Cannot::Lazy`. The left
side is re-evaluated per element (`$n++ xx 3` is `(0, 1, 2)`, `[1] xx 2`
gives two distinct arrays, `rand xx 2` two different Nums, `{ … } xx 2`
two blocks not called). A count of 0 or a negative one gives an empty
Seq, 2.7 gives 2, `"2"` gives 2, `"b"` is `X::Str::Numeric`, NaN is
`X::Numeric::CannotConvert`. `x` gives a Str: `1 x 3` is `111`,
`"ab" x 2.7` is `abab`, `"ab" x 0` and `"ab" x -1` are `""`,
`"a" x Inf` is `X::NYI`, `"a" x NaN` `X::Numeric::CannotConvert`.
`1 xx 2 xx 2` is `((1, 1), (1, 1))`, `(1..3).map(…) xx 2` two Seqs.
```
say (1 xx 3).^name, " ", (1 xx 3).raku, " ", (1 xx *).^name, " ", (1 xx *).is-lazy, " ", (1 xx *)[5], " ", (1 xx 3).is-lazy, " ", do { my $n = 0; ($n++ xx 3).raku }, " ", do { my $n = 0; my @a = $n++ xx 3; "@a[] $n" }, " ", (1 xx 0).raku, " ", (1 xx -1).raku, " ", ((1, 2) xx 2).raku, " ", ([1] xx 2).raku, " ", do { my @a = [1] xx 2; @a[0].push(9); @a.raku }, " ", ("a" xx 2).join(","), " ", (1 xx Inf).^name, " ", (1 xx Inf).is-lazy, " ", (1 xx 2.7).raku, " ", ("ab" x 2.7), " ", ("ab" x 0).raku, " ", ("ab" x -1).raku, " ", (1 xx 1).raku, " ", (Nil xx 2).raku, " ", (1 xx *).head(2).raku, " ", ((1..3).map({$_}) xx 2).raku, " ", (1 x 3), " ", (1 x 3).^name, " ", ("a" x 2.5e0), " ", (1 xx "2").raku, " ", do { my $n = 0; my @a = { $n++ } xx 2; @a.elems ~ $n }, " ", (rand xx 2).map(*.^name).raku, " ", do { my @r = rand xx 2; (@r[0] == @r[1]) }, " ", (1 xx 3).elems, " ", (1 xx 3)[3].raku, " ", ((1 xx 2) xx 2).raku, " ", (1 xx 2 xx 2).raku, " ", (try ("a" x Inf).chars) // $!.^name, " ", (try ("a" x NaN)) // $!.^name, " ", (try ("a" x "2")) // $!.^name, " ", (try ("a" x "b")) // $!.^name, " ", (try (1 xx "b").raku) // $!.^name, " ", (try (1 xx NaN).elems) // $!.^name
# rakudo 2026.08: Seq (1, 1, 1).Seq Seq True 1 False (0, 1, 2).Seq 0 1 2 3 ().Seq ().Seq ((1, 2), (1, 2)).Seq ([1], [1]).Seq [[1, 9], [1]] a,a Seq True (1, 1).Seq abab "" "" (1,).Seq (Nil, Nil).Seq (1, 1).Seq ((1, 2, 3), (1, 2, 3)).Seq 111 Str aa (1, 1).Seq 20 ("Num", "Num").Seq False 3 Nil ((1, 1), (1, 1)).Seq ((1, 1), (1, 1)).Seq X::NYI X::Numeric::CannotConvert aa X::Str::Numeric X::Str::Numeric X::Numeric::CannotConvert
say (try (1 xx *).elems) // $!.^name
# rakudo 2026.08: X::Cannot::Lazy
```
rakupp 4.0.1-84: differs in two fields — `"a" x Inf` is `X::Numeric::CannotConvert` and `(1 xx NaN).elems` is 0.

## B. Quote constructs and their adverbs

### CP-19  The escapes of Q, q and qq                                     D:yes R:yes V:spec
`Q` has no escapes at all (`Q[\n]` is two characters, `Q[\]` one). `q`
knows only `\\` and a backslash before its own delimiter (`q[a\\b]` is
`a\b`, `q{a\}b}` is `a}b`, `q[a\ ]` keeps both characters, `'it\'s'`;
`q<a\<b\>c>` unescapes both brackets); a backslash before the closing
bracket escapes it, so `q[\]` is unterminated (`X::Comp::AdHoc`); `\qq[…]`
inside `q` (and inside `'…'`) interpolates its content, `\q[…]` re-quotes
it. `qq` (and `"…"`) knows `\n \t \e \0 \a \b \f \r`, `\xHH`, `\x[HH,…]`,
`\c[NAME, NAME]`, `\cNN`, `\c?`/`\c@`/`\cI` (DEL, NUL, TAB), `\oNNN`,
`\o[…]`, and a backslash before any non-word character gives that
character (`\\ \" \$ \@ \{ \} \% \& \ `); a backslash before a word
character is a compile-time `X::Backslash::UnrecognizedSequence` (`\y`,
`\C`, `\d`, `\s`; `\1` carries `.suggestion` `$0`), `\x{…}`, `\o{…}` and
`\N{…}` are `X::Obsolete`, an unknown `\c[NAME]` is `X::Comp::AdHoc`,
`\x[110000]` is a run-time `X::AdHoc`. `Q:b` turns the backslashes on
alone. `"\c[LATIN SMALL LETTER E, COMBINING ACUTE ACCENT]"` is one
character; `"\r\n"` two.
```
say Q[\n].chars, " ", q[a\tb].chars, " ", (try EVAL Q[q 'it\'s']) // $!.^name, " ", q[a\\b], " ", q{a\}b}, " ", do { my $x = 42; q[a\qq[$x]b] ~ " " ~ ((try EVAL Q[my $x = 42; 'c\qq{$x}d']) // $!.^name) ~ " " ~ Q[e\qq[$x]f] ~ " " ~ 'g\qq[$x.Str()]h' ~ " " ~ q[\q[\n]].chars }, " ", "\n\t\e\0\a\b\f\r".ords.raku, " ", "\x41\x[42]\c[LATIN CAPITAL LETTER C]\c68\o[105]\o106\x[47,48]\c[69,70]", " ", "\\\"\$\@\{\}\%\&", " ", Q:b[\n].chars, " ", (try EVAL '"\y"') // $!.^name, " ", ((try EVAL '"\1"') // $!.^name) ~ ":" ~ ((try EVAL '"\1"') // $!.suggestion), " ", (try EVAL 'q:b"\y"') // $!.^name, " ", "a\ b", " ", (try EVAL 'Q[\].chars') // $!.^name, " ", (try EVAL 'q[\\].chars') // $!.^name, " ", (try EVAL 'q[\ ].chars') // $!.^name, " ", "\cI".ord, " ", "\c?".ord, " ", "\c@".ord, " ", "\x[1F600]".chars, " ", "\x[1F600]".codes, " ", "\c[LATIN SMALL LETTER E WITH ACUTE]".chars, " ", "\c[LATIN SMALL LETTER E, COMBINING ACUTE ACCENT]".chars, " ", (try EVAL '"\c[NO SUCH NAME X]"') // $!.^name, " ", (try EVAL '"\x[110000]"') // $!.^name, " ", "\x0041", " ", "\o101\o[101]", " ", (try EVAL Q["\o{101}"]) // $!.^name, " ", (try EVAL Q["\x{41}"]) // $!.^name, " ", (try EVAL Q["\N{LATIN SMALL LETTER A}"]) // $!.^name, " ", (try EVAL Q[q 'x\'y'.chars]) // $!.^name, " ", q<a\>b>, " ", q<a\<b\>c>, " ", 'a\qq[]b', " ", "\"", " ", (try EVAL Q{'\qq[$nope]'}) // $!.^name, " ", "\r\n".ords.raku, " ", "a\\nb".chars, " ", 'a\'b', " ", 'a\\b', " ", 'a\nb'.chars, " ", 'a\tb'.chars, " ", '\\\\'.chars, " ", 'a\\'.chars, " ", "\x[41]\x[42]".chars, " ", "\c[SPACE]".ord, " ", "\c[LATIN CAPITAL LETTER A]\c[LATIN SMALL LETTER B]", " ", "\x00041".chars, " ", "\e[0m".ords.raku, " ", "\b".ord, " ", (try EVAL '"\C"') // $!.^name, " ", (try EVAL '"\d"') // $!.^name, " ", (try EVAL '"\s"') // $!.^name
# rakudo 2026.08: 2 4 it's a\b a}b a42b c42d e\qq[$x]f g42h 2 (10, 9, 27, 0, 7, 8, 12, 13).Seq ABCDEFGHEF \"$@{}%& 1 X::Backslash::UnrecognizedSequence X::Backslash::UnrecognizedSequence:$0 X::Backslash::UnrecognizedSequence a b 1 X::Comp::AdHoc 2 9 127 0 1 1 1 1 X::Comp::AdHoc X::AdHoc A AA X::Obsolete X::Obsolete X::Obsolete 3 a>b a<b>c ab " X::Undeclared (13, 10).Seq 4 a'b a\b 4 4 2 2 2 32 Ab 1 (27, 91, 48, 109).Seq 8 X::Backslash::UnrecognizedSequence X::Backslash::UnrecognizedSequence X::Backslash::UnrecognizedSequence
```
rakupp 4.0.1-84: differs in eleven fields — the space forms `q 'it\'s'` are `X::Undeclared::Symbols`, `\qq[…]` and `\q[…]` inside `q` are not recognised, `"\1"` is accepted as `1`, `Q[\]`/`q[\\]` are `X::Syntax::Confused`, an unknown `\c[NAME]` is `X::Syntax::Confused`, `\x[110000]` prints four garbage bytes, and `\x{…}`/`\o{…}`/`\N{…}` are `X::Backslash::UnrecognizedSequence` instead of `X::Obsolete`.

### CP-20  What interpolates in qq                                        D:yes R:yes V:spec
A `$` variable always interpolates; a method call on it only with
parentheses (`"$s.uc"` is `ab.uc`, `"$s.uc()"` `AB`,
`"$s.chars.Str()"` and `"$s.uc().lc"` chain — the parenthesised call and
everything before it interpolate, what follows does not: `AB.lc`); a
postcircumfix interpolates directly (`"$x.[0]"`, `"@a[0]"`, `"%h<a>"`,
`"%h{'a'}"`, `"@a[0..1]"`, `"@a[*-1]"`, `"%h<a>[0]"`, `"@a[0][0]"`) and
so does `.^name()`; `"$x [0]"` with a space does not. `@a`, `%h` and
`&f` interpolate only with a postcircumfix or a parenthesised call
(`"@a[]"`, `"%h{}"` gives `key<TAB>value` lines, `"&f()"`, `"&f.name()"`;
`"@a.elems"`, `"%h"`, `"&f"`, `docs@raku.org` stay literal). `{ … }` is a
closure (`"{ 1 + 1 }"`; `"{$x}x"` separates a name from text; an empty
closure gives Nil and warns). An identifier continues through `-` or `'`
only when a letter follows: `"$x-1"`, `"$x-4"`, `"$x!"`, `"$x'"`,
`"$x.42"`, `"$x._"` are literal after the value, `"$x-a"` and `"$x's"` are
`X::Undeclared`, `"$x._a"` is a method name without parens (literal).
`\$`, `\[`, `\.` and `\{` protect. `"$s<a>"` on a Str, `"$x.Str(){1}"` and
`"{ die 5 }"` are run-time `X::AdHoc`; `"${x}"` is `X::Obsolete`; an
undeclared `$v`, `@a[]` or `&f()` is a compile-time `X::Undeclared`
(`"@a"` alone is literal). `"$0 $/ {$0}"`, `"$<k>.uc()"`, `"$?LINE"`,
`"$*RAKU"` interpolate; an undefined scalar gives `""` plus a run-time
warning (`CX::Warn`).
```
say do { my $x = 42; my @a = 1,2; my %h = a => 9; sub f { "F" }; my $s = "ab"; ("$x", "$s.uc", "$s.uc()", "$x-1", "$x!", "$x'", "\$x", "@a", "@a[]", "@a[0]", "@a.elems", "@a[].elems", "@a.elems()", "%h", "%h<a>", "%h{}", "&f", "&f()", "{ 1 + 1 }", "$s.chars.Str()", "@a.join(', ')", "$x.[0]", "$x.Str.chars()", "@a[]-1", "%h<a>x", "docs@raku.org", "1 + 1 = {1 + 1}", "$s.uc.lc", "$s.uc().lc", "$x.^name", "$x.^name()", "$s.WHAT.^name()", "$s.substr(1)", "$s.substr: 1").join("|") }
# rakudo 2026.08: 42|ab.uc|AB|42-1|42!|42'|$x|@a|1 2|1|@a.elems|1 2.elems|2|%h|9|a	9|&f|F|2|2|1, 2|42|2|1 2-1|9x|docs@raku.org|1 + 1 = 2|ab.uc.lc|AB.lc|42.^name|Int|Str|b|ab.substr: 1
say do { my $x = 42; my @a = 1,2; my %h = a => 9; sub f { "F" }; my $s = "ab"; ("@a[0..1]", "@a[*-1]", "%h<a a>", "%h{'a'}", "%h<a>.Str()", "$x\.abs()", "{ my $q = 5 }", "&f.name()", "$s.uc.comb()", "@a[0].Str()", "$x.Str()x", "$x.Str().x", "{$x}x", "$s.chars()+1", "%h<a>[0]", "$x [0]", "$x.[0].[0]", "@a[0][0]", "$s[0]", (try { "$x.Str(){1}" } // $!.^name), "{$x.Str}", "@a[0]@a[1]", "%h<a>%h<a>", "$x$x", "$x.Str.Str", "@a[]" ~ "@a", "%h" ~ "%h<a>", "$x\[0]", "@a\[0]", "$x.\{0}", "$x,$x;$x:$x", "$x.$x", "$x.foo", "$x.42", "$x._", "$x-4").join("|") }
# rakudo 2026.08: 1 2|2|9 9|9|9|42.abs()|5|f|A B|1|42x|42.x|42x|2+1|9|42 [0]|42|1|ab|X::AdHoc|42|12|99|4242|42.Str.Str|1 2@a|%h9|42[0]|@a[0]|42.{0}|42,42;42:42|42.42|42.foo|42.42|42._|42-4
say (try EVAL 'my $x = 1; "$x-a"') // $!.^name, "|", (try EVAL q{my $x = 1; "$x's"}) // $!.^name, "|", (try EVAL q{my $s = "ab"; "$s<a>"}) // $!.^name, "|", do { "ab" ~~ /(a)/; "$0 $/ {$0}" }, "|", do { "ab" ~~ /$<k>=(b)/; "$<k> $<k>.uc()" }, "|", ("$?FILE".chars > 0), "|", "$?LINE", "|", (try EVAL q{"@a"}) // $!.^name, "|", (try EVAL q{"@a[]"}) // $!.^name, "|", (try EVAL q{"$undeclared"}) // $!.^name, "|", do { my $u; my $w = ""; { CONTROL { when CX::Warn { $w = "warned"; .resume } }; "[$u]" ~ $w } }, "|", "$*RAKU", "|", (try EVAL q{"&nosuch()"}) // $!.^name, "|", do { my @e; "[@e[]]" }, "|", do { my %e; "[%e{}]" }, "|", (try EVAL '"{ die 5 }"') // $!.^name, "|", (try EVAL q[my $x = 42; "${x}"]) // $!.^name, "|", do { sub g { "G" }; "&g() &g &g()x" }, "|", (try EVAL q{my $x = 42; "$x._a"}) // $!.^name, "|", do { my @a = 1,2; "@a[0][0]" }, "|", do { my %h = a => (1,2); "%h<a>[1]" }, "|", do { my $x = (1,2); "$x[1]" }, "|", do { my $x = {a => 9}; "$x<a>" }, "|", do { my $x = "ab"; "$x.chars.Str()" }
# rakudo 2026.08: X::Undeclared|X::Undeclared|X::AdHoc|a a a|b B|True|1|@a|X::Undeclared|X::Undeclared|[]warned|Raku|X::Undeclared::Symbols|[]|[]|X::AdHoc|X::Obsolete|G &g Gx|42._a|1|2|2|9|2
```
rakupp 4.0.1-84: differs in three fields — `"&f.name()"` is literal, `"$*RAKU"` prints `name<TAB>Raku` and `ver<TAB>6.d` on two lines, and `"${x}"` is `X::Undeclared::Symbols` instead of `X::Obsolete`.

### CP-21  Word quoting, allomorphs and quote protection                   D:yes R:yes V:spec
`<a b>` and `qw<a b>` split on whitespace without quote protection
(`<a "b c" d>` is `("a", "\"b", "c\"", "d")`); one word gives a Str, not
a list (`<a>`, `qw<a>`, `qw<42>`); `<>` is `X::Obsolete`, `< >` and
`<<>>` are `()`; the list is immutable (`push` is `X::Immutable`) and
not lazy. `<…>`, `<< … >>` and `« … »` run `val` on each word: `<42>`,
`<-1>`, `< 42 >`, `<x 42>[1]`, `<< 1 2 >>` are IntStr, `< 1/2 >` RatStr,
`4.5` RatStr, `1e3`/`Inf`/`NaN`/`1e0` NumStr, `1_0`/`0x10`/`0b1`/`+1`/`0d1`
IntStr, `.5` RatStr; `1a`, `0x1G`, `1e`, `1.`, `1_` stay Str; a
single-word `<1/2>` is a Rat, `<1+2i>` a Complex, `<1/0>` a Rat whose
`.raku` is `<1/0>`; `qw` gives plain Str. `<< >>`, `« »`, `qqww` and
`qq:ww` interpolate and protect quoted sub-strings (`<< a "b c" d >>` is
`("a", "b c", "d")`, `qqww{"$x" y}` with `$x = "p q"` is `("p q", "y")`,
`<< $x >>` splits the interpolated value into `("p", "q")`, single-quoted
sub-strings do not interpolate); `qqw`/`qq:w` interpolate then split
(`qqw{$x y}` is `("p", "q", "y")`), `qw` leaves `$x` literal; a quoted
word still goes through `val` (`q:ww:v{"1" 2}` is two IntStr).
`\ ` is not an escape in `q:w` (`q:w{a\ b}` is `("a\\", "b")`).
```
say <a>.^name, " ", qw<a>.raku, " ", <a b>.^name, " ", <a b>[1], " ", <a "b c" d>.raku, " ", << a "b c" d >>.raku, " ", qww{"a b" c}.raku, " ", qw{"a b" c}.raku, " ", <a b>.WHAT.raku, " ", <a b>.is-lazy, " ", (try <a b>.push("c")) // $!.^name, " ", <a b c>[1..2].raku, " ", (try <a b 42>.raku) // $!.^name, " ", (try < 1 2 >.raku) // $!.^name, " ", (try EVAL '<< 1 2 >>.raku') // $!.^name, " ", (try qw< 1 2 >.raku) // $!.^name, " ", (try <1/2>.^name) // $!.^name, " ", (try < 1/2 >.^name) // $!.^name, " ", (try <1+2i>.^name) // $!.^name, " ", (try <42>.^name) // $!.^name, " ", (try <42>.WHAT.raku) // $!.^name, " ", (try < 42 >.WHAT.raku) // $!.^name, " ", (try EVAL '<<42>>.WHAT.raku') // $!.^name, " ", (try <x 42>[1].WHAT.raku) // $!.^name, " ", (try <1/3>.raku) // $!.^name, " ", (try <42>.raku) // $!.^name, " ", (try <-1>.raku) // $!.^name, " ", (try < -1 >.raku) // $!.^name, " ", (try qw<42>.raku) // $!.^name, " ", (try <1 2>.sum) // $!.^name, " ", (try EVAL '<<1 2>>.sum') // $!.^name, " ", (try qw<1 2>.map(*.^name).raku) // $!.^name
# rakudo 2026.08: Str "a" List b ("a", "\"b", "c\"", "d") ("a", "b c", "d") ("a b", "c") ("\"a", "b\"", "c") List False X::Immutable ("b", "c") ("a", "b", IntStr.new(42, "42")) (IntStr.new(1, "1"), IntStr.new(2, "2")) (IntStr.new(1, "1"), IntStr.new(2, "2")) ("1", "2") Rat RatStr Complex IntStr IntStr IntStr IntStr IntStr <1/3> IntStr.new(42, "42") IntStr.new(-1, "-1") IntStr.new(-1, "-1") "42" 3 3 ("Str", "Str").Seq
say do { my $x = "p q"; (qqw{$x y}.raku, qqww{"$x" y}.raku, qq:w{$x y}.raku, qq:ww{"$x" y}.raku, << $x "$x" y >>.raku, « $x y ».raku, qw{$x y}.raku, qq:w:v{$x 3}.raku, q:w:v{1 b}.raku, q:ww:v{"1" 2}.raku, Q:w{a\ b}.raku, q:w{a\ b}.raku, <a\ b>.raku, Q:ww{'a b' c}.raku, << $x >>.raku, qqww{$x}.raku, << 'a b' "c d" e >>.raku, qqww{'$x' "$x"}.raku).join(" ") }
# rakudo 2026.08: ("p", "q", "y") ("p q", "y") ("p", "q", "y") ("p q", "y") ("p", "q", "p q", "y") ("p", "q", "y") ("\$x", "y") ("p", "q", IntStr.new(3, "3")) (IntStr.new(1, "1"), "b") (IntStr.new(1, "1"), IntStr.new(2, "2")) ("a\\", "b") ("a\\", "b") ("a\\", "b") ("a b", "c") ("p", "q") ("p", "q") ("a b", "c d", "e") ("\$x", "p q")
say (try EVAL '<>') // $!.^name, " ", (try EVAL '< >.raku') // $!.^name, " ", (try EVAL '<<>>.raku') // $!.^name, " ", <42 4.5 1e3 0x10 0b1 -1 +1 1_0 Inf NaN 1/2 1e0 0d1>.map(*.^name).raku, " ", <1a 2b>.map(*.^name).raku, " ", <0x1G>.^name, " ", <1e>.^name, " ", <1.>.^name, " ", <.5>.^name, " ", <1_>.^name, " ", <1/0>.^name, " ", (try <1/0>.raku) // $!.^name
# rakudo 2026.08: X::Obsolete () () ("IntStr", "RatStr", "NumStr", "IntStr", "IntStr", "IntStr", "IntStr", "IntStr", "NumStr", "NumStr", "RatStr", "NumStr", "IntStr").Seq ("Str", "Str").Seq Str Str Str RatStr Str Rat <1/0>
```
rakupp 4.0.1-84: differs in sixteen fields — `< 1/2 >` is a Rat, `<<42>>` does not parse, `1_0`/`Inf`/`NaN` stay Str and `1/2` is a Rat in `<…>`, an interpolated value is not re-split (`qqw{$x y}` is `("p q", "y")`), `qq:ww`, `q:ww:v` and `Q:ww` return one Str, and `<a\ b>` is `"ab"`.

### CP-22  `:x`: run the string, return the output                         D:yes R:yes V:bug
`qx`, `q:x`, `Q:x` and `qqx` run the text in the shell and return its
standard output as a Str (`"hi\n"`, chompable, `.lines`, `.words`);
only `qqx` interpolates (`qx{echo hi $v}` leaves `$v` to the shell).
A failing or missing command gives `""` and no exception; the exit
status is not reported. With `:v` after `:x` the output goes through
`val` (`q:x:v{echo 42}` is `IntStr.new(42, "42\n")`); the order of
adverbs is the order of application: `q:x:w` splits the output
(`("a", "b")`), `q:w:x` splits the source first and runs the joined words
(`"a b\n"`), `q:x:ww` splits the output without quote protection. The
bug: with brace delimiters a nested `{…}` is passed to the shell wrongly —
`qx{echo {1+1}}` prints `{}` and the shell complains about a command
`1+1` on standard error, while `qx[echo {1+1}]` prints `{1+1}`; the
documented literal reading is the second one.
```
say qx{echo hi}.raku, " ", qx{echo hi}.^name, " ", do { my $v = "there"; qqx{echo hi $v}.raku ~ " " ~ qx{echo hi $v}.raku }, " ", qqx[echo {1+1}].raku, " ", q:x{echo q}.raku, " ", Q:x{echo Q}.raku, " ", qx{nonexistent-cmd-zz-42 2>/dev/null}.raku, " ", qx{echo a b c}.words.raku, " ", q:x:w{echo a b}.raku, " ", qx{exit 3}.raku, " ", q:w:x{echo a b}.raku, " ", qqx{printf '%s' "x"}.raku, " ", do { my $o = qx{echo out}; $o.chomp }, " ", qx{echo one; echo two}.lines.raku, " ", qx{printf 'a\nb'}.raku, " ", (qx{echo $HOME}.chars > 1), " ", (qqx{echo \$HOME}.chars > 1), " ", (qx{echo hi} ~~ Str), " ", qx{echo hi}.chomp.raku, " ", q:x:v{echo 42}.^name, " ", qx{echo ' a  b '}.raku, " ", q:x:ww{echo 'a b' c}.raku, " ", qqx[echo {"z" x 2}].raku, " ", qx[echo {1+1}].raku, " ", do { my $p = run($*EXECUTABLE, '-e', 'print qx{echo {1+1}}.raku', :out, :err); $p.out.slurp(:close) ~ "/" ~ ($p.err.slurp(:close).chars > 0) }, " ", qx{echo "a" "b"}.raku, " ", q:x{true}.raku, " ", qx{echo -n x}.raku, " ", qx{echo hi}.^name, " ", (qx{echo 42} + 1), " ", qx{echo 42}.^name, " ", q:x:v{echo 42}.raku, " ", qx{printf ''}.raku, " ", qx{printf ''}.^name, " ", (qx{exit 1} eq ""), " ", qx{echo a; exit 2}.raku
# rakudo 2026.08: "hi\n" Str "hi there\n" "hi\n" "2\n" "q\n" "Q\n" "" ("a", "b", "c").Seq ("a", "b") "" "a b\n" "x" out ("one", "two").Seq "a\nb" True True True "hi" IntStr " a  b \n" ("a", "b", "c") "zz\n" "\{1+1}\n" "\{}\n"/True "a b\n" "" "-n x\n" Str 43 Str IntStr.new(42, "42\n") "" Str True "a\n"
```
rakupp 4.0.1-84: differs in nine fields — `q:x`, `Q:x`, `q:x:v`, `q:x:w`, `q:w:x` and `q:x:ww` do not run the command (they return the source, or its words), `q:x{true}` returns `"true"`, `:v` is not applied, and the brace-delimited `qx{echo {1+1}}` correctly prints `{1+1}` where Rakudo has the bug.

### CP-23  Combining quote adverbs                                         D:partial R:partial V:spec
`q:s` interpolates scalars only, `q:a` arrays, `q:h` hashes, `q:f`
`&f()` calls, `q:c` closures, `q:b` backslashes; the long names
`:scalar :array :hash :function :closure :backslash :single :double
:words :quotewords :val :exec :heredoc` are the same adverbs.
`Q:s:a:h:f:c:b` equals `qq`; `qq:!s` turns scalars off and keeps the
rest, `qq:!c` turns closures off, `qq:!b` keeps a `\n` literal;
`Q:s:!s` ends with scalars off, `qq:c:!c` off and `qq:!c:c` on (the last
setting wins). A postprocessor adverb cannot be turned off again
(`q:w:!w`, `qq:x:!x` still split and run); `:x(0)`, `:s(1)` and `:1x`
are the boolean forms. `q:q`, `qq:qq`, `q:qq`, `qq:q`, `q:!q` are
compile-time `X::Comp::AdHoc` ("Too late for", "Cannot negate"), as are
an unknown adverb (`q:zz`, `q:o`, `Q:o`, `q:p`, `q:path`, `q:format`)
and `q:!to`; `q:to{a}`/`Q:heredoc{a}` with the body on the same line are
`X::AdHoc`; `q:regex{a}` is `X::AdHoc`; `q:cc{a}` is `a`. With brace
delimiters a nested `{…}` is never a closure, even under `:c`
(`qq{a{1+1}b}` is `a{1+1}b`, `Q:c{$x {$x}}` is literal, `qq[a{1+1}b]` is
`a2b`); `qq{a\{1+1\}b}` and `Q:b{\{}` unescape the brace. `:v` gives
IntStr/RatStr for a numeric string and Str otherwise (`Q:v{1 2}` is Str,
`Q:w:v{1 2}` two IntStr).
```
say do { my $x = 1; my @a = 2,3; my %h = k => 4; sub f { "F" }; (q:s[$x @a[] %h<k> &f() {1+1} \n], qq:!s[$x @a[] %h<k> &f() {1+1}], qq:!c[$x {1+1}], qq:!a{$x @a[]}, qq:!h{%h<k>}, qq:!f{&f()}, qq:!b{\n$x}, Q:c[{1+1} $x], Q:s:a:h:f:c:b[$x @a[] %h<k> &f() {1+1} \t].chars, q:a{$x @a[]}, q:h{%h<k> $x}, q:f{&f() $x}, q:b{a\nb}.chars, Q:q{a\'b}, Q:qq{$x\n}.chars, q :w /a b/.raku, q:c[{$x + 1}], Q:v{42}.^name, q:v{42}.^name, qq:v{4$x}.^name, Q:s{$x\n}.chars, Q:b{$x\n}.chars, qq:s:!b{$x\n}.chars, q:s:b{$x\n}.chars, Q:a{@a[] $x}, Q:h{%h<k> $x}, Q:f{&f() $x}, Q:c{$x {$x}}, Q:c[$x {$x}], Q:q:s{$x}, Q:s:q{$x}, Q:x:w{echo a b}.raku, q:w:x{echo a b}.raku, qq:c:!c[{1}], qq:!c:c[{1}], Q:single{$x\n}.chars, Q:double{$x\n}.chars, q:backslash{\n}.chars, Q:scalar{$x @a[]}, Q:array{$x @a[]}, Q:hash{%h<k>}, Q:function{&f()}, Q:closure[{1}], q:words{a b}.raku, q:quotewords{"a b" c}.raku, q:val{1}.^name, Q:exec{echo e}.raku, qq{a{1+1}b}, qq[a{1+1}b], qq{a\{1+1\}b}, "a{1+1}b", qq:c{a{1+1}b}, Q:c:s{$x {$x}}, Q:s:c[$x {$x}], Q:b{\{}, qq{\{}, Q:v{1}.^name, Q:v{1.5}.^name, Q:v{a}.^name, Q:v{1 2}.^name, Q:w:v{1 2}.map(*.^name).raku).join("|") }, "|", (try EVAL 'q:q{a}') // $!.^name, "|", (try EVAL 'qq:qq{a}') // $!.^name, "|", (try EVAL 'q:qq{a}') // $!.^name, "|", (try EVAL 'qq:q{a}') // $!.^name, "|", (try EVAL 'q:!q{a}') // $!.^name, "|", (try EVAL 'q:o{a}') // $!.^name, "|", (try EVAL 'Q:o{a}') // $!.^name, "|", (try EVAL 'q:zz{a}') // $!.^name, "|", (try EVAL 'q:!to{a}') // $!.^name, "|", (try EVAL 'Q:s:!s{$x}') // $!.^name, "|", (try EVAL 'q:regex{a}') // $!.^name, "|", (try EVAL 'q:w:!w{a b}.raku') // $!.^name, "|", (try EVAL 'q:x(0){echo a}.raku') // $!.^name, "|", (try EVAL 'q:s(1){$x}') // $!.^name, "|", (try EVAL 'q:v:v{1}.^name') // $!.^name, "|", (try EVAL 'Q:c:x[echo {1+1}].raku') // $!.^name, "|", (try EVAL 'qq:x:!x{echo a}.raku') // $!.^name, "|", (try EVAL 'q:1x{echo n}') // $!.^name, "|", (try EVAL 'q:cc{a}') // $!.^name, "|", (try EVAL 'q:to{a}') // $!.^name, "|", (try EVAL 'Q:heredoc{a}') // $!.^name, "|", (try EVAL 'q:p{a}') // $!.^name, "|", (try EVAL 'q:path{a}') // $!.^name, "|", (try EVAL 'q:format{a}') // $!.^name
# rakudo 2026.08: 1 @a[] %h<k> &f() {1+1} \n|$x 2 3 4 F 2|1 {1+1}|1 @a[]|%h<k>|&f()|\n1|2 $x|13|$x 2 3|4 $x|F $x|3|a\'b|2|("a", "b")|2|IntStr|IntStr|IntStr|3|3|3|2|2 3 $x|4 $x|F $x|$x {$x}|$x 1|1|1|("a", "b")|"a b\n"|{1}|1|4|2|1|1 @a[]|$x 2 3|4|F|1|("a", "b")|("a b", "c")|IntStr|"e\n"|a{1+1}b|a2b|a{1+1}b|a2b|a{1+1}b|1 {1}|1 1|{|{|IntStr|RatStr|Str|Str|("IntStr", "IntStr").Seq|X::Comp::AdHoc|X::Comp::AdHoc|X::Comp::AdHoc|X::Comp::AdHoc|X::Comp::AdHoc|X::Comp::AdHoc|X::Comp::AdHoc|X::Comp::AdHoc|X::Comp::AdHoc|$x|X::AdHoc|("a", "b")|"echo a"|X::Undeclared|IntStr|"2\n"|"a\n"|n||a|X::AdHoc|X::AdHoc|X::Comp::AdHoc|X::Comp::AdHoc|X::Comp::AdHoc
```
rakupp 4.0.1-84: differs in 31 of the 85 fields — `:v` never makes an allomorph, `Q:c{$x {$x}}` interpolates the nested closure, `:x` in a combination does not run, `qq:!c:c` stays off, `Q:double{$x\n}` counts 4, `q:quotewords` returns one Str, the "Too late for"/negation errors and `q:zz`/`q:!to`/`q:p`/`q:path` are accepted, `:o` is `X::Syntax::Confused`, `q:regex{a}` is `a`, and `q:s(1){$x}` does not interpolate.

### CP-24  Delimiters                                                     D:yes R:yes V:spec
Any paired bracket nests to its own depth (`Q{ { } }`, `q{a{b}c}`,
`q<a<b>c>`, `q<<a<<b>>c>>`, `q«a«b»c»`, `q{a⟨b⟩c}` keeps the inner
pair literal, `q{a{b}` is unterminated `X::Comp::AdHoc`, `q<a>b>` is
`X::Syntax::Confused`); a doubled opener is one delimiter that needs the
doubled closer (`Q{{a}}` and `Q[[a]]` are `a`, `Q｢｢a｣b｣｣` is `a｣b`,
`Q{{a}b}` is `X::Comp::AdHoc`, `Q:c{{1+1} a}` likewise). An unpaired
character is its own closer (`q/a\/b/`, `q!a!`, `q.a.`, `q,a,`, `q=a=`,
`q$a$`, `q@a@`, `q%a%`, `q&a&`, `q^a^`, `q*a*`, `q+a+`, `q~a~`, `q?a?`,
`q;a;`, `q\a\`, `q"a"`, `` q`a` ``, `q😀a😀`; `q|a|b|` and `Q/a\/b/`
are unterminated). Not allowed directly after `q`/`Q`: an identifier
character (`q'a'`, `Q'a'` are `X::Syntax::Confused`, `q1a1`, `q_a_` and
`q(a)`/`Q(a)` are calls, `X::Undeclared::Symbols`), `#`
(`X::Comp::AdHoc`), `:` (an adverb), `-` (joins the name), a bare space
(`q a a`); a space then a non-identifier delimiter works (`Q (a)`,
`q 'x'`, `q {a}`, `Q :w [a b]`, `q ｢a｣`). Curly quotes are paired
delimiters (`‘a’`, `“a”`, `Q ‘a’`, `q‘a’`), `q<>` is `""`, `q{ }` a
space, `q<<a b>>` one Str `a b` (not words), text after the closer is
`X::Syntax::Confused` (`q{a}.chars` is 1).
```
say Q{ { } }.raku, " ", q{a{b}c}, " ", q/a\/b/, " ", q!a!, " ", q<<a b>>.raku, " ", q<a b>.raku, " ", qw<a b>.raku, " ", (try EVAL "Q'a'") // $!.^name, " ", (try EVAL "Q(a)") // $!.^name, " ", (try EVAL "Q (a)") // $!.^name, " ", (try EVAL "q 'x'") // $!.^name, " ", qq«a», " ", q{ }.raku, " ", q{}.raku, " ", q<a<b>c>, " ", (try EVAL "q#a#") // $!.^name, " ", q[a\]b], " ", (try EVAL 'Q[a\]b]') // $!.^name, " ", Q[a\\b], " ", q[a\\b], " ", qq[a\\b], " ", (try EVAL "Q<a\\>") // $!.^name, " ", (try EVAL 'q<a<b>') // $!.^name, " ", Q{{a}}, " ", q{{a}}.chars, " ", qq{{1+1}}, " ", Q{{}}, " ", q{a\}\{b}, " ", "a{'}'}b", " ", q:w{a\ b\ c}.elems, " ", (try EVAL 'q a a') // $!.^name, " ", (try EVAL 'q:w[a b]') // $!.^name, " ", (try EVAL 'Q :w [a b]') // $!.^name, " ", (try EVAL 'q<a>b>') // $!.^name, " ", (try EVAL 'q<<a<<b>>c>>') // $!.^name, " ", (try EVAL 'q{a\{b}') // $!.^name, " ", (try EVAL 'q{a{b}') // $!.^name, " ", (try EVAL 'Q{a\}b}') // $!.^name, " ", (try EVAL 'q|a|b|') // $!.^name, " ", (try EVAL 'q/a\/b/') // $!.^name, " ", (try EVAL 'Q/a\/b/') // $!.^name, " ", (try EVAL "q'a'") // $!.^name, " ", (try EVAL 'qq"a"') // $!.^name, " ", (try EVAL 'q<>') // $!.^name, " ", (try EVAL 'q::<a>') // $!.^name, " ", (try EVAL 'q {a}') // $!.^name, " ", (try EVAL 'q{a} {b}') // $!.^name, " ", (try EVAL 'q(a)') // $!.^name, " ", (try EVAL 'q{a}.chars') // $!.^name, " ", (try EVAL 'q{a\nb}.chars') // $!.^name, " ", (try EVAL 'Q:c{{1+1} a}') // $!.^name, " ", (try EVAL 'Q{{a}}') // $!.^name, " ", (try EVAL 'Q{{{a}}}') // $!.^name, " ", (try EVAL 'Q{{a}b}') // $!.^name, " ", (try EVAL 'q{{a}}b') // $!.^name, " ", (try EVAL 'Q[[a]]') // $!.^name, " ", (try EVAL 'Q[[a] [b]]') // $!.^name, " ", (try EVAL 'q<<a>>') // $!.^name, " ", (try EVAL 'q<<a>b>>') // $!.^name, " ", (try EVAL 'q<<a b>>.elems') // $!.^name, " ", (try EVAL 'Q<<a b>>.elems') // $!.^name, " ", (try EVAL 'qq<<a b>>.elems') // $!.^name, " ", (try EVAL 'q.a.') // $!.^name, " ", (try EVAL 'q,a,') // $!.^name, " ", (try EVAL 'q:a:') // $!.^name, " ", (try EVAL 'q=a=') // $!.^name, " ", (try EVAL 'q a a') // $!.^name, " ", (try EVAL 'q$a$') // $!.^name, " ", (try EVAL 'q@a@') // $!.^name, " ", (try EVAL 'q%a%') // $!.^name, " ", (try EVAL 'q&a&') // $!.^name, " ", (try EVAL 'q^a^') // $!.^name, " ", (try EVAL 'q*a*') // $!.^name, " ", (try EVAL 'q-a-') // $!.^name, " ", (try EVAL 'q+a+') // $!.^name, " ", (try EVAL 'q~a~') // $!.^name, " ", (try EVAL 'q?a?') // $!.^name, " ", (try EVAL 'q;a;') // $!.^name, " ", (try EVAL "q\\a\\") // $!.^name, " ", (try EVAL 'q"a"') // $!.^name, " ", (try EVAL 'q`a`') // $!.^name, " ", (try EVAL 'q1a1') // $!.^name, " ", (try EVAL 'q_a_') // $!.^name, " ", (try EVAL 'q\'a\'') // $!.^name, " ", (try EVAL 'q<a\<b>') // $!.^name
# rakudo 2026.08: " \{ } " a{b}c a/b a "a b" "a b" ("a", "b") X::Syntax::Confused X::Undeclared::Symbols a x a " " "" a<b>c X::Comp::AdHoc a]b X::Syntax::Confused a\\b a\b a\b a\ X::Comp::AdHoc a 1 1+1  a}{b a}b 3 X::Comp::AdHoc (a b) (a b) X::Syntax::Confused a<<b>>c a{b X::Comp::AdHoc X::Syntax::Confused X::Syntax::Confused a/b X::Syntax::Confused X::Syntax::Confused a  X::Undeclared::Symbols a X::Comp::AdHoc X::Undeclared::Symbols 1 4 X::Comp::AdHoc a a X::Comp::AdHoc X::Syntax::Confused a a] [b a a>b 1 1 1 a a X::Comp::AdHoc a X::Comp::AdHoc a a a a a a X::Comp::AdHoc a a a a a a a X::Undeclared::Symbols X::Undeclared::Symbols X::Syntax::Confused a<b
say (try EVAL 'Q ‘a’') // $!.^name, " ", (try EVAL 'Q“a”') // $!.^name, " ", (try EVAL "Q‘a’") // $!.^name, " ", (try EVAL "‘a’") // $!.^name, " ", (try EVAL "“a”") // $!.^name, " ", (try EVAL "q‘a’") // $!.^name, " ", (try EVAL 'Q｢｢a｣｣') // $!.^name, " ", (try EVAL 'Q｢｢a｣b｣｣') // $!.^name, " ", (try EVAL 'q«a b».elems') // $!.^name, " ", (try EVAL 'q«a«b»c»') // $!.^name, " ", (try EVAL "Q«a»") // $!.^name, " ", (try EVAL "q「a」") // $!.^name, " ", (try EVAL "q⟨a⟩") // $!.^name, " ", (try EVAL 'q{a⟨b⟩c}') // $!.^name, " ", (try EVAL "q😀a😀") // $!.^name, " ", (try EVAL "q「a」b") // $!.^name, " ", (try EVAL 'q ｢a｣') // $!.^name, " ", (try EVAL 'q{a｢b｣c}') // $!.^name
# rakudo 2026.08: a a a a a a a a｣b 1 a«b»c a a a a⟨b⟩c a X::Syntax::Confused a a｢b｣c
```
rakupp 4.0.1-84: differs in 55 of the 103 fields — `q'a'`/`Q'a'` are accepted, the space forms `Q (a)`, `q 'x'`, `q ‘a’`, `q ｢a｣` and `q a a` are `X::Undeclared::Symbols`, most non-bracket delimiters (`. , = $ @ % & ^ * ? ; \ `` ` `` `+` and `q\a\`) are `X::Syntax::Confused` or `X::Comp::AdHoc`, `Q` unescapes backslashes (`Q[a\\b]` is `a\b`, `Q[a\]b]` and `Q{a\}b}` keep the backslash and parse, `Q<a\>` fails), `q#a#` is `X::Undeclared::Symbols`, `q{a{b}` and `Q{{a}b}` are `X::Syntax::Confused`, `q{a} {b}` is `X::Syntax::Confused`, `q::<a>` is `X::Placeholder::Mainline`, and `Q“a”`, `Q‘a’`, `q‘a’`, `Q｢｢a｣｣`, `q«a«b»c»` fail.

### CP-25  Heredocs: the terminator's indentation is removed              D:yes R:yes V:spec
The body starts on the next line and ends before a line holding only
the terminator, optionally indented and followed by spaces; the amount of
that indentation is removed from every body line, counting a tab as
reaching the next multiple of 8 columns (`$?TABSTOP`) — a tab-indented
terminator removes eight spaces or one tab, an eight-space one removes a
tab, a two-space one turns a following tab into six spaces, and a
partially consumed tab is expanded. A line with less indentation is
left as is, a blank line stays blank, a whitespace-only line keeps what is
beyond the indentation. The final newline is kept; an empty body is `""`;
`\r\n` in the body or the source becomes `\n`. A backslash before the
newline stays in `q:to` and is a line-continuation-free newline in
`qq:to`. `q:to` is a Str; `qq:to` interpolates with the dedent applied to
the template only (an interpolated `"  1"` keeps its spaces, a
multi-line value is inserted as is). Several heredocs on one line follow
each other in order.
```
say EVAL("my \$x = 42; my \$s = qq:to/END/;\n    a \$x\n      b\n    END\n\$s").raku, " ", EVAL("q:to/END/;\n  a\n b\nEND\n").raku, " ", EVAL("q:to/END/;\nEND\n").raku, " ", EVAL("q:to/END/;\n\nEND\n").raku, " ", EVAL("q:to/END/;\n    a\n  END\n").raku, " ", EVAL("Q:to/END/;\n  \\n \$x\n  END\n").raku, " ", EVAL("my \$s = q:to<END>;\n\ta\n\t\tb\n\tEND\n\$s").raku, " ", EVAL("my \$s = q:to<END>;\n\ta\n        b\n\tEND\n\$s").raku, " ", EVAL("my \$s = q:to<END>;\n        a\n\tb\n        END\n\$s").raku, " ", EVAL("q:to/A/, q:to/B/;\n  a1\n  A\n    b1\n    B\n").raku, " ", EVAL("my \$s = q:to/END/;\n  a\n\n  b\n  END\n\$s").raku, " ", EVAL("my \$s = q:to/END/;\n  a\n   b\n  END\n\$s").raku, " ", EVAL("my \$s = q:to<END>;\n  \ta\n  END\n\$s").raku, " ", EVAL("my \$s = q:to<END>;\n\t  a\n\tEND\n\$s").raku, " ", EVAL("my \$s = q:to<END>;\n    \ta\n\tEND\n\$s").raku, " ", EVAL("my \$s = q:to<END>;\n\t\ta\n        END\n\$s").raku, " ", EVAL("my \$s = q:to/END/;\n  a\\tb\n  END\n\$s").raku, " ", EVAL("my \$s = q:to/END/;\n  a\n  END\n\$s").^name, " ", EVAL("my \$s = q:to/END/;\n  a \\\n  b\n  END\n\$s").raku, " ", EVAL("my \$s = q:to/END/;\n    a\n    \\\n    END\n\$s").raku, " ", EVAL("my \$s = q:to/END/;\n\n\n  END\n\$s").raku, " ", EVAL("my \$s = q:to/END/;\n  a\n\n  END\n\$s").raku, " ", EVAL("my \$s = q:to/END/;\n     a\n   b\n  END\n\$s").raku, " ", EVAL("my \$s = q:to/END/;\n  a\n   \n  END\n\$s").raku, " ", EVAL("my \$s = q:to/END/;\n  END\n\$s").raku, " ", EVAL("my \$s = q:to/END/;\n    \n  END\n\$s").raku, " ", EVAL("my \$s = q:to/END/;\n  a\r\n  END\n\$s").raku, " ", EVAL("my \$s = q:to/END/;\r\n  a\r\n  END\r\n\$s").raku
# rakudo 2026.08: "a 42\n  b\n" "  a\n b\n" "" "\n" "  a\n" "\\n \$x\n" "a\n\tb\n" "a\nb\n" "a\nb\n" ("a1\n", "b1\n") "a\n\nb\n" "a\n b\n" "      a\n" "  a\n" "a\n" "\ta\n" "a\\tb\n" Str "a \\\nb\n" "a\n\\\n" "\n\n" "a\n\n" "   a\n b\n" "a\n \n" "" "  \n" "a\r\n" "a\r\n"
```
rakupp 4.0.1-84: differs in five fields — a tab in the indentation counts as one column, so a tab-terminated heredoc removes one tab and not eight spaces, and a partially consumed tab is not expanded.

### CP-26  Heredoc rules: what may follow on the line, errors, adverbs    D:partial R:partial V:spec
The rest of the line after the quote is ordinary code that runs with the
body (`qq:to/END/.chars;`, `my @l = q:to/END/.lines;`,
`my $s = q:to/END/.uc;`, `f q:to/END/;`, `my $s = q:to/END/; $s.chars;`,
`(q:to/A/, 2)`, `f(q:to/A/, q:to/B/)`, `{ q:to/END/ }()`), and the next
statement may begin on the line after the terminator (`$t ~ q:to/E2/;`).
A missing `;` before the newline is `X::Syntax::Confused`; a missing
terminator `X::Comp::AdHoc`; the terminator at end of file without a
newline is fine; it is matched exactly and case-sensitively (`ENDX`,
`end` are body), may contain spaces (`q:to/E N/`), and text after it on
its line is `X::Comp::AdHoc`; a `$x` terminator is `X::Comp::AdHoc`,
`q:to'END'` is `X::Comp::Group`, `q:to{END}`, `q :to /END/`, `Q:to`
work. Adverbs combine in order: `q:to:w` splits the body, `qq:to:ww`
protects quotes, `q:w:to` (words first) also splits, `q:to:v` does not
`val` the body (a Str), `qq:to:!s` keeps `$x`, `q:to:b` decodes `\t`,
`Q:to:s` interpolates scalars, `q:to:x` runs it, `q:to:c` and `Q:to:c`
interpolate closures (`\qq[…]` works in both `q:to` and `qq:to`);
`q:to:to` is `X::AdHoc`, `q:to:q` is accepted.
```
say EVAL("qq:to/END/.chars;\n  x\n  END\n"), " ", (try EVAL("q:to/END/;\n  a\n")) // $!.^name, " ", (try EVAL("q:to/END/;\n  a\n  END")) // $!.^name, " ", EVAL("my \@l = q:to/END/.lines;\n  a\n  b\n  END\n\@l.raku"), " ", EVAL("q:to/END/;\n  a\n  END\n").^name, " ", EVAL("sub f(\$s) \{ \$s.chars }; f q:to/END/;\n  ab\n  END\n"), " ", EVAL("my \$t = q:to/END/;\n  a\n  END\n#comment\n\$t").raku, " ", EVAL("my \$t = q:to/END/;\n  a\n  END\n\$t ~ q:to/E2/;\n  b\n  E2\n").raku, " ", EVAL("q:to/END/;\n  a\n  END \n").raku, " ", EVAL("q:to/END/;\n  a\n  ENDX\n  END\n").raku, " ", EVAL("q:to/END/;\n  a\n  end\n  END\n").raku, " ", EVAL("q:to/E N/;\n  a\n  E N\n").raku, " ", (try EVAL("q:to/\$x/;\n  a\n  END\n")) // $!.^name, " ", ((try EVAL("q:to'END';\n  a\n  END\n")) // $!.^name), " ", EVAL("q:to\{END};\n  a\n  END\n").raku, " ", EVAL("q :to /END/;\n  a\n  END\n").raku, " ", EVAL("Q:to/END/;\n  a\n  END\n").raku, " ", (try EVAL("q:to/END/.chars\n  a\n  END\n")) // $!.^name, " ", EVAL("my \$s = q:to/END/; my \$y = 1;\n  a\n  END\n\$s ~ \$y").raku, " ", EVAL("my \$s = (q:to/END/);\n  a\n  END\n\$s").raku, " ", EVAL("my \@x = (q:to/A/, 2);\n  a\n  A\n\@x.raku"), " ", (try EVAL("my \$s = q:to/END/; \$s.chars;\n  ab\n  END\n")) // $!.^name, " ", EVAL("sub f(\$a, \$b) \{ \$a ~ \$b }; f(q:to/A/, q:to/B/);\n  a\n  A\n  b\n  B\n").raku, " ", (try EVAL("my \$s = q:to/END/\n  a\n  END\n\$s")) // $!.^name, " ", EVAL("my \$s = q:to/END/.uc;\n  a\n  END\n\$s").raku, " ", EVAL("my \$s = q:to/END/ ~ 'x';\n  a\n  END\n\$s").raku, " ", EVAL("my \$s = q:to/END/;\n  a\n  END  \n\$s").raku, " ", (try EVAL("my \$s = q:to/END/;\n  a\n END X\n")) // $!.^name, " ", EVAL("\{ q:to/END/ }();\n  in\n  END\n").raku
# rakudo 2026.08: 2 X::Comp::AdHoc a| ["a", "b"] Str 3 "a\n" "a\nb\n" "a\n" "a\nENDX\n" "a\nend\n" "a\n" X::Comp::AdHoc X::Comp::Group "a\n" "a\n" "a\n" 2 "a\n1" "a\n" ["a\n", 2] 3 "a\nb\n" X::Syntax::Confused "A\n" "a\nx" "a\n" X::Comp::AdHoc "in\n"
say (try EVAL("my \$s = qq:to/END/;\n  a\\\n  b\n  END\n\$s").raku) // $!.^name, " ", (try EVAL("my \$x = 'X'; my \$s = qq:to/END/;\n    \$x\n     \$x y\n    z\n    END\n\$s").raku) // $!.^name, " ", (try EVAL("my \$m = \"1\\n  2\"; my \$s = qq:to/END/;\n    \$m\n    END\n\$s").raku) // $!.^name, " ", (try EVAL("my \$s = qq:to/END/;\n    \{ \"1\\n2\" }\n    END\n\$s").raku) // $!.^name, " ", (try EVAL("my \$s = qq:to/END/;\n  a\\tb\n  END\n\$s").raku) // $!.^name, " ", (try EVAL("my \$m = \"1\\n2\"; my \$s = qq:to/END/;\n    \$m\n    \$m\n    END\n\$s").raku) // $!.^name, " ", (try EVAL("my \$m = \"  1\"; my \$s = qq:to/END/;\n    \$m\n    END\n\$s").raku) // $!.^name, " ", (try EVAL("my \$s = qq:to/END/;\n    \$*RAKU.name() x\n    END\n\$s").chars) // $!.^name, " ", (try EVAL("my \$s = qq:to/END/;\n    a\n    \\\n    END\n\$s").raku) // $!.^name, " ", (try EVAL("my \$s = qq:to/END/;\n  a\\\n  END\n\$s").raku) // $!.^name, " ", (try EVAL("qq:to/END/;\n  \\qq[\{1+2}]\n  END\n").raku) // $!.^name, " ", (try EVAL("q:to/END/;\n  \\qq[\{1+2}]\n  END\n").raku) // $!.^name
# rakudo 2026.08: "a\nb\n" "X\n X y\nz\n" "1\n  2\n" "1\n2\n" "a\tb\n" "1\n2\n1\n2\n" "  1\n" 7 "a\n\n" "a\n" "3\n" "3\n"
say (try EVAL("q:to:w/END/;\n  a b\n  c\n  END\n").raku) // $!.^name, " ", (try EVAL("q:heredoc:c/END/;\n  \{1+1}\n  END\n").raku) // $!.^name, " ", (try EVAL("qq:to:ww/END/;\n  \"a b\" c\n  END\n").raku) // $!.^name, " ", (try EVAL("q:w:to/END/;\n  a b\n  END\n").raku) // $!.^name, " ", (try EVAL("q:to:v/END/;\n  42\n  END\n").^name) // $!.^name, " ", (try EVAL("qq:to:!s/END/;\n  \$x\n  END\n").raku) // $!.^name, " ", (try EVAL("q:to:b/END/;\n  a\\tb\n  END\n").raku) // $!.^name, " ", (try EVAL("Q:to:s/END/;\n  \$*RAKU.name()\n  END\n").raku) // $!.^name, " ", (try EVAL("q:to:x/END/;\n  echo hi\n  END\n").raku) // $!.^name, " ", (try EVAL("q:to:to/END/;\n  a\n  END\n")) // $!.^name, " ", (try EVAL("q:to:q/END/;\n  a\n  END\n")) // $!.^name, " ", (try EVAL("q:to:c/END/;\n  \{1+1}\n  END\n").raku) // $!.^name, " ", (try EVAL("Q:to:c/END/;\n  [\{1+1}]\n  END\n").raku) // $!.^name
# rakudo 2026.08: ("a", "b", "c") "2\n" ("a b", "c") ("a", "b") Str "\$x\n" "a\tb\n" "Raku\n" "hi\n" X::AdHoc a| "2\n" "[2]\n"
```
rakupp 4.0.1-84: differs in eleven fields — the errors are `X::Syntax::Confused` throughout, a missing `;` and `q:to'END'` are accepted, `\qq[…]` inside a heredoc is `X::Backslash::UnrecognizedSequence` (`qq:to`) or literal (`q:to`), `q:to:w`/`q:w:to` are `X::Undeclared::Symbols`, `qq:to:ww` returns one Str, `q:to:x` returns the source, and `q:to:to` is accepted.

## C. Sink context

### CP-27  What is sunk, and what a sunk object does                       D:partial R:yes V:quirk
A value is sunk — its `sink` method called — when it is the result of a
statement that is not the last of its block, of the last statement of a
block whose own value is not used (a bare block, a `do` block, `if`, the
body of `for`, `while`, `loop`, `try`, a pointy block called for its
effect, a `given` body), of a sub call whose value is not used (an
implicit return, an explicit `return`, a call with `;`), of a `sink`
prefix, of `.sink`/`.?sink`, and of the selected branch of a
sunk `?? !!`, `||` or the right side of a sunk `andthen`; every element
of a sunk list `(a, b)` is sunk too, and a sunk `xx` does not sink its
elements. Not sunk: anything assigned or bound (`my $v =`, `my ($z) =`,
`my @l =`, `my $y = do …`, `my $t = try …`), a method's invocant, the
condition of `andthen`, `&&`, `?? !!` and `given`, and (the quirk) the
left side of a sunk `//` and the last statement of a sunk block `for` when
that value is a Proc (CP-31) — Rakudo's `for` sinks a Failure and an
object with a `sink` method there (Roast `sink.t`, `for.t`) but lets a
Proc through.
```
say do { my @log; sub one { 1 }; my class S { has $.n; method sink { @log.push("s$!n") } }; S.new(:n(1)); my $v = S.new(:n(2)); S.new(:n(3)) if one(); (S.new(:n(4))); sub f { S.new(:n(5)) }; f(); my $r = f(); sub g { S.new(:n(6)); one() }; g(); do { S.new(:n(7)) }; my $d = do { S.new(:n(8)) }; sink S.new(:n(9)); for 1 { S.new(:n(10)) }; S.new(:n(11)) for 1; { S.new(:n(12)) }; my $x = { S.new(:n(13)) }(); if one() { S.new(:n(14)) }; my $y = do if one() { S.new(:n(15)) }; my ($z) = S.new(:n(16)); (S.new(:n(17)), one()); S.new(:n(18)).n; my @l = S.new(:n(19)); S.new(:n(20)) xx 2; sub h { return S.new(:n(21)) }; h(); my $w = h(); S.new(:n(22)) andthen one(); one() andthen S.new(:n(23)); S.new(:n(24)) && one(); sub k { S.new(:n(25)); }; k(); f; my $i = 0; while $i < 1 { $i++; S.new(:n(26)) }; loop (my $j = 0; $j < 1; $j++) { S.new(:n(27)) }; try { S.new(:n(28)) }; { S.new(:n(29)); one() }; -> { S.new(:n(30)) }(); S.new(:n(31)).sink; S.new(:n(32)).?sink; my $q = S.new(:n(33)) andthen one(); S.new(:n(34)) || one(); S.new(:n(35)) // one(); S.new(:n(36)) ?? one() !! one(); one() ?? S.new(:n(37)) !! one(); my @m = (S.new(:n(38)), one()); (S.new(:n(39)) for 1); my $t = try { S.new(:n(40)) }; S.new(:n(41)) andthen S.new(:n(42)); given S.new(:n(43)) { one() }; given one() { S.new(:n(44)) }; my $u = S.new(:n(45)).n; S.new(:n(46)).?n; sub p { S.new(:n(47)); return }; p(); sub q { S.new(:n(48)); Nil }; q(); my $sq = S.new(:n(49)); $sq = S.new(:n(50)); @log.raku }
# rakudo 2026.08: ["s1", "s3", "s4", "s5", "s6", "s7", "s9", "s10", "s11", "s12", "s14", "s17", "s21", "s23", "s25", "s5", "s26", "s27", "s28", "s29", "s30", "s31", "s32", "s34", "s37", "s39", "s42", "s44", "s47", "s48"]
```
rakupp 4.0.1-84: differs — 17 of the 30 sinks are missing: a statement-modifier `if`, a sub call, a `do` block, the `sink` prefix, an `if` block, a sunk list, an explicit `return`, the right side of `andthen`, a `;`-terminated last statement, a `try` block, a called pointy block, a sunk `||`, a chosen ternary branch, a `for` modifier, a `given` body; only bare statements, bare blocks, loop bodies, `.sink`/`.?sink` and `return`-less trailing statements sink.

### CP-28  Compile-time "Useless use" warnings                             D:partial R:partial V:spec
A sunk literal (`constant integer 1`, `rational 1.5`, `string "a"`,
`floating-point number 1e0`, `value a b` for a word list, `value Empty`,
`integer True`, `integer Int` for the type object), a sunk variable (`$x`,
`@a`, `%h`, `&f`, `$_`), a sunk pure operator expression (`"+" in
expression "$x + 1"`, `==`, `-`, `**`, `~`, `x`, `min`, `..`, `X`, `so `,
`not `, `*`), `...`, `()`, `'now'`, `[+]`, `>>+>>`, an anonymous sub and
each sunk branch of `?? !!`, `||`, `&&`, `and`, `andthen`, `,` are
reported once at compile time, also inside a sub body, also under `sink`
(`sink 42` warns, `sink $x` and `sink 1 + 1` do not). Silent: `Nil`, a
`~~`, `//`, `xx`, `.new`, a subscript, a method call, a class or block
declaration, `-> { }`, `{ ; }`. The text of a warning is not asserted by
Roast.
```
say do { sub W($c) { my $p = run($*EXECUTABLE, '-e', $c ~ ' Nil', :err, :out); my $e = $p.err.slurp(:close); $p.out.slurp(:close); $e.lines.map({ .trim }).grep({ $_ && $_ !~~ /^WARNINGS|^Potential|^at\s|^ '-'+ '>'/ }).map({ .subst(/\s+\(lines?\s<[\d,\s]>+\)/, "") }).sort.join("/") || "-" }; ('1;', 'my $x; $x;', 'my $x = 1; $x + 1;', '1, 2;', '1 ... 3;', '();', '"a";', '1.5;', '-1;', 'now;', '$_;', 'Int;', '1e0;', 'my @a; @a;', 'my %h; %h;', 'sub f {}; &f;', '"a" ~ "b";', 'True;', 'Nil;', 'Empty;', 'sink 42;', 'my $x = 1; sink $x;', 'sink 1 + 1;').map({ W($_) }).join(" | ") }
# rakudo 2026.08: Useless use of constant integer 1 in sink context | Useless use of $x in sink context | Useless use of "+" in expression "$x + 1" in sink context | Useless use of constant integer 1 in sink context/Useless use of constant integer 2 in sink context | Useless use of ... in sink context | Useless use of () in sink context | Useless use of constant string "a" in sink context | Useless use of constant rational 1.5 in sink context | Useless use of "-" in expression "-1" in sink context | Useless use of 'now' in sink context | Useless use of $_ in sink context | Useless use of constant integer Int in sink context | Useless use of constant floating-point number 1e0 in sink context | Useless use of @a in sink context | Useless use of %h in sink context | Useless use of &f in sink context | Useless use of "~" in expression "\"a\" ~ \"b\"" in sink context | Useless use of constant integer True in sink context | - | Useless use of constant value Empty in sink context | Useless use of constant integer 42 in sink context | - | -
say do { sub W($c) { my $p = run($*EXECUTABLE, '-e', $c ~ ' Nil', :err, :out); my $e = $p.err.slurp(:close); $p.out.slurp(:close); $e.lines.map({ .trim }).grep({ $_ && $_ !~~ /^WARNINGS|^Potential|^at\s|^ '-'+ '>'/ }).map({ .subst(/\s+\(lines?\s<[\d,\s]>+\)/, "") }).sort.join("/") || "-" }; ('1 == 1;', 'my $x = 1; $x ~~ 1;', 'my $x = 1; +$x;', 'my $x = 1; $x // 1;', 'my $x = 1; $x || 1;', '1 + 1;', 'sub f { 1; 2 };', '"a" x 2;', '1 xx 2;', '<a b>;', 'my $x = 1; $x ?? 1 !! 2;', 'so 1;', 'not 1;', 'my $x = 1; 1 if $x;', 'Int.new;', 'my @a = 1; @a[0];', 'my $x = 1; $x == 1;', 'my $x = 1; -$x;', 'my $x = 1; $x ** 2;', 'my $x = 1; $x, 2;', '1 andthen 2;', '1 min 2;', '1 .. 2;', 'my $x = 1; $x .. 2;', 'my $x = 1; $x ~ "a";', 'my $x = 1; ($x, 2);', '(1, 2) X (3, 4);', '1 ?? 2 !! 3;', 'my $x = 1; $x ?? 2 !! 3;', '[+] 1, 2;', 'my @a = 1; @a >>+>> 1;', 'my $x = 1; $x.abs;', '1 && 2;', 'my $x = 1; $x and 2;', 'my $x = 1; $x.Str;', 'my $x = 1; $x.abs, 2;', '{ ; };', '-> { };', 'sub { 1 };', 'class A { };', 'my $x = 1; $x xx 2;', '1 X 2;', 'my $x = 1; 2 * $x;').map({ W($_) }).join(" | ") }
# rakudo 2026.08: Useless use of "==" in expression "1 == 1" in sink context | - | Useless use of "+" in expression "+$x" in sink context | - | Useless use of constant integer 1 in sink context | Useless use of "+" in expression "1 + 1" in sink context | Useless use of constant integer 1 in sink context | Useless use of "x" in expression "\"a\" x 2" in sink context | - | Useless use of constant value a b in sink context | Useless use of constant integer 1 in sink context/Useless use of constant integer 2 in sink context | Useless use of "so " in expression "so 1" in sink context | Useless use of "not " in expression "not 1" in sink context | Useless use of constant integer 1 in sink context | - | - | Useless use of "==" in expression "$x == 1" in sink context | Useless use of "-" in expression "-$x" in sink context | Useless use of "**" in expression "$x ** 2" in sink context | Useless use of $x in sink context/Useless use of constant integer 2 in sink context | Useless use of constant integer 2 in sink context | Useless use of "min" in expression "1 min 2" in sink context | Useless use of ".." in expression "1 .. 2" in sink context | Useless use of ".." in expression "$x .. 2" in sink context | Useless use of "~" in expression "$x ~ \"a\"" in sink context | Useless use of $x in sink context/Useless use of constant integer 2 in sink context | Useless use of "X" in expression "(1, 2) X (3, 4)" in sink context | Useless use of constant integer 2 in sink context/Useless use of constant integer 3 in sink context | Useless use of constant integer 2 in sink context/Useless use of constant integer 3 in sink context | Useless use of [+] in sink context | Useless use of >>+>> in sink context | - | Useless use of constant integer 2 in sink context | Useless use of constant integer 2 in sink context | - | Useless use of constant integer 2 in sink context | - | - | Useless use of anonymous sub, did you forget to provide a name? | - | Useless use of $x in sink context | Useless use of "X" in expression "1 X 2" in sink context | Useless use of "*" in expression "2 * $x" in sink context
```
rakupp 4.0.1-84: differs — it reports literals, variables and arithmetic with its own wording (`Useless use of $x+1`, `constant number 1.5`, `constant integer -1`, `constant Bool True`, `Useless use of Int`), and is silent for `...`, `now`, `$_`, `&f`, `Empty`, `sink 42`, `$x || 1`, `?? !!`, `so`/`not`, `1 if $x`, `andthen`, `min`, `..`, `X`, `[+]`, the hyper, `&&`/`and`, `$x xx 2` and the anonymous sub.

### CP-29  Sink exemptions, and where the comma is sunk                    D:partial R:partial V:quirk
An assignment, `++`, `.=`, a `push`, a sub call, `Nil`, a declaration and
a statement-modifier expression are not warned about. `my $x = 1, 2`
assigns 1 and warns about the sunk 2; `my $x := 1, 2` binds the list;
`$x = 1, 2 if 1` assigns 1; `@a[0] = 1, 2` stores `(1, 2)` in the element
(CP-10); `my $y = $x = 2, 3` and `my $y = @a = 2, 3` assign 2 and `[2]`,
because the whole right side of a scalar assignment is parsed at
item-assignment precedence (an `@a =` inside it is not a list
assignment); `$x = 1 ?? 2 !! 3, 4` stores 2; `my $a = (1, 3) X (2, 4)`
stores `(1, 3)` and sinks the cross; `$x = 1 and 0` stores 1;
`$x = 1, 2 Z 3, 4 if 0` leaves `$x` undefined; `print $x ~ 2, 3` and
`print $x = 2, 3` print `123` and `23`. The quirk: `no worries` does not
silence the "Useless use" of a literal, and a bare block `{ 1 }` as a
statement warns for its inner literal. A heredoc whose terminator is
indented deeper than its text is compiled with a worry ("Asked to remove
N spaces, but the shortest indent is M spaces") and the text loses what it
has. `sub f { 1 }; f() + 1` warns about `+`; `$x + 1 for 1` and
`$x = 1 if 0` do not.
```
say do { sub W($c) { my $p = run($*EXECUTABLE, '-e', $c ~ ' Nil', :err, :out); my $e = $p.err.slurp(:close); my $o = $p.out.slurp(:close).trim; ($o || "-") ~ "/" ~ ($e.lines.map({ .trim }).grep({ $_ && $_ !~~ /^WARNINGS|^Potential|^at\s|^ '-'+ '>'/ }).map({ .subst(/\s+\(lines?\s<[\d,\s]>+\)/, "") }).sort.join("/") || "-") }; ('my $x; $x++;', 'my $x = 1;', 'my $x = 1, 2; print $x;', 'my $x := 1, 2; print $x.raku;', 'my $x; $x = 1, 2 if 1; print $x;', 'my $x = 1; $x.=abs;', 'my @a; @a.push(1);', 'my $x; $x = 1;', 'sub f { }; f;', 'my $x = 1; $x = $x;', '{ 1 };', 'my @a = 1, 2;', 'my $x = 1; $x;', 'my @a = 1; @a[0] = 1, 2; print @a.raku;', "q:to/END/;\n  a\n    END\n", "q:to/END/;\n  a\n\tEND\n", 'my $x; $x = 1 ?? 2 !! 3, 4; print $x;', 'my $x = 1; my $y = $x = 2, 3; print "$x $y";', 'sub f { 1 }; f() + 1;', 'my $x = 1; $x + 1 for 1;', 'my $x; $x = 1 if 0;', 'use fatal; 1;', 'no worries; 1;', 'quietly { 1 };', 'my $x = 1; { $x };', 'sub f { 42 }; f;', 'my $x = (1, 2);', '1 + 1 if 0;', 'my $x = 1 == 1;', 'my $a = (1, 3) X (2, 4); print $a.raku;', 'my @a = 1; my $y = @a = 2, 3; print $y.raku;', 'my $x = 1; my $z = $x = 2, 3; print $z.raku;', 'my $x = 1; $x = 2, 3 if 0; print $x;', 'print "x" x 2;', 'my $x = 1; print $x ~ 2, 3;', 'sink 1, 2;', 'my $x; $x = 1 and 0; print $x;', 'my $x; $x = 1, 2 Z 3, 4 if 0; print $x.raku;', 'my $x = 1; print $x = 2, 3;').map({ W($_) }).join(" | ") }
# rakudo 2026.08: -/- | -/- | 1/Useless use of constant integer 2 in sink context | (1, 2)/- | 1/Useless use of constant integer 2 in sink context | -/- | -/- | -/- | -/- | -/- | -/Useless use of constant integer 1 in sink context | -/- | -/Useless use of $x in sink context | [(1, 2),]/- | -/Asked to remove 4 spaces, but the shortest indent is 2 spaces | -/Asked to remove 8 spaces, but the shortest indent is 2 spaces | 2/Useless use of constant integer 4 in sink context | 2 2/Useless use of constant integer 3 in sink context | -/Useless use of "+" in expression "f() + 1" in sink context | -/- | -/- | -/Useless use of constant integer 1 in sink context | -/Useless use of constant integer 1 in sink context | -/- | -/Useless use of $x in sink context | -/- | -/- | -/Useless use of "+" in expression "1 + 1" in sink context | -/- | $(1, 3)/Useless use of "X" in expression "my $a = (1, 3) X (2, 4)" in sink context | $[2]/Useless use of constant integer 3 in sink context | 2/Useless use of constant integer 3 in sink context | 1/Useless use of constant integer 3 in sink context | xx/- | 123/- | -/- | 1/Useless use of constant integer 0 in sink context | Any/Useless use of "Z" in expression ", 2 Z 3," in sink context | 23/-
```
rakupp 4.0.1-84: differs in twelve fields — `my $y = @a = 2, 3` list-assigns `[2, 3]`, `$x = 1, 2 if 1`, `$x = 2, 3 if 0`, `$x = 1 and 0`, the sunk cross and zip do not warn, `no worries` silences the warning, `$x + 1 for 1` warns (with "use Nil instead"), `f() + 1` and `1 + 1 if 0` do not, and the deeper heredoc terminator gives "Useless use of constant string" instead of the indentation worry.

### CP-30  A sunk Failure throws; what marks it handled                    D:yes R:yes V:spec
A Failure that is sunk throws its exception (`X::AdHoc` for `fail "F"`):
as a sunk sub call (`f();`, `f;`, `f() for 1`), under the `sink` prefix,
via `.sink`, via `.self`; also when used as a number, string, list or in
`==`, `.Str`, `.gist`, `.list`, `.Slip`, `.elems`, `.chars`, `.Int`
(even after `.defined`), and — inside a `try` block, where `use fatal` is
in effect — the moment it is assigned (`try { my $y = f; 1 }` throws).
Not thrown: `.^name`, `.exception`, `.raku`, `.WHAT`, `.handled`, `~~`,
storing it in a scalar or array (`my @a = $x` has one element), `$x // 1`,
`$x || 7`, `$x orelse 1`, `$x andthen 1` (yields the Failure), `1 if $x`.
Testing it (`.so`, `.Bool`, `.defined`, `?? !!`, `//`, `orelse`) marks it
handled — through an alias too — and a handled Failure no longer throws
when sunk; a Failure that has thrown once is handled afterwards. A sunk
statement that throws stops the rest of its block (`try { w(); $c++ }`
leaves `$c` 0 and the try Nil). `.handled` reads False before and True
after.
```
say do { sub f { fail "F" }; my @o; my $r = f; @o.push($r.^name ~ ":" ~ $r.so); @o.push((try { f(); 1 }) // "threw:" ~ $!.^name); @o.push((try { f; 1 }) // "threw:" ~ $!.^name); @o.push(do { my $x = f; sub v1 { $x }; $x.so; try { v1(); 1 } // "threw" }); @o.push(do { my $x = f; sub v2 { $x }; try { v2(); 1 } // "threw" }); @o.push(do { my $x = f; try { sink $x; 1 } // "threw:" ~ $!.^name }); @o.push(do { my $x = f; $x.defined; try { sink $x; 1 } // "threw" }); @o.push(do { try { sink Failure.new; 1 } // "threw:" ~ $!.^name }); @o.push((try { f().sink; 1 }) // "threw:" ~ $!.message); @o.push(do { my $x = f; $x.handled ~ ":" ~ ($x.defined) ~ ":" ~ $x.handled }); @o.push(do { use fatal; (try { my $y = f; 1 }) // "threw:" ~ $!.^name }); @o.push(do { (try { my $y = f; 1 }) // "threw" }); @o.push(do { my $x = f; try { $x ?? 1 !! 2 } // "threw" }); @o.push(do { my $x = f; try { my $n = +$x; 1 } // "threw:" ~ $!.^name }); @o.push(do { try { my $n = f() + 1; 1 } // "threw:" ~ $!.^name }); @o.push(do { try { my $s = "" ~ f(); 1 } // "threw:" ~ $!.^name }); @o.push(do { try { f() for 1; 1 } // "threw:" ~ $!.^name }); @o.push(do { my $x = f; try { my $r = 1 if $x; 2 } // "threw" }); @o.push(do { my $x = f; my $y = $x; $y.so; $x.handled }); @o.push(do { my $x = f; try { my @a = $x; 1 } // "threw" }); @o.push(do { my $x = f; try { my @a = $x; @a.elems } // "threw" }); @o.push(do { my $x = f; try { $x.^name; 1 } // "threw" }); @o.push(do { my $x = f; try { $x.exception.^name; 1 } // "threw" }); @o.push(do { my $x = f; sub v3 { $x }; try { $x.Bool; v3(); 1 } // "threw" }); @o.push(do { my $x = f; try { $x.self; 1 } // "threw:" ~ $!.^name }); @o.push(do { my $x = f; try { $x.sink; 1 } // "threw" }); @o.push(do { my $x = f; try { $x // 1 } // "threw" }); @o.push(do { my $x = f; try { $x andthen 1.Int } // "threw" }); @o.push(do { my $x = f; try { $x orelse 1.Int } // "threw" }); @o.push(do { my $x = f; try { $x || 7 } // "threw" }); @o.push(do { my $x = f; try { $x.raku.chars > 0 } // "threw" }); @o.push(do { my $x = f; try { $x.Str; 1 } // "threw:" ~ $!.^name }); @o.push(do { my $x = f; try { $x.gist; 1 } // "threw" }); @o.push(do { my $x = f; try { my $b = $x == 1; 1 } // "threw:" ~ $!.^name }); @o.push(do { sub g { fail "G" }; my $q = g(); sub v4 { $q }; (try { v4(); 1 }) // "threw:" ~ $!.message }); @o.push(do { my $x = f; try { my $h = $x.handled; 1 } // "threw" }); @o.push(do { my $x = f; my $c = 0; try { $x.so; $c++; $x.so; $c++ }; $c }); @o.push(do { my $x = f; my $first = (try { $x.sink; 1 }) // "threw"; try { $x.sink; 1 } // "threw" }); @o.push(do { sub w { fail "W" }; my $c = 0; my $ok = try { w(); $c++ }; ($ok // "nil") ~ $c }); @o.push(do { my $x = f; try { $x.exception.message; 1 } // "threw" }); @o.push(do { my $x = f; try { $x.WHAT.raku; 1 } // "threw" }); @o.push(do { my $x = f; try { $x ~~ Failure; 1 } // "threw" }); @o.push(do { my $x = f; try { $x ~~ Str; 1 } // "threw" }); @o.push(do { my $x = f; try { $x.list; 1 } // "threw:" ~ $!.^name }); @o.push(do { my $x = f; try { $x.Slip; 1 } // "threw:" ~ $!.^name }); @o.push(do { my $x = f; try { $x.elems; 1 } // "threw:" ~ $!.^name }); @o.push(do { my $x = f; try { $x.chars; 1 } // "threw:" ~ $!.^name }); @o.push(do { my $x = f; try { $x.defined; $x.Int; 1 } // "threw:" ~ $!.^name }); @o.join(" ") }
# rakudo 2026.08: Failure:False threw:X::AdHoc threw:X::AdHoc 1 threw threw:X::AdHoc 1 threw:X::AdHoc threw:F False:False:True threw:X::AdHoc threw 2 threw:X::AdHoc threw:X::AdHoc threw:X::AdHoc threw:X::AdHoc 2 True 1 1 1 1 1 threw:X::AdHoc threw 1 threw 1 7 True threw:X::AdHoc threw threw:X::AdHoc threw:G 1 2 1 nil0 1 1 1 1 threw:X::AdHoc threw:X::AdHoc threw:X::AdHoc threw:X::AdHoc 1
```
rakupp 4.0.1-84: differs in fourteen fields — a sunk `f()` call, `sink $x`, `sink Failure.new`, `+$x` and `$x.Int` after `.defined` do not throw, neither does the assignment inside `try`; `.so`/`.Bool`/`.defined` do not mark it handled (the aliased test and the later `sink` still throw); a throw does not mark it handled; and a sub declared after its use inside a `do` block is "Undefined routine".

### CP-31  A sunk Proc throws `X::Proc::Unsuccessful`                       D:yes R:yes V:quirk
A Proc sunk after a non-zero exit or a signal throws
`X::Proc::Unsuccessful` whose `.proc` is the Proc (`.exitcode` 1, 7 from
`shell("exit 7")`, `.signal` 9); a missing command has exit code -1 and
throws too; exit 0 is silent and `.sink` gives Nil. Sunk means: a sunk
`run`/`shell` call, `sink $p`, `$p.sink`, a sunk sub returning it, a sunk
`do` block, an `if` body, a `for` modifier, `run(…, :out)` and even
`$p.out.close` (which returns the Proc). Not sunk: a Proc assigned, in a
list, tested with `.so` (False for a failure) or `.exitcode`, `&&`/`||`
when the other operand is chosen (`run("false") && one()` sinks the
Proc since it is the result; `run("false") || one()` does not), or the
condition of `andthen`. Reading `.exitcode`, `.so` or a first throw does
not mark it: every later sink throws again. The quirk: as the last
statement of a block `for` body the Proc is not sunk (`for 1 {
run("false") }` is silent) while a Failure there throws and a `run` in a
non-last position throws.
```
say do { my @o; sub one { 1 }; @o.push((try { run("false"); 1 }) // "threw:" ~ $!.^name ~ ":" ~ ((try $!.proc.exitcode) // "-")); @o.push((try { run("true"); 1 }) // "threw"); @o.push(do { my $p = run("false"); $p.exitcode }); @o.push((try { shell("exit 7"); 1 }) // "threw:" ~ ((try $!.proc.exitcode) // "-")); @o.push((try { run("false").sink; 1 }) // "threw:" ~ $!.^name); @o.push(do { my $p = run("false"); sub v1 { $p }; (try { v1(); 1 }) // "threw" }); @o.push(do { my $p = run("false"); (try { sink $p; 1 }) // "threw" }); @o.push(run("true").sink.raku); @o.push((try { run("false") for 1; 1 }) // "threw"); @o.push(do { sub f { run("false") }; (try { f(); 1 }) // "threw" }); @o.push(do { sub f { run("false") }; my $q = f(); $q.^name }); @o.push((try { run("sh", "-c", "kill -9 \$\$"); 1 }) // "threw:" ~ ((try $!.proc.signal) // "-")); @o.push(do { my $p = run("false"); sub v2 { $p }; (try { $p.exitcode; v2(); 1 }) // "threw" }); @o.push(do { (try { run("false", :out); 1 }) // "threw" }); @o.push(do { (try { my $p = run("false", :out); $p.out.close; 1 }) // "threw:" ~ $!.^name }); @o.push(do { (try { run("false").exitcode; 1 }) // "threw" }); @o.push(do { (try { run("false") && one(); 1 }) // "threw" }); @o.push(do { (try { run("false") andthen one(); 1 }) // "threw" }); @o.push(do { (try { my @a = run("false"); 1 }) // "threw" }); @o.push(do { (try { run("false").so; 1 }) // "threw" }); @o.push(run("false").so); @o.push(run("true").so); @o.push((try { run("nonexistent-zz-cmd"); 1 }) // "threw:" ~ $!.^name); @o.push(do { my $p = run("nonexistent-zz-cmd"); $p.exitcode }); @o.push(do { (try { shell("exit 0"); 1 }) // "threw" }); @o.push(do { (try { my $e = run("false").exitcode + 0; 1 }) // "threw" }); @o.push(do { (try { run("true") || one(); 1 }) // "threw" }); @o.push(do { (try { run("false") || one(); 1 }) // "threw" }); @o.push(do { my $p = run("false"); (try { $p.so; sub v3 { $p }; v3(); 1 }) // "threw" }); @o.push(do { my $p = run("false"); (try { $p.sink; 1 }) // "threw:" ~ $!.^name }); @o.push(do { my $p = run("true"); (try { $p.sink; 1 }) // "threw" }); @o.push(do { my $p = run("sh", "-c", "kill -9 \$\$"); "$p.exitcode() $p.signal()" }); @o.push(do { my $p = run("false"); sub v4 { $p }; my $first = (try { v4(); 1 }) // "threw"; (try { v4(); 1 }) // "threw" }); @o.push(do { my $p = run("false"); (try { $p.exitcode; $p.so; 1 }) // "threw" }); @o.push(do { (try { for 1 { run("false") }; 1 }) // "threw" }); @o.push(do { (try { if one() { run("false") }; 1 }) // "threw" }); @o.push(do { (try { my $q = do { run("false") }; 1 }) // "threw" }); @o.push(do { (try { do { run("false") }; 1 }) // "threw" }); @o.join(" ") }
# rakudo 2026.08: threw:X::Proc::Unsuccessful:1 1 1 threw:7 threw:X::Proc::Unsuccessful threw threw Nil threw threw Proc threw:9 threw threw threw:X::Proc::Unsuccessful 1 threw 1 1 1 False True threw:X::Proc::Unsuccessful -1 1 1 1 1 threw threw:X::Proc::Unsuccessful 1 0 9 threw 1 1 threw 1 threw
```
rakupp 4.0.1-84: differs in nine fields — a sunk `run("false")` statement, `sink $p` and `run("false") for 1` do not throw at the top of a `try` while the sub-returned and `.sink` forms do, `X::Proc::Unsuccessful` has no `.proc`, `run("true").sink` returns a hash of the Proc's fields instead of Nil, a missing command has exit code 127, `for 1 { run("false") }` throws where Rakudo is silent, and `if one() { run("false") }` does not throw.

### CP-32  A sunk Seq is iterated, a stored one is not — lazy or not       D:partial R:partial V:quirk
A `map`, `gather`, `.lazy`, `lazy (…)`, `eager`, `.Seq`, `.Array`,
`.grep`, `.elems`, `.head(1)`, `Seq.new(iterator)` or a `for`-loop value
that is sunk runs to the end (side effects happen: `$n` is 3), whether or
not it is lazy — `lazy (1..3).map` sunk still counts 3, a sunk infinite
`(1..*).map` never returns (measured inside `start` with a 2 s timeout:
the Promise stays Planned and the counter passes 100). `.sink` and
`.sink-all` run it too and return Nil; `$n++ xx 3` sunk evaluates three
times. Stored, passed to `.list`, `.List`, `.cache`, `.Slip`,
`.iterator`, `.is-lazy`, or used as an `andthen` condition, it stays
unrun (0); `&&` reifies one element (1); a `last` inside the map stops it
(2); a sunk `xx` of Seqs does not run them (0); an assigned lazy Seq
throws `X::Cannot::Lazy` on `.elems` without running. The quirk: a Seq
handed back with an explicit `return` and then sunk at the call site is
not iterated (0), while the same Seq as the sub's implicit value is (3).
```
say do { my @o; my $n = 0; (1..3).map({ $n++ }); @o.push($n); $n = 0; my $s = (1..3).map({ $n++ }); @o.push($n); $n = 0; (1..3).map({ $n++ }).sink; @o.push($n); $n = 0; lazy (1..3).map({ $n++ }); @o.push($n); $n = 0; (lazy (1..3).map({ $n++ })); @o.push($n); $n = 0; gather { $n++; take 1 }; @o.push($n); $n = 0; my $g = gather { $n++; take 1 }; @o.push($n); $n = 0; (1..3).map({ $n++ }) xx 2; @o.push($n); $n = 0; my $v = (1..3).map({ $n++ }).sink; @o.push($v.raku ~ $n); $n = 0; sub f { (1..3).map({ $n++ }) }; f(); @o.push($n); $n = 0; my $r = f(); @o.push($n); $r.elems; @o.push($n); $n = 0; eager (1..3).map({ $n++ }); @o.push($n); $n = 0; (1..3).map({ $n++ }).lazy; @o.push($n); $n = 0; { (1..3).map({ $n++ }) }; @o.push($n); $n = 0; do { (1..3).map({ $n++ }) }; @o.push($n); $n = 0; $n++ xx 3; @o.push($n); $n = 0; my @a = $n++ xx 3; @o.push($n); $n = 0; (1..3).map({ $n++ }).Seq; @o.push($n); $n = 0; (1..3).map({ $n++ }).list; @o.push($n); $n = 0; (1..3).map({ $n++ }).List; @o.push($n); $n = 0; (1..3).map({ $n++ }).Array; @o.push($n); $n = 0; (1..3).map({ $n++ }).cache; @o.push($n); $n = 0; (1..3).map({ $n++ }).head(1); @o.push($n); $n = 0; (1..3).map({ $n++ }).grep(*.so); @o.push($n); $n = 0; (1..3).map({ $n++ }).elems; @o.push($n); $n = 0; my $l = lazy (1..3).map({ $n++ }); @o.push($n); try { $l.elems }; @o.push($n); $n = 0; (1..3).map({ $n++ }) if 1; @o.push($n); $n = 0; if 1 { (1..3).map({ $n++ }) }; @o.push($n); $n = 0; for 1 { (1..3).map({ $n++ }) }; @o.push($n); $n = 0; (1..3).map({ $n++ }) for 1; @o.push($n); $n = 0; my @s = (1..3).map({ $n++ }).sink; @o.push($n ~ @s.elems); $n = 0; (1..3).map({ $n++ }).lazy.sink; @o.push($n); $n = 0; (1..3).lazy.map({ $n++ }); @o.push($n); $n = 0; Seq.new((1..3).map({ $n++ }).iterator); @o.push($n); $n = 0; (1..3).map({ $n++ }).Slip; @o.push($n); $n = 0; (1..3).map({ $n++; last if $n == 2 }); @o.push($n); $n = 0; ((1..3).map({ $n++ }), $n.Str); @o.push($n); $n = 0; sub h { (1..3).map({ $n++ }); Nil }; h(); @o.push($n); $n = 0; sub i { return (1..3).map({ $n++ }) }; i(); @o.push($n); $n = 0; (1..3).map({ $n++ }).iterator; @o.push($n); $n = 0; my $it = (1..3).map({ $n++ }).iterator; @o.push($n); $it.sink-all; @o.push($n); $n = 0; (1..3).map({ $n++ }).lazy.eager; @o.push($n); $n = 0; my $lz = (1..3).map({ $n++ }).lazy; @o.push($n ~ $lz.is-lazy); $n = 0; (1..3).map({ $n++ }).is-lazy; @o.push($n); $n = 0; (1..3).map({ $n++ }) andthen $n.Str; @o.push($n); $n = 0; (1..3).map({ $n++ }) && $n.Str; @o.push($n); $n = 0; @o.join(" ") }
# rakudo 2026.08: 3 0 3 3 3 1 0 0 Any3 3 0 3 3 3 3 3 3 3 3 0 0 3 0 1 3 3 0 0 3 3 3 3 31 3 3 3 0 2 3 3 3 0 0 3 3 0True 0 0 1
say do { my $n = 0; my $p = start { (1..*).map({ $n++ }); 1 }; await Promise.anyof($p, Promise.in(2)); ($p.status.raku, $n > 100).join(" ") }
# rakudo 2026.08: PromiseStatus::Planned True
```
rakupp 4.0.1-84: differs in 22 of the 49 counts and in the infinite case — a stored map, gather, `.list`, `.List`, `.cache`, `.Slip`, `.iterator`, `.is-lazy`, a stored lazy Seq and the `andthen`/`&&` operands all run to the end (map is eager), a sunk `xx` of Seqs runs them (6), and the infinite sunk map is not iterated (Kept, False).

### CP-33  The value of a block, a routine, a loop and a conditional       D:partial R:yes V:quirk
A routine's value is its last statement's value, also with a trailing `;`
or an empty statement after it (`42;`, `42; ;`), decontainerized
(`my $x = 1` as the last statement gives an Int, `my @a = 1, 2` an
Array; `.VAR.^name` Int/Array); an empty routine, a bare `return`, `Nil`,
`{ ; }`, `-> { }()`, `given 5 { }` and `try { die }` give Nil. `for`,
`while`, `until`, `repeat`, `loop`, a statement-modifier `for`/`while`
and `1.Int while 0` as the last statement give Nil (they are sunk),
`do for` gives the values `(2, 4, 6)` and `do while` a Seq; `if`,
`unless`, `elsif`, `with`, a statement-modifier `if`/`unless` that did
not run give `Empty` (a Slip, so `(1, f6(), 2)` is `(1, 2)`, while a
Nil-valued `for` stays in the list), a branch that ran gives its value
(`(1, 2)` is a List, `do if`, `(if 1 { 5 })`, `(for 1..2 { $_ })`,
`(1, 2)`, `my ($a, $b) = 1, 2`, `my $x := 5`, `42.Int if 1`);
`given` and a bare block give their body's value; `when 1 { 5 }` with
no topic gives 0; `try { 42 }` gives 42. The quirk: `return $x` hands
back the container (`.VAR.^name` Scalar) where the implicit last
statement hands back the value.
```
say do { sub f0 { }; sub f1 { 42; }; sub f2 { 42; ; }; sub f3 { my $x = 1 }; sub f4 { for 1..3 { $_ * 2 } }; sub f5 { my $i = 0; while $i < 3 { $i++ } }; sub f6 { if 0 { 1 } }; sub f7 { if 1 { 1, 2 } }; sub f8 { unless 1 { 1 } }; sub f9 { loop (my $i = 0; $i < 3; $i++) { $i.Str } }; sub f10 { $_ * 2 for 1..3 }; sub f11 { given 5 { $_ + 1 } }; sub f12 { try { 42 } }; sub f13 { try { die "x" } }; sub f14 { { 7 } }; sub f15 { return }; sub f16 { Nil }; sub f17 { my @a = 1, 2 }; sub f18 { do for 1..3 { $_ * 2 } }; sub f19 { 1; }; sub f20 { my $x = 1; $x }; sub f21 { if 0 { 1 } else { 2 } }; sub f22 { if 0 { 1 } elsif 0 { 2 } }; sub f23 { with Any { 1 } }; sub f24 { my $i = 0; repeat { $i++ } while $i < 3 }; sub f25 { for 1..3 { $_ * 2 }; }; sub f26 { 1.Int if 0 }; sub f27 { 1.Int unless 0 }; sub f28 { my $i = 0; while $i < 3 { $i++ }; }; sub f29 { $_.Str for 1..3 }; sub f30 { do { 5 } }; sub f31 { my $i = 0; do while $i < 3 { $i++ } }; sub f32 { 1.Int while 0 }; sub f33 { 2.Int unless 1 }; sub f34 { 1, 2 }; sub f35 { my ($a, $b) = 1, 2 }; sub f36 { (1, 2) }; sub f37 { my @a }; sub f38 { my %h }; sub f39 { given 5 { } }; sub f40 { -> { 9 }() }; sub f41 { my $i = 0; until $i == 2 { $i++ } }; sub f42 { do { my $i = 0; while $i < 3 { $i++ } } }; sub f43 { do if 0 { 1 } }; sub f44 { do given 5 { $_ } }; sub f45 { (if 1 { 5 }) }; sub f46 { (for 1..2 { $_ }) }; sub f47 { my $x := 5 }; sub f48 { my $x = 5; return $x }; sub f49 { my @a = 1, 2; return @a }; sub f50 { 42.Int if 1 }; sub f51 { when 1 { 5 } }; sub f52 { 5.Int; Nil; }; sub f53 { my $v = do { 1.Int; 2 }; $v }; sub f54 { { 1.Int; 2 } }; sub f55 { { ; } }; sub f56 { -> { }() }; sub f57 { my $n = 0; while $n < 2 { $n++; $n.Str } }; (f0().raku, f1().raku, f2().raku, f3().VAR.^name, f4().raku, f5().raku, f6().raku, f7().raku, f8().raku, f9().raku, f10().raku, f11().raku, f12().raku, f13().raku, f14().raku, f15().raku, f16().raku, f17().raku, f18().raku, f19().raku, f20().VAR.^name, f21().raku, f22().raku, f23().raku, f24().raku, f25().raku, f26().raku, f27().raku, f28().raku, f29().raku, f30().raku, f31().raku, f32().raku, f33().raku, f34().raku, f35().raku, f36().raku, f37().raku, f38().raku, f39().raku, f40().raku, f6().^name, f8().^name, f4().^name, f0().^name, f7().^name, (1, f6(), 2).raku, (1, f4(), 2).raku, (1, f10(), 2).raku, f41().raku, f42().raku, f43().raku, f44().raku, f45().raku, f46().raku, f47().raku, f48().VAR.^name, f49().raku, f50().raku, f51().raku, f52().raku, f53().raku, f54().raku, f55().raku, f56().raku, f57().raku, f26().^name, f43().^name, f3().raku, f17().VAR.^name, f49().VAR.^name, f12().^name, f2().^name).join(" ") }
# rakudo 2026.08: Nil 42 42 Int Nil Nil Empty (1, 2) Empty Nil Nil 6 42 Nil 7 Nil Nil [1, 2] (2, 4, 6) 1 Int 2 Empty Empty Nil Nil Empty 1 Nil Nil 5 (0, 1, 2).Seq Nil Empty (1, 2) (1, 2) (1, 2) [] {} Nil 9 Slip Slip Nil Nil List (1, 2) (1, Nil, 2) (1, Nil, 2) Nil Nil Empty 5 5 (1, 2) 5 Scalar [1, 2] 42 0 Nil 2 2 Nil Nil Nil Slip Slip 1 Array Array Int Int
```
rakupp 4.0.1-84: differs in five fields — `do while` gives a List, `return $x` gives the value, `when 1 { 5 }` with no topic gives `Bool::False`, and `{ ; }` as the last statement gives an empty Hash.

### CP-34  The `sink` statement prefix                                     D:yes R:yes V:spec
`sink EXPR` runs the whole statement (a list, a `for`, an `if`, a `map`,
a `gather`, a block called for its value — `sink { $e++ }` does call the
block) and discards it, returning Nil (`my $r = sink for ^5 {…}` leaves
`$r` undefined, `(sink one()) // 9` is 9, `sink k(), one()` is Nil, a sub
ending in `sink k()` returns Nil). It takes everything to the end of the
statement (`sink k() + 1 ?? 2 !! 3`, `sink 1 if $s == 0`), sinks the
value (a class's `sink` runs, a Failure or a failed Proc throws, a lazy
Seq is iterated, `sink S.new` and `S.new.sink` both count), and `.sink`
on a plain value (Nil, 42, a Str, an Array, a Range) is Nil. `sink 42`
still warns at compile time (CP-28).
```
say do { my $c = 0; my $r = sink for ^5 { $c++ }; my $d = do for ^3 { $_ }; my @log; sub k { 42 }; sub one { 1 }; my class S { method sink { @log.push("sunk") } }; my $v = sink S.new; my $w = S.new.sink; (($r.raku, $c, $d.raku, $d.^name, @log.raku, $v.raku, $w.raku, (1..3).map({$_}).sink.raku, (sink k()).raku, (sink one(), k()).raku, do { my $x = 0; sink $x++; $x }, Nil.sink.raku, 42.sink.raku, (my $z = 5).sink.raku, "s".sink.raku, [1,2].sink.raku, (sink S.new).raku, @log.elems, do { my $q = do { S.new }; $q.^name }, do { my $e = 0; my $q = sink { $e++ }; "$e" ~ $q.raku }, do { my $q = sink if one() { k() }; $q.raku }, do { my $q = sink k() + k(); $q.raku }, (sink Nil).raku, do { my @a = 1,2; (sink @a).raku }, do { my $q = (sink one()) // 9; $q }, do { my $q = do { sink one() }; $q.raku }, do { sub f { sink k() }; f().raku }, do { my $s = 0; sink (1..3).map({ $s++ }); $s }, do { my $s = 0; sink lazy (1..3).map({ $s++ }); $s }, do { my $g = 0; sink gather { $g++; take 1 }; $g }, do { (try { sink fail("x"); 1 }) // "threw" }, do { (try { sink run("false"); 1 }) // "threw" }, do { my $p = run("true"); (sink $p).raku }, do { my $q = sink { 5 }; $q.^name }, do { my $c2 = 0; sink for 1..3 { $c2++ }; $c2 }, do { my $q = sink (one(), k()); $q.raku }, do { sub f { sink k(); }; f().raku }, do { sub f { sink k(); 7 }; f().raku }, (sink k().Str).raku, do { my $q = sink k() + 1 ?? 2 !! 3; $q.raku }, do { my $q = sink k(), one(); $q.raku }, do { my @q = sink k(), one(); @q.raku }, do { my $s = 0; sink 1 if $s == 0; $s }).join(" ")) }
# rakudo 2026.08: Any 5 $(0, 1, 2) List ["sunk", "sunk"] Any $["sunk", "sunk"] Nil Nil Nil 1 Nil Nil Nil Nil Nil Nil 3 S 1Any Any Any Nil Nil 9 Any Nil 3 3 1 threw threw Nil Any 3 Any Nil 7 Nil Any Any [Any] 0
```
rakupp 4.0.1-84: differs in seven fields — `sink for ^5 {…}` and `sink for 1..3 {…}` run the body once, `sink S.new` does not call `sink` (one entry in the log instead of two), and `sink run("false")` does not throw.

## Counts

| | items |
|---|---|
| total | 34 |
| not fully stated by docs (D:yes) nor asserted by Roast (R:yes) | 8 |
| Rakudo bugs (do not imitate) | 1 — CP-22 brace-delimited `qx{…}` runs a nested `{…}` as a separate command |
| quirks (recorded, step two decides) | 8 — CP-05 chained `~~`, CP-13 `>>.^name`, CP-15 sunk `let`, CP-27/CP-31 a Proc escapes a block `for` and a sunk `//`, CP-29 `no worries` and the sunk bare block, CP-32 a returned Seq sunk at the call, CP-33 `return $x` keeps the container |
| rakupp 4.0.1-84 differs | 32 |
| rakupp 4.0.1-84 matches | 2 — CP-04, CP-17 |

Recurring rakupp gaps, for step two: the same-level rule of the
operator-precedence parser is missing — every list-associative mix that
Rakudo refuses (`X::Syntax::NonListAssociative`, `X::Syntax::NonAssociative`,
`X::Syntax::CannotMeta`) parses, and the non-associative structural row
folds left to right (CP-05, CP-07, CP-09, CP-11, CP-12); junctives, `min`,
the pair and the ternary sit at the wrong level (CP-01, CP-05, CP-07,
CP-08, CP-11); postfixes accept a space and the missing-term errors are
all `X::Syntax::Confused` (CP-14); operands of `+` are read before a
postfix `++` runs (CP-03); `=:=` on bound aliases is False and a `do`
block returns values not containers, so `temp`/`let` (which never
restore on `fail`/undefined) and the container quirks differ (CP-15);
`notandthen` does not exist, `andthen` on an undefined left side returns
it instead of `Empty`, and `orelse` does not mark a Failure handled
(CP-09). On the quote side: `q 'x'`/`Q (a)` space forms and most
non-bracket delimiters are refused while `q'a'` is accepted, `Q`
processes backslashes, `\qq[]`/`\q[]` are missing, `:v` never makes an
allomorph, `:x` inside a combination does not run, interpolated words
are not re-split and `qq:ww`/`Q:ww` return one Str, `<<42>>` does not
parse, and a tab counts as one column in a heredoc's indentation (CP-19
to CP-26). On the sink side: a sunk sub call, `do` block, `if` block,
list, chosen ternary branch and `sink` prefix do not sink their value, so
sunk Failures and Procs rarely throw and `.so`/`.defined` do not mark a
Failure handled; `map` is eager, so a stored Seq runs and the infinite
case is not iterated (CP-27, CP-30, CP-31, CP-32, CP-34); the "Useless
use" set is smaller and worded differently (CP-28, CP-29).

## Method (how this sheet was produced)

As for [Supply.md](Supply.md), with the precautions the topic demanded:
every syntax question runs through `EVAL` inside `try` and prints
`$!.^name`; every compile-time warning is read from the standard error of
a child process (`run $*EXECUTABLE, '-e', …`) with the `WARNINGS for`
header and the `(line N)` suffix stripped, so that the warnings could not
decorate a line; heredocs are built as strings with real newlines and
`EVAL`ed; probes that Raku++ cannot parse (`<<42>>`, `Q[\]`, the
space-form `q 'x'`, curly-quote delimiters) were moved behind `EVAL` so
that one construct did not blank a whole line. Traps met: a literal in a
sunk position anywhere on the line adds a `WARNINGS` block to the output,
including a literal in the thunk of `or`/`andthen` inside an `EVAL`
string that fails to compile (the worry is still printed) — sub calls
were used as operands there; `no worries` does not help; `{ … }` inside
a double-quoted `EVAL` string is a closure, so every brace that belongs
to the EVALed code is written `\{`; `q{…}` containing `\o{…}`/`\x{…}` is
not a compile-time constant for `EVAL`; a `{ … }` term followed by an
infix is a Block object, not a call; `Q:c{{1+1} …}` opens a doubled
delimiter; `awk -v` decodes backslashes, which corrupted a first version
of the probes file (the per-line files were rebuilt from scratch);
`(try EVAL …) // $!.^name` reads `Any` when the EVAL returns Nil. D flags
from `doc/Language/operators.rakudoc`, `quoting.rakudoc`,
`contexts.rakudoc`, `statement-prefixes.rakudoc`, `control.rakudoc` and
`doc/Type/{Failure,Proc,Seq,Any}.rakudoc`; R flags from the 43 Roast files
named in the second paragraph.
