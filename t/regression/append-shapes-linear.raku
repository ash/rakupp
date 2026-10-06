# Regression: every string-building shape of issue #130 stays linear. The same
# cases as tools/bench/append-shapes.raku, which prints the timings. The last
# three — prepending, `$k = $s` before each append, repeated .substr — need the
# old text kept while a new one is made from it, which a flat string can only
# do by copying; they are linear since strings may be views of a shared buffer
# (docs/dev/plans/APPEND-PLAN.md).
#
# The output carries no timings: the --jit/--cnp and --exe gates compare it
# byte for byte between lanes. Each case runs at 20k and at 160k steps, best of
# three: eight times the work takes about eight times as long when the case is
# linear and about sixty-four when it is quadratic. A case fails only when its
# growth is past 24x AND the long run took 100 ms or more, so load on a busy
# machine slows both sizes without turning a linear case red, and a case too
# fast to time cannot fail on noise.

my $fails = 0;
sub ck($ok, $desc) {
    if $ok { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc" }
}

class Acc { has Str $.b is rw = ''; method add($x) { $!b ~= $x } }
sub add-rw($x is rw, $y) { $x ~= $y }

my @cases =
    'my, Str'                => -> $n { my $s = ''; $s ~= 'abcde' for ^$n; $s },
    'my, Int.Str'            => -> $n { my $s = ''; $s ~= .Str for ^$n; $s },
    'my, Int'                => -> $n { my $s = ''; $s ~= $_ for ^$n; $s },
    'my, Num'                => -> $n { my $s = ''; $s ~= 1.5e0 for ^$n; $s },
    'my, Bool'               => -> $n { my $s = ''; $s ~= True for ^$n; $s },
    'closure, Str'           => -> $n { my $s = ''; my &f = { $s ~= 'abcde' }; f() for ^$n; $s },
    '$!attr, Str'            => -> $n { my $o = Acc.new; $o.add('abcde') for ^$n; $o.b },
    '$!attr, Int'            => -> $n { my $o = Acc.new; $o.add($_) for ^$n; $o.b },
    '$s = $s ~ "abcde"'      => -> $n { my $s = ''; $s = $s ~ 'abcde' for ^$n; $s },
    '$s = $s ~ X, no kernel' => -> $n { my $s = ''; for ^$n { $s = $s ~ 'abcde'; $s.say if False }; $s },
    'is rw param, Str'       => -> $n { my $s = ''; add-rw($s, 'abcde') for ^$n; $s },
    '$s = "abcde" ~ $s'      => -> $n { my $s = ''; $s = 'abcde' ~ $s for ^$n; $s },
    'prepend, no kernel'     => -> $n { my $s = ''; for ^$n { $s = "abcde" ~ $s; $s.say if False }; $s },
    '$k = $s; $s ~= "abcde"' => -> $n { my $s = ''; my $k; for ^$n { $k = $s; $s ~= 'abcde' }; $s },
    '$s = $s.substr(5)'      => -> $n { my $s = 'abcde' x $n; $s = $s.substr(5) while $s.chars; $s };

sub ms(&case, $n) {
    my $best = Inf;
    for ^3 { my $t = now; case($n); $best min= (now - $t) * 1000 }
    $best
}

for @cases -> $c {
    my $name = $c.key; my &case = $c.value;
    my $short = ms(&case, 20_000);
    my $long  = ms(&case, 160_000);
    my $quadratic = $long >= 100 && $long / max($short, 0.01) >= 24;
    ck(!$quadratic, "$name: linear");
    note "  $name: {$short.fmt('%.1f')} ms at 20k, {$long.fmt('%.1f')} ms at 160k" if $quadratic;
}

say $fails ?? "FAILED $fails" !! "PASS";
