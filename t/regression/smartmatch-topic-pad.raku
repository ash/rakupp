# Regression: `~~` must not leave its left operand as the topic.
#
# `X ~~ Y` topicalizes — `$_` is bound to X while Y is evaluated, so `$x ~~ .so`
# works — and then it has to be PUT BACK. It was, for every `$_` that lived in
# the scope's variable map. A `$_` that is a ROUTINE PARAMETER does not live
# there: it is a pad slot, and `Env::define` writes the pad and erases the map
# twin. The save asked the map, got nothing, concluded there was no local topic;
# the define then clobbered the parameter; and the restore erased a map entry
# that was never there. So the match's LHS stayed the topic for the rest of the
# routine — silently, and only inside a sub whose parameter is named `$_`.
#
# Found in Needle::Compile 0.0.12, whose `handle(Pair:D $_, %_)` does
#
#     my $target := .value<>;
#     $target ~~ List  ??  handle $target, :type(.key), %_
#                      !!  handle .key, .value, %_
#
# and died with "No such method 'key' for invocant of type 'Str'" one line after
# the smartmatch. That is App::Rak's needle compiler, and this was the last
# engine bug between it and its own suite passing.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}

# The topic survives a smartmatch, whatever the right-hand side is — a type, a
# value, a block, a Whatever. (A regex RHS never had the bug; it takes its own
# path, which is why the first reductions all looked fine.)
sub probe($_, $label) {
    my $ignored = "x" ~~ Int;
    check $_.^name, 'Pair', "the topic survives `~~ Int` ($label)";
}
my $pair = :contains("bar");
probe($pair, 'parameter');

for 'Int', 'List', '5', '"x"', '*', '{ .so }' -> $rhs {
    my $p = run($*EXECUTABLE, '-e',
        'sub f($_) { my $i = "x" ~~ ' ~ $rhs ~ '; print $_.^name }; my $q = :c(1); f($q)',
        :out, :err);
    my $out = $p.out.slurp(:close); $p.err.slurp(:close);
    check $out, 'Pair', "a `$rhs` right-hand side leaves the topic alone";
}

# …and the shape the dist actually has: read a value off the topic, smartmatch
# it, then read the topic again.
sub handle-like($_) {
    my $target := .value<>;
    $target ~~ List ?? 'list' !! .key
}
check handle-like($pair), 'contains', 'value, smartmatch, then key — the dist\'s shape';

# The forms that never broke, so a fix that trades one for the other is caught:
# a `my $_` and a `given` topic both live in the map, not the pad.
{
    my $_ = $pair;
    my $i = "x" ~~ Int;
    check $_.^name, 'Pair', 'a `my $_` topic still survives';
}
given $pair {
    my $i = "x" ~~ Int;
    check $_.^name, 'Pair', 'a `given` topic still survives';
}
# An OUTER topic must still be visible after an inner match — the restore has to
# erase its own shadow rather than leave `$_ = Any` hiding it.
given $pair {
    {
        my $i = "x" ~~ Int;
        check $_.^name, 'Pair', 'an outer topic is not shadowed by the restore';
    }
}

if @fail {
    note "FAIL: $_" for @fail;
    say "FAIL: $_" for @fail;
    say "FAIL ({+@fail})";
    exit 1;
}
say "PASS";
