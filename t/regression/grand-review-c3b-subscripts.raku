# Regression: the Grand Review, batch C3b — a negative subscript is out of
# range (docs/dev/findings/REVIEW-GRAND.md). Raku indexes from the end with
# `*-N`; a negative number never does (Rakudo: X::OutOfRange). Two dozen sites
# used to wrap Python-style or answer quietly: a READ is an armed Failure (quiet
# in a boolean test, fatal on use), a WRITE throws. The literal form `@a[-1]`
# is refused at parse time on both engines, so every case goes through a variable.

my $ok = True;
sub check($got, $want, $label) {
    unless $got eqv $want { note "FAIL: $label — {$got.raku} vs {$want.raku}"; $ok = False }
}
sub dies(&code) { my $lived = False; try { code(); $lived = True }; !$lived }

my @a = 1, 2, 3;
my $n = -1;
my $m = -2;

# 1. Writes throw and leave the array alone.
check(dies({ @a[$n] = 9 }),        True, '@a[$n] = 9 throws');
check(dies({ @a[$n]++ }),          True, '@a[$n]++ throws');
check(dies({ @a[$n] .= succ }),    True, '@a[$n] .= succ throws');
check(dies({ @a[$n, 0] = 9, 8 }),  True, 'a slice assignment with a negative index throws');
check(@a, [1, 2, 3],                     '…and the array is untouched');
check(dies({ my @mx = [1, 2], [3, 4]; @mx[0; $n] = 9 }), True, 'a multidim write throws');
check(dies({ my $b = Blob.new(1, 2, 3); $b[$n] = 9 }),   True, 'a Blob element write throws');
check(dies({ my $s = "abc"; substr-rw($s, $n, 1) = "x" }), True, 'substr-rw with a negative start throws');
check(dies({ my @c = 1, 2, 3; temp @c[$n]; 1 }),         True, 'temp on a negative index throws');

# 2. A single read answers a Failure that detonates on use; a slice throws.
check(@a[$n].^name,                    'Failure', '@a[$n] is a Failure');
check(dies({ my $x = @a[$n]; $x + 1 }), True,     '…which dies when used');
check(dies({ @a[$n, 0] }),              True,     'a SLICE with a negative index throws at once');
check(dies({ @a[$m..$n] }),             True,     '…a Range slice too');
check(((1, 2), (3, 4)).map(*[$n]).map(*.^name).List, ('Failure', 'Failure'), '*[$n] curried over rows');
check(dies({ my @mx = [1, 2], [3, 4]; @mx[0; $n].Int }), True, 'a multidim read dies on use');
check(dies({ ("abc" ~~ /(a)(b)(c)/)[$n].Str }),          True, 'a Match capture by negative index dies on use');
check(dies({ Blob.new(1, 2, 3)[$n].Int }),               True, 'a Blob element by negative index dies on use');
check(dies({ "@a[$n]" }),                                True, 'an interpolated negative subscript dies');
check(dies({ (1..5)[$n].Int }),                          True, 'a Range indexed negatively dies on use');

# 3. The quiet forms stay quiet, as on Rakudo.
check(@a[$n]:exists,      False, '@a[$n]:exists is False');
check((@a[$n]:v).elems,   0,     '@a[$n]:v is empty');
check(@a[*-1],            3,     '*-1 is the way to index from the end');
check(@a[*-1, *-2],       (3, 2), '…in slices too');
my @e;
check(@e[*-1].defined,    False, '@empty[*-1] stays a quiet Failure');
check(@a, [1, 2, 3],             'nothing above changed the array');

if $ok { say "PASS" } else { say "FAIL"; exit 1 }
