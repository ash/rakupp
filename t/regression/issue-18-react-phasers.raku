# Issue #18: LAST/QUIT phasers in react whenevers + deferred activation.
# Three behaviors, each matching Rakudo's reference output:
#   1. the react BODY runs before any whenever drains — a say at the body's
#      end prints before the first emitted value;
#   2. a LAST phaser fires when a whenever'd supply is exhausted, and it sees
#      the block's parameter as of the LAST invocation;
#   3. a die in a whenever body KILLS the react and propagates (it used to be
#      swallowed, leaving an interval ticker running forever) — and a QUIT
#      phaser does NOT catch it (QUIT is for the source's own quit; the
#      issue's Rakudo output confirms).
# Runs under both engines: programs go through $*EXECUTABLE.

my $work = $*TMPDIR.add("issue18-$*PID");
mkdir $work;

my $fails = 0;
sub check(Str $desc, Bool $ok) {
    if $ok {
        say "ok - $desc";
    }
    else {
        $fails++;
        say "not ok - $desc";
    }
}

sub run-prog(Str $name, Str $code) {
    my $f = $work.add($name);
    $f.spurt($code);
    my $p = run($*EXECUTABLE, $f.Str, :out, :err);
    my $out = $p.out.slurp(:close);
    $p.err.slurp(:close);
    ($out, $p.exitcode)
}

# 1 + 2: body-first ordering, LAST sees the last-bound parameter
my ($out1, $exit1) = run-prog('t1.raku', q:to/END/);
    react {
        whenever <a b c>.Supply -> $c {
            say $c;
            LAST { say "Done with $c" }
        }
        say "Starting...";
    }
    END
check('react body runs before the whenever drains, LAST sees $c',
      $out1 eq "Starting...\na\nb\nc\nDone with c\n" && $exit1 == 0);

# 3: a die in a whenever body kills the react (no hang) and QUIT stays quiet
my ($out2, $exit2) = run-prog('t2.raku', q:to/END/);
    react {
        whenever <a b c>.Supply -> $c {
            say $c;
            LAST { say "Done with $c" }
        }
        whenever Supply.interval(0.2) {
            die "something went wrong..";
            QUIT { default { say "oops: {.message}" }}
        }
        say "Starting...";
    }
    END
check('a die in a whenever body kills the react and propagates',
      $exit2 != 0);
check('the finished supply still ran its LAST before the die',
      $out2.contains("Done with c"));
check('QUIT does not catch a body die (reference behavior)',
      !$out2.contains("oops"));
check('the body say still leads the output',
      $out2.starts-with("Starting...\n"));

unlink $work.add($_) for <t1.raku t2.raku>;
say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
