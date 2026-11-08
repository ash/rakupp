# A keyword written tight against `(` calls a `&loop` PARAMETER of that name,
# as it already called a `sub loop` or a `my &loop`: `sub count(&loop) {
# loop({ $n++ }) }` died "Malformed loop spec (expected 3 parts separated by
# semicolon, got 1)", where Rakudo calls the block. Without a routine of the
# name, `loop(…) { … }` is refused as `if(…) { … }` already was: Rakudo reads
# it as a call of `loop()` too.
# Every line answers the same under Rakudo 2026.09.

use MONKEY-SEE-NO-EVAL;
sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

sub count(&loop) { my $n = 0; loop({ $n++ }); loop({ $n++ }); $n }
ck count(-> &b { b() }), 2, '&loop parameter';
sub w(&while) { while(3) }
ck w(-> $x { "w$x" }), 'w3', '&while parameter';
sub f(:&for) { for(2) }
ck f(for => -> $x { "f$x" }), 'f2', 'a named :&for parameter';
my $p = -> &if { if(5) };
ck $p(-> $x { $x * 2 }), 10, 'a pointy block\'s &if';

try EVAL 'loop(my $i = 0; $i < 2; $i++) { }';
ck $!.^name, 'X::Comp::Group', 'loop(…) { } with no &loop is refused';
my @seen;
loop (my $i = 0; $i < 2; $i++) { @seen.push($i) }
ck @seen, [0, 1], 'loop (…) with a space (as before)';
my $j = 0;
loop { last if ++$j > 2 }
ck $j, 3, 'a bare loop (as before)';

say 'PASS';
