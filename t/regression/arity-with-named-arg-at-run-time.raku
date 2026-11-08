# A call that passes a named argument and the wrong number of positionals
# is refused when it runs, in the run's words, as Rakudo's compile-time
# check passes over any call with a named argument: `sub g(@l) { };
# g(a => 1)` said "Calling g() will never work with declared signature
# (@l)", Rakudo's compile-time wording, where Rakudo says "Too few
# positionals passed; expected 1 argument but got 0". A call with no named
# argument still gets the compile-time wording, and an `@` parameter is
# counted as one (it was "expected at least 1 arguments").
# Every line answers the same under Rakudo 2026.09.

use MONKEY-SEE-NO-EVAL;
sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub msg($code) { try EVAL $code; $!.message }

ck msg('sub g(@l) { }; g(a => 1)'), 'Too few positionals passed; expected 1 argument but got 0', 'an @ given a named';
ck msg('sub g($x) { }; g(:a)'), 'Too few positionals passed; expected 1 argument but got 0', 'a $ given a named';
ck msg('sub g($x, $y) { }; g(1, a => 2)'), 'Too few positionals passed; expected 2 arguments but got 1', 'one short';
ck msg('sub g($x) { }; g(1, 2, :a)'), 'Too many positionals passed; expected 1 argument but got 2', 'one over';
ck msg('sub g(:$a) { }; g(1, :a)'), 'Too many positionals passed; expected 0 arguments but got 1', 'none wanted';
ck msg('sub g($x, :$a) { }; g(:a)'), 'Too few positionals passed; expected 1 argument but got 0', 'its own named';
ck msg('sub g(@l, $x?) { }; g(:a)'), 'Too few positionals passed; expected 1 or 2 arguments but got 0', 'an @ and an optional';
ck msg('sub g($x, *@r) { }; g(:a)'), 'Too few positionals passed; expected at least 1 arguments but got only 0', 'a slurpy';
ck msg('sub g($x) { }; g(1, 2)'), 'Calling g(Int, Int) will never work with declared signature ($x)', 'no named';

say 'PASS';
