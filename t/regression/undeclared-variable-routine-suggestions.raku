# An undeclared variable's suggestions take in the routines in scope, the
# core's and the program's own, as Rakudo's suggest_lexicals does, scored by
# its Levenshtein distance (a sigil for another costs 0.5, two letters
# switched 1): `$prnt` suggests `&print`, `$nope` `&none` and `&note`, and
# `$frob` beside `sub frob` `&frob`, where each got the "Perhaps you forgot a
# 'sub'" hint. `.suggestions` is an Array, as Rakudo's.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub sugg($code) { try EVAL $code; $!.suggestions }

ck sugg(q[say $prnt]).List, ('&print',), 'a core routine';
ck sugg(q[say $nope]).sort.List, <&none &note>, 'two of them';
ck sugg(q[say $mapp]).List, ('&map',), 'map';
ck sugg(q[say $EVLA]).List, ('&EVAL',), 'two letters switched';
sub frob { }
ck sugg(q[say $frob]).List, ('&frob',), "the program's own";
ck sugg(q[say $sya]).List, (), 'too far: none';
ck sugg(q[say $prnt]).^name, 'Array', 'an Array';
ck (try EVAL q[say $prnt]) // $!.message, "Variable '\$prnt' is not declared. Did you mean '&print'?", 'the message';
my $foo = 1;
ck (sugg(q[say $fooo]).List, sugg(q[say $fo]).List), (('$foo',), ()), 'variables, as before';

say 'PASS';
