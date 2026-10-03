# Run every use case here and compare what it prints with expected/NAME.out.
#
#     rakupp check.raku                 # every use case, under rakupp
#     rakupp check.raku forms           # just the ones whose names match
#     rakudo check.raku                 # the same, under Rakudo
#     rakupp check.raku --engine=PATH   # under another binary
#
# A use case that runs longer than --timeout seconds (default 60) is stopped
# and counts as a failure. The exit code is the number of failures.

sub MAIN(*@only, Str :$engine = ~$*EXECUTABLE, Int :$timeout = 60) {
    my $here = $*PROGRAM.parent;
    my @cases = $here.dir(test => /'.raku' $/).grep(*.basename ne 'check.raku').sort;
    @cases .= grep(-> $c { @only.first({ $c.basename.contains($_) }) }) if @only;
    my $failed = 0;
    for @cases -> $case {
        my $name = $case.basename.subst(/'.raku' $/, '');
        my $expected-file = $here.add("expected/$name.out");
        my ($out, $status) = run-case($engine, $case, $timeout);
        my $want = $expected-file.e ?? $expected-file.slurp !! '';
        if $status eq 'ok' && $out eq $want {
            say "PASS  $name";
            next;
        }
        $failed++;
        say "FAIL  $name" ~ ($status eq 'ok' ?? '' !! " ($status)");
        my @got = $out.lines;
        my @exp = $want.lines;
        for ^max(@got, @exp) -> $i {
            next if (@got[$i] // '') eq (@exp[$i] // '');
            say "      line {$i + 1}:";
            say "        expected: {@exp[$i] // '(nothing)'}";
            say "        got:      {@got[$i] // '(nothing)'}";
            last;
        }
    }
    say "{@cases - $failed} of {+@cases} use cases print what they should";
    exit $failed;
}

# Run one use case; its stdout, and 'ok', 'timed out' or 'exit N'.
sub run-case($engine, $case, $timeout) {
    my $proc = Proc::Async.new($engine, ~$case);
    my $out = '';
    $proc.stdout.tap(-> $s { $out ~= $s });
    $proc.stderr.tap(-> $ { });          # Cro logs a handler's death here; only stdout is compared
    my $done = $proc.start(cwd => $case.parent);
    my $timer = Promise.in($timeout);
    await Promise.anyof($done, $timer);
    unless $done {
        $proc.kill(SIGKILL);
        try await $done;
        return $out, 'timed out';
    }
    my $code = $done.result.exitcode;
    return $out, $code == 0 ?? 'ok' !! "exit $code";
}
