# Regression (#258): an `:in` that is not a handle is what Rakudo's Proc
# makes of it: True is a pipe the program writes, the Str '-' inherits our
# stdin, and anything else — False, a Str (a file name too: it is not a
# handle), a number, Any, Nil — closes the child's stdin, so a child that reads
# it meets EOF at once.
#
# Every truthy value opened the pipe here and nothing closed it, so
# `run(:in("x"), "cat", :out)` deadlocked: cat waited for EOF, and
# `.out.slurp(:close)` waited for cat. `:in(False)` handed the child our own
# stdin, and `:in('-')` hung like "x". `shell` read only a handle: `:in` gave
# it no pipe (the child read our stdin, and `.in.print` went nowhere).
#
# The child gives up on its stdin by itself after a while, so the old
# behaviour is a failure here and not a hung suite.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

# the child: how many characters stdin held, or "open" if it never closed
my $CHILD = 'my $p = start $*IN.slurp; await Promise.anyof($p, Promise.in(8)); '
          ~ 'print $p.status ~~ Kept ?? "eof:" ~ $p.result.chars !! "open"; exit 0';
my @cmd = $*EXECUTABLE, '-e', $CHILD;

for "x", "/no/such/file", False, 42, Any, Nil -> $in {
    check run(|@cmd, :$in, :out).out.slurp(:close), 'eof:0', ":in({$in.raku}) closes the child's stdin";
}
{
    my $p = run(|@cmd, :in, :out);
    $p.in.print("abc");
    $p.in.close;
    check $p.out.slurp(:close), 'eof:3', ':in is a pipe the program writes';
}
{
    my $p = run($*EXECUTABLE, '-e', 'print run(|@*ARGS, :in<->, :out).out.slurp(:close)', |@cmd, :in, :out);
    $p.in.print("inherited");
    $p.in.close;
    check $p.out.slurp(:close), 'eof:9', ":in('-') inherits our stdin";
}
my $sh = @cmd.map({ "'" ~ .subst("'", q['"'"'], :g) ~ "'" }).join(' ');
unless $*DISTRO.is-win {
    check shell($sh, :in("x"), :out).out.slurp(:close), 'eof:0', 'shell :in("x") closes it too';
    my $p = shell($sh, :in, :out);
    $p.in.print("ab");
    $p.in.close;
    check $p.out.slurp(:close), 'eof:2', 'shell :in is a pipe';
}

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
