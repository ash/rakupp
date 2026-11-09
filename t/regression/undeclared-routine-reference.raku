# A routine named by its `&` and declared nowhere is refused at compile time,
# as a call to it is and as Rakudo refuses it: `say "hi"; say &nope` printed
# hi and then died "Undefined routine 'nope'", where Rakudo prints nothing
# and reports "Undeclared routine: nope used at line 1" with its
# suggestions. `&say`, a `my &g`, a sub declared later and an operator's
# `&infix:<+>` are as before.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub child($code) {
    my $p = run $*EXECUTABLE, '-e', $code, :out, :err;
    ($p.out.slurp(:close), $p.err.slurp(:close).lines.head(3).List)
}

ck child(q[say "hi"; my $f = &prnt; say $f]),
   ('', ('===SORRY!=== Error while compiling -e', 'Undeclared routine:', "    prnt used at line 1. Did you mean 'print'?")),
   'refused before anything runs';
my ($out, $err) = child(q[say "hi"; say &nope]);
ck ($out, $err[1], $err[2].starts-with('    nope used at line 1. Did you mean')), ('', 'Undeclared routine:', True), 'another';
my &g = &uc;
sub later { 5 }
ck (&say.name, g("a"), &later(), &infix:<+>(1, 2)), ('say', 'A', 5, 3), 'what is declared, as before';

say 'PASS';
