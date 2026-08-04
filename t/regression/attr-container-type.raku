# Regression: two fixes found taking DBIish and Config further, 2026-08-04.
# Both are general; the modules only showed where to look. Checked against
# Rakudo.

my $ok = True;
sub ck($got, $want, $l) { unless $got eqv $want { say "FAIL: $l — {$got.raku} vs {$want.raku}"; $ok = False } }

# 1. `has %.x is SomeType` makes the attribute an INSTANCE of that type, not a
#    plain Hash. Only the QuantHash family was recognised, so DBIish's
#    `has %.Converter is DBDish::TypeConverter` stayed a Hash and `.convert` on
#    it was "No such method".
{
    my class C does Associative { has %!s handles <AT-KEY>; method m() { 'C' } }
    my role  R does Associative { has %!s handles <AT-KEY>; method m() { 'R' } }
    my class H {
        has %.c is C;
        has %.r is R;
        has %.q is Set;     # the QuantHash family still works
        has %.plain;        # and an untraited hash is untouched
    }
    my $h = H.new;
    ck(($h.c.^name, $h.r.^name), ('C', 'R'), 'a class and a role both instantiate');
    ck(($h.c.m, $h.r.m), ('C', 'R'), 'and their methods are reachable');
    ck($h.q.^name, 'Set', 'is Set is unchanged');
    ck($h.plain.^name, 'Hash', 'and a plain hash attribute stays a Hash');
}

say $ok ?? 'PASS' !! 'FAIL';
