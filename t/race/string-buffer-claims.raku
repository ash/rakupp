# Many threads growing copies of one shared string at once, 200 fresh processes.
#
#   build/rakupp t/race/string-buffer-claims.raku               # 200 runs
#   build/rakupp t/race/string-buffer-claims.raku --runs=50
#
# The binary that runs this file is the one under test.
#
# A long string that more than one variable holds is a VIEW of a shared buffer
# (docs/dev/plans/APPEND-PLAN.md): `$mine = $base; $mine ~= …` claims the free
# space after the view with one compare-and-swap on the buffer's end and writes
# there, and `x ~ $mine` claims the space in front. Eight workers start from
# copies of ONE view and grow them at the same moment, so they race for the same
# end and front of the same buffer: exactly one may win each claim, the others
# must move to a buffer of their own, and nobody may see another's bytes. Every
# worker then checks its whole string. The only correct output is `bad: 0`.
#
# The last line is PASS or FAIL, and the exit code is 0 or 1, as in t/stress.

my constant REPRO = q:to/END/;
    my $bad = 0;
    for ^10 {
        my $base = 'b' x 400;
        my $held = $base; $base ~= '|';   # $base is now a view others can claim after
        my $go = Promise.new;
        my @p = (^8).map: -> $w {
            start {
                await $go;
                my $mine = $base;
                my $tag = ('a'..'h')[$w];
                for ^300 { $mine ~= $tag }
                for ^50  { $mine = $tag ~ $mine }
                my $k = $mine.substr(50, 401);
                $mine.chars == 401 + 350 && $mine.starts-with($tag x 50) &&
                    $mine.ends-with($tag x 300) && $k eq ('b' x 400 ~ '|') &&
                    $mine.substr(451) eq $tag x 300
            }
        };
        $go.keep;
        for @p { $bad++ unless try .result }
        $bad++ unless $base eq 'b' x 400 ~ '|' && $held eq 'b' x 400;
    }
    say "bad: $bad";
    END

my $runs = 200;
for @*ARGS -> $a {
    if $a ~~ / ^ '--runs=' (\d+) $ / { $runs = +$0 }
    else { note "string-buffer-claims: unknown argument: $a"; exit 2 }
}

my $prog = $*TMPDIR.add("rakupp-string-claims-race-{$*PID}.raku");
$prog.spurt(REPRO);
my $engine = $*EXECUTABLE.absolute;
say "string-buffer-claims: $engine, $runs runs";

my (%signals, %wrong);
my ($deaths, $wrong) = 0, 0;
my $t0 = now;
for 1..$runs -> $i {
    my $p = run 'perl', '-e', 'alarm 30; exec @ARGV or die "exec: $!"', $engine, $prog.absolute,
                :out, :err;
    my $out = $p.out.slurp(:close); $p.err.slurp(:close);
    my $sig = $p.signal // 0;
    if $sig {
        $deaths++;
        %signals{$sig == 14 ?? 'hung (30 s cap)' !! "signal $sig"}++;
    }
    elsif $out.trim ne 'bad: 0' {
        $wrong++;
        %wrong{$out.trim.lines.tail // '(no output)'}++;
    }
    note "string-buffer-claims: $i of $runs, $deaths deaths, $wrong wrong answers" if $i %% 50 && $i < $runs;
}
$prog.unlink;

say "string-buffer-claims: $runs runs in {(now - $t0).round(0.1)} s — $deaths deaths, $wrong wrong answers";
say "  $_.key(): $_.value()" for %signals.sort;
say "  printed '$_.key()': $_.value()" for %wrong.sort;
if $deaths || $wrong {
    say 'FAIL';
    exit 1;
}
say 'PASS';
