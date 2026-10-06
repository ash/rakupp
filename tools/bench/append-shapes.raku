# Every string-building shape from issue #130 in one table: which stay linear?
#
#   rakupp tools/bench/append-shapes.raku                                  # interpreted
#   rakupp --exe -o /tmp/append-shapes tools/bench/append-shapes.raku && /tmp/append-shapes
#   rakudo tools/bench/append-shapes.raku                                  # the reference
#
# Each case runs at 10k, 20k and 40k steps (best of two). Doubling the work
# doubles a linear case's time and quadruples a quadratic one's, so the last
# column reads the 20k -> 40k growth: about 2x is linear, 3-4x quadratic (a
# copy per step on top of the step's own work lands between the two).
# Cases too fast to time reliably (under 10 ms at 40k) count as linear.

class Acc { has Str $.b is rw = ''; method add($x) { $!b ~= $x } }
sub add-rw($x is rw, $y) { $x ~= $y }

my @cases =
    # `~=` on the right-hand sides and the containers the issue names
    'my, Str'                => -> $n { my $s = ''; $s ~= 'abcde' for ^$n; $s },
    'my, Int.Str'            => -> $n { my $s = ''; $s ~= .Str for ^$n; $s },
    'my, Int'                => -> $n { my $s = ''; $s ~= $_ for ^$n; $s },
    'my, Num'                => -> $n { my $s = ''; $s ~= 1.5e0 for ^$n; $s },
    'my, Bool'               => -> $n { my $s = ''; $s ~= True for ^$n; $s },
    'is rw param, Str'       => -> $n { my $s = ''; add-rw($s, 'abcde') for ^$n; $s },
    'closure, Str'           => -> $n { my $s = ''; my &f = { $s ~= 'abcde' }; f() for ^$n; $s },
    '$!attr, Str'            => -> $n { my $o = Acc.new; $o.add('abcde') for ^$n; $o.b },
    '$!attr, Int'            => -> $n { my $o = Acc.new; $o.add($_) for ^$n; $o.b },
    # building or consuming a string without `~=`
    '$s = $s ~ "abcde"'      => -> $n { my $s = ''; $s = $s ~ 'abcde' for ^$n; $s },
    '$s = "abcde" ~ $s'      => -> $n { my $s = ''; $s = 'abcde' ~ $s for ^$n; $s },
    '$k = $s; $s ~= "abcde"' => -> $n { my $s = ''; my $k; for ^$n { $k = $s; $s ~= 'abcde' }; $s },
    '$s = $s.substr(5)'      => -> $n { my $s = 'abcde' x $n; $s = $s.substr(5) while $s.chars; $s };

sub ms(&case, $n) {
    my $best = Inf;
    for ^2 { my $t = now; case($n); $best min= (now - $t) * 1000 }
    $best
}

my $engine = $*RAKU.compiler.name ~ ' ' ~ $*RAKU.compiler.version;
say "engine: $engine";
say '';
printf "%-24s %8s %8s %8s   %7s  %s\n", 'case', '10k', '20k', '40k', '20k→40k', 'verdict';
say '-' x 72;
my ($lin, $quad) = 0, 0;
for @cases -> $c {
    my $name = $c.key; my &case = $c.value;
    my @t = (10_000, 20_000, 40_000).map: { ms(&case, $_) };
    my $growth = @t[2] / max(@t[1], 0.001);
    my $verdict = @t[2] < 10       ?? 'linear (fast)'
               !! $growth < 2.6    ?? 'linear'
               !! $growth >= 2.9   ?? 'QUADRATIC'
               !!                     'unclear';
    $verdict.starts-with('linear') ?? $lin++ !! $verdict eq 'QUADRATIC' ?? $quad++ !! Nil;
    printf "%-24s %6.1fms %6.1fms %6.1fms   %6.1fx  %s\n", $name, |@t, $growth, $verdict;
}
say '-' x 72;
say "linear: $lin   quadratic: $quad   of {+@cases}";
