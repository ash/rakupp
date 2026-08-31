# Regression: under `--exe`, a range with a Whatever endpoint was built INSIDE OUT.
#
# `for 1 .. * { … }` ran ZERO times natively and correctly interpreted — a silent
# wrong answer, no crash, no fallback:
#
#     my $n = 0;
#     for 1 .. * { $n++; last if $_ > 2 }
#     say "iterations: $n";        # interpreted 3, compiled 0
#
# The `for` loop was only where it showed. rtRangeVal — the range constructor
# native codegen emits for every `A..B`, and whose own comment says it is "kept
# in step with the NK::RangeLit arm in eval" — had drifted from that arm: the
# interpreter has an explicit Whatever block (a Whatever endpoint is unbounded,
# the LLONG extreme marking an infinite range), and rtRangeVal had none. So `*`
# fell through to `.toInt()`, which is 0, and `1..*` became the EMPTY `1..0`
# while `*..5` became `0..5`. Every consumer inherited it: iteration, .gist,
# indexing, smartmatch, .head, .grep.
#
# `gather { for 1..* { take $_ } }` was hit through the same door — its block's
# loop never ran, so `$l[^5]` was (Nil Nil Nil Nil Nil). `gather { loop {…} }`
# was always fine, which is what said the bug was the range and not gather.
#
# Every case asserts INTERPRETED == COMPILED.
# Contract: exit 0 + last line PASS.

my $v = run($*EXECUTABLE, '--version', :out, :err);
my $banner = $v.out.slurp(:close); $v.err.slurp(:close);
unless $banner.contains('rakupp') {
    # only rakupp has --exe; under Rakudo there is nothing this file can compile
    note 'exe-endless-range: not rakupp, nothing to compile';
    say 'PASS';
    exit 0;
}

my $work = $*TMPDIR.add("exe-endless-range-$*PID");
mkdir $work;
my @made;
my $fails = 0;

sub check(Str $desc, $got, $want) {
    if $got eq $want { say "ok - $desc" }
    else {
        $fails++;
        say "not ok - $desc";
        note "GOT [{$got}] WANT [{$want}]";
    }
}

# Run CODE interpreted and again as a compiled binary; both must equal WANT.
# :native asserts the compile stayed native — a bundling fallback would run the
# interpreter and agree for the wrong reason, testing nothing.
sub agree(Str $name, Str $code, Str $want) {
    my $src = $work.add($name ~ '.raku');
    $src.spurt($code);
    my $bin = $work.add($name ~ '-bin');
    @made.push($src.Str, $bin.Str);

    my $i = run($*EXECUTABLE, $src.Str, :out, :err);
    my $interp = $i.out.slurp(:close).lines.join('|');
    $i.err.slurp(:close);

    my $c = run($*EXECUTABLE, '--exe', '-o', $bin.Str, $src.Str, :out, :err);
    my $cout = $c.out.slurp(:close) ~ $c.err.slurp(:close);
    if $c.exitcode != 0 {
        $fails++;
        note "compile of $name failed:\n$cout";
        return;
    }
    unless $cout.contains('(native)') {
        $fails++;
        note "$name fell back to bundling, so the compiled path was never tested:\n$cout";
    }
    my $r = run($bin.Str, :out, :err);
    my $compiled = $r.out.slurp(:close).lines.join('|');
    $r.err.slurp(:close);

    check("$name: interpreted",     $interp,   $want);
    check("$name: compiled agrees", $compiled, $interp);
}

# 1. The report, plus every other consumer of an unbounded range. `for 1..*`
#    counted 0; the rest answered for `1..0` and `0..5`.
agree('endless', q:to/END/, '3|1..Inf|4|True|(1 2 3 4)|-Inf..5|(2 3 5 7)|(1 2 3 4 5)');
    my $n = 0;
    for 1 .. * { $n++; last if $_ > 2 }
    say $n;                             # 3   — was 0
    say (1..*).gist;                    # 1..Inf
    say (1..*)[3];                      # 4
    say 5 ~~ 1..*;                      # True
    say (1..*).head(4);                 # (1 2 3 4)
    say (*..5).gist;                    # -Inf..5
    say (1..*).grep(*.is-prime).head(4);
    my $l = gather { for 1..* { take $_ } };
    say $l[^5];                         # was (Nil Nil Nil Nil Nil)
    END

# 2. The endpoint shapes around it: exclusive markers are dropped on the
#    unbounded side (as the interpreter's arm does), a runtime endpoint works
#    the same as a literal one, and `* .. *` is unbounded both ways.
agree('endpoints', q:to/END/, '1..Inf|-Inf..5|7..Inf|9|-Inf..Inf');
    say (1 ..^ *).gist;
    say (* ^.. 5).gist;
    my $x = 7;
    say ($x .. *).gist;
    say ($x .. *)[2];
    say (* .. *).gist;
    END

# 3. …and the ranges that must NOT move: the Whatever block sits between the Str
#    and fractional arms, so a plain, exclusive, Str or fractional range has to
#    come through it untouched.
agree('finite-intact', q:to/END/, '1..5|(1 2 3 4)|abcde|0..^2.5|True');
    say (1..5).gist;
    say (1..^5).list;
    say ('a'..'e').join;
    say (0..^2.5).gist;
    say 2.4 ~~ 0..^2.5;
    END

unlink $_ for @made;
try rmdir $work;
say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
