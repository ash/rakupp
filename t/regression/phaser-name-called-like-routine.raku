# A phaser's name with a tight paren is a call, of a routine nothing
# declares, as in Rakudo: `FIRST($_ * 10)` ran the FIRST phaser.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}
sub refused($code) { try EVAL $code; $! ?? $!.^name.subst(/'+{' .* $/, '') !! 'ok' }

check refused('for ^3 { FIRST($_ * 10) }'), 'X::Undeclared::Symbols', 'FIRST(…)';
check refused('ENTER(1)'), 'X::Undeclared::Symbols', 'ENTER(…)';
check refused('INIT(1)'), 'X::Undeclared::Symbols', 'INIT(…)';
check refused('for ^2 { LAST(1) }'), 'X::Undeclared::Symbols', 'LAST(…)';
check refused('for ^2 { FIRST (1) }; 1'), 'ok', 'with a space it is the phaser';
check refused('sub POST($u) { $u }; POST(4)'), 'ok', 'a routine of that name the code declares';
my @seen;
for ^2 { FIRST { @seen.push('first') } }
check @seen.join, 'first', 'a FIRST block, as before';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
