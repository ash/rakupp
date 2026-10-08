# A compiled loop must not hold a variable another thread can write —
# PARALLEL-SCALING-PLAN P5.
#
# While workers are live, a loop kernel may now run, provided no other thread
# can reach the variables it holds in its frame. A loop polling a flag that a
# worker sets is exactly the variable it must not hold: held, the loop never
# sees the write and spins for ever. Each program here polls an Int flag (a
# Bool condition the kernel declines anyway) that another thread sets, and
# must END, in every mode the kernels have: by default, under `--cnp`, and
# with kernels off. Each run is capped at 20 s; a hang is the failure.

unless $*RAKU.compiler.name eq 'Raku++' {
    note 'kernel-shared-flag: not rakupp, no kernels to check';
    say 'PASS';
    exit 0;
}

my $work = $*TMPDIR.add("kernel-shared-flag-$*PID-{(^1e9).pick}");
mkdir $work;
LEAVE { .unlink for $work.dir; rmdir $work }

my %programs =
    # a mainline flag a `start` block polls, set by the main thread
    'worker-polls' => q:to/END/,
        my $stop = 0;
        my $w = start { my $n = 0; while $stop == 0 { $n = $n + 1 }; 'seen' };
        sleep 0.02;
        $stop = 1;
        say await $w;
        END
    # a routine's flag the routine polls, set from a worker through a nested sub
    'owner-polls' => q:to/END/,
        sub owner() {
            my $stop = 0;
            sub set-it() { $stop = 1 }
            my $w = start { sleep 0.02; set-it() };
            my $n = 0;
            while $stop == 0 { $n = $n + 1 }
            await $w;
            'seen'
        }
        say owner();
        END
    # …and through a `start` block that closes over it, in a C-style loop
    'owner-loop' => q:to/END/,
        sub owner() {
            my $stop = 0;
            my $w = start { sleep 0.02; $stop = 1 };
            my $n = 0;
            loop (my $i = 0; $stop == 0; $i++) { $n = $n + 1 }
            await $w;
            'seen'
        }
        say owner();
        END
    ;

my $fails = 0;
for %programs.sort(*.key) -> (:key($name), :value($code)) {
    my $f = $work.add("$name.raku");
    $f.spurt($code);
    for ('default', ()), ('cnp', ('--cnp=threshold=0',)), ('no-kernels', ()) -> ($mode, @flags) {
        my %env = %*ENV;
        %env<RAKUPP_NO_KERNELS> = '1' if $mode eq 'no-kernels';
        %env<RAKUPP_PRIVATE_SLOTS>:delete;
        my $p = run 'perl', '-e', 'alarm 20; exec @ARGV or die "exec: $!"', $*EXECUTABLE.absolute, |@flags, $f,
                    :out, :err, :%env;
        my $out = $p.out.slurp(:close).trim;
        my $err = $p.err.slurp(:close);
        unless $p.exitcode == 0 && $out eq 'seen' {
            $fails++;
            say "FAIL: $name under $mode — exit {$p.exitcode}{$p.signal ?? " signal {$p.signal}" !! ''}, out '$out'";
            note $err if $err;
        }
    }
}
say $fails ?? "FAIL ($fails)" !! 'PASS';
exit($fails ?? 1 !! 0);
