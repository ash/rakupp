# Regression: issue #162 (2026-11-10).
#
# In an `--exe` binary, `for (b => 2,) -> (:$key, :$value) { }` bound Nil to
# both names — at the top level, in a sub and in a module routine — where the
# interpreter and Rakudo bind "b" and 2. The compiled loop took each name of
# the sub-signature by POSITION from the element; a named one binds from the
# element's accessor (a Pair's .key and .value, an object's attribute), as
# the interpreter's binder has it (Interpreter::subSigCapture), and a Hash
# binds its keys.
#
# Meant to be compiled: t/run.raku builds it natively and its binary must
# print PASS, and t/exe/run.raku compares its -O binary with the interpreter.
# Expected answers are Rakudo 2026.09's (2026-11-10).
# Contract: exit 0 + last line PASS.

my $fails = 0;
sub check($got, $want, $what) {
    return if $got eqv $want;
    $fails++;
    say "not ok - $what\n  got:  {$got.raku}\n  want: {$want.raku}";
}

sub one() { my @r; for (b => 2,) -> (:$key, :$value) { @r.push: ($key, $value) }; @r.List }
check one(), (('b', 2),), 'one Pair through -> (:$key, :$value)';
sub many() { my @r; for (a => 1, b => 2) -> (:$key, :$value) { @r.push: "$key=$value" }; @r.join(',') }
check many(), 'a=1,b=2', 'two Pairs';
sub of-hash() { my %h = x => 10, y => 20; my @r; for %h.sort -> (:$key, :$value) { @r.push: $key ~ $value }; @r.join(',') }
check of-hash(), 'x10,y20', 'a sorted Hash';
sub renamed() { my @r; for (k => 'v',) -> (:key($k), :value($v)) { @r.push: "$k $v" }; @r.join }
check renamed(), 'k v', ':key($k), :value($v)';
sub typed() { my @r; for (t => 5,) -> (Str :$key, Int :$value) { @r.push: "$key $value" }; @r.join }
check typed(), 't 5', 'typed named parameters';
sub interp() { my @r; for (b => 2,) -> (:$key, :$value) { @r.push: "signature: $key.raku() $value.raku()" }; @r.join }
check interp(), 'signature: "b" 2', 'the issue line';

class P { has $.x; has $.y }
sub of-object() { my @r; for (P.new(x => 1, y => 2),) -> (:$x, :$y) { @r.push: "$x $y" }; @r.join }
check of-object(), '1 2', "an object's attributes";
sub of-hashes() { my @r; for ({ k => 9 },) -> (:$k) { @r.push: $k }; @r.join }
check of-hashes(), '9', "a Hash's key";
sub positional() { my @r; for ((1, 2), (3, 4)) -> ($a, $b) { @r.push: "$a$b" }; @r.join(',') }
check positional(), '12,34', 'a positional sub-signature, as before';

my @top; for (c => 3,) -> (:$key, :$value) { @top.push: ($key, $value) }
check @top.List, (('c', 3),), 'at the top level';
my @top2; for %(q => 1).pairs -> (:$key, :$value) { @top2.push: "$key $value" }
check @top2.join, 'q 1', "a Hash's .pairs at the top level";

if $fails { say "$fails failure(s)"; exit 1 }
say 'PASS';
