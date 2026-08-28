# Regression: what TAP::Harness needs from Proc::Async streams (2026-08-28).
# Four related gaps, found writing raku.online's TAP module page — each made
# the harness report NOTESTS for every test file. Every check Rakudo-verified.
#
# 1. A plain `.stdout` tap got ONE Blob-kinded chunk; Rakudo emits decoded
#    Strs (`.stdout(:bin)` is the byte path). 2. `.lines(:!chomp)` chomped
#    anyway — TAP's grammar needs the trailing "\n" to close each entry.
# 3. `whenever $proc.stdout.lines` INSIDE a `supply {…}` block wired nothing
#    (tapSupply had no proc-stream case), so the child ran uncaptured.
# 4. `$start.then({…})` fired AT REGISTRATION with the still-Planned start,
#    and `$start.result` answered the Proc::Async itself where Rakudo hands a
#    finished Proc (TAP's `multi method new(Proc $proc)` needs the type, and
#    `.signal` must exist on it).

my $ok = True;
sub ck($got, $want, $l) { unless $got eqv $want { say "FAIL: $l — {$got.raku} vs {$want.raku}"; $ok = False } }

# 1. decoded Str chunks by default; Blob only under :bin
{
    my $a = Proc::Async.new('printf', "a\nb\n");
    my @kinds; my $data = '';
    $a.stdout.tap({ @kinds.push(.^name); $data ~= $_ });
    await $a.start;
    sleep 0.2; # Rakudo delivers tap callbacks on a worker; give it a beat
    ck(@kinds.unique.join('|'), 'Str', 'plain .stdout taps emit Str chunks');
    ck($data, "a\nb\n", 'the decoded chunk carries the full output');
}
{
    my $a = Proc::Async.new('printf', 'xy');
    my $b = Buf.new;
    my $done = False;
    $a.stdout(:bin).tap({ $b.append($_) }, :done({ $done = True }));
    await $a.start;
    ck($b.decode, 'xy', ':bin taps still get appendable bytes');
    ck($done, True, 'the :done callback fires when the stream closes');
}

# 2. .lines honours :!chomp (and still chomps by default)
{
    my $a = Proc::Async.new('printf', "a\nb\n");
    my @got; $a.stdout.lines(:!chomp).tap({ @got.push($_) });
    await $a.start;
    sleep 0.2;
    ck(@got.join('|'), "a\n|b\n", '.lines(:!chomp) keeps each terminator');
}
{
    my $a = Proc::Async.new('printf', "a\r\nb\n");
    my @got; $a.stdout.lines.tap({ @got.push($_) });
    await $a.start;
    sleep 0.2;
    ck(@got.join('|'), 'a|b', '.lines still chomps by default, CRLF too');
}

# 3. whenever over a proc stream inside a supply block (TAP's parse-stream shape)
{
    my $a = Proc::Async.new('printf', "one\ntwo\n");
    my $s = supply { whenever $a.stdout.lines(:!chomp) -> $l { emit $l } };
    my @got; $s.tap({ @got.push($_) });
    await $a.start;
    sleep 0.2; # Rakudo delivers these on a worker; give it a beat
    ck(@got.join('|'), "one\n|two\n", 'supply-wrapped whenever taps the proc stream');
}

# 4. .then waits for the process; .result is a finished Proc with .signal
{
    my $a = Proc::Async.new($*EXECUTABLE.absolute, '-e', 'exit 3');
    $a.stdout.tap({;});
    my $start = $a.start;
    my $p = $start.then({ my $r = $start.result; ($r.^name, $r.exitcode, $r.signal) });
    my ($name, $code, $sig) = (await $p).list;
    ck($name, 'Proc', '.result answers a Proc, as Rakudo does');
    ck($code, 3, '…whose exitcode is the real exit status');
    ck($sig, 0, '…and whose .signal exists (0 for a normal exit)');
}

say $ok ?? 'PASS' !! 'FAIL';
exit($ok ?? 0 !! 1);
