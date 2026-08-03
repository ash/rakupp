# Two gaps found while turning showcase/rakus into an installable distribution.
#
#   * IO::Handle.flush did not exist. These handles buffer in memory until
#     .close, so a program that flushes deliberately — a log or a trace file
#     something else reads WHILE it runs — saw nothing until exit, and calling
#     .flush died outright. Flushing now writes what has been buffered and
#     remembers that it did, so the close (or the exit flush) appends the rest
#     instead of truncating away what the flush was for.
#
#   * &MAIN's generated usage differed from Rakudo's in four ways at once: the
#     options were not listed before the positionals, the routine's `#|` was
#     not used as the candidate's description, a Str default was printed
#     unquoted, and a one-character option printed as `--x` instead of `-x`.
#     A fifth was a real leak: on a ONE-LINE signature the routine's own `#|`
#     was also "above" its parameters, so `#| Another way.` on
#     `multi MAIN('go', Int $n)` came out as the help text for `<n>`.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what:\n     got  {$got.raku}\n     want {$want.raku}") unless $got eqv $want
}

# ---- IO::Handle.flush ------------------------------------------------------
my $dir = $*TMPDIR.add("rakupp-flush-{$*PID}");
$dir.mkdir;
LEAVE { for dir($dir).map(*.IO) { unlink $_ }; rmdir $dir }

my $a = $dir.add('a.txt');
my $h = $a.open(:w);
$h.print("one ");
$h.flush;
check($a.slurp, 'one ', 'flush puts the buffer on disk before close');
$h.print("two");
$h.close;
check($a.slurp, 'one two', 'and close appends the rest exactly once');

my $b = $dir.add('b.txt');
my $h2 = $b.open(:w);
$h2.print("a"); $h2.flush;
$h2.print("b"); $h2.flush;
$h2.print("c"); $h2.close;
check($b.slurp, 'abc', 'repeated flushes do not duplicate or truncate');

my $c = $dir.add('c.txt');
$c.spurt("old ");
my $h3 = $c.open(:a);
$h3.print("new"); $h3.flush; $h3.close;
check($c.slurp, 'old new', 'append mode keeps what was already there');

my $d = $dir.add('d.txt');
my $h4 = $d.open(:w);
$h4.flush;                       # nothing written yet
$h4.print("late"); $h4.close;
check($d.slurp, 'late', 'flushing an empty buffer loses nothing');

check((try { $*OUT.flush; 'ok' }) // 'died', 'ok', '$*OUT.flush works');
check((try { $*ERR.flush; 'ok' }) // 'died', 'ok', '$*ERR.flush works');

# ---- &MAIN usage -----------------------------------------------------------
my $prog = $dir.add('prog.raku');
$prog.spurt: q:to/PROG/;
    #| Do the thing.
    multi MAIN(
        Str $a,           #= first positional
        Int $b = 7,       #= second positional
        :$x = 'dflt',     #= a string option
        Bool :$flag,      #= a boolean
    ) { }

    #| Another way.
    multi MAIN('go', Int $n) { }
    PROG

my $proc = run($*EXECUTABLE, $prog.absolute, '--help', :out, :err);
my $usage = $proc.out.slurp(:close) ~ $proc.err.slurp(:close);

# Rakudo's exact text, line for line.
# The line begins with the path the interpreter was given, so match the tail.
check($usage.lines[1].trim.ends-with(
        "prog.raku [-x[=Any]] [--flag] <a> [<b>] -- Do the thing."), True,
      'options first, then positionals, then the routine description');
check($usage.lines[2].trim.ends-with("prog.raku go <n> -- Another way."), True,
      'each candidate carries its own description');
check($usage.contains("[default: 'dflt']"), True, 'a Str default is quoted');
check($usage.contains('[default: 7]'),      True, 'a number default is not');
check($usage.contains('-x[=Any]'),          True, 'a one-character option is short');

# the leak: a candidate's description must not become a parameter's
check($usage.contains('<n>         Another way.'), False,
      "a candidate's #| does not leak onto its first parameter");
check($usage.lines.grep(*.contains('Another way.')).elems, 1,
      'and appears exactly once in the whole usage');

if @fail {
    .say for @fail;
    say 'FAIL';
    exit 1;
}
say 'PASS';
