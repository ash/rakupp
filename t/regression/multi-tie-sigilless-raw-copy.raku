# Regression (RAKUDO-ISSUES-PLAN R4; rakudo#1286): two multi candidates that
# each win a position, or that are the same, are ambiguous whether their
# parameters are `$a`, sigilless `\a`, `is raw` or `is copy`.
#
# Before the fix a sigilless parameter took the candidate out of the
# ambiguity check, and so did a difference in `is raw` or `is copy`:
# `multi f(Int \a, Any \b)` beside `multi f(Any \a, Int \b)` quietly took the
# first for `f(42, 42)`, where Rakudo dies X::Multi::Ambiguous. `is rw` still
# tells candidates apart, by whether the argument is writable.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub ambiguous(Str $what, &code) {
    my $name = 'nothing';
    try { code(); CATCH { default { $name = .^name } } }
    @fail.push("$what: got $name, want X::Multi::Ambiguous") unless $name eq 'X::Multi::Ambiguous';
}
sub check(Str $what, $got, $want) {
    @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

multi f1(Int \a, Any \b) { 1 }
multi f1(Any \a, Int \b) { 2 }
ambiguous 'sigilless, each winning a position (rakudo#1286)', { f1(42, 42) };
check 'sigilless, one match each', (f1(1, 'x'), f1('x', 1)), (1, 2);

multi f2(Int \a) { 1 }
multi f2(Int $a) { 2 }
ambiguous '\a beside $a', { f2(1) };
multi f3(Int $a is raw) { 1 }
multi f3(Int $b) { 2 }
ambiguous 'is raw beside plain', { f3(1) };
multi f4(Int $a is copy) { 1 }
multi f4(Int $b) { 2 }
ambiguous 'is copy beside plain', { f4(1) };
class C { multi method m(Int \a) { 1 }; multi method m(Int $a) { 2 } }
ambiguous 'multi methods', { C.new.m(1) };

# what still decides
multi g1(Int $a is rw) { 1 }
multi g1(Int $b) { 2 }
my $x = 1;
check 'is rw takes a writable argument', g1($x), 1;
check '…and the other takes a value', g1(1), 2;
multi g2(Int \a where * > 0, Any \b) { 1 }
multi g2(Any \a, Int \b where * < 100) { 2 }
check 'a where clause: the first that binds', g2(42, 42), 1;
multi g3(\a) is default { 1 }
multi g3($a) { 2 }
check 'is default', g3(1), 1;
multi g4(Int \a) { 1 }
multi g4(Str \a) { 2 }
check 'different types', (g4(1), g4('x')), (1, 2);

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
