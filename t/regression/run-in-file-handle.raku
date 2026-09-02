# Regression: `run`/`shell` with `:in($handle)` — a file handle as the child's
# standard input. Rakudo hands the child the handle's own descriptor, so a child
# that INSPECTS its stdin sees the plain file: macOS `script` does a tcgetattr
# on it (Roast's Test::Util run-with-tty passes a file handle for exactly that
# reason — S32-io/out-buffering.t's "prompt does not hang"), and `test -f
# /dev/stdin` holds. rakupp read any `:in` value as the Bool that selects the
# deferred piped mode (`run`) or ignored it (`shell`), so the child inherited
# OUR stdin: under a harness whose stdin is a pipe or a socket the input was
# lost or `script` died in tcgetattr, and the file flapped between skip and
# fail in every Roast sweep.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

my $p = $*TMPDIR.add("run-in-$*PID.txt");
$p.spurt: "bar\nbaz\n";
sub reading(&code) { my $fh = $p.open(:r); my $r = code($fh); $fh.close; $r }

check(reading({ run('cat', :in($_), :out).out.slurp(:close) }), "bar\nbaz\n", 'run :in($fh) feeds the file to the child');
check(reading({ run('cat', :in($_), :out).exitcode }), '0', 'and the child exits 0');
check(reading({ shell('cat', :in($_), :out).out.slurp(:close) }), "bar\nbaz\n", 'shell :in($fh) too');
check(reading({ run('sh', '-c', 'test -f /dev/stdin && echo file || echo notfile', :in($_), :out).out.slurp(:close) }),
      "file\n", 'the child sees a PLAIN FILE on its stdin, not a pipe or a socket');
check(reading({ my $pr = run 'sh', '-c', 'echo E >&2; cat', :in($_), :out, :err;
                $pr.out.slurp(:close) ~ '|' ~ $pr.err.slurp(:close) }),
      "bar\nbaz\n|E\n", ':out and :err capture alongside :in($fh)');
# a captured stream of one Proc as the input of the next
my $first = run 'sh', '-c', 'printf "one\ntwo\n"', :out;
check(run('cat', :in($first.out), :out).out.slurp(:close), "one\ntwo\n", 'a Proc.out handle feeds the next child');
# the piped mode is untouched
my $piped = run 'cat', :in, :out;
$piped.in.print("fed\n"); $piped.in.close;
check($piped.out.slurp(:close), "fed\n", ':in (a Bool) still opens the piped stdin');
unlink $p;

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
