# ORACLE-PLAN P1: our DEFLATE, zlib and gzip against real libz and GNU gzip, on
# generated inputs, for as long as the budget allows.
#
# t/vectors/zlib.vec pins streams a third party produced, and stops at 420
# bytes. #159 needed a megabyte of log lines to grow a Huffman tree past the
# 15-bit limit, so it shipped under a green suite. This gate draws its inputs
# instead (t/oracle/zlib-oracle.py says how: log scale to 2 MB, the format's
# boundary sizes, Fibonacci-weighted literals and match classes, runs,
# window-edge repeats, mixtures) and asks four things of each:
#
#   1. every stream libz and gzip produced for it, at random levels,
#      strategies and memory levels, inflates here to exactly the body;
#   2. every stream WE produce, at random levels in all three wrappers, is
#      read back to exactly the body by libz (strictly: it must end where the
#      input does) and, for gzip, by `gzip -d`;
#   3. our own round trip;
#   4. every mutant of a libz stream gets the same verdict here as in libz:
#      libz's bytes when it accepts, a refusal when it refuses. A decoder
#      that accepts what libz refuses is reported apart, as LENIENT, because
#      that is a divergence to decide on rather than a wrong answer.
#
#   rakupp t/oracle/zlib.raku [--seconds=60] [--seed=N] [--rounds=N] [--keep]
#
# Every round prints its seed. `--seed=N --rounds=1` replays one round
# exactly, and a failing round keeps its directory: the body, its description
# and every stream are in it, named as zlib-oracle.py's header lists.
#
# Needs python3 (3.8 or newer) on PATH; gzip is used when present. It FAILS
# when python3 is missing, because a check that skips looks the same as one
# that passed: pass --allow-missing to make that a skip on purpose.
#
# Contract: exit 0 + last line PASS, as t/regression has it.

my &compress   = &::('rakupp-compress');
my &uncompress = &::('rakupp-uncompress');

my $seconds = 60;
my $seed    = Int;
my $rounds  = Int;
my $keep    = False;
my $allow-missing = False;
for @*ARGS -> $a {
    if    $a.starts-with('--seconds=') { $seconds = +$a.substr(10) }
    elsif $a.starts-with('--seed=')    { $seed    = +$a.substr(7) }
    elsif $a.starts-with('--rounds=')  { $rounds  = +$a.substr(9) }
    elsif $a eq '--keep'               { $keep = True }
    elsif $a eq '--allow-missing'      { $allow-missing = True }
    else { note "unknown option $a"; exit 2 }
}

my $ORACLE = $*PROGRAM.parent.add('zlib-oracle.py').absolute;

sub python-ok() {
    my $p = run 'python3', '-c', 'import sys, zlib; sys.exit(0 if sys.version_info >= (3, 8) else 1)', :out, :err;
    $p.out.slurp(:close); $p.err.slurp(:close);
    $p.exitcode == 0
}
unless python-ok() {
    if $allow-missing { say "# SKIP: no python3 3.8+ on PATH"; say 'PASS'; exit 0 }
    note "python3 3.8+ with zlib is needed as the oracle (pass --allow-missing to skip)";
    say 'FAIL';
    exit 1;
}

sub adverbs(Str $wrap) { $wrap eq 'zlib' ?? \() !! $wrap eq 'gzip' ?? \(:gzip) !! \(:raw) }

my @fails;
my @lenient;
my %count is default(0);

