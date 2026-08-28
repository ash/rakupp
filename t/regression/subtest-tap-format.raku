# Regression: subtest TAP framing (2026-08-28). Test's subtest printed neither
# the "# Subtest: <name>" banner nor — when the body declared no plan — the
# trailing indented plan line ("    1..N") Rakudo emits. Strict TAP consumers
# key nested blocks off exactly those (the TAP module's Sub-Test parser is
# one; it reported such subtests as parse errors). Found writing raku.online's
# TAP page; the expected text below is Rakudo 2026.08's stdout, byte for byte.

my $ok = True;
sub ck($got, $want, $l) { unless $got eqv $want { say "FAIL: $l — {$got.raku} vs {$want.raku}"; $ok = False } }

sub tap-of(Str $code --> Str) {
    my $r = run($*EXECUTABLE.absolute, '-e', $code, :out, :err);
    $r.out.slurp(:close);
}

# banner + up-front plan when the subtest plans
ck tap-of('use Test; plan 1; subtest "inner stuff", { plan 2; ok 1, "one"; ok 1, "two"; };'),
   qq:to/END/, 'planned subtest: banner, indented plan, indented points';
   1..1
   # Subtest: inner stuff
       1..2
       ok 1 - one
       ok 2 - two
   ok 1 - inner stuff
   END

# no plan inside: the trailing plan closes the block; nesting indents both
ck tap-of('use Test; plan 1; subtest "outer", { subtest "inner", { ok 1, "x" } };'),
   qq:to/END/, 'unplanned + nested: trailing plans at each depth';
   1..1
   # Subtest: outer
       # Subtest: inner
           ok 1 - x
           1..1
       ok 1 - inner
       1..1
   ok 1 - outer
   END

# no description: bare "# Subtest" banner, as Rakudo prints it
ck tap-of('use Test; plan 1; subtest { ok 1, "x" };'),
   qq:to/END/, 'descriptionless subtest: bare banner';
   1..1
   # Subtest
       ok 1 - x
       1..1
   ok 1 -\x20
   END

say $ok ?? 'PASS' !! 'FAIL';
exit($ok ?? 0 !! 1);
