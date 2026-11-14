# `.=` into a `$` variable itemizes what it stores, as `=` does (rakudo#3621):
# `$x .= split(',')` holds `$(…)`, so `.raku` shows the container and the
# value is one thing in a later list or loop.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

my $x = '1,2,,3';
$x .= split(',');
check $x.raku, '$(("1", "2", "", "3").Seq)', 'a Seq';
my $w = 'a b'; $w .= words;
check (my @a = $w, 1).elems, '2', 'one element of a later list';
my $n = 0; $n++ for $w;
check $n, '1', 'one pass of a loop';
my $h = {a => 1}; $h .= Hash;
check $h.raku, '${:a(1)}', 'a Hash';
my $z = (1, 2); $z .= Array;
check $z.raku, '$[1, 2]', 'an Array';
my $r = 1..3; $r .= minmax;
check $r.raku, '$(1, 3)', 'a List from a method';
my $e = 3; $e .= Str;
check $e.raku, '"3"', 'an item stays an item';
my @b = 'e f'; @b[0] .= words;
check @b.raku, '[("e", "f").Seq,]', 'an Array element is not a `$` variable';
my @c = <p q>; .=uc for @c;
check @c.raku, '["P", "Q"]', 'the topic still writes through';
my @d = 1, 2; with @d { $_ .= reverse };
check @d.raku, '[2, 1]', 'a topic bound to an Array list-assigns';
my @e = 1, 2; given @e { .=reverse };
check @e.raku, '[2, 1]', '…under given too';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
