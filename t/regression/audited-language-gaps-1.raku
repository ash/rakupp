# Regression: language behaviour from the first batch of audited mutsu gap
# files (2026-10-04) — the files a random audit classed as LANG, not Rakudo
# prose or internals.
#
# - `.EXISTS-POS(-1)` is False; `return()` (tight) is Nil, `return ()` the
#   empty List; `%h<=>` glued to its term is the key `=`.
# - `<+digit +[!]>` parses; under :i a NAMED class member does not fold.
# - deepmap/duckmap iterate a Range (and itemize a nested one); `@a».*m` is
#   an Array.
# - A typed `state` variable checks later assignments.
# - Promise/Channel/Supply do Awaitable; BagHash.add counts each item.
# - A user Exception subclass's type object cannot `.throw`.
# - nextcallee in the only candidate is Nil; placeholders order by NAME, the
#   sigil ignored; (Int, Any) beside (Any, Int) is ambiguous for (1, 1).
#
# Every expectation below was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
sub err(&code) { CATCH { default { return $_ } }; code(); 'lived' }

ck((1..5).EXISTS-POS(-1), False, 'a negative position never exists');
sub r1 { return() }
sub r2 { return () }
ck((r1().raku, r2().raku), ('Nil', '()'), 'return() is Nil, return () the empty List');
my %h = '=' => 5;
ck(%h<=>, 5, '%h<=> is the key =');
%h<=> = 2;
ck(%h<=>, 2, '... and assignable');
ck(('a!3' ~~ m:g/<+digit +[!]>/).map(~*).List, ('!', '3'), '<+digit +[!]> parses');
ck(~('zQ' ~~ m:i/<+upper -[A]>/), 'Q', ':i does not fold a named member');
ck(~('B' ~~ m:i/<[a..c]>/), 'B', '... but still folds a literal one');
ck((1..3).deepmap(* * 2), (2, 4, 6), 'deepmap iterates a Range');
ck((1, 2..3).deepmap(* + 1).raku, '(2, $(3, 4))', 'a nested Range itemizes');
ck((1, (2..3)).duckmap(-> Int $x { $x * 10 }).raku, '(10, $(20, 30))', 'duckmap descends into a Range');
class HM { method m { 1 } }
my @hm = HM.new;
ck((@hm».*m).^name, 'Array', '@a».*m is an Array');
sub st { state Str $s = 's'; $s = 99 }
ck(err(&st).^name, 'X::TypeCheck::Assignment', 'a typed state variable checks assignment');
ck((Promise ~~ Awaitable, Channel ~~ Awaitable), (True, True), 'Promise and Channel do Awaitable');
my $b = BagHash.new: <c c>;
$b.add('c');
$b.add(<d d>);
ck(($b<c>, $b<d>), (3, 2), 'BagHash.add counts each item');
class MyErr is Exception { }
ck(err({ MyErr.throw }).^name, 'X::Parameter::InvalidConcreteness', 'a user Exception type object cannot throw');
multi only-one($x) { nextcallee().raku }
ck(only-one(1), 'Nil', 'nextcallee in the only candidate is Nil');
sub ph { (@^arr, &^cb, %^h).map(*.^name).List }
ck(ph([1], { 2 }, { a => 1 }), ('Array', 'Block', 'Hash'), 'placeholders order by name');
multi amb(Int, Any) { 1 }
multi amb(Any, Int) { 2 }
ck(err({ amb(1, 1) }).^name, 'X::Multi::Ambiguous', 'incomparable core-typed candidates are ambiguous');
multi nar(Int, Int) { 1 }
multi nar(Any, Any) { 2 }
ck(nar(1, 1), 1, '... while a narrower one still wins');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
