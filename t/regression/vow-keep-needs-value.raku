# A Vow's `.keep` and `.break` take their value, as Rakudo's `method
# keep(Vow:D: \result)` does: `$vow.keep` kept the promise with True. A
# Promise's own `.keep` and `.break` still default to True and "Died".
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my $p = Promise.new;
my $vow = $p.vow;
try $vow.keep;
ck $!.message, 'Too few positionals passed; expected 2 arguments but got 1', '$vow.keep';
try $vow.break;
ck $!.message, 'Too few positionals passed; expected 2 arguments but got 1', '$vow.break';
ck $p.status, Planned, 'the promise is still planned';
$vow.keep(42);
ck $p.result, 42, '$vow.keep(42)';
my $q = Promise.new;
$q.keep;
ck $q.result, True, 'Promise.keep defaults to True (as before)';
my $r = Promise.new;
$r.break;
ck $r.cause.message, 'Died', 'Promise.break defaults to Died (as before)';

say 'PASS';
