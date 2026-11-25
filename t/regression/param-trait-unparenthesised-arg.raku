# Regression: a parameter trait's argument without parentheses, and a module's
# ARGS-TO-CAPTURE for the auto-invoked MAIN (#284).
#
# `sub MAIN(Bool :$v is option<!>)` is how App::Prove6 declares its Getopt::Long
# options, and the parser read only `is option(…)`: `<!>` was a parse error, so
# `prove6` could not start. A parameter now takes the argument forms an
# attribute already took — `<…>` (also `<=s>`, which lexes as `<=`), `{…}` and
# `[…]` — as Rakudo does.
#
# Behind it, Getopt::Long's exported ARGS-TO-CAPTURE was never asked: the
# auto-invoked MAIN parsed @*ARGS itself, so `--no-v` printed the usage where
# Rakudo answers False. Only an explicit RUN-MAIN called the hook. Both do now.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}
my @seen;
multi trait_mod:<is>(Parameter $p, :$opt!) { @seen.push: $opt }
sub a($x is opt<!>)        { }
sub b($x is opt<=s>)       { }
sub c($x is opt<a b>)      { }
sub d($x is opt{ :k(1) })  { }
sub e($x is opt[1, 2])     { }
sub f($x is opt("z"))      { }
sub g(Bool :$v is opt<!>, :$q is opt<=s>) { }
check @seen[0], '!', 'is opt<!>';
check @seen[1], '=s', 'is opt<=s>';
check @seen[2], ("a", "b"), 'is opt<a b>';
check @seen[3], {k => 1}, 'is opt{ :k(1) }';
check @seen[4], [1, 2], 'is opt[1, 2]';
check @seen[5], 'z', 'is opt("z")';
check @seen[6 .. 7].List, ('!', '=s'), 'named parameters';
sub h($x is copy, $y is rw) { $x = 5; $y = $x }
my $yy; h(1, $yy);
check $yy, 5, 'is copy / is rw unchanged';

# the auto-invoked MAIN asks a user ARGS-TO-CAPTURE
my $dir = $*TMPDIR.add("a2c-{$*PID}");
$dir.mkdir;
my $prog = $dir.add('prog.raku');
$prog.spurt: q:to/END/;
    sub ARGS-TO-CAPTURE(&main, @args) {
        note "hook: @args[]";
        \(:v(@args[0] ne '--no-v'))
    }
    sub MAIN(Bool :$v) { say $v ?? 'verbose' !! 'quiet' }
    END
for '--v', 'verbose', '--no-v', 'quiet' -> $arg, $want {
    my $p = run($*EXECUTABLE, ~$prog, $arg, :out, :err);
    check $p.out.slurp(:close).trim, $want, "MAIN $arg";
    check $p.err.slurp(:close).trim, "hook: $arg", "the hook ran for $arg";
}
$prog.unlink; $dir.rmdir;

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
