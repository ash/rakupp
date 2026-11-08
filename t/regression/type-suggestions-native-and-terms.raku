# A misspelled type's suggestions take in the native types and the numeric
# terms, as Rakudo's do from its CORE: `class C is Strr {}` suggested Str,
# where Rakudo suggests Str and str, and `Iny` Any and Int, where Rakudo adds
# Inf. (Rakudo's order varies from run to run; the set does not.)
# Every line answers the same under Rakudo 2026.09.

use MONKEY-SEE-NO-EVAL;
sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub sugg($parent) { try EVAL "class C is $parent \{}"; $!.suggestions.sort.List }

ck sugg('Strr'), <Str str>, 'Strr';
ck sugg('Iny'), ('Any', 'Inf', 'Int'), 'Iny';
ck sugg('Num1'), <Num num>, 'Num1';
ck sugg('Nan'), ('NaN',), 'Nan';
ck sugg('Int64'), <int64 uint64>, 'Int64';
ck sugg('Rap'), <Map Rat Tap>, 'Rap, as before';

say 'PASS';
