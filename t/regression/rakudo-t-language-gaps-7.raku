# Regression: language behaviour from the seventh batch of Rakudo t/ gap files
# (2026-10-05).
#
# - `BEGIN / … /` — a value-answering phaser takes a regex term.
# - `my (::T, $x) := (Int, 5)` captures a type for the rest of the scope.
# - A subset called as a coercer (`Even(56)`) checks the value, coercing it to
#   the subset's nominal type first; `UInt(-3)` is X::Coerce::Impossible.
# - `make` with no Match in `$/` dies X::Make::MatchRequired; with one, the
#   match takes the value.
# - A hash as a regex assertion (`<$h>`, `<%h>`, a hash inside `<@a>`) is
#   X::Syntax::Reserved.
# - `my ($a, @b is copy) := …` takes the parameter trait.
#
# Every expectation below was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
sub err(&code) { (try { code(); 'lived' }) // $!.^name }

ck(err({ make 5 }), 'X::Make::MatchRequired', 'make with no match dies');

my $r = BEGIN / ^ \w+ $ /;
ck(so('abc' ~~ $r), True, 'BEGIN takes a regex term');

my (::LT, $lx) := (Int, 5);
ck((LT, $lx), (Int, 5), 'a type capture in a list declaration');
my LT $constrained = 3;
ck($constrained, 3, 'the captured type constrains a later declaration');

subset Even of UInt where * %% 2;
ck((Even(56), Even("58")), (56, 58), 'a subset coerces a value it accepts');
ck(err({ Even(-3) }), 'X::Coerce::Impossible', 'a subset refuses a value it does not accept');
ck(err({ UInt(-3) }), 'X::Coerce::Impossible', 'UInt(-3)');

'a' ~~ /a/;
make 7;
ck($/.made, 7, 'make attaches to the $/ in scope');

my $h = { a => 1 };
my @withHash = 'ab', $h;
ck(err({ 'a' ~~ / <$h> / }), 'X::Syntax::Reserved', '<$h> is reserved');
ck(err({ EVAL q[my %hv = a => 1; 'a' ~~ / <%hv> /] }), 'X::Syntax::Reserved', '<%h> is reserved');
ck(err({ 'ab' ~~ / <@withHash> / }), 'X::Syntax::Reserved', 'a hash inside <@a> is reserved');

ck(do { my ($a, @pc is copy) := (0, ['old']); @pc := ['new'] }, ['new'], 'is copy in a list declaration');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
