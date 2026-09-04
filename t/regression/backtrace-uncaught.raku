# Regression: an uncaught error printed its MESSAGE and nothing else — no file,
# no line, no call chain (issue #67). Every `throw RakuError{…}` in the tree now
# captures the live chain in the constructor, because C++ unwinding pops the
# frames before any catch runs, and the top-level printer renders it.
#
# What this asserts is the SHAPE consumers parse: the message stays line 1, and
# the frames below it are Rakudo's `  in <kind> <name> at FILE line N`.
# Contract: exit 0 + last line PASS.
my @fail;

my $dir = $*TMPDIR.add("rakupp-bt-{$*PID}");
$dir.mkdir;
my $prog = $dir.add('boom.raku');
$prog.spurt(q:to/END/);
class Foo {
    method bar($x) { self.baz($x) }
    method baz($x) { die "boom $x" }
}
sub helper($n) { Foo.new.bar($n) }
helper(2);
END

sub runit(*@args) {
    my $p = run($*EXECUTABLE.absolute, |@args, :out, :err);
    my $o = $p.out.slurp(:close); my $e = $p.err.slurp(:close);
    ($o, $e, $p.exitcode)
}

my ($out, $err, $rc) = runit($prog.absolute);
@fail.push("exit code $rc, wanted 1") unless $rc == 1;
my @lines = $err.lines;
@fail.push("message is line 1, got '{@lines[0] // ''}'") unless (@lines[0] // '') eq 'boom 2';
# the whole chain, innermost first, each naming its routine KIND
# a method frame carries its DECLARING class, which a bare `in method new`
# does not: three classes with a `new` each are otherwise indistinguishable
@fail.push('method baz frame')  unless $err ~~ /'in method Foo::baz at ' .*? 'boom.raku line 3'/;
@fail.push('method bar frame')  unless $err ~~ /'in method Foo::bar at ' .*? 'boom.raku line 2'/;
@fail.push('sub helper frame')  unless $err ~~ /'in sub helper at ' .*? 'boom.raku line 5'/;
@fail.push('mainline frame')    unless $err ~~ /'in block <unit> at ' .*? 'boom.raku line 6'/;
# …in that order (innermost first, as Rakudo prints it)
my $ib = $err.index('in method Foo::baz'); my $iu = $err.index('in block <unit>');
@fail.push('innermost first') unless $ib.defined && $iu.defined && $ib < $iu;
# nothing on stdout: a backtrace is a diagnostic
@fail.push("stdout must stay empty, got '$out'") unless $out eq '';

# a BUILTIN error (thrown from C++, not from `die`) carries the chain too
my $p2 = $dir.add('nomethod.raku');
$p2.spurt(q:to/END/);
sub g($x) { $x.nonexistent-method }
g(42);
END
my ($o2, $e2, $rc2) = runit($p2.absolute);
@fail.push('builtin error names its sub')
    unless $e2 ~~ /'in sub g at ' .*? 'nomethod.raku line 1'/;
@fail.push('builtin error names the mainline')
    unless $e2 ~~ /'in block <unit> at ' .*? 'nomethod.raku line 2'/;
# …and the exception TYPE, which is what a reader needs to write a CATCH
@fail.push('type line for a typed exception') unless $e2 ~~ /'(X::Method::NotFound)'/;

# a frame from a MODULE names the module's file, not the program's
my $mod = $dir.add('BtMod.rakumod');
$mod.spurt(q:to/END/);
unit module BtMod;
sub deep($x) is export { die "deep $x" }
END
my $p3 = $dir.add('usemod.raku');
$p3.spurt(qq:to/END/);
use BtMod;
deep(7);
END
my ($o3, $e3, $rc3) = runit('-I', $dir.absolute, $p3.absolute);
@fail.push("module frame names the module, got:\n$e3")
    unless $e3 ~~ /'in sub deep at ' .*? 'BtMod.rakumod'/;
@fail.push('module frame keeps the caller') unless $e3 ~~ /'usemod.raku line 2'/;

# RAKUPP_BACKTRACE=0 is the escape hatch: the message alone, as before
my %env = %*ENV.clone; %env<RAKUPP_BACKTRACE> = '0';
my $p4 = run($*EXECUTABLE.absolute, $prog.absolute, :out, :err, :%env);
my $e4 = $p4.err.slurp(:close); $p4.out.slurp(:close);
@fail.push("RAKUPP_BACKTRACE=0 prints the message alone, got:\n$e4")
    unless $e4.trim eq 'boom 2';

# deep recursion collapses instead of filling the terminal…
my $p5 = $dir.add('deep.raku');
$p5.spurt(q:to/END/);
sub r($n) { $n == 0 ?? die 'bottom' !! r($n - 1) }
r(300);
END
my ($o5, $e5, $rc5) = runit($p5.absolute);
@fail.push("300 frames must collapse, got {$e5.lines.elems} lines")
    unless $e5.lines.elems < 20;
@fail.push('…and say how many it folded') unless $e5 ~~ /'more frames'/;
# …unless asked for everything
my ($o6, $e6, $rc6) = runit('--ll-exception', $p5.absolute);
@fail.push("--ll-exception shows them all, got {$e6.lines.elems} lines")
    unless $e6.lines.elems > 250;

unlink($_) for $prog, $p2, $p5, $mod, $p3;
rmdir($dir);

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' }
else     { say 'PASS' }
