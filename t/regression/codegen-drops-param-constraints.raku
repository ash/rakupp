# A `where` or a `:D`/`:U` smiley on an ORDINARY routine was never emitted by the
# C++ backend. The guard for exactly this existed but sat only on the two
# multi-candidate paths (undecidableMulti, and the multi dispatcher), so a plain
# sub compiled NATIVELY with its constraint silently absent:
#
#     sub f(Int $n where * > 0) { "got $n" }; f(-1)
#
# ran the body and answered "got -1", where the interpreter refuses it. A counter
# inside the constraint proved it was never evaluated at all rather than
# evaluated and ignored — the check was not compiled, not merely mis-compiled.
# `sub f(Int:D $n)` called with the type object `Int` bound it just as happily.
#
# That is a wrong answer rather than a slow one, it arrives with no note and no
# error, and it lands on the shape a program uses to VALIDATE its input — so a
# program lost its validation by being compiled. It also broke the invariant the
# three execution modes are supposed to hold: interpreter, --exe and browser
# agree. Codegen now declines these signatures and bundles the interpreter, the
# way the multi paths already did.
#
# Guarded here: every constraint form answers the same compiled as interpreted.
# Emitting the check instead of falling back is the follow-up; this file only
# asserts they AGREE, so it keeps passing when that lands.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

my $bin = $*EXECUTABLE.absolute;
my $dir = $*TMPDIR.add("rakupp-constraint-{$*PID}");
$dir.mkdir;
LEAVE { try { .unlink for $dir.dir; $dir.rmdir } }

#| Run $src interpreted and as an --exe binary; return (interpreted, compiled).
sub both(Str $src, Str $name) {
    my $f = $dir.add("$name.raku");
    $f.spurt($src);
    my $interp = run($bin, $f.absolute, :out, :err);
    my $iout = $interp.out.slurp(:close).trim; $interp.err.slurp(:close);
    my $exe = $dir.add("$name.bin");
    my $c = run($bin, '--exe', $f.absolute, '-o', $exe.absolute, :out, :err);
    $c.out.slurp(:close); $c.err.slurp(:close);
    return ($iout, 'DID-NOT-COMPILE') unless $exe.e;
    my $r = run($exe.absolute, :out, :err);
    my $cout = $r.out.slurp(:close).trim; $r.err.slurp(:close);
    ($iout, $cout)
}

# Each case: the constraint must REFUSE the argument, both ways.
my @cases =
    ('block',   'sub f(Int $n where { $_ > 0 }) { "got $n" }; say (try f(-1)) // "REJECTED";'),
    ('whatever','sub f(Int $n where * > 0) { "got $n" }; say (try f(-1)) // "REJECTED";'),
    ('junction','sub f(Int $n where 1|3|5) { "got $n" }; say (try f(2)) // "REJECTED";'),
    ('typeobj', 'sub f(Int $n where Int) { "got $n" }; say (try f(2)) // "got 2";'),
    ('smiley-D','sub f(Int:D $n) { "got $n" }; say (try f(Int)) // "REJECTED";'),
    ('smiley-U','sub f(Int:U $n) { "got it" }; say (try f(42)) // "REJECTED";'),
    ('method',  'class K { method m(Int $n where * > 0) { "got $n" } }; say (try K.new.m(-1)) // "REJECTED";'),
    ('block-par','my $f = -> Int $n where * > 0 { "got $n" }; say (try $f(-1)) // "REJECTED";');

for @cases -> ($name, $src) {
    my ($i, $c) = both($src, $name);
    check($c, $i, "$name: compiled agrees with interpreted (interpreted said '$i')");
}

# …and the constraint must still ACCEPT what it should, so the guard is not
# just refusing everything.
my @ok =
    ('block-ok',   'sub f(Int $n where { $_ > 0 }) { "got $n" }; say f(3);',           'got 3'),
    ('junction-ok','sub f(Int $n where 1|3|5) { "got $n" }; say f(3);',                'got 3'),
    ('smiley-D-ok','sub f(Int:D $n) { "got $n" }; say f(7);',                          'got 7');

for @ok -> ($name, $src, $want) {
    my ($i, $c) = both($src, $name);
    check($i, $want, "$name: interpreted accepts");
    check($c, $want, "$name: compiled accepts");
}

if @fail { note "FAIL:\n" ~ @fail.join("\n"); exit 1 }
say 'PASS';
