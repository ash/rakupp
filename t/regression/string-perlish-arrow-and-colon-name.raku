# Regression (issue #109, rows B and C): inside a string, `->` straight after an
# interpolated variable is Perl's arrow (X::Obsolete), and `$h:word` continues
# the variable's NAME (`$h:Set<1>`, undeclared) — both refused, as Rakudo does.
# The near misses still interpolate. Contract: exit 0 + last line PASS.
use MONKEY-SEE-NO-EVAL;
my @fail;
for q[my ($a, $b) = 'A', 'B'; "$a->$b"], q[my @a = 1; "@a[0]->x"], q[my $a = 1; "$a->[0]"] -> $c {
    try EVAL $c;
    @fail.push("not Obsolete: $c") unless $! ~~ X::Obsolete;
}
for q[my $h = 'H'; "$h:Set(1)"], q[my $h = 'H'; "$h:foo"] -> $c {
    try EVAL $c;
    @fail.push("not Undeclared: $c") unless $! ~~ X::Undeclared;
}
my ($a, $b, $h) = 'A', 'B', 'H';
@fail.push('spaced arrow') unless "$a -> $b" eq 'A -> B';
@fail.push('long arrow')   unless "$a-->$b" eq 'A-->B';
@fail.push('colon space')  unless "$h: x" eq 'H: x';
@fail.push('colon var')    unless "$h:$a" eq 'H:A';
@fail.push('braced')       unless "{$h}:foo" eq 'H:foo';
.say for @fail;
say @fail ?? 'FAIL' !! 'PASS';
