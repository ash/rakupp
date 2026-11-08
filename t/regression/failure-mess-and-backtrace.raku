# A Failure answers `.backtrace`, where it was made, and `.mess`, its message
# and that backtrace, as Rakudo's Failure does: both made the unhandled
# Failure throw. An exception never thrown has no backtrace (`.backtrace` is
# Nil, a Failure's own exception too), where rakupp made one where it was
# asked. And a type check that refuses a Failure opens with the Failure's
# `.mess`, so a `fail` in a sub names that sub's frame.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
my $file = $?FILE.IO.basename;
sub frames($s) { $s.subst(/\S* $file/, 'FILE', :g) }

sub fi { fail "inner" }
my $g = fi();
ck frames($g.mess), "inner\n  in sub fi at FILE line 17\n  in block <unit> at FILE line 18\n", '.mess';
ck $g.backtrace.^name, 'Backtrace', '.backtrace';
ck frames($g.backtrace.Str), "  in sub fi at FILE line 17\n  in block <unit> at FILE line 18\n", 'its frames';
ck $g.exception.backtrace, Nil, "the exception's, never thrown";
ck $g.handled, False, 'none of them handles it';
$g.so;
ck X::AdHoc.new(payload => "x").backtrace, Nil, 'an exception never thrown';
my $e = X::AdHoc.new(payload => "x"); try $e.throw;
ck $e.backtrace.list.elems > 0, True, 'one thrown';
sub f(Str $s) { }
sub fail-it { fail "inner" }
try f(fail-it());
ck frames($!.message),
   "Earlier failure:\n inner\n  in sub fail-it at FILE line 29\n  in block <unit> at FILE line 30\n\nFinal error:\n Type check failed in binding to parameter '\$s'; expected Str but got Failure (Failure.new(exceptio...)",
   'a type check that refuses one';

say 'PASS';
