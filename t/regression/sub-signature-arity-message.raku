# A sub-signature's arity refusal is worded as Rakudo words it (arity_fail
# in its BOOTSTRAP): the routine that binds it, '<anon>' for a block, the
# counts as for any routine ("1 or 2", "2 to 4", "at least … but got only"),
# then " in sub-signature", "or parameter @x" when the parameter holding it
# has a name (a bare `@` has none), and the phrase again for each level it is
# nested in. It said "Too many positionals passed; expected 2 arguments but
# got 3 in sub-signature", with no routine, "of parameter @x", "expected 1
# argument" where the second was optional, and the phrase once however deep.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

sub refusal(&code) {
    code();
    return 'no refusal';
    CATCH { default { return .message } }
}

sub f(($a, $b)) { }
ck refusal({ f((1, 2, 3)) }), "Too many positionals passed to 'f'; expected 2 arguments but got 3 in sub-signature", 'too many';
ck refusal({ f((1,)) }), "Too few positionals passed to 'f'; expected 2 arguments but got 1 in sub-signature", 'too few';
sub g(@x ($a, $b)) { }
ck refusal({ g((1, 2, 3)) }), "Too many positionals passed to 'g'; expected 2 arguments but got 3 in sub-signature or parameter @x",
    'the parameter holding it';
sub two(@ ($a, $b)) { }
ck refusal({ two([1, 2, 3]) }), "Too many positionals passed to 'two'; expected 2 arguments but got 3 in sub-signature",
    'a bare @ names no parameter';
sub k(($a, $b?)) { }
ck refusal({ k(()) }), "Too few positionals passed to 'k'; expected 1 or 2 arguments but got 0 in sub-signature", 'an optional';
sub u(($a, $b, $c?, $d?)) { }
ck refusal({ u((1,)) }), "Too few positionals passed to 'u'; expected 2 to 4 arguments but got 1 in sub-signature", 'two optionals';
sub s(*@r ($a, *@rest)) { }
ck refusal({ s() }), "Too few positionals passed to 's'; expected at least 1 argument but got only 0 in sub-signature or parameter @r",
    'a slurpy one';
sub t(($a, ($b, $c))) { }
ck refusal({ t((1, (2, 3, 4))) }), "Too many positionals passed to 't'; expected 2 arguments but got 3 in sub-signature in sub-signature",
    'nested';
class K { method m(($a, $b)) { } }
ck refusal({ K.m((1,)) }), "Too few positionals passed to 'm'; expected 2 arguments but got 1 in sub-signature", 'a method';
my $c = -> ($a, $b) { };
ck refusal({ $c((1, 2, 3)) }), "Too many positionals passed to '<anon>'; expected 2 arguments but got 3 in sub-signature", 'a block';
sub w(@x [$a, $b]) { }
ck refusal({ w([1]) }), "Too few positionals passed to 'w'; expected 2 arguments but got 1 in sub-signature or parameter @x", 'brackets';
ck refusal({ f((1, 2)) }), 'no refusal', 'one that fits';

say 'PASS';
