# The shared-array refill race, run until it shows or 200 runs have not shown
# it.
#
#   build/rakupp t/race/array-refill-shared.raku               # 200 runs
#   build/rakupp t/race/array-refill-shared.raku --runs=50
#
# The binary that runs this file is the one under test.
#
# `@a = …` refills the array's own storage in place (arrayStore), so every
# alias of @a sees the new elements. Two `start` workers refilling one shared
# array at once each freed its old buffer, and the second free aborted the
# process (SIGTRAP or SIGABRT from malloc, with nothing on stderr). The
# refill now takes the storage's stripe. Found while stress-testing issue
# #215's fix; on 8a23004a 28 of 30 runs died.
#
# The workers only refill: a READ on another thread takes the element's
# stripe, not the storage's, so it can still copy an element the refill is
# freeing, and read Nil or die (SIGSEGV, a few runs in two hundred when eight
# threads read and refill one array). Closing that means a lock on every
# element read of a shared array, or freeing the old buffer only once no read
# can be in it; TODO-FOLLOW-UPS.md has it. The only correct output is `bad: 0`.
#
# The last line is PASS or FAIL, and the exit code is 0 or 1, as in t/stress.

my constant REPRO = Q:to/END/;
    my @shared = ^20;
    await (^8).map: -> $w { start { for ^1500 { @shared = $w %% 2 ?? ^20 !! (^20).list } } };
    say "bad: ", @shared eqv [^20] ?? 0 !! 1;
    END

my $runs = 200;
for @*ARGS -> $a {
    if $a ~~ / ^ '--runs=' (\d+) $ / { $runs = +$0 }
    else { note "array-refill-shared: unknown argument: $a"; exit 2 }
}

my $prog = $*TMPDIR.add("rakupp-array-refill-shared-race-{$*PID}.raku");
$prog.spurt(REPRO);
my $engine = $*EXECUTABLE.absolute;
say "array-refill-shared: $engine, $runs runs";

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
    note "array-refill-shared: $i of $runs, $deaths deaths, $wrong wrong answers" if $i %% 50 && $i < $runs;
}
$prog.unlink;

say "array-refill-shared: $runs runs in {(now - $t0).round(0.1)} s — $deaths deaths, $wrong wrong answers";
say "  $_.key(): $_.value()" for %signals.sort;
say "  printed '$_.key()': $_.value()" for %wrong.sort;
if $deaths || $wrong {
    say 'FAIL';
    exit 1;
}
say 'PASS';