sub one-round(Int $round-seed) {
    my $dir = $*TMPDIR.add("rakupp-oracle-zlib-$*PID-$round-seed");
    $dir.mkdir;
    my $bodies = 12;
    my $g = run 'python3', '-I', $ORACLE, 'gen', $dir.Str, ~$round-seed, ~$bodies, :out, :err;
    my $gerr = $g.err.slurp(:close); $g.out.slurp(:close);
    if $g.exitcode != 0 {
        @fails.push: "seed $round-seed: the generator failed: $gerr";
        return;
    }
    my @round-fails;
    my $rng = $round-seed;          # our own choices, derived from the seed
    sub pick-int(Int $n) { $rng = ($rng * 6364136223846793005 + 1442695040888963407) % 2**64; ($rng +> 33) % $n }

    for ^$bodies -> $k {
        my $body = $dir.add("$k.in").slurp(:bin);
        my $desc = $dir.add("$k.desc").slurp.chomp;
        my $progress = $dir.add('progress');

        # 1. their streams, our inflate
        for $dir.dir(test => /^ "$k.L." /).sort -> $f {
            my $wrap = $f.basename.split('.')[2];
            $progress.spurt("inflating {$f.basename}\n");
            %count<theirs>++;
            my $got = try uncompress($f.slurp(:bin), |adverbs($wrap));
            if !$got.defined {
                @round-fails.push: "{$f.basename} ($desc): we refused it: {$!.message}";
            }
            elsif !($got eqv $body) {
                @round-fails.push: "{$f.basename} ($desc): we inflated it to different bytes";
            }
        }

        # 2 and 3. our streams, their inflate and ours
        my @plan = (6 => 'zlib'), |(^3).map({ pick-int(11) - 1 => <zlib raw gzip>[pick-int(3)] });
        for @plan -> (:key($level), :value($wrap)) {
            $progress.spurt("deflating $k at $level as $wrap\n");
            %count<ours>++;
            my $c = try compress($body, $level, |adverbs($wrap));
            if !$c.defined {
                @round-fails.push: "$k.in ($desc) at level $level, $wrap: compress threw: {$!.message}";
                next;
            }
            my $tag = $level < 0 ?? 'd' !! ~$level;
            $dir.add("$k.O.{$wrap}.$tag").spurt($c);
            my $back = try uncompress($c, |adverbs($wrap));
            @round-fails.push: "$k.in ($desc) at level $level, $wrap: our own round trip failed: {$! // 'different bytes'}"
                unless $back.defined && $back eqv $body;
        }

        # 4. mutants
        for $dir.dir(test => /^ "$k.M" \d+ '.' \w+ $/).sort -> $f {
            my $wrap = $f.basename.split('.')[2];
            my $how  = $f.sibling($f.basename ~ '.how').slurp.chomp;
            my $want-file = $f.sibling($f.basename ~ '.want');
            $progress.spurt("inflating mutant {$f.basename}\n");
            %count<mutants>++;
            my $got = try uncompress($f.slurp(:bin), |adverbs($wrap));
            if $want-file.e {
                my $want = $want-file.slurp(:bin);
                if !$got.defined {
                    @round-fails.push: "{$f.basename} ($how): libz accepts it, we refused: {$!.message}";
                }
                elsif !($got eqv $want) {
                    @round-fails.push: "{$f.basename} ($how): libz and we inflate it to different bytes";
                }
            }
            elsif $got.defined {
                @lenient.push: "seed $round-seed {$f.basename} ($how): libz refuses it, we accepted it";
            }
        }
    }

    # 2. their verdict on our streams
    my $c = run 'python3', '-I', $ORACLE, 'check', $dir.Str, :out, :err;
    my $out = $c.out.slurp(:close);
    my $cerr = $c.err.slurp(:close);
    @round-fails.push: "the checker failed: $cerr" if $c.exitcode != 0;
    for $out.lines.grep(*.starts-with('FAIL ')) -> $l {
        my $k = $l.substr(5).split('.')[0];
        my $desc = $dir.add("$k.desc").e ?? $dir.add("$k.desc").slurp.chomp !! '?';
        @round-fails.push: "{$l.substr(5)} [$desc]";
    }

    if @round-fails {
        @fails.push: "seed $round-seed, kept in $dir:\n    " ~ @round-fails.join("\n    ");
    }
    elsif !$keep {
        .unlink for $dir.dir;
        $dir.rmdir;
    }
}

my $start = now;
my $n = 0;
my $base-seed = $seed // (now.Rat * 1000).Int % 2**31;
loop {
    my $s = $base-seed + $n;
    say "# round {$n + 1}, seed $s";
    one-round($s);
    $n++;
    last if $rounds.defined && $n >= $rounds;
    last if !$rounds.defined && now - $start >= $seconds;
}

say "# {$n} rounds, {%count<theirs>} third-party streams inflated, {%count<ours>} of ours read back, {%count<mutants>} mutants";
if @lenient {
    say "# LENIENT: {@lenient.elems} mutants libz refuses and we accept";
    say "#   $_" for @lenient.head(10);
}
if @fails {
    say "not ok - $_" for @fails;
    say 'FAIL';
    exit 1;
}
say 'PASS';
