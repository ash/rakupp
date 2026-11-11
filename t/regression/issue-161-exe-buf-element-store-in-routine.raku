# Regression: issue #161 (2026-11-10).
#
# In an `--exe` binary, `$b[0] = 9` on a Buf held in an untyped variable
# inside a routine turned the variable into an Array holding only that
# element: `Array [9]` where the interpreter gives `Buf:0x<09 02 03>`.
# Meant to be compiled too (t/exe/run.raku and t/run.raku's native list).
#
# Expected answers are Rakudo 2026.09's (2026-11-10).

my $fails = 0;
sub check($got, $want, $what) {
    return if $got eqv $want;
    $fails++;
    say "not ok - $what\n  got:  {$got.raku}\n  want: {$want.raku}";
}

sub untyped() { my $b = Buf.new(1, 2, 3); $b[0] = 9; ($b.^name, $b.list) }
check untyped(), ('Buf', (9, 2, 3)), 'my $b = Buf.new; $b[0] = 9 in a routine';
sub typed() { my Buf $b = Buf.new(1, 2, 3); $b[2] = 7; ($b.^name, $b.list) }
check typed(), ('Buf', (1, 2, 7)), 'my Buf $b, the same';
sub from-blob() { my $c = Buf.new(Blob.new(4, 5)); ($c.^name, $c.list) }
check from-blob(), ('Buf', (4, 5)), 'Buf.new(Blob)';
sub by-var($i, $v) { my $b = Buf.new(0, 0, 0); $b[$i] = $v; $b[$i + 1] = $v + 1; ($b.^name, $b.list) }
check by-var(1, 250), ('Buf', (0, 250, 251)), 'stores at a variable index';
sub wraps() { my $b = Buf.new(1); $b[0] = 300; $b.list }
check wraps(), (44,), 'a stored byte wraps at 8 bits';
sub grows() { my $b = Buf.new(1); $b[3] = 4; ($b.^name, $b.list) }
check grows(), ('Buf', (1, 0, 0, 4)), 'a store past the end grows the Buf';
sub gist() { my $b = Buf.new(1, 2, 3); $b[0] = 9; $b.gist }
check gist(), 'Buf:0x<09 02 03>', 'its gist';
my $top = Buf.new(1, 2, 3); $top[1] = 8;
check ($top.^name, $top.list), ('Buf', (1, 8, 3)), 'at the top level';

if $fails { say "$fails failure(s)"; exit 1 }
say 'PASS';
