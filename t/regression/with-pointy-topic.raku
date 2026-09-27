# Regression: a `with`/`given`/`without` whose block is POINTY binds its own
# parameter and leaves `$_` alone (6.c/MISC/bug-coverage.t, 2026-09-27).
# `with 1 -> $a { .flip }` flips the OUTER topic, and `$_ = …` inside assigns
# there; the topic was set to the tested value for every block, pointy or not.
# A plain `else { }` after one still gets the tested value as `$_`, and a
# pointy `else -> $e { }` does not.
#
# Expectations checked against Rakudo 2026.08 via /opt/homebrew/bin/raku — NOT
# the bare name `raku`, which on this box has pointed at rakupp. Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

$_ = 42;
my @seen;
with 1 -> $a { @seen.push: ($_, $a) }
ck(@seen[0], (42, 1), '`with … -> $a` leaves $_ the outer topic');
given 1 -> $a { @seen.push: ($_, $a) }
ck(@seen[1], (42, 1), '`given … -> $a` as well');
with 1 { @seen.push: $_ }
ck(@seen[2], 1, 'a plain `with` block topicalizes');
with 1 -> $_ { @seen.push: $_ }
ck(@seen[3], 1, '…as does one whose parameter IS $_');
without Int -> $u { @seen.push: ($_, $u) }
ck(@seen[4], (42, Int), '`without … -> $u` leaves $_ alone too');
with 5 -> $a { $_ = 7 }
ck($_, 7, '`$_ = …` in the pointy block assigns the outer $_');
$_ = 42;
ck((with 1 -> $a { .flip }), '24', '`.flip` in it is the outer topic\'s');
with Int -> $a { } else { @seen.push: $_ }
ck(@seen[5], Int, 'a plain else sees the tested value');
with Int -> $a { } else -> $e { @seen.push: ($_, $e) }
ck(@seen[6], (42, Int), 'a pointy else does not');

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
