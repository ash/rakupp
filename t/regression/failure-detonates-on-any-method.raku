# Regression: a method a Failure does not answer itself is a USE of the value,
# and using an unhandled Failure throws the exception it carries — Rakudo's
# Failure.FALLBACK. Here the call fell through to the Hash the Failure is
# stored as, so `"x".Int.elems` answered 2 (the key count of that hash) and a
# program that had been handed a Failure by mistake went on computing with
# it. Date::Names met this on an unknown language: its symbolic lookup
# missed, and the miss was consumed as if it were the names table.
#
# Every expectation below was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
sub outcome(&code) {
    { my $v = code(); return 'value ' ~ $v.raku; CATCH { default { return .^name } } }
}

ck(outcome({ my $f = "x".Int; $f.elems }),   'X::Str::Numeric', '.elems on a Failure throws its exception');
ck(outcome({ my $f = "x".Int; $f.keys }),    'X::Str::Numeric', '.keys does too');
ck(outcome({ my $f = "x".Int; $f.list }),    'X::Str::Numeric', 'and .list');
ck(outcome({ my $f = "x".Int; $f.Str }),     'X::Str::Numeric', 'and .Str');
ck(outcome({ my $f = "x".Int; $f.succ }),    'X::Str::Numeric', 'and a method the type would have');

# what stays quiet: the Failure's own methods and introspection
ck(outcome({ my $f = "x".Int; $f.defined }),          'value Bool::False', '.defined answers False');
ck(outcome({ my $f = "x".Int; $f.handled }),          'value Bool::False', '.handled reads the flag');
ck(outcome({ my $f = "x".Int; $f.exception.^name }),  'value "X::Str::Numeric"', '.exception hands the exception over');
ck(outcome({ my $f = "x".Int; $f.^name }),            'value "Failure"', '.^name introspects');
ck(outcome({ my $f = "x".Int; $f ~~ Failure }),       'value Bool::True', 'smartmatch against the type');
ck(outcome({ my $f = "x".Int; $f.WHAT.^name }),       'value "Failure"', '.WHAT');

# the thrown exception marks it handled, so a later test of it is calm
my $f = "x".Int;
try { $f.elems }
ck($f.handled, True, 'a detonated Failure is marked handled');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
