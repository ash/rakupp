# MAIN usage generation — GitHub issue #17, plus what reproducing it found.
# Each case writes a MAIN script, runs it under $*EXECUTABLE, and compares the
# usage text BYTE-FOR-BYTE (stream and exit code included) against Rakudo's
# behaviour, oracle-captured 2026-08-07 (Rakudo 2026.07):
#   1. a parameter's trailing `#= doc` belongs to the PARAMETER — with no `#|`
#      on the sub there is no ` -- …` suffix on the usage line (the bug glued
#      every param doc into it);
#   2. `is required` on a named param prints WITHOUT the optionality brackets
#      (and the `!` marker spelling agrees);
#   3. a `#=` on the line where the signature CLOSES sits after the `)` — it
#      documents the ROUTINE (usage suffix, no option entry);
#   4. an explicit --help prints the usage to STDOUT and exits 0; a failed
#      dispatch (bare -h included) prints to STDERR and exits 2.
# Passes under both rakupp and Rakudo: what it asserts is exactly the
# behaviour the two engines share.

my $work = $*TMPDIR.add("main-usage-17-$*PID");
mkdir $work;

my $fails = 0;
sub check(Str $desc, $got, $want) {
    if $got eq $want {
        say "ok - $desc";
    }
    else {
        $fails++;
        say "not ok - $desc";
        note "GOT:\n{$got}<<<\nWANT:\n{$want}<<<";
    }
}

# run a MAIN script; returns (stdout, stderr, exit)
sub run-main(Str $name, Str $code, *@args) {
    my $f = $work.add($name);
    $f.spurt($code);
    my $p = run($*EXECUTABLE, $f.Str, |@args, :out, :err);
    ($f.Str, $p.out.slurp(:close), $p.err.slurp(:close), $p.exitcode)
}

# --- 1 + 2: the issue #17 example, verbatim ------------------------------
my $issue = q:to/END/;
    sub MAIN(
        Str :$foo is required, #= some stuff
        Bool :$verbose = False, #= verbose mode
    ) { say "ok" }
    END
{
    my ($path, $out, $err, $exit) = run-main('t1.raku', $issue, '-h');
    my $want = ("Usage:",
                "  $path --foo=<Str> [--verbose]",
                "  ",
                "    --foo=<Str>    some stuff",
                "    --verbose      verbose mode [default: False]",
                "").join("\n");
    check('issue example: usage on stderr, no param-doc suffix, required unbracketed', $err, $want);
    check('issue example: nothing on stdout', $out, '');
    check('issue example: exit 2', $exit, 2);
}

# --- the sub's own #| IS the suffix --------------------------------------
{
    my ($path, $out, $err, $exit) =
        run-main('t2.raku', "#| frobnicate the widget\n" ~ $issue, '-h');
    my $want = ("Usage:",
                "  $path --foo=<Str> [--verbose] -- frobnicate the widget",
                "  ",
                "    --foo=<Str>    some stuff",
                "    --verbose      verbose mode [default: False]",
                "").join("\n");
    check('a #| on the sub becomes the -- suffix', $err, $want);
}

# --- 3: a #= after the closing paren documents the routine ----------------
{
    my ($path, $out, $err, $exit) =
        run-main('t3.raku', "sub MAIN(Int \$x) \{ say \"ok\" \}  #= one-line doc\n", '-h');
    my $want = ("Usage:",
                "  $path <x> -- one-line doc",
                "").join("\n");
    check('one-line #= documents the sub: suffix yes, option entry no', $err, $want);
}

# --- 2 again, the ! spelling + positionals/defaults/slurpy ----------------
{
    my ($path, $out, $err, $exit) =
        run-main('t4.raku', "sub MAIN(Int \$pos, Str \$opt = 'x', Str :\$a!, *\@rest) \{ say \"ok\" \}\n", '-h');
    my $want = ("Usage:",
                "  $path -a=<Str> <pos> [<opt>] [<rest> ...]",
                "").join("\n");
    check('! spelling unbracketed; positional/default/slurpy shapes', $err, $want);
}

# --- 4: --help is a request, not a failure --------------------------------
{
    my ($path, $out, $err, $exit) = run-main('t5.raku', $issue, '--help');
    my $want = ("Usage:",
                "  $path --foo=<Str> [--verbose]",
                "  ",
                "    --foo=<Str>    some stuff",
                "    --verbose      verbose mode [default: False]",
                "").join("\n");
    check('--help: usage on stdout', $out, $want);
    check('--help: stderr empty', $err, '');
    check('--help: exit 0', $exit, 0);
}

unlink $work.add($_) for <t1.raku t2.raku t3.raku t4.raku t5.raku>;
say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
