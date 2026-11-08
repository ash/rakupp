# An undeclared variable is reported as Rakudo 2026.09 reports it: with the
# names in scope it nearly spells, within a third of its length past the
# sigil (`$baz` finds `$bar`, `$fo` finds no `$foo`), or, with none, "Perhaps
# you forgot a 'sub' if this was intended to be part of a signature?",
# filled to 71 columns. The compile-time report offered no suggestion at all,
# and an EVAL's suggested more than Rakudo's.
# Every line answers the same under Rakudo 2026.09.

use MONKEY-SEE-NO-EVAL;
sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub compiled($code) {
    my $p = run $*EXECUTABLE, '-e', $code, :out, :err;
    $p.err.slurp(:close).lines.grep(none /^ '===SORRY!===' | ^ 'at ' | ^ '------>' | ^ \s* $/).join("\n")
}
my $hint = "Perhaps you forgot a 'sub' if this was\nintended to be part of a signature?";

ck compiled('my $foo = 1; say $fo;'), "Variable '\$fo' is not declared. $hint", 'too near to suggest';
ck compiled('say $x;'), "Variable '\$x' is not declared. $hint", 'nothing near';
ck compiled('my $foo = 1; say $fooo;'), "Variable '\$fooo' is not declared. Did you mean '\$foo'?", 'one more letter';
ck compiled('my $bar = 1; say $baz;'), "Variable '\$baz' is not declared. Did you mean '\$bar'?", 'one other letter';
ck compiled('my $count = 1; say $Count;'), "Variable '\$Count' is not declared. Did you mean '\$count'?", 'a case';
ck compiled('my $foo = 1; say @foo;'), "Variable '\@foo' is not declared. Did you mean '\$foo'?", 'a sigil';
my $foo = 1;
ck do { try EVAL q[say $fo]; $!.message }, "Variable '\$fo' is not declared. $hint", "an EVAL's";
ck do { try EVAL q[say $fooo]; $!.message }, "Variable '\$fooo' is not declared. Did you mean '\$foo'?", "and its suggestion";
ck do { try EVAL q[say $zz]; $!.suggestions.elems }, 0, 'no suggestions';

say 'PASS';
