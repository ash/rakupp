# Regression: a multi METHOD dispatched differently once compiled — and the
# compiled answer was wrong, not merely different.
#
# Codegen::classRegister emits a dispatcher whose guard sees POSITIONAL arity
# and nominal type and nothing else, so three things it cannot decide were
# being decided anyway:
#   · a REQUIRED named is invisible to the guard, so `multi method g(:$size!)`
#     matched a call that passed nothing and bound $size to Any;
#   · a `where` clause or a :D/:U smiley never enters the guard at all;
#   · a candidate declared in a PARENT class is unreachable, though in Rakudo a
#     multi's candidate set spans the MRO (the interpreter defers up the chain
#     — the parentNext branch in Interpreter::invokeMethod).
# `class K is P` with P's `multi method g(UInt:D $s = 1)` printed `k` compiled
# and `k1` interpreted. Codegen now refuses such a group — the same call
# multiDef() already made for multi SUBS — so the program falls back to AOT
# bundling and both faces agree again.
#
# Every case below asserts INTERPRETED == COMPILED. The last one also asserts
# the compile stayed "(native)": the bail must not quietly swallow the ordinary
# multi methods it can still decide.
# Contract: exit 0 + last line PASS.

my $v = run($*EXECUTABLE, '--version', :out, :err);
my $banner = $v.out.slurp(:close); $v.err.slurp(:close);
unless $banner.contains('rakupp') {
    # only rakupp has --exe; under Rakudo there is nothing this file can test
    note 'multi-method-compiled-dispatch: not rakupp, nothing to compile';
    say 'PASS';
    exit 0;
}

my $work = $*TMPDIR.add("multi-method-compiled-$*PID");
mkdir $work;
my @made;
my $fails = 0;

sub check(Str $desc, $got, $want) {
    if $got eq $want { say "ok - $desc" }
    else {
        $fails++;
        say "not ok - $desc";
        note "GOT [{$got}] WANT [{$want}]";
    }
}

# Run CODE interpreted and again as a native/AOT binary; check both against
# WANT (so the two agree with each other and with Rakudo's answer).
sub agree(Str $name, Str $code, Str $want, Bool :$native = False) {
    my $src = $work.add($name ~ '.raku');
    $src.spurt($code);
    my $bin = $work.add($name);
    @made.push($src.Str, $bin.Str);

    my $i = run($*EXECUTABLE, $src.Str, :out, :err);
    my $interp = $i.out.slurp(:close).lines.join('|');
    $i.err.slurp(:close);

    my $c = run($*EXECUTABLE, '--exe', '-o', $bin.Str, $src.Str, :out, :err);
    my $cout = $c.out.slurp(:close) ~ $c.err.slurp(:close);
    if $c.exitcode != 0 {
        $fails++;
        note "compile of $name failed:\n$cout";
        return;
    }
    if $native && !$cout.contains('(native)') {
        $fails++;
        note "$name was expected to compile natively, but did not:\n$cout";
    }
    my $r = run($bin.Str, :out, :err);
    my $compiled = $r.out.slurp(:close).lines.join('|');
    $r.err.slurp(:close);

    check("$name: interpreted",           $interp,   $want);
    check("$name: compiled agrees",       $compiled, $interp);
}

# 1. A required named. The old guard matched the FIRST candidate on positional
#    arity alone (0 in, 0 required) and bound $size to Any → "named-".
agree('required-named', q:to/END/, 'none|named-7');
    class K {
        multi method g(:$size!) { "named-$size" }
        multi method g()        { 'none' }
    }
    say K.new.g;
    say K.new.g(:size(7));
    END

# 2. A candidate that only the PARENT declares. Compiled, K's dispatcher threw
#    X::Multi::NoMatch instead of deferring to P.
agree('parent-candidate', q:to/END/, 'px|k3');
    class P { multi method g($s)    { "p$s" } }
    class K is P { multi method g(Int $i) { "k$i" } }
    say K.new.g('x');
    say K.new.g(3);
    END

# 3. The original report: :D smileys, a named, and the candidate split across
#    two classes all at once. Compiled printed "k" — $size bound to nothing.
agree('smiley-named-inherited', q:to/END/, 'k1|k3|k4');
    class P {
        multi method g(UInt:D $s = 1)     { self.g(:size($s)) }
        multi method g(UInt:D :$size = 1) { !!! }
    }
    class K is P {
        multi method g(UInt:D :$size) { "k$size" }
    }
    say K.new.g;
    say K.new.g(3);
    say K.new.g(:size(4));
    END

# 4. …and the guard still decides what it CAN decide: plain positional
#    candidates on a parentless class stay natively compiled.
agree('plain-positionals', q:to/END/, 'int 1|str a|none', :native);
    class C {
        multi method f(Int $x) { "int $x" }
        multi method f(Str $s) { "str $s" }
        multi method f()       { 'none' }
    }
    say C.new.f(1);
    say C.new.f('a');
    say C.new.f;
    END

unlink $_ for @made;
try rmdir $work;
say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
