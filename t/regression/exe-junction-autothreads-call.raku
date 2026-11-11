# --exe: a Junction argument autothreads a call of a natively compiled
# routine, as the interpreter's callCallable does. Compiled, `sub f($v)` got
# the junction itself: `f(any(1, 2))` answered for the Junction, and Cro's
# `sub extract($v --> Nil) { @constraints.push($v) }`, handed a parameter's
# `all(…)` constraints, pushed the junction, so a route with a literal segment
# never matched (404). A parameter typed Mu or Junction, a native, a slurpy,
# and an untyped parameter of a block take the junction whole. (Not checked
# here: a slurpy's junction, which the interpreter flattens into its
# eigenstates where Rakudo keeps the Junction.)
# Checked both ways by t/exe/parity.raku.
# Contract: exit 0 + last line PASS.

my @fail;
sub check(Mu $got, Mu $want, $what) { @fail.push("$what: got {$got.gist}, want {$want.gist}") unless $got.gist eq $want.gist }

sub top($v) { $v.^name }
check top(any(1, 2)), any('Int', 'Int'), 'a sub threads an untyped parameter';
sub typed(Int $v) { $v + 1 }
check typed(1 | 2), any(2, 3), '…and a typed one';
sub mu(Mu $v) { $v.^name }
check mu(1 | 2), 'Junction', 'a Mu parameter takes it whole';
sub two($a, $b) { "$a$b" }
check two(1 | 2, 'x' & 'y'), all(any('1x', '2x'), any('1y', '2y')), 'an all junction threads first';

my &anon = sub ($v) { $v * 10 };
check anon(1 | 2), any(10, 20), 'an anonymous sub threads';
my &blk = -> $v { $v.^name };
check blk(1 | 2), 'Junction', "a block's untyped parameter is Mu";
my &tblk = -> Int $v { $v * 2 };
check tblk(1 | 2), any(2, 4), "…a typed one threads";

sub outer(Int $x) {
    my @got;
    sub inner($v --> Nil) { @got.push($v) }
    inner(all($x, $x + 1));
    @got
}
check outer(1), [1, 2], 'a lexical sub threads (the Cro shape)';

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
