# Regression: a process started inside a `supply { … }` block ran with nobody
# reading its pipes — the output reached the taps only when something awaited
# the promise, which Cro's runner never does — and `whenever $proc.start` there
# fired at once with the Promise, not the finished Proc. `.start(:ENV(…))` also
# ignored the environment, so Cro's services never saw their HOST and PORT.
# Contract: exit 0 + last line PASS.
my @fail;

my $s = supply {
    my $proc = Proc::Async.new($*EXECUTABLE.absolute, '-e',
        'say "env={%*ENV<RAKUPP_PROBE_ENV> // "none"}"; note "to-err"; sleep 0.3; say "late"');
    whenever $proc.stdout.lines -> $line { emit "out:$line" }
    whenever $proc.stderr.lines -> $line { emit "err:$line" }
    my %env = %*ENV, RAKUPP_PROBE_ENV => 'given';
    whenever $proc.start(:ENV(%env)) -> $done { emit "exit:{$done.exitcode}"; done }
}
my @got;
react {
    whenever $s { @got.push($_); done if .starts-with("exit") }
    whenever Promise.in(10) { @got.push("timeout"); done }
}
@fail.push("no stdout line") unless @got.grep("out:env=given");
@fail.push("no late line") unless @got.grep("out:late");
@fail.push("no stderr line") unless @got.grep("err:to-err");
@fail.push("exit not last: {@got.raku}") unless @got.tail eq "exit:0";

say @fail ?? "FAIL: @fail.join('; ')" !! "PASS";
exit @fail ?? 1 !! 0;
