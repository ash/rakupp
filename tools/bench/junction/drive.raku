#!/usr/bin/env raku
# Drives compare.raku across two or more engines and prints the before/after
# table. compare.raku measures ONE engine; this runs it under each in turn.
#
#   raku tools/bench/junction/drive.raku \
#        before=/tmp/jbefore/build/rakupp after=./build/rakupp rakudo=raku
#
# Each argument is LABEL=COMMAND. The FIRST label is the baseline every other
# column is divided by; a label containing "rakudo" is reported but never used
# as the baseline. Order on the command line is the column order.
#
# The engines are ALTERNATED round by round rather than run one after the other:
# a batched sweep lets the machine's own drift into the ratio, which on this
# laptop has been worth 30% (docs/guide/PARALLEL-SPEEDUP.md). compare.raku
# already interleaves its cases within a run; this interleaves the binaries.
#
# Checksums are compared before any timing is reported. Two engines that
# disagree about an answer are not doing the same work, and the ratio between
# them means nothing — so a mismatch is fatal rather than a footnote.
#
# To build a "before" engine without disturbing the working tree:
#   git worktree add /tmp/jbefore <commit>
#   cmake -S /tmp/jbefore -B /tmp/jbefore/build -DCMAKE_BUILD_TYPE=Release
#   make -C /tmp/jbefore/build -j6 rakupp
#   ...
#   git worktree remove --force /tmp/jbefore

my $ROUNDS = %*ENV<ROUNDS> ?? +%*ENV<ROUNDS> !! 3;
my $SCRIPT = $*PROGRAM.parent.add('compare.raku').Str;

my @engines = @*ARGS.map: {
    my ($label, $cmd) = .split('=', 2);
    die "argument '$_' is not LABEL=COMMAND" unless $cmd;
    %(:$label, :$cmd);
};
die "give at least two engines, e.g. before=... after=..." if @engines < 2;

# label -> case -> (best-us, checksum)
my %r;
for ^$ROUNDS -> $round {
    for @engines -> %e {
        my $p = run(|%e<cmd>.words, $SCRIPT, :out, :err);
        my $out = $p.out.slurp(:close);
        my $err = $p.err.slurp(:close);
        die "%e<label> ({%e<cmd>}) failed:\n$err" unless $p.exitcode == 0;
        for $out.lines.skip(1) -> $line {
            next unless $line.trim;
            my ($case, $us, $sum) = $line.split("\t");
            my $v = +$us;
            my $cur = %r{%e<label>}{$case};
            %r{%e<label>}{$case} = ($v, $sum) if !$cur || $v < $cur[0];
        }
        note "round {$round + 1}/$ROUNDS: %e<label> done";
    }
}

# --- checksums first: a disagreement invalidates every number below ---------
my $base = @engines[0]<label>;
my @cases = %r{$base}.keys.sort;
my @mismatch;
for @engines[1..*] -> %e {
    for @cases -> $c {
        my $a = %r{$base}{$c}[1];
        my $b = %r{%e<label>}{$c}[1] // 'MISSING';
        @mismatch.push("$c: $base=$a %e<label>=$b") if $a ne $b;
    }
}
if @mismatch {
    note "CHECKSUM MISMATCH — the engines did not compute the same answers:";
    note "  $_" for @mismatch;
    exit 1;
}
say "checksums: all engines agree on all {@cases.elems} cases";
say '';

# --- the table, in the order compare.raku emitted the cases -----------------
my @order = run(|@engines[0]<cmd>.words, $SCRIPT, :out).out.slurp(:close)
              .lines.skip(1).grep(*.trim).map(*.split("\t")[0]);

my $ratio = @engines.first({ !.<label>.lc.contains('rakudo') && .<label> ne $base });
my $w = @order.map(*.chars).max max 4;
say sprintf("%-{$w}s", 'case') ~ @engines.map({ sprintf("%10s", .<label>) }).join
    ~ ($ratio ?? sprintf("%10s", 'change') !! '');
say '-' x ($w + 10 * (@engines + ($ratio ?? 1 !! 0)));
for @order -> $c {
    my $row = sprintf("%-{$w}s", $c);
    $row ~= @engines.map({ sprintf("%10.2f", %r{.<label>}{$c}[0]) }).join;
    if $ratio {
        my $sp = %r{$base}{$c}[0] / %r{$ratio<label>}{$c}[0];
        $row ~= sprintf("%10s", 0.95 < $sp < 1.05 ?? '--'
                              !! $sp < 100        ?? sprintf("%.2fx", $sp)
                              !!                     sprintf("%.0fx", $sp));
    }
    say $row;
}
say '';
say "us per operation, best of $ROUNDS interleaved rounds."
  ~ ($ratio ?? "  'change' is $base / {$ratio<label>}." !! '');
