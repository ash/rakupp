#!/usr/bin/env rakupp
# The TYPES-PLAN benchmarks (docs/dev/plans/TYPES-PLAN.md): each kernel timed
# under every configuration this engine offers, best of N, with its checksum
# checked against the first configuration's, so a speed-up that changes the
# answer is flagged instead of reported.
#
#   build/rakupp tools/bench/types/run.raku
#   build/rakupp tools/bench/types/run.raku --reps=5 --rakudo=/path/to/rakudo
#   build/rakupp tools/bench/types/run.raku mandel      # only kernels matching
#
# The configurations are the interpreter, `--cnp`, `--exe` and `--exe -O`, and,
# once the engine has it, `--types` with each of those. An `--exe` column
# compiles the kernel once, outside the timing, and times the binary; a kernel
# `--exe` could not compile natively is bundled with the interpreter instead,
# and its cell says so, since that time is the interpreter's. Rakudo is timed
# too when one is found (the `--rakudo=` option, or `rakudo` on PATH). Pass
# `--rakudo=` (empty) to skip it, and `--no-exe` to skip the compiled columns.

my $here = $?FILE.IO.parent;

# Kernel pairs: the same computation over plain `my $` and over declared
# natives. `mandel-rat` has no native twin — Raku has no native Rat.
my @kernels = <
    intloop-plain intloop-native
    numloop-plain numloop-native
    mandel-plain  mandel-native
    mandel-rat
>;

sub on-path(Str $name) {
    for (%*ENV<PATH> // '').split(':') -> $d {
        my $p = $d.IO.add($name);
        return ~$p if $d && $p.x;
    }
    Nil
}

# Does this engine accept an option? Asked once, by running a tiny program
# under it: the program's own output is the answer, because an unknown option
# is reported on stderr and the exit status alone does not say so.
sub accepts(Str $engine, *@opt) {
    my $p = run $engine, |@opt, '-e', 'print "ok"', :out, :err;
    my $out = $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    $p.exitcode == 0 && $out eq 'ok' && !$err
}

# Compile a kernel with `--exe` (plus any extra options) into $dir. Answers the
# binary's path and whether it is native, or Nil when the compile failed.
sub compile-exe(Str $engine, @opt, Str $file, IO::Path $dir, Str $tag) {
    my $bin = ~$dir.add($file.IO.basename.subst('.raku', '') ~ "-$tag");
    my $p = run $engine, |@opt, '-o', $bin, $file, :out, :err;
    my $said = $p.out.slurp(:close) ~ $p.err.slurp(:close);
    return Nil unless $p.exitcode == 0 && $bin.IO.x;
    ($bin, $said.contains('(native)'))
}

# One timed run: the wall time and the checksum line.
sub time-one(@cmd) {
    my $t0 = now;
    my $p = run |@cmd, :out, :err;
    my $out = $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    my $wall = now - $t0;
    return (Nil, "exit {$p.exitcode}: {$err.lines.head // ''}") unless $p.exitcode == 0;
    my $sum = $out.lines.first(*.starts-with('sum=')) // '(no sum)';
    ($wall, $sum)
}

sub MAIN(*@only, Int :$reps = 3, Str :$rakudo = (on-path('rakudo') // ''), Str :$engine = ~$*EXECUTABLE,
          Bool :$no-exe = False) {
    # a configuration is its column name, the engine options, and whether it
    # compiles with --exe (then the options are the COMPILE options)
    my @configs = ('interp' => { opt => [], exe => False },);
    my $cnp = accepts($engine, '--cnp');
    my $exe = !$no-exe;
    @configs.push('--cnp' => { opt => ['--cnp'], exe => False }) if $cnp;
    if $exe {
        @configs.push('--exe'    => { opt => ['--exe'],       exe => True });
        @configs.push('--exe -O' => { opt => ['--exe', '-O'], exe => True });
    }
    my $types = accepts($engine, '--types');
    note $types ?? "--types: supported, timed in its own columns"
                !! "--types: not in this engine yet (TYPES-PLAN phase N1), so no --types columns";
    if $types {
        @configs.push('--types' => { opt => ['--types'], exe => False });
        @configs.push('--types --cnp' => { opt => ['--types', '--cnp'], exe => False }) if $cnp;
        @configs.push('--types --exe -O' => { opt => ['--types', '--exe', '-O'], exe => True }) if $exe;
    }
    my $bindir = $*TMPDIR.add("rakupp-types-bench-$*PID");
    $bindir.mkdir;
    my @cols = @configs.map(*.key);
    @cols.push('Rakudo') if $rakudo;

    my @run = @only ?? @kernels.grep(-> $k { @only.first({ $k.contains($_) }) }) !! @kernels;
    note "engine: {$engine}  ({qqx{$engine --version}.trim})";
    note "rakudo: {$rakudo || '(skipped)'}";
    note "best of $reps; every configuration must print the same checksum";

    say '| kernel | ' ~ @cols.join(' | ') ~ ' | checksum |';
    say '|---|' ~ ('---:|' x @cols) ~ '---|';
    my $bad = 0;
    for @run -> $k {
        my $file = ~$here.add("$k.raku");
        # compile the --exe columns first; each is built once and timed as a binary
        my @note = '' xx @configs;
        my @cmds = @configs.kv.map(-> $i, $c {
            if $c.value<exe> {
                my $tag = $c.key.subst(/\W+/, '-', :g).trim;
                with compile-exe($engine, $c.value<opt>, $file, $bindir, $tag) -> ($bin, $native) {
                    @note[$i] = ' (bundled)' unless $native;
                    [$bin]
                }
                else { @note[$i] = ' (no build)'; [] }
            }
            else { [$engine, |$c.value<opt>, $file] }
        });
        @cmds.push([$rakudo, $file]) if $rakudo;
        @note.push('') if $rakudo;
        my @best = Nil xx @cmds;
        my @sums = '' xx @cmds;
        # interleaved: every configuration once per round, so machine load
        # drifts across all of them alike
        for ^$reps {
            for @cmds.kv -> $i, @cmd {
                next unless @cmd;
                my ($wall, $sum) = time-one(@cmd);
                @sums[$i] = $sum;
                @best[$i] = $wall if $wall.defined && (!@best[$i].defined || $wall < @best[$i]);
            }
        }
        my $want = @sums[0];
        my @cells = @best.kv.map(-> $i, $b {
            my $cell = ($b.defined ?? sprintf('%.3f s', $b) !! 'failed') ~ @note[$i];
            if $b.defined && @sums[$i] ne $want { $cell ~= ' **≠**'; $bad++ }
            $cell
        });
        say "| $k | " ~ @cells.join(' | ') ~ " | {$want.subst('sum=', '')} |";
    }
    .unlink for $bindir.dir;
    $bindir.rmdir;
    if $bad {
        note "$bad cell(s) printed a different checksum — marked ≠";
        exit 1;
    }
}
