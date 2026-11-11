# Regression: issue #97 (2026-11-10).
#
#   `my $a = Array.new; for $a -> $i { ... }` was reported to add an extra
#   layer. An itemized Array, List or [] in a scalar is ONE item to `for`, so
#   the loop runs once with the container itself, and its .raku keeps the `$`.
#
# Expected answers are Rakudo 2026.09's (2026-11-10).

my $fails = 0;
sub check($got, $want, $what) {
    return if $got eqv $want;
    $fails++;
    say "not ok - $what\n  got:  {$got.raku}\n  want: {$want.raku}";
}

sub passes($x) { my @seen; for $x -> $i { @seen.push: $i.raku }; @seen.List }

my $a = Array.new; my $b = (); my $c = []; my $d = [1, 2];
check $a.raku, '$[]',  'Array.new in a scalar';
check $b.raku, '$( )', '() in a scalar';
check $c.raku, '$[]',  '[] in a scalar';
check passes($a), ('$[]',),  'for over Array.new runs once, with the Array';
check passes($b), ('$( )',), 'for over () in a scalar runs once';
check passes($c), ('$[]',),  'for over [] runs once';
check passes($d), ('$[1, 2]',), 'for over a non-empty itemized Array runs once';
my @e;
check (my @r = do for @e -> $i { $i }).elems, 0, 'an @ empty Array runs no pass';
check (do for @$a -> $i { $i }).elems, 0, '@$a decontainerizes: no pass';
check (do for $a<> -> $i { $i }).elems, 0, '$a<> decontainerizes: no pass';
check (do for $d<> -> $i { $i }).List, (1, 2), '$d<> iterates the elements';
my $s = "x"; my $t = 'x';
check passes([$s, $t]), ('$["x", "x"]',), 'both quote kinds inside';

if $fails { say "$fails failure(s)"; exit 1 }
say 'PASS';
