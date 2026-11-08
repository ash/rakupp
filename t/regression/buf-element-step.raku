# `$b[1]++` steps a Buf's element in place, wrapping at its width, as Rakudo
# does: it died "Index out of range. Is: 1, should be in 0..0", and `$b[0]++`
# "Cannot modify an immutable Buf"; `$b[0] += 10` and `$b[1] = 7` were right.
# One past the end grows the Buf, a parameter's step reaches the caller's,
# and a Blob's element is no container.
# Every line answers the same under Rakudo 2026.09, and compiled.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my $b = Buf.new(1, 2);
$b[1]++;
ck $b.list, (1, 3), '$b[1]++';
my $b2 = Buf.new(1, 255);
ck $b2[1]++, 255, 'a postfix step answers the old value';
ck $b2.list, (1, 0), '…and wraps at the byte';
ck ++$b2[0], 2, 'a prefix step answers the new one';
$b2[*-1]--;
ck $b2.list, (2, 255), 'a WhateverCode index, stepped down past zero';
my $c = buf8.new(0);
$c[0]--;
ck $c.list, (255,), 'buf8 below zero';
my $f = buf16.new(65535);
$f[0]++;
ck $f.list, (0,), 'buf16 wraps at its width';
my $g = buf64.new(0);
$g[0]--;
ck $g.list, (18446744073709551615,), 'buf64 below zero';
my $d = Buf.new(1);
$d[3]++;
ck $d.list, (1, 0, 0, 1), 'past the end grows it';
sub bump($x) { $x[1]++ }
bump($b);
ck $b.list, (1, 4), 'through a parameter';
my @a = Buf.new(5);
@a[0][0]++;
ck @a[0].list, (6,), 'a Buf held in an array';
my $e = Blob.new(1);
try { $e[0]++ }
ck $!.^name, 'X::Multi::NoMatch', 'a Blob element is no container';

say 'PASS';
