# `for $=pod` takes one block at a time, as Rakudo's does: `$=pod` is bound,
# no Scalar container, so the loop iterates it, where it made one pass with
# the whole list. A `$` variable holding an Array is one item, as before.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

=begin pod
A
=end pod

=begin pod
B
=end pod

my @seen;
for $=pod { @seen.push: .^name }
ck @seen.List, <Pod::Block::Named Pod::Block::Named>, 'one block at a time';
my @pairs;
for $=pod -> $a, $b { @pairs.push: ($a.^name, $b.^name) }
ck @pairs.elems, 1, 'two at a time';
my $arr = [1, 2]; my $n = 0;
for $arr { $n++ }
ck $n, 1, 'an Array in a $ variable, as before';

say 'PASS';
