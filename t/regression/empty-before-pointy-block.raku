# `Empty` is a term, as `now`, `e`, `pi` and `rand` are, so a pointy block or
# a block after it belongs to the statement: `for Empty -> $x { }` was
# "Function 'Empty' needs parens to avoid gobbling block".
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my @seen;
for Empty -> $x { @seen.push($x) }
ck @seen.elems, 0, 'for Empty -> $x';
for 1, Empty -> \v { @seen.push(v) }
ck @seen, [1], 'for 1, Empty -> \v';
@seen = ();
for 1, Empty { @seen.push($_) }
ck @seen, [1], 'for 1, Empty { } (as before)';
ck (if Empty { 1 } else { 2 }), 2, 'if Empty';
my $n = 0;
while Empty { $n++; last }
ck $n, 0, 'while Empty';
ck Empty.raku, 'Empty', 'Empty itself';

say 'PASS';
