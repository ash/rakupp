# Regression: a method invocant (bare type / qualified / with :D smiley) must not
# be confused with a named-parameter alias or a `:$named` marker when the first
# real parameter is a coercion type (`Str()`) or a plain positional variable.
# Surfaced by the URI module (URI.rakumod: `method parse(URI:D: Str() $str, ...)`,
# URI/Query.rakumod: `method ASSIGN-POS(URI::Query:D: $i, Pair $p)`).
my $ok = 0; my $n = 0;
sub ck($got, $want, $desc) {
    $n++;
    if $got eqv $want { $ok++ }
    else { say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

class Q {}
class C {
    # invocant (:D smiley) + coercion-type first param — the `Str()` empty parens
    # must not be read as a named-alias `:Str(...)`
    method a(C:D: Str() $s) { $s.chars }
    # bare-type invocant + coercion
    method b(C: Str() $s) { $s.uc }
    # qualified-name invocant (:D) + positional Var, then another positional —
    # the invocant colon must not be read as a `:$i` named marker, and it must
    # bind to self rather than eating the first argument
    method c(C:D: $i, Int $p) { $i + $p }
    # invocant + positional Var only
    method d(C:D: $x) { $x * 2 }
    # genuine named params must still parse as named
    method e(Int :$x, Int :$y) { $x + $y }
    # genuine named alias must still parse
    method f(Int :count($n)) { $n }
}

my $c = C.new;
ck($c.a("héllo"), 5, 'invocant :D + Str() coercion');
ck($c.b("hi"), "HI", 'bare-type invocant + Str() coercion');
ck($c.c(3, 4), 7, 'invocant :D binds self + positional Var + positional');
ck($c.d(21), 42, 'invocant :D + single positional Var');
ck($c.e(:x(5), :y(6)), 11, 'named params after nothing still named');
ck($c.f(:count(9)), 9, 'named alias still parses');

say $ok == $n ?? 'PASS' !! "FAIL ($ok/$n)";
