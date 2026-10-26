# Regression (2026-10-26): a source read live but finite is not lazy.
#
# `Channel.list`, a handle's `.lines` (standard input, a process's output)
# and a `Seq.from-loop` with a condition answered `.is-lazy` True, and an
# Array assigned from one kept the flag. Rakudo asks the iterator, and theirs
# say no: `.is-lazy` is False, and list assignment reads the source to its
# end. A `for` still walks them live, and `lazy` still declares them lazy.
#
# Expected answers are Rakudo 2026.09's (2026-10-26).

my $fails = 0;
sub check($got, $want, $what) {
    return if $got eqv $want;
    $fails++;
    say "not ok - $what\n  got:  {$got.raku}\n  want: {$want.raku}";
}

sub sent(*@v) {
    my $c = Channel.new;
    start { for @v { $c.send($_); sleep 0.01 }; $c.close };
    $c
}

check sent(1, 2).list.is-lazy, False, 'Channel.list is not lazy';
my @a = sent(1, 2, 3).list;
check @a.is-lazy, False, 'an Array assigned from Channel.list is not lazy';
check @a, [1, 2, 3], '…and holds everything sent before the close';
my @seen;
for sent(4, 5).list { @seen.push: $_ }
check @seen, [4, 5], 'a for loop still walks Channel.list';

my $l = lazy sent(6, 7).list;
check $l.is-lazy, True, '`lazy` declares it lazy';
check $l.eager, (6, 7), '…and it reads the same values';
my $plain = sent(8).list;
my @declared = lazy $plain;
check $plain.is-lazy, False, '…without changing the list it was given';

my $i = 0;
check Seq.from-loop({ $i++ }, { $i < 3 }).is-lazy, False, 'Seq.from-loop with a condition is not lazy';
check Seq.from-loop({ 1 }).is-lazy, True, '…and without one it is';

my $p = run $*EXECUTABLE, '-e', 'say 1; say 2', :out;
check $p.out.lines.is-lazy, False, "a process's lines are not lazy";
$p = run $*EXECUTABLE, '-e', 'say 3; say 4', :out;
my @lines = $p.out.lines;
check @lines, ['3', '4'], "…and list assignment reads them to the end";
$p = run $*EXECUTABLE, '-e', 'my @l = lines(); say @l.elems, " ", @l.is-lazy', :in, :out;
$p.in.print("x\ny\nz\n");
$p.in.close;
check $p.out.slurp(:close).trim, '3 False', 'standard input: lines() assigned is read to the end';

if $fails { say "$fails failure(s)"; exit 1 }
say 'PASS';
