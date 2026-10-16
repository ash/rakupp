# Regression: a routine with a signature, and every method, read and WROTE the
# mainline's `$_`.
#
#   $_ = 'outer';
#   sub f($v) { $_ = 'f' }; f(1); say $_;   # Rakudo: outer; rakupp: f
#
# Rakudo gives every routine a `$_` of its own: its `$_` parameter, or a fresh
# undefined one. rakupp did that only for a sub with no signature at all; a sub
# with parameters and every method found the mainline's (or the enclosing
# scope's) instead. bindRoutineParams now gives one, and a method with no
# parameters gets one too. A pointy block keeps seeing the outer `$_`, as in
# Rakudo. Only a routine whose body can touch the topic pays for defining it
# (bodyMayUseTopic): a method call cost ~14% more when every call did.
#
# Found while making --exe leave `$_` alone in a pointy `for` (compiled code
# now gives routines their own topic as well). Every expectation was checked
# against Rakudo 2026.09.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — got {$got.raku} want {$want.raku}" }
}

$_ = 'outer';

sub with-param($v) { $_ // 'U' }
sub writes-param($v) { $_ = 'w'; $_ }
sub topic-param($_) { $_ }
sub no-sig { $_ // 'U' }
ck with-param(1), 'U', 'a sub with a signature reads its own $_';
ck writes-param(1), 'w', '…writes it';
ck $_, 'outer', "…and the mainline's is left alone";
ck topic-param('p'), 'p', 'a $_ parameter IS the topic';
ck no-sig(), 'U', 'a sub without one (always was)';

class K {
    method m0 { $_ // 'U' }
    method m1($v) { $_ // 'U' }
    method w { $_ = 'mw'; $_ }
    method t($_) { $_ }
    submethod s { $_ // 'U' }
    method uses-topic-in-a-loop(@xs) { my @r; for @xs { @r.push: .uc }; @r.join }
    method no-topic($x) { $x * 2 }
}
ck K.m0, 'U', 'a method without parameters';
ck K.m1(1), 'U', 'a method with one';
ck K.w, 'mw', 'a method writes its own';
ck $_, 'outer', "…and not the mainline's";
ck K.t('tp'), 'tp', "a method's \$_ parameter";
ck K.s, 'U', 'a submethod';
ck K.uses-topic-in-a-loop(<a b>), 'AB', 'a loop in a method still topicalizes';
ck K.no-topic(21), 42, 'a method that never mentions $_';

{
    $_ = 'block';
    my &pointy = -> $v { $_ };
    ck pointy(1), 'block', 'a pointy block still sees the outer $_';
    my &bare = { $_ };
    ck bare(7), 7, 'a bare block binds it';
    $_ = 'outer';
}

for 1, 2 -> $x {
    sub in-loop { $_ // 'U' }
    ck in-loop(), 'U', 'a sub declared in a loop does not see its topic';
    last;
}

say $fails == 0 ?? 'PASS' !! 'FAIL';
exit $fails == 0 ?? 0 !! 1;
