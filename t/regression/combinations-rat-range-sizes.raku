# Regression (RAKUDO-ISSUES-PLAN R5; rakudo#1629): `.combinations` over a
# non-Int Range of sizes takes them as Rakudo does: from
# `(min + excludes-min).Int`, to `(max - excludes-max).Int`, or every size
# when max is past the list's.
#
# Before the fix `<a b c>.combinations(2.3..^3.6)` dropped (a b c): the
# exclusive 3.6 was read as an Int bound and lost size 3, where Rakudo's max
# is past the three elements and takes every size up to 3.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check(Str $what, $got, Str $want) {
    @fail.push("$what: got {$got.gist}, want $want") unless $got.gist eq $want;
}
check '2.3..^3.6 over three (rakudo#1629)', <a b c>.combinations(2.3..^3.6), '((a b) (a c) (b c) (a b c))';
check '1.5..2.5', <a b c d>.combinations(1.5..2.5).map(*.elems).unique, '(1 2)';
check '1.5..^2.5', <a b c d>.combinations(1.5..^2.5).map(*.elems).unique, '(1)';
check '0.9..1.1', <a b c d>.combinations(0.9..1.1).map(*.elems).unique, '(0 1)';
check 'an Int range, as before', (<a b c>.combinations(2..^3), <a b c>.combinations(1^..3).elems), '(((a b) (a c) (b c)) 4)';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
