# Regression: standard input is read a block at a time (review ledger O24).
#
# Every line reader of $*IN — `$*IN.lines`, `$*IN.get`, `lines()` (which `-n`
# and `-p` loop over), `prompt` — was `std::getline(std::cin, …)`, and the
# slurps were `ss << std::cin.rdbuf()`. std::cin keeps no buffer of its own
# (it is synced with C stdio, and libc++ ignores sync_with_stdio(false)), so
# each character was a virtual call down to getc: 400k lines took 40G
# instructions and 4.5 s through `$*IN.lines` against 4.6G for `.IO.lines` of
# the same file, and `$*IN.slurp` 57G. They now read the same stdio FILE with
# getline(3) and fread. What that must not change is checked here: the lines
# themselves, a NUL or a CR inside one, the last line without a newline, the
# rest of the stream left for `.read`/`.slurp`, and std::getline's flush of
# $*OUT before a read that waits — a program that raised `.out-buffer` still
# shows its answer before it blocks on the next line of input.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

my $exe = $*EXECUTABLE.absolute;
my $in  = $*TMPDIR.add("stdin-block-$*PID.txt");
sub child(Str $input, Str $code, *@opt) {
    $in.spurt($input, :bin) if $input.defined;
    my $fh = $in.open(:r);
    my $p  = run $exe, |@opt, '-e', $code, :in($fh), :out, :err;
    my $out = $p.out.slurp(:close); $p.err.slurp(:close); $fh.close;
    $out
}

# the lines, whichever reader takes them
check child("a\nb\r\nc", Q[.raku.say for $*IN.lines]), qq["a"\n"b"\n"c"\n], '$*IN.lines: CRLF folded, last line kept';
check child("a\nb\r\nc", Q[.raku.say for lines()]),    qq["a"\n"b"\n"c"\n], 'lines() the same';
check child("a\nb\n", Q[.=uc], '-p'),                   "A\nB\n",            '-p';
check child("x\x[0]y\nz\n", Q[say $*IN.get.chars; say $*IN.get]), "3\nz\n", 'a NUL inside a line';
check child("a\nb", Q[$*IN.chomp = False; .raku.say for $*IN.get, $*IN.get, $*IN.get; say $*IN.eof]),
      qq["a\\n"\n"b"\nNil\nTrue\n], ':!chomp: the newline, none on the last line, then Nil';
check child("", Q[say $*IN.get.raku; say lines().elems; say $*IN.eof]), "Nil\n0\nTrue\n", 'empty input';
check child("e\x[301]x\n", Q[say $*IN.get.chars]), "2\n", 'still NFC';

# what a line read leaves behind is the next reader's
check child("one\nXYZ", Q[say $*IN.get; say $*IN.read(3)]), "one\nBuf[uint8]:0x<58 59 5A>\n", '.get then .read';
check child("one\ntwo\nthree\n", Q[say $*IN.get; print $*IN.slurp]), "one\ntwo\nthree\n", '.get then .slurp';
check child("a\nb\nc\n", Q[say $*IN.lines(1); say $*IN.get; say lines()]), "(a)\nb\n(c)\n", 'a limit, then .get, then lines()';
check child("x y\nz\n", Q[say $*IN.get; say words()]), "x y\n(z)\n", '.get then words()';
check child("42\n", Q[my $x = prompt; say $x + 1]), "43\n", 'prompt';

# std::getline flushed std::cout before each read; a read that may wait still
# does, so the answer to the first line is out while stdin is still open
{
    my $proc = Proc::Async.new($exe, '-e', Q[$*OUT.out-buffer = 65536; for lines() { say "got $_" }], :w);
    my @got;
    my $first = Promise.new;
    $proc.stdout.lines.tap: { @got.push($_); $first.keep(True) if $first.status ~~ Planned };
    my $done = $proc.start;
    await $proc.print("one\n");
    await Promise.anyof($first, Promise.in(10));
    check $first.status ~~ Kept, True, 'the first answer is out before stdin closes';
    await $proc.print("two\n");
    $proc.close-stdin;
    await Promise.anyof($done, Promise.in(10));
    check @got, ['got one', 'got two'], 'both answers';
}

# and it is not a character at a time any more: the same lines through stdin
# and through .IO.lines cost about the same (the old reader was ~10x)
unless $*DISTRO.is-win {
    my $big = $*TMPDIR.add("stdin-block-big-$*PID.txt");
    $big.spurt: (^150_000).map({ "line $_ of a file read through standard input" }).join("\n") ~ "\n";
    sub took(*@cmd) { my $t0 = now; run(|@cmd, :out).out.slurp(:close); now - $t0 }
    my $file  = min (^2).map: { took $exe, '-e', "my \$n = 0; \$n += .chars for '$big'.IO.lines; say \$n" };
    my $stdin = min (^2).map: { took 'sh', '-c', "'$exe' -e 'my \$n = 0; \$n += .chars for \$*IN.lines; say \$n' < '$big'" };
    check $stdin < 3 * $file, True, "\$*IN.lines within 3x of .IO.lines ({$stdin.round(0.01)} s against {$file.round(0.01)} s)";
    $big.unlink;
}
$in.unlink;

if @fail { note "FAILED:\n  " ~ @fail.join("\n  "); say 'FAIL' } else { say 'PASS' }
